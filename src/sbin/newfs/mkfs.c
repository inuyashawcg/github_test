/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Marshall
 * Kirk McKusick and Network Associates Laboratories, the Security
 * Research Division of Network Associates, Inc. under DARPA/SPAWAR
 * contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA CHATS
 * research program.
 *
 * Copyright (c) 1980, 1989, 1993
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
 */

#if 0
#ifndef lint
static char sccsid[] = "@(#)mkfs.c	8.11 (Berkeley) 5/3/95";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD: releng/12.0/sbin/newfs/mkfs.c 331095 2018-03-17 12:59:55Z emaste $");

#define	IN_RTLD			/* So we pickup the P_OSREL defines */
#include <sys/param.h>
#include <sys/disklabel.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <err.h>
#include <grp.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ffs/fs.h>
#include "newfs.h"

/*
 * make file system for cylinder-group style file systems
 */
#define UMASK		0755
#define POWEROF2(num)	(((num) & ((num) - 1)) == 0)

static struct	csum *fscs;
#define	sblock	disk.d_fs
#define	acg	disk.d_cg

union dinode {
	struct ufs1_dinode dp1;
	struct ufs2_dinode dp2;
};
#define DIP(dp, field) \
	((sblock.fs_magic == FS_UFS1_MAGIC) ? \
	(dp)->dp1.field : (dp)->dp2.field)

static caddr_t iobuf;
static long iobufsize;
static ufs2_daddr_t alloc(int size, int mode);
static int charsperline(void);
static void clrblock(struct fs *, unsigned char *, int);
static void fsinit(time_t);
static int ilog2(int);
static void initcg(int, time_t);
static int isblock(struct fs *, unsigned char *, int);
static void iput(union dinode *, ino_t);
static int makedir(struct direct *, int);
static void setblock(struct fs *, unsigned char *, int);
static void wtfs(ufs2_daddr_t, int, char *);
static u_int32_t newfs_random(void);

