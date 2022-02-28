/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)buf.h	8.9 (Berkeley) 3/30/95
 * $FreeBSD: releng/12.0/sys/sys/buf.h 333576 2018-05-13 09:47:28Z kib $
 */

#ifndef _SYS_BUF_H_
#define	_SYS_BUF_H_

#include <sys/bufobj.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/lockmgr.h>

struct bio;
struct buf;
struct bufobj;
struct mount;
struct vnode;
struct uio;

/*
 * To avoid including <ufs/ffs/softdep.h> 
 */   
LIST_HEAD(workhead, worklist);
/*
 * These are currently used only by the soft dependency code, hence
 * are stored once in a global variable. If other subsystems wanted
 * to use these hooks, a pointer to a set of bio_ops could be added
 * to each buffer.
 * 它们当前仅由软依赖代码使用，因此在全局变量中存储一次。如果其他子系统想要使用这些钩子，
 * 可以向每个缓冲区添加一个指向一组 bio_ops 的指针。
 */
extern struct bio_ops {
	void	(*io_start)(struct buf *);
	void	(*io_complete)(struct buf *);
	void	(*io_deallocate)(struct buf *);
	int	(*io_countdeps)(struct buf *, int);
} bioops;

struct vm_object;
struct vm_page;

typedef uint32_t b_xflags_t;

/* 
 * The buffer header describes an I/O operation in the kernel.
 * 缓冲区头描述一个内核中的 I/O 操作
 *
 * NOTES:
 *	b_bufsize, b_bcount.  b_bufsize is the allocation size of the
 *	buffer, either DEV_BSIZE or PAGE_SIZE aligned.  b_bcount is the
 *	originally requested buffer size and can serve as a bounds check
 *	against EOF.  For most, but not all uses, b_bcount == b_bufsize.
 * 
 * b_bufsize 表示的是申请的 buffer 的大小，DEV_BSIZE 或者 PAGE_SIZE 对齐；
 * b_bcount 表示的是最初请求的 buffer 的大小，可以用于针对 EOF 的边界检查。
 * 大多数情况下，但不是所有，两者是相等的
 * 
 * 
 *	b_dirtyoff, b_dirtyend.  Buffers support piecemeal, unaligned
 *	ranges of dirty data that need to be written to backing store.
 *	The range is typically clipped at b_bcount ( not b_bufsize ).
 *  缓冲区支持需要写入后备存储器的零碎、未对齐的脏数据范围。范围通常在 b_bcount
 * （而不是b\u bufsize）处剪裁
 *  
 * 
 *	b_resid.  Number of bytes remaining in I/O.  After an I/O operation
 *	completes, b_resid is usually 0 indicating 100% success.
 *	I/O中剩余的字节数。I/O操作完成后，b_resid 通常为0，表示100%成功
 *  
 *	All fields are protected by the buffer lock except those marked:
 *		V - Protected by owning bufobj lock
 *		Q - Protected by the buf queue lock
 *		D - Protected by an dependency implementation specific lock
 */
