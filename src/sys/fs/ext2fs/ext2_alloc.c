/*-
 *  modified for Lites 1.1
 *
 *  Aug 1995, Godmar Back (gback@cs.utah.edu)
 *  University of Utah, Department of Computer Science
 */
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)ffs_alloc.c	8.8 (Berkeley) 2/21/94
 * $FreeBSD: releng/12.0/sys/fs/ext2fs/ext2_alloc.c 338151 2018-08-21 18:39:29Z fsu $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/vnode.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/buf.h>
#include <sys/endian.h>

#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/inode.h>
#include <fs/ext2fs/ext2_mount.h>
#include <fs/ext2fs/ext2fs.h>
#include <fs/ext2fs/ext2_extern.h>

static daddr_t	ext2_alloccg(struct inode *, int, daddr_t, int);
static daddr_t	ext2_clusteralloc(struct inode *, int, daddr_t, int);
static u_long	ext2_dirpref(struct inode *);
static e4fs_daddr_t ext2_hashalloc(struct inode *, int, long, int,
    daddr_t (*)(struct inode *, int, daddr_t, 
						int));
static daddr_t	ext2_nodealloccg(struct inode *, int, daddr_t, int);
static daddr_t  ext2_mapsearch(struct m_ext2fs *, char *, daddr_t);

/*
 * Allocate a block in the filesystem.
 *
 * A preference may be optionally specified. If a preference is given
 * the following hierarchy is used to allocate a block:
 *   1) allocate the requested block.
 *   2) allocate a rotationally optimal block in the same cylinder.
 *   3) allocate a block in the same cylinder group.
 *   4) quadradically rehash into other cylinder groups, until an
 *        available block is located.
 * If no block preference is given the following hierarchy is used
 * to allocate a block:
 *   1) allocate a block in the cylinder group that contains the
 *        inode for the file.
 *   2) quadradically rehash into other cylinder groups, until an
 *        available block is located.
 * 文件系统数据块的分配策略.
 * 
 * 一个优先选择(块组？策略？)可能会被指定。如果给出了指定，那就按照下面的等级划分去查找一个数据块:
 * 	1、分配一个建议的块(inode 结构体中指定的块号？)
 * 	2、同一个块组中的最佳快
 * 	3、同一个块组中的块
 * 	4、跳转到其他的块组查找，直到查找到一个块是可用的
 * 
 * 如果优先选择没有给出，那按照下面的登记划分去查找一个数据块:
 * 	1、在包含文件inode的块组中找到一个块
 * 	2、跳转到其他块组找出一个可用的块
 * 
 * ext2_balloc 函数中会调用该函数，其中 bpref 参数表示的就是优先分配的块号，或者是距离
 * 某个磁盘块距离比较近的磁盘块号
 */
int
ext2_alloc(struct inode *ip, daddr_t lbn, e4fs_daddr_t bpref, int size,
    struct ucred *cred, e4fs_daddr_t *bnp)
{
	struct m_ext2fs *fs;
	struct ext2mount *ump;
	e4fs_daddr_t bno;
	int cg;

	*bnp = 0;
	fs = ip->i_e2fs;
	ump = ip->i_ump;
	mtx_assert(EXT2_MTX(ump), MA_OWNED);
#ifdef INVARIANTS
	if ((u_int)size > fs->e2fs_bsize || blkoff(fs, size) != 0) {
		vn_printf(ip->i_devvp, "bsize = %lu, size = %d, fs = %s\n",
		    (long unsigned int)fs->e2fs_bsize, size, fs->e2fs_fsmnt);
		panic("ext2_alloc: bad size");
	}
	if (cred == NOCRED)
		panic("ext2_alloc: missing credential");
#endif		/* INVARIANTS */
	/* 判断文件系统是否还有空闲块 */
	if (size == fs->e2fs_bsize && fs->e2fs_fbcount == 0)
		goto nospace;
	/* 
		ext2 文件系统设计中会包含有一些保留块，如果可用块的数量小于保留块，也就说明
		文件系统已经没有足够的空间再去容纳其他数据
	*/
	if (cred->cr_uid != 0 &&
	    fs->e2fs_fbcount < fs->e2fs_rbcount)
		goto nospace;
	if (bpref >= fs->e2fs_bcount)
		bpref = 0;
	/* 当优先选择的块号为0时，那就执行第二种策略，即在inode所在的块组中查找可用块 
		cg 表示其快组号。当块组号为 0 时，我们就需要找到 inode 所在的块组，然后从
		该块组中去找合适的数据块。如果不为0的话，就从 bpref 所在的块组查找空闲数据块
	*/
	if (bpref == 0)
		cg = ino_to_cg(fs, ip->i_number);
	else
		cg = dtog(fs, bpref);
	bno = (daddr_t)ext2_hashalloc(ip, cg, bpref, fs->e2fs_bsize,
	    ext2_alloccg);
	if (bno > 0) {
		/* set next_alloc fields as done in block_getblk */
		ip->i_next_alloc_block = lbn;
		ip->i_next_alloc_goal = bno;

		ip->i_blocks += btodb(fs->e2fs_bsize);
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		*bnp = bno;
		return (0);
	}
nospace:
	EXT2_UNLOCK(ump);
	ext2_fserr(fs, cred->cr_uid, "filesystem full");
	uprintf("\n%s: write failed, filesystem is full\n", fs->e2fs_fsmnt);
	return (ENOSPC);
}

/*
 * Allocate EA's block for inode.
 */
e4fs_daddr_t
ext2_alloc_meta(struct inode *ip)
{
	struct m_ext2fs *fs;
	daddr_t blk;

	fs = ip->i_e2fs;

	EXT2_LOCK(ip->i_ump);
	blk = ext2_hashalloc(ip, ino_to_cg(fs, ip->i_number), 0, fs->e2fs_bsize,
	    ext2_alloccg);
	if (0 == blk)
		EXT2_UNLOCK(ip->i_ump);

	return (blk);
}

/*
 * Reallocate a sequence of blocks into a contiguous sequence of blocks.
 *
 * The vnode and an array of buffer pointers for a range of sequential
 * logical blocks to be made contiguous is given. The allocator attempts
 * to find a range of sequential blocks starting as close as possible to
 * an fs_rotdelay offset from the end of the allocation for the logical
 * block immediately preceding the current range. If successful, the
 * physical block numbers in the buffer pointers and in the inode are
 * changed to reflect the new allocation. If unsuccessful, the allocation
 * is left unchanged. The success in doing the reallocation is returned.
 * Note that the error return is not reflected back to the user. Rather
 * the previous block allocation will be used.
 */

static SYSCTL_NODE(_vfs, OID_AUTO, ext2fs, CTLFLAG_RW, 0, "EXT2FS filesystem");

static int doasyncfree = 1;

SYSCTL_INT(_vfs_ext2fs, OID_AUTO, doasyncfree, CTLFLAG_RW, &doasyncfree, 0,
    "Use asychronous writes to update block pointers when freeing blocks");

static int doreallocblks = 0;

SYSCTL_INT(_vfs_ext2fs, OID_AUTO, doreallocblks, CTLFLAG_RW, &doreallocblks, 0, "");