/*
	这里我们需要明确 tptfs superblock 中的字段表示的意义以及如何获取(或计算)。在该函数中
	这些都是要体现出来的
*/
void
mkfs(struct partition *pp, char *fsys)
{
	/*
		fragsperinode - 每个 inode 所占用的 fragment 数量
		optimalfpg - 每个块组所包含的 fragment 数量
		origdensity - inode 数据密度
		lastminfpg - 最后一个块组所拥有的 fragment 最小数量？
	*/
	int fragsperinode, optimalfpg, origdensity, minfpg, lastminfpg;
	long i, j, csfrags;
	uint cg;	/* 块组号？ */
	time_t utime;
	quad_t sizepb;
	int width;
	ino_t maxinum;
	int minfragsperinode;	/* minimum ratio of frags to inodes 碎片与inodes的最小比率 */
	char tmpbuf[100];	/* XXX this will break in about 2,500 years */
	struct fsrecovery *fsr;
	char *fsrbuf;
	/* 超级块 */
	union {
		struct fs fdummy;
		char cdummy[SBLOCKSIZE];
	} dummy;
#define fsdummy dummy.fdummy
#define chdummy dummy.cdummy

	/*
	 * Our blocks == sector size, and the version of UFS we are using is
	 * specified by Oflag.
	 * Oflag 用于指定ufs版本信息
	 * 从下文的代码实现来看，函数操作的对象就是 newfs 中定义的全局量 disk。要么直接操作
	 * disk 中的成员，要么操作 disk->sblock
	 */
	disk.d_bsize = sectorsize;	/* 指定扇区大小，newfs -S 来获取 */
	disk.d_ufs = Oflag;	/* 指定ufs版本信息 */
	/* 回归测试标识 */
	if (Rflag)
		utime = 1000000000;
	else
		time(&utime);	/* 获取从boot开始到现在的时间 */

	/* disk->fs(超级块)属性设置 */
	sblock.fs_old_flags = FS_FLAGS_UPDATED;
	sblock.fs_flags = 0;	/* 设置超级块flags，其实就是说明文件系统支持哪些操作或者属性 */
	if (Uflag)
		sblock.fs_flags |= FS_DOSOFTDEP;
	if (Lflag)
		strlcpy(sblock.fs_volname, volumelabel, MAXVOLLEN);	/* 设置超级块volume name */
	if (Jflag)
		sblock.fs_flags |= FS_GJOURNAL;
	if (lflag)
		sblock.fs_flags |= FS_MULTILABEL;
	if (tflag)
		sblock.fs_flags |= FS_TRIM;
	/*
	 * Validate the given file system size.
	 * Verify that its last block can actually be accessed.
	 * Convert to file system fragment sized units.
	 * - 验证给定的文件系统大小
	 * - 验证其最后一个块是否可以实际访问。可能最后一个块在大多数情况下不会被用到，所以用它来进行测试；
	 *   即使测试失败，也不会对磁盘造成重大损坏
	 * - 转换为文件系统片段大小的单位
	 */
	if (fssize <= 0) {
		printf("preposterous(荒谬的，不合理的) size %jd\n", (intmax_t)fssize);
		exit(13);
	}
	wtfs(fssize - (realsectorsize / DEV_BSIZE), realsectorsize,
	    (char *)&sblock);	/* 对应注释中的将超级块数据写入最后一个数据块 */
	/*
	 * collect and verify the file system density info
	 * 收集并验证文件系统密度信息。下面这些字段信息在奇海操作系统中都是没有的。
	 * 这些步骤应该就可以省略
	 */
	sblock.fs_avgfilesize = avgfilesize;	/* 设置超级块平均文件长度， newfs -g */
	sblock.fs_avgfpdir = avgfilesperdir;	/* 设置每个目录下包含的文件数， newfs -h */
	/* 对上述两个获取的属性进行判断 */
	if (sblock.fs_avgfilesize <= 0)	
		printf("illegal expected average file size %d\n",
		    sblock.fs_avgfilesize), exit(14);
	if (sblock.fs_avgfpdir <= 0)
		printf("illegal expected number of files per directory %d\n",
		    sblock.fs_avgfpdir), exit(15);

restart:
	/*
	 * collect and verify the block and fragment sizes
	 * bsize 在 newfs.h 文件中有定义，其实是一个全局的变量，这里可以直接使用
	 */
	sblock.fs_bsize = bsize;	/* newfs -b */
	sblock.fs_fsize = fsize;	/* newfs -f */
	/* 检测 bsize 和 fsize 两者的大小是不是2的幂 */
	if (!POWEROF2(sblock.fs_bsize)) {
		printf("block size must be a power of 2, not %d\n",
		    sblock.fs_bsize);
		exit(16);
	}
	if (!POWEROF2(sblock.fs_fsize)) {
		printf("fragment size must be a power of 2, not %d\n",
		    sblock.fs_fsize);
		exit(17);
	}
	/* fsize 和 bsize 是否超过规定的范围大小 */
	if (sblock.fs_fsize < sectorsize) {
		printf("increasing fragment size from %d to sector size (%d)\n",
		    sblock.fs_fsize, sectorsize);
		sblock.fs_fsize = sectorsize;
	}
	if (sblock.fs_bsize > MAXBSIZE) {
		printf("decreasing block size from %d to maximum (%d)\n",
		    sblock.fs_bsize, MAXBSIZE);
		sblock.fs_bsize = MAXBSIZE;
	}
	if (sblock.fs_bsize < MINBSIZE) {
		printf("increasing block size from %d to minimum (%d)\n",
		    sblock.fs_bsize, MINBSIZE);
		sblock.fs_bsize = MINBSIZE;
	}
	if (sblock.fs_fsize > MAXBSIZE) {
		printf("decreasing fragment size from %d to maximum (%d)\n",
		    sblock.fs_fsize, MAXBSIZE);
		sblock.fs_fsize = MAXBSIZE;
	}
	if (sblock.fs_bsize < sblock.fs_fsize) {
		printf("increasing block size from %d to fragment size (%d)\n",
		    sblock.fs_bsize, sblock.fs_fsize);
		sblock.fs_bsize = sblock.fs_fsize;
	}
	if (sblock.fs_fsize * MAXFRAG < sblock.fs_bsize) {
		printf(
		"increasing fragment size from %d to block size / %d (%d)\n",
		    sblock.fs_fsize, MAXFRAG, sblock.fs_bsize / MAXFRAG);
		sblock.fs_fsize = sblock.fs_bsize / MAXFRAG;
	}
	/* 应该是跟块组相关的参数(参考 ext2)，奇海系统中省略掉 */
	if (maxbsize == 0)
		maxbsize = bsize;
	if (maxbsize < bsize || !POWEROF2(maxbsize)) {
		sblock.fs_maxbsize = sblock.fs_bsize;
		printf("Extent size set to %d\n", sblock.fs_maxbsize);
	} else if (sblock.fs_maxbsize > FS_MAXCONTIG * sblock.fs_bsize) {
		sblock.fs_maxbsize = FS_MAXCONTIG * sblock.fs_bsize;
		printf("Extent size reduced to %d\n", sblock.fs_maxbsize);
	} else {
		sblock.fs_maxbsize = maxbsize;	/* newfs -d */
	}
	/*
	 * Maxcontig sets the default for the maximum number of blocks
	 * that may be allocated sequentially. With file system clustering
	 * it is possible to allocate contiguous blocks up to the maximum
	 * transfer size permitted by the controller or buffering.
	 * Maxcontig 设置可按顺序分配的最大块数的默认值。使用文件系统群集，可以分配连续块，
	 * 最大可达控制器或缓冲区允许的最大传输大小
	 */
	if (maxcontig == 0)
		maxcontig = MAX(1, MAXPHYS / bsize);	/* newfs -a */
	sblock.fs_maxcontig = maxcontig;
	if (sblock.fs_maxcontig < sblock.fs_maxbsize / sblock.fs_bsize) {
		sblock.fs_maxcontig = sblock.fs_maxbsize / sblock.fs_bsize;
		printf("Maxcontig raised to %d\n", sblock.fs_maxbsize);
	}
	if (sblock.fs_maxcontig > 1)
		sblock.fs_contigsumsize = MIN(sblock.fs_maxcontig, FS_MAXCONTIG);
	sblock.fs_bmask = ~(sblock.fs_bsize - 1);
	sblock.fs_fmask = ~(sblock.fs_fsize - 1);
	sblock.fs_qbmask = ~sblock.fs_bmask;
	sblock.fs_qfmask = ~sblock.fs_fmask;
	sblock.fs_bshift = ilog2(sblock.fs_bsize);
	sblock.fs_fshift = ilog2(sblock.fs_fsize);
	sblock.fs_frag = numfrags(&sblock, sblock.fs_bsize);
	sblock.fs_fragshift = ilog2(sblock.fs_frag);
	if (sblock.fs_frag > MAXFRAG) {
		printf("fragment size %d is still too small (can't happen)\n",
		    sblock.fs_bsize / MAXFRAG);
		exit(21);
	}
	sblock.fs_fsbtodb = ilog2(sblock.fs_fsize / sectorsize);
	sblock.fs_size = fssize = dbtofsb(&sblock, fssize);
	sblock.fs_providersize = dbtofsb(&sblock, mediasize / sectorsize);

	/*
	 * Before the filesystem is finally initialized, mark it
	 * as incompletely initialized.
	 * 在文件系统最终初始化之前，请将其标记为未完全初始化
	 */
	sblock.fs_magic = FS_BAD_MAGIC;
	/* 
		UFS 目前是分成了两个版本，Oflag == 1 应该就是表示1版本，也就是老版本。从代码逻辑可以看出，
		1和2两个版本在超级块找那个都有一些特殊的字段，通过不同的分支做对应的初始化
	*/
	if (Oflag == 1) {
		sblock.fs_sblockloc = SBLOCK_UFS1;
		sblock.fs_sblockactualloc = SBLOCK_UFS1;
		sblock.fs_nindir = sblock.fs_bsize / sizeof(ufs1_daddr_t);
		sblock.fs_inopb = sblock.fs_bsize / sizeof(struct ufs1_dinode);
		sblock.fs_maxsymlinklen = ((UFS_NDADDR + UFS_NIADDR) *
		    sizeof(ufs1_daddr_t));
		sblock.fs_old_inodefmt = FS_44INODEFMT;
		sblock.fs_old_cgoffset = 0;
		sblock.fs_old_cgmask = 0xffffffff;
		sblock.fs_old_size = sblock.fs_size;
		sblock.fs_old_rotdelay = 0;
		sblock.fs_old_rps = 60;
		sblock.fs_old_nspf = sblock.fs_fsize / sectorsize;
		sblock.fs_old_cpg = 1;
		sblock.fs_old_interleave = 1;
		sblock.fs_old_trackskew = 0;
		sblock.fs_old_cpc = 0;
		sblock.fs_old_postblformat = 1;
		sblock.fs_old_nrpos = 1;
	} else {
		sblock.fs_sblockloc = SBLOCK_UFS2;	/* 设置超级块的位置，ufs中包含有多个超级块副本 */
		sblock.fs_sblockactualloc = SBLOCK_UFS2;
		sblock.fs_nindir = sblock.fs_bsize / sizeof(ufs2_daddr_t);	/* 间接块数 */
		sblock.fs_inopb = sblock.fs_bsize / sizeof(struct ufs2_dinode);	/* 一个块中包含几个索引节点 */
		sblock.fs_maxsymlinklen = ((UFS_NDADDR + UFS_NIADDR) *
		    sizeof(ufs2_daddr_t));	/* 计算 inode 最大块寻址 */
	}
	/*  
		fs_sblockloc 表示的是 ufs 文件系统用的是哪个位置的超级块(上面赋值的时候可以看到，该字段赋值
		的是 SBLOCK_UFS2，它表示的是超级块的偏移量，计算单位应该是 byte)。然后再调用 roundup 宏计算
		所在的块号
		howmany 宏看着像是把字节为单位的 offset 转换成 fragment/block 
	*/
	sblock.fs_sblkno =
	    roundup(howmany(sblock.fs_sblockloc + SBLOCKSIZE, sblock.fs_fsize),
		sblock.fs_frag);
	/* 块组描述符起始块号 = 超级块起始块号 + 一个块？ */
	sblock.fs_cblkno = sblock.fs_sblkno +
	    roundup(howmany(SBLOCKSIZE, sblock.fs_fsize), sblock.fs_frag);
	/* inode table 起始块号 = 快组描述符起始块号 + 一个块？ */
	sblock.fs_iblkno = sblock.fs_cblkno + sblock.fs_frag;

	/* 计算文件最大size，(直接块 + 间接块) * block size */
	sblock.fs_maxfilesize = sblock.fs_bsize * UFS_NDADDR - 1;
	for (sizepb = sblock.fs_bsize, i = 0; i < UFS_NIADDR; i++) {
		sizepb *= NINDIR(&sblock);
		sblock.fs_maxfilesize += sizepb;
	}

	/*
	 * It's impossible to create a snapshot in case that fs_maxfilesize
	 * is smaller than the fssize.
	 * 如果 fs_maxfilesize < fssize，那将不可能建立一个快照
	 */
	if (sblock.fs_maxfilesize < (u_quad_t)fssize) {
		warnx("WARNING: You will be unable to create snapshots on this "
		      "file system.  Correct by using a larger blocksize.");
	}

	/*
	 * Calculate the number of blocks to put into each cylinder group.
	 * 计算放入到每个块组中的 block 数量
	 *
	 * This algorithm selects the number of blocks per cylinder
	 * group. The first goal is to have at least enough data blocks
	 * in each cylinder group to meet the density requirement. Once
	 * this goal is achieved we try to expand to have at least
	 * MINCYLGRPS cylinder groups. Once this goal is achieved, we
	 * pack as many blocks into each cylinder group map as will fit.
	 * 这个算法挑选出每个块组中的块的数量。第一个目标是每个块组中包含有足够的数据块，满足
	 * 数据密度要求。一旦这个目标达成，我们将会尝试扩展到至少 MINCYLGRPS 个块组(这个
	 * 宏表示ufs文件系统最少需要多少个块组)。一旦这个目标也实现了，那我们将把尽可能多的
	 * 数据块打包放入每个块组映射当中
	 *
	 * We start by calculating the smallest number of blocks that we
	 * can put into each cylinder group. If this is too big, we reduce
	 * the density until it fits.
	 * 我们首先计算一下需要放入到每个块组中的数据块的数量。如果太大的话，我们将降低数据密度，
	 * 直到它符合我们的要求
	 */
	maxinum = (((int64_t)(1)) << 32) - INOPB(&sblock);
	minfragsperinode = 1 + fssize / maxinum;
	if (density == 0) {
		density = MAX(NFPI, minfragsperinode) * fsize;
	} else if (density < minfragsperinode * fsize) {
		origdensity = density;
		density = minfragsperinode * fsize;
		fprintf(stderr, "density increased from %d to %d\n",
		    origdensity, density);
	}
	origdensity = density;	/* 数据密度处理 */
	for (;;) {
		fragsperinode = MAX(numfrags(&sblock, density), 1);
		if (fragsperinode < minfragsperinode) {
			bsize <<= 1;
			fsize <<= 1;
			printf("Block size too small for a file system %s %d\n",
			     "of this size. Increasing blocksize to", bsize);
			goto restart;
		}
		minfpg = fragsperinode * INOPB(&sblock);
		if (minfpg > sblock.fs_size)
			minfpg = sblock.fs_size;
		sblock.fs_ipg = INOPB(&sblock);
		sblock.fs_fpg = roundup(sblock.fs_iblkno +
		    sblock.fs_ipg / INOPF(&sblock), sblock.fs_frag);
		if (sblock.fs_fpg < minfpg)
			sblock.fs_fpg = minfpg;
		sblock.fs_ipg = roundup(howmany(sblock.fs_fpg, fragsperinode),
		    INOPB(&sblock));
		sblock.fs_fpg = roundup(sblock.fs_iblkno +
		    sblock.fs_ipg / INOPF(&sblock), sblock.fs_frag);
		if (sblock.fs_fpg < minfpg)
			sblock.fs_fpg = minfpg;
		sblock.fs_ipg = roundup(howmany(sblock.fs_fpg, fragsperinode),
		    INOPB(&sblock));
		if (CGSIZE(&sblock) < (unsigned long)sblock.fs_bsize)
			break;
		density -= sblock.fs_fsize;
	}
	if (density != origdensity)
		printf("density reduced from %d to %d\n", origdensity, density);
	/* 到这里我们可以看到，已经将数据密度相关操作做完了，开始第二步骤 */
	/*
	 * Start packing more blocks into the cylinder group until
	 * it cannot grow any larger, the number of cylinder groups
	 * drops below MINCYLGRPS, or we reach the size requested.
	 * For UFS1 inodes per cylinder group are stored in an int16_t
	 * so fs_ipg is limited to 2^15 - 1.
	 * 开始将更多的数据块放入数组中，直到它不能继续增加为止，块组的数量降到 MINCYLGRPS 以下，
	 * 或者我们到达所要求的大小。对于 UFS1 文件系统，每个块组中包含的 inode 数量是存放到一个
	 * 16位整型变量当中，所以 fs_ipg 的值被限制到 2^15 - 1
	 */
	for ( ; sblock.fs_fpg < maxblkspercg; sblock.fs_fpg += sblock.fs_frag) {
		sblock.fs_ipg = roundup(howmany(sblock.fs_fpg, fragsperinode),
		    INOPB(&sblock));
		if (Oflag > 1 || (Oflag == 1 && sblock.fs_ipg <= 0x7fff)) {
			if (sblock.fs_size / sblock.fs_fpg < MINCYLGRPS)
				break;
			if (CGSIZE(&sblock) < (unsigned long)sblock.fs_bsize)
				continue;
			if (CGSIZE(&sblock) == (unsigned long)sblock.fs_bsize)
				break;
		}
		sblock.fs_fpg -= sblock.fs_frag;
		sblock.fs_ipg = roundup(howmany(sblock.fs_fpg, fragsperinode),
		    INOPB(&sblock));
		break;
	}
	/*
	 * Check to be sure that the last cylinder group has enough blocks
	 * to be viable. If it is too small, reduce the number of blocks
	 * per cylinder group which will have the effect of moving more
	 * blocks into the last cylinder group.
	 * 检查确保最后一个块组拥有足够可用的数据块。如果太小的话，减少每个块组中的数据块的数量，
	 * 使得文件系统可以将更多的块移动到最后一个块组当中。处理完之后，填充超级块对应的字段
	 */
	optimalfpg = sblock.fs_fpg;
	for (;;) {
		sblock.fs_ncg = howmany(sblock.fs_size, sblock.fs_fpg);
		lastminfpg = roundup(sblock.fs_iblkno +
		    sblock.fs_ipg / INOPF(&sblock), sblock.fs_frag);
		if (sblock.fs_size < lastminfpg) {
			printf("Filesystem size %jd < minimum size of %d\n",
			    (intmax_t)sblock.fs_size, lastminfpg);
			exit(28);
		}
		if (sblock.fs_size % sblock.fs_fpg >= lastminfpg ||
		    sblock.fs_size % sblock.fs_fpg == 0)
			break;
		sblock.fs_fpg -= sblock.fs_frag;
		sblock.fs_ipg = roundup(howmany(sblock.fs_fpg, fragsperinode),
		    INOPB(&sblock));
	}
	if (optimalfpg != sblock.fs_fpg)
		printf("Reduced frags per cylinder group from %d to %d %s\n",
		   optimalfpg, sblock.fs_fpg, "to enlarge last cyl group");
	/*
		cgsize 的计算要看一下 CGSIZE 的定义，它里面包含了许多 metadata 数据。alloc 函数中会调用 bread 函数，
		读取 cgsize 大小的数据到内存缓冲区，也就是说明一次 alloc 操作就会读取一个块组所有的元数据。这样就可以对
		元数据进行读写操作
	*/
	sblock.fs_cgsize = fragroundup(&sblock, CGSIZE(&sblock));
	/*
		fs_iblkno 表示的是 inode table 的偏移量。fs_ipg 表示的是每个组拥有的 inode 数量，INOPF 用于计算
		每个 block / fragment 中包含的 inode 数量，两者取模就可以得出 inode 一共会占用多少个 block / fragment,
		所以，fs_dblkno 表示的和可能是用于文件存储的数据块的起始位置，根据下文判断也可能是 csum 数据段的起始位置？
	*/
	sblock.fs_dblkno = sblock.fs_iblkno + sblock.fs_ipg / INOPF(&sblock);
	if (Oflag == 1) {
		sblock.fs_old_spc = sblock.fs_fpg * sblock.fs_old_nspf;
		sblock.fs_old_nsect = sblock.fs_old_spc;
		sblock.fs_old_npsect = sblock.fs_old_spc;
		sblock.fs_old_ncyl = sblock.fs_ncg;
	}
	/* 到了这里，关于块组相关字段的处理已经完成，下面是对剩余字段的赋值。所以上面几段代码可以省略 */
	/*
	 * fill in remaining fields of the super block
	 * 填写超级块的剩余字段。其中 csaddr 和 cssize 都是块组相关的块属性的一些定义。然后涉及到了一个
	 * csum 结构，说明这个结构是表示块组的 checksum。感觉 ufs 中可能包含有 csum 的一个单独数据段，
	 * 通过 cgdmin 算出 csum 的起始位置
	 */
	sblock.fs_csaddr = cgdmin(&sblock, 0);	
	sblock.fs_cssize =
	    fragroundup(&sblock, sblock.fs_ncg * sizeof(struct csum));	/* csum 的大小 */
	fscs = (struct csum *)calloc(1, sblock.fs_cssize);
	if (fscs == NULL)
		errx(31, "calloc failed");

	/* 
		fs_sbsize 表示的是文件系统超级块的实际大小，应该是真正占用的字节数，而不是通过块计算出的。 
	*/
	sblock.fs_sbsize = fragroundup(&sblock, sizeof(struct fs));
	if (sblock.fs_sbsize > SBLOCKSIZE)
		sblock.fs_sbsize = SBLOCKSIZE;
	if (sblock.fs_sbsize < realsectorsize)
		sblock.fs_sbsize = realsectorsize;
	/* 文件系统所能接受的最小空闲块所占比重，tptfs 中可能不需要这个 */
	sblock.fs_minfree = minfree;	
	if (metaspace > 0 && metaspace < sblock.fs_fpg / 2)
		sblock.fs_metaspace = blknum(&sblock, metaspace);	/* 块组相关 */
	else if (metaspace != -1)
		/* reserve half of minfree for metadata blocks */
		sblock.fs_metaspace = blknum(&sblock,
		    (sblock.fs_fpg * minfree) / 200);
	if (maxbpg == 0)	/* 块组中单个文件所能分配的做大块数 */
		sblock.fs_maxbpg = MAXBLKPG(sblock.fs_bsize);
	else
		sblock.fs_maxbpg = maxbpg;
	sblock.fs_optim = opt;
	sblock.fs_cgrotor = 0;
	sblock.fs_pendingblocks = 0;
	sblock.fs_pendinginodes = 0;
	sblock.fs_fmod = 0;
	sblock.fs_ronly = 0;
	sblock.fs_state = 0;
	sblock.fs_clean = 1;
	sblock.fs_id[0] = (long)utime;
	sblock.fs_id[1] = newfs_random();
	sblock.fs_fsmnt[0] = '\0';
	/* 应该是计算 csum 所占用的数据块数量 */
	csfrags = howmany(sblock.fs_cssize, sblock.fs_fsize);
	/*
		fs_dsize - 文件系统 data block 的数量，可能表示用于数据存储的数据块
		fs_size - 文件系统 block 数量
		fs_sblkno - superblock 占用的块数
		fs_ncg - 表示文件系统包含多少个块组
		fs_dblkno - 推测表示的是用于文件数据存储的块区域的起始地址
	*/
	sblock.fs_dsize = sblock.fs_size - sblock.fs_sblkno -
	    sblock.fs_ncg * (sblock.fs_dblkno - sblock.fs_sblkno);

	/* 计算空闲块的数量 */
	sblock.fs_cstotal.cs_nbfree =
	    fragstoblks(&sblock, sblock.fs_dsize) -
	    howmany(csfrags, sblock.fs_frag);

	/* 计算空闲片的数量 */
	sblock.fs_cstotal.cs_nffree =
	    fragnum(&sblock, sblock.fs_size) +
	    (fragnum(&sblock, csfrags) > 0 ?
	     sblock.fs_frag - fragnum(&sblock, csfrags) : 0);

	/* 计算空闲 inode 数量，方法就是所有的计算所有inode数量，然后减去root inode number */
	sblock.fs_cstotal.cs_nifree =
	    sblock.fs_ncg * sblock.fs_ipg - UFS_ROOTINO;

	sblock.fs_cstotal.cs_ndir = 0;	/* 文件系统包含的目录项置零 */
	sblock.fs_dsize -= csfrags;
	sblock.fs_time = utime;

	if (Oflag == 1) {
		sblock.fs_old_time = utime;
		sblock.fs_old_dsize = sblock.fs_dsize;
		sblock.fs_old_csaddr = sblock.fs_csaddr;
		sblock.fs_old_cstotal.cs_ndir = sblock.fs_cstotal.cs_ndir;
		sblock.fs_old_cstotal.cs_nbfree = sblock.fs_cstotal.cs_nbfree;
		sblock.fs_old_cstotal.cs_nifree = sblock.fs_cstotal.cs_nifree;
		sblock.fs_old_cstotal.cs_nffree = sblock.fs_cstotal.cs_nffree;
	}
	/*
	 * Set flags for metadata that is being check-hashed.
	 *
	 * Metadata check hashes are not supported in the UFS version 1
	 * filesystem to keep it as small and simple as possible.
	 */
	if (Oflag > 1) {
		sblock.fs_flags |= FS_METACKHASH;
		if (getosreldate() >= P_OSREL_CK_CYLGRP)
			sblock.fs_metackhash = CK_CYLGRP;
	}

	/*
	 * Dump out summary information about file system.
	 */
#	define B2MBFACTOR (1 / (1024.0 * 1024.0))
	printf("%s: %.1fMB (%jd sectors) block size %d, fragment size %d\n",
	    fsys, (float)sblock.fs_size * sblock.fs_fsize * B2MBFACTOR,
	    (intmax_t)fsbtodb(&sblock, sblock.fs_size), sblock.fs_bsize,
	    sblock.fs_fsize);
	printf("\tusing %d cylinder groups of %.2fMB, %d blks, %d inodes.\n",
	    sblock.fs_ncg, (float)sblock.fs_fpg * sblock.fs_fsize * B2MBFACTOR,
	    sblock.fs_fpg / sblock.fs_frag, sblock.fs_ipg);
	if (sblock.fs_flags & FS_DOSOFTDEP)
		printf("\twith soft updates\n");
#	undef B2MBFACTOR

	if (Eflag && !Nflag) {
		printf("Erasing sectors [%jd...%jd]\n", 
		    sblock.fs_sblockloc / disk.d_bsize,
		    fsbtodb(&sblock, sblock.fs_size) - 1);
		berase(&disk, sblock.fs_sblockloc / disk.d_bsize,
		    sblock.fs_size * sblock.fs_fsize - sblock.fs_sblockloc);
	}
	/*
	 * Wipe out old UFS1 superblock(s) if necessary.
	 * 如有必要，清除旧的UFS1超级块。之前也提到过，ufs 中包含有多个超级块的副本，每个副本都是
	 * 在不同的位置，而且代表的意义也是不一样的。有的是表示 UFS2 类型，有的表示 UFS1 类型。
	 * 所以才初始化磁盘的时候，如果有必要可以将 UFS1 类型的超级块数据给清除掉
	 */
	if (!Nflag && Oflag != 1 && realsectorsize <= SBLOCK_UFS1) {
		i = bread(&disk, part_ofs + SBLOCK_UFS1 / disk.d_bsize, chdummy,
		    SBLOCKSIZE);
		if (i == -1)
			err(1, "can't read old UFS1 superblock: %s",
			    disk.d_error);

		if (fsdummy.fs_magic == FS_UFS1_MAGIC) {
			fsdummy.fs_magic = 0;
			bwrite(&disk, part_ofs + SBLOCK_UFS1 / disk.d_bsize,
			    chdummy, SBLOCKSIZE);
			for (cg = 0; cg < fsdummy.fs_ncg; cg++) {
				if (fsbtodb(&fsdummy, cgsblock(&fsdummy, cg)) >
				    fssize)
					break;
				bwrite(&disk, part_ofs + fsbtodb(&fsdummy,
				  cgsblock(&fsdummy, cg)), chdummy, SBLOCKSIZE);
			}
		}
	}
	/* 将超级块数据写入到指定位置，不写入副本 */
	if (!Nflag && sbput(disk.d_fd, &disk.d_fs, 0) != 0)
		err(1, "sbput: %s", disk.d_error);
	if (Xflag == 1) {
		printf("** Exiting on Xflag 1\n");
		exit(0);
	}
	if (Xflag == 2)
		printf("** Leaving BAD MAGIC on Xflag 2\n");
	else
		sblock.fs_magic = (Oflag != 1) ? FS_UFS2_MAGIC : FS_UFS1_MAGIC;

	/*
	 * Now build the cylinders group blocks and
	 * then print out indices of cylinder groups.
	 */
	printf("super-block backups (for fsck_ffs -b #) at:\n");
	i = 0;
	width = charsperline();
	/*
	 * Allocate space for two sets of inode blocks.
	 * 为两组inode块分配空间，推测应该是为根目录创建的
	 */
	iobufsize = 2 * sblock.fs_bsize;
	if ((iobuf = calloc(1, iobufsize)) == 0) {
		printf("Cannot allocate I/O buffer\n");
		exit(38);
	}
	/*
	 * Write out all the cylinder groups and backup superblocks.
	 * 遍历所有的块组并进行初始化操作
	 */
	for (cg = 0; cg < sblock.fs_ncg; cg++) {
		if (!Nflag)
			initcg(cg, utime);
		j = snprintf(tmpbuf, sizeof(tmpbuf), " %jd%s",
		    (intmax_t)fsbtodb(&sblock, cgsblock(&sblock, cg)),
		    cg < (sblock.fs_ncg-1) ? "," : "");
		if (j < 0)
			tmpbuf[j = 0] = '\0';
		if (i + j >= width) {
			printf("\n");
			i = 0;
		}
		i += j;
		printf("%s", tmpbuf);
		fflush(stdout);
	}
	printf("\n");
	if (Nflag)
		exit(0);
	/* 上面已经将大部分要初始化的 superblock 字段填充完成，下面开始对文件系统和磁盘进行操作 */
	/*
	 * Now construct the initial file system,
	 * then write out the super-block.
	 * 现在开始构建初始化文件系统，然后写出超级块
	 */
	fsinit(utime);
	if (Oflag == 1) {
		sblock.fs_old_cstotal.cs_ndir = sblock.fs_cstotal.cs_ndir;
		sblock.fs_old_cstotal.cs_nbfree = sblock.fs_cstotal.cs_nbfree;
		sblock.fs_old_cstotal.cs_nifree = sblock.fs_cstotal.cs_nifree;
		sblock.fs_old_cstotal.cs_nffree = sblock.fs_cstotal.cs_nffree;
	}
	if (Xflag == 3) {
		printf("** Exiting on Xflag 3\n");
		exit(0);
	}
	/*
	 * Reference the summary information so it will also be written.
	 */
	sblock.fs_csp = fscs;
	if (sbput(disk.d_fd, &disk.d_fs, 0) != 0)
		err(1, "sbput: %s", disk.d_error);
	/*
	 * For UFS1 filesystems with a blocksize of 64K, the first
	 * alternate superblock resides at the location used for
	 * the default UFS2 superblock. As there is a valid
	 * superblock at this location, the boot code will use
	 * it as its first choice. Thus we have to ensure that
	 * all of its statistcs on usage are correct.
	 */
	if (Oflag == 1 && sblock.fs_bsize == 65536)
		wtfs(fsbtodb(&sblock, cgsblock(&sblock, 0)),
		    sblock.fs_bsize, (char *)&sblock);
	/*
	 * Read the last sector of the boot block, replace the last
	 * 20 bytes with the recovery information, then write it back.
	 * The recovery information only works for UFS2 filesystems.
	 */
	if (sblock.fs_magic == FS_UFS2_MAGIC) {
		if ((fsrbuf = malloc(realsectorsize)) == NULL || bread(&disk,
		    part_ofs + (SBLOCK_UFS2 - realsectorsize) / disk.d_bsize,
		    fsrbuf, realsectorsize) == -1)
			err(1, "can't read recovery area: %s", disk.d_error);
		fsr =
		    (struct fsrecovery *)&fsrbuf[realsectorsize - sizeof *fsr];
		fsr->fsr_magic = sblock.fs_magic;
		fsr->fsr_fpg = sblock.fs_fpg;
		fsr->fsr_fsbtodb = sblock.fs_fsbtodb;
		fsr->fsr_sblkno = sblock.fs_sblkno;
		fsr->fsr_ncg = sblock.fs_ncg;
		wtfs((SBLOCK_UFS2 - realsectorsize) / disk.d_bsize,
		    realsectorsize, fsrbuf);
		free(fsrbuf);
	}
	/*
	 * Update information about this partition in pack
	 * label, to that it may be updated on disk.
	 */
	if (pp != NULL) {
		pp->p_fstype = FS_BSDFFS;
		pp->p_fsize = sblock.fs_fsize;
		pp->p_frag = sblock.fs_frag;
		pp->p_cpg = sblock.fs_fpg;
	}
}

