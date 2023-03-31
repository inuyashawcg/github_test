#include <iostream>
#include <string.h>

#define	setbit(a,i)	(((unsigned char *)(a))[(i)/NBBY] |= 1<<((i)%NBBY))
#define	clrbit(a,i)	(((unsigned char *)(a))[(i)/NBBY] &= ~(1<<((i)%NBBY)))
#define	isset(a,i)							\
	(((const unsigned char *)(a))[(i)/NBBY] & (1<<((i)%NBBY)))
#define	isclr(a,i)							\
	((((const unsigned char *)(a))[(i)/NBBY] & (1<<((i)%NBBY))) == 0)

#define howmany(x, y)  (((x) + ((y) - 1)) / (y))
#define NBBY 8

/*
 * Find Last Set bit
 */
int
fls(int mask)
{
	int bit;

	if (mask == 0)
		return (0);
	for (bit = 1; mask != 1; bit++)
		mask = (unsigned int)mask >> 1;
	return (bit);
}

int findLastSetBit(int Mask) {
    int bit;

    if (Mask == 0)
        return (0);
    for (bit = 1; Mask != 1; bit++)
        Mask = (static_cast<unsigned int>(Mask)) >> 1;
    return (bit);
}

int findFirstSetBit(int Mask) {
    int bit;

    if (Mask == 0)
        return (0);
    for (bit = 1; !(Mask & 1); bit++)
        Mask = (static_cast<unsigned int>(Mask)) >> 1;
    return (bit);
}

int64_t alloc_block(void) {
    int64_t runstart = 0, runlen = 0, bno = 0;
    int loc, start, end, bit;
    uint8_t bbp[2] = {0xf3, 0xff};

    start = 0;
    end = howmany(16, NBBY);
    std::cout << "end is " << end << std::endl;

    for (loc = start; loc < end; loc++) {
		if (bbp[loc] == (unsigned char)0xff) {
			runlen = 0;
			continue;
		}
		/* Start of a run, find the number of high clear bits. */
		if (runlen == 0) {
			bit = findLastSetBit(bbp[loc]);
			runlen = NBBY - bit;
			runstart = loc * NBBY + bit;
		} else if (bbp[loc] == 0) {
			/* Continue a run. */
			runlen += NBBY;
		} else {
			/*
			 * Finish the current run.  If it isn't long
			 * enough, start a new one.
			 */
			bit = findFirstSetBit(bbp[loc]) - 1;
			runlen += bit;
			if (runlen >= 8) {
				bno = runstart;
				goto gotit;
			}

			/* Run was too short, start a new one. */
			bit = findLastSetBit(bbp[loc]);
			runlen = NBBY - bit;
			runstart = loc * NBBY + bit;
		}

		/* If the current run is long enough, use it. */
		if (runlen >= 8) {
			bno = runstart;
			goto gotit;
		}
	}

gotit:
    std::cout << "block number is " << bno << std::endl;
    return 0;
}

int main() {
    int val = 0x3f;
    // int ret1 = findFirstSetBit(val);
    // int ret2 = findLastSetBit(val);

    // ret1 = fls(val);
    // alloc_block();

    // std::cout << "ret1 is " << ret1 << "\n"
    //           << "ret2 is " << ret2 << std::endl;

    // test setbit and clrbit.
    // void* buffer = malloc(2);
    // memset(buffer, 0, 2);
    // setbit(buffer, 2);
    // for (int i = 0; i < 2; i++) {
    //     std::cout << reinterpret_cast<uint8_t*>(buffer)[i] << std::endl;
    // }

    int bno = ffs(~val) - 1;
    std::cout << bno << std::endl;
    return 0;
}