int
ext2_reallocblks(struct vop_reallocblks_args *ap)
{
	struct m_ext2fs *fs;
	struct inode *ip;
	struct vnode *vp;
	struct buf *sbp, *ebp;
	uint32_t *bap, *sbap, *ebap;
	struct ext2mount *ump;
	struct cluster_save *buflist;
	struct indir start_ap[EXT2_NIADDR + 1], end_ap[EXT2_NIADDR + 1], *idp;
	e2fs_lbn_t start_lbn, end_lbn;
	int soff;
	e2fs_daddr_t newblk, blkno;
	int i, len, start_lvl, end_lvl, pref, ssize;

	if (doreallocblks == 0)
		return (ENOSPC);

	vp = ap->a_vp;
	ip = VTOI(vp);
	fs = ip->i_e2fs;
	ump = ip->i_ump;

	if (fs->e2fs_contigsumsize <= 0 || ip->i_flag & IN_E4EXTENTS)
		return (ENOSPC);

	buflist = ap->a_buflist;
	len = buflist->bs_nchildren;
	start_lbn = buflist->bs_children[0]->b_lblkno;
	end_lbn = start_lbn + len - 1;
#ifdef INVARIANTS
	for (i = 1; i < len; i++)
		if (buflist->bs_children[i]->b_lblkno != start_lbn + i)
			panic("ext2_reallocblks: non-cluster");
#endif
	/*
	 * If the cluster crosses the boundary for the first indirect
	 * block, leave space for the indirect block. Indirect blocks
	 * are initially laid out in a position after the last direct
	 * block. Block reallocation would usually destroy locality by
	 * moving the indirect block out of the way to make room for
	 * data blocks if we didn't compensate here. We should also do
	 * this for other indirect block boundaries, but it is only
	 * important for the first one.
	 */
	if (start_lbn < EXT2_NDADDR && end_lbn >= EXT2_NDADDR)
		return (ENOSPC);
	/*
	 * If the latest allocation is in a new cylinder group, assume that
	 * the filesystem has decided to move and do not force it back to
	 * the previous cylinder group.
	 */
	if (dtog(fs, dbtofsb(fs, buflist->bs_children[0]->b_blkno)) !=
	    dtog(fs, dbtofsb(fs, buflist->bs_children[len - 1]->b_blkno)))
		return (ENOSPC);
	if (ext2_getlbns(vp, start_lbn, start_ap, &start_lvl) ||
	    ext2_getlbns(vp, end_lbn, end_ap, &end_lvl))
		return (ENOSPC);
	/*
	 * Get the starting offset and block map for the first block.
	 */
	if (start_lvl == 0) {
		sbap = &ip->i_db[0];
		soff = start_lbn;
	} else {
		idp = &start_ap[start_lvl - 1];
		if (bread(vp, idp->in_lbn, (int)fs->e2fs_bsize, NOCRED, &sbp)) {
			brelse(sbp);
			return (ENOSPC);
		}
		sbap = (u_int *)sbp->b_data;
		soff = idp->in_off;
	}
	/*
	 * If the block range spans two block maps, get the second map.
	 */
	ebap = NULL;
	if (end_lvl == 0 || (idp = &end_ap[end_lvl - 1])->in_off + 1 >= len) {
		ssize = len;
	} else {
#ifdef INVARIANTS
		if (start_ap[start_lvl - 1].in_lbn == idp->in_lbn)
			panic("ext2_reallocblks: start == end");
#endif
		ssize = len - (idp->in_off + 1);
		if (bread(vp, idp->in_lbn, (int)fs->e2fs_bsize, NOCRED, &ebp))
			goto fail;
		ebap = (u_int *)ebp->b_data;
	}
	/*
	 * Find the preferred location for the cluster.
	 */
	EXT2_LOCK(ump);
	pref = ext2_blkpref(ip, start_lbn, soff, sbap, 0);
	/*
	 * Search the block map looking for an allocation of the desired size.
	 */
	if ((newblk = (e2fs_daddr_t)ext2_hashalloc(ip, dtog(fs, pref), pref,
	    len, ext2_clusteralloc)) == 0) {
		EXT2_UNLOCK(ump);
		goto fail;
	}
	/*
	 * We have found a new contiguous block.
	 *
	 * First we have to replace the old block pointers with the new
	 * block pointers in the inode and indirect blocks associated
	 * with the file.
	 */
#ifdef DEBUG
	printf("realloc: ino %ju, lbns %jd-%jd\n\told:",
	    (uintmax_t)ip->i_number, (intmax_t)start_lbn, (intmax_t)end_lbn);
#endif	/* DEBUG */
	blkno = newblk;
	for (bap = &sbap[soff], i = 0; i < len; i++, blkno += fs->e2fs_fpb) {
		if (i == ssize) {
			bap = ebap;
			soff = -i;
		}
#ifdef INVARIANTS
		if (buflist->bs_children[i]->b_blkno != fsbtodb(fs, *bap))
			panic("ext2_reallocblks: alloc mismatch");
#endif
#ifdef DEBUG
		printf(" %d,", *bap);
#endif	/* DEBUG */
		*bap++ = blkno;
	}
	/*
	 * Next we must write out the modified inode and indirect blocks.
	 * For strict correctness, the writes should be synchronous since
	 * the old block values may have been written to disk. In practise
	 * they are almost never written, but if we are concerned about
	 * strict correctness, the `doasyncfree' flag should be set to zero.
	 *
	 * The test on `doasyncfree' should be changed to test a flag
	 * that shows whether the associated buffers and inodes have
	 * been written. The flag should be set when the cluster is
	 * started and cleared whenever the buffer or inode is flushed.
	 * We can then check below to see if it is set, and do the
	 * synchronous write only when it has been cleared.
	 */
	if (sbap != &ip->i_db[0]) {
		if (doasyncfree)
			bdwrite(sbp);
		else
			bwrite(sbp);
	} else {
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		if (!doasyncfree)
			ext2_update(vp, 1);
	}
	if (ssize < len) {
		if (doasyncfree)
			bdwrite(ebp);
		else
			bwrite(ebp);
	}
	/*
	 * Last, free the old blocks and assign the new blocks to the buffers.
	 */
#ifdef DEBUG
	printf("\n\tnew:");
#endif	/* DEBUG */
	for (blkno = newblk, i = 0; i < len; i++, blkno += fs->e2fs_fpb) {
		ext2_blkfree(ip, dbtofsb(fs, buflist->bs_children[i]->b_blkno),
		    fs->e2fs_bsize);
		buflist->bs_children[i]->b_blkno = fsbtodb(fs, blkno);
#ifdef DEBUG
		printf(" %d,", blkno);
#endif	/* DEBUG */
	}
#ifdef DEBUG
	printf("\n");
#endif	/* DEBUG */
	return (0);

fail:
	if (ssize < len)
		brelse(ebp);
	if (sbap != &ip->i_db[0])
		brelse(sbp);
	return (ENOSPC);
}

/*
 * Allocate an inode in the filesystem.
 * 在文件系统中申请一个 inode 结构
 * 通过实际应用场景可知，pvp 表示的很可能是当前目录所对应的 vnode，vpp 表示的可能是我们
 * 所要创建的文件或者文件夹所对应的 vnode
 */