/*
 * Initialize a cylinder group. 初始化柱面组
 */
void
initcg(int cylno, time_t utime)
{
	long blkno, start;
	off_t savedactualloc;
	uint i, j, d, dlower, dupper;
	ufs2_daddr_t cbase, dmax;
	struct ufs1_dinode *dp1;
	struct ufs2_dinode *dp2;
	struct csum *cs;

	/*
	 * Determine block bounds for cylinder group.
	 * Allow space for super block summary information in first
	 * cylinder group.
	 * 确定柱面组的块边界；为在第一个柱面组中的超级块汇总信息申请空间
	 */
	cbase = cgbase(&sblock, cylno);	/* 计算出块组的起始位置块号 */
	dmax = cbase + sblock.fs_fpg;	/* 计算块组终止位置 fragment number，从而限定整个块组的范围 */
	if (dmax > sblock.fs_size)
		dmax = sblock.fs_size;
	dlower = cgsblock(&sblock, cylno) - cbase;	/* 计算块组中的超级块起始位置(块号) */
	dupper = cgdmin(&sblock, cylno) - cbase;	/* 计算 data block 首地址，表示的应该是元数据区 */
	/*
		当我们处理的是块组0的时候，里边可能比其他块组多包含了一个关于组描述符的区域，保存的应该是所有的
		块组的组描述符，应该是要占用几个块。fs_cssize 表示的应该是所有组描述符所占用的数据量大小(in bytes),
		fs_fsize 表示的是 fragment size，所以这里可以计算出组描述符一共占用多少 fragment。
		计算完成后更新dupper。也可以印证前面的猜测，初始化块组主要操作的就是块组的元数据，感觉应该是要包含 
		inode table 数据区
	*/
	if (cylno == 0)
		dupper += howmany(sblock.fs_cssize, sblock.fs_fsize);
	cs = &fscs[cylno];	/* 获取csum，可以看出该数组保存的是所有块组的信息 */
	memset(&acg, 0, sblock.fs_cgsize);	/* 块组字段置空 */

	acg.cg_time = utime;	/* 更新最后一次被写入的时间 */
	acg.cg_magic = CG_MAGIC;	/* 块组魔数 */
	acg.cg_cgx = cylno;	/* 块组号 */
	acg.cg_niblk = sblock.fs_ipg;	/* 每个块组中拥有的 inode 数量 */
	acg.cg_initediblk = MIN(sblock.fs_ipg, 2 * INOPB(&sblock));	/* 最后一个被初始化的 inode */
	acg.cg_ndblk = dmax - cbase;	/* 计算整个块组一共有多少个数据块 */

	if (sblock.fs_contigsumsize > 0)
		acg.cg_nclusterblks = acg.cg_ndblk / sblock.fs_frag;	/* 簇大小 */
	/*  start 表示的应该是块组描述符 */
	start = &acg.cg_space[0] - (u_char *)(&acg.cg_firstfield);
	if (Oflag == 2) {
		/* 表示的应该是 inode map table 所占用的数据块的起始块号，字节数？ */
		acg.cg_iusedoff = start;
	} else {
		acg.cg_old_ncyl = sblock.fs_old_cpg;	
		acg.cg_old_time = acg.cg_time;
		acg.cg_time = 0;
		acg.cg_old_niblk = acg.cg_niblk;
		acg.cg_niblk = 0;
		acg.cg_initediblk = 0;
		acg.cg_old_btotoff = start;
		acg.cg_old_boff = acg.cg_old_btotoff +
		    sblock.fs_old_cpg * sizeof(int32_t);
		acg.cg_iusedoff = acg.cg_old_boff +
		    sblock.fs_old_cpg * sizeof(u_int16_t);
	}
	/*
		块组拥有的inode总数 / CHAR_BIT = 所有 inode 用 bit 表示，一共会占用多少个字节
		cg_nextfreeoff 推测表示的应该是数据块位图所占用的字节数。因为这里处理的是位图，
		对某个 bit 进行处理的时候，是按照8位来进行查找和处理的
	*/
	acg.cg_freeoff = acg.cg_iusedoff + howmany(sblock.fs_ipg, CHAR_BIT);
	acg.cg_nextfreeoff = acg.cg_freeoff + howmany(sblock.fs_fpg, CHAR_BIT);

	if (sblock.fs_contigsumsize > 0) {
		acg.cg_clustersumoff =
		    roundup(acg.cg_nextfreeoff, sizeof(u_int32_t));

		acg.cg_clustersumoff -= sizeof(u_int32_t);

		acg.cg_clusteroff = acg.cg_clustersumoff +
		    (sblock.fs_contigsumsize + 1) * sizeof(u_int32_t);
				
		acg.cg_nextfreeoff = acg.cg_clusteroff +
		    howmany(fragstoblks(&sblock, sblock.fs_fpg), CHAR_BIT);
	}
	if (acg.cg_nextfreeoff > (unsigned)sblock.fs_cgsize) {
		printf("Panic: cylinder group too big\n");
		exit(37);
	}
	/*
		这里要注意，csum 是有两种类型，一种是提供给块组使用的，存储的是块组中的信息；
		另外一种是提供给超级块使用，它所管理的是整个文件系统的信息(csum_total)
	*/
	acg.cg_cs.cs_nifree += sblock.fs_ipg;	/* 初始化可用inode节点数为块组包含的所有inode */
	/* 
		通过 cg_inosused 宏找到inode位图的起始块号，然后将对应的i位设置为1，表明 inode 已经被使用。
		最后再将块组 csum 中的空闲节点个数字段做--操作。
		从判断条件可以看出，块组0中存放 root inode
	*/
	if (cylno == 0)
		for (i = 0; i < (long)UFS_ROOTINO; i++) {
			setbit(cg_inosused(&acg), i);
			acg.cg_cs.cs_nifree--;
		}
	if (cylno > 0) {
		/*
		 * In cylno 0, beginning space is reserved
		 * for boot and super blocks.
		 * 块组0开头的空间留给 boot 和 superblock。ext2 文件系统中引导块是单独列出的
		 * 从这里可以看出，d 表示的是 fragment 号，每次循环递增 fs_frag
		 */
		for (d = 0; d < dlower; d += sblock.fs_frag) {
			blkno = d / sblock.fs_frag;	/* 把 fragment number 转换成 block number */
			setblock(&sblock, cg_blksfree(&acg), blkno);
			if (sblock.fs_contigsumsize > 0)
				setbit(cg_clustersfree(&acg), blkno);
			acg.cg_cs.cs_nbfree++;
		}
	}
	if ((i = dupper % sblock.fs_frag)) {
		acg.cg_frsum[sblock.fs_frag - i]++;
		for (d = dupper + sblock.fs_frag - i; dupper < d; dupper++) {
			setbit(cg_blksfree(&acg), dupper);
			acg.cg_cs.cs_nffree++;	/* free fragment 计数++ */
		}
	}
	for (d = dupper; d + sblock.fs_frag <= acg.cg_ndblk;
	     d += sblock.fs_frag) {
		blkno = d / sblock.fs_frag;
		setblock(&sblock, cg_blksfree(&acg), blkno);
		if (sblock.fs_contigsumsize > 0)
			setbit(cg_clustersfree(&acg), blkno);
		acg.cg_cs.cs_nbfree++;
	}
	if (d < acg.cg_ndblk) {
		acg.cg_frsum[acg.cg_ndblk - d]++;
		for (; d < acg.cg_ndblk; d++) {
			setbit(cg_blksfree(&acg), d);
			acg.cg_cs.cs_nffree++;
		}
	}
	if (sblock.fs_contigsumsize > 0) {
		int32_t *sump = cg_clustersum(&acg);
		u_char *mapp = cg_clustersfree(&acg);
		int map = *mapp++;
		int bit = 1;
		int run = 0;

		for (i = 0; i < acg.cg_nclusterblks; i++) {
			if ((map & bit) != 0)
				run++;
			else if (run != 0) {
				if (run > sblock.fs_contigsumsize)
					run = sblock.fs_contigsumsize;
				sump[run]++;
				run = 0;
			}
			if ((i & (CHAR_BIT - 1)) != CHAR_BIT - 1)
				bit <<= 1;
			else {
				map = *mapp++;
				bit = 1;
			}
		}
		if (run != 0) {
			if (run > sblock.fs_contigsumsize)
				run = sblock.fs_contigsumsize;
			sump[run]++;
		}
	}
	*cs = acg.cg_cs;
	/*
	 * Write out the duplicate super block. Then write the cylinder
	 * group map and two blocks worth of inodes in a single write.
	 * 写出重复的超级块。然后在一次写入中写入柱面组映射和两个索引节点块
	 */
	savedactualloc = sblock.fs_sblockactualloc;
	sblock.fs_sblockactualloc =
	    dbtob(fsbtodb(&sblock, cgsblock(&sblock, cylno)));
	if (sbput(disk.d_fd, &disk.d_fs, 0) != 0)
		err(1, "sbput: %s", disk.d_error);
	sblock.fs_sblockactualloc = savedactualloc;
	if (cgput(&disk, &acg) != 0)
		err(1, "initcg: cgput: %s", disk.d_error);
	start = 0;
	dp1 = (struct ufs1_dinode *)(&iobuf[start]);
	dp2 = (struct ufs2_dinode *)(&iobuf[start]);
	for (i = 0; i < acg.cg_initediblk; i++) {
		if (sblock.fs_magic == FS_UFS1_MAGIC) {
			dp1->di_gen = newfs_random();
			dp1++;
		} else {
			dp2->di_gen = newfs_random();
			dp2++;
		}
	}
	wtfs(fsbtodb(&sblock, cgimin(&sblock, cylno)), iobufsize, iobuf);
	/*
	 * For the old file system, we have to initialize all the inodes.
	 */
	if (Oflag == 1) {
		for (i = 2 * sblock.fs_frag;
		     i < sblock.fs_ipg / INOPF(&sblock);
		     i += sblock.fs_frag) {
			dp1 = (struct ufs1_dinode *)(&iobuf[start]);
			for (j = 0; j < INOPB(&sblock); j++) {
				dp1->di_gen = newfs_random();
				dp1++;
			}
			wtfs(fsbtodb(&sblock, cgimin(&sblock, cylno) + i),
			    sblock.fs_bsize, &iobuf[start]);
		}
	}
}

