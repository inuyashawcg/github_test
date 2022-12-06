#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifndef __DECONST
#  define	__DECONST(type, var)	((type)(uintptr_t)(const void *)(var))
#endif

#ifndef howmany
# define howmany(x, y)  (((x) + ((y) - 1)) / (y))
#endif

#define	LONGPTR_MASK (sizeof(long) - 1)
#define	TESTBYTE				\
	do {					\
		if (*p != (unsigned char)c)	\
			goto done;		\
		p++;				\
	} while (0)

#define BLOCK_SIZE 4096
#define NBBY 8

void * memcchr(const void *begin, int c, size_t n)
{
	const unsigned long *lp;
	const unsigned char *p, *end;
	unsigned long word;

	/* Four or eight repetitions of `c'. */
	word = (unsigned char)c;
	word |= word << 8;
	word |= word << 16;
#if LONG_BIT >= 64
	word |= word << 32;
#endif

	/* Don't perform memory I/O when passing a zero-length buffer. */
	if (n == 0)
		return (NULL);

	/*
	 * First determine whether there is a character unequal to `c'
	 * in the first word.  As this word may contain bytes before
	 * `begin', we may execute this loop spuriously.
	 */
	lp = (const unsigned long *)((uintptr_t)begin & ~LONGPTR_MASK);
	end = (const unsigned char *)begin + n;
	if (*lp++ != word)
		for (p = (const unsigned char*)begin; p < (const unsigned char *)lp;)
			TESTBYTE;

	/* Now compare the data one word at a time. */
	for (; (const unsigned char *)lp < end; lp++) {
		if (*lp != word) {
			p = (const unsigned char *)lp;
			TESTBYTE;
			TESTBYTE;
			TESTBYTE;
#if LONG_BIT >= 64
			TESTBYTE;
			TESTBYTE;
			TESTBYTE;
			TESTBYTE;
#endif
			goto done;
		}
	}

	return (NULL);

done:
	/*
	 * If the end of the buffer is not word aligned, the previous
	 * loops may obtain an address that's beyond the end of the
	 * buffer.
	 */
	if (p < end)
		return (__DECONST(void *, p));
	return (NULL);
}