int
ext2_valloc(struct vnode *pvp, int mode, struct ucred *cred, struct vnode **vpp)
{
	struct timespec ts;
	struct inode *pip;
	struct m_ext2fs *fs;	/* in-memory superblock */
	struct inode *ip;
	struct ext2mount *ump;	/* ext2_mount */
	ino_t ino, ipref;
	int error, cg;

	/* 初始时的一些赋值操作 */
	*vpp = NULL;
	pip = VTOI(pvp);	/* 当前目录所对应的 inode 信息 */
	fs = pip->i_e2fs;	/* 获取超级块信息 */
	ump = pip->i_ump;	/* 获取挂载点信息 */

	EXT2_LOCK(ump);
	/* 首先判断空闲 inode 的数量是否为0 */
	if (fs->e2fs->e2fs_ficount == 0)
		goto noinodes;
	/*
	 * If it is a directory then obtain a cylinder group based on
	 * ext2_dirpref else obtain it using ino_to_cg. The preferred inode is
	 * always the next inode.
	 * 
	 * 如果这是一个目录，那么通过 ext2_dirpref 获得一个柱面组，否则通过 ino_to_cg 来获取。
	 * 首选 inode 总是下一个 inode。从下面的代码逻辑可以看出，文件系统对目录的分配有一套策略，
	 * 给人的感觉是目录要平均分配到不同的块组当中，而不是说先集中使用同一个块组进行存储
	 */
	if ((mode & IFMT) == IFDIR) {
		/* 
			通过一套策略找到最适合存放该目录的一个块组，但是在 tptfs 中只有一个块组，所以这个操作其实
			是不需要的。对于
		*/
		cg = ext2_dirpref(pip);	
		if (fs->e2fs_contigdirs[cg] < 255)
			fs->e2fs_contigdirs[cg]++;	/* 该块组中包含的目录数量++ */
	} else {
		/* 如果是对普通文件的操作，就直接根据 inode number 来计算块组号 */
		cg = ino_to_cg(fs, pip->i_number);
		/*
			如果处理的不是目录项，就要执行--操作？
		*/
		if (fs->e2fs_contigdirs[cg] > 0)
			fs->e2fs_contigdirs[cg]--;
	}
	/*
		块组编号 * 每个块组中的inode数量 + 1 = 该块组中 inode 的起始编号，并将它作为优先获取的
		inode
	*/
	ipref = cg * fs->e2fs->e2fs_ipg + 1;
	ino = (ino_t)ext2_hashalloc(pip, cg, (long)ipref, mode, ext2_nodealloccg);

	if (ino == 0)
		goto noinodes;
	error = VFS_VGET(pvp->v_mount, ino, LK_EXCLUSIVE, vpp);
	if (error) {
		ext2_vfree(pvp, ino, mode);
		return (error);
	}
	ip = VTOI(*vpp);	/* 获取的 inode 对应到了 vpp 当中 */

	/*
	 * The question is whether using VGET was such good idea at all:
	 * Linux doesn't read the old inode in when it is allocating a
	 * new one. I will set at least i_size and i_blocks to zero.
	 */
	ip->i_flag = 0;
	ip->i_size = 0;
	ip->i_blocks = 0;
	ip->i_mode = 0;
	ip->i_flags = 0;
	if (EXT2_HAS_INCOMPAT_FEATURE(fs, EXT2F_INCOMPAT_EXTENTS)
	    && (S_ISREG(mode) || S_ISDIR(mode)))
		ext4_ext_tree_init(ip);
	else
		memset(ip->i_data, 0, sizeof(ip->i_data));
	

	/*
	 * Set up a new generation number for this inode.
	 * Avoid zero values.
	 */
	do {
		ip->i_gen = arc4random();
	} while (ip->i_gen == 0);

	vfs_timestamp(&ts);
	ip->i_birthtime = ts.tv_sec;
	ip->i_birthnsec = ts.tv_nsec;

/*
printf("ext2_valloc: allocated inode %d\n", ino);
*/
	return (0);
noinodes:
	EXT2_UNLOCK(ump);
	ext2_fserr(fs, cred->cr_uid, "out of inodes");
	uprintf("\n%s: create/symlink failed, no inodes free\n", fs->e2fs_fsmnt);
	return (ENOSPC);
}

/*
 * 64-bit compatible getters and setters for struct ext2_gd from ext2fs.h
 */
static uint64_t
e2fs_gd_get_b_bitmap(struct ext2_gd *gd)
{
	/* 
		块组描述符结构体中直接就包含有字段，表示的应该是该块组的起始数据块号。tptfs 中这些数据
		其实是包含在超级块当中，因为只有一个块组
	*/
	return (((uint64_t)(gd->ext4bgd_b_bitmap_hi) << 32) |
	    gd->ext2bgd_b_bitmap);
}

/*
	同上，计算 inode bitmap 的起始块号
*/
static uint64_t
e2fs_gd_get_i_bitmap(struct ext2_gd *gd)
{

	return (((uint64_t)(gd->ext4bgd_i_bitmap_hi) << 32) |
	    gd->ext2bgd_i_bitmap);
}

/*
	貌似是获取这个块组中索引节点表的第一个块的块号
*/
uint64_t
e2fs_gd_get_i_tables(struct ext2_gd *gd)
{

	return (((uint64_t)(gd->ext4bgd_i_tables_hi) << 32) |
	    gd->ext2bgd_i_tables);
}

static uint32_t
e2fs_gd_get_nbfree(struct ext2_gd *gd)
{

	return (((uint32_t)(gd->ext4bgd_nbfree_hi) << 16) |
	    gd->ext2bgd_nbfree);
}

static void
e2fs_gd_set_nbfree(struct ext2_gd *gd, uint32_t val)
{

	gd->ext2bgd_nbfree = val & 0xffff;
	gd->ext4bgd_nbfree_hi = val >> 16;
}

static uint32_t
e2fs_gd_get_nifree(struct ext2_gd *gd)
{

	return (((uint32_t)(gd->ext4bgd_nifree_hi) << 16) |
	    gd->ext2bgd_nifree);
}

static void
e2fs_gd_set_nifree(struct ext2_gd *gd, uint32_t val)
{

	gd->ext2bgd_nifree = val & 0xffff;
	gd->ext4bgd_nifree_hi = val >> 16;
}

uint32_t
e2fs_gd_get_ndirs(struct ext2_gd *gd)
{

	return (((uint32_t)(gd->ext4bgd_ndirs_hi) << 16) |
	    gd->ext2bgd_ndirs);
}

static void
e2fs_gd_set_ndirs(struct ext2_gd *gd, uint32_t val)
{

	gd->ext2bgd_ndirs = val & 0xffff;
	gd->ext4bgd_ndirs_hi = val >> 16;
}

/* 计算块组中还未被使用的 inode 的个数，tptfs 应该也是需要添加这样的一个字段 */
static uint32_t
e2fs_gd_get_i_unused(struct ext2_gd *gd)
{
	return (((uint32_t)(gd->ext4bgd_i_unused_hi) << 16) |
	    gd->ext4bgd_i_unused);
}

static void
e2fs_gd_set_i_unused(struct ext2_gd *gd, uint32_t val)
{

	gd->ext4bgd_i_unused = val & 0xffff;
	gd->ext4bgd_i_unused_hi = val >> 16;
}

/*
 * Find a cylinder to place a directory. 找到一个柱面来放置一个目录
 *
 * The policy implemented by this algorithm is to allocate a
 * directory inode in the same cylinder group as its parent
 * directory, but also to reserve space for its files inodes
 * and data. Restrict the number of directories which may be
 * allocated one after another in the same cylinder group
 * without intervening allocation of files.
 * 该算法实现的策略是在与父目录相同的柱面组中分配一个目录inode，同时为其
 * 文件 inode 和数据预留空间。限制可以在同一个柱面组中一个接一个地分配的目录
 * 的数量，而不干预文件的分配
 *
 * If we allocate a first level directory then force allocation
 * in another cylinder group.
 * ext2_dirpref： ext2 directory preference(偏爱)?
 */
