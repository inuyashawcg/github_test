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
 *	@(#)ffs_inode.c	8.5 (Berkeley) 12/30/93
 * $FreeBSD: releng/12.0/sys/fs/ext2fs/ext2_inode.c 333584 2018-05-13 19:19:10Z fsu $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mount.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/rwlock.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/inode.h>
#include <fs/ext2fs/ext2_mount.h>
#include <fs/ext2fs/ext2fs.h>
#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/ext2_extern.h>
#include <fs/ext2fs/ext2_extattr.h>

/*
 * Update the access, modified, and inode change times as specified by the
 * IN_ACCESS, IN_UPDATE, and IN_CHANGE flags respectively.  Write the inode
 * to disk if the IN_MODIFIED flag is set (it may be set initially, or by
 * the timestamp update).  The IN_LAZYMOD flag is set to force a write
 * later if not now.  If we write now, then clear both IN_MODIFIED and
 * IN_LAZYMOD to reflect the presumably successful write, and if waitfor is
 * set, then wait for the write to complete.
 * 分别更新 IN_ACCESS、IN_UPDATE 和 IN_CHANGE 标志所对应的访问、修改和改变的时间。
 * 如果 IN_MODIFIED 标志被设置的话，则将 inode 写入到磁盘当中(可以在初始化的时候设置，
 * 可以通过 timestamp 进行更新)。如果现在不写入，则设置 IN_LAZYMOD 强制延迟写入。
 * 如果我们现在写入，则清除IN_MODIFIED和IN_LAZYMOD以反映可能成功的写入，如果设置了waitfor，
 * 则等待写入完成
 */
int
ext2_update(struct vnode *vp, int waitfor)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct inode *ip;
	int error;

	ASSERT_VOP_ELOCKED(vp, "ext2_update");
	ext2_itimes(vp);
	ip = VTOI(vp);	/* vnode 对应的 inode */
	if ((ip->i_flag & IN_MODIFIED) == 0 && waitfor == 0)
		return (0);
	ip->i_flag &= ~(IN_LAZYACCESS | IN_LAZYMOD | IN_MODIFIED);
	fs = ip->i_e2fs;
	if (fs->e2fs_ronly)
		return (0);
	if ((error = bread(ip->i_devvp,
	    fsbtodb(fs, ino_to_fsba(fs, ip->i_number)),
	    (int)fs->e2fs_bsize, NOCRED, &bp)) != 0) {
		brelse(bp);
		return (error);
	}
	/* 将内存格式转换成磁盘格式，然后调用 bwrite 函数将数据更新到磁盘 */
	error = ext2_i2ei(ip, (struct ext2fs_dinode *)((char *)bp->b_data +
	    EXT2_INODE_SIZE(fs) * ino_to_fsbo(fs, ip->i_number)));
	if (error) {
		brelse(bp);
		return (error);
	}
	if (waitfor && !DOINGASYNC(vp))
		return (bwrite(bp));
	else {
		bdwrite(bp);
		return (0);
	}
}

#define	SINGLE	0	/* index of single indirect block */
#define	DOUBLE	1	/* index of double indirect block */
#define	TRIPLE	2	/* index of triple indirect block */

/*
 * Release blocks associated with the inode ip and stored in the indirect
 * block bn.  Blocks are free'd in LIFO order up to (but not including)
 * lastbn.  If level is greater than SINGLE, the block is an indirect block
 * and recursive calls to indirtrunc must be used to cleanse other indirect
 * blocks.
 * 释放与 inode ip 关联并存储在间接块 bn 中的块。区块按后进先出顺序免费，最多（但不包括）最后
 * 一个 bn。如果 level 大于 SINGLE，则该块是一个间接块，必须使用对 indirtrunc 的递归调用来
 * 清除其他间接块
 *
 * NB: triple indirect blocks are untested. 三重间接块未经测试
 */
