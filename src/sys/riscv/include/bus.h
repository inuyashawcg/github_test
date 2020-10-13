/*	$NetBSD: bus.h,v 1.11 2003/07/28 17:35:54 thorpej Exp $	*/

/*-
 * Copyright (c) 1996, 1997, 1998, 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1996 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1996 Christopher G. Demetriou.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * From: sys/arm/include/bus.h
 *
 * $FreeBSD: releng/12.0/sys/riscv/include/bus.h 292407 2015-12-17 18:44:30Z br $
 */

#ifndef _MACHINE_BUS_H_
#define	_MACHINE_BUS_H_

#include <machine/_bus.h>

#define	BUS_SPACE_ALIGNED_POINTER(p, t) ALIGNED_POINTER(p, t)

#define	BUS_SPACE_MAXADDR_24BIT	0xFFFFFFUL
#define	BUS_SPACE_MAXADDR_32BIT 0xFFFFFFFFUL
#define	BUS_SPACE_MAXSIZE_24BIT	0xFFFFFFUL
#define	BUS_SPACE_MAXSIZE_32BIT	0xFFFFFFFFUL

#define	BUS_SPACE_MAXADDR 	0xFFFFFFFFFFFFFFFFUL
#define	BUS_SPACE_MAXSIZE 	0xFFFFFFFFFFFFFFFFUL

#define	BUS_SPACE_MAP_CACHEABLE		0x01
#define	BUS_SPACE_MAP_LINEAR		0x02
#define	BUS_SPACE_MAP_PREFETCHABLE     	0x04

#define	BUS_SPACE_UNRESTRICTED	(~0)

#define	BUS_SPACE_BARRIER_READ	0x01
#define	BUS_SPACE_BARRIER_WRITE	0x02

/*
	bus_space功能允许设备驱动程序独立于机器访问总线内存和寄存器区域，其目的是为了让单个驱动程序源文件去
	操作不同系统架构上的一组设备，并且允许单个驱动程序文件操作单个架构上多个总线类型上的一组设备

	一个机器中可能含有多个bus space，它们通过tag来进行区分，例如memory space，I/O space等。私有的总线
	或者设备上可能会使用到多个bus space tag，不同的tags可能指向同一个bus space类型

	bus space的大小通过bus address和bus size来确定。bus address指明了space的起始地址，size指明了空间
	的字节数。不可字节寻址的总线可能需要使用具有适当对齐地址和适当舍入大小的总线空间范围

	通过使用总线空间句柄（bus space handle）可以方便地访问总线空间区域，这些句柄通常是通过映射特定范围的总线空间来创建的，
	句柄也可以通过分配和映射一系列总线空间来创建，总线空间的实际位置由实现在分配函数的调用者指定的范围内选择

	所有访问bus space的函数，都需要一个bus space tag参数，至少有一个句柄参数（handle）和至少一个偏移量参数（offset）
	bus space tag指定一个空间，handler指定空间中的一个区域，offset指定该区域中的所要访问的具体的位置；offset以字节来进行
	表示，但是bus一般会施加字节对齐的一些限制。offset指定的位置必须要在映射的空间区域之内，如果超出这个范围就会出错

	由于某些架构的内存系统使用缓冲来提高内存和设备访问性能，因此有一种机制可用于在总线空间读写流中创建“屏障”
	屏障总共有三种类型：read，write和read/write，在屏障之前开始的任何读取操作必须要在屏障开始之后的任何读取操作之前完成，
	写入操作也是如此；屏障开始之前的读/写操作则是要在屏障开始之后任何读或者写操作之前完成。正确写入的驱动程序都会包含合适的
	屏障，并且假设屏障的读写操作顺序已经明确

	试图编写具有总线空间功能的便携式驱动程序的人应该尽量对系统允许的内容做出最小的假设。特别是，他们应该期望系统要求被访问的
	总线空间地址自然对齐（即，添加到偏移量的句柄的基地址是访问大小的倍数），并且系统对指针进行对齐检查（即指向被读取和写入的
	对象的指针必须指向正确对齐的数据）。意思就是一定要满足系统的最低要求限制
*/
struct bus_space {
	/* cookie */
	void		*bs_cookie;