static u_long
ext2_dirpref(struct inode *pip)
{
	struct m_ext2fs *fs;	/* in-memory superblock */
	int cg, prefcg, cgsize;
	uint64_t avgbfree, minbfree;
	u_int avgifree, avgndir, curdirsize;
	u_int minifree, maxndir;
	u_int mincg, minndir;
	u_int dirsize, maxcontigdirs;

	mtx_assert(EXT2_MTX(pip->i_ump), MA_OWNED);
	fs = pip->i_e2fs;
	/*
		空闲索引节点数 / 块组数 = 平均每个块组拥有的空闲索引节点数
		空闲块数 / 块组数 = 平均每个块组拥有的空闲块数
		总的目录数 / 块组数 = 平均每个块组拥有的目录数
	*/
	avgifree = fs->e2fs->e2fs_ficount / fs->e2fs_gcount;
	avgbfree = fs->e2fs_fbcount / fs->e2fs_gcount;
	avgndir = fs->e2fs_total_dir / fs->e2fs_gcount;

	/*
	 * Force allocation in another cg if creating a first level dir.
	 * 如果创建一个一级目录，那就强制在另外一个块组中进行申请
	 */
	ASSERT_VOP_LOCKED(ITOV(pip), "ext2fs_dirpref");
	if (ITOV(pip)->v_vflag & VV_ROOT) {	/* 如果 inode 对应的 vnode 关联的是根目录 */
		prefcg = arc4random() % fs->e2fs_gcount;	/* 随机产生一个数字 / 块组数，随机选取根目录放哪个块组？ */
		mincg = prefcg;	/* 可以认为是一个组号 */
		minndir = fs->e2fs_ipg;	/* 每个组拥有的 inode 数量 */
		/* 通过块组号遍历所有的块组 */
		for (cg = prefcg; cg < fs->e2fs_gcount; cg++)
			/*
					该块组中的目录数量 < 块组拥有的 inode 数量(初始情况)，动态变化；
					空闲 inode 的数量 >= 平均每个块组拥有的空闲 inode 数量；
					空闲块数 >= 平均每个组拥有的空闲块数；
					满足上述三个条件，更新 mincg 和 minndir；其用意就是从所有的组中选出一个最合适的组用于存放目录项？
			*/
			if (e2fs_gd_get_ndirs(&fs->e2fs_gd[cg]) < minndir &&
			    e2fs_gd_get_nifree(&fs->e2fs_gd[cg]) >= avgifree &&
			    e2fs_gd_get_nbfree(&fs->e2fs_gd[cg]) >= avgbfree) {
				mincg = cg;
				minndir = e2fs_gd_get_ndirs(&fs->e2fs_gd[cg]);
			}
		/*
			上述循环首先查找的是后一部分的块组，下面的分支在遍历查找前一部分块组
		*/
		for (cg = 0; cg < prefcg; cg++)
			if (e2fs_gd_get_ndirs(&fs->e2fs_gd[cg]) < minndir &&
			    e2fs_gd_get_nifree(&fs->e2fs_gd[cg]) >= avgifree &&
			    e2fs_gd_get_nbfree(&fs->e2fs_gd[cg]) >= avgbfree) {
				mincg = cg;
				minndir = e2fs_gd_get_ndirs(&fs->e2fs_gd[cg]);
			}
		return (mincg);
	}	// end if VV_ROOT，下面处理不是根目录的情况
	/*
	 * Count various limits which used for
	 * optimal allocation of a directory inode.
	 * 计算用于优化目录inode分配的各种限制
	 */
	maxndir = min(avgndir + fs->e2fs_ipg / 16, fs->e2fs_ipg);
	minifree = avgifree - avgifree / 4;
	if (minifree < 1)
		minifree = 1;
	minbfree = avgbfree - avgbfree / 4;
	if (minbfree < 1)
		minbfree = 1;
	cgsize = fs->e2fs_fsize * fs->e2fs_fpg;
	dirsize = AVGDIRSIZE;
	curdirsize = avgndir ? (cgsize - avgbfree * fs->e2fs_bsize) / avgndir : 0;
	if (dirsize < curdirsize)
		dirsize = curdirsize;
	maxcontigdirs = min((avgbfree * fs->e2fs_bsize) / dirsize, 255);
	maxcontigdirs = min(maxcontigdirs, fs->e2fs_ipg / AFPDIR);
	if (maxcontigdirs == 0)
		maxcontigdirs = 1;

	/*
	 * Limit number of dirs in one cg and reserve space for
	 * regular files, but only if we have no deficit in
	 * inodes or space.
	 */
	prefcg = ino_to_cg(fs, pip->i_number);
	for (cg = prefcg; cg < fs->e2fs_gcount; cg++)
		if (e2fs_gd_get_ndirs(&fs->e2fs_gd[cg]) < maxndir &&
		    e2fs_gd_get_nifree(&fs->e2fs_gd[cg]) >= minifree &&
		    e2fs_gd_get_nbfree(&fs->e2fs_gd[cg]) >= minbfree) {
			if (fs->e2fs_contigdirs[cg] < maxcontigdirs)
				return (cg);
		}
	for (cg = 0; cg < prefcg; cg++)
		if (e2fs_gd_get_ndirs(&fs->e2fs_gd[cg]) < maxndir &&
		    e2fs_gd_get_nifree(&fs->e2fs_gd[cg]) >= minifree &&
		    e2fs_gd_get_nbfree(&fs->e2fs_gd[cg]) >= minbfree) {
			if (fs->e2fs_contigdirs[cg] < maxcontigdirs)
				return (cg);
		}
	/*
	 * This is a backstop when we have deficit in space.
	 */
	for (cg = prefcg; cg < fs->e2fs_gcount; cg++)
		if (e2fs_gd_get_nifree(&fs->e2fs_gd[cg]) >= avgifree)
			return (cg);
	for (cg = 0; cg < prefcg; cg++)
		if (e2fs_gd_get_nifree(&fs->e2fs_gd[cg]) >= avgifree)
			break;
	return (cg);
}

/*
 * Select the desired position for the next block in a file.
 * 为文件中的下一个块选择所需的位置
 *
 * we try to mimic(模仿) what Remy does in inode_getblk/block_getblk
 *
 * we note: blocknr == 0 means that we're about to allocate either
 * a direct block or a pointer block at the first level of indirection
 * (In other words, stuff that will go in i_db[] or i_ib[])
 *
 * blocknr != 0 means that we're allocating a block that is none
 * of the above. Then, blocknr tells us the number of the block
 * that will hold the pointer
 */
e4fs_daddr_t
ext2_blkpref(struct inode *ip, e2fs_lbn_t lbn, int indx, e2fs_daddr_t *bap,
    e2fs_daddr_t blocknr)
{
	struct m_ext2fs *fs;
	int tmp;

	fs = ip->i_e2fs;
	/* 
		ext2mount 结构体中包含一个 mutex 成员，tpt_mount 感觉也需要添加类似的字段 
	*/
	mtx_assert(EXT2_MTX(ip->i_ump), MA_OWNED);

	/*
	 * If the next block is actually what we thought it is, then set the
	 * goal to what we thought it should be.
	 */
	if (ip->i_next_alloc_block == lbn && ip->i_next_alloc_goal != 0)
		return ip->i_next_alloc_goal;

	/*
	 * Now check whether we were provided with an array that basically
	 * tells us previous blocks to which we want to stay close.
	 * 这里想要表达的意思应该是: 前面如果已经得到了可以优先分配的数据块号，
	 * 那就直接返回。如果前面没找到，那就找一个距离最近的一个非空块，我们次优先
	 * 分配这个块附近的数据块。如果还是没有，那就执行再下面的代码分支
	 */
	if (bap)
		for (tmp = indx - 1; tmp >= 0; tmp--)
			if (bap[tmp])	/* 逻辑块号对应的数组元素不为空 */
				return bap[tmp];	/* 返回的其实是磁盘块号 */

	/*
	 * Else lets fall back to the blocknr or, if there is none, follow
	 * the rule that a block should be allocated near its inode.
	 * 否则，让我们回到blocknr，或者，如果没有，则遵循块应该在其inode附近分配的规则。
	 * 从代码逻辑来看，分配的应该是块组的第一个块
	 */
	return (blocknr ? blocknr :
	    (e2fs_daddr_t)(ip->i_block_group *
	    EXT2_BLOCKS_PER_GROUP(fs)) + fs->e2fs->e2fs_first_dblock);
}

/*
 * Implement the cylinder overflow algorithm.
 *
 * The policy implemented by this algorithm is:
 *   1) allocate the block in its requested cylinder group.
 *   2) quadradically rehash on the cylinder group number.
 *   3) brute force search for a free block.
 * 		在请求的柱面组中分配一个块；
 * 		刷新柱面组编号；
 * 		暴力搜索一个空闲块
 * 		
 */
