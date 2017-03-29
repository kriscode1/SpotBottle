#include "PdhHelperFunctions.h"
#include <iostream>
#include <string>
#include <Pdh.h>
//Link pdh.lib
#include <PdhMsg.h>

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