static int
ext2_indirtrunc(struct inode *ip, daddr_t lbn, daddr_t dbn,
    daddr_t lastbn, int level, e4fs_daddr_t *countp)
{
	struct buf *bp;
	struct m_ext2fs *fs = ip->i_e2fs;
	struct vnode *vp;
	e2fs_daddr_t *bap, *copy;
	int i, nblocks, error = 0, allerror = 0;
	e2fs_lbn_t nb, nlbn, last;
	e4fs_daddr_t blkcount, factor, blocksreleased = 0;

	/*
	 * Calculate index in current block of last
	 * block to be kept.  -1 indicates the entire
	 * block so we need not calculate the index.
	 * 计算当前块中要保留的最后一个块的索引，-1表示整个块，因此我们不需要计算索引
	 */
	factor = 1;
	for (i = SINGLE; i < level; i++)
		factor *= NINDIR(fs);
	/*
		从上级函数调用的情况来看，lastbn 表示的是减去上级索引之后，剩余的 block 的个数。
		这也就解释了 factor 计算每一级索引所能索引的最大块数；
		factor 表示的是上一个索引等级所能映射的最大数据块的数量，last / factor 表示的就是
		当前索引块中的偏移量
	*/
	last = lastbn;
	if (lastbn > 0)
		last /= factor;
	nblocks = btodb(fs->e2fs_bsize);
	/*
	 * Get buffer of block pointers, zero those entries corresponding
	 * to blocks to be free'd, and update on disk copy first.  Since
	 * double(triple) indirect before single(double) indirect, calls
	 * to bmap on these blocks will fail.  However, we already have
	 * the on disk address, so we have to set the b_blkno field
	 * explicitly instead of letting bread do everything for us.
	 */
	vp = ITOV(ip);
	bp = getblk(vp, lbn, (int)fs->e2fs_bsize, 0, 0, 0);
	/*
		这里应该是需要等待 I/O 操纵完成，获取到对应磁盘块的数据并保存到 buf 中。
		tptfs 中应该可以直接省略这一步，直接利用 dbn 定位到数据所在的虚拟页
	*/
	if ((bp->b_flags & (B_DONE | B_DELWRI)) == 0) {
		bp->b_iocmd = BIO_READ;
		if (bp->b_bcount > bp->b_bufsize)
			panic("ext2_indirtrunc: bad buffer size");
		bp->b_blkno = dbn;
		vfs_busy_pages(bp, 0);
		bp->b_iooffset = dbtob(bp->b_blkno);
		bstrategy(bp);
		error = bufwait(bp);
	}
	if (error) {
		brelse(bp);
		*countp = 0;
		return (error);
	}
	bap = (e2fs_daddr_t *)bp->b_data;
	copy = malloc(fs->e2fs_bsize, M_TEMP, M_WAITOK);
	/* 将 buf 中的数据拷贝一份到 copy 当中 */
	bcopy((caddr_t)bap, (caddr_t)copy, (u_int)fs->e2fs_bsize);

	/* 将没有映射数据块的 entry 全部置零 */
	bzero((caddr_t)&bap[last + 1],
	    (NINDIR(fs) - (last + 1)) * sizeof(e2fs_daddr_t));
	
	if (last == -1)
		bp->b_flags |= B_INVAL;
	/* 将数据同步到磁盘当中 */	
	if (DOINGASYNC(vp)) {
		bdwrite(bp);
	} else {
		error = bwrite(bp);
		if (error)
			allerror = error;
	}
	bap = copy;

	/*
	 * Recursively free totally unused blocks. 递归释放完全未使用的块
	 */
	for (i = NINDIR(fs) - 1, nlbn = lbn + 1 - i * factor; i > last;
	    i--, nlbn += factor) {
		nb = bap[i];
		if (nb == 0)
			continue;
		if (level > SINGLE) {
			if ((error = ext2_indirtrunc(ip, nlbn,
			    fsbtodb(fs, nb), (int32_t)-1, level - 1, &blkcount)) != 0)
				allerror = error;
			blocksreleased += blkcount;
		}
		ext2_blkfree(ip, nb, fs->e2fs_bsize);
		blocksreleased += nblocks;
	}

	/*
	 * Recursively free last partial block.
	 * 递归释放最后一个部分块
	 */
	if (level > SINGLE && lastbn >= 0) {
		last = lastbn % factor;
		nb = bap[i];
		if (nb != 0) {
			if ((error = ext2_indirtrunc(ip, nlbn, fsbtodb(fs, nb),
			    last, level - 1, &blkcount)) != 0)
				allerror = error;
			blocksreleased += blkcount;
		}
	}
	free(copy, M_TEMP);
	*countp = blocksreleased;
	return (allerror);
}

/*
 * Truncate the inode oip to at most length size, freeing the
 * disk blocks. 
 * 将 inode oip 截断为最大长度大小，释放磁盘块
 */