	/* 
		mapping/unmapping
		bus_addr_t: 表示bus的地址，必须是unsigned整型用来保存系统最大的地址范围，主要用于map和unmap
		bus_size_t: 表示bus space的大小，也必须是unsigned整型。几乎所有总线空间函数都使用这种类型，
					用于描述在执行空间访问操作时映射区域和偏移到区域时的大小
		bus_space_tag_t: 总线空间标记类型用于描述机器上的特定总线空间。它的内容依赖于机器，应该被机器无关的
					代码认为是不透明的。所有总线空间函数都使用此类型来命名它们所运行的空间。
		bus_space_handle_t： 总线空间句柄类型用于描述总线空间范围的映射。它的内容依赖于机器，应该被机器无关的
					代码认为是不透明的。此类型在执行总线空间访问操作时使用。
	*/
	int		(*bs_map) (void *, bus_addr_t, bus_size_t,
			    int, bus_space_handle_t *);
	void		(*bs_unmap) (void *, bus_space_handle_t, bus_size_t);
	int		(*bs_subregion) (void *, bus_space_handle_t,
			    bus_size_t, bus_size_t, bus_space_handle_t *);

	/* allocation/deallocation */
	int		(*bs_alloc) (void *, bus_addr_t, bus_addr_t,
			    bus_size_t, bus_size_t, bus_size_t, int,
			    bus_addr_t *, bus_space_handle_t *);
	void		(*bs_free) (void *, bus_space_handle_t,
			    bus_size_t);

	/* get kernel virtual address */
	/* barrier */
	void		(*bs_barrier) (void *, bus_space_handle_t,
			    bus_size_t, bus_size_t, int);

	/* read single */
	u_int8_t	(*bs_r_1) (void *, bus_space_handle_t, bus_size_t);
	u_int16_t	(*bs_r_2) (void *, bus_space_handle_t, bus_size_t);
	u_int32_t	(*bs_r_4) (void *, bus_space_handle_t, bus_size_t);
	u_int64_t	(*bs_r_8) (void *, bus_space_handle_t, bus_size_t);

	/* read multiple */
	void		(*bs_rm_1) (void *, bus_space_handle_t, bus_size_t,
	    u_int8_t *, bus_size_t);
	void		(*bs_rm_2) (void *, bus_space_handle_t, bus_size_t,
	    u_int16_t *, bus_size_t);
	void		(*bs_rm_4) (void *, bus_space_handle_t,
			    bus_size_t, u_int32_t *, bus_size_t);
	void		(*bs_rm_8) (void *, bus_space_handle_t,
			    bus_size_t, u_int64_t *, bus_size_t);
					
	/* read region */
	void		(*bs_rr_1) (void *, bus_space_handle_t,
			    bus_size_t, u_int8_t *, bus_size_t);
	void		(*bs_rr_2) (void *, bus_space_handle_t,
			    bus_size_t, u_int16_t *, bus_size_t);
	void		(*bs_rr_4) (void *, bus_space_handle_t,
			    bus_size_t, u_int32_t *, bus_size_t);
	void		(*bs_rr_8) (void *, bus_space_handle_t,
			    bus_size_t, u_int64_t *, bus_size_t);
					
	/* write single */
	void		(*bs_w_1) (void *, bus_space_handle_t,
			    bus_size_t, u_int8_t);
	void		(*bs_w_2) (void *, bus_space_handle_t,
			    bus_size_t, u_int16_t);
	void		(*bs_w_4) (void *, bus_space_handle_t,
			    bus_size_t, u_int32_t);
	void		(*bs_w_8) (void *, bus_space_handle_t,
			    bus_size_t, u_int64_t);

	/* write multiple */
	void		(*bs_wm_1) (void *, bus_space_handle_t,
			    bus_size_t, const u_int8_t *, bus_size_t);
	void		(*bs_wm_2) (void *, bus_space_handle_t,
			    bus_size_t, const u_int16_t *, bus_size_t);
	void		(*bs_wm_4) (void *, bus_space_handle_t,
			    bus_size_t, const u_int32_t *, bus_size_t);
	void		(*bs_wm_8) (void *, bus_space_handle_t,
			    bus_size_t, const u_int64_t *, bus_size_t);
					