/*
	bufobj 可以认为是 buf 的一个基类，其中包含了一些锁、buffer operations 等成员；
	内核实现了 buffer cache 的 KVM 抽象，允许它将可能完全不同的的 vm_page 合并到连续的 KVM 中，
	以供(主要是文件系统)设备和设备I/O使用。这个抽象所支持的 block size 从 DEV_BSIZE(通常是512字节)
	延伸到几个页或者更多。它还支持相对原始的字节粒度有效范围和当前硬编码供NFS使用的脏范围。实现VM缓冲区抽象
	的代码主要集中在 vfs_bio.c 文件当中
	处理缓冲区指针（struct buf）时要记住的最重要的一点是，底层页面直接从缓冲区缓存映射。在该方案中不会发生
	数据复制，尽管某些文件系统（如UFS）在处理文件片段时确实需要进行少量复制；第二，由于底层页映射的缘故，b_data
	(表示的应该是真正的数据)一般是 page aligned，而不是 block aligned。当我们有 VM buffer 表示一些 b_offset
	和 b_size 时，buffer 真正开始的地方其实是 b_data + (b_offset & PAGE_MASK) 而不仅仅是 b_data。最后，
	VM system 的核心 buffer cache 支持 DEV_BSIZE 页中的有效位和脏位(m->valid,m->dirty)
*/
struct buf {
	struct bufobj	*b_bufobj;	
	long		b_bcount;	/* 请求的 buffer 区域的大小 */
	void		*b_caller1;
	caddr_t		b_data;
	int		b_error;
	uint16_t	b_iocmd;	/* BIO_* bio_cmd from bio.h 指定bio的用途，读写或者获取属性等 */
	uint16_t	b_ioflags;	/* BIO_* bio_flags from bio.h */
	off_t		b_iooffset;	/* 数据偏移量？ */
	long		b_resid;	/* 剩余未处理的数据量 */
	void	(*b_iodone)(struct buf *);
	void	(*b_ckhashcalc)(struct buf *);
	uint64_t	b_ckhash;	/* B_CKHASH requested check-hash */
	daddr_t b_blkno;		/* Underlying physical block number. 底层物理块号 */
	off_t	b_offset;		/* Offset into file. 文件中的偏移量 */
	TAILQ_ENTRY(buf) b_bobufs;	/* (V) Buffer's associated vnode. */
	uint32_t	b_vflags;	/* (V) BV_* flags */
	uint8_t		b_qindex;	/* (Q) buffer queue index 缓存队列索引 */
	uint8_t		b_domain;	/* (Q) buf domain this resides in 驻留的buf域 */
	uint16_t	b_subqueue;	/* (Q) per-cpu q if any 多cpu */
	uint32_t	b_flags;	/* B_* flags. */
	b_xflags_t b_xflags;		/* extra flags 额外标志 */
	struct lock b_lock;		/* Buffer lock 缓存区锁 */
	long	b_bufsize;		/* Allocated buffer size. 分配的缓存区大小 */
	int	b_runningbufspace;	/* when I/O is running, pipelining 当I/O运行时，执行流水线 */
	int	b_kvasize;		/* size of kva for buffer 缓存的内核虚拟地址大小 */
	int	b_dirtyoff;		/* Offset in buffer of dirty region. 脏区在缓冲区中的偏移 */
	int	b_dirtyend;		/* Offset of end of dirty region. 脏区结束地址在缓冲区中的偏移 */
	caddr_t	b_kvabase;		/* base kva for buffer 缓冲区的内核虚拟地址的基地址 */
	daddr_t b_lblkno;		/* Logical block number. 逻辑块号 */
	struct	vnode *b_vp;		/* Device vnode. 设备 vnode */
	struct	ucred *b_rcred;		/* Read credentials reference. 读取凭证引用 */
	struct	ucred *b_wcred;		/* Write credentials reference. 写入凭证引用 */
	union {
		TAILQ_ENTRY(buf) b_freelist; /* (Q) */
		struct {
			void	(*b_pgiodone)(void *, vm_page_t *, int, int);
			int	b_pgbefore;
			int	b_pgafter;
		};s
	};
	union	cluster_info {
		TAILQ_HEAD(cluster_list_head, buf) cluster_head;
		TAILQ_ENTRY(buf) cluster_entry;
	} b_cluster;
	struct	vm_page *b_pages[btoc(MAXPHYS)]; /* 对应的虚拟页 */
	int		b_npages;	/* 对应的页的数量 */
	struct	workhead b_dep;		/* (D) List of filesystem dependencies. 文件系统依赖项列表 */
	void	*b_fsprivate1;
	void	*b_fsprivate2;
	void	*b_fsprivate3;

#if defined(FULL_BUF_TRACKING)
#define BUF_TRACKING_SIZE	32
#define BUF_TRACKING_ENTRY(x)	((x) & (BUF_TRACKING_SIZE - 1))
	const char	*b_io_tracking[BUF_TRACKING_SIZE];
	uint32_t	b_io_tcnt;
#elif defined(BUF_TRACKING)
	const char	*b_io_tracking;
#endif
};