static int
ext2_ind_truncate(struct vnode *vp, off_t length, int flags, struct ucred *cred,
    struct thread *td)
{
	struct vnode *ovp = vp;
	e4fs_daddr_t lastblock;	/* 最后一个数据块 */
	struct inode *oip;	// oip：old inode pointer？意思可能是表示状态改变之前的 inode
	e4fs_daddr_t bn, lbn, lastiblock[EXT2_NIADDR], indir_lbn[EXT2_NIADDR];
	uint32_t oldblks[EXT2_NDADDR + EXT2_NIADDR];
	uint32_t newblks[EXT2_NDADDR + EXT2_NIADDR];
	struct m_ext2fs *fs;
	struct buf *bp;
	int offset, size, level;
	e4fs_daddr_t count, nblocks, blocksreleased = 0;	/* 释放掉的数据块的个数 */
	int error, i, allerror;
	off_t osize;	// old file size?
#ifdef INVARIANTS
	struct bufobj *bo;
#endif

	oip = VTOI(ovp);
#ifdef INVARIANTS
	bo = &ovp->v_bufobj;
#endif

	fs = oip->i_e2fs;	/* 超级块 */
	osize = oip->i_size;	/* 文件大小(in bytes) */
	/*
	 * Lengthen the size of the file. We must ensure that the
	 * last byte of the file is allocated. Since the smallest
	 * value of osize is 0, length will be at least 1.
	 * 延长文件的大小。我们必须确保文件的最后一个字节已分配。由于 osize 的最小值
	 * 为0，因此长度至少为1
	 * osize 表示的是 inode 对应文件的原有数据量大小，length 表示的应该是数据截取
	 * 其实位置？
	 */
	if (osize < length) {
		if (length > oip->i_e2fs->e2fs_maxfilesize)
			return (EFBIG);
		/* 
			tptfs 中的数据已经全部在虚拟内存当中了，这里应该不再需要缓存机制，这一步操作
			感觉是可以省略掉的
		*/
		vnode_pager_setsize(ovp, length);
		offset = blkoff(fs, length - 1);	/* 在最后一个数据块中的数据偏移量 */
		lbn = lblkno(fs, length - 1);	/* 逻辑块号 */
		flags |= BA_CLRBUF;
		/* 读取逻辑块对应的磁盘块中的数据，并存放 buf 当中 */
		error = ext2_balloc(oip, lbn, offset + 1, cred, &bp, flags);
		if (error) {
			vnode_pager_setsize(vp, osize);
			return (error);
		}
		oip->i_size = length;	/* 把文件长度设置成了length */
		if (bp->b_bufsize == fs->e2fs_bsize)
			bp->b_flags |= B_CLUSTEROK;
		if (flags & IO_SYNC)
			bwrite(bp);
		else if (DOINGASYNC(ovp))
			bdwrite(bp);
		else
			bawrite(bp);
		oip->i_flag |= IN_CHANGE | IN_UPDATE;
		return (ext2_update(ovp, !DOINGASYNC(ovp)));
	}
	/*
	 * Shorten the size of the file. If the file is not being
	 * truncated to a block boundary, the contents of the
	 * partial block following the end of the file must be
	 * zero'ed in case it ever become accessible again because
	 * of subsequent file growth.
	 * 缩短文件的大小。如果文件未被截断到块边界，则文件结尾后的部分块的内容必须为零，
	 * 以防由于后续文件增长而再次访问该文件
	 * 
	 * 这里其实就是分成了两种情况，第一就是当我们打算分割的位置比文件本身的实际长度还要长的时候，
	 * 就通过上面的代码分支进行操作。其中需要注意的一点是，它把表示文件长度的字段做了更新，相当
	 * 于是增加了文件的长度；当分割的位置比文件本身长度要小的时候，则执行下面的代码分支
	 */
	/* I don't understand the comment above */
	offset = blkoff(fs, length);
	if (offset == 0) {
		oip->i_size = length;	/* 当截取位置恰好为一个完整的数据块的边界值，i_size 直接赋值 */
	} else {
		lbn = lblkno(fs, length);	/* 计算 length 对应的逻辑块号 */
		flags |= BA_CLRBUF;	// 清空 buffer 中的无效区域
		error = ext2_balloc(oip, lbn, offset, cred, &bp, flags);	/* 读取逻辑块对应磁盘块中的数据 */
		if (error)
			return (error);
		oip->i_size = length;
		size = blksize(fs, oip, lbn);
		/*
			把数据块中 offset 之后的数据置零，这里只是读取了一个数据块中的内容
		*/
		bzero((char *)bp->b_data + offset, (u_int)(size - offset));
		allocbuf(bp, size);
		if (bp->b_bufsize == fs->e2fs_bsize)
			bp->b_flags |= B_CLUSTEROK;
		if (flags & IO_SYNC)
			bwrite(bp);
		else if (DOINGASYNC(ovp))
			bdwrite(bp);
		else
			bawrite(bp);
	}
	// 上面的代码貌似只是处理了 length 所在的那一个数据块
	/*
	 * Calculate index into inode's block list of
	 * last direct and indirect blocks (if any)
	 * which we want to keep.  Lastblock is -1 when
	 * the file is truncated to 0.
	 */
	/* 
		计算截断之后的文件的最后一个逻辑块的块号，也就表示截取后的文件一共包含有多少个数据块
	*/
	lastblock = lblkno(fs, length + fs->e2fs_bsize - 1) - 1;
	/* 
		减去直接块个数之后，剩余的数据块个数。可以用作判断被截取后文件大小的一个标志，当数组的第一个元素小于 0 的时候，
		就意味着变化后的文件用直接块就可以完全表示，不再需要间接索引，下面同理。
		NINDIR 宏表示一个数据块中能够存放多少个指针
	*/
	lastiblock[SINGLE] = lastblock - EXT2_NDADDR;	// 一级间接索引的起始逻辑块号
	lastiblock[DOUBLE] = lastiblock[SINGLE] - NINDIR(fs);	/* 二级间接索引的起始逻辑块号 */
	lastiblock[TRIPLE] = lastiblock[DOUBLE] - NINDIR(fs) * NINDIR(fs);	/* 三级间接索引的起始逻辑块号 */
	nblocks = btodb(fs->e2fs_bsize);	/* 文件系统块号与磁盘块号的转换，tptfs 中是 1 */
	/*
	 * Update file and block pointers on disk before we start freeing
	 * blocks.  If we crash before free'ing blocks below, the blocks
	 * will be returned to the free list.  lastiblock values are also
	 * normalized to -1 for calls to ext2_indirtrunc below.
	 * 首先将截取后的文件的直接索引和间接索引数据中的值更新到 inode 字段，要注意
	 * 此时还没有开始释放原有文件申请的、阶段之后不再使用的数据块
	 */
	for (level = TRIPLE; level >= SINGLE; level--) {
		/* 把间接索引信息更新到 oldblks 数组当中 */
		oldblks[EXT2_NDADDR + level] = oip->i_ib[level];
		if (lastiblock[level] < 0) {
			/*
				当判断 lastiblock 数组中的元素小于 0 时，表示文件不会再用到当前等级的数据块索引了，
				那就直接把 inode 对应的索引数组元素的值置0，对应的 lastiblock 数组置-1
			*/
			oip->i_ib[level] = 0;
			lastiblock[level] = -1;
		}
	}
	/* 把直接块索引的信息也更新到 oldblks 数组当中 */
	for (i = 0; i < EXT2_NDADDR; i++) {
		oldblks[i] = oip->i_db[i];

		if (i > lastblock)
			oip->i_db[i] = 0;
	}
	/* 
		到了这一步，inode 中数据块索引数组中的元素都已经更新完毕了。然后下面就执行 update 函数，把更新后的数据
		同步到磁盘当中。猜测后面应该就是对之前文件占用、但现在已经不再使用的数据块进行 free
	*/
	oip->i_flag |= IN_CHANGE | IN_UPDATE;	/* 更新 inode 状态 */
	allerror = ext2_update(ovp, !DOINGASYNC(ovp));