	/* write region */
	void		(*bs_wr_1) (void *, bus_space_handle_t,
			    bus_size_t, const u_int8_t *, bus_size_t);
	void		(*bs_wr_2) (void *, bus_space_handle_t,
			    bus_size_t, const u_int16_t *, bus_size_t);
	void		(*bs_wr_4) (void *, bus_space_handle_t,
			    bus_size_t, const u_int32_t *, bus_size_t);
	void		(*bs_wr_8) (void *, bus_space_handle_t,
			    bus_size_t, const u_int64_t *, bus_size_t);

	/* set multiple */
	void		(*bs_sm_1) (void *, bus_space_handle_t,
			    bus_size_t, u_int8_t, bus_size_t);
	void		(*bs_sm_2) (void *, bus_space_handle_t,
			    bus_size_t, u_int16_t, bus_size_t);
	void		(*bs_sm_4) (void *, bus_space_handle_t,
			    bus_size_t, u_int32_t, bus_size_t);
	void		(*bs_sm_8) (void *, bus_space_handle_t,
			    bus_size_t, u_int64_t, bus_size_t);

	/* set region */
	void		(*bs_sr_1) (void *, bus_space_handle_t,
			    bus_size_t, u_int8_t, bus_size_t);
	void		(*bs_sr_2) (void *, bus_space_handle_t,
			    bus_size_t, u_int16_t, bus_size_t);
	void		(*bs_sr_4) (void *, bus_space_handle_t,
			    bus_size_t, u_int32_t, bus_size_t);
	void		(*bs_sr_8) (void *, bus_space_handle_t,
			    bus_size_t, u_int64_t, bus_size_t);

	/* copy */
	void		(*bs_c_1) (void *, bus_space_handle_t, bus_size_t,
			    bus_space_handle_t, bus_size_t, bus_size_t);
	void		(*bs_c_2) (void *, bus_space_handle_t, bus_size_t,
			    bus_space_handle_t, bus_size_t, bus_size_t);
	void		(*bs_c_4) (void *, bus_space_handle_t, bus_size_t,
			    bus_space_handle_t, bus_size_t, bus_size_t);
	void		(*bs_c_8) (void *, bus_space_handle_t, bus_size_t,
			    bus_space_handle_t, bus_size_t, bus_size_t);

	/* read single stream */
	u_int8_t	(*bs_r_1_s) (void *, bus_space_handle_t, bus_size_t);
	u_int16_t	(*bs_r_2_s) (void *, bus_space_handle_t, bus_size_t);
	u_int32_t	(*bs_r_4_s) (void *, bus_space_handle_t, bus_size_t);
	u_int64_t	(*bs_r_8_s) (void *, bus_space_handle_t, bus_size_t);

	/* 
		read multiple stream 
		大多数总线空间函数都包含主机字节顺序和总线字节顺序，并负责调用方的任何转换。然而，在某些情况下，硬件可以映射FIFO或其他一些内存区域，
		调用者可能希望对其使用多字、但未翻译的访问。访问这些类型的内存区域应该使用bus_space_u*\u stream_N（）函数。
	*/
	void		(*bs_rm_1_s) (void *, bus_space_handle_t, bus_size_t,
	    u_int8_t *, bus_size_t);
	void		(*bs_rm_2_s) (void *, bus_space_handle_t, bus_size_t,
	    u_int16_t *, bus_size_t);
	void		(*bs_rm_4_s) (void *, bus_space_handle_t,
			    bus_size_t, u_int32_t *, bus_size_t);
	void		(*bs_rm_8_s) (void *, bus_space_handle_t,
			    bus_size_t, u_int64_t *, bus_size_t);
					
	/* read region stream */
	void		(*bs_rr_1_s) (void *, bus_space_handle_t,
			    bus_size_t, u_int8_t *, bus_size_t);
	void		(*bs_rr_2_s) (void *, bus_space_handle_t,
			    bus_size_t, u_int16_t *, bus_size_t);
	void		(*bs_rr_4_s) (void *, bus_space_handle_t,
			    bus_size_t, u_int32_t *, bus_size_t);
	void		(*bs_rr_8_s) (void *, bus_space_handle_t,
			    bus_size_t, u_int64_t *, bus_size_t);
					
