#	From: @(#)Makefile	8.3 (Berkeley) 1/25/94
# $FreeBSD: releng/12.0/usr.sbin/mountd/Makefile 314659 2017-03-04 11:38:03Z ngie $

PROG=	mountd
SRCS=	mountd.c getmntopts.c
MAN=	exports.5 netgroup.5 mountd.8

MOUNT=  ${SRCTOP}/sbin/mount
CFLAGS+= -I${MOUNT}
WARNS?= 2

.PATH: ${MOUNT}

LIBADD=	util

.include <bsd.prog.mk>
