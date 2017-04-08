// ResourceMonitor.cpp : Defines main()
// Written by Kristofer Christakos
// Command line resource monitor. 
// Displays CPU, network, disk, RAM, and process bottleneck information.

#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <queue>
#include <ctime>
#include <Pdh.h>//Link pdh.lib
#pragma comment(lib, "pdh.lib")
#include <PdhMsg.h>

#include "PdhHelperFunctions.h"
#include "StringHelpers.h"

using namespace std;

const wchar_t USAGE_TEXT[] =
L"RESOURCEMONITOR [/T seconds] [/L logfile] /TSV /H\n\n"
" /T\tIndicates the time delay between data collection is given, in seconds.\n"
"    \tDefaults to 1 second. May be a decimal.\n\n"
" /L\tIndicates an output logfile name is given.\n"
"    \tWarning: No write buffer is used. Use a large [/T seconds].\n\n"
" /TSV\tTab Separated Values. Disables smart formatting for tabs instead.\n\n"
" /H\tDisplays this usage/help text.\n\n\n"
"Data Collected:\n\n"
" Disk%\tPercent Disk Read/Write Time for the physical disk most in use.\n"
"      \tInternally calculated for all physical disks and then the highest is\n"
"      \tdisplayed to catch a disk-related bottleneck.\n"
"      \tHard disk drives are often the cause of a slow computer.\n\n"
" Download Bytes downloaded, summed across all network interfaces.\n\n"
" Upload\tBytes uploaded, summed across all network interfaces.\n\n"
" CPU%\tPercent Processor Usage Time, averaged across all processor cores.\n\n"
" RAM%\tPercent Physical RAM used.\n\n\n"
"Bottleneck Cause Key:\n\n"
" CPU:\tIndicates CPU bottleneck.\n"
"     \tDisplays the estimated percent CPU time the process used.\n\n"
" RIO:\tIndicates Read-bytes I/O bottleneck.\n"
"     \tUsed as an estimation to determine per-process download bytes.\n\n"
" WIO:\tIndicates Write-bytes I/O bottleneck.\n"
"     \tUsed as an estimation to determine per-process upload bytes.\n\n"
" TIO:\tIndicates Total-bytes I/O bottleneck.\n"
"     \tUsed as an estimation to determine per-process percent disk usage.\n\n\n"
"Data Collection Note:\n\n"
"\tThis program uses the Windows Performance Counters API, which by \n"
"\tdefault does not track process IDs (PIDs) along with process names. \n"
"\tThis will cause gaps in the displayed data when a new process is \n"
"\tcreated or destroyed, because process names are not unique. To enable \n"
"\ttracking of PIDs, run this program once as an administrator and the \n"
"\tsetting will be enabled if not already set. Further calls to this \n"
"\tprogram will not require administrator rights.\n\n\n"
"Example Usage:\n\n"
"RESOURCEMONITOR\n"
"RESOURCEMONITOR /T 3\n"
"RESOURCEMONITOR /T 10 /L C:\\logfile.txt /TSV\n"
;

const wchar_t WELCOME_HEADER[] = L"Resource Monitor, Kristofer Christakos, April 2017";

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

DWORD GetProcessorCount() {
	SYSTEM_INFO sys_info;
	GetSystemInfo(&sys_info);
	return sys_info.dwNumberOfProcessors;
}

enum bottleneck_causes {none, cpu, wio, rio, tio};

size_t GetLargestValueInQueue(queue <size_t>* size_queue) {
	queue <size_t> temp;
	size_t max_value = 0;

	//Store values in temp while finding the largest
	while (size_queue->size() > 0) {
		size_t check_value = size_queue->front();
		size_queue->pop();
		temp.push(check_value);
		if (check_value > max_value) max_value = check_value;
	}

	//Loop again to restore the loop
	while (temp.size() > 0) {
		size_queue->push(temp.front());
		temp.pop();
	}
	
	return max_value;
}

