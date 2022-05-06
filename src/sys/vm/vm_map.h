/*-
 * SPDX-License-Identifier: (BSD-3-Clause AND MIT-CMU)
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University.
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
 *	@(#)vm_map.h	8.9 (Berkeley) 5/17/95
 *
 *
 * Copyright (c) 1987, 1990 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Authors: Avadis Tevanian, Jr., Michael Wayne Young
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 *
 * $FreeBSD: releng/12.0/sys/vm/vm_map.h 338370 2018-08-29 12:24:19Z kib $
 */

/*
 *	Virtual memory map module definitions.
 */
#ifndef	_VM_MAP_
#define	_VM_MAP_

#include <sys/lock.h>
#include <sys/sx.h>
#include <sys/_mutex.h>

/*
 *	Types defined:
 *
 *	vm_map_t		the high-level address map data structure.
 *	vm_map_entry_t		an entry in an address map.
 */

typedef u_char vm_flags_t;
typedef u_int vm_eflags_t;

/*
 *	Objects which live in maps may be either VM objects, or
 *	another map (called a "sharing map") which denotes read-write
 *	sharing with other maps.
 		存在于映射中的对象可以是VM对象，也可以是表示与其他映射进行读写共享的另一个映射（称为“共享映射”）

		Normally, the sub_map member is only used by system maps to indicate that
    a memory range is managed by a subordinate system map. Within a user
    process map, each struct vm_map_entry is backed by a struct vm_object.
		通常，sub_map 成员仅由系统映射使用，以指示内存范围由从属系统映射管理。在用户进程映射中，
		每个struct vm_map_entry 都由 struct vm_object 支持 (只在用户进程中才是一个 vm_object?)
 */
union vm_map_object {
	struct vm_object *vm_object;	/* object object */
	struct vm_map *sub_map;		/* belongs to another map */
};

/*
 *	Address map entries consist of start and end addresses,
 *	a VM object (or sharing map) and offset into that object,
 *	and user-exported inheritance and protection information.
 *	Also included is control information for virtual copy operations.

	- 地址映射条目包括起始地址和结束地址、VM对象（或共享映射）和该对象的偏移量，
	以及用户导出的继承和保护信息。还包括虚拟复制操作的控制信息。
	- 表示一段连续的虚拟地址范围，这些地址共享保护权限和继承属性，并且使用相同的
	后备存储对象
	- 主要作用还是用于内存分配以及在缺页的时候实现快速查找。结构体中包含有起始地址和终止地址，
	说明它表示的就是一块虚拟内存地址空间。而 vm_map 中会包含有一个 vm_map_entry 链表，说明
	vm_map 管理的是一大块虚拟内存地址空间，然后有拆分成了许多小块，每个小块用一个 entry 来表示
 */
struct vm_map_entry {
	/*
		Pointer to the previous node in a doubly-linked, circular list.
	*/
	struct vm_map_entry *prev;	/* previous entry */
	/*
		Pointer to the next node in a	doubly-linked, circular	list.
		注意，下面的两个成员跟这两个表示的意义是不同的。这里表示的是链表结构成员指针，
		下面两个表示的二叉搜索树节点指针
	*/
	struct vm_map_entry *next;	/* next entry */
	struct vm_map_entry *left;	/* left child in binary search tree */
	struct vm_map_entry *right;	/* right child in binary search tree */
	/*
		Lower	address	bound of this entry's region.
	*/
	vm_offset_t start;		/* start address */
	/*
		Upper	address	bound of this entry's region.
	*/
	vm_offset_t end;		/* end address */
	vm_offset_t next_read;		/* vaddr of the next sequential read 下一次顺序读取的 vaddr */
	/*
		The amount of	free, unmapped address space adjacent to and immediately
		following	this map entry.
		与此映射条目相邻并紧随其后的可用未映射地址空间量
	*/
	vm_size_t adj_free;		/* amount of adjacent free space 相邻可用空间的数量 */
	/*
		The maximum amount of	contiguous free	space in this map entry's	subtree.
	*/
	vm_size_t max_free;		/* max free space in subtree */
	/*
		每个 entry 会对应一个 object，这个貌似跟虚拟文件系统有很大的关联，需要看一下
	*/
	union vm_map_object object;	/* object I point to 所关联的object */
	/*
		Offset within	the object which is mapped from	start onwards.
		从起点开始映射的对象内的偏移
	*/
	vm_ooffset_t offset;		/* offset into object */
	vm_eflags_t eflags;		/* map entry flags 标志符 */
	/*
		Memory protection bits applied to this region.
		- user process's address space
	*/
	vm_prot_t protection;		/* protection code 保护代码 */
	/*
		Mask for the memory protection bits which may be actually
		be applied to this region. - user process's address space
	*/
	vm_prot_t max_protection;	/* maximum protection */
	/*
		Contains flags which specify how this entry should be treated 
		during fork processing. - user process's address space
	*/
	vm_inherit_t inheritance;	/* inheritance 继承 */
	uint8_t read_ahead;		/* pages in the read-ahead window */
	/*
		Count of how many times this entry has been wired into physical memory.
	*/
	int wired_count;		/* can be paged if = 0 */
	struct ucred *cred;		/* tmp storage for creator ref */
	struct thread *wiring_thread;	/* 对应的线程 */
};

