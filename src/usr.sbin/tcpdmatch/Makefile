# $FreeBSD: releng/12.0/usr.sbin/tcpdmatch/Makefile 314458 2017-03-01 04:17:18Z ngie $

.include <src.opts.mk>

.PATH: ${SRCTOP}/contrib/tcp_wrappers

PROG=	tcpdmatch
MAN=	tcpdmatch.8
SRCS=	tcpdmatch.c fakelog.c inetcf.c scaffold.c

CFLAGS+=-DREAL_DAEMON_DIR=\"${LIBEXECDIR}\" \
	-DSEVERITY=LOG_INFO -DRFC931_TIMEOUT=10
.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+=-DINET6
.endif

WARNS?=	0

LIBADD=	wrap

.include <bsd.prog.mk>