#define b_object	b_bufobj->bo_object

/*
 * These flags are kept in b_flags.
 *
 * Notes:
 *
 *	B_ASYNC		VOP calls on bp's are usually async whether or not
 *			B_ASYNC is set, but some subsystems, such as NFS, like 
 *			to know what is best for the caller so they can
 *			optimize the I/O.
 				无论是否设置了B_ASYNC，bp上的VOP调用通常都是异步的，但是一些子系统（如NFS）
				希望知道什么对调用方最有利，以便优化I/O
 *
 *	B_PAGING	Indicates that bp is being used by the paging system or
 *			some paging system and that the bp is not linked into
 *			the b_vp's clean/dirty linked lists or ref counts.
 *			Buffer vp reassignments are illegal in this case.
 				指示bp正被分页系统或某些分页系统使用，并且bp未链接到 b_vp的干净/脏链表或ref计数中。
				在这种情况下，缓冲区vp重新分配是非法的。
 *
 *	B_CACHE		This may only be set if the buffer is entirely valid.
 *			The situation where B_DELWRI is set and B_CACHE is
 *			clear MUST be committed to disk by getblk() so 
 *			B_DELWRI can also be cleared.  See the comments for
 *			getblk() in kern/vfs_bio.c.  If B_CACHE is clear,
 *			the caller is expected to clear BIO_ERROR and B_INVAL,
 *			set BIO_READ, and initiate an I/O.
 *
 *			The 'entire buffer' is defined to be the range from
 *			0 through b_bcount.

 				只有当缓冲区完全可用的时候才可能会被设置。设置了 B_DELWRI 并且清除了 B_CACHE 的情况
				必须通过 getblk()提交到磁盘，这样也可以清除 B_DELWRI。如果 B_CACHE 清除，则调用者
				需要清除 BIO_ERROR 和 B_INVAL，设置 BIO_READ，并启动 I/O
 *
 *	B_MALLOC	Request that the buffer be allocated from the malloc
 *			pool, DEV_BSIZE aligned instead of PAGE_SIZE aligned.
				请求从 malloc 池中分配缓冲区，以 DEV_BSIZE 而不是 PAGE_SIZE 对齐
 *
 *	B_CLUSTEROK	This flag is typically set for B_DELWRI buffers
 *			by filesystems that allow clustering when the buffer
 *			is fully dirty and indicates that it may be clustered
 *			with other adjacent dirty buffers.  Note the clustering
 *			may not be used with the stage 1 data write under NFS
 *			but may be used for the commit rpc portion.
 				此标志通常由文件系统为 B_DELWRI 缓冲区设置，这些文件系统允许在缓冲区
				完全脏时进行群集，并指示它可能与其他相邻的脏缓冲区进行群集。注意：集群
				不能用于NFS下的阶段1数据写入，但可以用于提交rpc部分
 *
 *	B_VMIO		Indicates that the buffer is tied into an VM object.
 *			The buffer's data is always PAGE_SIZE aligned even
 *			if b_bufsize and b_bcount are not.  ( b_bufsize is 
 *			always at least DEV_BSIZE aligned, though ).
 				指示缓冲区绑定到VM对象。缓冲区的数据总是页大小对齐，即使 b_bufsize 和 b_bcount
				不对齐(b_bufsize 始终至少与 DEV_BSIZE 对齐）。
 *
 *	B_DIRECT	Hint that we should attempt to completely free
 *			the pages underlying the buffer.  B_DIRECT is
 *			sticky until the buffer is released and typically
 *			only has an effect when B_RELBUF is also set.
 				提示我们应该尝试完全释放缓冲区下面的页面。在释放缓冲区之前，B_DIRECT 是粘性的，
				并且通常仅在同时设置 B_RELBUF 时才起作用。
 *
 */