static e4fs_daddr_t
ext2_hashalloc(struct inode *ip, int cg, long pref, int size,
    daddr_t (*allocator) (struct inode *, int, daddr_t, int))
{
	struct m_ext2fs *fs;
	e4fs_daddr_t result;	/* 64位 */
	int i, icg = cg;

	mtx_assert(EXT2_MTX(ip->i_ump), MA_OWNED);
	fs = ip->i_e2fs;
	/*
	 * 1: preferred cylinder group 首选的柱面组，也就是inode所在的柱面组
	 */
	result = (*allocator)(ip, cg, pref, size);
	if (result)
		return (result);
	/*
	 * 2: quadratic rehash 如果在inode当前柱面组中没有找到合适的数据块，则在
	 			其他柱面组进行查找
	 */
	for (i = 1; i < fs->e2fs_gcount; i *= 2) {
		cg += i;
		if (cg >= fs->e2fs_gcount)
			cg -= fs->e2fs_gcount;
		result = (*allocator)(ip, cg, 0, size);
		if (result)
			return (result);
	}
	/*
	 * 3: brute force search
	 * Note that we start at i == 2, since 0 was checked initially,
	 * and 1 is always checked in the quadratic rehash.
	 * 最后实在不行就暴力查找，与上述一种方式相对应，两者都是基于一定的规则来进行的。
	 * 在 tptfs 中不再区分柱面，所以不需要确定柱面组
	 */
	cg = (icg + 2) % fs->e2fs_gcount;
	for (i = 2; i < fs->e2fs_gcount; i++) {
		result = (*allocator)(ip, cg, 0, size);
		if (result)
			return (result);
		cg++;
		if (cg == fs->e2fs_gcount)
			cg = 0;
	}
	return (0);
}

static unsigned long
ext2_cg_number_gdb_nometa(struct m_ext2fs *fs, int cg)
{

	if (!ext2_cg_has_sb(fs, cg))
		return (0);

	if (EXT2_HAS_INCOMPAT_FEATURE(fs, EXT2F_INCOMPAT_META_BG))
		return (fs->e2fs->e3fs_first_meta_bg);

	return ((fs->e2fs_gcount + EXT2_DESCS_PER_BLOCK(fs) - 1) /
	    EXT2_DESCS_PER_BLOCK(fs));
}

static unsigned long
ext2_cg_number_gdb_meta(struct m_ext2fs *fs, int cg)
{
	unsigned long metagroup;
	int first, last;

	metagroup = cg / EXT2_DESCS_PER_BLOCK(fs);
	first = metagroup * EXT2_DESCS_PER_BLOCK(fs);
	last = first + EXT2_DESCS_PER_BLOCK(fs) - 1;

	if (cg == first || cg == first + 1 || cg == last)
		return (1);

	return (0);
}

static unsigned long
ext2_cg_number_gdb(struct m_ext2fs *fs, int cg)
{
	unsigned long first_meta_bg, metagroup;

	first_meta_bg = fs->e2fs->e3fs_first_meta_bg;
	metagroup = cg / EXT2_DESCS_PER_BLOCK(fs);

	if (!EXT2_HAS_INCOMPAT_FEATURE(fs, EXT2F_INCOMPAT_META_BG) ||
	    metagroup < first_meta_bg)
		return (ext2_cg_number_gdb_nometa(fs, cg));

	return ext2_cg_number_gdb_meta(fs, cg);
}

static int
ext2_number_base_meta_blocks(struct m_ext2fs *fs, int cg)
{
	int number;

	number = ext2_cg_has_sb(fs, cg);

	if (!EXT2_HAS_INCOMPAT_FEATURE(fs, EXT2F_INCOMPAT_META_BG) ||
	    cg < fs->e2fs->e3fs_first_meta_bg * EXT2_DESCS_PER_BLOCK(fs)) {
		if (number) {
			number += ext2_cg_number_gdb(fs, cg);
			number += fs->e2fs->e2fs_reserved_ngdb;
		}
	} else {
		number += ext2_cg_number_gdb(fs, cg);
	}

	return (number);
}

static void
ext2_mark_bitmap_end(int start_bit, int end_bit, char *bitmap)
{
	int i;

	if (start_bit >= end_bit)
		return;

	for (i = start_bit; i < ((start_bit + 7) & ~7UL); i++)
		setbit(bitmap, i);
	if (i < end_bit)
		memset(bitmap + (i >> 3), 0xff, (end_bit - i) >> 3);
}

static int
ext2_get_group_number(struct m_ext2fs *fs, e4fs_daddr_t block)
{

	return ((block - fs->e2fs->e2fs_first_dblock) / fs->e2fs_bsize);
}

static int
ext2_block_in_group(struct m_ext2fs *fs, e4fs_daddr_t block, int cg)
{

	return ((ext2_get_group_number(fs, block) == cg) ? 1 : 0);
}

static int
ext2_cg_block_bitmap_init(struct m_ext2fs *fs, int cg, struct buf *bp)
{
	int bit, bit_max, inodes_per_block;
	uint64_t start, tmp;

	if (!(fs->e2fs_gd[cg].ext4bgd_flags & EXT2_BG_BLOCK_UNINIT))
		return (0);

	memset(bp->b_data, 0, fs->e2fs_bsize);

	bit_max = ext2_number_base_meta_blocks(fs, cg);
	if ((bit_max >> 3) >= fs->e2fs_bsize)
		return (EINVAL);

	for (bit = 0; bit < bit_max; bit++)
		setbit(bp->b_data, bit);

	start = (uint64_t)cg * fs->e2fs->e2fs_bpg + fs->e2fs->e2fs_first_dblock;

	/* Set bits for block and inode bitmaps, and inode table. */
	tmp = e2fs_gd_get_b_bitmap(&fs->e2fs_gd[cg]);
	if (!EXT2_HAS_INCOMPAT_FEATURE(fs, EXT2F_INCOMPAT_FLEX_BG) ||
	    ext2_block_in_group(fs, tmp, cg))
		setbit(bp->b_data, tmp - start);

	tmp = e2fs_gd_get_i_bitmap(&fs->e2fs_gd[cg]);
	if (!EXT2_HAS_INCOMPAT_FEATURE(fs, EXT2F_INCOMPAT_FLEX_BG) ||
	    ext2_block_in_group(fs, tmp, cg))
		setbit(bp->b_data, tmp - start);

	tmp = e2fs_gd_get_i_tables(&fs->e2fs_gd[cg]);
	inodes_per_block = fs->e2fs_bsize/EXT2_INODE_SIZE(fs);
	while( tmp < e2fs_gd_get_i_tables(&fs->e2fs_gd[cg]) +
	    fs->e2fs->e2fs_ipg / inodes_per_block ) {
		if (!EXT2_HAS_INCOMPAT_FEATURE(fs, EXT2F_INCOMPAT_FLEX_BG) ||
		    ext2_block_in_group(fs, tmp, cg))
			setbit(bp->b_data, tmp - start);
		tmp++;
	}

	/*
	 * Also if the number of blocks within the group is less than
	 * the blocksize * 8 ( which is the size of bitmap ), set rest
	 * of the block bitmap to 1
	 */
	ext2_mark_bitmap_end(fs->e2fs->e2fs_bpg, fs->e2fs_bsize * 8,
	    bp->b_data);

	/* Clean the flag */
	fs->e2fs_gd[cg].ext4bgd_flags &= ~EXT2_BG_BLOCK_UNINIT;

	return (0);
}

/*
 * Determine whether a block can be allocated.
 *
 * Check to see if a block of the appropriate size is available,
 * and if it is, allocate it.
 * 检查是否有适当大小的块可用，如果可用，则分配它
 */