/*
	The system should not flush the data associated with this	map periodically,
	but only when it needs to.
*/
#define MAP_ENTRY_NOSYNC		0x0001
/*
	If set, then the object member specifies a subordinate (下级，下属的) map
*/
#define MAP_ENTRY_IS_SUB_MAP		0x0002
/*
	Indicate that this is a copy-on-write region.
*/
#define MAP_ENTRY_COW			0x0004
/*
	Indicate that a copy-on-write region needs to be copied.
*/
#define MAP_ENTRY_NEEDS_COPY		0x0008
/*
	Specifies that accesses within this region should never cause a page fault. 
	If a page fault occurs within this region, the system will panic.
*/
#define MAP_ENTRY_NOFAULT		0x0010
/*
	Indicate that this region was wired on behalf of a user process.
*/
#define MAP_ENTRY_USER_WIRED		0x0020
/*
	The system should use the default paging behaviour for this region.
*/
#define MAP_ENTRY_BEHAV_NORMAL		0x0000	/* default behavior */
/*
	The system should depress(降低) the priority of pages immediately
	preceding each page within this region when faulted in.
*/
#define MAP_ENTRY_BEHAV_SEQUENTIAL	0x0040	/* expect sequential access */
/*
	Is a hint that pages within this region will be accessed randomly,
	and that prefetching is likely not advantageous.
	这意味着该区域内的页面将被随机访问，而预取可能并不有利
*/
#define MAP_ENTRY_BEHAV_RANDOM		0x0080	/* expect random access */
#define MAP_ENTRY_BEHAV_RESERVED	0x00C0	/* future use */

#define MAP_ENTRY_BEHAV_MASK		0x00C0
/*
	Indicate that wiring or unwiring of an entry is in progress, and that
	other	kernel threads should	not attempt to modify fields in	the structure.
*/
#define MAP_ENTRY_IN_TRANSITION		0x0100	/* entry being changed */
/*
	Indicate that there are kernel threads waiting for this region to	become available.
*/
#define MAP_ENTRY_NEEDS_WAKEUP		0x0200	/* waiters in transition */
/*
	The region should not be included in a core dump.
*/
#define MAP_ENTRY_NOCOREDUMP		0x0400	/* don't include in a core */

#define	MAP_ENTRY_GROWS_DOWN		0x1000	/* Top-down stacks */
#define	MAP_ENTRY_GROWS_UP		0x2000	/* Bottom-up stacks */

#define	MAP_ENTRY_WIRE_SKIPPED		0x4000
#define	MAP_ENTRY_VN_WRITECNT		0x8000	/* writeable vnode mapping */
#define	MAP_ENTRY_GUARD			0x10000
#define	MAP_ENTRY_STACK_GAP_DN		0x20000
#define	MAP_ENTRY_STACK_GAP_UP		0x40000

