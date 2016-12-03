// ResourceMonitor.cpp : Defines the entry point for the console application.
//

#include <iostream>
#include <string>
#include <Pdh.h>
#include <PdhMsg.h>
//Link pdh.lib
#include <Windows.h>
#include "PdhHelperFunctions.h"

using namespace std;


double GetPercentUsedRAM() {
	//Gets the system physical ram usage percent, returned as a double.
	MEMORYSTATUSEX data;
	data.dwLength = sizeof(data);
	if (GlobalMemoryStatusEx(&data) == 0) {
		cout << "GetPercentUsedRAM() GlobalMemoryStatusEx() failed." << endl;
		return 0.0;
	}
	double bytes_in_use = (double)(data.ullTotalPhys - data.ullAvailPhys);
	return bytes_in_use / ((double)data.ullTotalPhys) * 100;
}

int main()
{
	cout << "Resource Monitor\tDec 2016" << endl;
	cout << "Disk   Download\tUpload\tCPU    Process\tRAM" << endl;

	//Open query
	PDH_HQUERY query_handle;
	PDH_STATUS pdh_status = PdhOpenQuery(NULL, 0, &query_handle);
	if (pdh_status != ERROR_SUCCESS) {
		cout << "PdhOpenQuery() error." << endl;
		return 0;
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
		cout << "First sample collection failed." << endl;
		return 0;
	}

	int sleep_time = 1000;
	while (true) {
		Sleep(sleep_time);
		if (sleep_time != 1000) {
			sleep_time = 1000;
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
					if (highest_disk_usage >= 90.0) {
						//Find process with highest total IO
						long long highest_total_io = 0;
						DWORD index_of_highest = 0;
						for (DWORD procN = 1; procN < process_count; ++procN) {
							long long total_io = process_write_bytes[procN].FmtValue.largeValue + process_read_bytes[procN].FmtValue.largeValue;
							if (total_io > highest_total_io) {
								highest_total_io = total_io;
								index_of_highest = procN;
							}
						}
						if (index_of_highest != 0) {
							//Save the process name for output
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
		if (bottleneck_name.length() == 0) {
			bottleneck_name.assign(L"\t");
		}
		wprintf(L"%5.2f  %u\t%u\t%5.2f  %s\t%5.2f\n", 
			highest_disk_usage, 
			(unsigned int) recv_bytes, 
			(unsigned int) sent_bytes, 
			cpu_pct.doubleValue, 
			bottleneck_name.c_str(), 
			ram_pct);
	}

    return 0;
}