	/*
	 * Having written the new inode to disk, save its new configuration
	 * and put back the old block pointers long enough to process them.
	 * Note that we save the new block configuration so we can check it
	 * when we are done.
	 */
	/*
		inode 最原始的数据是在 oldblks 数组当中的，inode 中的数据已经完成了更新。
		然后再把更新后的 inode 数据放到 newblks 中，再用 oldblks 中的数据更新 inode
		中的数据。为什么不直接把更新后的数据放到 oldblks 中，而是有中转了一下？应该是
		为了能够方便的执行 ext2_update 函数。tptfs 也是可以用这种实现方式的
	*/
	for (i = 0; i < EXT2_NDADDR; i++) {
		newblks[i] = oip->i_db[i];
		oip->i_db[i] = oldblks[i];
	}
	for (i = 0; i < EXT2_NIADDR; i++) {
		newblks[EXT2_NDADDR + i] = oip->i_ib[i];
		oip->i_ib[i] = oldblks[EXT2_NDADDR + i];
	}	/* inode 在内存中的数据其实是已经还原成了初始数据 */

	/* 
		前面把 i_size 赋值 length，这里接着还原。inode 到这一步感觉都没怎么变化。个人理解是因为
		我们已经把更新后的数据写回磁盘了，现在就需要对原有的数据块进行清理，还是要用到 inode 结构体，
		所以这里才把它进行了还原，处理完之后应该就可以直接释放掉了。
		在 tptfs 中操作的时候应该也是要按照这个步骤，但是感觉需要构建新的 inode 对象，保存原有的数据。
		(tptfs 直接操作的就是虚拟页，在原对象上的操作会直接反映到虚拟页当中，更新后的数据会被更改掉)
		或者是将更新后的数据备份，等其他操作完毕之后再来更新 inode 的数据 
	*/
	oip->i_size = osize;	
	error = vtruncbuf(ovp, cred, length, (int)fs->e2fs_bsize);
	if (error && (allerror == 0))
		allerror = error;
	vnode_pager_setsize(ovp, length);