#ifdef	_KERNEL
static __inline u_char
vm_map_entry_behavior(vm_map_entry_t entry)
{
	return (entry->eflags & MAP_ENTRY_BEHAV_MASK);
}

static __inline int
vm_map_entry_user_wired_count(vm_map_entry_t entry)
{
	if (entry->eflags & MAP_ENTRY_USER_WIRED)
		return (1);
	return (0);
}

static __inline int
vm_map_entry_system_wired_count(vm_map_entry_t entry)
{
	return (entry->wired_count - vm_map_entry_user_wired_count(entry));
}
#endif	/* _KERNEL */

/*
 *	A map is a set of map entries.  These map entries are
 *	organized both as a binary search tree and as a doubly-linked
 *	list.  Both structures are ordered based upon the start and
 *	end addresses contained within each map entry.
 *	vm_map是一组映射条目。这些地图条目被组织为二叉搜索树和双链表。这两种结构都是
 * 	基于每个映射条目中包含的起始地址和结束地址排序的。
 *
 *	Counterintuitively, the map's min offset value is stored in
 *	map->header.end, and its max offset value is stored in
 *	map->header.start.
 *
 *	The list header has max start value and min end value to act
 *	as sentinels for sequential search of the doubly-linked list.
 *	Sleator and Tarjan's top-down splay algorithm is employed to
 *	control height imbalance in the binary search tree.
 *	列表头具有max start value和min end value作为双链表顺序搜索的哨兵。
 *	采用Sleator和Tarjan自顶向下的splay算法控制二叉搜索树的高度不平衡。
 *
 *	List of locks
 *	(c)	const until freed
 *	表示与机器地址无关的虚拟地址空间的最高层数据结构
 */
struct vm_map {
	/* 
		Head node of a circular, doubly linked list of struct vm_map_entry objects.
		Each object defines a	particular region	within this map's address space.
		entry 才是真正对应虚拟地址空间的结构体，这里用一个链表来统一管理。entry中包含有起始地址，结束地址等信息
	*/
	struct vm_map_entry header;	/* List of entries */
	/*
		map min_offset	header.end	(c)
		map max_offset	header.start	(c)
		Used to serialize(序列化) access to the structure.
	*/
	struct sx lock;			/* Lock for map data 共享锁 */
	/*
		A mutex which is used if the	map is a system	map.
	*/
	struct mtx system_mtx;
	int nentries;			/* Number of entries 应该是 map_entry 的数量 */
	vm_size_t size;			/* virtual size 该结构体所管理的总的虚拟地址空间的大小 */
	/*
		Used to determine if the map has changed since its last access.
	*/
	u_int timestamp;		/* Version number */
	/*
		Indicates if a thread is waiting for an allsocation within the map.
		Used only by system maps.
	*/
	u_char needs_wakeup;
	/*
		Set to TRUE to indicate that map is a system map; 
		otherwise, it belongs to a user process.
	*/
	u_char system_map;		/* (c) Am I a system map? */
	vm_flags_t flags;		/* flags for this vm_map */
	/*
		Root node of	a binary search	tree used for fast lookup of map entries
	*/
	vm_map_entry_t root;		/* Root of a binary search tree */
	/*
		Pointer to the underlying physical map with which this virtual map is associated.
	*/
	pmap_t pmap;			/* (c) Physical map */
	/*
		Map busy counter, prevents forks.
	*/
	int busy;
};

/*
 * vm_flags_t values
 */
#define MAP_WIREFUTURE		0x01	/* wire all future pages 连接所有未来页面 */
/* There are waiters for the map busy status. */
#define	MAP_BUSY_WAKEUP		0x02

#ifdef	_KERNEL
#if defined(KLD_MODULE) && !defined(KLD_TIED)
#define	vm_map_max(map)		vm_map_max_KBI((map))
#define	vm_map_min(map)		vm_map_min_KBI((map))
#define	vm_map_pmap(map)	vm_map_pmap_KBI((map))
#else
static __inline vm_offset_t
vm_map_max(const struct vm_map *map)
{

	return (map->header.start);
}