static daddr_t
ext2_alloccg(struct inode *ip, int cg, daddr_t bpref, int size)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct ext2mount *ump;
	daddr_t bno, runstart, runlen;
	int bit, loc, end, error, start;
	char *bbp;
	/* XXX ondisk32 */
	fs = ip->i_e2fs;
	ump = ip->i_ump;
	/*
		判断块组中的可用数据块的数量，如果为0的话就直接返回
	*/
	if (e2fs_gd_get_nbfree(&fs->e2fs_gd[cg]) == 0)
		return (0);
	EXT2_UNLOCK(ump);
	/* 定位到数据块位图的第一个块，读取出来 
		ext2 文件系统块组的数据块位图貌似只占用一个数据块
	*/
	error = bread(ip->i_devvp, fsbtodb(fs,
	    e2fs_gd_get_b_bitmap(&fs->e2fs_gd[cg])),
	    (int)fs->e2fs_bsize, NOCRED, &bp);
	if (error) {
		brelse(bp);
		EXT2_LOCK(ump);
		return (0);
	}
	/* 
		处理 checksum。当前 ext2 中的 checksum 相关的实现貌似都是跟 ext4 文件系统有关系。
		所以在目前的实现当中可以暂时先不考虑这些，整理出一个基础版本
	*/
	if (EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_GDT_CSUM) ||
	    EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM)) {
		error = ext2_cg_block_bitmap_init(fs, cg, bp);
		if (error) {
			brelse(bp);
			EXT2_LOCK(ump);
			return (0);
		}
		ext2_gd_b_bitmap_csum_set(fs, cg, bp);
	}
	error = ext2_gd_b_bitmap_csum_verify(fs, cg, bp);
	if (error) {
		brelse(bp);
		EXT2_LOCK(ump);
		return (0);
	}
	if (e2fs_gd_get_nbfree(&fs->e2fs_gd[cg]) == 0) {
		/*
		 * Another thread allocated the last block in this
		 * group while we were waiting for the buffer.
		 * 在我们等待缓冲区时，另一个线程分配了这个组中的最后一个块
		 */
		brelse(bp);
		EXT2_LOCK(ump);
		return (0);
	}
	bbp = (char *)bp->b_data;

	/*
		判断我们想要分配的数据块在不在当前的块组中，如果在的话，执行后面的if代码分支；如果不在的话，
		直接置零，应该是要从当前块组或者其他块组中找一个可用的块
	*/
	if (dtog(fs, bpref) != cg)
		bpref = 0;
	if (bpref != 0) {
		bpref = dtogd(fs, bpref);	/* 获取优先分配的数据块在块组中的相对位置 */
		/*
		 * if the requested block is available, use it
		 		从这里可以看到，isclr 传入的块号其实是数据块在块组中的相对块号。
				判断这个块是否已经被占用了。如果没有被占用，直接goto跳转，如果被
				占用了，执行下面的代码分支
		 */
		if (isclr(bbp, bpref)) {
			bno = bpref;
			goto gotit;
		}
	}
	/*
	 * no blocks in the requested cylinder, so take next
	 * available one in this cylinder group.
	 * first try to get 8 contigous blocks, then fall back to a single
	 * block.
	 * 在块组中并没有找到对应的块，所以要在当前块组中接着查找下一个可用的块。
	 * 首先尝试获取8个连续的块，然后再跳转回单个数据块
	 * dtogd 宏计算出 bpref 块在块组中的偏移，然后除8取模，算出来 bpref 是在 bitmap
	 * 块中的那个字节当中；e2fs_fpg 表示一个块组中一共包含有多少个片(块)，宏 howmany 
	 * 计算出这些数据块一共会占用多少个 8 bit，作为 bitmap 遍历的结尾
	 */
	if (bpref)
		start = dtogd(fs, bpref) / NBBY;
	else
		start = 0;
	end = howmany(fs->e2fs->e2fs_fpg, NBBY) - start;
retry:
	runlen = 0;
	runstart = 0;
	for (loc = start; loc < end; loc++) {
		/*
			这里处理的情况是优先块是在cg块组中，但是它已经被占用的情况
		*/
		if (bbp[loc] == (char)0xff) {
			runlen = 0;
			continue;
		}

		/* Start of a run, find the number of high clear bits. 
			fls() 函数的作用是返回最后一个高位bit，所以 runlen 就表示后面剩余
			空闲 bit 的数量(此时要保证 bit 后全部是低位，因为从注释可以看出，
			这部分逻辑处理的是一次分配连续8个可用数据块。runstart 表示的应该是
			连续可用数据块的首个数据块号)
		*/
		if (runlen == 0) {
			bit = fls(bbp[loc]);
			runlen = NBBY - bit;
			runstart = loc * NBBY + bit;
		} else if (bbp[loc] == 0) {
			/* Continue a run. */
			runlen += NBBY;
		} else {
			/*
			 * Finish the current run.  If it isn't long
			 * enough, start a new one.
			 * ffs() 函数返回的是第一个高位 bit，需要保证该 bit 前面的所有位
			 * 都是低位(这样做是为了跟前一次高可用 bit 连贯起来，因为它们在内存
			 * 中的位置是不连续的)。从这里可以看出，runlen 表示就是连续可用 bit
			 * 数，只要 runlen >= 8，就满足了连续块数量要求，就可以返回 (返回值)
			 * 是连续块首块号
			 */
			bit = ffs(bbp[loc]) - 1;
			runlen += bit;
			if (runlen >= 8) {
				bno = runstart;
				goto gotit;
			}

			/* Run was too short, start a new one. */
			bit = fls(bbp[loc]);
			runlen = NBBY - bit;
			runstart = loc * NBBY + bit;
		}

		/* If the current run is long enough, use it. */
		if (runlen >= 8) {
			bno = runstart;
			goto gotit;
		}
	}	/* end for */
	/*
	  类似二分查找，查找从 start 开始的后半部分是否有可用 block。如果没有，
	  将 end 设置成 start，同时 start 设置成 0，相当于是检查前一部分。
	  这一部分貌似就没有数据块连续排布的要求，每次好像只分配一个数据块
	*/
	if (start != 0) {
		end = start;
		start = 0;
		goto retry;
	}
	bno = ext2_mapsearch(fs, bbp, bpref);
	if (bno < 0) {
		brelse(bp);
		EXT2_LOCK(ump);
		return (0);
	}
gotit:
#ifdef INVARIANTS
	if (isset(bbp, bno)) {
		printf("ext2fs_alloccgblk: cg=%d bno=%jd fs=%s\n",
		    cg, (intmax_t)bno, fs->e2fs_fsmnt);
		panic("ext2fs_alloccg: dup alloc");
	}
#endif
	setbit(bbp, bno);
	EXT2_LOCK(ump);
	ext2_clusteracct(fs, bbp, cg, bno, -1);
	fs->e2fs_fbcount--;
	e2fs_gd_set_nbfree(&fs->e2fs_gd[cg],
	    e2fs_gd_get_nbfree(&fs->e2fs_gd[cg]) - 1);
	fs->e2fs_fmod = 1;
	EXT2_UNLOCK(ump);
	ext2_gd_b_bitmap_csum_set(fs, cg, bp);
	bdwrite(bp);
	return (((uint64_t)cg) * fs->e2fs->e2fs_fpg + fs->e2fs->e2fs_first_dblock + bno);
}

/*
 * Determine whether a cluster can be allocated.
 */
