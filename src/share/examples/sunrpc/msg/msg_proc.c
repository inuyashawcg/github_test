/* @(#)msg_proc.c	2.1 88/08/11 4.0 RPCSRC */
/* $FreeBSD: releng/12.0/share/examples/sunrpc/msg/msg_proc.c 327366 2017-12-29 23:58:05Z eadler $ */
/*
 * msg_proc.c: implementation of the remote procedure "printmessage"
 */
#include <paths.h>
#include <stdio.h>
#include <rpc/rpc.h>	/* always need this here */
#include "msg.h"	/* need this too: msg.h will be generated by rpcgen */

/*
 * Remote version of "printmessage"
 */
int *
printmessage_1(msg)
	char **msg;
{
	static int result; /* must be static! */
	FILE *f;

	f = fopen(_PATH_CONSOLE, "w");
	if (f == NULL) {
		result = 0;
		return (&result);
	}
	fprintf(f, "%s\n", *msg);
	fclose(f);
	result = 1;
	return (&result);
}
