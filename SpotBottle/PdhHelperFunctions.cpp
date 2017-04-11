#include "PdhHelperFunctions.h"
#include <iostream>
#include <string>
#include <PdhMsg.h>
#include "StringHelpers.h"

using namespace std;

PDH_HCOUNTER AddSingleCounter(PDH_HQUERY query_handle, LPCWSTR query_str) {
	//Adds a counter (query_str) to the opened query (query_handle).
	PDH_HCOUNTER counter_handle;
	PDH_STATUS pdh_status = PdhAddCounter(query_handle, query_str, 0, &counter_handle);
	if (pdh_status != ERROR_SUCCESS) {
		wcout << "AddSingleCounter() PdhAddCounter() error." << endl;
		return 0;
	}
	return counter_handle;
}

bool CollectQueryData(PDH_HQUERY query_handle) {
	//Updates raw query data, assuming counters have been added.
	//Returns true if successful.
	PDH_STATUS pdh_status = PdhCollectQueryData(query_handle);
	if (pdh_status != ERROR_SUCCESS) {
		cout << "CollectQueryData() PdhCollectQueryData() error." << endl;
		if (pdh_status == PDH_INVALID_HANDLE) wcout << "The query handle is not valid." << endl;
		else if (pdh_status == PDH_NO_DATA) wcout << "The query does not currently contain any counters." << endl;
		return false;
	}
	return true;
}

DWORD GetCounterArray(PDH_HCOUNTER counters, DWORD format, PDH_FMT_COUNTERVALUE_ITEM** values_out) {
	//Allocates memory to save an array of counter data, size is returned.
	//Counter data saved to values_out. This must be deallocated later.
	//Returns 0 if an error.
	DWORD buffer_size = 0;
	DWORD counter_count = 0;
	PDH_STATUS pdh_status = PdhGetFormattedCounterArray(counters, format, &buffer_size, &counter_count, 0);
	if (pdh_status != PDH_MORE_DATA) {
		wcout << "GetCounterArray() expected PDH_MORE_DATA." << endl;
		return 0;
	}
	*values_out = (PDH_FMT_COUNTERVALUE_ITEM*) new char[buffer_size];
	pdh_status = PdhGetFormattedCounterArray(counters, format, &buffer_size, &counter_count, *values_out);
	if (pdh_status != ERROR_SUCCESS) {
		//wcout << "GetCounterArray() error code " << std::hex << (unsigned int)pdh_status << endl;
		delete[] * values_out;
		return 0;
	}
	return counter_count;
}

DWORD GetCounterArrayRawValues(PDH_HCOUNTER counters, PDH_RAW_COUNTER_ITEM** values_out) {
	//Allocates memory to save an array of counter data, size is returned.
	//Counter data saved to values_out. This must be deallocated later.
	//Returns 0 if an error.
	DWORD buffer_size = 0;
	DWORD counter_count = 0;
	PDH_STATUS pdh_status = PdhGetRawCounterArray(counters, &buffer_size, &counter_count, 0);
	if (pdh_status != PDH_MORE_DATA) {
		wcout << "PdhGetRawCounterArray() expected PDH_MORE_DATA." << endl;
		return 0;
	}
	*values_out = (PDH_RAW_COUNTER_ITEM*) new char[buffer_size];
	pdh_status = PdhGetRawCounterArray(counters, &buffer_size, &counter_count, *values_out);
	if (pdh_status != ERROR_SUCCESS) {
		//wcout << "PdhGetRawCounterArray() error code " << std::hex << (unsigned int)pdh_status << endl;
		delete[] * values_out;
		return 0;
	}
	return counter_count;
}

unsigned long long SumCounterArray(PDH_HCOUNTER counters) {
	//Gets an array of counter data (unsigned long long) and returns their sum.
	//Intended for adding bytes over all network interfaces for IO counters.
	PDH_FMT_COUNTERVALUE_ITEM* values = 0;
	DWORD values_count = GetCounterArray(counters, PDH_FMT_LARGE, &values);
	if (values_count == 0) {
		wcout << "SumCounterArray() error." << endl;
		return 0;
	}

	//Sum the values in the array
	unsigned long long total = 0;
	for (DWORD entry = 0; entry < values_count; ++entry) {
		total += values[entry].FmtValue.largeValue;
	}
	delete[] values;
	return total;
}

DWORD FindIndexOfProcessWithHighestDouble(PDH_FMT_COUNTERVALUE_ITEM* processes, DWORD process_count) {
	//Returns the index of the given array, treating the data as double, or -1 on failure.
	double highest = 0.0;
	DWORD index_of_highest = -1;
	for (DWORD n = 0; n < process_count; ++n) {
		if (processes[n].FmtValue.doubleValue > highest) {
			if (StringsMatch(processes[n].szName, L"_Total") ||
				StringsMatch(processes[n].szName, L"Idle")) {
				//Ignore collecting values for these
			}
			else {
				highest = processes[n].FmtValue.doubleValue;
				index_of_highest = n;
			}
		}
	}
	return index_of_highest;
}

DWORD FindIndexOfProcessWithHighestLongLong(PDH_FMT_COUNTERVALUE_ITEM* processes, DWORD process_count) {
	//Returns the index of the given array, treating the data as long long, or -1 on failure.
	long long highest = 0ULL;
	DWORD index_of_highest = -1;
	for (DWORD n = 0; n < process_count; ++n) {
		if (processes[n].FmtValue.largeValue > highest) {
			if (StringsMatch(processes[n].szName, L"_Total") ||
				StringsMatch(processes[n].szName, L"Idle")) {
				//Ignore collecting values for these
			}
			else {
				highest = processes[n].FmtValue.largeValue;
				index_of_highest = n;
			}
		}
	}
	return index_of_highest;
}