#define	B_AGE		0x00000001	/* Move to age queue when I/O done. */
#define	B_NEEDCOMMIT	0x00000002	/* Append-write in progress. 进程中追加写入 */
#define	B_ASYNC		0x00000004	/* Start I/O, do not wait. 不等待，直接开启I/O */
#define	B_DIRECT	0x00000008	/* direct I/O flag (pls free vmio) */
#define	B_DEFERRED	0x00000010	/* Skipped over for cleaning 跳过数据更新/脏页写回磁盘？ */
#define	B_CACHE		0x00000020	/* Bread found us in the cache. 表示该 buffer 已经在缓存区当中了 */
#define	B_VALIDSUSPWRT	0x00000040	/* Valid write during suspension.暂停期间的有效写入 */
#define	B_DELWRI	0x00000080	/* Delay I/O until buffer reused. 延迟I/O直到缓冲区被重用 */
#define	B_CKHASH	0x00000100	/* checksum hash calculated on read 读取时计算的校验和哈希 */
#define	B_DONE		0x00000200	/* I/O completed. io操作完成 */
#define	B_EINTR		0x00000400	/* I/O was interrupted io操作被中断 */
#define	B_NOREUSE	0x00000800	/* Contents not reused once released. 内容发布后不可重复使用 */
#define	B_REUSE		0x00001000	/* Contents reused, second chance. 内容重复使用，第二次机会 */
#define	B_INVAL		0x00002000	/* Does not contain valid info. 不包含有效信息 */
#define	B_BARRIER	0x00004000	/* Write this and all preceding first. 先写这个和前面所有的 */
#define	B_NOCACHE	0x00008000	/* Do not cache block after use. 使用后不缓存块 */
#define	B_MALLOC	0x00010000	/* malloced b_data 为b_data字段申请空间 */
#define	B_CLUSTEROK	0x00020000	/* Pagein op, so swap() can count it. pagein操作，所以swap可以计算它 */
#define	B_00040000	0x00040000	/* Available flag. */
#define	B_00080000	0x00080000	/* Available flag. */
#define	B_00100000	0x00100000	/* Available flag. */
#define	B_00200000	0x00200000	/* Available flag. */
#define	B_RELBUF	0x00400000	/* Release VMIO buffer. 释放 VMIO buffer */
#define	B_FS_FLAG1	0x00800000	/* Available flag for FS use. */
#define	B_NOCOPY	0x01000000	/* Don't copy-on-write this buf. 别抄袭我写的东西 */
#define	B_INFREECNT	0x02000000	/* buf is counted in numfreebufs 该buf被计算到了numfreebufs当中 */
#define	B_PAGING	0x04000000	/* volatile paging I/O -- bypass VMIO 易失性分页I/O—绕过VMIO */
#define B_MANAGED	0x08000000	/* Managed by FS. 被文件系统管理？ */
#define B_RAM		0x10000000	/* Read ahead mark (flag) 预读标记 */
#define B_VMIO		0x20000000	/* VMIO flag */
#define B_CLUSTER	0x40000000	/* pagein op, so swap() can count it */
#define B_REMFREE	0x80000000	/* Delayed bremfree 延迟bremfree操作 */

#define PRINT_BUF_FLAGS "\20\40remfree\37cluster\36vmio\35ram\34managed" \
	"\33paging\32infreecnt\31nocopy\30b23\27relbuf\26b21\25b20" \
	"\24b19\23b18\22clusterok\21malloc\20nocache\17b14\16inval" \
	"\15reuse\14noreuse\13eintr\12done\11b8\10delwri" \
	"\7validsuspwrt\6cache\5deferred\4direct\3async\2needcommit\1age"