static daddr_t
ext2_clusteralloc(struct inode *ip, int cg, daddr_t bpref, int len)
{
	struct m_ext2fs *fs;
	struct ext2mount *ump;
	struct buf *bp;
	char *bbp;
	int bit, error, got, i, loc, run;
	int32_t *lp;
	daddr_t bno;

	fs = ip->i_e2fs;
	ump = ip->i_ump;

	if (fs->e2fs_maxcluster[cg] < len)
		return (0);

	EXT2_UNLOCK(ump);
	error = bread(ip->i_devvp,
	    fsbtodb(fs, e2fs_gd_get_b_bitmap(&fs->e2fs_gd[cg])),
	    (int)fs->e2fs_bsize, NOCRED, &bp);
	if (error)
		goto fail_lock;

	bbp = (char *)bp->b_data;
	EXT2_LOCK(ump);
	/*
	 * Check to see if a cluster of the needed size (or bigger) is
	 * available in this cylinder group.
	 */
	lp = &fs->e2fs_clustersum[cg].cs_sum[len];
	for (i = len; i <= fs->e2fs_contigsumsize; i++)
		if (*lp++ > 0)
			break;
	if (i > fs->e2fs_contigsumsize) {
		/*
		 * Update the cluster summary information to reflect
		 * the true maximum-sized cluster so that future cluster
		 * allocation requests can avoid reading the bitmap only
		 * to find no cluster.
		 */
		lp = &fs->e2fs_clustersum[cg].cs_sum[len - 1];
		for (i = len - 1; i > 0; i--)
			if (*lp-- > 0)
				break;
		fs->e2fs_maxcluster[cg] = i;
		goto fail;
	}
	EXT2_UNLOCK(ump);

	/* Search the bitmap to find a big enough cluster like in FFS. */
	if (dtog(fs, bpref) != cg)
		bpref = 0;
	if (bpref != 0)
		bpref = dtogd(fs, bpref);
	loc = bpref / NBBY;
	bit = 1 << (bpref % NBBY);
	for (run = 0, got = bpref; got < fs->e2fs->e2fs_fpg; got++) {
		if ((bbp[loc] & bit) != 0)
			run = 0;
		else {
			run++;
			if (run == len)
				break;
		}
		if ((got & (NBBY - 1)) != (NBBY - 1))
			bit <<= 1;
		else {
			loc++;
			bit = 1;
		}
	}

	if (got >= fs->e2fs->e2fs_fpg)
		goto fail_lock;

	/* Allocate the cluster that we found. */
	for (i = 1; i < len; i++)
		if (!isclr(bbp, got - run + i))
			panic("ext2_clusteralloc: map mismatch");

	bno = got - run + 1;
	if (bno >= fs->e2fs->e2fs_fpg)
		panic("ext2_clusteralloc: allocated out of group");

	EXT2_LOCK(ump);
	for (i = 0; i < len; i += fs->e2fs_fpb) {
		setbit(bbp, bno + i);
		ext2_clusteracct(fs, bbp, cg, bno + i, -1);
		fs->e2fs_fbcount--;
		e2fs_gd_set_nbfree(&fs->e2fs_gd[cg],
		    e2fs_gd_get_nbfree(&fs->e2fs_gd[cg]) - 1);
	}
	fs->e2fs_fmod = 1;
	EXT2_UNLOCK(ump);

	bdwrite(bp);
	return (cg * fs->e2fs->e2fs_fpg + fs->e2fs->e2fs_first_dblock + bno);

fail_lock:
	EXT2_LOCK(ump);
fail:
	brelse(bp);
	return (0);
}

static int
ext2_zero_inode_table(struct inode *ip, int cg)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	int i, all_blks, used_blks;

	fs = ip->i_e2fs;

	/* 判断块组中的 inode table 是否已经设置为空 */
	if (fs->e2fs_gd[cg].ext4bgd_flags & EXT2_BG_INODE_ZEROED)
		return (0);

	/* 计算 inode 所占用的数据块个数 */
	all_blks = fs->e2fs->e2fs_inode_size * fs->e2fs->e2fs_ipg /
	    fs->e2fs_bsize;

	/* 计算已经被占用的数据块个数？ */
	used_blks = howmany(fs->e2fs->e2fs_ipg -
	    e2fs_gd_get_i_unused(&fs->e2fs_gd[cg]),
	    fs->e2fs_bsize / EXT2_INODE_SIZE(fs));

	/* 把所有 inode table 中未使用的 inode entry 数据清零 */
	for (i = 0; i < all_blks - used_blks; i++) {
		bp = getblk(ip->i_devvp, fsbtodb(fs,
		    e2fs_gd_get_i_tables(&fs->e2fs_gd[cg]) + used_blks + i),
		    fs->e2fs_bsize, 0, 0, 0);
		if (!bp)
			return (EIO);

		vfs_bio_bzero_buf(bp, 0, fs->e2fs_bsize);
		bawrite(bp);
	}
	
	fs->e2fs_gd[cg].ext4bgd_flags |= EXT2_BG_INODE_ZEROED;

	return (0);
}

/*
 * Determine whether an inode can be allocated.
 * 确定是否可以分配inode
 *
 * Check to see if an inode is available, and if it is,
 * allocate it using tode in the specified cylinder group.
 * 检查 inode 是否可用，如果可用，则在指定的柱面组中使用 tode 分配它
 */
static daddr_t
ext2_nodealloccg(struct inode *ip, int cg, daddr_t ipref, int mode)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct ext2mount *ump;
	int error, start, len, ifree;
	char *ibp, *loc;

	/* 从上层调用来看，ipref 指定的是块组中第一个的 inode */
	ipref--;	/* to avoid a lot of (ipref -1) */
	if (ipref == -1)
		ipref = 0;
	fs = ip->i_e2fs;	/* superblock */
	ump = ip->i_ump;	/* 挂载点信息 */

	/* 如果空闲的 inode 数量刚好等于0，直接返回0 */
	if (e2fs_gd_get_nifree(&fs->e2fs_gd[cg]) == 0)
		return (0);
	EXT2_UNLOCK(ump);

	/* 
		如果还有空闲的 inode，指向下面的代码分支，将获取到的数据存放到 buf 当中 
	*/
	error = bread(ip->i_devvp, fsbtodb(fs,
	    e2fs_gd_get_i_bitmap(&fs->e2fs_gd[cg])),
	    (int)fs->e2fs_bsize, NOCRED, &bp);
	if (error) {
		brelse(bp);
		EXT2_LOCK(ump);
		return (0);
	}
	if (EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_GDT_CSUM) ||
	    EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM)) {
		if (fs->e2fs_gd[cg].ext4bgd_flags & EXT2_BG_INODE_UNINIT) {
			memset(bp->b_data, 0, fs->e2fs_bsize);
			fs->e2fs_gd[cg].ext4bgd_flags &= ~EXT2_BG_INODE_UNINIT;
		}
		/* 下面的操作主要就是对读取的数据计算checksum，判断准确性 */
		ext2_gd_i_bitmap_csum_set(fs, cg, bp);
		error = ext2_zero_inode_table(ip, cg);
		if (error) {
			brelse(bp);
			EXT2_LOCK(ump);
			return (0);
		}
	}
	error = ext2_gd_i_bitmap_csum_verify(fs, cg, bp);
	if (error) {
		brelse(bp);
		EXT2_LOCK(ump);
		return (0);
	}
	if (e2fs_gd_get_nifree(&fs->e2fs_gd[cg]) == 0) {
		/*
		 * Another thread allocated the last i-node in this
		 * group while we were waiting for the buffer.
		 * 当我们在等待buf的时候，另外一个线程申请了最后一个这个块组中可用的 inode
		 */
		brelse(bp);
		EXT2_LOCK(ump);
		return (0);
	}
	/* 将从磁盘读取到的 buf 中的数据提取出来 */
	ibp = (char *)bp->b_data;
	if (ipref) {
		ipref %= fs->e2fs->e2fs_ipg;
		if (isclr(ibp, ipref))
			goto gotit;
	}
	start = ipref / NBBY;
	len = howmany(fs->e2fs->e2fs_ipg - ipref, NBBY);
	loc = memcchr(&ibp[start], 0xff, len);
	if (loc == NULL) {
		len = start + 1;
		start = 0;
		loc = memcchr(&ibp[start], 0xff, len);
		if (loc == NULL) {
			printf("cg = %d, ipref = %lld, fs = %s\n",
			    cg, (long long)ipref, fs->e2fs_fsmnt);
			panic("ext2fs_nodealloccg: map corrupted");
			/* NOTREACHED */
		}
	}
	ipref = (loc - ibp) * NBBY + ffs(~*loc) - 1;