/*
 * initialize the file system
 */
#define ROOTLINKCNT 3

static struct direct root_dir[] = {
	{ UFS_ROOTINO, sizeof(struct direct), DT_DIR, 1, "." },
	{ UFS_ROOTINO, sizeof(struct direct), DT_DIR, 2, ".." },
	{ UFS_ROOTINO + 1, sizeof(struct direct), DT_DIR, 5, ".snap" },
};

#define SNAPLINKCNT 2

static struct direct snap_dir[] = {
	{ UFS_ROOTINO + 1, sizeof(struct direct), DT_DIR, 1, "." },
	{ UFS_ROOTINO, sizeof(struct direct), DT_DIR, 2, ".." },
};

void
fsinit(time_t utime)
{
	union dinode node;	/* inode */
	struct group *grp;	/* 文件所在的组(gid)，是文件属性，跟文件系统块组无关 */
	gid_t gid;
	int entries;

	memset(&node, 0, sizeof node);
	if ((grp = getgrnam("operator")) != NULL) {	/* 用于获取文件 group id */
		gid = grp->gr_gid;
	} else {
		warnx("Cannot retrieve operator gid, using gid 0.");
		gid = 0;	/* 获取失败，设置为0 */
	}
	entries = (nflag) ? ROOTLINKCNT - 1: ROOTLINKCNT;	/* 指定文件链接数 */
	/* UFS1 文件系统版本代码分支 */
	if (sblock.fs_magic == FS_UFS1_MAGIC) {
		/*
		 * initialize the node
		 */
		node.dp1.di_atime = utime;
		node.dp1.di_mtime = utime;
		node.dp1.di_ctime = utime;
		/*
		 * create the root directory
		 */
		node.dp1.di_mode = IFDIR | UMASK;
		node.dp1.di_nlink = entries;
		node.dp1.di_size = makedir(root_dir, entries);
		node.dp1.di_db[0] = alloc(sblock.fs_fsize, node.dp1.di_mode);
		node.dp1.di_blocks =
		    btodb(fragroundup(&sblock, node.dp1.di_size));
		/* 将逻辑块号转换成磁盘扇区号 */
		wtfs(fsbtodb(&sblock, node.dp1.di_db[0]), sblock.fs_fsize,
		    iobuf);
		iput(&node, UFS_ROOTINO);
		if (!nflag) {
			/*
			 * create the .snap directory
			 */
			node.dp1.di_mode |= 020;
			node.dp1.di_gid = gid;
			node.dp1.di_nlink = SNAPLINKCNT;
			node.dp1.di_size = makedir(snap_dir, SNAPLINKCNT);
				node.dp1.di_db[0] =
				    alloc(sblock.fs_fsize, node.dp1.di_mode);
			node.dp1.di_blocks =
			    btodb(fragroundup(&sblock, node.dp1.di_size));
				wtfs(fsbtodb(&sblock, node.dp1.di_db[0]),
				    sblock.fs_fsize, iobuf);
			iput(&node, UFS_ROOTINO + 1);
		}
	} else {
		/*
		 * initialize the node 初始化 inode 中的各个时间字段
		 */
		node.dp2.di_atime = utime;
		node.dp2.di_mtime = utime;
		node.dp2.di_ctime = utime;
		node.dp2.di_birthtime = utime;
		/*
		 * create the root directory	创建根目录
		 */
		node.dp2.di_mode = IFDIR | UMASK;
		node.dp2.di_nlink = entries;
		node.dp2.di_size = makedir(root_dir, entries);	/* 返回目录的占用的字节数 */
		/* alloc 函数会通过 bitmap 找到可用数据块，进行相关操作之后返回块号 */
		node.dp2.di_db[0] = alloc(sblock.fs_fsize, node.dp2.di_mode);
		/* 指定文件实际持有的块数量 */
		node.dp2.di_blocks =
		    btodb(fragroundup(&sblock, node.dp2.di_size));
		/* 向数据块中输入数据(应该是目录项) */
		wtfs(fsbtodb(&sblock, node.dp2.di_db[0]), sblock.fs_fsize,
		    iobuf);	/* 把 iobuf 中的数据写入到直接寻址第一个块 */
		iput(&node, UFS_ROOTINO);
		/* 奇海操作系统不需要创建 .snapshot 文件夹，所以下面的代码分支不需要执行 */
		if (!nflag) {
			/*
			 * create the .snap directory
			 */
			node.dp2.di_mode |= 020;
			node.dp2.di_gid = gid;
			node.dp2.di_nlink = SNAPLINKCNT;
			node.dp2.di_size = makedir(snap_dir, SNAPLINKCNT);
				node.dp2.di_db[0] =
				    alloc(sblock.fs_fsize, node.dp2.di_mode);
			node.dp2.di_blocks =
			    btodb(fragroundup(&sblock, node.dp2.di_size));
				wtfs(fsbtodb(&sblock, node.dp2.di_db[0]), 
				    sblock.fs_fsize, iobuf);
			iput(&node, UFS_ROOTINO + 1);
		}
	}
}