	/* write single stream */
	void		(*bs_w_1_s) (void *, bus_space_handle_t,
			    bus_size_t, u_int8_t);
	void		(*bs_w_2_s) (void *, bus_space_handle_t,
			    bus_size_t, u_int16_t);
	void		(*bs_w_4_s) (void *, bus_space_handle_t,
			    bus_size_t, u_int32_t);
	void		(*bs_w_8_s) (void *, bus_space_handle_t,
			    bus_size_t, u_int64_t);

	/* write multiple stream */
	void		(*bs_wm_1_s) (void *, bus_space_handle_t,
			    bus_size_t, const u_int8_t *, bus_size_t);
	void		(*bs_wm_2_s) (void *, bus_space_handle_t,
			    bus_size_t, const u_int16_t *, bus_size_t);
	void		(*bs_wm_4_s) (void *, bus_space_handle_t,
			    bus_size_t, const u_int32_t *, bus_size_t);
	void		(*bs_wm_8_s) (void *, bus_space_handle_t,
			    bus_size_t, const u_int64_t *, bus_size_t);
					
	/* write region stream */
	void		(*bs_wr_1_s) (void *, bus_space_handle_t,
			    bus_size_t, const u_int8_t *, bus_size_t);
	void		(*bs_wr_2_s) (void *, bus_space_handle_t,
			    bus_size_t, const u_int16_t *, bus_size_t);
	void		(*bs_wr_4_s) (void *, bus_space_handle_t,
			    bus_size_t, const u_int32_t *, bus_size_t);
	void		(*bs_wr_8_s) (void *, bus_space_handle_t,
			    bus_size_t, const u_int64_t *, bus_size_t);
};


/*
 * Utility macros; INTERNAL USE ONLY.
 */
#define	__bs_c(a,b)		__CONCAT(a,b)
#define	__bs_opname(op,size)	__bs_c(__bs_c(__bs_c(bs_,op),_),size)

#define	__bs_rs(sz, t, h, o)						\
	(*(t)->__bs_opname(r,sz))((t)->bs_cookie, h, o)
#define	__bs_ws(sz, t, h, o, v)						\
	(*(t)->__bs_opname(w,sz))((t)->bs_cookie, h, o, v)
#define	__bs_nonsingle(type, sz, t, h, o, a, c)				\
	(*(t)->__bs_opname(type,sz))((t)->bs_cookie, h, o, a, c)
#define	__bs_set(type, sz, t, h, o, v, c)				\
	(*(t)->__bs_opname(type,sz))((t)->bs_cookie, h, o, v, c)
#define	__bs_copy(sz, t, h1, o1, h2, o2, cnt)				\
	(*(t)->__bs_opname(c,sz))((t)->bs_cookie, h1, o1, h2, o2, cnt)

#define	__bs_opname_s(op,size)	__bs_c(__bs_c(__bs_c(__bs_c(bs_,op),_),size),_s)
#define	__bs_rs_s(sz, t, h, o)						\
	(*(t)->__bs_opname_s(r,sz))((t)->bs_cookie, h, o)
#define	__bs_ws_s(sz, t, h, o, v)					\
	(*(t)->__bs_opname_s(w,sz))((t)->bs_cookie, h, o, v)
#define	__bs_nonsingle_s(type, sz, t, h, o, a, c)			\
	(*(t)->__bs_opname_s(type,sz))((t)->bs_cookie, h, o, a, c)


/*
 * Mapping and unmapping operations.
 * bus space必须在map之后才能被使用，当它不再被需要的时候，就要及时unmap
 * 一些驱动程序需要能够将已经映射的总线空间的子区域传递给驱动程序中的另一个驱动程序或模块，
 * bus_space_subregion允许创建这样的子区域，共享？？
 */