/*
 * These flags are kept in b_xflags.
 *
 * BX_FSPRIV reserves a set of eight flags that may be used by individual
 * filesystems for their own purpose. Their specific definitions are
 * found in the header files for each filesystem that uses them.
 * BX_FSPRIV 保留了一组八个标志，每个文件系统都可以使用它们来实现自己的目的。
 * 它们的特定定义可以在每个使用它们的文件系统的头文件中找到
 */
#define	BX_VNDIRTY	0x00000001	/* On vnode dirty list 对应 vnode 脏页链表 */
#define	BX_VNCLEAN	0x00000002	/* On vnode clean list */
#define	BX_BKGRDWRITE	0x00000010	/* Do writes in background 后台执行写操作？ */
#define BX_BKGRDMARKER	0x00000020	/* Mark buffer for splay tree slpay tree(伸展树)标记缓冲区 */
#define	BX_ALTDATA	0x00000040	/* Holds extended data 保存扩展数据 */
#define	BX_FSPRIV	0x00FF0000	/* filesystem-specific flags mask */

#define	PRINT_BUF_XFLAGS "\20\7altdata\6bkgrdmarker\5bkgrdwrite\2clean\1dirty"

#define	NOOFFSET	(-1LL)		/* No buffer offset calculated yet */

/*
 * These flags are kept in b_vflags.
 */
#define	BV_SCANNED	0x00000001	/* VOP_FSYNC funcs mark written bufs */
#define	BV_BKGRDINPROG	0x00000002	/* Background write in progress */
#define	BV_BKGRDWAIT	0x00000004	/* Background write waiting */
#define	BV_BKGRDERR	0x00000008	/* Error from background write */

#define	PRINT_BUF_VFLAGS "\20\4bkgrderr\3bkgrdwait\2bkgrdinprog\1scanned"

#ifdef _KERNEL
/*
 * Buffer locking
 */
extern const char *buf_wmesg;		/* Default buffer lock message */
#define BUF_WMESG "bufwait"
#include <sys/proc.h>			/* XXX for curthread */
#include <sys/mutex.h>

/*
 * Initialize a lock.
 */
#define BUF_LOCKINIT(bp)						\
	lockinit(&(bp)->b_lock, PRIBIO + 4, buf_wmesg, 0, 0)
/*
 *
 * Get a lock sleeping non-interruptably until it becomes available.
 */
#define	BUF_LOCK(bp, locktype, interlock)				\
	_lockmgr_args_rw(&(bp)->b_lock, (locktype), (interlock),	\
	    LK_WMESG_DEFAULT, LK_PRIO_DEFAULT, LK_TIMO_DEFAULT,		\
	    LOCK_FILE, LOCK_LINE)

/*
 * Get a lock sleeping with specified interruptably and timeout.
 		获取一个以指定的中断和超时休眠的锁
 */
#define	BUF_TIMELOCK(bp, locktype, interlock, wmesg, catch, timo)	\
	_lockmgr_args_rw(&(bp)->b_lock, (locktype) | LK_TIMELOCK,	\
	    (interlock), (wmesg), (PRIBIO + 4) | (catch), (timo),	\
	    LOCK_FILE, LOCK_LINE)

/*
 * Release a lock. Only the acquiring process may free the lock unless
 * it has been handed off to biodone.
 */
#define	BUF_UNLOCK(bp) do {						\
	KASSERT(((bp)->b_flags & B_REMFREE) == 0,			\
	    ("BUF_UNLOCK %p while B_REMFREE is still set.", (bp)));	\
									\
	(void)_lockmgr_args(&(bp)->b_lock, LK_RELEASE, NULL,		\
	    LK_WMESG_DEFAULT, LK_PRIO_DEFAULT, LK_TIMO_DEFAULT,		\
	    LOCK_FILE, LOCK_LINE);					\
} while (0)

/*
 * Check if a buffer lock is recursed.
 */
#define	BUF_LOCKRECURSED(bp)						\
	lockmgr_recursed(&(bp)->b_lock)

