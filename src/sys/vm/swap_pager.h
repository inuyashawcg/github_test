/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990 University of Utah.
 * Copyright (c) 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)swap_pager.h	7.1 (Berkeley) 12/5/90
 * $FreeBSD: releng/12.0/sys/vm/swap_pager.h 326023 2017-11-20 19:43:44Z pfg $
 */

#ifndef	_VM_SWAP_PAGER_H_
#define	_VM_SWAP_PAGER_H_ 1

typedef	int32_t	swblk_t;	/*
				 * swap offset.  This is the type used to
				 * address the "virtual swap device" and
				 * therefore the maximum swap space is
				 * 2^32 pages.
				 */

struct buf;
struct swdevt;
typedef void sw_strategy_t(struct buf *, struct swdevt *);
typedef void sw_close_t(struct thread *, struct swdevt *);

/*
 * Swap device table
		通常情况下表示的是一个字符设备，操作系统的 swap 区域并不一定只在同一个设备上，
		应该也是可以分布在不同的设备之上的。每个设备就利用下面的结构体来抽象，所有设备
		共同组成操作系统的 swap region
 */
struct swdevt {
	int	sw_flags;
	int	sw_nblks;
	int     sw_used;
	dev_t	sw_dev;
	struct vnode *sw_vp;
	void	*sw_id;	// 表示的可能是一个 g_consumer 对象指针
	/*
		每个 swap device 组成了操作系统整个 swap region，所以每个设备都应该对应其中的
		一段区域。sw_first 表示的可能就是该 swap device 所代表区域的起始位置，end 的话
		表示的是结束位置
	*/
	swblk_t	sw_first;
	swblk_t	sw_end;
	struct blist *sw_blist;
	/*
		所有的 swap device 应该也是通过一个全局链表来管理的
	*/
	TAILQ_ENTRY(swdevt)	sw_list;
	sw_strategy_t		*sw_strategy;
	sw_close_t		*sw_close;
};

#define	SW_UNMAPPED	0x01
#define	SW_CLOSING	0x04

#ifdef _KERNEL

extern int swap_pager_avail;

struct xswdev;
int swap_dev_info(int name, struct xswdev *xs, char *devname, size_t len);
void swap_pager_copy(vm_object_t, vm_object_t, vm_pindex_t, int);
vm_pindex_t swap_pager_find_least(vm_object_t object, vm_pindex_t pindex);
void swap_pager_freespace(vm_object_t, vm_pindex_t, vm_size_t);
void swap_pager_swap_init(void);
int swap_pager_nswapdev(void);
int swap_pager_reserve(vm_object_t, vm_pindex_t, vm_size_t);
void swap_pager_status(int *total, int *used);
void swapoff_all(void);

#endif				/* _KERNEL */
#endif				/* _VM_SWAP_PAGER_H_ */