gotit:
	setbit(ibp, ipref);
	EXT2_LOCK(ump);
	e2fs_gd_set_nifree(&fs->e2fs_gd[cg],
	    e2fs_gd_get_nifree(&fs->e2fs_gd[cg]) - 1);
	if (EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_GDT_CSUM) ||
	    EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM)) {
		ifree = fs->e2fs->e2fs_ipg - e2fs_gd_get_i_unused(&fs->e2fs_gd[cg]);
		if (ipref + 1 > ifree)
			e2fs_gd_set_i_unused(&fs->e2fs_gd[cg],
			    fs->e2fs->e2fs_ipg - (ipref + 1));
	}
	fs->e2fs->e2fs_ficount--;
	fs->e2fs_fmod = 1;
	if ((mode & IFMT) == IFDIR) {
		e2fs_gd_set_ndirs(&fs->e2fs_gd[cg],
		    e2fs_gd_get_ndirs(&fs->e2fs_gd[cg]) + 1);
		fs->e2fs_total_dir++;
	}
	EXT2_UNLOCK(ump);
	ext2_gd_i_bitmap_csum_set(fs, cg, bp);
	bdwrite(bp);
	return ((uint64_t)cg * fs->e2fs_ipg + ipref + 1);
}

/*
 * Free a block or fragment.
 *
 */
void
ext2_blkfree(struct inode *ip, e4fs_daddr_t bno, long size)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct ext2mount *ump;
	int cg, error;
	char *bbp;

	fs = ip->i_e2fs;
	ump = ip->i_ump;
	cg = dtog(fs, bno);
	if (bno >= fs->e2fs_bcount) {
		printf("bad block %lld, ino %ju\n", (long long)bno,
		    (uintmax_t)ip->i_number);
		ext2_fserr(fs, ip->i_uid, "bad block");
		return;
	}
	error = bread(ip->i_devvp,
	    fsbtodb(fs, e2fs_gd_get_b_bitmap(&fs->e2fs_gd[cg])),
	    (int)fs->e2fs_bsize, NOCRED, &bp);
	if (error) {
		brelse(bp);
		return;
	}
	bbp = (char *)bp->b_data;
	bno = dtogd(fs, bno);
	if (isclr(bbp, bno)) {
		printf("block = %lld, fs = %s\n",
		    (long long)bno, fs->e2fs_fsmnt);
		panic("ext2_blkfree: freeing free block");
	}
	clrbit(bbp, bno);
	EXT2_LOCK(ump);
	ext2_clusteracct(fs, bbp, cg, bno, 1);
	fs->e2fs_fbcount++;
	e2fs_gd_set_nbfree(&fs->e2fs_gd[cg],
	    e2fs_gd_get_nbfree(&fs->e2fs_gd[cg]) + 1);
	fs->e2fs_fmod = 1;
	EXT2_UNLOCK(ump);
	ext2_gd_b_bitmap_csum_set(fs, cg, bp);
	bdwrite(bp);
}

/*
 * Free an inode.
 *
 */
int
ext2_vfree(struct vnode *pvp, ino_t ino, int mode)
{
	struct m_ext2fs *fs;
	struct inode *pip;
	struct buf *bp;
	struct ext2mount *ump;
	int error, cg;
	char *ibp;

	pip = VTOI(pvp);
	fs = pip->i_e2fs;
	ump = pip->i_ump;
	if ((u_int)ino > fs->e2fs_ipg * fs->e2fs_gcount)
		panic("ext2_vfree: range: devvp = %p, ino = %ju, fs = %s",
		    pip->i_devvp, (uintmax_t)ino, fs->e2fs_fsmnt);

	cg = ino_to_cg(fs, ino);
	error = bread(pip->i_devvp,
	    fsbtodb(fs, e2fs_gd_get_i_bitmap(&fs->e2fs_gd[cg])),
	    (int)fs->e2fs_bsize, NOCRED, &bp);
	if (error) {
		brelse(bp);
		return (0);
	}
	ibp = (char *)bp->b_data;
	ino = (ino - 1) % fs->e2fs->e2fs_ipg;
	if (isclr(ibp, ino)) {
		printf("ino = %ju, fs = %s\n",
		    ino, fs->e2fs_fsmnt);
		if (fs->e2fs_ronly == 0)
			panic("ext2_vfree: freeing free inode");
	}
	clrbit(ibp, ino);
	EXT2_LOCK(ump);
	fs->e2fs->e2fs_ficount++;
	e2fs_gd_set_nifree(&fs->e2fs_gd[cg],
	    e2fs_gd_get_nifree(&fs->e2fs_gd[cg]) + 1);
	if ((mode & IFMT) == IFDIR) {
		e2fs_gd_set_ndirs(&fs->e2fs_gd[cg],
		    e2fs_gd_get_ndirs(&fs->e2fs_gd[cg]) - 1);
		fs->e2fs_total_dir--;
	}
	fs->e2fs_fmod = 1;
	EXT2_UNLOCK(ump);
	ext2_gd_i_bitmap_csum_set(fs, cg, bp);
	bdwrite(bp);
	return (0);
}

/*
 * Find a block in the specified cylinder group.
 *
 * It is a panic if a request is made to find a block if none are
 * available.
 */
static daddr_t
ext2_mapsearch(struct m_ext2fs *fs, char *bbp, daddr_t bpref)
{
	char *loc;
	int start, len;

	/*
	 * find the fragment by searching through the free block
	 * map for an appropriate bit pattern
	 */
	if (bpref)
		start = dtogd(fs, bpref) / NBBY;
	else
		start = 0;
	len = howmany(fs->e2fs->e2fs_fpg, NBBY) - start;
	loc = memcchr(&bbp[start], 0xff, len);
	if (loc == NULL) {
		len = start + 1;
		start = 0;
		loc = memcchr(&bbp[start], 0xff, len);
		if (loc == NULL) {
			printf("start = %d, len = %d, fs = %s\n",
			    start, len, fs->e2fs_fsmnt);
			panic("ext2_mapsearch: map corrupted");
			/* NOTREACHED */
		}
	}
	return ((loc - bbp) * NBBY + ffs(~*loc) - 1);
}

/*
 * Fserr prints the name of a filesystem with an error diagnostic.
 *
 * The form of the error message is:
 *	fs: error message
 */
void
ext2_fserr(struct m_ext2fs *fs, uid_t uid, char *cp)
{

	log(LOG_ERR, "uid %u on %s: %s\n", uid, fs->e2fs_fsmnt, cp);
}

int
ext2_cg_has_sb(struct m_ext2fs *fs, int cg)
{
	int a3, a5, a7;

	if (cg == 0)
		return (1);

	if (EXT2_HAS_COMPAT_FEATURE(fs, EXT2F_COMPAT_SPARSESUPER2)) {
		if (cg == fs->e2fs->e4fs_backup_bgs[0] ||
		    cg == fs->e2fs->e4fs_backup_bgs[1])
			return (1);
		return (0);
	}

	if ((cg <= 1) ||
	    !EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_SPARSESUPER))
		return (1);

	if (!(cg & 1))
		return (0);

	for (a3 = 3, a5 = 5, a7 = 7;
	    a3 <= cg || a5 <= cg || a7 <= cg;
	    a3 *= 3, a5 *= 5, a7 *= 7)
		if (cg == a3 || cg == a5 || cg == a7)
			return (1);
	return (0);
}
