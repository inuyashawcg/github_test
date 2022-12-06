#include "c_cpp_shared.h"

int main() {
  char buffer[BLOCK_SIZE];
  int ipref = 0, 
      start = ipref / NBBY,
      len = howmany(BLOCK_SIZE * NBBY - ipref, NBBY);
  memset(buffer, 0x00, BLOCK_SIZE);

  // intialize buffer.
  buffer[0] = 0xff;
  buffer[1] = 0xff;
  buffer[2] = 0x03;

  char *loc = memcchr(&buffer[start], 0xff, len);
  if (loc == NULL) {
		len = start + 1;
		start = 0;
		loc = memcchr(&buffer[start], 0xff, len);
		if (loc == NULL) {
			printf("map corrupted\n");
		}
	}
	ipref = (loc - buffer) * NBBY + ffs(~*loc) - 1;

  printf("loc - buffer = %ld\n", loc - buffer);
  printf("~*loc = %d\n", (~*loc));
  printf("ffs return value is: %d\n",  ffs(~*loc));
  printf("ipref is: %d\n", ipref);
  return 0;
}