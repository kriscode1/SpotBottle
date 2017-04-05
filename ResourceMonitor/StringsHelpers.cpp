#include "StringHelpers.h"
#include <locale>

void ConvertCStringToUpper(wchar_t* input) {
	//Replaces all lowercase characters in a null terminated wchar_t array with uppercase characters, dependent on the locale.
	std::locale loc;
	while (*input != 0) {
		*input = toupper(*input, loc);
		input++;
	}
}

bool StringsMatch(wchar_t* input1, wchar_t* input2) {
	//Returns true if two wchar_t strings compare the same, else false.
	if (wcscmp(input1, input2) == 0) return true;
	return false;
}

bool StringsMatch(std::wstring input1, std::wstring input2) {
	//Returns true if two wstrings compare the same, else false.
	if (input1.compare(input2) == 0) return true;
	return false;
}
