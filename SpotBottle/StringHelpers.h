//Functions for manipulating strings.

#ifndef RESOURCEMONITOR_STRINGHELPERS_H
#define RESOURCEMONITOR_STRINGHELPERS_H

#include <string>

void ConvertCStringToUpper(wchar_t* input);
bool StringsMatch(wchar_t* input1, wchar_t* input2);
bool StringsMatch(std::wstring input1, std::wstring input2);

#endif