/*
 * Check if a buffer lock is currently held.
 */
#define	BUF_ISLOCKED(bp)						\
	lockstatus(&(bp)->b_lock)
/*
 * Free a buffer lock.
 */
#define BUF_LOCKFREE(bp) 						\
	lockdestroy(&(bp)->b_lock)

/*
 * Print informations on a buffer lock.
 */
#define BUF_LOCKPRINTINFO(bp) 						\
	lockmgr_printinfo(&(bp)->b_lock)

/*
 * Buffer lock assertions.
 */
#if defined(INVARIANTS) && defined(INVARIANT_SUPPORT)
#define	BUF_ASSERT_LOCKED(bp)						\
	_lockmgr_assert(&(bp)->b_lock, KA_LOCKED, LOCK_FILE, LOCK_LINE)
#define	BUF_ASSERT_SLOCKED(bp)						\
	_lockmgr_assert(&(bp)->b_lock, KA_SLOCKED, LOCK_FILE, LOCK_LINE)
#define	BUF_ASSERT_XLOCKED(bp)						\
	_lockmgr_assert(&(bp)->b_lock, KA_XLOCKED, LOCK_FILE, LOCK_LINE)
#define	BUF_ASSERT_UNLOCKED(bp)						\
	_lockmgr_assert(&(bp)->b_lock, KA_UNLOCKED, LOCK_FILE, LOCK_LINE)
#define	BUF_ASSERT_HELD(bp)
#define	BUF_ASSERT_UNHELD(bp)
#else
#define	BUF_ASSERT_LOCKED(bp)
#define	BUF_ASSERT_SLOCKED(bp)
#define	BUF_ASSERT_XLOCKED(bp)
#define	BUF_ASSERT_UNLOCKED(bp)
#define	BUF_ASSERT_HELD(bp)
#define	BUF_ASSERT_UNHELD(bp)
#endif

#ifdef _SYS_PROC_H_	/* Avoid #include <sys/proc.h> pollution */
/*
 * When initiating asynchronous I/O, change ownership of the lock to the
 * kernel. Once done, the lock may legally released by biodone. The
 * original owning process can no longer acquire it recursively, but must
 * wait until the I/O is completed and the lock has been freed by biodone.
 */
#define	BUF_KERNPROC(bp)						\
	_lockmgr_disown(&(bp)->b_lock, LOCK_FILE, LOCK_LINE)
#endif

#endif /* _KERNEL */

struct buf_queue_head {
	TAILQ_HEAD(buf_queue, buf) queue;
	daddr_t last_pblkno;
	struct	buf *insert_point;
	struct	buf *switch_point;
};

/*
 * This structure describes a clustered I/O. 此结构描述集群I/O
 * 意思可能是把一些有某种联系的io进行合并，然后统一执行io操作
 */
struct cluster_save {
	long	bs_bcount;		/* Saved b_bcount. */
	long	bs_bufsize;		/* Saved b_bufsize. */
	int	bs_nchildren;		/* Number of associated buffers. 关联的buffer的数量 */
	struct buf **bs_children;	/* List of associated buffers. buffer队列 */
};

#ifdef _KERNEL

static __inline int
bwrite(struct buf *bp)
{

	KASSERT(bp->b_bufobj != NULL, ("bwrite: no bufobj bp=%p", bp));
	KASSERT(bp->b_bufobj->bo_ops != NULL, ("bwrite: no bo_ops bp=%p", bp));
	KASSERT(bp->b_bufobj->bo_ops->bop_write != NULL,
	    ("bwrite: no bop_write bp=%p", bp));
	return (BO_WRITE(bp->b_bufobj, bp));
}

