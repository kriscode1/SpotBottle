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
#include <locale>

#include "PdhHelperFunctions.h"

using namespace std;

const wchar_t USAGE_TEXT[] =
L"RESOURCEMONITOR [/T seconds] [/L logfile]\n"
L"  \n"
L"  /T\tIndicates the time delay between data collection is given, in seconds.\n"
L"    \tDefaults to 1 second. May be a decimal.\n"
L"  /L\tIndicates an output logfile name is given.\n"
L"    \tWarning: No write buffer is used. Use a large [/T seconds].\n"
//L"  /H\tDisplays this help message.\n"
L"  ";

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

void convert_cstring_toupper(wchar_t* input) {
	//Replaces all lowercase characters in a null terminated wchar_t array with uppercase characters, dependent on the locale.
	locale loc;
	while (*input != 0) {
		*input = toupper(*input, loc);
		input++;
	}
}

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
		convert_cstring_toupper(argv[argn]);
		if (wcscmp(argv[argn], L"/T") == 0) {
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
			if (argn < argc) {
				logging_filename = argv[argn];
			}
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
		cout << "PdhOpenQuery() error." << endl;
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

		////////// Determine which bottleneck to care about //////////
		//  If an error occurs with process counter data, 
		//	skip outputting the bottleneck process
		//	and output the resource stats anyways,
		wstring bottleneck_name = L"";
		wstring bottleneck_cause = L"";
		if (cpu_pct.doubleValue >= 90.0) {
			//Find process with highest processor usage
			PDH_FMT_COUNTERVALUE_ITEM* process_cpu_pcts = 0;
			DWORD process_count = GetCounterArray(process_cpu_pct_counters, 
												  PDH_FMT_DOUBLE, 
												  &process_cpu_pcts);
			if (process_count == 0) {
				//cout << "GetCounterArray() error for process cpu percent counters." << endl;
			}
			else {
				double highest_cpu_pct = 0.0;
				DWORD index_of_highest_cpu_pct = 0;
				for (DWORD procN = 1; procN < process_count; ++procN) {
					if (process_cpu_pcts[procN].FmtValue.doubleValue > highest_cpu_pct) {
						highest_cpu_pct = process_cpu_pcts[procN].FmtValue.doubleValue;
						index_of_highest_cpu_pct = procN;
					}
				}
				if (index_of_highest_cpu_pct != 0) {
					bottleneck_cause.assign(L"CPU");
					bottleneck_name.assign(process_cpu_pcts[index_of_highest_cpu_pct].szName);
				}//else there was an error and all values were probably set to 0
				delete[] process_cpu_pcts;
			}
		}
		else {
			//Not a CPU bottleneck so IO is more interesting now
			//First try to obtain the write bytes for each process
			PDH_FMT_COUNTERVALUE_ITEM* process_write_bytes = 0;
			DWORD process_count = GetCounterArray(process_write_bytes_counters, 
												  PDH_FMT_LARGE, 
												  &process_write_bytes);
			if (process_count == 0) {
				//cout << "GetCounterArray() error for process_write_bytes_counters." << endl;
			}
			else {
				//Second, try to obtain the read bytes for each process
				PDH_FMT_COUNTERVALUE_ITEM* process_read_bytes = 0;
				process_count = GetCounterArray(process_read_bytes_counters, 
												PDH_FMT_LARGE, 
												&process_read_bytes);
				if (process_count == 0) {
					//cout << "GetCounterArray() error for process_read_bytes_counters." << endl;
					delete[] process_write_bytes;
				}
				else {
					//Both write and read bytes were obtained,
					//	so continue with determining a bottleneck process
					if (highest_disk_usage >= 10.0) {
						//Find process with highest total IO
						long long highest_total_io = 0;
						DWORD index_of_highest = 0;
						for (DWORD procN = 1; procN < process_count; ++procN) {
							wcout << process_read_bytes[procN].szName << endl;
							long long total_io = process_write_bytes[procN].FmtValue.largeValue + process_read_bytes[procN].FmtValue.largeValue;
							if (total_io > highest_total_io) {
								highest_total_io = total_io;
								index_of_highest = procN;
							}
						}
						if (index_of_highest != 0) {
							//Save the process name for output
							bottleneck_cause.assign(L"TIO");
							bottleneck_name.assign(process_write_bytes[index_of_highest].szName);
						}
					}
					else if (recv_bytes > sent_bytes) {
						//Find process with highest read IO
						long long highest_read_io = 0;
						DWORD index_of_highest = 0;
						for (DWORD procN = 1; procN < process_count; ++procN) {
							if (process_read_bytes[procN].FmtValue.largeValue > highest_read_io) {
								highest_read_io = process_read_bytes[procN].FmtValue.largeValue;
								index_of_highest = procN;
							}
						}
						if (index_of_highest != 0) {
							//Save the process name for output
							bottleneck_cause.assign(L"RIO");
							bottleneck_name.assign(process_read_bytes[index_of_highest].szName);
						}
					}
					else if (sent_bytes < recv_bytes) {
						//Find process with highest write IO
						long long highest_write_io = 0;
						DWORD index_of_highest = 0;
						for (DWORD procN = 1; procN < process_count; ++procN) {
							if (process_write_bytes[procN].FmtValue.largeValue > highest_write_io) {
								highest_write_io = process_write_bytes[procN].FmtValue.largeValue;
								index_of_highest = procN;
							}
						}
						if (index_of_highest != 0) {
							//Save the process name for output
							bottleneck_cause.assign(L"WIO");
							bottleneck_name.assign(process_write_bytes[index_of_highest].szName);
						}
					}
					else {
						//Nothing is happening
						//bottleneck_name.assign(L"nothing");
					}
					delete[] process_write_bytes;
					delete[] process_read_bytes;
				}
			}
		}

		////////// Format Output //////////
		if ((bottleneck_name.length() == 0) || (bottleneck_name.compare(L"_Total") == 0)) {
			bottleneck_cause.assign(L"");
			bottleneck_name.assign(L"\t");
		}
		else {
			bottleneck_cause += L":";
		}
		wchar_t text_buffer[1024];
		swprintf(text_buffer, 1024, L"%5.2f  %u\t%u\t%5.2f  %s%s\t\t%5.2f\n",
			highest_disk_usage, 
			(unsigned int) recv_bytes, 
			(unsigned int) sent_bytes, 
			cpu_pct.doubleValue, 
			bottleneck_cause.c_str(),
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
			//logfile.write(text_buffer, wcslen(text_buffer));
			logfile << text_buffer;

			//Flush file before computer crashes
			logfile.flush();
		}
	}

    return EXIT_SUCCESS;
}