/*
* 映射函数原型 - bus_space_map(space,	address, size, flags, handlep)
* 函数的作用是映射由space、address和size参数命名的总线空间区域，如果映射成功则返回0，
* 并且将bus space handle传递给handlep用于访问映射区域；如果映射不成功的话就会返回一个
* 非零错误码，此时hanglep将处于未定义的状态
* flag则是控制空间如何被映射，取值主要是有两个：
* BUS_SPACE_MAP_CACHEABLE：尝试映射空间，以便系统可以缓存和/或预取访问。如果未指定此标志，
* 							则实现应映射空间，以使其不会被缓存或预取。
* BUS_SPACE_MAP_LINEAR： 尝试映射空间，以便可以通过正常的内存访问方法（例如指针取消引用和结构访问）线性访问其内容。
* 						  当软件要直接访问存储设备（例如帧缓冲区）时，这很有用。如果指定了此标志且无法进行线性映射，
* 						  则bus_space_map调用应失败。如果未指定此标志，则系统可能会以最方便的方式映射空间。
* 两种类型取值在不同系统中有着不同的用法，有的系统需要两者结合，而有的则不支持
* 一些功能函数会去跟踪bus space的使用状况，防止同一个空间被重复分配。对于没有特定于插槽空间寻址（例如ISA）概念的总线空间，
* 以及与这些空间共存的空间（例如PCI内存和I/O空间与ISA内存和I/O空间共存）鼓励使用此方法
* 
* 一些映射区域可能并没有关联到某个设备，如果访问了到了这些区域，那么返回的结果将取决与总线
*/
#define	bus_space_map(t, a, s, c, hp)					\
	(*(t)->bs_map)((t)->bs_cookie, (a), (s), (c), (hp))

#define	bus_space_unmap(t, h, s)					\
	(*(t)->bs_unmap)((t)->bs_cookie, (h), (s))

/*
	bus_space_subregion(space, handle, offset, size, nhandlep)
	bus_space_subdivision（）函数是一个方便的函数，它为总线空间的已映射区域的某个子区域创建新的句柄。
	被新的handle指定的区域从handle指定的区域中偏移offset的位置开始，大小是size，这块区域要完全包含在
	原有的区域范围内。操作成功，函数会返回0，并且新区域的句柄传递给nhandlep，失败的话就会返回非零错误码，
	并且nhandlep将会处于未定义的状态
	处理bus_space_subregion句柄的做法一般是丢弃，绝对不能在该句柄上使用umap函数，否则将混淆bus space的
	资源管理，造成未定义的行为
*/
#define	bus_space_subregion(t, h, o, s, hp)				\
	(*(t)->bs_subregion)((t)->bs_cookie, (h), (o), (s), (hp))


/*
 * Allocation and deallocation operations.
 * bus_space_alloc(space, reg_start, reg_end, size, alignment, boundary,
     flags, addrp, handlep)
	有些设备需要或允许操作系统分配总线空间，以供设备使用。bus_space_alloc提供着了这样的功能
	函数的作用是根据给定的约束(start, end, boundary， alignment)，以大小给定的大小分配和映射总线空间的区域
 */
#define	bus_space_alloc(t, rs, re, s, a, b, c, ap, hp)			\
	(*(t)->bs_alloc)((t)->bs_cookie, (rs), (re), (s), (a), (b),	\
	    (c), (ap), (hp))

/*
	bus_space_free(space, handle, size)
*/
#define	bus_space_free(t, h, s)						\
	(*(t)->bs_free)((t)->bs_cookie, (h), (s))

/*
 * Bus barrier operations.
 */
#define	bus_space_barrier(t, h, o, l, f)				\
	(*(t)->bs_barrier)((t)->bs_cookie, (h), (o), (l), (f))



/*
 * Bus read (single) operations.
 * - bus_space_read_1(space, handle, offset)
 * 	bus_space_read_N函数系列将1、2、4或8字节的数据项从offset指定的偏移量读入由space指定的总线空间句柄指定的区域。
 * 	为了便于移植，handle指定的区域的起始地址加上偏移量应该是被读取数据项大小的倍数。在某些系统中，不遵守此要求可能会导致
 * 	读取不正确的数据，而在另一些系统上则可能导致系统崩溃。
 * 	由bus_space_Read_N函数完成的读取操作可能会与其他挂起的读写操作无序执行，除非使用bus_space_barrier函数强制执行顺序
 */
#define	bus_space_read_1(t, h, o)	__bs_rs(1,(t),(h),(o))
#define	bus_space_read_2(t, h, o)	__bs_rs(2,(t),(h),(o))
#define	bus_space_read_4(t, h, o)	__bs_rs(4,(t),(h),(o))
#define	bus_space_read_8(t, h, o)	__bs_rs(8,(t),(h),(o))

