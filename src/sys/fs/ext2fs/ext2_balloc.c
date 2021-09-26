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
 *	@(#)ffs_balloc.c	8.4 (Berkeley) 9/23/93
 * $FreeBSD: releng/12.0/sys/fs/ext2fs/ext2_balloc.c 327584 2018-01-05 10:04:01Z fsu $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mount.h>
#include <sys/vnode.h>

#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/inode.h>
#include <fs/ext2fs/ext2fs.h>
#include <fs/ext2fs/ext2_dinode.h>
#include <fs/ext2fs/ext2_extern.h>
#include <fs/ext2fs/ext2_mount.h>

static int
ext2_ext_balloc(struct inode *ip, uint32_t lbn, int size,
    struct ucred *cred, struct buf **bpp, int flags)
{
	struct m_ext2fs *fs;
	struct buf *bp = NULL;
	struct vnode *vp = ITOV(ip);
	daddr_t newblk;
	int osize, nsize, blks, error, allocated;

	fs = ip->i_e2fs;
	blks = howmany(size, fs->e2fs_bsize);

	error = ext4_ext_get_blocks(ip, lbn, blks, cred, NULL, &allocated, &newblk);
	if (error)
		return (error);

	if (allocated) {
		if (ip->i_size < (lbn + 1) * fs->e2fs_bsize)
			nsize = fragroundup(fs, size);
		else
			nsize = fs->e2fs_bsize;

		bp = getblk(vp, lbn, nsize, 0, 0, 0);
		if(!bp)
			return (EIO);

		bp->b_blkno = fsbtodb(fs, newblk);
		if (flags & BA_CLRBUF)
			vfs_bio_clrbuf(bp);
	} else {
		if (ip->i_size >= (lbn + 1) * fs->e2fs_bsize) {

			error = bread(vp, lbn, fs->e2fs_bsize, NOCRED, &bp);
			if (error) {
				brelse(bp);
				return (error);
			}
			bp->b_blkno = fsbtodb(fs, newblk);
			*bpp = bp;
			return (0);
		}

		/*
		 * Consider need to reallocate a fragment.
		 */
		osize = fragroundup(fs, blkoff(fs, ip->i_size));
		nsize = fragroundup(fs, size);
		if (nsize <= osize)
			error = bread(vp, lbn, osize, NOCRED, &bp);
		else
			error = bread(vp, lbn, fs->e2fs_bsize, NOCRED, &bp);
		if (error) {
			brelse(bp);
			return (error);
		}
		bp->b_blkno = fsbtodb(fs, newblk);
	}

	*bpp = bp;

	return (error);
}

/*
 * Balloc defines the structure of filesystem storage
 * by allocating the physical blocks on a device given
 * the inode and the logical block number in a file.
 * Balloc 通过在给定 inode 和文件中的逻辑块号的设备上分配物理块来定义文件系统存储的结构
 */
int
ext2_balloc(struct inode *ip, e2fs_lbn_t lbn, int size, struct ucred *cred,
    struct buf **bpp, int flags)
{
	struct m_ext2fs *fs;
	struct ext2mount *ump;
	struct buf *bp, *nbp;
	struct vnode *vp = ITOV(ip);
	struct indir indirs[EXT2_NIADDR + 2];
	e4fs_daddr_t nb, newb;
	e2fs_daddr_t *bap, pref;
	int osize, nsize, num, i, error;

	*bpp = NULL;	/* 用于存放得到的数据 */
	if (lbn < 0)
		return (EFBIG);
	fs = ip->i_e2fs;
	ump = ip->i_ump;

	/*
	 * check if this is a sequential block allocation.
	 * If so, increment next_alloc fields to allow ext2_blkpref
	 * to make a good guess
	 * 检查这是否是顺序块分配。如果是这样的话，增加 next_alloc 字段以允许
	 * ext2_blkpref 做出正确的猜测
	 * ext2 中会将下次可以被分配的块的块号在 inode 中做个记录，我们在执行块分配操作的时候可以对这个字段
	 * 进行检测，可以提高分配效率，tptfs 中也可以加上这个字段
	 */
	if (lbn == ip->i_next_alloc_block + 1) {
		ip->i_next_alloc_block++;
		ip->i_next_alloc_goal++;
	}

	/* 判断是否需要处理ext4扩展属性的数据 */
	if (ip->i_flag & IN_E4EXTENTS)
		return (ext2_ext_balloc(ip, lbn, size, cred, bpp, flags));

	/*
	 * The first EXT2_NDADDR blocks are direct blocks
	 * 如果这个块落在了直接块索引数组当中，那就可以直接获取磁盘块号
	 */
	if (lbn < EXT2_NDADDR) {
		nb = ip->i_db[lbn];	/* 通过逻辑块号索引得到物理块号 */
		/*
		 * no new block is to be allocated, and no need to expand
		 * the file
		 * 没有新的块需要申请，并且不需要扩展文件。此时nb不为0，说明在 ip->i_db[lbn] 中
		 * 已经存放了数据，也就是说对应的物理块中已经被填充进去了文件数据
		 */
		if (nb != 0 && ip->i_size >= (lbn + 1) * fs->e2fs_bsize) {
			error = bread(vp, lbn, fs->e2fs_bsize, NOCRED, &bp);
			if (error) {
				brelse(bp);
				return (error);
			}
			/* 
				buf 结构体中有一个字段专门用于表示其所对应的磁盘块号，所以 tptfs 中也可以增加一个
				对应字段用于存放虚拟页号
			*/
			bp->b_blkno = fsbtodb(fs, nb);
			*bpp = bp;
			return (0);
		}
		/*  
			下面是对要求的块号大于文件大小所对应的逻辑块号的时候，那也就是说需要重新分配数据块
		*/
		if (nb != 0) {
			/*
			 * Consider need to reallocate a fragment.
			 * 考虑重新分配片段的需要
			 */
			osize = fragroundup(fs, blkoff(fs, ip->i_size));
			nsize = fragroundup(fs, size);
			if (nsize <= osize) {
				error = bread(vp, lbn, osize, NOCRED, &bp);
				if (error) {
					brelse(bp);
					return (error);
				}
				bp->b_blkno = fsbtodb(fs, nb);
			} else {
				/*
				 * Godmar thinks: this shouldn't happen w/o
				 * fragments
				 */
				printf("nsize %d(%d) > osize %d(%d) nb %d\n",
				    (int)nsize, (int)size, (int)osize,
				    (int)ip->i_size, (int)nb);
				panic(
				    "ext2_balloc: Something is terribly wrong");
						/*
						* please note there haven't been any changes from here on -
						* FFS seems to work.
						*/
			}
		} else {
			/* 
				当直接索引数组中的元素为0的时候，说明我们需要申请的这个数据块当前还是没有被分配的，
				所以要调用 ext2_alloc 函数去分配一个我们所需要的数据块
			*/
			if (ip->i_size < (lbn + 1) * fs->e2fs_bsize)
				nsize = fragroundup(fs, size);
			else
				nsize = fs->e2fs_bsize;
			EXT2_LOCK(ump);
			error = ext2_alloc(ip, lbn,
			    ext2_blkpref(ip, lbn, (int)lbn, &ip->i_db[0], 0),
			    nsize, cred, &newb);
			if (error)
				return (error);
			/*
			 * If the newly allocated block exceeds 32-bit limit,
			 * we can not use it in file block maps.
			 */
			if (newb > UINT_MAX)
				return (EFBIG);
			/*
				通过上述过程的处理之后，我们已经得到了要分配给直接块索引的物理块号 newb，然后还有一个 buf 。
				猜测一下，该函数执行完成之后，后续应该会有一个数据同步到磁盘的步骤。应该会借助这里的这个 buf
				将数据暂存到 b_data 字段中，然后根据 b_blkno 执行磁盘写入操作
			*/
			bp = getblk(vp, lbn, nsize, 0, 0, 0);
			bp->b_blkno = fsbtodb(fs, newb);
			if (flags & BA_CLRBUF)
				vfs_bio_clrbuf(bp);
		}
		/* 填充 inode 字段 */
		ip->i_db[lbn] = dbtofsb(fs, bp->b_blkno);
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		*bpp = bp;
		return (0);
	}

	/*
	 * Determine the number of levels of indirection.
	 		上面的代码都是处理直接块寻址的情况，下面开始处理间接块寻址的情况
	 */
	pref = 0;
	/*
		ext2_getlbns 函数将间接寻址的信息放到 indirs 数组当中，num 表示的是查找等级
	*/
	if ((error = ext2_getlbns(vp, lbn, indirs, &num)) != 0)
		return (error);
#ifdef INVARIANTS
	if (num < 1)
		panic("ext2_balloc: ext2_getlbns returned indirect block");
#endif
	/*
	 * Fetch the first indirect block allocating if necessary.
	 */
	--num;
	/* 
		从这里可以看出，indir 结构体中的 in_off 字段表示的在间接查找数组的索引.
		nb = 0，说明当前 ip->i_ib[indirs[0].in_off] 中并没有存放块号，所以要先
		给它分配一个数据块
	*/
	nb = ip->i_ib[indirs[0].in_off];
	if (nb == 0) {
		EXT2_LOCK(ump);
		pref = ext2_blkpref(ip, lbn, indirs[0].in_off +
		    EXT2_NDIR_BLOCKS, &ip->i_db[0], 0);
		if ((error = ext2_alloc(ip, lbn, pref, fs->e2fs_bsize, cred,
		    &newb)))
			return (error);
		if (newb > UINT_MAX)
			return (EFBIG);
		nb = newb;
		bp = getblk(vp, indirs[1].in_lbn, fs->e2fs_bsize, 0, 0, 0);
		bp->b_blkno = fsbtodb(fs, newb);
		vfs_bio_clrbuf(bp);
		/*
		 * Write synchronously so that indirect blocks
		 * never point at garbage.
		 */
		if ((error = bwrite(bp)) != 0) {
			ext2_blkfree(ip, nb, fs->e2fs_bsize);
			return (error);
		}
		ip->i_ib[indirs[0].in_off] = newb;
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
	}
	/*
	 * Fetch through the indirect blocks, allocating as necessary.
	 		获取间接块，根据需要进行分配
	 */
	for (i = 1;;) {
		error = bread(vp,
		    indirs[i].in_lbn, (int)fs->e2fs_bsize, NOCRED, &bp);
		if (error) {
			brelse(bp);
			return (error);
		}
		bap = (e2fs_daddr_t *)bp->b_data;
		nb = bap[indirs[i].in_off];
		if (i == num)
			break;
		i += 1;
		if (nb != 0) {
			bqrelse(bp);
			continue;
		}
		EXT2_LOCK(ump);
		if (pref == 0)
			pref = ext2_blkpref(ip, lbn, indirs[i].in_off, bap,
			    bp->b_lblkno);
		error = ext2_alloc(ip, lbn, pref, (int)fs->e2fs_bsize, cred, &newb);
		if (error) {
			brelse(bp);
			return (error);
		}
		if (newb > UINT_MAX)
			return (EFBIG);
		nb = newb;
		nbp = getblk(vp, indirs[i].in_lbn, fs->e2fs_bsize, 0, 0, 0);
		nbp->b_blkno = fsbtodb(fs, nb);
		vfs_bio_clrbuf(nbp);
		/*
		 * Write synchronously so that indirect blocks
		 * never point at garbage.
		 */
		if ((error = bwrite(nbp)) != 0) {
			ext2_blkfree(ip, nb, fs->e2fs_bsize);
			EXT2_UNLOCK(ump);
			brelse(bp);
			return (error);
		}
		bap[indirs[i - 1].in_off] = nb;
		/*
		 * If required, write synchronously, otherwise use
		 * delayed write.
		 */
		if (flags & IO_SYNC) {
			bwrite(bp);
		} else {
			if (bp->b_bufsize == fs->e2fs_bsize)
				bp->b_flags |= B_CLUSTEROK;
			bdwrite(bp);
		}
	}	/* end for */
	/*
	 * Get the data block, allocating if necessary.
	 */
	if (nb == 0) {
		EXT2_LOCK(ump);
		pref = ext2_blkpref(ip, lbn, indirs[i].in_off, &bap[0],
		    bp->b_lblkno);
		if ((error = ext2_alloc(ip,
		    lbn, pref, (int)fs->e2fs_bsize, cred, &newb)) != 0) {
			brelse(bp);
			return (error);
		}
		if (newb > UINT_MAX)
			return (EFBIG);
		nb = newb;
		nbp = getblk(vp, lbn, fs->e2fs_bsize, 0, 0, 0);
		nbp->b_blkno = fsbtodb(fs, nb);
		if (flags & BA_CLRBUF)
			vfs_bio_clrbuf(nbp);
		bap[indirs[i].in_off] = nb;
		/*
		 * If required, write synchronously, otherwise use
		 * delayed write.
		 */
		if (flags & IO_SYNC) {
			bwrite(bp);
		} else {
			if (bp->b_bufsize == fs->e2fs_bsize)
				bp->b_flags |= B_CLUSTEROK;
			bdwrite(bp);
		}
		*bpp = nbp;
		return (0);
	}
	brelse(bp);
	if (flags & BA_CLRBUF) {
		int seqcount = (flags & BA_SEQMASK) >> BA_SEQSHIFT;

		if (seqcount && (vp->v_mount->mnt_flag & MNT_NOCLUSTERR) == 0) {
			error = cluster_read(vp, ip->i_size, lbn,
			    (int)fs->e2fs_bsize, NOCRED,
			    MAXBSIZE, seqcount, 0, &nbp);
		} else {
			error = bread(vp, lbn, (int)fs->e2fs_bsize, NOCRED, &nbp);
		}
		if (error) {
			brelse(nbp);
			return (error);
		}
	} else {
		nbp = getblk(vp, lbn, fs->e2fs_bsize, 0, 0, 0);
		nbp->b_blkno = fsbtodb(fs, nb);
	}
	*bpp = nbp;
	return (0);
}