int FindPIDInProcessRawArray(ProcessRaw* process_raw_array, int array_length, int PID) {
	//Searches an array of ProcessRaw objects for the given PID.
	//Returns -1 on failure.
	for (int i = 0; i < array_length; ++i) {
		if (process_raw_array[i].PID == PID) return i;
	}
	return -1;
}

ProcessRaw::ProcessRaw() {
	//Constructor
	PID = 0;
	name.assign(L"");
	cpu = 0;
	wio = 0;
	rio = 0;
	tio = 0;
}

void ProcessRaw::Copy(ProcessRaw* source) {
	//Safely copy the data from another ProcessRaw object.
	PID = source->PID;
	name.assign(source->name);
	memcpy(&raw_cpu, &source->raw_cpu, sizeof(PDH_RAW_COUNTER));
	memcpy(&raw_wio, &source->raw_wio, sizeof(PDH_RAW_COUNTER));
	memcpy(&raw_rio, &source->raw_rio, sizeof(PDH_RAW_COUNTER));
	cpu = source->cpu;
	wio = source->wio;
	rio = source->rio;
	tio = source->tio;
}

void ProcessRaw::ParseRawCounterName(wchar_t* szName) {
	//Calculates the ProcessRaw object's PID and name from a raw counter name.
	//Expecting names like: processname_0000, where the numbers after the underscore is the PID
	wstring name = szName;//wstring version for the functions
	size_t underscore_pos = name.rfind('_');
	if (StringsMatch(szName, L"_Total") ||
		StringsMatch(szName, L"Idle") ||
		(underscore_pos == std::string::npos)) {
		this->name.assign(szName);
		this->PID = 0;
		return;
	}
	this->name = name.substr(0, underscore_pos);
	wstring PID = name.substr(underscore_pos + 1, name.length() - underscore_pos);
	this->PID = stoi(PID);
}

DWORD ParsePIDFromRawCounterName(wchar_t* szName) {
	//Returns the PID of the raw counter szName as a DWORD, 0 on failure.
	//Failure includes processes named "_Total" or "Idle".
	wstring name = szName;//wstring version for the functions
	size_t underscore_pos = name.rfind('_');
	if (StringsMatch(szName, L"_Total") ||
		StringsMatch(szName, L"Idle") ||
		(underscore_pos == std::string::npos)) {
		//Ignore collecting values for these
		return 0;
	}
	wstring PID = name.substr(underscore_pos + 1, name.length() - underscore_pos);
	return stoi(PID);
}

wstring ParseNameFromRawCounterName(wchar_t* szName) {
	//Returns the name of the raw counter szName, removing the PID, or L"" on failure.
	//Failure includes processes named "_Total" or "Idle".
	wstring name = szName;//wstring version for the functions
	size_t underscore_pos = name.rfind('_');
	if (StringsMatch(szName, L"_Total") ||
		StringsMatch(szName, L"Idle") ||
		(underscore_pos == std::string::npos)) {
		//Ignore collecting values for these
		name.assign(L"");
	}
	else {
		name.assign(name.substr(0, underscore_pos));
	}
	return name;
}

bool RegistryIsSetForPIDs() {
	//This is a registry read-only function to check if it is set correctly or not.
	//Does not require admin rights. Hoping the value is set already.
	//Gracefully return false on error. The program will attempt to set the registry 
	//correctly later if this returns false.

	//First attempt to open the registry key
	HKEY key;
	LONG ret = RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\PerfProc\\Performance", 0, KEY_QUERY_VALUE, &key);
	if (ret != ERROR_SUCCESS) {
		//wcout << "Could not open registry key. Error: " << ret << endl;
		return false;
	}

	//Attempt to read the registry value
	DWORD data_type = 0;
	DWORD data_value = 0;
	DWORD data_size = sizeof(data_value);
	ret = RegQueryValueEx(key, L"ProcessNameFormat", NULL, &data_type, (BYTE*)&data_value, &data_size);
	if (ret == ERROR_SUCCESS) {
		bool correct_value = true;
		if (data_type != REG_DWORD) {
			//wcout << "Unexpected data type while reading registry." << endl;
			correct_value = false;
		}
		else if (data_value != 2) {
			//Checking the setting needed to display PIDs in the process names
			correct_value = false;
		}
		else {
			//wcout << "Registry value is correct." << endl;
		}
		RegCloseKey(key);
		return correct_value;
	}
	/*else if (ret == ERROR_MORE_DATA) {
	wcout << "ERROR_MORE_DATA" << endl;
	}
	else if (ret == ERROR_FILE_NOT_FOUND) {
	wcout << "ERROR_FILE_NOT_FOUND" << endl;
	}
	else {
	wcout << ret << endl;
	}*/

	//Registry value was not read or set correctly.
	return false;
}

bool SetRegistryForPIDs() {
	//This function requires admin rights to write to the registry. 
	//Returns true if everything went well, otherwise false. 

	//First create the registry key (automatically opens the key if it exists)
	HKEY key;
	DWORD ret = RegCreateKeyEx(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\PerfProc\\Performance", 0, 0, 0, KEY_SET_VALUE, NULL, &key, NULL);
	if (ret != ERROR_SUCCESS) {
		//wcout << "Error creating registry key to specify PID data: " << ret << endl;
		return false;
	}

	//Set the appropriate value to display PIDs
	DWORD data_value = 2;
	ret = RegSetValueEx(key, L"ProcessNameFormat", 0, REG_DWORD, (BYTE*)&data_value, sizeof(data_value));
	bool set_value = true;
	if (ret != ERROR_SUCCESS) {
		//wcout << "Error setting registry key value: " << ret << endl;
		set_value = false;
	}

	//Cleanup regardless of having admin rights or not
	RegCloseKey(key);
	return set_value;
}