static __inline void
bstrategy(struct buf *bp)
{

	KASSERT(bp->b_bufobj != NULL, ("bstrategy: no bufobj bp=%p", bp));
	KASSERT(bp->b_bufobj->bo_ops != NULL,
	    ("bstrategy: no bo_ops bp=%p", bp));
	KASSERT(bp->b_bufobj->bo_ops->bop_strategy != NULL,
	    ("bstrategy: no bop_strategy bp=%p", bp));
	BO_STRATEGY(bp->b_bufobj, bp);
}

static __inline void
buf_start(struct buf *bp)
{
	if (bioops.io_start)
		(*bioops.io_start)(bp);
}

static __inline void
buf_complete(struct buf *bp)
{
	if (bioops.io_complete)
		(*bioops.io_complete)(bp);
}

static __inline void
buf_deallocate(struct buf *bp)
{
	if (bioops.io_deallocate)
		(*bioops.io_deallocate)(bp);
}

static __inline int
buf_countdeps(struct buf *bp, int i)
{
	if (bioops.io_countdeps)
		return ((*bioops.io_countdeps)(bp, i));
	else
		return (0);
}

static __inline void
buf_track(struct buf *bp, const char *location)
{

#if defined(FULL_BUF_TRACKING)
	bp->b_io_tracking[BUF_TRACKING_ENTRY(bp->b_io_tcnt++)] = location;
#elif defined(BUF_TRACKING)
	bp->b_io_tracking = location;
#endif
}

#endif /* _KERNEL */

/*
 * Zero out the buffer's data area.
 */
#define	clrbuf(bp) {							\
	bzero((bp)->b_data, (u_int)(bp)->b_bcount);			\
	(bp)->b_resid = 0;						\
}

/*
 * Flags for getblk's last parameter.
 */
#define	GB_LOCK_NOWAIT	0x0001		/* Fail if we block on a buf lock. */
#define	GB_NOCREAT	0x0002		/* Don't create a buf if not found. */
#define	GB_NOWAIT_BD	0x0004		/* Do not wait for bufdaemon. */
/*
	当两个进程试图共享同一块内存区域的时候，可以调用 mmap 函数。实现方式就是设置 flags 为
	shared 类型还是 private 类型指明该内存区域的属性
*/
#define	GB_UNMAPPED	0x0008		/* Do not mmap buffer pages. */
#define	GB_KVAALLOC	0x0010		/* But allocate KVA. */
#define	GB_CKHASH	0x0020		/* If reading, calc checksum hash */
#define	GB_NOSPARSE	0x0040		/* Do not instantiate holes */

#ifdef _KERNEL
extern int	nbuf;			/* The number of buffer headers */
extern long	maxswzone;		/* Max KVA for swap structures */
extern long	maxbcache;		/* Max KVA for buffer cache */
extern int	maxbcachebuf;		/* Max buffer cache block size */
extern long	runningbufspace;
extern long	hibufspace;
extern int	dirtybufthresh;
extern int	bdwriteskip;
extern int	dirtybufferflushes;
extern int	altbufferflushes;
extern int	nswbuf;			/* Number of swap I/O buffer headers. */
extern int	cluster_pbuf_freecnt;	/* Number of pbufs for clusters */
extern int	vnode_pbuf_freecnt;	/* Number of pbufs for vnode pager */
extern int	vnode_async_pbuf_freecnt; /* Number of pbufs for vnode pager,
					     asynchronous reads */
extern caddr_t	unmapped_buf;	/* Data address for unmapped buffers. */

static inline int
buf_mapped(struct buf *bp)
{

	return (bp->b_data != unmapped_buf);
}

void	runningbufwakeup(struct buf *);
void	waitrunningbufspace(void);
caddr_t	kern_vfs_bio_buffer_alloc(caddr_t v, long physmem_est);
void	bufinit(void);
void	bufshutdown(int);
void	bdata2bio(struct buf *bp, struct bio *bip);
void	bwillwrite(void);
int	buf_dirty_count_severe(void);
void	bremfree(struct buf *);
void	bremfreef(struct buf *);	/* XXX Force bremfree, only for nfs. */
/*
	在一个场景中，传入的是 blkno = SBLOCK = 2，size = SBSIZE = 1024，
	cred = NOCRED，获取到的数据放到 bpp 当中，vp的话猜测应该是挂载点对应的 vnode
*/
#define bread(vp, blkno, size, cred, bpp) \
	    breadn_flags(vp, blkno, size, NULL, NULL, 0, cred, 0, NULL, bpp)