static __inline vm_offset_t
vm_map_min(const struct vm_map *map)
{

	return (map->header.end);
}

static __inline pmap_t
vm_map_pmap(vm_map_t map)
{
	return (map->pmap);
}

static __inline void
vm_map_modflags(vm_map_t map, vm_flags_t set, vm_flags_t clear)
{
	map->flags = (map->flags | set) & ~clear;
}
#endif	/* KLD_MODULE */
#endif	/* _KERNEL */

/*
 * Shareable process virtual address space.
 * vmspace 所表示的是进程的虚拟地址空间，一个进程中包含多个段，比如text、data、stack等等，用于
 * 保存相应的数据信息
 * vmspace 结构体中最重要的就是 vm_map 和 vm_pmap。pmap 是跟平台相关，vm_map 结构体管理的是虚拟地址空间段。
 * 同时，vm_map 还管理着虚拟地址空间和物理地址空间的映射关系
 *
 * List of locks
 *	(c)	const until freed
 */
struct vmspace {
	struct vm_map vm_map;	/* VM address map 虚拟地址映射 */
	struct shmmap_state *vm_shm;	/* SYS5 shared memory private data XXX 共享内存私有数据 */
	segsz_t vm_swrss;	/* resident set size before last swap 上次交换前的驻留集大小 */
	segsz_t vm_tsize;	/* text size (pages) XXX text 段的大小 (以页为单位) */
	segsz_t vm_dsize;	/* data size (pages) XXX data 段的大小 (以页为单位)*/
	segsz_t vm_ssize;	/* stack size (pages) 堆栈大小(页为单位) */
	caddr_t vm_taddr;	/* (c) user virtual address of text - text 段的用户虚拟地址空间 */
	caddr_t vm_daddr;	/* (c) user virtual address of data - data 段的用户虚拟地址空间 */
	caddr_t vm_maxsaddr;	/* user VA at max stack growth 限制用户虚拟地址空间的上限 */
	volatile int vm_refcnt;	/* number of references 引用计数 */
	/*
	 * Keep the PMAP last, so that CPU-specific variations of that
	 * structure on a single architecture don't result in offset
	 * variations of the machine-independent fields in the vmspace.
	 * 将PMAP放在最后，这样在单个体系结构上该结构的CPU特定变化就不会导致vmspace中与机器无关的字段的偏移量变化
	 * 
	 */
	struct pmap vm_pmap;	/* private physical map - CPU相关物理内存，不同平台的定义是不一样的 */
};

#ifdef	_KERNEL
static __inline pmap_t
vmspace_pmap(struct vmspace *vmspace)
{
	return &vmspace->vm_pmap;
}
#endif	/* _KERNEL */

#ifdef	_KERNEL
/*
 *	Macros:		vm_map_lock, etc.
 *	Function:
 *		Perform locking on the data portion of a map.  Note that
 *		these macros mimic procedure calls returning void.  The
 *		semicolon is supplied by the user of these macros, not
 *		by the macros themselves.  The macros can safely be used
 *		as unbraced elements in a higher level statement.
 */

void _vm_map_lock(vm_map_t map, const char *file, int line);
void _vm_map_unlock(vm_map_t map, const char *file, int line);
int _vm_map_unlock_and_wait(vm_map_t map, int timo, const char *file, int line);
void _vm_map_lock_read(vm_map_t map, const char *file, int line);
void _vm_map_unlock_read(vm_map_t map, const char *file, int line);
int _vm_map_trylock(vm_map_t map, const char *file, int line);
int _vm_map_trylock_read(vm_map_t map, const char *file, int line);
int _vm_map_lock_upgrade(vm_map_t map, const char *file, int line);
void _vm_map_lock_downgrade(vm_map_t map, const char *file, int line);
int vm_map_locked(vm_map_t map);
void vm_map_wakeup(vm_map_t map);
void vm_map_busy(vm_map_t map);
void vm_map_unbusy(vm_map_t map);
void vm_map_wait_busy(vm_map_t map);
vm_offset_t vm_map_max_KBI(const struct vm_map *map);
vm_offset_t vm_map_min_KBI(const struct vm_map *map);
pmap_t vm_map_pmap_KBI(vm_map_t map);

