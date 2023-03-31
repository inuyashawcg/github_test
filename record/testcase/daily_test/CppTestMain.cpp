// #include "CppTest.h"

// using namespace CppTestCase;

#include <iostream>

// int matchComponent(const char *Path, const char *Component) {
// 	for (;;Path++, Component++) {
// 		if (*Path != *Component) {
// 			if (*Path == '/' && *Component == '\0')
// 				return 1;
// 			else
// 				return 0;
// 		} else if (*Path == '\0')
// 			return 1;
// 	}
// }

// int
// kc_fls(int mask)
// {
// 	int bit;

// 	if (mask == 0)
// 		return (0);
// 	for (bit = 1; mask != 1; bit++)
// 		mask = (unsigned int)mask >> 1;
// 	return (bit);
// }

int main() {
//   const char* a = "ttyu0";
//   const char* b = "ttyu0.init";

//   int ret = matchComponent(a, b);
//   std::cout << "ret is " << ret << std::endl;
	// int a = 0x0b;
	// int number = kc_fls(a);
	uint64_t number = 1;
	number <<= 16;

	std::cout << number << std::endl;
  	return 0;
}