	/*
	 * Indirect blocks first.
	 * 这里为什么给了负数？ 虽然目前还不太清除具体的原因，但之前在 bmap 函数的实现中的确是出现过类似的情况，
	 * 那里会将负数转换成正值，然后再进行其他的计算。下面处理逻辑中会调用 ext2_indirtrunc 函数，这个函数
	 * 中会调用 getblk 函数，传入逻辑块号的是一个负数，所以再往下大概率是会调用 bmap 函数的，要不然这个负数
	 * 确实不知道要如何处理
	 */
	indir_lbn[SINGLE] = -EXT2_NDADDR;	/* 一级间接索引起始逻辑块号，取负 */
	indir_lbn[DOUBLE] = indir_lbn[SINGLE] - NINDIR(fs) - 1;	/* 二级间接索引起始逻辑块号，取负 */
	indir_lbn[TRIPLE] = indir_lbn[DOUBLE] - NINDIR(fs) * NINDIR(fs) - 1;	/* 同上 */

	/* 处理间接索引的时候就要跟申请操作的流程相反，首先处理三级间接索引 */
	for (level = TRIPLE; level >= SINGLE; level--) {
		bn = oip->i_ib[level];	/* 每个等级间接索引对应的块号，真实的物理块号(不是扇区号)，对应 tptfs 中的虚拟页号 */
		/* 当间接索引对应块号不为0的时候 */
		if (bn != 0) {
			error = ext2_indirtrunc(oip, indir_lbn[level],
			    fsbtodb(fs, bn), lastiblock[level], level, &count);
			if (error)
				allerror = error;
			blocksreleased += count;
			if (lastiblock[level] < 0) {
				oip->i_ib[level] = 0;
				ext2_blkfree(oip, bn, fs->e2fs_fsize);
				blocksreleased += nblocks;
			}
		}
		/* 
			间接索引块号为0，并且对应的 lastiblock 元素 >= 0，说明该文件已经用到了该等级，那直接块的处理其实就
			不用考虑了，直接跳转
		*/
		if (lastiblock[level] >= 0)
			goto done;
	}	// end for

	/*
	 * All whole direct blocks or frags.
	 * 当文件截取到用直接索引就可以表示的情况时，执行下面的代码分支。lastblock 之后的数组元素置0
	 */
	for (i = EXT2_NDADDR - 1; i > lastblock; i--) {
		long bsize;

		bn = oip->i_db[i];
		if (bn == 0)
			continue;
		oip->i_db[i] = 0;
		bsize = blksize(fs, oip, i);
		/* 释放掉对应 bn 的磁盘数据块 */
		ext2_blkfree(oip, bn, bsize);
		blocksreleased += btodb(bsize);
	}
	if (lastblock < 0)
		goto done;

