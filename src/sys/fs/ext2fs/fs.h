/*-
 *  modified for EXT2FS support in Lites 1.1
 *
 *  Aug 1995, Godmar Back (gback@cs.utah.edu)
 *  University of Utah, Department of Computer Science
 */
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1993
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
 *	@(#)fs.h	8.7 (Berkeley) 4/19/94
 * $FreeBSD: releng/12.0/sys/fs/ext2fs/fs.h 333584 2018-05-13 19:19:10Z fsu $
 */

#ifndef _FS_EXT2FS_FS_H_
#define	_FS_EXT2FS_FS_H_

/*
 * Each disk drive contains some number of file systems.
 * A file system consists of a number of cylinder groups.
 * Each cylinder group has inodes and data.
 * 每个磁盘驱动器都包含一些文件系统。文件系统由许多柱面组组成。每个柱面组都有索引节点和数据。
 *
 * A file system is described by its super-block, which in turn
 * describes the cylinder groups.  The super-block is critical
 * data and is replicated in each cylinder group to protect against
 * catastrophic loss.  This is done at `newfs' time and the critical
 * super-block data does not change, so the copies need not be
 * referenced further unless disaster strikes.
 * 文件系统由它的超级块来描述，超级块又描述了柱面组。超级块是关键数据，在每个柱面组中复制，以防止
 * 灾难性损失。这是在“newfs”时间完成的，并且关键超级块数据不会更改，因此除非发生灾难，否则不需要
 * 进一步引用副本
 *
 * The first boot and super blocks are given in absolute disk addresses.
 * The byte-offset forms are preferred, as they don't imply a sector size.
 * 第一个引导和超级块以绝对磁盘地址给出。字节偏移量形式是首选的，因为它们并不意味着扇区大小
 */
#define	SBSIZE		1024
#define	SBLOCK		2

/*
 * The path name on which the file system is mounted is maintained
 * in fs_fsmnt. MAXMNTLEN defines the amount of space allocated in
 * the super block for this name.
 */
#define	MAXMNTLEN	512

/*
 * A summary of contiguous blocks of various sizes is maintained
 * in each cylinder group. Normally this is set by the initial
 * value of fs_maxcontig.
 *
 * XXX:FS_MAXCONTIG is set to 16 to conserve space. Here we set
 * EXT2_MAXCONTIG to 32 for better performance.
 */
#define	EXT2_MAXCONTIG	32

/*
 * Grigoriy Orlov <gluk@ptci.ru> has done some extensive work to fine
 * tune the layout preferences for directories within a filesystem.
 * His algorithm can be tuned by adjusting the following parameters
 * which tell the system the average file size and the average number
 * of files per directory. These defaults are well selected for typical
 * filesystems, but may need to be tuned for odd cases like filesystems
 * being used for squid caches or news spools.
 * AVFPDIR is the expected number of files per directory. AVGDIRSIZE is
 * obtained by multiplying AVFPDIR and AVFILESIZ which is assumed to be
 * 16384.
 */

#define	AFPDIR		64
#define	AVGDIRSIZE	1048576

/*
 * Macros for access to superblock array structures
 */

/*
 * Turn file system block numbers into disk block addresses.
 * This maps file system blocks to device size blocks.
 * 将文件系统块号转换为磁盘块地址。这会将文件系统块映射到设备大小块
 * 所以推测一下，e2fs_fsbtodb 字段就是用来计算磁盘块地址的。这个字段会在 compute_sb_data 函数
 * 中被赋值为 e2fs_log_bsize + 1。而 e2fs_log_bsize 字段表示的是一个对数，参考 ext2fs 中的
 * 注释。
 * 磁盘的一个扇区通常是512字节，ext2 块大小通常是定义为1024字节，根据公式
 * 	block size = 1024*(2 ^ e2fs_log_bsize) 
 * 可以看出，e2fs_log_bsize (on-disk)的值应该是等于0的，那 e2fs_fsbtodb (in_memory)的值为1。
 * 参数 b 其实是我们传入的文件系统层面的块号，左移一位，相当于是块号扩大2倍，刚好对应的是磁盘的扇区号
 */
#define	fsbtodb(fs, b)	((daddr_t)(b) << (fs)->e2fs_fsbtodb)
#define	dbtofsb(fs, b)	((b) >> (fs)->e2fs_fsbtodb)

/* get group containing inode 
	e2fs_ipg 表示的是每个 group 中包含的 inode 的数量，x 表示的是 inode 编号，
	取模操作后，就可以算出来 inode 是在哪个 group 当中，相当于是获取到了 group 的 index
*/
#define	ino_to_cg(fs, x)	(((x) - 1) / (fs->e2fs_ipg))

/* get block containing inode from its number x - 从编号中获取包含 inode 的块
	ino_to_fsba: inode to filesystem block address ？？
	vfsops 文件中的一个应用场景中 x 传入的是 i_number，fs 传入的是 in-memory superblock

	宏的处理流程:
		- 通过 ino_to_cg 宏获取 inode 所在块组的索引
		- 在 e2fs_gd 中获取这个块组的组描述符
		- e2fs_gd_get_i_tables 获取每个块组中存放索引节点表的第一个块的块号
		- % 取余操作，e2fs_ipg 字段表示的是每个组中的 inode 个数，最终获取的是该
			inode 在这个块组中的偏移量
		- 再用偏移量 / 每个块中包含的 inode 数量，确定这个 inode 存放在哪个块
	所以，这个宏确定的是 inode 结构体本身所在的数据块，而不是 inode 管理的数据所在的数据块。

	这个宏实现的功能在奇海文件系统中实现起来就很简单，首先 superblock 一定要保存 inode 表的
	第一个块的块号(直接给出或者计算都可以)；由于文件系统总共只有一个 inode 表，所以直接用
		inode number / 每个 block 包含 inode 的个数，就可以直接计算出来 inode 所在块号；
	里面涉及到那个对数感觉不用改，直接拿来用就可以？
	奇海磁盘中会包含有多个备份，所以还需要确定我们需要修改的到底是哪一个，所以虚拟分页跟磁盘块
	不是一一对应的关系，这个还要进一步考虑
*/
#define	ino_to_fsba(fs, x)                                              \
        (e2fs_gd_get_i_tables(&(fs)->e2fs_gd[ino_to_cg((fs), (x))]) +   \
        (((x) - 1) % (fs)->e2fs->e2fs_ipg) / (fs)->e2fs_ipb)

/* get offset for inode in block */
#define	ino_to_fsbo(fs, x)	((x-1) % (fs->e2fs_ipb))

/*
 * Give cylinder group number for a file system block.
 * Give cylinder group block number for a file system block.
 */
#define	dtog(fs, d)	(((d) - fs->e2fs->e2fs_first_dblock) / \
			EXT2_BLOCKS_PER_GROUP(fs))