/*
 * construct a set of directory entries in "iobuf".
 * return size of directory.
 * 在 “iobuf” 中构造一组目录项。返回目录大小
 */
int
makedir(struct direct *protodir, int entries)
{
	char *cp;
	int i, spcleft;

	spcleft = DIRBLKSIZ;
	/*  
		DIRBLKSIZ = 512，首先申请一块 512 字节的内存区域并置零。iobuf 表示的是一个指针
	*/
	memset(iobuf, 0, DIRBLKSIZ);	
	for (cp = iobuf, i = 0; i < entries - 1; i++) {
		protodir[i].d_reclen = DIRSIZ(0, &protodir[i]);
		memmove(cp, &protodir[i], protodir[i].d_reclen);
		cp += protodir[i].d_reclen;
		spcleft -= protodir[i].d_reclen;
	}
	protodir[i].d_reclen = spcleft;
	memmove(cp, &protodir[i], DIRSIZ(0, &protodir[i]));
	return (DIRBLKSIZ);
}

/*
 * allocate a block or frag 申请一个 block 或者 fragment
 */
ufs2_daddr_t
alloc(int size, int mode)
{
	int i, blkno, frag;
	uint d;
	/* 
		读取第0个块组的属性信息，存放到 acg 当中；通过代码调试可以发现，fs_cgsize = 32768，
		说明函数读取的不仅仅是快描述符，而是将对应的位图、表等 metadata 全部读取出来，acg 指针
		指向这块内存区域，所以就可以通过 cg_blksfree 宏定位到对应的位图进行操作
	*/
	bread(&disk, part_ofs + fsbtodb(&sblock, cgtod(&sblock, 0)), (char *)&acg,
	    sblock.fs_cgsize);
	if (acg.cg_magic != CG_MAGIC) {	/* 判断块组魔数 */
		printf("cg 0: bad magic number\n");
		exit(38);
	}
	if (acg.cg_cs.cs_nbfree == 0) {	/* 判断空闲块数量 */
		printf("first cylinder group ran out of space\n");
		exit(39);
	}
	/* 遍历数据块位图，查找可用的数据块，簇号是d */
	for (d = 0; d < acg.cg_ndblk; d += sblock.fs_frag)
		if (isblock(&sblock, cg_blksfree(&acg), d / sblock.fs_frag))
			goto goth;
	printf("internal error: can't find block in cyl 0\n");
	exit(40);
goth:
	/* 
		将簇号转换成块号，然后对 block map 进行相应处理。这里我们可以看到，即使已经得到了
		簇号，还是要转换成块号进行操作。说明文件系统或者初始化工具真正关注的其实是块
	*/
	blkno = fragstoblks(&sblock, d);	
	clrblock(&sblock, cg_blksfree(&acg), blkno);
	if (sblock.fs_contigsumsize > 0)
		clrbit(cg_clustersfree(&acg), blkno);
	/* 块组和文件系统空闲inode计数做--操作 */
	acg.cg_cs.cs_nbfree--;
	sblock.fs_cstotal.cs_nbfree--;
	/* fscs 保存的是所有块组的信息，数组的下标就是块组的索引 */
	fscs[0].cs_nbfree--;
	if (mode & IFDIR) {	/* 判断申请对象是否为一个目录 */
		acg.cg_cs.cs_ndir++;
		sblock.fs_cstotal.cs_ndir++;
		fscs[0].cs_ndir++;
	}	
	/* 
		如果申请的空间不够一个标准块大小(奇海中是不存在这样的问题的)，那就要该空间大小
		包含有多少个 fragment
	*/
	if (size != sblock.fs_bsize) {
		frag = howmany(size, sblock.fs_fsize);
		fscs[0].cs_nffree += sblock.fs_frag - frag;
		sblock.fs_cstotal.cs_nffree += sblock.fs_frag - frag;
		acg.cg_cs.cs_nffree += sblock.fs_frag - frag;
		acg.cg_frsum[sblock.fs_frag - frag]++;
		for (i = frag; i < sblock.fs_frag; i++)
			setbit(cg_blksfree(&acg), d + i);
	}
	if (cgput(&disk, &acg) != 0)
		err(1, "alloc: cgput: %s", disk.d_error);
	return ((ufs2_daddr_t)d);	/* 返回的是一个块号 */
}

