/*	$NetBSD: bus.h,v 1.12 1997/10/01 08:25:15 fvdl Exp $	*/

/*-
 * SPDX-License-Identifier: (BSD-2-Clause-NetBSD AND BSD-4-Clause)
 *
 * Copyright (c) 1996, 1997 The NetBSD Foundation, Inc.
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
 */
/* $FreeBSD: releng/12.0/sys/sys/bus_dma.h 328945 2018-02-06 19:14:15Z bz $ */

#ifndef _BUS_DMA_H_
#define _BUS_DMA_H_

#include <sys/_bus_dma.h>

/*
 * Machine independent interface for mapping physical addresses to peripheral
 * bus 'physical' addresses, and assisting with DMA operations.
 *
 * XXX This file is always included from <machine/bus_dma.h> and should not
 *     (yet) be included directly.
 */

/*
 * Flags used in various bus DMA methods.
 */
#define	BUS_DMA_WAITOK		0x00	/* safe to sleep (pseudo-flag) */
#define	BUS_DMA_NOWAIT		0x01	/* not safe to sleep */
#define	BUS_DMA_ALLOCNOW	0x02	/* perform resource allocation now */
#define	BUS_DMA_COHERENT	0x04	/* hint: map memory in a coherent(连贯的) way */
#define	BUS_DMA_ZERO		0x08	/* allocate zero'ed memory */
#define	BUS_DMA_BUS1		0x10	/* placeholders for bus functions... */
#define	BUS_DMA_BUS2		0x20
#define	BUS_DMA_BUS3		0x40
#define	BUS_DMA_BUS4		0x80

/*
 * The following two flags are non-standard or specific to only certain
 * architectures 以下两个标志是非标准的或仅特定于某些体系结构
 */
#define	BUS_DMA_NOWRITE		0x100
#define	BUS_DMA_NOCACHE		0x200

/*
 * The following flag is a DMA tag hint that the page offset of the
 * loaded kernel virtual address must be preserved in the first
 * physical segment address, when the KVA is loaded into DMA.
 * 下面的标志是一个DMA标记，提示当KVA加载到DMA中时，可加载的内核虚拟地址的页偏移量必须
 * 保留在第一个物理段地址中
 */
#define	BUS_DMA_KEEP_PG_OFFSET	0x400

#define	BUS_DMA_LOAD_MBUF	0x800

/* Forwards needed by prototypes below. */
union ccb;
struct bio;
struct mbuf;
struct memdesc;
struct pmap;
struct uio;

/*
 * Operations performed by bus_dmamap_sync().
 */
#define	BUS_DMASYNC_PREREAD	1
#define	BUS_DMASYNC_POSTREAD	2
#define	BUS_DMASYNC_PREWRITE	4
#define	BUS_DMASYNC_POSTWRITE	8

/*
 *	bus_dma_segment_t
 *
 *	Describes a single contiguous DMA transaction.  Values
 *	are suitable for programming into DMA registers.
 *	描述单个连续的DMA事务，它的值适合于编写到DMA寄存器。segment意思就是段，
 		所以这个结构体表示的就是一个地址空间段
 */
typedef struct bus_dma_segment {
	bus_addr_t	ds_addr;	/* DMA address */
	bus_size_t	ds_len;		/* length of transfer */
} bus_dma_segment_t;

/*
 * A function that returns 1 if the address cannot be accessed by
 * a device and 0 if it can be.
 */
typedef int bus_dma_filter_t(void *, bus_addr_t);

/*
 * Generic helper function for manipulating mutexes.
 */
void busdma_lock_mutex(void *arg, bus_dma_lock_op_t op);