#define	bus_space_read_stream_1(t, h, o)        __bs_rs_s(1,(t), (h), (o))
#define	bus_space_read_stream_2(t, h, o)        __bs_rs_s(2,(t), (h), (o))
#define	bus_space_read_stream_4(t, h, o)        __bs_rs_s(4,(t), (h), (o))
#define	bus_space_read_stream_8(t, h, o)	__bs_rs_s(8,8,(t),(h),(o))

/*
 * Bus read multiple operations.
 */
#define	bus_space_read_multi_1(t, h, o, a, c)				\
	__bs_nonsingle(rm,1,(t),(h),(o),(a),(c))
#define	bus_space_read_multi_2(t, h, o, a, c)				\
	__bs_nonsingle(rm,2,(t),(h),(o),(a),(c))
#define	bus_space_read_multi_4(t, h, o, a, c)				\
	__bs_nonsingle(rm,4,(t),(h),(o),(a),(c))
#define	bus_space_read_multi_8(t, h, o, a, c)				\
	__bs_nonsingle(rm,8,(t),(h),(o),(a),(c))

#define	bus_space_read_multi_stream_1(t, h, o, a, c)			\
	__bs_nonsingle_s(rm,1,(t),(h),(o),(a),(c))
#define	bus_space_read_multi_stream_2(t, h, o, a, c)			\
	__bs_nonsingle_s(rm,2,(t),(h),(o),(a),(c))
#define	bus_space_read_multi_stream_4(t, h, o, a, c)			\
	__bs_nonsingle_s(rm,4,(t),(h),(o),(a),(c))
#define	bus_space_read_multi_stream_8(t, h, o, a, c)			\
	__bs_nonsingle_s(rm,8,(t),(h),(o),(a),(c))


/*
 * Bus read region operations.
 */
#define	bus_space_read_region_1(t, h, o, a, c)				\
	__bs_nonsingle(rr,1,(t),(h),(o),(a),(c))
#define	bus_space_read_region_2(t, h, o, a, c)				\
	__bs_nonsingle(rr,2,(t),(h),(o),(a),(c))
#define	bus_space_read_region_4(t, h, o, a, c)				\
	__bs_nonsingle(rr,4,(t),(h),(o),(a),(c))
#define	bus_space_read_region_8(t, h, o, a, c)				\
	__bs_nonsingle(rr,8,(t),(h),(o),(a),(c))

#define	bus_space_read_region_stream_1(t, h, o, a, c)			\
	__bs_nonsingle_s(rr,1,(t),(h),(o),(a),(c))
#define	bus_space_read_region_stream_2(t, h, o, a, c)			\
	__bs_nonsingle_s(rr,2,(t),(h),(o),(a),(c))
#define	bus_space_read_region_stream_4(t, h, o, a, c)			\
	__bs_nonsingle_s(rr,4,(t),(h),(o),(a),(c))
#define	bus_space_read_region_stream_8(t, h, o, a, c)			\
	__bs_nonsingle_s(rr,8,(t),(h),(o),(a),(c))


/*
 * Bus write (single) operations.
 */
#define	bus_space_write_1(t, h, o, v)	__bs_ws(1,(t),(h),(o),(v))
#define	bus_space_write_2(t, h, o, v)	__bs_ws(2,(t),(h),(o),(v))
#define	bus_space_write_4(t, h, o, v)	__bs_ws(4,(t),(h),(o),(v))
#define	bus_space_write_8(t, h, o, v)	__bs_ws(8,(t),(h),(o),(v))

#define	bus_space_write_stream_1(t, h, o, v)	__bs_ws_s(1,(t),(h),(o),(v))
#define	bus_space_write_stream_2(t, h, o, v)	__bs_ws_s(2,(t),(h),(o),(v))
#define	bus_space_write_stream_4(t, h, o, v)	__bs_ws_s(4,(t),(h),(o),(v))
#define	bus_space_write_stream_8(t, h, o, v)	__bs_ws_s(8,(t),(h),(o),(v))


/*
 * Bus write multiple operations.
 */
#define	bus_space_write_multi_1(t, h, o, a, c)				\
	__bs_nonsingle(wm,1,(t),(h),(o),(a),(c))
#define	bus_space_write_multi_2(t, h, o, a, c)				\
	__bs_nonsingle(wm,2,(t),(h),(o),(a),(c))