/*
 * Allocate an inode on the disk
 */
void
iput(union dinode *ip, ino_t ino)
{
	ufs2_daddr_t d;
	/* 宏cgtod 中的参数0，表示是块组号，所以这里读取的是第0个块组的组描述符 */
	bread(&disk, part_ofs + fsbtodb(&sblock, cgtod(&sblock, 0)), (char *)&acg,
	    sblock.fs_cgsize);
	if (acg.cg_magic != CG_MAGIC) {	/* 判断块组魔数 */
		printf("cg 0: bad magic number\n");
		exit(31);
	}
	/* 因为我们要重新申请一个inode，所以空闲inode计数要做--操作 */
	acg.cg_cs.cs_nifree--;
	/* 
		通过 inode number(貌似不是 genetic number)来确定在索引节点位图中的位置。感觉应该不是
		操作索引节点表， 
	*/
	setbit(cg_inosused(&acg), ino);
	/* 应该是快组描述符的写入操作 */
	if (cgput(&disk, &acg) != 0)
		err(1, "iput: cgput: %s", disk.d_error);
	sblock.fs_cstotal.cs_nifree--;	/* 超级块数据更新 */
	fscs[0].cs_nifree--;	/* 块组csum数据更新 */
	/* inode 号不是随机生成的，应该是顺序生成的，否则这里无法进行大小比较 */
	if (ino >= (unsigned long)sblock.fs_ipg * sblock.fs_ncg) {
		printf("fsinit: inode value out of range (%ju).\n",
		    (uintmax_t)ino);
		exit(32);
	}
	/* 通过 inode number 计算出对应的 disk block number */
	d = fsbtodb(&sblock, ino_to_fsba(&sblock, ino));
	/* 读取该数据块存放的数据 */
	bread(&disk, part_ofs + d, (char *)iobuf, sblock.fs_bsize);
	if (sblock.fs_magic == FS_UFS1_MAGIC)	/* 通过魔数判断文件系统类型 */
		((struct ufs1_dinode *)iobuf)[ino_to_fsbo(&sblock, ino)] =
		    ip->dp1;
	else
		((struct ufs2_dinode *)iobuf)[ino_to_fsbo(&sblock, ino)] =
		    ip->dp2;
	wtfs(d, sblock.fs_bsize, (char *)iobuf);	/* 应该是将更新后的数据写入 */
}