#define bread_gb(vp, blkno, size, cred, gbflags, bpp) \
	    breadn_flags(vp, blkno, size, NULL, NULL, 0, cred, \
		gbflags, NULL, bpp)
#define breadn(vp, blkno, size, rablkno, rabsize, cnt, cred, bpp) \
	    breadn_flags(vp, blkno, size, rablkno, rabsize, cnt, cred, \
		0, NULL, bpp)
int	breadn_flags(struct vnode *, daddr_t, int, daddr_t *, int *, int,
	    struct ucred *, int, void (*)(struct buf *), struct buf **);
void	bdwrite(struct buf *);
void	bawrite(struct buf *);
void	babarrierwrite(struct buf *);
int	bbarrierwrite(struct buf *);
void	bdirty(struct buf *);
void	bundirty(struct buf *);
void	bufstrategy(struct bufobj *, struct buf *);
void	brelse(struct buf *);
void	bqrelse(struct buf *);
int	vfs_bio_awrite(struct buf *);
void	vfs_drain_busy_pages(struct buf *bp);
struct buf *     getpbuf(int *);
struct buf *incore(struct bufobj *, daddr_t);
struct buf *gbincore(struct bufobj *, daddr_t);
struct buf *getblk(struct vnode *, daddr_t, int, int, int, int);
int	getblkx(struct vnode *vp, daddr_t blkno, int size, int slpflag,
	    int slptimeo, int flags, struct buf **bpp);
struct buf *geteblk(int, int);
int	bufwait(struct buf *);
int	bufwrite(struct buf *);
void	bufdone(struct buf *);
void	bd_speedup(void);

int	cluster_read(struct vnode *, u_quad_t, daddr_t, long,
	    struct ucred *, long, int, int, struct buf **);
int	cluster_wbuild(struct vnode *, long, daddr_t, int, int);
void	cluster_write(struct vnode *, struct buf *, u_quad_t, int, int);
void	vfs_bio_brelse(struct buf *bp, int ioflags);
void	vfs_bio_bzero_buf(struct buf *bp, int base, int size);
void	vfs_bio_clrbuf(struct buf *);
void	vfs_bio_set_flags(struct buf *bp, int ioflags);
void	vfs_bio_set_valid(struct buf *, int base, int size);
void	vfs_busy_pages(struct buf *, int clear_modify);
void	vfs_unbusy_pages(struct buf *);
int	vmapbuf(struct buf *, int);
void	vunmapbuf(struct buf *);
void	relpbuf(struct buf *, int *);
void	brelvp(struct buf *);
void	bgetvp(struct vnode *, struct buf *);
void	pbgetbo(struct bufobj *bo, struct buf *bp);
void	pbgetvp(struct vnode *, struct buf *);
void	pbrelbo(struct buf *);
void	pbrelvp(struct buf *);
int	allocbuf(struct buf *bp, int size);
void	reassignbuf(struct buf *);
struct	buf *trypbuf(int *);
void	bwait(struct buf *, u_char, const char *);
void	bdone(struct buf *);

typedef daddr_t (vbg_get_lblkno_t)(struct vnode *, vm_ooffset_t);
typedef int (vbg_get_blksize_t)(struct vnode *, daddr_t);
int	vfs_bio_getpages(struct vnode *vp, struct vm_page **ma, int count,
	    int *rbehind, int *rahead, vbg_get_lblkno_t get_lblkno,
	    vbg_get_blksize_t get_blksize);

#endif /* _KERNEL */

#endif /* !_SYS_BUF_H_ */
