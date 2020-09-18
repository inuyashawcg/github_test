# $FreeBSD: releng/12.0/lib/libc/powerpcspe/Makefile.inc 314557 2017-03-02 17:07:28Z brooks $

CFLAGS+= -I${LIBC_SRCTOP}/powerpc
SRCS+=	trivial-vdso_tc.c

# Long double is 64-bits
SRCS+=machdep_ldisd.c
SYM_MAPS+=${LIBC_SRCTOP}/powerpc/Symbol.map