/*
 * possibly write to disk
 * 在前面的调用中可以看到，bno 要么直接是扇区号，要么就是利用 fsbtodb 宏将块号转换
 * 成磁盘的扇区号。由此可见，磁盘的操作的单位是扇区
 */
static void
wtfs(ufs2_daddr_t bno, int size, char *bf)
{
	if (Nflag)
		return;
	if (bwrite(&disk, part_ofs + bno, bf, size) < 0)
		err(36, "wtfs: %d bytes at sector %jd", size, (intmax_t)bno);
}

/*
 * check if a block is available
 */
static int
isblock(struct fs *fs, unsigned char *cp, int h)
{
	unsigned char mask;

	switch (fs->fs_frag) {
	case 8:
		return (cp[h] == 0xff);
	case 4:
		mask = 0x0f << ((h & 0x1) << 2);
		return ((cp[h >> 1] & mask) == mask);
	case 2:
		mask = 0x03 << ((h & 0x3) << 1);
		return ((cp[h >> 2] & mask) == mask);
	case 1:
		mask = 0x01 << (h & 0x7);
		return ((cp[h >> 3] & mask) == mask);
	default:
		fprintf(stderr, "isblock bad fs_frag %d\n", fs->fs_frag);
		return (0);
	}
}

/*
 * take a block out of the map
 */