#define	vm_map_lock(map)	_vm_map_lock(map, LOCK_FILE, LOCK_LINE)
#define	vm_map_unlock(map)	_vm_map_unlock(map, LOCK_FILE, LOCK_LINE)
#define	vm_map_unlock_and_wait(map, timo)	\
			_vm_map_unlock_and_wait(map, timo, LOCK_FILE, LOCK_LINE)
#define	vm_map_lock_read(map)	_vm_map_lock_read(map, LOCK_FILE, LOCK_LINE)
#define	vm_map_unlock_read(map)	_vm_map_unlock_read(map, LOCK_FILE, LOCK_LINE)
#define	vm_map_trylock(map)	_vm_map_trylock(map, LOCK_FILE, LOCK_LINE)
#define	vm_map_trylock_read(map)	\
			_vm_map_trylock_read(map, LOCK_FILE, LOCK_LINE)
#define	vm_map_lock_upgrade(map)	\
			_vm_map_lock_upgrade(map, LOCK_FILE, LOCK_LINE)
#define	vm_map_lock_downgrade(map)	\
			_vm_map_lock_downgrade(map, LOCK_FILE, LOCK_LINE)

long vmspace_resident_count(struct vmspace *vmspace);
#endif	/* _KERNEL */


/* XXX: number of kernel maps to statically allocate
	要静态分配的内核映射数
*/
#define MAX_KMAP	10

/*
 * Copy-on-write flags for vm_map operations
 */
#define MAP_INHERIT_SHARE	0x0001
/*
	The mapping is copy-on-write.
*/
#define MAP_COPY_ON_WRITE	0x0002
/*
	The mapping should not generate page faults.
*/
#define MAP_NOFAULT		0x0004
/*
	The mapping should be prefaulted into physical memory.
*/
#define MAP_PREFAULT		0x0008
/*
	The mapping should be partially (部分的，不完全的) prefaulted into
	physical memory.
*/
#define MAP_PREFAULT_PARTIAL	0x0010
/*
	Do not periodically (周期性的，定期的) flush dirty pages; only flush them 
	when absolutely necessary.
*/
#define MAP_DISABLE_SYNCER	0x0020
#define	MAP_CHECK_EXCL		0x0040
#define	MAP_CREATE_GUARD	0x0080
/*
	Do not include the mapping in a core dump.
*/
#define MAP_DISABLE_COREDUMP	0x0100
/*
	Specify that the request is from a user process calling madvise(2).
*/
#define MAP_PREFAULT_MADVISE	0x0200	/* from (user) madvise request */
#define	MAP_VN_WRITECOUNT	0x0400
#define	MAP_STACK_GROWS_DOWN	0x1000
#define	MAP_STACK_GROWS_UP	0x2000
/*
	Region is already charged to the requestor by some means.
*/
#define	MAP_ACC_CHARGED		0x4000
/*
	Do not charge for allocated region.
*/
#define	MAP_ACC_NO_CHARGE	0x8000
#define	MAP_CREATE_STACK_GAP_UP	0x10000
#define	MAP_CREATE_STACK_GAP_DN	0x20000

/*
 * vm_fault option flags
 */
#define	VM_FAULT_NORMAL	0		/* Nothing special */
#define	VM_FAULT_WIRE	1		/* Wire the mapped page */
#define	VM_FAULT_DIRTY	2		/* Dirty the page; use w/VM_PROT_COPY */

/*
 * Initially, mappings are slightly sequential.  The maximum window size must
 * account for the map entry's "read_ahead" field being defined as an uint8_t.
 */
#define	VM_FAULT_READ_AHEAD_MIN		7
#define	VM_FAULT_READ_AHEAD_INIT	15
#define	VM_FAULT_READ_AHEAD_MAX		min(atop(MAXPHYS) - 1, UINT8_MAX)

/*
 * The following "find_space" options are supported by vm_map_find().
 *
 * For VMFS_ALIGNED_SPACE, the desired alignment is specified to
 * the macro argument as log base 2 of the desired alignment.
 */