/*
 * Allocate a device specific dma_tag encapsulating(封装) the constraints of
 * the parent tag in addition to other restrictions specified:
 *
 *  parent:		指定了此标签的父标签，如果是要创建顶层的DMA标签，那么就将 bus_get_dma_tag(device_t dev)
 * 				作为 parent 参数传递给 bus_dma_tag_create
 * 
 *	alignment:	Alignment for segments. 指定了此标签各DMA分段的物理内存对齐(以字节为单位).DMA映射依据DMA
				的标签属性而分配内存区域，这些内存区域也被称为DMA分段(DMA segment), callback函数的arg参数
				其实就是被赋予了一个DMA分段地址。alignment取值必须是1(表示没有特别的对齐要求)或者2的幂。例如，
				如果驱动程序要求DMA缓冲区对齐到4KB的整数倍，那么alignment参数应传递4096

 *	boundary:	Boundary that segments cannot cross. 指定各DMA分段不能跨越的物理地址边界-它们不能跨越任何
				boundary的整数倍地址。此参数的取值必须是0(表示没有边界限制)或者是2的幂

 *	lowaddr:	Low restricted address that cannot appear in a mapping.
 *	highaddr:	High restricted address that cannot appear in a mapping.
				high和low限制了DMA不能使用的地址范围。例如，某个设备的DMA不能使用高于4G的地址，那么它将以0xffffffff
				作为lowaddr而以 BUS_SPACE_MAXADDR 作为 highaddr？？ BUS_SPACE_MAXADDR 表示体系结构最大可寻址范围

 *	filtfunc:	An optional function to further test if an address
 *			within the range of lowaddr and highaddr cannot appear
 *			in a mapping.
 *	filtfuncarg:	An argument that will be passed to filtfunc in addition
 *			to the address to test.
			两者指定了一个可选的回调函数和其首参数。当准备加载(映射)位于 lowaddr 和 highaddr之间的DMA区域的时候，函数
			会被调用。如果范围之内没有可用的空间，那么 filtfunc 则会告知系统；如果两个参数同时设置为NULL，那就相当于在
			从low到high范围内的所有空间都被视为是不可访问的

 *	maxsize:	Maximum mapping size supported by this tag.
				单个DMA映射最大可分配的内存大小(以字节作为单位)

 *	nsegments:	Number of discontinuities allowed in maps.
				指定单个DMA映射允许包含的分散/聚集段(scatter/gather segment)的最大数量。一个分散/聚集段实际上就是
				一个内存页。其名字的来源是: 当我们把一组物理上不连续的页组合成一个逻辑上连续的缓冲区时，我们必须将写操作
				分散而把读操作聚集。有些设备需要操作连续的内存，但有时候并没有足够大的连续内存块，这个时候就可以使用分散/
				聚集的方式组成缓冲区来欺骗设备。每一个DMA分段都是一个分散/聚集段
				nsegments 可以用 BUS_SPACE_UNRESTRICTED 赋值，表示没有数量限制。但是采用这种方式创建的DMA不能用于
				创建DMA映射，而只能作为其他标签的父标签，因为系统无法支持由无限个分散/聚集段所构成的DMA映射

 *	maxsegsz:	Maximum size of a segment in the map. 指定单个DMA映射中DMA分段个体的最大字节数
 *	flags:		Bus DMA flags. 描述tag create函数的行为
 *	lockfunc:	An optional function to handle driver-defined lock
 *			operations.
 *	lockfuncarg:	An argument that will be passed to lockfunc in addition
 *			to the lock operation.
 *	dmat:		A pointer to set to a valid dma tag should the return
 *			value of this function indicate success.
 */
/* XXX Should probably allow specification of alignment 
	创建一个DMA标签(tag)，主要用于描述DMA事务的特性和限制；可以多次调用该函数，注意每次传入的参数会不同，
	因为一个DMA标签可以从其他DMA标签处继承特性和限制。当然，子标签不能放宽父标签所建立的限制
*/
int bus_dma_tag_create(bus_dma_tag_t parent, bus_size_t alignment,
		       bus_addr_t boundary, bus_addr_t lowaddr,
		       bus_addr_t highaddr, bus_dma_filter_t *filtfunc,
		       void *filtfuncarg, bus_size_t maxsize, int nsegments,
		       bus_size_t maxsegsz, int flags, bus_dma_lock_t *lockfunc,
		       void *lockfuncarg, bus_dma_tag_t *dmat);

/*
 * Set the memory domain to be used for allocations.
 *
 * Automatic for PCI devices.  Must be set prior to creating maps or
 * allocating memory. 必须在创建映射或分配内存之前设置
 * 从函数代码来看，DMA 功能应该是要划归到bus当中，并且tag应该是一个相当重要的量
 */
int bus_dma_tag_set_domain(bus_dma_tag_t dmat, int domain);

int bus_dma_tag_destroy(bus_dma_tag_t dmat);

/*
 * A function that processes a successfully loaded dma map or an error
 * from a delayed load map.
 * 这个函数用于处理加载成功的DMA映射和加载错误的延迟映射的情况
 */
typedef void bus_dmamap_callback_t(void *, bus_dma_segment_t *, int, int);

/*
 * Like bus_dmamap_callback but includes map size in bytes.  This is
 * defined as a separate interface to maintain compatibility for users
 * of bus_dmamap_callback_t--at some point these interfaces should be merged.
 * 只是比上面的函数多了一个映射的字节数
 */
typedef void bus_dmamap_callback2_t(void *, bus_dma_segment_t *, int, bus_size_t, int);