#define	bus_space_write_multi_4(t, h, o, a, c)				\
	__bs_nonsingle(wm,4,(t),(h),(o),(a),(c))
#define	bus_space_write_multi_8(t, h, o, a, c)				\
	__bs_nonsingle(wm,8,(t),(h),(o),(a),(c))

#define	bus_space_write_multi_stream_1(t, h, o, a, c)			\
	__bs_nonsingle_s(wm,1,(t),(h),(o),(a),(c))
#define	bus_space_write_multi_stream_2(t, h, o, a, c)			\
	__bs_nonsingle_s(wm,2,(t),(h),(o),(a),(c))
#define	bus_space_write_multi_stream_4(t, h, o, a, c)			\
	__bs_nonsingle_s(wm,4,(t),(h),(o),(a),(c))
#define	bus_space_write_multi_stream_8(t, h, o, a, c)			\
	__bs_nonsingle_s(wm,8,(t),(h),(o),(a),(c))


/*
 * Bus write region operations.
 */
#define	bus_space_write_region_1(t, h, o, a, c)				\
	__bs_nonsingle(wr,1,(t),(h),(o),(a),(c))
#define	bus_space_write_region_2(t, h, o, a, c)				\
	__bs_nonsingle(wr,2,(t),(h),(o),(a),(c))
#define	bus_space_write_region_4(t, h, o, a, c)				\
	__bs_nonsingle(wr,4,(t),(h),(o),(a),(c))
#define	bus_space_write_region_8(t, h, o, a, c)				\
	__bs_nonsingle(wr,8,(t),(h),(o),(a),(c))

#define	bus_space_write_region_stream_1(t, h, o, a, c)			\
	__bs_nonsingle_s(wr,1,(t),(h),(o),(a),(c))
#define	bus_space_write_region_stream_2(t, h, o, a, c)			\
	__bs_nonsingle_s(wr,2,(t),(h),(o),(a),(c))
#define	bus_space_write_region_stream_4(t, h, o, a, c)			\
	__bs_nonsingle_s(wr,4,(t),(h),(o),(a),(c))
#define	bus_space_write_region_stream_8(t, h, o, a, c)			\
	__bs_nonsingle_s(wr,8,(t),(h),(o),(a),(c))


/*
 * Set multiple operations.
 */
#define	bus_space_set_multi_1(t, h, o, v, c)				\
	__bs_set(sm,1,(t),(h),(o),(v),(c))
#define	bus_space_set_multi_2(t, h, o, v, c)				\
	__bs_set(sm,2,(t),(h),(o),(v),(c))
#define	bus_space_set_multi_4(t, h, o, v, c)				\
	__bs_set(sm,4,(t),(h),(o),(v),(c))
#define	bus_space_set_multi_8(t, h, o, v, c)				\
	__bs_set(sm,8,(t),(h),(o),(v),(c))


/*
 * Set region operations.
 */
#define	bus_space_set_region_1(t, h, o, v, c)				\
	__bs_set(sr,1,(t),(h),(o),(v),(c))
#define	bus_space_set_region_2(t, h, o, v, c)				\
	__bs_set(sr,2,(t),(h),(o),(v),(c))
#define	bus_space_set_region_4(t, h, o, v, c)				\
	__bs_set(sr,4,(t),(h),(o),(v),(c))
#define	bus_space_set_region_8(t, h, o, v, c)				\
	__bs_set(sr,8,(t),(h),(o),(v),(c))


/*
 * Copy operations.
 */
#define	bus_space_copy_region_1(t, h1, o1, h2, o2, c)				\
	__bs_copy(1, t, h1, o1, h2, o2, c)
#define	bus_space_copy_region_2(t, h1, o1, h2, o2, c)				\
	__bs_copy(2, t, h1, o1, h2, o2, c)
#define	bus_space_copy_region_4(t, h1, o1, h2, o2, c)				\
	__bs_copy(4, t, h1, o1, h2, o2, c)
#define	bus_space_copy_region_8(t, h1, o1, h2, o2, c)				\
	__bs_copy(8, t, h1, o1, h2, o2, c)

#include <machine/bus_dma.h>

#endif /* _MACHINE_BUS_H_ */