#define	VMFS_NO_SPACE		0	/* don't find; use the given range */
#define	VMFS_ANY_SPACE		1	/* find a range with any alignment */
#define	VMFS_OPTIMAL_SPACE	2	/* find a range with optimal alignment*/
#define	VMFS_SUPER_SPACE	3	/* find a superpage-aligned range */
#define	VMFS_ALIGNED_SPACE(x)	((x) << 8) /* find a range with fixed alignment */

/*
 * vm_map_wire and vm_map_unwire option flags
 */
#define VM_MAP_WIRE_SYSTEM	0	/* wiring in a kernel map */
#define VM_MAP_WIRE_USER	1	/* wiring in a user map */

#define VM_MAP_WIRE_NOHOLES	0	/* region must not have holes */
#define VM_MAP_WIRE_HOLESOK	2	/* region may have holes */

#define VM_MAP_WIRE_WRITE	4	/* Validate writable. */

#ifdef _KERNEL
boolean_t vm_map_check_protection (vm_map_t, vm_offset_t, vm_offset_t, vm_prot_t);
vm_map_t vm_map_create(pmap_t, vm_offset_t, vm_offset_t);
int vm_map_delete(vm_map_t, vm_offset_t, vm_offset_t);
int vm_map_find(vm_map_t, vm_object_t, vm_ooffset_t, vm_offset_t *, vm_size_t,
    vm_offset_t, int, vm_prot_t, vm_prot_t, int);
int vm_map_find_min(vm_map_t, vm_object_t, vm_ooffset_t, vm_offset_t *,
    vm_size_t, vm_offset_t, vm_offset_t, int, vm_prot_t, vm_prot_t, int);
int vm_map_fixed(vm_map_t, vm_object_t, vm_ooffset_t, vm_offset_t, vm_size_t,
    vm_prot_t, vm_prot_t, int);
int vm_map_findspace (vm_map_t, vm_offset_t, vm_size_t, vm_offset_t *);
int vm_map_inherit (vm_map_t, vm_offset_t, vm_offset_t, vm_inherit_t);
void vm_map_init(vm_map_t, pmap_t, vm_offset_t, vm_offset_t);
int vm_map_insert (vm_map_t, vm_object_t, vm_ooffset_t, vm_offset_t, vm_offset_t, vm_prot_t, vm_prot_t, int);
int vm_map_lookup (vm_map_t *, vm_offset_t, vm_prot_t, vm_map_entry_t *, vm_object_t *,
    vm_pindex_t *, vm_prot_t *, boolean_t *);
int vm_map_lookup_locked(vm_map_t *, vm_offset_t, vm_prot_t, vm_map_entry_t *, vm_object_t *,
    vm_pindex_t *, vm_prot_t *, boolean_t *);
void vm_map_lookup_done (vm_map_t, vm_map_entry_t);
boolean_t vm_map_lookup_entry (vm_map_t, vm_offset_t, vm_map_entry_t *);
int vm_map_protect (vm_map_t, vm_offset_t, vm_offset_t, vm_prot_t, boolean_t);
int vm_map_remove (vm_map_t, vm_offset_t, vm_offset_t);
void vm_map_simplify_entry(vm_map_t map, vm_map_entry_t entry);
void vm_map_startup (void);
int vm_map_submap (vm_map_t, vm_offset_t, vm_offset_t, vm_map_t);
int vm_map_sync(vm_map_t, vm_offset_t, vm_offset_t, boolean_t, boolean_t);
int vm_map_madvise (vm_map_t, vm_offset_t, vm_offset_t, int);
int vm_map_stack (vm_map_t, vm_offset_t, vm_size_t, vm_prot_t, vm_prot_t, int);
int vm_map_unwire(vm_map_t map, vm_offset_t start, vm_offset_t end,
    int flags);
int vm_map_wire(vm_map_t map, vm_offset_t start, vm_offset_t end,
    int flags);
long vmspace_swap_count(struct vmspace *vmspace);
#endif				/* _KERNEL */
#endif				/* _VM_MAP_ */