/*
 * Map the buffer buf into bus space using the dmamap map.
 * 将 buffer 映射到总线地址空间，所以说DMA应该还是属于总线操作的范畴
 */
int bus_dmamap_load(bus_dma_tag_t dmat, bus_dmamap_t map, void *buf,
		    bus_size_t buflen, bus_dmamap_callback_t *callback,
		    void *callback_arg, int flags);

/*
 * Like bus_dmamap_load but for mbufs.  Note the use of the
 * bus_dmamap_callback2_t interface.
 */
int bus_dmamap_load_mbuf(bus_dma_tag_t dmat, bus_dmamap_t map,
			 struct mbuf *mbuf,
			 bus_dmamap_callback2_t *callback, void *callback_arg,
			 int flags);

int bus_dmamap_load_mbuf_sg(bus_dma_tag_t dmat, bus_dmamap_t map,
			    struct mbuf *mbuf, bus_dma_segment_t *segs,
			    int *nsegs, int flags);

/*
 * Like bus_dmamap_load but for uios.  Note the use of the
 * bus_dmamap_callback2_t interface.	
 */
int bus_dmamap_load_uio(bus_dma_tag_t dmat, bus_dmamap_t map,
			struct uio *ui,
			bus_dmamap_callback2_t *callback, void *callback_arg,
			int flags);

/*
 * Like bus_dmamap_load but for cam control blocks.	CAM控制块
 */
int bus_dmamap_load_ccb(bus_dma_tag_t dmat, bus_dmamap_t map, union ccb *ccb,
			bus_dmamap_callback_t *callback, void *callback_arg,
			int flags);

/*
 * Like bus_dmamap_load but for bios.
 */
int bus_dmamap_load_bio(bus_dma_tag_t dmat, bus_dmamap_t map, struct bio *bio,
			bus_dmamap_callback_t *callback, void *callback_arg,
			int flags);

/*
 * Loads any memory descriptor. dma_load都是用于映射，只是作用的对象不同
 */
int bus_dmamap_load_mem(bus_dma_tag_t dmat, bus_dmamap_t map,
			struct memdesc *mem, bus_dmamap_callback_t *callback,
			void *callback_arg, int flags);

/*
 * Placeholder(占位符) for use by busdma implementations which do not benefit
 * from optimized procedure to load an array of vm_page_t.  Falls back
 * to do _bus_dmamap_load_phys() in loop.
 */
int bus_dmamap_load_ma_triv(bus_dma_tag_t dmat, bus_dmamap_t map,
    struct vm_page **ma, bus_size_t tlen, int ma_offs, int flags,
    bus_dma_segment_t *segs, int *segp);

#ifdef WANT_INLINE_DMAMAP
#define BUS_DMAMAP_OP static inline
#else
#define BUS_DMAMAP_OP
#endif

/*
 * Allocate a handle for mapping from kva/uva/physical
 * address space into bus device space. 用于管理总线设备地址空间的映射
 * kernel virtual address / user virtual address / physical address
 */
BUS_DMAMAP_OP int bus_dmamap_create(bus_dma_tag_t dmat, int flags, bus_dmamap_t *mapp);

/*
 * Destroy a handle for mapping from kva/uva/physical
 * address space into bus device space.
 */
BUS_DMAMAP_OP int bus_dmamap_destroy(bus_dma_tag_t dmat, bus_dmamap_t map);

/*
 * Allocate a piece of memory that can be efficiently mapped into
 * bus device space based on the constraints listed in the dma tag.
 * A dmamap to for use with dmamap_load is also allocated.
 * 用于分配一块可用于DMA映射的内存地址空间
 */
BUS_DMAMAP_OP int bus_dmamem_alloc(bus_dma_tag_t dmat, void** vaddr, int flags,
		     bus_dmamap_t *mapp);

/*
 * Free a piece of memory and its allocated dmamap, that was allocated
 * via bus_dmamem_alloc.
 */
BUS_DMAMAP_OP void bus_dmamem_free(bus_dma_tag_t dmat, void *vaddr, bus_dmamap_t map);

/*
 * Perform a synchronization operation on the given map. If the map
 * is NULL we have a fully IO-coherent(连贯的) system.
 */
BUS_DMAMAP_OP void bus_dmamap_sync(bus_dma_tag_t dmat, bus_dmamap_t dmamap, bus_dmasync_op_t op);

/*
 * Release the mapping held by map.
 */
BUS_DMAMAP_OP void bus_dmamap_unload(bus_dma_tag_t dmat, bus_dmamap_t dmamap);

#undef BUS_DMAMAP_OP

#endif /* _BUS_DMA_H_ */