int wmain(int argc, wchar_t* argv[])
{
	//Argument vars to be assigned during argument parsing
	wchar_t* logging_filename = 0;
	int master_sleep_time = 1000;
	bool smart_formatting = true;

	//Argument parsing
	for (int argn = 1; argn < argc; ++argn) {
		//Remove double dashes
		if (wcslen(argv[argn]) > 1) {
			if ((argv[argn][1] == L'-') && (argv[argn][0] == L'-')) {
				wstring no_double_dashes = argv[argn];
				no_double_dashes = no_double_dashes.substr(1, no_double_dashes.length() - 1);
				no_double_dashes[0] = L'/';
				wcscpy_s(argv[argn], no_double_dashes.length() + 1, no_double_dashes.c_str());
			}
		}
		//Remove single dashes
		if (argv[argn][0] == L'-') {
			argv[argn][0] = L'/';
		}
		ConvertCStringToUpper(argv[argn]);
		//Actual matching
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
		else if (StringsMatch(argv[argn], L"/L")) {
			//Logging, read filename next
			++argn;
			if (argn < argc) logging_filename = argv[argn];
			else {
				wcout << "Did not specify logging filename." << endl;
				wcout << USAGE_TEXT;
				return EXIT_FAILURE;
			}
		}
		else if (StringsMatch(argv[argn], L"/TSV")) {
			//No Smart Formatting
			smart_formatting = false;
		}
		else if (StringsMatch(argv[argn], L"/H") ||
				 StringsMatch(argv[argn], L"/HELP") ||
			     StringsMatch(argv[argn], L"/?")) {
			wcout << USAGE_TEXT;
			return EXIT_SUCCESS;
		}
		else {
			//Display usage text
			wcout << "Unknown input." << endl << endl;
			wcout << USAGE_TEXT;
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
		wcout << "Your system is not configured to monitor processes using their PIDs. You will see missing data. Run once with admin rights to enable more accurate process monitoring. See the usage/help for more details." << endl;
	}

	//Open logging file if specified
	wofstream logfile;
	if (logging_filename != 0) {
		logfile.open(logging_filename, ios::out | ios::app);
		if (!logfile.is_open()) {
			wcout << "Error opening logfile \"" << logging_filename << "\"" << endl;
			wcout << USAGE_TEXT;
			return EXIT_FAILURE;
		}
	}

	//Get number of processor cores
	DWORD processor_count = GetProcessorCount();

	//Welcome message
	wcout << WELCOME_HEADER << endl;
	if (smart_formatting) wcout << L"Disk%  Download\tUpload\tCPU%   Process\t\tRAM%" << endl;
	else				  wcout << L"Disk%\tDownload\tUpload\tCPU%\tProcess\tRAM%" << endl;
	if (logging_filename != 0) {
		logfile << WELCOME_HEADER << endl;
		if (smart_formatting) logfile << L"Disk%  Download\tUpload\tCPU%   Process\t\tRAM%" << endl;
		else logfile << L"Disk%\tDownload\tUpload\tCPU%\tProcess\tRAM%" << endl;
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

	//Formatting queues
	const unsigned int MAX_QUEUE_SIZE = 10;
	queue <size_t> bottleneck_name_length_queue;
	queue <size_t> bottleneck_cause_length_queue;

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
					process_raw_new[n].ParseRawCounterName(process_cpu_pcts[n].szName);
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
		ProcessRaw bottleneck;
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
				//Nothing is happening, pick something anyways
				if (cpu_pct.doubleValue > highest_disk_usage) {
					bottleneck_cause = cpu;
					need_process_cpu = true;
				}
				else if (highest_disk_usage > 1.00) {
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
					bottleneck_cause = tio;
					need_process_rio = true;
					need_process_wio = true;
				}
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
					bottleneck.name.assign(process_cpu_pcts[index_of_highest].szName);
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
					bottleneck.name.assign(process_total_bytes[index_of_highest].szName);
				}
			}
			else if (bottleneck_cause == rio) {
				//Find process with highest read IO
				index_of_highest = FindIndexOfProcessWithHighestLongLong(process_read_bytes, process_count);
				if (index_of_highest != -1) {
					bottleneck.name.assign(process_read_bytes[index_of_highest].szName);
				}
			}
			else if (bottleneck_cause == wio) {
				//Find process with highest write IO
				index_of_highest = FindIndexOfProcessWithHighestLongLong(process_write_bytes, process_count);
				if (index_of_highest != -1) {
					bottleneck.name.assign(process_write_bytes[index_of_highest].szName);
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

			//Add the process as the bottleneck
			if (index_of_highest != -1) {
				bottleneck.Copy(&process_raw_new[index_of_highest]);
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
		if (bottleneck.name.length() != 0) {
			const size_t number_text_length = 128;
			wchar_t number_text[number_text_length];
			if (bottleneck_cause == cpu) {
				bottleneck_cause_text = L"CPU:";
				swprintf(number_text, number_text_length, L"%1.0f%%", bottleneck.cpu/processor_count);
				bottleneck_cause_text.append(number_text);
			}
			else if (bottleneck_cause == tio) {
				bottleneck_cause_text = L"TIO:";
				//swprintf(number_text, number_text_length, L"%u%", bottleneck.tio);
				//bottleneck_cause_text.append(number_text);
			}
			else if (bottleneck_cause == wio) {
				bottleneck_cause_text = L"WIO:";
				//swprintf(number_text, number_text_length, L"%u%", bottleneck.wio);
				//bottleneck_cause_text.append(number_text);
			}
			else if (bottleneck_cause == rio) {
				bottleneck_cause_text = L"RIO:";
				//swprintf(number_text, number_text_length, L"%u%", bottleneck.rio);
				//bottleneck_cause_text.append(number_text);
			}
		}
		const size_t text_buffer_size = 1024;
		wchar_t text_buffer[text_buffer_size];
		if (smart_formatting) {
			//Assume 80 char width, try to format within 80 chars
			
			//Temp variables
			const size_t str_size = 32;
			wchar_t disk_str[str_size];
			wchar_t DL_str[str_size];
			wchar_t UL_str[str_size];
			wchar_t CPU_str[str_size];
			wchar_t RAM_str[str_size];

			//Format the pieces
			swprintf(disk_str, str_size, L"%5.2f", highest_disk_usage);
			swprintf(DL_str, str_size, L"%u", (unsigned int)recv_bytes);
			swprintf(UL_str, str_size, L"%u", (unsigned int)sent_bytes);
			swprintf(CPU_str, str_size, L"%5.2f", cpu_pct.doubleValue);
			swprintf(RAM_str, str_size, L"%5.2f", ram_pct);

			//Assume lengths after the pieces
			size_t after_disk = 2;
			if (wcslen(disk_str) == 6) after_disk = 1;
			
			size_t after_DL;
			if (wcslen(DL_str) < 8) after_DL = 8 - wcslen(DL_str);
			else after_DL = 1;

			size_t after_UL;
			if (wcslen(DL_str) < 8) after_UL = 8 - wcslen(UL_str);
			else after_UL = 1;

			size_t after_CPU = 2;
			if (wcslen(CPU_str) == 6) after_CPU = 1;

			if (bottleneck_cause_length_queue.size() > MAX_QUEUE_SIZE) bottleneck_cause_length_queue.pop();
			bottleneck_cause_length_queue.push(bottleneck_cause_text.length());
			size_t after_cause = GetLargestValueInQueue(&bottleneck_cause_length_queue) - bottleneck_cause_text.length() + 1;

			wstring bottleneck_name_text = bottleneck.name;
			if (bottleneck.PID != 0) {
				bottleneck_name_text.append(L"_");
				bottleneck_name_text.append(to_wstring(bottleneck.PID));
			}
			if (bottleneck_name_length_queue.size() > MAX_QUEUE_SIZE) bottleneck_name_length_queue.pop();
			bottleneck_name_length_queue.push(bottleneck_name_text.length());
			size_t after_name = GetLargestValueInQueue(&bottleneck_name_length_queue) - bottleneck_name_text.length() + 2;
			size_t text_chars_needed = 
				wcslen(disk_str) +
				wcslen(DL_str) +
				wcslen(UL_str) +
				wcslen(CPU_str) +
				bottleneck_cause_text.length() +
				bottleneck_name_text.length() +
				wcslen(RAM_str);
			size_t desired_space = text_chars_needed + after_disk + after_DL + after_UL + after_CPU + after_cause + after_name;
			if (desired_space > 79) {
				//wcout << "!!!!!!!!!!!desired_space=" << desired_space << endl;
				//Output won't fit in command prompt after the return character.
				//Adjust the process name to compensate.
				size_t space_needed = desired_space - 79;
				space_needed += 3;//For adding a "..." to show the name was too long

				//Recalculate bottleneck_name_text
				bottleneck_name_text = bottleneck.name.substr(0, bottleneck.name.length() - space_needed);
				bottleneck_name_text.append(L"..._");
				bottleneck_name_text.append(to_wstring(bottleneck.PID));

				//Clear the formatting queue
				while (bottleneck_name_length_queue.size() > 0) bottleneck_name_length_queue.pop();
				bottleneck_name_length_queue.push(bottleneck_name_text.length());
				after_name = 2;
			}

			//Create the final output string string
			swprintf(text_buffer, text_buffer_size, L"%s%*s%s%*s%s%*s%s%*s%s%*s%s%*s%s\n",
				disk_str, (int)after_disk, L"", 
				DL_str, (int)after_DL, L"",
				UL_str, (int)after_UL, L"",
				CPU_str, (int)after_CPU, L"",
				bottleneck_cause_text.c_str(), (int)after_cause, L"",
				bottleneck_name_text.c_str(), (int)after_name, L"",
				RAM_str);
		}
		else {
			//No smart formatting, simple tabular output
			wstring bottleneck_name_text = bottleneck.name;
			if (bottleneck.PID != 0) {
				bottleneck_name_text.append(L"_");
				bottleneck_name_text.append(to_wstring(bottleneck.PID));
			}
			swprintf(text_buffer, text_buffer_size, L"%4.2f\t%u\t%u\t%4.2f\t%s\t%s\t%4.2f\n",
				highest_disk_usage,
				(unsigned int)recv_bytes,
				(unsigned int)sent_bytes,
				cpu_pct.doubleValue,
				bottleneck_cause_text.c_str(),
				bottleneck_name_text.c_str(),
				ram_pct);
			//Old line: swprintf(text_buffer, text_buffer_size, L"%5.2f  %u\t%u\t%5.2f  %s %s\t%5.2f\n",
		}
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
