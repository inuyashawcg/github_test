#include <iostream>
extern "C" {
  #include "c_cpp_shared.h"
}

int findFirstSetBit(int Mask) {
  int bit;

	if (Mask == 0)
		return (0);
	for (bit = 1; !(Mask & 1); bit++)
		Mask = static_cast<unsigned int>(Mask) >> 1;
	return (bit);
}

int main() {
  char buffer[BLOCK_SIZE];
  int InodeNumPreferred = 0,
      set_start = InodeNumPreferred / NBBY,
      length = howmany(BLOCK_SIZE * NBBY - InodeNumPreferred, NBBY);

  memset(buffer, 0x00, BLOCK_SIZE);
  buffer[0] = 0xff;
  buffer[1] = 0xff;
  buffer[2] = 0x03;

  char* location = static_cast<char*>(memcchr(&buffer[set_start], 0xff, length));
  if (location == NULL) {
    length = set_start + 1;
    set_start = 0;
    location = static_cast<char*>(memcchr(&buffer[set_start], 0xff, length));
    if (location == NULL) {
      std::cout << "map corrupted" << std::endl;
      return 0;
    }
  }
  InodeNumPreferred = (location - buffer) * NBBY + findFirstSetBit(~*location) - 1;

  std::cout << "location - buffer = " << location - buffer << '\n'
            << "~*location = " << ~*location << '\n'
            << "findFirstSetBit() = " << findFirstSetBit(~*location) << '\n'
            << "InodeNumPreferred = " << InodeNumPreferred
            << std::endl;

  return 0;
}