/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Attilio Rao <attilio@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice(s), this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified other than the possible 
 *    addition of one or more copyright notices.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice(s), this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * $FreeBSD: releng/12.0/sys/sys/_lockmgr.h 326256 2017-11-27 15:01:59Z pfg $
 */

#ifndef _SYS__LOCKMGR_H_
#define	_SYS__LOCKMGR_H_

#ifdef DEBUG_LOCKS
#include <sys/_stack.h>
#endif

/*
	lock_object 则可以认为是一个类对象。lk_lock 表示的就是锁操作对应的变量。
	锁机制就类似与原子操作，假设当锁值为0的时候，表示该锁已经释放，目前可以被获取。假设值为1的时候，锁
	已经被占有，其他模块需要申请的时候就要等待，当它的值再次变成0的时候就表示可以获取到了
*/
struct lock {
	struct lock_object	lock_object;
	volatile uintptr_t	lk_lock;
	u_int			lk_exslpfail;
	int			lk_timo;	// lock timeout?
	int			lk_pri;
	// 如果要调试锁的话，需要为其分配一个堆栈
#ifdef DEBUG_LOCKS
	struct stack		lk_stack;
#endif
};

#endif
