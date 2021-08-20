/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1991, 1993
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
 *	@(#)ufs_bmap.c	8.7 (Berkeley) 3/21/95
 * $FreeBSD: releng/12.0/sys/fs/ext2fs/ext2_bmap.c 333584 2018-05-13 19:19:10Z fsu $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/racct.h>
#include <sys/resourcevar.h>
#include <sys/stat.h>

#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/inode.h>
#include <fs/ext2fs/ext2fs.h>
#include <fs/ext2fs/ext2_dinode.h>
#include <fs/ext2fs/ext2_extern.h>
#include <fs/ext2fs/ext2_mount.h>

/*
 * Bmap converts the logical block number of a file to its physical block
 * number on the disk. The conversion is done by using the logical block
 * number to index into the array of block pointers described by the dinode.
 * 
 * bmap是将一个文件的逻辑块号转换成它在磁盘上的物理块号。这种转换是通过使用逻辑块号索引到由
 * dinode描述的块指针数组来完成的
 * 
 * 	struct vop_bmap_args {
		struct vnode *a_vp;
		daddr_t  a_bn;
		struct bufobj **a_bop;
		daddr_t *a_bnp;
		int *a_runp;
		int *a_runb;
	}
 */
int
ext2_bmap(struct vop_bmap_args *ap)
{
	daddr_t blkno;
	int error;

	/*
	 * Check for underlying vnode requests and ensure that logical
	 * to physical mapping is requested.
	 * 检查底层 vnode 请求并确保请求了逻辑到物理的映射
	 * a_bop 表示的 bufboj 指针，a_vp 表示的是 vnode 指针，a_bnp: block number，应该还是逻辑块号(因为我们
	 * 访问磁盘的话，应该不会准确知道这个文件存在磁盘上的哪个物理块中，直接操作的还是逻辑块号)
	 */
	if (ap->a_bop != NULL)
		*ap->a_bop = &VTOI(ap->a_vp)->i_devvp->v_bufobj;
	if (ap->a_bnp == NULL)	// 判断请求中是否有逻辑块号
		return (0);
	/*
		a_vp 表示的是 vnode 指针，a_bn 表示的应该是 blkno
	*/
	if (VTOI(ap->a_vp)->i_flag & IN_E4EXTENTS)
		error = ext4_bmapext(ap->a_vp, ap->a_bn, &blkno,
		    ap->a_runp, ap->a_runb);
	else
		error = ext2_bmaparray(ap->a_vp, ap->a_bn, &blkno,
		    ap->a_runp, ap->a_runb);
	*ap->a_bnp = blkno;	/* a_bnp 应该就是指向我们需要计算的磁盘块号 */
	return (error);
}

/*
 * Convert the logical block number of a file to its physical block number
 * on the disk within ext4 extents.
 */
int
ext4_bmapext(struct vnode *vp, int32_t bn, int64_t *bnp, int *runp, int *runb)
{
	struct inode *ip;
	struct m_ext2fs *fs;
	struct ext4_extent_header *ehp;
	struct ext4_extent *ep;
	struct ext4_extent_path *path = NULL;
	daddr_t lbn;
	int error, depth;

	ip = VTOI(vp);
	fs = ip->i_e2fs;
	lbn = bn;
	ehp = (struct ext4_extent_header *)ip->i_data;
	depth = ehp->eh_depth;

	*bnp = -1;
	if (runp != NULL)
		*runp = 0;
	if (runb != NULL)
		*runb = 0;

	error = ext4_ext_find_extent(ip, lbn, &path);
	if (error)
		return (error);

	ep = path[depth].ep_ext;
	if(ep) {
		if (lbn < ep->e_blk) {
			if (runp != NULL)
				*runp = ep->e_blk - lbn - 1;
		} else if (ep->e_blk <= lbn && lbn < ep->e_blk + ep->e_len) {
			*bnp = fsbtodb(fs, lbn - ep->e_blk +
			    (ep->e_start_lo | (daddr_t)ep->e_start_hi << 32));
			if (runp != NULL)
				*runp = ep->e_len - (lbn - ep->e_blk) - 1;
			if (runb != NULL)
				*runb = lbn - ep->e_blk;
		} else {
			if (runb != NULL)
				*runb = ep->e_blk + lbn - ep->e_len;
		}
	}

	ext4_ext_path_free(path);

	return (error);
}

/*
 * Indirect blocks are now on the vnode for the file.  They are given negative
 * logical block numbers.  Indirect blocks are addressed by the negative
 * address of the first data block to which they point.  Double indirect blocks
 * are addressed by one less than the address of the first indirect block to
 * which they point.  Triple indirect blocks are addressed by one less than
 * the address of the first double indirect block to which they point.
 * 这里讲的内容应该是文件的三级间接寻址 
 *
 * ext2_bmaparray does the bmap conversion, and if requested returns the
 * array of logical blocks which must be traversed to get to a block.
 * Each entry contains the offset into that block that gets you to the
 * next block and the disk address of the block (if it is assigned).
 * ext2_bmaparray 执行 bmap 转换，如果请求，则返回逻辑块数组，必须遍历这些逻辑块才能得到块。
 * 每个条目都包含到该块的偏移量，该偏移量使您到达下一个块，以及该块的磁盘地址（如果已分配）
 * 
 * bn: block number(从上层函数追下来，bn = 2)，bn表示的应该就是计算出的那个逻辑块号
 * bnp: block number pointer
 */

int
ext2_bmaparray(struct vnode *vp, daddr_t bn, daddr_t *bnp, int *runp, int *runb)
{
	struct inode *ip;
	struct buf *bp;
	struct ext2mount *ump;
	struct mount *mp;
	struct indir a[EXT2_NIADDR + 1], *ap;	/* a表示的应该是三级间接寻址 */
	daddr_t daddr;
	e2fs_lbn_t metalbn;	/* metadata logical block number */
	int error, num, maxrun = 0, bsize;
	int *nump;	/* 指向上面定义的变量 num */

	ap = NULL;
	ip = VTOI(vp);	/* vnode 对应的 inode */
	mp = vp->v_mount;	/* 挂载点对应的 mount 结构 */
	ump = VFSTOEXT2(mp);

	/* 获取文件系统 block_size 大小 */
	bsize = EXT2_BLOCK_SIZE(ump->um_e2fs);

	if (runp) {
		maxrun = mp->mnt_iosize_max / bsize - 1;
		*runp = 0;
	}
	if (runb)
		*runb = 0;


	ap = a;	/* 指向寻址数组的指针 */
	nump = &num;	
	error = ext2_getlbns(vp, bn, ap, nump);
	if (error)
		return (error);

	num = *nump;
	if (num == 0) {
		*bnp = blkptrtodb(ump, ip->i_db[bn]);
		if (*bnp == 0) {
			*bnp = -1;
		} else if (runp) {
			daddr_t bnb = bn;

			for (++bn; bn < EXT2_NDADDR && *runp < maxrun &&
			    is_sequential(ump, ip->i_db[bn - 1], ip->i_db[bn]);
			    ++bn, ++*runp);
			bn = bnb;
			if (runb && (bn > 0)) {
				for (--bn; (bn >= 0) && (*runb < maxrun) &&
					is_sequential(ump, ip->i_db[bn],
						ip->i_db[bn + 1]);
						--bn, ++*runb);
			}
		}
		return (0);
	}

	/* Get disk address out of indirect block array */
	daddr = ip->i_ib[ap->in_off];

	for (bp = NULL, ++ap; --num; ++ap) {
		/*
		 * Exit the loop if there is no disk address assigned yet and
		 * the indirect block isn't in the cache, or if we were
		 * looking for an indirect block and we've found it.
		 */

		metalbn = ap->in_lbn;
		if ((daddr == 0 && !incore(&vp->v_bufobj, metalbn)) || metalbn == bn)
			break;
		/*
		 * If we get here, we've either got the block in the cache
		 * or we have a disk address for it, go fetch it.
		 */
		if (bp)
			bqrelse(bp);

		bp = getblk(vp, metalbn, bsize, 0, 0, 0);
		if ((bp->b_flags & B_CACHE) == 0) {
#ifdef INVARIANTS
			if (!daddr)
				panic("ext2_bmaparray: indirect block not in cache");
#endif
			bp->b_blkno = blkptrtodb(ump, daddr);
			bp->b_iocmd = BIO_READ;
			bp->b_flags &= ~B_INVAL;
			bp->b_ioflags &= ~BIO_ERROR;
			vfs_busy_pages(bp, 0);
			bp->b_iooffset = dbtob(bp->b_blkno);
			bstrategy(bp);
#ifdef RACCT
			if (racct_enable) {
				PROC_LOCK(curproc);
				racct_add_buf(curproc, bp, 0);
				PROC_UNLOCK(curproc);
			}
#endif
			curthread->td_ru.ru_inblock++;
			error = bufwait(bp);
			if (error) {
				brelse(bp);
				return (error);
			}
		}

		daddr = ((e2fs_daddr_t *)bp->b_data)[ap->in_off];
		if (num == 1 && daddr && runp) {
			for (bn = ap->in_off + 1;
			    bn < MNINDIR(ump) && *runp < maxrun &&
			    is_sequential(ump,
			    ((e2fs_daddr_t *)bp->b_data)[bn - 1],
			    ((e2fs_daddr_t *)bp->b_data)[bn]);
			    ++bn, ++*runp);
			bn = ap->in_off;
			if (runb && bn) {
				for (--bn; bn >= 0 && *runb < maxrun &&
					is_sequential(ump,
					((e2fs_daddr_t *)bp->b_data)[bn],
					((e2fs_daddr_t *)bp->b_data)[bn + 1]);
					--bn, ++*runb);
			}
		}
	}
	if (bp)
		bqrelse(bp);

	/*
	 * Since this is FFS independent code, we are out of scope for the
	 * definitions of BLK_NOCOPY and BLK_SNAP, but we do know that they
	 * will fall in the range 1..um_seqinc, so we use that test and
	 * return a request for a zeroed out buffer if attempts are made
	 * to read a BLK_NOCOPY or BLK_SNAP block.
	 */
	if ((ip->i_flags & SF_SNAPSHOT) && daddr > 0 && daddr < ump->um_seqinc) {
		*bnp = -1;
		return (0);
	}
	*bnp = blkptrtodb(ump, daddr);
	if (*bnp == 0) {
		*bnp = -1;
	}
	return (0);
}

/*
 * Create an array of logical block number/offset pairs which represent the
 * path of indirect blocks required to access a data block.  The first "pair"
 * contains the logical block number of the appropriate single, double or
 * triple indirect block and the offset into the inode indirect block array.
 * Note, the logical block number of the inode single/double/triple indirect
 * block appears twice in the array, once with the offset into the i_ib and
 * once with the offset into the page itself.
 * 创建一个逻辑块号/偏移对数组，表示访问数据块所需的间接块的路径。第一个“对”包含相应的单、双或
 * 三个间接块的逻辑块号，以及inode间接块数组中的偏移量。注意，inode single/double/triple 
 * 间接寻址块的逻辑块号在数组中出现两次，一次偏移量出现在 i_ib 中，另一次偏移量出现在页本身中
 * 
 * ext2fs get logical block number
 * 从上层代码逻辑可以看出，ap 指向的是数据块寻址的数组，bn 应该是文件块的逻辑块号
 */
int
ext2_getlbns(struct vnode *vp, daddr_t bn, struct indir *ap, int *nump)
{
	long blockcnt;
	e2fs_lbn_t metalbn, realbn;	/* 逻辑块 */
	struct ext2mount *ump;
	int i, numlevels, off;
	int64_t qblockcnt;

	/* 获取ext2 mount */
	ump = VFSTOEXT2(vp->v_mount);
	if (nump)
		*nump = 0;
	numlevels = 0; /* 应该表示的是寻址的等级，1、2、3级直接或者间接寻址 */
	realbn = bn;	
	if ((long)bn < 0)
		bn = -(long)bn;

	/* The first EXT2_NDADDR blocks are direct blocks. 
		如果块号是在一级直接寻址范围内(也就是小于12)，就直接返回，此时 *unmp = 0。
		当块号大于12的时候，执行下面的代码分支；
		从这里其实可以反推，bn 就是表示的文件逻辑块号
	*/
	if (bn < EXT2_NDADDR)
		return (0);

	/*
	 * Determine the number of levels of indirection.  After this loop
	 * is done, blockcnt indicates the number of data blocks possible
	 * at the previous level of indirection, and EXT2_NIADDR - i is the
	 * number of levels of indirection needed to locate the requested block.
	 * 确定间接寻址的级别数。完成此循环后，blockcnt 表示上一级间接寻址可能的数据块数，
	 * EXT2_NIADDR-i 表示定位请求块所需的间接寻址级别数
	 */
	for (blockcnt = 1, i = EXT2_NIADDR, bn -= EXT2_NDADDR; ;
	    i--, bn -= blockcnt) {
		if (i == 0)
			return (EFBIG);	/* 返回错误码：文件太大 */
		/*
		 * Use int64_t's here to avoid overflow for triple indirect
		 * blocks when longs have 32 bits and the block size is more
		 * than 4K.
		 * 当long有32位并且块大小超过4K时，在这里使用 int64_t 可以避免三重间接块溢出。
		 * MNINDIR 表示每个块能包含有多少个块指针。第一次循环，blockcnt = 1，qblockcnt
		 * 就表示一级间接寻址所包含的块数。如果 bn < qblockcnt，也就说明这个块也就用到了
		 * 一级间接寻址(bn在循环开始赋值的时候就已经把直接寻址的12个块给去掉了)
		 */
		qblockcnt = (int64_t)blockcnt * MNINDIR(ump);
		if (bn < qblockcnt)
			break;
		blockcnt = qblockcnt;
		/*
			i = 3 时，1级间接寻址；
			i = 2 时，2级间接寻址；
			i = 1 时，3级间接寻址。
			所以当 i = 0 的时候，相当于三级间接寻址都不够用，返回error。blockcnt 最多也就只能表示到
			二级间接寻址的所能容纳的数据块的个数。bn 的话其实就表示减去上一级所能容纳的最大数据块个数
			之后，当前寻址等级中剩余的数据块的个数
		*/
	}

	/* 
		Calculate the address of the first meta-block.
		间接寻址的时候，文件系统会分配单独的一个数据块用来存放磁盘块号。所以我们需要先找到存放数据块块号的这个
		块。所以下面的逻辑应该就是先找到这个数据块，然后利用offset找到我们需要读取的数据块块号在这个块中的存储
		位置，这样就可以拿到真正的磁盘块号，最后读取真实数据。bn 存放的是当前间接查找等级剩余的数据块，所以 metalbn
		表示的就是前面所有检查等级所能容纳的数据块的个数加上 EXT2_NIADDR - i (可以认为是一种offset)
	*/
	if (realbn >= 0)
		metalbn = -(realbn - bn + EXT2_NIADDR - i);
	else
		metalbn = -(-realbn - bn + EXT2_NIADDR - i);

	/*
	 * At each iteration, off is the offset into the bap array which is
	 * an array of disk addresses at the current level of indirection.
	 * The logical block number and the offset in that block are stored
	 * into the argument array.
	 * 在每次迭代中，off是bap数组的偏移量，bap数组是当前间接寻址级别的磁盘地址数组。
	 * 逻辑块号和该块中的偏移量存储在参数数组中
	 */
	ap->in_lbn = metalbn;
	ap->in_off = off = EXT2_NIADDR - i;
	ap++;
	for (++numlevels; i <= EXT2_NIADDR; i++) {
		/* If searching for a meta-data block, quit when found. */
		if (metalbn == realbn)
			break;

		/* 
			剩余量 / 上一个间接检查等级blockcnt = count
			count / 每个数据块所能包含的磁盘块指针 = 数据块中的 offset
		*/
		off = (bn / blockcnt) % MNINDIR(ump);

		++numlevels;
		ap->in_lbn = metalbn;
		ap->in_off = off;
		++ap;

		/* metabln 可以看做是每个检查等级的 base address */
		metalbn -= -1 + off * blockcnt;
		blockcnt /= MNINDIR(ump);
	}
	if (nump)
		*nump = numlevels;
	return (0);
}
