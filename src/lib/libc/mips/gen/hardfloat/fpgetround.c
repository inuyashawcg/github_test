/*	$NetBSD: fpgetround.c,v 1.5 2005/12/24 23:10:08 perry Exp $	*/

/*
 * Written by J.T. Conklin, Apr 11, 1995
 * Public domain.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: releng/12.0/lib/libc/mips/gen/hardfloat/fpgetround.c 201856 2010-01-08 23:50:39Z imp $");
#if defined(LIBC_SCCS) && !defined(lint)
__RCSID("$NetBSD: fpgetround.c,v 1.5 2005/12/24 23:10:08 perry Exp $");
#endif /* LIBC_SCCS and not lint */

#include "namespace.h"

#include <ieeefp.h>

#ifdef __weak_alias
__weak_alias(fpgetround,_fpgetround)
#endif

fp_rnd_t
fpgetround()
{
	int x;

	__asm("cfc1 %0,$31" : "=r" (x));
	return x & 0x03;
}