	/*
	 * Finally, look for a change in size of the
	 * last direct block; release any frags.
	 */
	bn = oip->i_db[lastblock];
	if (bn != 0) {
		long oldspace, newspace;

		/*
		 * Calculate amount of space we're giving
		 * back as old block size minus new block size.
		 * 最后，查找最后一个直接块大小的变化；释放所有碎片
		 */
		oldspace = blksize(fs, oip, lastblock);
		oip->i_size = length;	/* i_size 重置为 length */
		newspace = blksize(fs, oip, lastblock);
		if (newspace == 0)
			panic("ext2_truncate: newspace");
		if (oldspace - newspace > 0) {
			/*
			 * Block number of space to be free'd is
			 * the old block # plus the number of frags
			 * required for the storage we're keeping.
			 */
			bn += numfrags(fs, newspace);
			ext2_blkfree(oip, bn, oldspace - newspace);
			blocksreleased += btodb(oldspace - newspace);
		}
	}
done:
#ifdef INVARIANTS
	for (level = SINGLE; level <= TRIPLE; level++)
		if (newblks[EXT2_NDADDR + level] != oip->i_ib[level])
			panic("itrunc1");
	for (i = 0; i < EXT2_NDADDR; i++)
		if (newblks[i] != oip->i_db[i])
			panic("itrunc2");
	BO_LOCK(bo);
	if (length == 0 && (bo->bo_dirty.bv_cnt != 0 ||
	    bo->bo_clean.bv_cnt != 0))
		panic("itrunc3");
	BO_UNLOCK(bo);
#endif	/* INVARIANTS */
	/*
	 * Put back the real size.
	 */
	oip->i_size = length;
	if (oip->i_blocks >= blocksreleased)
		oip->i_blocks -= blocksreleased;
	else				/* sanity */
		oip->i_blocks = 0;
	oip->i_flag |= IN_CHANGE;
	vnode_pager_setsize(ovp, length);
	return (allerror);
}

static int
ext2_ext_truncate(struct vnode *vp, off_t length, int flags,
    struct ucred *cred, struct thread *td)
{
	struct vnode *ovp = vp;
	int32_t lastblock;
	struct m_ext2fs *fs;
	struct inode *oip;
	struct buf *bp;
	uint32_t lbn, offset;
	int error, size;
	off_t osize;

	oip = VTOI(ovp);
	fs = oip->i_e2fs;
	osize = oip->i_size;

	if (osize < length) {
		if (length > oip->i_e2fs->e2fs_maxfilesize) {
			return (EFBIG);
		}
		vnode_pager_setsize(ovp, length);
		offset = blkoff(fs, length - 1);
		lbn = lblkno(fs, length - 1);
		flags |= BA_CLRBUF;
		error = ext2_balloc(oip, lbn, offset + 1, cred, &bp, flags);
		if (error) {
			vnode_pager_setsize(vp, osize);
			return (error);
		}
		oip->i_size = length;
		if (bp->b_bufsize == fs->e2fs_bsize)
			bp->b_flags |= B_CLUSTEROK;
		if (flags & IO_SYNC)
			bwrite(bp);
		else if (DOINGASYNC(ovp))
			bdwrite(bp);
		else
			bawrite(bp);
		oip->i_flag |= IN_CHANGE | IN_UPDATE;
		return (ext2_update(ovp, !DOINGASYNC(ovp)));
	}

	lastblock = (length + fs->e2fs_bsize - 1) / fs->e2fs_bsize;
	error = ext4_ext_remove_space(oip, lastblock, flags, cred, td);
	if (error)
		return (error);

	offset = blkoff(fs, length);
	if (offset == 0) {
		oip->i_size = length;
	} else {
		lbn = lblkno(fs, length);
		flags |= BA_CLRBUF;
		error = ext2_balloc(oip, lbn, offset, cred, &bp, flags);
		if (error) {
			return (error);
		}
		oip->i_size = length;
		size = blksize(fs, oip, lbn);
		bzero((char *)bp->b_data + offset, (u_int)(size - offset));
		allocbuf(bp, size);
		if (bp->b_bufsize == fs->e2fs_bsize)
			bp->b_flags |= B_CLUSTEROK;
		if (flags & IO_SYNC)
			bwrite(bp);
		else if (DOINGASYNC(ovp))
			bdwrite(bp);
		else
			bawrite(bp);
	}

