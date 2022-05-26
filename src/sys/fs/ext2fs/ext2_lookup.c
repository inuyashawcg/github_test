/*-
 *  modified for Lites 1.1
 *
 *  Aug 1995, Godmar Back (gback@cs.utah.edu)
 *  University of Utah, Department of Computer Science
 */
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
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
 *	@(#)ufs_lookup.c	8.6 (Berkeley) 4/1/94
 * $FreeBSD: releng/12.0/sys/fs/ext2fs/ext2_lookup.c 341085 2018-11-27 17:58:25Z markj $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/endian.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/dirent.h>
#include <sys/sysctl.h>

#include <ufs/ufs/dir.h>

#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/inode.h>
#include <fs/ext2fs/ext2_mount.h>
#include <fs/ext2fs/ext2fs.h>
#include <fs/ext2fs/ext2_dinode.h>
#include <fs/ext2fs/ext2_dir.h>
#include <fs/ext2fs/ext2_extern.h>
#include <fs/ext2fs/fs.h>

#ifdef INVARIANTS
static int dirchk = 1;
#else
static int dirchk = 0;
#endif

static SYSCTL_NODE(_vfs, OID_AUTO, e2fs, CTLFLAG_RD, 0, "EXT2FS filesystem");
SYSCTL_INT(_vfs_e2fs, OID_AUTO, dircheck, CTLFLAG_RW, &dirchk, 0, "");

/*
   DIRBLKSIZE in ffs is DEV_BSIZE (in most cases 512)
   while it is the native blocksize in ext2fs - thus, a #define
   is no longer appropriate
*/
#undef  DIRBLKSIZ

/* 
	文件类型与目录类型的相互转换 
*/
/* file type to directory type ? */
static u_char ext2_ft_to_dt[] = {
	DT_UNKNOWN,		/* EXT2_FT_UNKNOWN */
	DT_REG,			/* EXT2_FT_REG_FILE */
	DT_DIR,			/* EXT2_FT_DIR */
	DT_CHR,			/* EXT2_FT_CHRDEV */
	DT_BLK,			/* EXT2_FT_BLKDEV */
	DT_FIFO,		/* EXT2_FT_FIFO */
	DT_SOCK,		/* EXT2_FT_SOCK */
	DT_LNK,			/* EXT2_FT_SYMLINK */
};
#define	FTTODT(ft) \
    ((ft) < nitems(ext2_ft_to_dt) ? ext2_ft_to_dt[(ft)] : DT_UNKNOWN)

static u_char dt_to_ext2_ft[] = {
	EXT2_FT_UNKNOWN,	/* DT_UNKNOWN */
	EXT2_FT_FIFO,		/* DT_FIFO */
	EXT2_FT_CHRDEV,		/* DT_CHR */
	EXT2_FT_UNKNOWN,	/* unused */
	EXT2_FT_DIR,		/* DT_DIR */
	EXT2_FT_UNKNOWN,	/* unused */
	EXT2_FT_BLKDEV,		/* DT_BLK */
	EXT2_FT_UNKNOWN,	/* unused */
	EXT2_FT_REG_FILE,	/* DT_REG */
	EXT2_FT_UNKNOWN,	/* unused */
	EXT2_FT_SYMLINK,	/* DT_LNK */
	EXT2_FT_UNKNOWN,	/* unused */
	EXT2_FT_SOCK,		/* DT_SOCK */
	EXT2_FT_UNKNOWN,	/* unused */
	EXT2_FT_UNKNOWN,	/* DT_WHT */
};
#define	DTTOFT(dt) \
    ((dt) < nitems(dt_to_ext2_ft) ? dt_to_ext2_ft[(dt)] : EXT2_FT_UNKNOWN)

static int	ext2_dirbadentry(struct vnode *dp, struct ext2fs_direct_2 *de,
		    int entryoffsetinblock);
static int	ext2_is_dot_entry(struct componentname *cnp);
static int	ext2_lookup_ino(struct vnode *vdp, struct vnode **vpp,
		    struct componentname *cnp, ino_t *dd_ino);

/* 通过组件名称判断是不是 . 目录项 */
static int
ext2_is_dot_entry(struct componentname *cnp)
{
	if (cnp->cn_namelen <= 2 && cnp->cn_nameptr[0] == '.' &&
	    (cnp->cn_nameptr[1] == '.' || cnp->cn_nameptr[1] == '\0'))
		return (1);
	return (0);
}

/*
 * Vnode op for reading directories.
 * 读取目录项的 vnode operations
 * 应用程序应该是构造出一个 vop_readdir_args 结构体
 */
int
ext2_readdir(struct vop_readdir_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct buf *bp;
	struct inode *ip;
	struct ext2fs_direct_2 *dp, *edp;	/* 可能表示的是起始和终止目录项 */
	u_long *cookies;
	struct dirent dstdp;
	off_t offset, startoffset;
	size_t readcnt, skipcnt;
	ssize_t startresid;
	u_int ncookies;
	int DIRBLKSIZ = VTOI(ap->a_vp)->i_e2fs->e2fs_bsize;	/* in-memory superblock -> block size */
	int error;

	/* uio 数据是不可能小于0的，为0的时候说明数据传输已经完毕 */
	if (uio->uio_offset < 0)	
		return (EINVAL);
	ip = VTOI(vp);
	/*
		cookies 其中一种翻译是网络饼干，表示的是用户发送给中央信息处理器的数据。用在这里的话表示的可能就是
		从内核空间发送到用户空间的数据长度计数
	*/
	if (ap->a_ncookies != NULL) {
		if (uio->uio_resid < 0)
			ncookies = 0;
		else
			ncookies = uio->uio_resid;	/* ncookies 赋值为 uio 剩余字节数，ncookies 表示的就是剩余多少字节要处理 */
		if (uio->uio_offset >= ip->i_size)	/* 如果uio_offset偏移量已经大于文件大小，令剩余处理字节数为0 */
			ncookies = 0;
		else if (ip->i_size - uio->uio_offset < ncookies)	
			ncookies = ip->i_size - uio->uio_offset;	/* 如果文件中剩余字节数已经小于需要uio处理的字节数，那就以剩余
																												文件字节数作为需要处理的字节数 */

		ncookies = ncookies / (offsetof(struct ext2fs_direct_2,
		    e2d_namlen) + 4) + 1;
		cookies = malloc(ncookies * sizeof(*cookies), M_TEMP, M_WAITOK);
		*ap->a_ncookies = ncookies;
		*ap->a_cookies = cookies;
	} else {
		ncookies = 0;
		cookies = NULL;
	}	// end if ap->a_ncookies != NULL

	offset = startoffset = uio->uio_offset;	/* 确定数据在文件中的起始偏移量 */
	startresid = uio->uio_resid;	/* 仍然需要被处理的数据 */
	error = 0;

	/*
		while 循环执行的条件:
			- uio 剩余要处理的数据长度不为0
			- uio offset 要小于 inode size
	*/
	while (error == 0 && uio->uio_resid > 0 &&
	    uio->uio_offset < ip->i_size) {	
		error = ext2_blkatoff(vp, uio->uio_offset, NULL, &bp);
		if (error)
			break;
		/* 
			bp->b_offset 表示的是 lbn * bsize，也就是说当前偏移量 + bcount(当前情形下貌似是 block size)已经比 inode
			对应的文件长度要大了，也就是说明剩余需要被处理的数据已经装不满一整个块了。所以 readcnt 表示这一次操作需要读取的
			数据的长度，最大好像也就是一个数据块大小
		*/
		if (bp->b_offset + bp->b_bcount > ip->i_size)
			readcnt = ip->i_size - bp->b_offset;
		else
			readcnt = bp->b_bcount;	/* 否则就取 b_count */

		/*
			uio_offset 表示的应该是数据在文件中的真正偏移量，但是这个偏移量可能刚好处在一个块的中间位置。读取的时候不是刚好
			从 offset 处开始的，而是将 offset 所处的这个块的数据全部读取出来。bp->b_offset 表示的应该就是这个块的起始位置，
			uio->uio_offset 包含在这个块中。然后再将两者相减得到所需要的数据在这个块中的偏移，即 skipcnt。最后用 bp->b_offset
			就得到了数据的真实起始位置
		*/
		skipcnt = (size_t)(uio->uio_offset - bp->b_offset) &
		    ~(size_t)(DIRBLKSIZ - 1);
		offset = bp->b_offset + skipcnt;
		dp = (struct ext2fs_direct_2 *)&bp->b_data[skipcnt];
		edp = (struct ext2fs_direct_2 *)&bp->b_data[readcnt];

		while (error == 0 && uio->uio_resid > 0 && dp < edp) {
			/*
				当计算出的目录项长度大于结构体长度字段值，或者超过访问位置上界时，报错
			*/
			if (dp->e2d_reclen <= offsetof (struct ext2fs_direct_2,
			    e2d_namlen) || (caddr_t)dp + dp->e2d_reclen >
			    (caddr_t)edp) {
				error = EIO;
				break;
			}
			/*-
			 * "New" ext2fs directory entries differ in 3 ways
			 * from ufs on-disk ones:
			 * - the name is not necessarily NUL-terminated.
			 * - the file type field always exists and always
			 *   follows the name length field.
			 * - the file type is encoded in a different way.
			 *
			 * "Old" ext2fs directory entries need no special
			 * conversions, since they are binary compatible
			 * with "new" entries having a file type of 0 (i.e.,
			 * EXT2_FT_UNKNOWN).  Splitting the old name length
			 * field didn't make a mess like it did in ufs,
			 * because ext2fs uses a machine-independent disk
			 * layout.
			 */
			dstdp.d_namlen = dp->e2d_namlen;
			dstdp.d_type = FTTODT(dp->e2d_type);
			if (offsetof(struct ext2fs_direct_2, e2d_namlen) +
			    dstdp.d_namlen > dp->e2d_reclen) {
				error = EIO;
				break;
			}
			if (offset < startoffset || dp->e2d_ino == 0)
				goto nextentry;
			dstdp.d_fileno = dp->e2d_ino;
			dstdp.d_reclen = GENERIC_DIRSIZ(&dstdp);
			bcopy(dp->e2d_name, dstdp.d_name, dstdp.d_namlen);
			dirent_terminate(&dstdp);
			if (dstdp.d_reclen > uio->uio_resid) {
				if (uio->uio_resid == startresid)
					error = EINVAL;
				else
					error = EJUSTRETURN;
				break;
			}
			/* Advance dp. */
			error = uiomove((caddr_t)&dstdp, dstdp.d_reclen, uio);
			if (error)
				break;
			if (cookies != NULL) {
				KASSERT(ncookies > 0,
				    ("ext2_readdir: cookies buffer too small"));
				*cookies = offset + dp->e2d_reclen;
				cookies++;
				ncookies--;
			}
nextentry:
			offset += dp->e2d_reclen;
			dp = (struct ext2fs_direct_2 *)((caddr_t)dp +
			    dp->e2d_reclen);
		}	// enf while in

		bqrelse(bp);
		uio->uio_offset = offset;
	}	// end while out

	/* We need to correct uio_offset. */
	uio->uio_offset = offset;
	if (error == EJUSTRETURN)
		error = 0;
	if (ap->a_ncookies != NULL) {
		if (error == 0) {
			ap->a_ncookies -= ncookies;
		} else {
			free(*ap->a_cookies, M_TEMP);
			*ap->a_ncookies = 0;
			*ap->a_cookies = NULL;
		}
	}
	if (error == 0 && ap->a_eofflag)
		*ap->a_eofflag = ip->i_size <= uio->uio_offset;
	return (error);
}

/*
 * Convert a component of a pathname into a pointer to a locked inode.
 * This is a very central and rather complicated routine.
 * If the file system is not maintained in a strict tree hierarchy,
 * this can result in a deadlock situation (see comments in code below).
 * 
 * 将路径名的组件转换为指向锁定 inode 的指针。这是一个非常核心和相当复杂的程序。
 * 如果文件系统没有在严格的树层次结构中维护，这可能会导致死锁情况
 *
 * The cnp->cn_nameiop argument is LOOKUP, CREATE, RENAME, or DELETE depending
 * on whether the name is to be looked up, created, renamed, or deleted.
 * When CREATE, RENAME, or DELETE is specified, information usable in
 * creating, renaming, or deleting a directory entry may be calculated.
 * If flag has LOCKPARENT or'ed into it and the target of the pathname
 * exists, lookup returns both the target and its parent directory locked.
 * When creating or renaming and LOCKPARENT is specified, the target may
 * not be ".".  When deleting and LOCKPARENT is specified, the target may
 * be "."., but the caller must check to ensure it does an vrele and vput
 * instead of two vputs.
 *
 * Overall outline of ext2_lookup:
 *
 *	search for name in directory, to found or notfound
 * notfound:
 *	if creating, return locked directory, leaving info on available slots
 *	else return error
 * found:
 *	if at end of path and deleting, return information to allow delete
 *	if at end of path and rewriting (RENAME and LOCKPARENT), lock target
 *	  inode and return info to allow rewrite
 *	if not at end, add name to cache; if at end and neither creating
 *	  nor deleting, add name to cache
 */
int
ext2_lookup(struct vop_cachedlookup_args *ap)
{
	/*
		vfs_lookup.c 文件 lookup 函数中调用了 VOP_LOOKUP 宏，对应到不同文件系统中的 lookup 函数。
		传入参数一共包含3个，a_dvp 表示的是当前文件所在的目录项对应的 vnode。如果该目录项是一个挂载点
		的情况是如何处理的？
	*/
	return (ext2_lookup_ino(ap->a_dvp, ap->a_vpp, ap->a_cnp, NULL));
}

/*
	ext2_lookup_ino 函数将查找、重命名、创建等操作都统一到了一个函数当中，从函数的代码分支中可以看出。
	目录树需不需要分解？
*/
static int
ext2_lookup_ino(struct vnode *vdp, struct vnode **vpp, struct componentname *cnp,
    ino_t *dd_ino)
{
	struct inode *dp;		/* inode for directory being searched 正在搜索的目录的 inode */
	struct buf *bp;			/* a buffer of directory entries 目录项的缓冲区 */
	struct ext2fs_direct_2 *ep;	/* the current directory entry 当前目录项 */
	int entryoffsetinblock;		/* offset of ep in bp's buffer 表示的应该是 htree entry 在 block 中的 offset */
	struct ext2fs_searchslot ss;
	doff_t i_diroff;		/* cached i_diroff value */
	doff_t i_offset;		/* cached i_offset value */
	int numdirpasses;		/* strategy for directory search 目录查找策略 */
	doff_t endsearch;		/* offset to end directory search 目录查找终止位置的偏移量 */
	doff_t prevoff;			/* prev entry dp->i_offset 上一个 entry 的 offset */
	struct vnode *pdp;		/* saved dp during symlink work 符号链接时候保存的 vnode 指针 */
	struct vnode *tdp;		/* returned by VFS_VGET 宏定义返回的 vnode 指针 */
	doff_t enduseful;		/* pointer past last used dir slot 指向最后一次使用的目录slot后？ */
	u_long bmask;			/* block offset mask 块偏移标志，应该是用于计算数据在块中所占大小，偏移等 */
	int error;
	struct ucred *cred = cnp->cn_cred;	/* 用户凭证 */
	int flags = cnp->cn_flags;
	int nameiop = cnp->cn_nameiop;	/* 判断文件操作，创建、重命名等等 */
	ino_t ino, ino1;
	int ltype;
	int entry_found = 0;
	/* 需要确定 block size 的大小，所以在 in-memory inode 中保存了一个指向超级块的指针 */
	int DIRBLKSIZ = VTOI(vdp)->i_e2fs->e2fs_bsize;

	if (vpp != NULL)
		*vpp = NULL;

	/* 表示的应该是目录项 inode 节点 */
	dp = VTOI(vdp);
	bmask = VFSTOEXT2(vdp->v_mount)->um_mountp->mnt_stat.f_iosize - 1;
restart:
	bp = NULL;	/* buf置空 */
	/*
		空闲区域的 offset，表示的应该就是空闲区域的起始位置
	*/
	ss.slotoffset = -1;

	/*
	 * We now have a segment name to search for, and a directory to search.
	 *
	 * Suppress search for slots unless creating
	 * file and at end of pathname, in which case
	 * we watch for a place to put the new file in
	 * case it doesn't already exist.
	 * 禁止对插槽的搜索，除非创建文件并在路径名的末尾，在这种情况下，我们将监视放置
	 * 新文件的位置，以防它不存在
	 * 
	 * 从注释中我们可以看到， i_diroff 表示的是用于查找最后一个 dirent。它可能表示两种情况，第一种就是
	 * 表示在块最后的那个 entry 所在的位置；第二种情况表示的是当前已有的 entry 的最后一个。 ufs 中也有
	 * 对应的字段，可以通过 gdb 调试看一下其表示的具体含义。
	 * 另外一种可能就是 i_diroff 只用于定位目录文件最后一个数据块，而不是定位到某个具体的 entry？
	 * 
	 * dp 表示的是目标文件所在的目录项，i_diroff 赋值的是一个目录文件对应的字段，普通文件对应字段有什么差别？
	 */
	i_diroff = dp->i_diroff;	/* 用于查找目录中的最后一个 entry */
	ss.slotstatus = FOUND;	/* 把 search slot 的状态设置为 found */
	ss.slotfreespace = ss.slotsize = ss.slotneeded = 0;	/* slot 相关属性全置零 */

	/*
		如果我们要执行的操作是创建、重命名并且得是路径的最后一个组件名称，那么就执行 if 代码分支
	*/
	if ((nameiop == CREATE || nameiop == RENAME) &&
	    (flags & ISLASTCN)) {
		/* 
			当我们执行的不是查找操作的时候，设置 slot 的状态为 NONE。slotneeded 表示的应该是我当前
			需要找到一块多大的空闲区域去填充新建的 entry，而不是已有的 entry
		*/
		ss.slotstatus = NONE;
		ss.slotneeded = EXT2_DIR_REC_LEN(cnp->cn_namelen);
		/*
		 * was ss.slotneeded = (sizeof(struct direct) - MAXNAMLEN +
		 * cnp->cn_namelen + 3) &~ 3;
		 */
	}
	/*
	 * Try to lookup dir entry using htree directory index.
	 * 通过 htree 树的索引查找目录项
	 *
	 * If we got an error or we want to find '.' or '..' entry,
	 * we will fall back to linear search.
	 * 如果出现错误或要查找“.”或“..”条目，我们将退回到线性搜索；
	 * 也就是说，目录项的查找目前有两种方式：树搜索和线性搜索。在调试 tptfs 创建新的文件的时候，我们所要查找的
	 * 就不是 . 和 .. 两个目录，所以下面的代码分支是会执行的。判断条件中一个带！一个不带，这两个函数的实现逻辑
	 * 是一致的
	 * HTree 在构造的时候会在数据块起始位置放 . / .. 两个目录项，其他的目录项在超过整个数据块大小之后开始分裂，
	 * 但是 . / .. 仍然是之前的状态。可能是这个原因使得对于它们的查找依然采用线性查找的方式
	 */
	if (!ext2_is_dot_entry(cnp) && ext2_htree_has_idx(dp)) {
		numdirpasses = 1;	/* 采用策略1 */
		entryoffsetinblock = 0;	/* entry在块中的偏移量赋值为0 */
		/* 
			从tptfs调试中可以得知，cn_nameptr 传入的是我们所要创建的文件名，后面跟的是它的名字长度。i_offset 表示
			的应该就是我们所要找的目录项成员在数据块中的偏移，或者是计算文件末尾数据在数据块中的偏移
		*/
		switch (ext2_htree_lookup(dp, cnp->cn_nameptr, cnp->cn_namelen,
		    &bp, &entryoffsetinblock, &i_offset, &prevoff,
		    &enduseful, &ss)) {
		case 0:
			ep = (struct ext2fs_direct_2 *)((char *)bp->b_data +
			    (i_offset & bmask));
			goto foundentry;
		case ENOENT:
			/*
				如果没有找到对应的 entry，更新 i_offset 的值为目录项的大小，
			*/
			i_offset = roundup2(dp->i_size, DIRBLKSIZ);
			goto notfound;
		default:
			/*
			 * Something failed; just fallback to do a linear
			 * search.
			 */
			break;
		}	/* end switch */
	}	/* end if */

	/*
	 * If there is cached information on a previous search of
	 * this directory, pick up where we last left off.
	 * We cache only lookups as these are the most common
	 * and have the greatest payoff. Caching CREATE has little
	 * benefit as it usually must search the entire directory
	 * to determine that the entry does not exist. Caching the
	 * location of the last DELETE or RENAME has not reduced
	 * profiling time and hence has been removed in the interest
	 * of simplicity.
	 * 如果在此目录的上一次搜索中有缓存信息，请选择上次停止的位置。我们只缓存查找，因为这些是最常见的，
	 * 并且有最大的回报。缓存 CREATE 没有什么好处，因为它通常必须搜索整个目录以确定条目不存在。缓存最后
	 * 一次删除或重命名的位置并没有减少分析时间，因此为了简单起见，已将其删除
	 * 
	 * 上面的代码分支执行的是 hash index 的查找方式，如果查找出错，那么将执行下面的代码分支。可以看一下
	 * 上面的代码逻辑，如果通过 hash 找到了对应的目录项 entry(这个肯定是保存在目录项数据块，跟 . 和.. 
	 * 不在同一个位置)，所以一些局部变量可以通过不同的应用场景来分析其具体的作用是什么
	 */
	if (nameiop != LOOKUP || i_diroff == 0 ||
	    i_diroff > dp->i_size) {
		/*
			线性查找过程除了 . 和 .. 两个目录项，其他的应该就不会再涉及到 root node，都是直接操作存储目录项的
			数据块。所以 entryoffsetinblock 表示的应该是目录项在块数据中的偏移，应该不是 hash 查找中的 entry

			这里表示的就是不利用缓存信息，从头开始遍历整个数据块。i_diroff > dp->i_size 可能的情况就是目录中的
			一个中间的 entry 删除了，所以就会有多一个空闲空间。原本 i_diroff 跟 i_size 可能是相等的，删除之后
			就会出现上述情况。
			i_offset 表示的可用空间的偏移量。当目录没有空洞的时候，它应该指向最后一个 entry 的下一个位置。但是当
			出现空洞的时候，那我们查找可用空间存放新创建的 entry 时，应该就要从头开始查找，也就是将 i_offset 置零，
			同时 entryoffsetinblock 置零
		*/
		entryoffsetinblock = 0;
		i_offset = 0;
		numdirpasses = 1;
	} else {
		/*
			当我们执行查找操作的时候，我们会利用上一次查找遗留的相关信息接着查找。遗留的信息应该就是对应到 i_diroff 或者
			i_offset 当中，所以这里直接将 entryoffsetinblock 赋值为 i_offset，也就是 i_diroff
		*/
		i_offset = i_diroff;
		if ((entryoffsetinblock = i_offset & bmask) &&
		    (error = ext2_blkatoff(vdp, (off_t)i_offset, NULL,
		    &bp)))
			return (error);
		numdirpasses = 2;
		nchstats.ncs_2passes++;
	}
	/*
		从上面的注释可以看到，当我们执行的是 lookup 操作的时候，可以利用上次的缓存信息，可能就不需要重新遍历
		整个数据块中的 entry；但如果我们执行的是 create 操作，那就必须重新搜索整个数据块，确保 entry 不存在。
		所以也就不需要利用缓存信息，不走 else 分支。就是为了获取 i_offset，即上一次在目录项查找的 offset
	*/
	prevoff = i_offset;
	/*
		roundup2 宏的作用应该类似于四舍五入。这里的话就比如说目录的大小为 4000 个字节，但是每个数据块的大小是
		4096 个字节，利用 roundup2 就可以将 4000 扩展到距离最近的 4096 的整数倍，也就是 4096 * 1。所以，
		endsearch = 4096 * n。
	*/
	endsearch = roundup2(dp->i_size, DIRBLKSIZ);
	enduseful = 0;

searchloop:
	while (i_offset < endsearch) {
		/*
		 * If necessary, get the next directory block.
		 		如果有必要的话，获取下一个文件项的数据块。当 i_offset < endsearch 的时，跳出 while 循环。说明下面
				代码肯定会有增大 i_offset 的操作。
		 */
		if (bp != NULL)
			brelse(bp);
		/* 
			读取偏移量 i_offset 所在的数据块。ext2_blkatoff 函数参数2表示的是数据在文件中的相对偏移量，
			所以可以知道 i_offset 表示的就是数据在目录文件中的相对偏移量
		*/
		error = ext2_blkatoff(vdp, (off_t)i_offset, NULL, &bp);
		if (error != 0)
			return (error);

		entryoffsetinblock = 0;	// entryoffsetinblock 重新置零
		if (ss.slotstatus == NONE) {
			ss.slotoffset = -1;
			ss.slotfreespace = 0;	// 注意，这里将 freespace 设置为了0
		}

		error = ext2_search_dirblock(dp, bp->b_data, &entry_found,
		    cnp->cn_nameptr, cnp->cn_namelen,
		    &entryoffsetinblock, &i_offset, &prevoff,
		    &enduseful, &ss);
		if (error != 0) {
			brelse(bp);
			return (error);
		}
		if (entry_found) {
			ep = (struct ext2fs_direct_2 *)((char *)bp->b_data +
			    (entryoffsetinblock & bmask));
foundentry:
			ino = ep->e2d_ino;	/* inode number 赋值 */
			goto found;
		}
	}	// end while
notfound:
	/*
	 * If we started in the middle of the directory and failed
	 * to find our target, we must check the beginning as well.
	 * 
	 * 从注释可以看出，i_diroff 可以认为是一个指针，当我们上次已经查找过该目录项之后，它会停止到一个位置。
	 * 当我们下次又要查找该目录项的时候，我们就从这个中间位置开始查找，所以肯定会有找不到目标文件的情况。如果
	 * 出现了，则令 i_offset = 0，endsearch 设置为中间位置(即 i_diroff)，相当与就是查找目录的前一部分。
	 * numdirpasses = 1 应该就表示从头开始查找，如果是 2 就表示从中间某个位置开始查找
	 */
	if (numdirpasses == 2) {
		numdirpasses--;
		i_offset = 0;
		endsearch = i_diroff;
		goto searchloop;
	}
	if (bp != NULL)
		brelse(bp);
	/*
	 * If creating, and at end of pathname and current
	 * directory has not been removed, then can consider
	 * allowing file to be created.
	 */
	if ((nameiop == CREATE || nameiop == RENAME) &&
	    (flags & ISLASTCN) && dp->i_nlink != 0) {
		/*
		 * Access for write is interpreted as allowing
		 * creation of files in the directory.
		 */
		if ((error = VOP_ACCESS(vdp, VWRITE, cred, cnp->cn_thread)) != 0)
			return (error);
		/*
		 * Return an indication of where the new directory
		 * entry should be put.  If we didn't find a slot,
		 * then set dp->i_count to 0 indicating
		 * that the new slot belongs at the end of the
		 * directory. If we found a slot, then the new entry
		 * can be put in the range from dp->i_offset to
		 * dp->i_offset + dp->i_count.
		 */
		if (ss.slotstatus == NONE) {
			dp->i_offset = roundup2(dp->i_size, DIRBLKSIZ);
			dp->i_count = 0;
			enduseful = dp->i_offset;
		} else {
			dp->i_offset = ss.slotoffset;
			dp->i_count = ss.slotsize;
			if (enduseful < ss.slotoffset + ss.slotsize)
				enduseful = ss.slotoffset + ss.slotsize;
		}
		dp->i_endoff = roundup2(enduseful, DIRBLKSIZ);
		/*
		 * We return with the directory locked, so that
		 * the parameters we set up above will still be
		 * valid if we actually decide to do a direnter().
		 * We return ni_vp == NULL to indicate that the entry
		 * does not currently exist; we leave a pointer to
		 * the (locked) directory inode in ndp->ni_dvp.
		 * The pathname buffer is saved so that the name
		 * can be obtained later.
		 *
		 * NB - if the directory is unlocked, then this
		 * information cannot be used.
		 */
		cnp->cn_flags |= SAVENAME;
		return (EJUSTRETURN);
	}
	/*
	 * Insert name into cache (as non-existent) if appropriate.
	 */
	if ((cnp->cn_flags & MAKEENTRY) != 0)
		cache_enter(vdp, NULL, cnp);
	return (ENOENT);

found:
	if (dd_ino != NULL)
		*dd_ino = ino;	/* inode number指针赋值 */
	if (numdirpasses == 2)	/* 判断是否采用策略2 */
		nchstats.ncs_pass2++;
	/*
	 * Check that directory length properly reflects presence
	 * of this entry. 检查目录长度是否正确反映了此条目的存在
	 */
	if (entryoffsetinblock + EXT2_DIR_REC_LEN(ep->e2d_namlen)
	    > dp->i_size) {
		ext2_dirbad(dp, i_offset, "i_size too small");
		dp->i_size = entryoffsetinblock + EXT2_DIR_REC_LEN(ep->e2d_namlen);
		dp->i_flag |= IN_CHANGE | IN_UPDATE;
	}
	brelse(bp);

	/*
	 * Found component in pathname.
	 * If the final component of path name, save information
	 * in the cache as to where the entry was found.
	 */
	if ((flags & ISLASTCN) && nameiop == LOOKUP)
		dp->i_diroff = rounddown2(i_offset, DIRBLKSIZ);
	/*
	 * If deleting, and at end of pathname, return
	 * parameters which can be used to remove file.
	 */
	if (nameiop == DELETE && (flags & ISLASTCN)) {
		if (flags & LOCKPARENT)
			ASSERT_VOP_ELOCKED(vdp, __FUNCTION__);
		/*
		 * Write access to directory required to delete files.
		 */
		if ((error = VOP_ACCESS(vdp, VWRITE, cred, cnp->cn_thread)) != 0)
			return (error);
		/*
		 * Return pointer to current entry in dp->i_offset,
		 * and distance past previous entry (if there
		 * is a previous entry in this block) in dp->i_count.
		 * Save directory inode pointer in ndp->ni_dvp for dirremove().
		 *
		 * Technically we shouldn't be setting these in the
		 * WANTPARENT case (first lookup in rename()), but any
		 * lookups that will result in directory changes will
		 * overwrite these.
		 */
		dp->i_offset = i_offset;
		if ((dp->i_offset & (DIRBLKSIZ - 1)) == 0)
			dp->i_count = 0;
		else
			dp->i_count = dp->i_offset - prevoff;
		if (dd_ino != NULL)
			return (0);
		if (dp->i_number == ino) {
			VREF(vdp);
			*vpp = vdp;
			return (0);
		}
		if ((error = VFS_VGET(vdp->v_mount, ino, LK_EXCLUSIVE,
		    &tdp)) != 0)
			return (error);
		/*
		 * If directory is "sticky", then user must own
		 * the directory, or the file in it, else she
		 * may not delete it (unless she's root). This
		 * implements append-only directories.
		 */
		if ((dp->i_mode & ISVTX) &&
		    cred->cr_uid != 0 &&
		    cred->cr_uid != dp->i_uid &&
		    VTOI(tdp)->i_uid != cred->cr_uid) {
			vput(tdp);
			return (EPERM);
		}
		*vpp = tdp;
		return (0);
	}

	/*
	 * If rewriting (RENAME), return the inode and the
	 * information required to rewrite the present directory
	 * Must get inode of directory entry to verify it's a
	 * regular file, or empty directory.
	 */
	if (nameiop == RENAME && (flags & ISLASTCN)) {
		if ((error = VOP_ACCESS(vdp, VWRITE, cred, cnp->cn_thread)) != 0)
			return (error);
		/*
		 * Careful about locking second inode.
		 * This can only occur if the target is ".".
		 */
		dp->i_offset = i_offset;
		if (dp->i_number == ino)
			return (EISDIR);
		if (dd_ino != NULL)
			return (0);
		if ((error = VFS_VGET(vdp->v_mount, ino, LK_EXCLUSIVE,
		    &tdp)) != 0)
			return (error);
		*vpp = tdp;
		cnp->cn_flags |= SAVENAME;
		return (0);
	}
	if (dd_ino != NULL)
		return (0);

	/*
	 * Step through the translation in the name.  We do not `vput' the
	 * directory because we may need it again if a symbolic link
	 * is relative to the current directory.  Instead we save it
	 * unlocked as "pdp".  We must get the target inode before unlocking
	 * the directory to insure that the inode will not be removed
	 * before we get it.  We prevent deadlock by always fetching
	 * inodes from the root, moving down the directory tree. Thus
	 * when following backward pointers ".." we must unlock the
	 * parent directory before getting the requested directory.
	 * There is a potential race condition here if both the current
	 * and parent directories are removed before the VFS_VGET for the
	 * inode associated with ".." returns.  We hope that this occurs
	 * infrequently since we cannot avoid this race condition without
	 * implementing a sophisticated deadlock detection algorithm.
	 * Note also that this simple deadlock detection scheme will not
	 * work if the file system has any hard links other than ".."
	 * that point backwards in the directory structure.
	 */
	pdp = vdp;
	if (flags & ISDOTDOT) {
		error = vn_vget_ino(pdp, ino, cnp->cn_lkflags, &tdp);
		if (pdp->v_iflag & VI_DOOMED) {
			if (error == 0)
				vput(tdp);
			error = ENOENT;
		}
		if (error)
			return (error);
		/*
		 * Recheck that ".." entry in the vdp directory points
		 * to the inode we looked up before vdp lock was
		 * dropped.
		 */
		error = ext2_lookup_ino(pdp, NULL, cnp, &ino1);
		if (error) {
			vput(tdp);
			return (error);
		}
		if (ino1 != ino) {
			vput(tdp);
			goto restart;
		}
		*vpp = tdp;
	} else if (dp->i_number == ino) {
		VREF(vdp);	/* we want ourself, ie "." */
		/*
		 * When we lookup "." we still can be asked to lock it
		 * differently.
		 * 从函数逻辑可以看出，当 lookup() 执行完成之后，得到的目标 vnode 应该是处于加锁的状态
		 */
		ltype = cnp->cn_lkflags & LK_TYPE_MASK;
		if (ltype != VOP_ISLOCKED(vdp)) {
			if (ltype == LK_EXCLUSIVE)
				vn_lock(vdp, LK_UPGRADE | LK_RETRY);
			else	/* if (ltype == LK_SHARED) */
				vn_lock(vdp, LK_DOWNGRADE | LK_RETRY);
		}
		*vpp = vdp;
	} else {
		if ((error = VFS_VGET(vdp->v_mount, ino, cnp->cn_lkflags,
		    &tdp)) != 0)
			return (error);
		*vpp = tdp;
	}

	/*
	 * Insert name into cache if appropriate.
	 */
	if (cnp->cn_flags & MAKEENTRY)
		cache_enter(vdp, *vpp, cnp);
	return (0);
}

/*
	从 ext2_htree.c 中的实际调用情况来看，entry offset in block pointer 传入的值是0
	foundp：指示我们是否找到 entry 的标志位
	ss：执行查找操作或者是 CREATE/RENAME 时，ss 的初始化是不一样的
*/
int
ext2_search_dirblock(struct inode *ip, void *data, int *foundp,
    const char *name, int namelen, int *entryoffsetinblockp,
    doff_t *offp, doff_t *prevoffp, doff_t *endusefulp,
    struct ext2fs_searchslot *ssp)
{
	struct vnode *vdp;
	struct ext2fs_direct_2 *ep, *top;
	uint32_t bsize = ip->i_e2fs->e2fs_bsize;
	/* offset 表示在数据块中的偏移 */
	int offset = *entryoffsetinblockp;
	int namlen;

	vdp = ITOV(ip);
	/* 
		entry pointer 指向数据块 offset 位置，top 限制了指针的访问空间大小，指向
		了数据块的末尾位置。
		这里有一点要注意，ep 和 top 都是目录项指针，也就是说 data 中存放的数据是目录项数据
	*/
	ep = (struct ext2fs_direct_2 *)((char *)data + offset);
	top = (struct ext2fs_direct_2 *)((char *)data + bsize);
	while (ep < top) {
		/*
		 * Full validation checks are slow, so we only check
		 * enough to insure forward progress through the
		 * directory. Complete checks can be run by setting
		 * "vfs.e2fs.dirchk" to be true.
		 * 判断 entry 是否满足文件系统对目录项的一些要求，比如说目录项长度是不是4的倍数，
		 * name_length 是否超标等等
		 */
		if (ep->e2d_reclen == 0 ||
		    (dirchk && ext2_dirbadentry(vdp, ep, offset))) {
			int i;
			/* mangled entry: 破损的入口 */
			ext2_dirbad(ip, *offp, "mangled entry");
			i = bsize - (offset & (bsize - 1));
			*offp += i;
			offset += i;
			continue;
		}

		/*
		 * If an appropriate sized slot has not yet been found,
		 * check to see if one is available. Also accumulate space
		 * in the current block so that we can determine if
		 * compaction is viable.
		 * 如果尚未找到合适大小的插槽，请检查是否有可用的插槽。还可以
		 * 在当前块中累积空间，以便确定压缩是否可行
		 */
		if (ssp->slotstatus != FOUND) {
			int size = ep->e2d_reclen;
			/*
				size 首先设置为目录项成员的数据长度，计算根据 namlen 得到的长度，两者相减得到
				剩余空间的大小(可能是因为数据对齐的原因，导致两者大小不一致，也有可能是两个目录项
				成员合并了)。如果还包含 tail，则再减去 tail 结构的长度
			*/
			if (ep->e2d_ino != 0)
				size -= EXT2_DIR_REC_LEN(ep->e2d_namlen);
			else if (ext2_is_dirent_tail(ip, ep))
				size -= sizeof(struct ext2fs_direct_tail);

			/* 
				经过上述处理之后的长度值仍然大于0，这个应该就是处理把两个目录项合并成一个，或者数据块的
				最后一个目录项成员的情况
			*/
			if (size > 0) {
				if (size >= ssp->slotneeded) {
					ssp->slotstatus = FOUND;
					ssp->slotoffset = *offp;
					ssp->slotsize = ep->e2d_reclen;
				} else if (ssp->slotstatus == NONE) {
					ssp->slotfreespace += size;
					if (ssp->slotoffset == -1)
						ssp->slotoffset = *offp;
					if (ssp->slotfreespace >= ssp->slotneeded) {
						ssp->slotstatus = COMPACT;
						ssp->slotsize = *offp +
						    ep->e2d_reclen -
						    ssp->slotoffset;
					}
				}
			}	// end if (size > 0)
		}	// 处理当 ssp->slotstatus ！= FOUND，也就是创建、重命名并且是最后一个组件的情况
	
		/*
		 * Check for a name match.
		 */
		if (ep->e2d_ino) {
			namlen = ep->e2d_namlen;
			if (namlen == namelen &&
			    !bcmp(name, ep->e2d_name, (unsigned)namlen)) {
				/*
				 * Save directory entry's inode number and
				 * reclen in ndp->ni_ufs area, and release
				 * directory buffer.
				 */
				*foundp = 1;
				return (0);
			}
		}
		*prevoffp = *offp;
		*offp += ep->e2d_reclen;
		offset += ep->e2d_reclen;
		*entryoffsetinblockp = offset;
		if (ep->e2d_ino)
			*endusefulp = *offp;
		/*
		 * Get pointer to the next entry.
		 */
		ep = (struct ext2fs_direct_2 *)((char *)data + offset);
	}	// end while

	return (0);
}

void
ext2_dirbad(struct inode *ip, doff_t offset, char *how)
{
	struct mount *mp;

	mp = ITOV(ip)->v_mount;
	if ((mp->mnt_flag & MNT_RDONLY) == 0)
		panic("ext2_dirbad: %s: bad dir ino %ju at offset %ld: %s\n",
		    mp->mnt_stat.f_mntonname, (uintmax_t)ip->i_number,
		    (long)offset, how);
	else
		(void)printf("%s: bad dir ino %ju at offset %ld: %s\n",
		    mp->mnt_stat.f_mntonname, (uintmax_t)ip->i_number,
		    (long)offset, how);

}

/*
 * Do consistency checking on a directory entry:
 *	record length must be multiple of 4
 *	entry must fit in rest of its DIRBLKSIZ block
 *	record must be large enough to contain entry
 *	name is not longer than MAXNAMLEN
 *	name must be as long as advertised, and null terminated
	
	对目录进行一致性检查:
		- 目录项长度必须是4的倍数
		- 条目必须适合其DIRBLKSIZ块的其余部分
		- 目录项的长度必须足够包括一个entry
		- 名字的长度不能超过 MAXNAMLEN
		- 名字必须要与展示的一样长，并且以null作为结尾
 */
/*
 *	changed so that it confirms to ext2_check_dir_entry
 */
static int
ext2_dirbadentry(struct vnode *dp, struct ext2fs_direct_2 *de,
    int entryoffsetinblock)
{
	int DIRBLKSIZ = VTOI(dp)->i_e2fs->e2fs_bsize;

	char *error_msg = NULL;

	if (de->e2d_reclen < EXT2_DIR_REC_LEN(1))
		error_msg = "rec_len is smaller than minimal";
	else if (de->e2d_reclen % 4 != 0)
		error_msg = "rec_len % 4 != 0";
	else if (de->e2d_reclen < EXT2_DIR_REC_LEN(de->e2d_namlen))
		error_msg = "reclen is too small for name_len";
	else if (entryoffsetinblock + de->e2d_reclen > DIRBLKSIZ)
		error_msg = "directory entry across blocks";
	/*
		entryoffsetinblock 从上述判断可以看出，表示的就是目录项成员在数据块中的偏移量
	*/
	/* else LATER
	     if (de->inode > dir->i_sb->u.ext2_sb.s_es->s_inodes_count)
		error_msg = "inode out of bounds";
	*/

	if (error_msg != NULL) {
		printf("bad directory entry: %s\n", error_msg);
		printf("offset=%d, inode=%lu, rec_len=%u, name_len=%u\n",
			entryoffsetinblock, (unsigned long)de->e2d_ino,
			de->e2d_reclen, de->e2d_namlen);
	}
	return error_msg == NULL ? 0 : 1;
}

/*
 * Insert an entry into the fresh directory block.
 * Initialize entry tail if the metadata_csum feature is turned on.
 */
static int
ext2_add_first_entry(struct vnode *dvp, struct ext2fs_direct_2 *entry,
    struct componentname *cnp)
{
	struct inode *dp;
	struct iovec aiov;
	struct uio auio;
	char* buf = NULL;
	int dirblksize, error;

	dp = VTOI(dvp);
	dirblksize = dp->i_e2fs->e2fs_bsize;

	if (dp->i_offset & (dirblksize - 1))
		panic("ext2_add_first_entry: bad directory offset");

	if (EXT2_HAS_RO_COMPAT_FEATURE(dp->i_e2fs,
	    EXT2F_ROCOMPAT_METADATA_CKSUM)) {
		entry->e2d_reclen = dirblksize - sizeof(struct ext2fs_direct_tail);
		buf = malloc(dirblksize, M_TEMP, M_WAITOK);
		if (!buf) {
			error = ENOMEM;
			goto out;
		}
		memcpy(buf, entry, EXT2_DIR_REC_LEN(entry->e2d_namlen));
		ext2_init_dirent_tail(EXT2_DIRENT_TAIL(buf, dirblksize));
		ext2_dirent_csum_set(dp, (struct ext2fs_direct_2 *)buf);

		auio.uio_offset = dp->i_offset;
		auio.uio_resid = dirblksize;
		aiov.iov_len = auio.uio_resid;
		aiov.iov_base = (caddr_t)buf;
	} else {
		entry->e2d_reclen = dirblksize;
		auio.uio_offset = dp->i_offset;
		auio.uio_resid = EXT2_DIR_REC_LEN(entry->e2d_namlen);
		aiov.iov_len = auio.uio_resid;
		aiov.iov_base = (caddr_t)entry;
	}

	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_rw = UIO_WRITE;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_td = (struct thread *)0;
	error = VOP_WRITE(dvp, &auio, IO_SYNC, cnp->cn_cred);
	if (error)
		goto out;

	dp->i_size = roundup2(dp->i_size, dirblksize);
	dp->i_flag |= IN_CHANGE;

out:
	free(buf, M_TEMP);
	return (error);

}

/*
 * Write a directory entry after a call to namei, using the parameters
 * that it left in nameidata.  The argument ip is the inode which the new
 * directory entry will refer to.  Dvp is a pointer to the directory to
 * be written, which was left locked by namei. Remaining parameters
 * (dp->i_offset, dp->i_count) indicate how the space for the new
 * entry is to be obtained.
 * 
 * 在调用namei之后，使用nameidata中的参数编写一个目录条目。参数ip是新的目录项所对应的
 * inode。dvp是将要被写入的目录项所对应的指针，它被namei锁住。其余参数（dp->i_offset，dp->i_count）
 * 指示如何获取新条目的空间
 * 
 */
int
ext2_direnter(struct inode *ip, struct vnode *dvp, struct componentname *cnp)
{
	struct inode *dp;
	struct ext2fs_direct_2 newdir;	/* 目录项 */
	int DIRBLKSIZ = ip->i_e2fs->e2fs_bsize;	/* 指定块大小 */
	int error;


#ifdef INVARIANTS
	if ((cnp->cn_flags & SAVENAME) == 0)
		panic("ext2_direnter: missing name");
#endif
	dp = VTOI(dvp);	/* directory inode */
	newdir.e2d_ino = ip->i_number;	/* 目录中新成员的 inode number */
	newdir.e2d_namlen = cnp->cn_namelen;
	/*
		tptfs 中还没有设置该属性，所以暂时处理的方式就是直接对 mode 进行转换。在目录下执行 ls 命令的时候，会提示新创建的
		文件既不是普通文件类型，又不是目录类型，可能跟这里是有关系的
	*/
	if (EXT2_HAS_INCOMPAT_FEATURE(ip->i_e2fs,
	    EXT2F_INCOMPAT_FTYPE))
		newdir.e2d_type = DTTOFT(IFTODT(ip->i_mode));	/* directory type to file type */
	else
		newdir.e2d_type = EXT2_FT_UNKNOWN;
	/* 将名字从 componentname 结构中拷贝到目录项结构 */
	bcopy(cnp->cn_nameptr, newdir.e2d_name, (unsigned)cnp->cn_namelen + 1);

	/*
		从这里我们可以看出，即使是通过 hash htree 来管理目录项，其实还是需要申请 inode，说明
		hash tree 最终并没有改变文件系统的作用机制
	*/
	if (ext2_htree_has_idx(dp)) {
		error = ext2_htree_add_entry(dvp, &newdir, cnp);
		if (error) {
			dp->i_flag &= ~IN_E3INDEX;
			dp->i_flag |= IN_CHANGE | IN_UPDATE;
		}
		return (error);
	}

	/*
		当文件系统拥有这个属性，但是 inode 不包含这个属性的时候，执行下面的代码分支。所以这里要区分文件系统
		和 inode 它们之间的属性；
		当 inode size 和 i_offset 满足一定条件的时候，才会去调用 create index 函数。所以，tptfs 没有
		执行到这一步的原因可能是这些值的初始化设置出现了问题，可以尝试着修改一下；
		调试 ext2 文件系统并开启 hash 索引，对比其中的这些成员的初始值设置
	*/
	if (EXT2_HAS_COMPAT_FEATURE(ip->i_e2fs, EXT2F_COMPAT_DIRHASHINDEX) &&
	    !ext2_htree_has_idx(dp)) {
		if ((dp->i_size / DIRBLKSIZ) == 1 &&
		    dp->i_offset == DIRBLKSIZ) {
			/*
			 * Making indexed directory when one block is not
			 * enough to save all entries.
			 * 当一个块不足以保存所有条目时生成索引目录
			 */
			return ext2_htree_create_index(dvp, cnp, &newdir);
		}
	}

	/*
	 * If dp->i_count is 0, then namei could find no
	 * space in the directory. Here, dp->i_offset will
	 * be on a directory block boundary and we will write the
	 * new entry into a fresh block.
	 */
	if (dp->i_count == 0)
		return ext2_add_first_entry(dvp, &newdir, cnp);

	error = ext2_add_entry(dvp, &newdir);
	if (!error && dp->i_endoff && dp->i_endoff < dp->i_size)
		error = ext2_truncate(dvp, (off_t)dp->i_endoff, IO_SYNC,
		    cnp->cn_cred, cnp->cn_thread);
	return (error);
}

/*
 * Insert an entry into the directory block.
 * Compact the contents.
 * 
 * 先从函数的参数来看，它添加的是一个目录项成员，所以它作用的数据块应该是存放目录项成员的数据块，
 * 而不是 node 所在的数据块。
 * 从调试的数据来看，root node info 结构中的 levels 被改成了 5，这是在 touch a.txt 的时候发生的。
 * 可以注意到 a.txt 的 namlen 刚好也是5，可以联想到很有可能是 entry 的数据更新到了 node 所在的数据
 * 块导致的，位置刚好卡在了 info 所在的地方。对比一下 info 和 dirent 的结构体中的字段构成，好像确实是
 * 上述原因导致的结果
 */
int
ext2_add_entry(struct vnode *dvp, struct ext2fs_direct_2 *entry)
{
	struct ext2fs_direct_2 *ep, *nep;
	struct inode *dp;
	struct buf *bp;
	u_int dsize;
	int error, loc, newentrysize, spacefree;
	char *dirbuf;

	dp = VTOI(dvp);

	/*
	 * If dp->i_count is non-zero, then namei found space
	 * for the new entry in the range dp->i_offset to
	 * dp->i_offset + dp->i_count in the directory.
	 * To use this space, we may have to compact the entries located
	 * there, by copying them together towards the beginning of the
	 * block, leaving the free space in one usable chunk at the end.
	 */

	/*
	 * Increase size of directory if entry eats into new space.
	 * This should never push the size past a new multiple of
	 * DIRBLKSIZE.
	 *
	 * N.B. - THIS IS AN ARTIFACT OF 4.2 AND SHOULD NEVER HAPPEN.
	 * 当数据偏移 + 剩余可用数据 > i_size 的时候，更新 i_size 数据为较大值。从这里可以推断一下几个字段的含义：
	 * i_offset 注释中说的是目录项可用空间的偏移，这就包含有两种情况。第一种就是假设目录项分配了两个数据块，但是
	 * 只使用了其中的一个半，还有半个块的空间没有使用，i_offset 可能会指向这个位置；第二种情况就是中间的一个目录项
	 * 成员数据被释放了，所以空闲出了一块空间，i_offset 也有可能是指向这个地方。i_count 可能就表示这块空间的大小
	 */
	if (dp->i_offset + dp->i_count > dp->i_size)
		dp->i_size = dp->i_offset + dp->i_count;
	/*
	 * Get the block containing the space for the new directory entry.
	 */
	if ((error = ext2_blkatoff(dvp, (off_t)dp->i_offset, &dirbuf,
	    &bp)) != 0)
		return (error);
	/*
	 * Find space for the new entry. In the simple case, the entry at
	 * offset base will have the space. If it does not, then namei
	 * arranged that compacting the region dp->i_offset to
	 * dp->i_offset + dp->i_count would yield the
	 * space.
	 * 
	 * dirbuf 指向数据 offset 的位置。从这段逻辑可以看出，即使保存目录项的数据块后面并没有真实的目录项，
	 * 也要将这块区域进行"格式化"，即把它认为是一个空的目录项，namelen 应该就是指定为0
	 */
	newentrysize = EXT2_DIR_REC_LEN(entry->e2d_namlen);
	
	/*
		dirbuf 指向的 i_offset 所在的位置，也就是可用空间的其实位置。
	*/
	ep = (struct ext2fs_direct_2 *)dirbuf;
	dsize = EXT2_DIR_REC_LEN(ep->e2d_namlen);
	spacefree = ep->e2d_reclen - dsize;
	/*
		上面可以认为是计算空目录项的实际大小，也就是数据块中未被使用的区域的大小。从下面的代码逻辑可以推测，i_count 表示的可能是
		包含可用数据的目录项成员总的大小，也就是说如果一个被释放的成员合并到上一个目录项当中，i_count 表示的就是这两个合并后总的
		大小，i_offset 就是指向这个区域的起始地址，也就是原来的上一个成员的起始地址
	*/
	for (loc = ep->e2d_reclen; loc < dp->i_count; ) {
		/* next entry pointer */
		nep = (struct ext2fs_direct_2 *)(dirbuf + loc);
		if (ep->e2d_ino) {
			/* trim the existing slot */
			ep->e2d_reclen = dsize;
			ep = (struct ext2fs_direct_2 *)((char *)ep + dsize);
		} else {
			/* overwrite; nothing there; header is ours */
			spacefree += dsize;
		}
		dsize = EXT2_DIR_REC_LEN(nep->e2d_namlen);
		spacefree += nep->e2d_reclen - dsize;
		loc += nep->e2d_reclen;
		bcopy((caddr_t)nep, (caddr_t)ep, dsize);
	}
	/*
	 * Update the pointer fields in the previous entry (if any),
	 * copy in the new entry, and write out the block.
	 */
	if (ep->e2d_ino == 0) {
		if (spacefree + dsize < newentrysize)
			panic("ext2_direnter: compact1");
		entry->e2d_reclen = spacefree + dsize;
	} else {
		if (spacefree < newentrysize)
			panic("ext2_direnter: compact2");
		entry->e2d_reclen = spacefree;
		ep->e2d_reclen = dsize;
		ep = (struct ext2fs_direct_2 *)((char *)ep + dsize);
	}
	bcopy((caddr_t)entry, (caddr_t)ep, (u_int)newentrysize);
	ext2_dirent_csum_set(dp, (struct ext2fs_direct_2 *)bp->b_data);
	if (DOINGASYNC(dvp)) {
		bdwrite(bp);
		error = 0;
	} else {
		error = bwrite(bp);
	}
	dp->i_flag |= IN_CHANGE | IN_UPDATE;
	return (error);
}

/*
 * Remove a directory entry after a call to namei, using
 * the parameters which it left in nameidata. The entry
 * dp->i_offset contains the offset into the directory of the
 * entry to be eliminated.  The dp->i_count field contains the
 * size of the previous record in the directory.  If this
 * is 0, the first entry is being deleted, so we need only
 * zero the inode number to mark the entry as free.  If the
 * entry is not the first in the directory, we must reclaim
 * the space of the now empty record by adding the record size
 * to the size of the previous entry.
 */
int
ext2_dirremove(struct vnode *dvp, struct componentname *cnp)
{
	struct inode *dp;
	struct ext2fs_direct_2 *ep, *rep;
	struct buf *bp;
	int error;

	dp = VTOI(dvp);
	/* 判断目录中的 free slot 数量是否为0，这就从另外一个角度说明 dp 表示的目录项 */
	if (dp->i_count == 0) {
		/*
		 * First entry in block: set d_ino to zero.
		 * 通过 ext2_blkatoff 函数将获取到的目录项结构体存放到 ep 当中，读取的整个
		 * 块的数据存放到 bp 当中
		 */
		if ((error =
		    ext2_blkatoff(dvp, (off_t)dp->i_offset, (char **)&ep,
		    &bp)) != 0)
			return (error);
		ep->e2d_ino = 0;	/* 将该目录项对应的 inode 号设置为0 */
		/* 计算checksum，验证读取的数据是否正确 */
		ext2_dirent_csum_set(dp, (struct ext2fs_direct_2 *)bp->b_data);
		error = bwrite(bp);
		dp->i_flag |= IN_CHANGE | IN_UPDATE;
		return (error);
	}
	/*
	 * Collapse new free space into previous entry.将新的可用空间折叠到上一个条目中
	 * 这里可以参考 Linux 中关于目录项的处理方式
	 */
	if ((error = ext2_blkatoff(dvp, (off_t)(dp->i_offset - dp->i_count),
	    (char **)&ep, &bp)) != 0)
		return (error);

	/* Set 'rep' to the entry being removed. */
	if (dp->i_count == 0)
		rep = ep;
	else
		rep = (struct ext2fs_direct_2 *)((char *)ep + ep->e2d_reclen);
	ep->e2d_reclen += rep->e2d_reclen;
	ext2_dirent_csum_set(dp, (struct ext2fs_direct_2 *)bp->b_data);
	if (DOINGASYNC(dvp) && dp->i_count != 0)
		bdwrite(bp);
	else
		error = bwrite(bp);
	dp->i_flag |= IN_CHANGE | IN_UPDATE;
	return (error);
}

/*
 * Rewrite an existing directory entry to point at the inode
 * supplied.  The parameters describing the directory entry are
 * set up by a call to namei.
 */
int
ext2_dirrewrite(struct inode *dp, struct inode *ip, struct componentname *cnp)
{
	struct buf *bp;
	struct ext2fs_direct_2 *ep;
	struct vnode *vdp = ITOV(dp);
	int error;

	if ((error = ext2_blkatoff(vdp, (off_t)dp->i_offset, (char **)&ep,
	    &bp)) != 0)
		return (error);
	ep->e2d_ino = ip->i_number;
	if (EXT2_HAS_INCOMPAT_FEATURE(ip->i_e2fs,
	    EXT2F_INCOMPAT_FTYPE))
		ep->e2d_type = DTTOFT(IFTODT(ip->i_mode));
	else
		ep->e2d_type = EXT2_FT_UNKNOWN;
	ext2_dirent_csum_set(dp, (struct ext2fs_direct_2 *)bp->b_data);
	error = bwrite(bp);
	dp->i_flag |= IN_CHANGE | IN_UPDATE;
	return (error);
}

/*
 * Check if a directory is empty or not.
 * Inode supplied must be locked.
 *
 * Using a struct dirtemplate here is not precisely
 * what we want, but better than using a struct direct.
 *
 * NB: does not handle corrupted directories.
 */
int
ext2_dirempty(struct inode *ip, ino_t parentino, struct ucred *cred)
{
	off_t off;
	struct dirtemplate dbuf;
	struct ext2fs_direct_2 *dp = (struct ext2fs_direct_2 *)&dbuf;
	int error, namlen;
	ssize_t count;
#define	MINDIRSIZ (sizeof(struct dirtemplate) / 2)

	for (off = 0; off < ip->i_size; off += dp->e2d_reclen) {
		error = vn_rdwr(UIO_READ, ITOV(ip), (caddr_t)dp, MINDIRSIZ,
		    off, UIO_SYSSPACE, IO_NODELOCKED | IO_NOMACCHECK, cred,
		    NOCRED, &count, (struct thread *)0);
		/*
		 * Since we read MINDIRSIZ, residual must
		 * be 0 unless we're at end of file.
		 */
		if (error || count != 0)
			return (0);
		/* avoid infinite loops */
		if (dp->e2d_reclen == 0)
			return (0);
		/* skip empty entries */
		if (dp->e2d_ino == 0)
			continue;
		/* accept only "." and ".." */
		namlen = dp->e2d_namlen;
		if (namlen > 2)
			return (0);
		if (dp->e2d_name[0] != '.')
			return (0);
		/*
		 * At this point namlen must be 1 or 2.
		 * 1 implies ".", 2 implies ".." if second
		 * char is also "."
		 */
		if (namlen == 1)
			continue;
		if (dp->e2d_name[1] == '.' && dp->e2d_ino == parentino)
			continue;
		return (0);
	}
	return (1);
}

/*
 * Check if source directory is in the path of the target directory.
 * Target is supplied locked, source is unlocked.
 * The target is always vput before returning.
 */
int
ext2_checkpath(struct inode *source, struct inode *target, struct ucred *cred)
{
	struct vnode *vp;
	int error, namlen;
	struct dirtemplate dirbuf;

	// vp 表示的是 target directory
	vp = ITOV(target);
	if (target->i_number == source->i_number) {
		error = EEXIST;
		goto out;
	}
	if (target->i_number == EXT2_ROOTINO) {
		error = 0;
		goto out;
	}

	for (;;) {
		if (vp->v_type != VDIR) {
			error = ENOTDIR;
			break;
		}
		error = vn_rdwr(UIO_READ, vp, (caddr_t)&dirbuf,
		    sizeof(struct dirtemplate), (off_t)0, UIO_SYSSPACE,
		    IO_NODELOCKED | IO_NOMACCHECK, cred, NOCRED, NULL,
		    NULL);
		if (error != 0)
			break;
		namlen = dirbuf.dotdot_type;	/* like ufs little-endian */
		if (namlen != 2 ||
		    dirbuf.dotdot_name[0] != '.' ||
		    dirbuf.dotdot_name[1] != '.') {
			error = ENOTDIR;
			break;
		}
		if (dirbuf.dotdot_ino == source->i_number) {
			error = EINVAL;
			break;
		}
		if (dirbuf.dotdot_ino == EXT2_ROOTINO)
			break;
		vput(vp);
		if ((error = VFS_VGET(vp->v_mount, dirbuf.dotdot_ino,
		    LK_EXCLUSIVE, &vp)) != 0) {
			vp = NULL;
			break;
		}
	}

out:
	if (error == ENOTDIR)
		printf("checkpath: .. not a directory\n");
	if (vp != NULL)
		vput(vp);
	return (error);
}
