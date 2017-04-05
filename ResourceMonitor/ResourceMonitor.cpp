// ResourceMonitor.cpp : Defines main()
// Written by Kristofer Christakos
// Command line resource monitor. 
// Displays CPU, network, disk, RAM, and process bottleneck information.

#include <iostream>
#include <fstream>
#include <string>
#include <ctime>
#include <Pdh.h>//Link pdh.lib
#include <PdhMsg.h>
#include <Windows.h>
#include "PdhHelperFunctions.h"
#include "StringHelpers.h"

using namespace std;

const wchar_t USAGE_TEXT[] = L"\
RESOURCEMONITOR [/T seconds] [/L logfile]\
\
  /T\tIndicates the time delay between data collection is given, in seconds.\
    \tDefaults to 1 second. May be a decimal.\
  /L\tIndicates an output logfile name is given.\
    \tWarning: No write buffer is used. Use a large [/T seconds].\
  ";//TODO detailed note about admin rights

double GetPercentUsedRAM() {
	//Gets the system physical ram usage percent, returned as a double.
	MEMORYSTATUSEX data;
	data.dwLength = sizeof(data);
	if (GlobalMemoryStatusEx(&data) == 0) {
		wcout << "GetPercentUsedRAM() GlobalMemoryStatusEx() failed." << endl;
		return 0.0;
	}
	double bytes_in_use = (double)(data.ullTotalPhys - data.ullAvailPhys);
	return bytes_in_use / ((double)data.ullTotalPhys) * 100;
}

enum bottleneck_causes {none, cpu, wio, rio, tio};

