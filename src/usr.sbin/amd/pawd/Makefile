# ex:ts=8
#
# Makefile for amd
# 	This file is under a "BSD" copyright (c) by David O'Brien 1998
#
# $FreeBSD: releng/12.0/usr.sbin/amd/pawd/Makefile 305787 2016-09-14 01:47:01Z cy $

.PATH:	${SRCTOP}/contrib/amd/amq

BINDIR=	/usr/bin

PROG=	pawd
SRCS=	pawd.c amq_clnt.c amq_xdr.c

CFLAGS+= -I${SRCTOP}/contrib/amd/amq

LIBADD=	amu

.include <bsd.prog.mk>