static void
clrblock(struct fs *fs, unsigned char *cp, int h)
{
	switch ((fs)->fs_frag) {
	case 8:
		cp[h] = 0;
		return;
	case 4:
		cp[h >> 1] &= ~(0x0f << ((h & 0x1) << 2));
		return;
	case 2:
		cp[h >> 2] &= ~(0x03 << ((h & 0x3) << 1));
		return;
	case 1:
		cp[h >> 3] &= ~(0x01 << (h & 0x7));
		return;
	default:
		fprintf(stderr, "clrblock bad fs_frag %d\n", fs->fs_frag);
		return;
	}
}

/*
 * put a block into the map
 */
static void
setblock(struct fs *fs, unsigned char *cp, int h)
{
	switch (fs->fs_frag) {
	case 8:
		cp[h] = 0xff;
		return;
	case 4:
		cp[h >> 1] |= (0x0f << ((h & 0x1) << 2));
		return;
	case 2:
		cp[h >> 2] |= (0x03 << ((h & 0x3) << 1));
		return;
	case 1:
		cp[h >> 3] |= (0x01 << (h & 0x7));
		return;
	default:
		fprintf(stderr, "setblock bad fs_frag %d\n", fs->fs_frag);
		return;
	}
}

/*
 * Determine the number of characters in a
 * single line.
 */

static int
charsperline(void)
{
	int columns;
	char *cp;
	struct winsize ws;

	columns = 0;
	if (ioctl(0, TIOCGWINSZ, &ws) != -1)
		columns = ws.ws_col;
	if (columns == 0 && (cp = getenv("COLUMNS")))
		columns = atoi(cp);
	if (columns == 0)
		columns = 80;	/* last resort */
	return (columns);
}

static int
ilog2(int val)
{
	u_int n;

	for (n = 0; n < sizeof(n) * CHAR_BIT; n++)
		if (1 << n == val)
			return (n);
	errx(1, "ilog2: %d is not a power of 2\n", val);
}

/*
 * For the regression test, return predictable random values.
 * Otherwise use a true random number generator.
 * 对于回归测试，返回可预测的随机值。否则使用真随机数生成器
 */
static u_int32_t
newfs_random(void)
{
	static int nextnum = 1;

	if (Rflag)
		return (nextnum++);
	return (arc4random());
}
