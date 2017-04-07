#ifndef RESOURCEMONITOR_PDHHELPERFUNCTIONS_H
#define RESOURCEMONITOR_PDHHELPERFUNCTIONS_H

#include <Pdh.h>//Link pdh.lib
#include <string>

using namespace std;

PDH_HCOUNTER AddSingleCounter(PDH_HQUERY query_handle, LPCWSTR query_str);
bool CollectQueryData(PDH_HQUERY query_handle);
DWORD GetCounterArray(PDH_HCOUNTER counters, DWORD format, PDH_FMT_COUNTERVALUE_ITEM** values_out);
DWORD GetCounterArrayRawValues(PDH_HCOUNTER counters, PDH_RAW_COUNTER_ITEM** values_out);
unsigned long long SumCounterArray(PDH_HCOUNTER counters);
DWORD FindIndexOfProcessWithHighestDouble(PDH_FMT_COUNTERVALUE_ITEM* processes, DWORD process_count);
DWORD FindIndexOfProcessWithHighestLongLong(PDH_FMT_COUNTERVALUE_ITEM* processes, DWORD process_count);

struct ProcessRaw {
	int PID;
	wstring name;
	PDH_RAW_COUNTER raw_cpu;//CPU %
	PDH_RAW_COUNTER raw_wio;//Write I/O bytes
	PDH_RAW_COUNTER raw_rio;//Read I/O bytes
	double cpu;
	long long wio;
	long long rio;
	long long tio;//Total rio + wio
	ProcessRaw();//Constructor
	void Copy(ProcessRaw* source);
	void ParseRawCounterName(wchar_t* szName);
};

DWORD ParsePIDFromRawCounterName(wchar_t* szName);
wstring ParseNameFromRawCounterName(wchar_t* szName);
int FindPIDInProcessRawArray(ProcessRaw* process_raw_array, int array_length, int PID);

bool RegistryIsSetForPIDs();
bool SetRegistryForPIDs();

#endif
