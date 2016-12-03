#ifndef RESOURCEMONITOR_PDHHELPERFUNCTIONS_H
#define RESOURCEMONITOR_PDHHELPERFUNCTIONS_H

#include <Pdh.h>
//Link pdh.lib

PDH_HCOUNTER AddSingleCounter(PDH_HQUERY query_handle, LPCWSTR query_str);
bool CollectQueryData(PDH_HQUERY query_handle);
DWORD GetCounterArray(PDH_HCOUNTER counters, DWORD format, PDH_FMT_COUNTERVALUE_ITEM** values_out);
unsigned long long SumCounterArray(PDH_HCOUNTER counters);

#endif