#define	dtogd(fs, d)	(((d) - fs->e2fs->e2fs_first_dblock) % \
			EXT2_BLOCKS_PER_GROUP(fs))

/*
 * The following macros optimize certain frequently calculated
 * quantities by using shifts and masks in place of divisions
 * modulos and multiplications.
 */
#define	blkoff(fs, loc)		/* calculates (loc % fs->fs_bsize) */ \
	((loc) & (fs)->e2fs_qbmask)

#define	lblktosize(fs, blk)	/* calculates (blk * fs->fs_bsize) */ \
	((blk) << (fs->e2fs_bshift))

/* 
	计算逻辑块号。这里的 loc 从实际的使用情况来看，像是数据在磁盘的中的字符偏移量，然后与数据块大小进行取模运算，
	得出该数据所在的逻辑块号(基于文件系统的逻辑块号，不是磁盘真正的块号) 
*/
#define	lblkno(fs, loc)		/* calculates (loc / fs->fs_bsize) */ \
	((loc) >> (fs->e2fs_bshift))

/* no fragments -> logical block number equal # of frags */
#define	numfrags(fs, loc)	/* calculates (loc / fs->fs_fsize) */ \
	((loc) >> (fs->e2fs_bshift))

#define	fragroundup(fs, size)	/* calculates roundup(size, fs->fs_fsize) */ \
	roundup(size, fs->e2fs_fsize)
	/* was (((size) + (fs)->fs_qfmask) & (fs)->fs_fmask) */

/*
 * Determining the size of a file block in the file system. 确定文件系统中文件块的大小
 * easy w/o fragments
 */
#define	blksize(fs, ip, lbn) ((fs)->e2fs_fsize)

/*
 * INOPB is the number of inodes in a secondary storage block.
 * INOPB是辅助存储块中的索引节点数
 */
#define	INOPB(fs)	(fs->e2fs_ipb)

/*
 * NINDIR is the number of indirects in a file system block.
 * NINDIR是文件系统块中的间接块数
 */
#define	NINDIR(fs)	(EXT2_ADDR_PER_BLOCK(fs))

/*
 * Use if additional debug logging is required.
 */
/* #define EXT2FS_DEBUG */

#endif	/* !_FS_EXT2FS_FS_H_ */