int wmain(int argc, wchar_t* argv[])
{
	//Argument vars to be assigned during argument parsing
	wchar_t* logging_filename = 0;
	int master_sleep_time = 1000;

	//Argument parsing
	for (int argn = 1; argn < argc; ++argn) {
		if (argv[argn][0] == L'-') {
			argv[argn][0] = L'/';
		}
		ConvertCStringToUpper(argv[argn]);
		if (StringsMatch(argv[argn], L"/T")) {
			//Time input
			++argn;
			if (argn < argc) {
				wstring sleep_time_string;
				sleep_time_string.assign(argv[argn]);
				double sleep_time_seconds = stod(argv[argn]);
				if (sleep_time_seconds < 0.001) {
					wcout << "Time must be at least 0.001 seconds." << endl;
					return EXIT_FAILURE;
				}
				if (sleep_time_seconds > 2147483) {
					wcout << "Time must be less than or equal to 2,147,483 seconds." << endl;
					return EXIT_FAILURE;
				}
				master_sleep_time = (int) 1000 * sleep_time_seconds;//Generates a compiler warning, but I did the safety checks above. 
			}
			else {
				wcout << "Did not specify a time." << endl;
				return EXIT_FAILURE;
			}
		}
		else if (wcscmp(argv[argn], L"/L") == 0) {
			//Logging, read filename next
			++argn;
			if (argn < argc) logging_filename = argv[argn];
			else {
				wcout << "Did not specify logging filename." << endl;
				wcout << USAGE_TEXT << endl;
				return EXIT_FAILURE;
			}
		}
		else {
			//Display usage text
			wcout << "Unknown input." << endl << endl;
			wcout << USAGE_TEXT << endl;
			return EXIT_FAILURE;
		}
	}

	//Check if the registry is set to see PIDs when collecting process data
	bool registry_is_set = RegistryIsSetForPIDs();
	if (!registry_is_set) {
		//Attempt to set the registry correctly
		registry_is_set = SetRegistryForPIDs();
	}
	if (!registry_is_set) {
		wcout << "Your system is not configured to monitor processes using their PIDs. You will see missing data. Run once with admin rights to enable more accurate process monitoring." << endl;
	}

	//Open logging file if specified
	wofstream logfile;
	if (logging_filename != 0) {
		logfile.open(logging_filename, ios::out | ios::app);
		if (!logfile.is_open()) {
			wcout << "Error opening logfile \"" << logging_filename << "\"" << endl;
			wcout << USAGE_TEXT << endl;
			return EXIT_FAILURE;
		}
	}

	//Welcome message
	wcout << L"Resource Monitor, Kristofer Christakos, March 2017" << endl;
	wcout << L"Disk%  Download\tUpload\tCPU%   Process\t\tRAM%" << endl;
	if (logging_filename != 0) {
		logfile << L"Resource Monitor, Kristofer Christakos, March 2017" << endl;
		logfile << L"Disk%  Download\tUpload\tCPU%   Process\t\tRAM%" << endl;
	}

	//Open query
	PDH_HQUERY query_handle;
	PDH_STATUS pdh_status = PdhOpenQuery(NULL, 0, &query_handle);
	if (pdh_status != ERROR_SUCCESS) {
		wcout << "PdhOpenQuery() error." << endl;
		return EXIT_FAILURE;
	}

	//Add counters
	PDH_HCOUNTER cpu_pct_counter = AddSingleCounter(query_handle, 
									L"\\Processor(_Total)\\% Processor Time");
	PDH_HCOUNTER disk_pct_counters = AddSingleCounter(query_handle, 
									L"\\PhysicalDisk(*)\\% Disk Time");
	PDH_HCOUNTER bytes_sent_counters = AddSingleCounter(query_handle, 
									L"\\Network Interface(*)\\Bytes Sent/sec");
	PDH_HCOUNTER bytes_recv_counters = AddSingleCounter(query_handle, 
									L"\\Network Interface(*)\\Bytes Received/sec");
	PDH_HCOUNTER process_cpu_pct_counters = AddSingleCounter(query_handle, 
									L"\\Process(*)\\% Processor Time");
	PDH_HCOUNTER process_write_bytes_counters = AddSingleCounter(query_handle, 
									L"\\Process(*)\\IO Write Bytes/sec");
	PDH_HCOUNTER process_read_bytes_counters = AddSingleCounter(query_handle, 
									L"\\Process(*)\\IO Read Bytes/sec");
	
	//Collect first sample
	if (!CollectQueryData(query_handle)) {
		wcout << "First sample collection failed." << endl;
		return EXIT_FAILURE;
	}

	//Pointers for keeping track of processes
	ProcessRaw* process_raw_old = 0;
	ProcessRaw* process_raw_new = 0;
	DWORD process_raw_old_length = 0;
	DWORD process_raw_new_length = 0;

	int sleep_time = 1000;
	while (true) {
		Sleep(sleep_time);
		if (sleep_time != master_sleep_time) {
			sleep_time = master_sleep_time;
		}
		CollectQueryData(query_handle);

		////////// CPU % //////////
		PDH_FMT_COUNTERVALUE cpu_pct;
		pdh_status = PdhGetFormattedCounterValue(cpu_pct_counter, PDH_FMT_DOUBLE, 0, &cpu_pct);
		if ((pdh_status != ERROR_SUCCESS) || (cpu_pct.CStatus != ERROR_SUCCESS)) {
			//This will be the first to error if something changes.
			//	(for example, a disk drive is connected)
			//The counters will be fine next cycle, so gracefully ignore the error.
			//cout << "PdhGetFormattedCounterValue() cpu_pct_counter error." << endl;
			sleep_time = 1;
			continue;
		}
		//Use cpu_pct.doubleValue

		////////// Disk %s //////////
		PDH_FMT_COUNTERVALUE_ITEM* disk_pcts = 0;
		DWORD counter_count = GetCounterArray(disk_pct_counters, PDH_FMT_DOUBLE, &disk_pcts);
		if (counter_count == 0) {
			//cout << "GetCounterArray() error for disk percent counters." << endl;
			sleep_time = 1;
			continue;
		}
		
		//Find the maximum disk usage to display, that will be the bottleneck I care about
		//Skip the first disk, it is an average of all disks
		double highest_disk_usage = 0.0;
		for (DWORD diskN = 1; diskN < counter_count; ++diskN) {
			if (disk_pcts[diskN].FmtValue.doubleValue > highest_disk_usage) {
				highest_disk_usage = disk_pcts[diskN].FmtValue.doubleValue;
			}
		}
		delete[] disk_pcts;
		//Use highest_disk_usage

		////////// Network I/O bytes //////////
		unsigned long long sent_bytes = SumCounterArray(bytes_sent_counters);
		unsigned long long recv_bytes = SumCounterArray(bytes_recv_counters);
		
		////////// RAM % //////////
		double ram_pct = GetPercentUsedRAM();

		////////// Save High-Performance Per-Process Data //////////
		if (registry_is_set) {
			//CPU (and initialize process_raw_new here too)
			PDH_RAW_COUNTER_ITEM* process_cpu_pcts = 0;
			process_raw_new_length = GetCounterArrayRawValues(process_cpu_pct_counters, &process_cpu_pcts);
			if (process_raw_new_length == 0) {
				wcout << "GetCounterArrayRawValues() error for process cpu percent counters." << endl;
			}
			else {
				process_raw_new = new ProcessRaw[process_raw_new_length];
				for (DWORD n = 0; n < process_raw_new_length; ++n) {
					process_raw_new[n].PID = ParsePIDFromRawCounterName(process_cpu_pcts[n].szName);
					process_raw_new[n].name = ParseNameFromRawCounterName(process_cpu_pcts[n].szName);
					memcpy(&process_raw_new[n].raw_cpu, &process_cpu_pcts[n].RawValue, sizeof(PDH_RAW_COUNTER));
				}
			}
			delete[] process_cpu_pcts;

			//Write I/O
			if (process_raw_new_length != 0) {
				PDH_RAW_COUNTER_ITEM* process_wio = 0;
				DWORD process_count = GetCounterArrayRawValues(process_write_bytes_counters, &process_wio);
				if (process_count == 0) {
					wcout << "GetCounterArrayRawValues() error for process WIO counters." << endl;
				}
				else {
					for (DWORD n = 0; n < process_count; ++n) {
						DWORD PID = ParsePIDFromRawCounterName(process_wio[n].szName);
						if (PID != 0) {
							//Find the process and add the raw data
							int index = FindPIDInProcessRawArray(process_raw_new, process_raw_new_length, PID);
							if (index != -1) {
								memcpy(&process_raw_new[n].raw_wio, &process_wio[n].RawValue, sizeof(PDH_RAW_COUNTER));
							}
						}
					}
					delete[] process_wio;
				}
			}

			//Read I/O
			if (process_raw_new_length != 0) {
				PDH_RAW_COUNTER_ITEM* process_rio = 0;
				DWORD process_count = GetCounterArrayRawValues(process_read_bytes_counters, &process_rio);
				if (process_count == 0) {
					wcout << "GetCounterArrayRawValues() error for process RIO counters." << endl;
				}
				else {
					for (DWORD n = 0; n < process_count; ++n) {
						DWORD PID = ParsePIDFromRawCounterName(process_rio[n].szName);
						if (PID != 0) {
							//Find the process and add the raw data
							int index = FindPIDInProcessRawArray(process_raw_new, process_raw_new_length, PID);
							if (index != -1) {
								memcpy(&process_raw_new[n].raw_rio, &process_rio[n].RawValue, sizeof(PDH_RAW_COUNTER));
							}
						}
					}
					delete[] process_rio;
				}
			}
		}

		////////// Determine which bottleneck to care about //////////
		wstring bottleneck_name = L"";
		bottleneck_causes bottleneck_cause = none;
		bool need_process_cpu = false;
		bool need_process_rio = false;
		bool need_process_wio = false;
		if (cpu_pct.doubleValue >= 90.0) {
			//Find process with highest processor usage
			bottleneck_cause = cpu;
			need_process_cpu = true;
		}
		else {
			//Not a CPU bottleneck so IO is more interesting now
			if (highest_disk_usage >= 20.0) {
				bottleneck_cause = tio;
				need_process_rio = true;
				need_process_wio = true;
			}
			else if (recv_bytes > sent_bytes) {
				bottleneck_cause = rio;
				need_process_rio = true;
			}
			else if (sent_bytes < recv_bytes) {
				bottleneck_cause = wio;
				need_process_wio = true;
			}
			else {
				//Nothing is happening
				bottleneck_cause = none;
			}
		}

		///////// If registry is not set, save the needed formatted data //////////
		DWORD process_count = 0;
		PDH_FMT_COUNTERVALUE_ITEM* process_cpu_pcts = 0;
		PDH_FMT_COUNTERVALUE_ITEM* process_write_bytes = 0;
		PDH_FMT_COUNTERVALUE_ITEM* process_read_bytes = 0;
		PDH_FMT_COUNTERVALUE_ITEM* process_total_bytes = 0;
		if (registry_is_set == false) {
			//Allocates memory to the above pointers. Will need to deallocate later.
			if (need_process_cpu) {
				process_count = GetCounterArray(process_cpu_pct_counters, PDH_FMT_DOUBLE, &process_cpu_pcts);
			}
			if (need_process_wio) {
				process_count = GetCounterArray(process_write_bytes_counters, PDH_FMT_LARGE, &process_write_bytes);
			}
			if (need_process_rio) {
				process_count = GetCounterArray(process_read_bytes_counters, PDH_FMT_LARGE, &process_read_bytes);
			}
			if ((bottleneck_cause == tio) && (process_count > 0)) {
				process_total_bytes = new PDH_FMT_COUNTERVALUE_ITEM[process_count];
				memcpy(process_total_bytes, process_read_bytes, process_count * sizeof(PDH_FMT_COUNTERVALUE_ITEM));
				for (DWORD n = 0; n < process_count; ++n) {
					process_total_bytes[n].FmtValue.largeValue += process_write_bytes[n].FmtValue.largeValue;
				}
			}
		}

		////////// If registry is set, calculate the needed formatted data //////////
		if (registry_is_set) {
			for (DWORD n = 0; n < process_raw_new_length; ++n) {
				//Check if in process_raw_old, and calculate formmated values if so
				int old_index = FindPIDInProcessRawArray(process_raw_old, process_raw_old_length, process_raw_new[n].PID);
				if (old_index == -1) {
					//Process is not there to calculate, probably a new process
					continue;
				}
				PDH_FMT_COUNTERVALUE formatted_data;
				if (need_process_cpu) {
					PDH_STATUS ret = PdhCalculateCounterFromRawValue(
						process_cpu_pct_counters, 
						PDH_FMT_DOUBLE, 
						&process_raw_new[n].raw_cpu, 
						&process_raw_old[n].raw_cpu, 
						&formatted_data);
					if (ret == ERROR_SUCCESS) {
						process_raw_new[n].cpu = formatted_data.doubleValue;
					}
				}
				if (need_process_wio) {
					PDH_STATUS ret = PdhCalculateCounterFromRawValue(
						process_write_bytes_counters, 
						PDH_FMT_LARGE, 
						&process_raw_new[n].raw_wio, 
						&process_raw_old[n].raw_wio, 
						&formatted_data);
					if (ret == ERROR_SUCCESS) {
						process_raw_new[n].wio = formatted_data.largeValue;
					}
				}
				if (need_process_rio) {
					PDH_STATUS ret = PdhCalculateCounterFromRawValue(
						process_read_bytes_counters, 
						PDH_FMT_LARGE, 
						&process_raw_new[n].raw_rio, 
						&process_raw_old[n].raw_rio, 
						&formatted_data);
					if (ret == ERROR_SUCCESS) {
						process_raw_new[n].rio = formatted_data.largeValue;
					}
				}
				if (bottleneck_cause == tio) {
					process_raw_new[n].tio = process_raw_new[n].wio + process_raw_new[n].rio;
				}
			}
		}

		////////// Determine the bottleneck process, if registry is not set //////////
		//  If an error occurs with process counter data, skip outputting
		//  the bottleneck process and output the resource stats anyways.
		if ((registry_is_set == false) && (process_count != 0)) {
			DWORD index_of_highest = -1;
			if (bottleneck_cause == cpu) {
				index_of_highest = FindIndexOfProcessWithHighestDouble(process_cpu_pcts, process_count);
				if (index_of_highest != -1) {
					bottleneck_name.assign(process_cpu_pcts[index_of_highest].szName);
				}
				/*else {
					//There was an error and all values were probably set to 0
					//Error likely caused by number of processes changing
				}*/
			}
			else if (bottleneck_cause == tio) {
				//Find process with highest total IO
				index_of_highest = FindIndexOfProcessWithHighestLongLong(process_total_bytes, process_count);
				if (index_of_highest != -1) {
					bottleneck_name.assign(process_total_bytes[index_of_highest].szName);
				}
			}
			else if (bottleneck_cause == rio) {
				//Find process with highest read IO
				index_of_highest = FindIndexOfProcessWithHighestLongLong(process_read_bytes, process_count);
				if (index_of_highest != -1) {
					bottleneck_name.assign(process_read_bytes[index_of_highest].szName);
				}
			}
			else if (bottleneck_cause == wio) {
				//Find process with highest write IO
				index_of_highest = FindIndexOfProcessWithHighestLongLong(process_write_bytes, process_count);
				if (index_of_highest != -1) {
					bottleneck_name.assign(process_write_bytes[index_of_highest].szName);
				}
			}
		}

		////////// Determine the bottleneck process, if registry is set //////////
		if (registry_is_set) {
			DWORD index_of_highest = -1;
			if (bottleneck_cause == cpu) {
				double highest_value = 0.0;
				for (DWORD n = 0; n < process_raw_new_length; ++n) {
					if ((process_raw_new[n].PID != 0) && (process_raw_new[n].cpu > highest_value)) {
						highest_value = process_raw_new[n].cpu;
						index_of_highest = n;
					}
				}
			}
			else if (bottleneck_cause == tio) {
				long long highest_value = 0;
				for (DWORD n = 0; n < process_raw_new_length; ++n) {
					if ((process_raw_new[n].PID != 0) && (process_raw_new[n].tio > highest_value)) {
						highest_value = process_raw_new[n].tio;
						index_of_highest = n;
					}
				}
			}
			else if (bottleneck_cause == wio) {
				long long highest_value = 0;
				for (DWORD n = 0; n < process_raw_new_length; ++n) {
					if ((process_raw_new[n].PID != 0) && (process_raw_new[n].wio > highest_value)) {
						highest_value = process_raw_new[n].wio;
						index_of_highest = n;
					}
				}
			}
			else if (bottleneck_cause == rio) {
				long long highest_value = 0;
				for (DWORD n = 0; n < process_raw_new_length; ++n) {
					if ((process_raw_new[n].PID != 0) && (process_raw_new[n].rio > highest_value)) {
						highest_value = process_raw_new[n].rio;
						index_of_highest = n;
					}
				}
			}

			//Add the process name as the bottleneck
			if (index_of_highest != -1) {
				bottleneck_name.assign(process_raw_new[index_of_highest].name);
				bottleneck_name.append(L"_");
				bottleneck_name.append(to_wstring(process_raw_new[index_of_highest].PID));
			}
		}

		//Cleanup if needed
		delete[] process_cpu_pcts;
		delete[] process_write_bytes;
		delete[] process_read_bytes;
		delete[] process_total_bytes;

		//Set the new process data to be the old data point next loop
		delete[] process_raw_old;
		process_raw_old = process_raw_new;
		process_raw_old_length = process_raw_new_length;
		process_raw_new = 0;
		process_raw_new_length = 0;

		////////// Format Output //////////
		wstring bottleneck_cause_text = L"";
		if ((bottleneck_name.length() == 0)/* || (bottleneck_name.compare(L"_Total") == 0)*/) {
			//bottleneck_cause.assign(L"");
			bottleneck_name = L"\t\t";
		}
		else {
			if (bottleneck_cause == cpu) bottleneck_cause_text = L"CPU:";
			else if (bottleneck_cause == tio) bottleneck_cause_text = L"TIO:";
			else if (bottleneck_cause == wio) bottleneck_cause_text = L"WIO:";
			else if (bottleneck_cause == rio) bottleneck_cause_text = L"RIO:";
		}
		wchar_t text_buffer[1024];
		swprintf(text_buffer, 1024, L"%5.2f  %u\t%u\t%5.2f  %s%s\t%5.2f\n",
			highest_disk_usage, 
			(unsigned int) recv_bytes, 
			(unsigned int) sent_bytes, 
			cpu_pct.doubleValue, 
			bottleneck_cause_text.c_str(),
			bottleneck_name.c_str(), 
			ram_pct);
		wcout << text_buffer;
		if (logging_filename != 0) {
			//First write the time
			time_t rawtime = time(0);
			//struct tm realtime;
			//_localtime32_s(&rawtime, &realtime);
			wchar_t time_buffer[256];
			wcsftime(time_buffer, 256, L"%F %T\t", localtime(&rawtime));
			logfile << time_buffer;

			//Then write the output line
			logfile << text_buffer;

			//Flush file before computer crashes
			logfile.flush();
		}
	}

    return EXIT_SUCCESS;
}
