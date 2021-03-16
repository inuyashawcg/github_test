/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Poul-Henning Kamp
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: releng/12.0/sys/sys/_unrhdr.h 326256 2017-11-27 15:01:59Z pfg $
 */

#ifndef _SYS_UNRHDR_H
#define _SYS_UNRHDR_H

#include <sys/queue.h>

struct mtx;

/* Header element for a unr number space. 
	从定义来看，应该是管理内存分配相关的结构体
*/

struct unrhdr {
	TAILQ_HEAD(unrhd,unr)	head;
	u_int			low;	/* Lowest item */
	u_int			high;	/* Highest item */
	u_int			busy;	/* Count of allocated items 已经分配的项目计数 */
	u_int			alloc;	/* Count of memory allocations 内存分配计数 */
	u_int			first;	/* items in allocated from start 分配的第一个项目 */
	u_int			last;	/* items free at end 在最后释放的项目 */
	struct mtx		*mtx;
	TAILQ_HEAD(unrfr,unr)	ppfree;	/* Items to be freed after mtx
					   lock dropped mtx锁丢失后要释放的项目 */
};

#endif