	oip->i_size = osize;
	error = vtruncbuf(ovp, cred, length, (int)fs->e2fs_bsize);
	if (error)
		return (error);

	vnode_pager_setsize(ovp, length);

	oip->i_size = length;
	oip->i_flag |= IN_CHANGE | IN_UPDATE;
	error = ext2_update(ovp, !DOINGASYNC(ovp));

	return (error);
}

/*
 * Truncate(截断) the inode ip to at most length size, freeing the
 * disk blocks.
 * 应该是把文件在长度 length 之后的数据全部舍弃掉
 * 
 * 文件截断可以理解为删除那些我们不需要的数据块；
 * 所谓的截断文件指的是：一个特定长度的文件，我们从某个位置开始丢弃后面的数据，之前的数据依然保留。
 * 对具体文件系统来说，截断数据主要意味着两件事情：1. 文件大小发生变化；2. 文件被截断部分之前占用
 * 的数据块（对 ext2 和 minix 来说可能还包含间接块）释放，让其他文件可以使用。由于 ext2 和 minix 
 * 特殊的映射方式，我们不仅需要截断数据块，还可能包括映射这些数据块的间接索引块
 */
int
ext2_truncate(struct vnode *vp, off_t length, int flags, struct ucred *cred,
    struct thread *td)
{
	struct inode *ip;
	int error;

	ASSERT_VOP_LOCKED(vp, "ext2_truncate");
	/* 文件长度小于0，无效参数 */
	if (length < 0)
		return (EINVAL);
	/*
		如果 vnode 对应的是链接文件，则需要判断一下 symlink length 是否符合要求。链接文件存储的是
		目标文件的路径信息，存放的位置是在 inode 结构中 i_db 索引块所在的地方，所以对长度是有要求的。
		如果目标文件路径长度过长的话，可能就要分配新的数据块来存放
	*/
	ip = VTOI(vp);
	if (vp->v_type == VLNK &&
	    ip->i_size < vp->v_mount->mnt_maxsymlinklen) {
#ifdef INVARIANTS
		if (length != 0)
			panic("ext2_truncate: partial truncate of symlink");
#endif
		bzero((char *)&ip->i_shortlink, (u_int)ip->i_size);
		ip->i_size = 0;
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		return (ext2_update(vp, 1));
	}
	if (ip->i_size == length) {
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		return (ext2_update(vp, 0));
	}

	/* 通过 inode flag 判断我们所要截取的是文件真实的数据，还是它的属性信息 */
	if (ip->i_flag & IN_E4EXTENTS)
		error = ext2_ext_truncate(vp, length, flags, cred, td);
	else
		error = ext2_ind_truncate(vp, length, flags, cred, td);

	return (error);
}

/*
 *	discard preallocated blocks
 */
int
ext2_inactive(struct vop_inactive_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	struct thread *td = ap->a_td;
	int mode, error = 0;

	/*
	 * Ignore inodes related to stale file handles.
	 */
	if (ip->i_mode == 0)
		goto out;
	if (ip->i_nlink <= 0) {
		ext2_extattr_free(ip);
		error = ext2_truncate(vp, (off_t)0, 0, NOCRED, td);
		if (!(ip->i_flag & IN_E4EXTENTS))
			ip->i_rdev = 0;
		mode = ip->i_mode;
		ip->i_mode = 0;
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		ext2_vfree(vp, ip->i_number, mode);
	}
	if (ip->i_flag & (IN_ACCESS | IN_CHANGE | IN_MODIFIED | IN_UPDATE))
		ext2_update(vp, 0);
out:
	/*
	 * If we are done with the inode, reclaim it
	 * so that it can be reused immediately.
	 */
	if (ip->i_mode == 0)
		vrecycle(vp);
	return (error);
}

/*
 * Reclaim an inode so that it can be used for other purposes.
 */
int
ext2_reclaim(struct vop_reclaim_args *ap)
{
	struct inode *ip;
	struct vnode *vp = ap->a_vp;

	ip = VTOI(vp);
	if (ip->i_flag & IN_LAZYMOD) {
		ip->i_flag |= IN_MODIFIED;
		ext2_update(vp, 0);
	}
	vfs_hash_remove(vp);
	free(vp->v_data, M_EXT2NODE);
	vp->v_data = 0;
	vnode_destroy_vobject(vp);
	return (0);
}
