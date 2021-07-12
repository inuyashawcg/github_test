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
 * Copyright (c) 1983, 1989, 1993, 1994
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
static const char copyright[] =
"@(#) Copyright (c) 1983, 1989, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)newfs.c	8.13 (Berkeley) 5/1/95";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD: releng/12.0/sbin/newfs/newfs.c 335597 2018-06-24 05:40:42Z eadler $");

/*
 * newfs: friendly front end to mkfs
 */
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/disk.h>
#include <sys/disklabel.h>
#include <sys/file.h>
#include <sys/mount.h>

#include <ufs/ufs/dir.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>
#include <ufs/ufs/ufsmount.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <paths.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <libutil.h>

#include "newfs.h"

int	Eflag;			/* Erase previous disk contents */
int	Lflag;			/* add a volume label */
int	Nflag;			/* run without writing file system 在不写入文件系统的情况下运行 */
int	Oflag = 2;		/* file system format (1 => UFS1, 2 => UFS2) */
int	Rflag;			/* regression test */
int	Uflag;			/* enable soft updates for file system */
int	jflag;			/* enable soft updates journaling for filesys */
int	Xflag = 0;		/* exit in middle of newfs for testing */
int	Jflag;			/* enable gjournal for file system */
int	lflag;			/* enable multilabel for file system */
int	nflag;			/* do not create .snap directory */
int	tflag;			/* enable TRIM */
/*
	从代码逻辑可以看出，fssize 是通过解析 newfs 指令中的参数 -s 获取到的。查阅 newfs 
	用户手册可以看到，它表示的是文件系统以扇区为单位的大小。所以 fssize 表示的是文件系统
	总的扇区数
	下面定义的这些参数可以大致反应出来我们磁盘格式化工具到底需要哪些参数，或者是要获取哪些
	参数。这些参数在 mkfs 中看着都是要用到的
*/
intmax_t fssize;		/* file system size 文件系统大小(sectors) */
off_t	mediasize;		/* device size 设备大小(bytes) */
int	sectorsize;		/* bytes/sector 扇区大小 */
int	realsectorsize;		/* bytes/sector in hardware 实际申请的扇区的大小 */
int	fsize = 0;		/* fragment size 片大小 */
int	bsize = 0;		/* block size 块大小 */
int	maxbsize = 0;		/* maximum clustering I/O簇策略 */
int	maxblkspercg = MAXBLKSPERCG; /* maximum blocks per cylinder group 每个块组最大能占用多少块 */
int	minfree = MINFREE;	/* free space threshold 可用数据块所占最小比例 */
int	metaspace;		/* space held for metadata blocks 元数据所占用的空间大小(块组相关) */
/*
	该参数是 ufs 文件系统功能优化会用到的东西，大概就是时间和空间的如何取舍的问题。简单理解为比如说我们
	在文件系统中要采用一种策略，策略可以通过所占用空间大小和完成操作所用的时间作为评估标准。哪种标准对于
	我们来说更能接受，我们就采用该标准来评估来我们的策略
*/
int	opt = DEFAULTOPT;	/* optimization preference (space or time) */
/*
	字面意思是一个 inode 对应多少数据块，其实就是对 inode 总体数量进行预测。因为文件系统在初始化的时候
	需要确定 inode 位图以及数据结构所要占用的数据块的数量，这样才能确定每个数据区的相对位置。比如整个文件
	系统一共有10000个数据块，我们假定每个 inode 平均占用4个数据块，那这样我们就需要申请大概2500个 inode，
	这样就可以对 inode 所占用的空间做一个大致评估
*/
int	density;		/* number of bytes per inode */
int	maxcontig = 0;		/* max contiguous blocks to allocate 簇I/O */
int	maxbpg;			/* maximum blocks per file in a cyl group 块组中每个文件最大能占用的块数 */
int	avgfilesize = AVFILESIZ;/* expected average file size 预期的平均文件的大小 */
int	avgfilesperdir = AFPDIR;/* expected number of files per directory 预期的每个目录下的文件数 */
u_char	*volumelabel = NULL;	/* volume label for filesystem 文件系统卷标签 */
struct uufsd disk;		/* libufs disk structure */

static char	device[MAXPATHLEN];
static u_char   bootarea[BBSIZE];
static int	is_file;		/* work on a file, not a device */
static char	*dkname;
static char	*disktype;

static void getfssize(intmax_t *, const char *p, intmax_t, intmax_t);
static struct disklabel *getdisklabel(void);
static void usage(void);
static int expand_number_int(const char *buf, int *num);

ufs2_daddr_t part_ofs; /* partition offset in blocks, used with files */

int
main(int argc, char *argv[])
{
	struct partition *pp;
	struct disklabel *lp;
	struct stat st;
	char *cp, *special;
	intmax_t reserved;
	int ch, i, rval;
	char part_name;		/* partition name, default to full disk */

	part_name = 'c';
	reserved = 0;
	while ((ch = getopt(argc, argv,
	    "EJL:NO:RS:T:UXa:b:c:d:e:f:g:h:i:jk:lm:no:p:r:s:t")) != -1)
		switch (ch) {
		case 'E':
			/*
				在创建文件系统之前擦除磁盘上的内容。在超级块前保留的区域(引导块)的数据将不会
				被擦除。擦除仅与闪存或精简配置的设备相关。擦除可能需要很长时间。如果设备不支持
				BIO_DELETE，命令将失败。
			*/
			Eflag = 1;
			break;
		case 'J':
			/*
				通过gjournal在新文件系统上启用日志记录
			*/
			Jflag = 1;
			break;
		case 'L':
			/* 添加一个卷名 */
			volumelabel = optarg;
			i = -1;
			while (isalnum(volumelabel[++i]) ||
			    volumelabel[i] == '_');
			if (volumelabel[i] != '\0') {
				errx(1, "bad volume label. Valid characters are alphanumerics.");
			}
			if (strlen(volumelabel) >= MAXVOLLEN) {
				errx(1, "bad volume label. Length is longer than %d.",
				    MAXVOLLEN);
			}
			Lflag = 1;
			break;
		case 'N':
			/* 使文件系统参数打印出来而不真正创建文件系统 */
			Nflag = 1;
			break;
		case 'O':
			/* 用于指定文件系统版本(UFS1 / UFS2) */
			if ((Oflag = atoi(optarg)) < 1 || Oflag > 2)
				errx(1, "%s: bad file system format value",
				    optarg);
			break;
		case 'R':
			Rflag = 1;
			break;
		case 'S':
			/* 指定扇区的大小 */
			rval = expand_number_int(optarg, &sectorsize);
			if (rval < 0 || sectorsize <= 0)
				errx(1, "%s: bad sector size", optarg);
			break;
		case 'T':
			disktype = optarg;
			break;
		case 'j':
			jflag = 1;
			/* fall through to enable soft updates */
			/* FALLTHROUGH */
		case 'U':
			/* 在新文件系统上启用软更新 */
			Uflag = 1;
			break;
		case 'X':
			Xflag++;
			break;
		case 'a':
			/* 指定在强制旋转延迟之前将布局的最大连续块数。默认值为 16(maxcontig) */
			rval = expand_number_int(optarg, &maxcontig);
			if (rval < 0 || maxcontig <= 0)
				errx(1, "%s: bad maximum contiguous blocks",
				    optarg);
			break;
		case 'b':
			/*
				文件系统的块大小，以字节为单位。它必须是2的幂。默认大小是32768字节，
				允许的最小大小是4096字节。最佳 block:fragment 比率是8:1。其他比例
				是可能的，但不推荐，并可能产生较差的结果
			*/
			rval = expand_number_int(optarg, &bsize);
			if (rval < 0)
				 errx(1, "%s: bad block size",
                                    optarg);
			if (bsize < MINBSIZE)
				errx(1, "%s: block size too small, min is %d",
				    optarg, MINBSIZE);
			if (bsize > MAXBSIZE)
				errx(1, "%s: block size too large, max is %d",
				    optarg, MAXBSIZE);
			break;
		case 'c':
			/*
				文件系统中每个柱面组的块数。默认值是计算其他参数允许的最大值。
				此值取决于其他参数，特别是块大小和每个inode的字节数
			*/
			rval = expand_number_int(optarg, &maxblkspercg);
			if (rval < 0 || maxblkspercg <= 0)
				errx(1, "%s: bad blocks per cylinder group",
				    optarg);
			break;
		case 'd':
			/*
				文件系统可以选择使用扩展数据块存储大文件。此参数指定可以使用的最大数据块大小。
				默认值是文件系统块大小。它目前被限制为文件系统块大小的16倍的最大值和文件系统
				块大小的最小值
			*/
			rval = expand_number_int(optarg, &maxbsize);
			if (rval < 0 || maxbsize < MINBSIZE)
				errx(1, "%s: bad extent block size", optarg);
			break;
		case 'e':
			/*
				指示在强制开始从另一个柱面组分配块之前，任何单个文件可以从柱面组中分配的最大块数。
				默认值约为柱面组中总块数的四分之一
			*/
			rval = expand_number_int(optarg, &maxbpg);
			if (rval < 0 || maxbpg <= 0)
			  errx(1, "%s: bad blocks per file in a cylinder group",
				    optarg);
			break;
		case 'f':
			/*
				文件系统的片段大小（字节）。它必须是 blocksize/8 和 blocksize 之间的二次幂。
				默认值为4096字节
			*/
			rval = expand_number_int(optarg, &fsize);
			if (rval < 0 || fsize <= 0)
				errx(1, "%s: bad fragment size", optarg);
			break;
		case 'g':
			/* 指定文件系统平均文件长度 */
			rval = expand_number_int(optarg, &avgfilesize);
			if (rval < 0 || avgfilesize <= 0)
				errx(1, "%s: bad average file size", optarg);
			break;
		case 'h':
			/* 文件系统中每个目录的预期平均文件数 */
			rval = expand_number_int(optarg, &avgfilesperdir);
			if (rval < 0 || avgfilesperdir <= 0)
			       errx(1, "%s: bad average files per dir", optarg);
			break;
		case 'i':
			/*
				指定文件系统中inode的密度。默认值是为每个（2*frag大小）字节的数据空间
				创建一个inode。如果需要较少的inode，则应使用较大的inode；要创建更多
				inode，应该给出一个较小的数字。每个不同的文件都需要一个inode，因此该值
				有效地指定了文件系统上的平均文件大小。
			*/
			rval = expand_number_int(optarg, &density);
			if (rval < 0 || density <= 0)
				errx(1, "%s: bad bytes per inode", optarg);
			break;
		case 'l':
			lflag = 1;
			break;
		case 'k':
			/*
				为每个圆柱体组中的元数据块设置要保留的空间量。设置后，文件系统首选项例程
				将尝试在每个柱面组的 inode 块之后立即保存指定的空间量，以供元数据块使用。
				对元数据块进行聚类加速了随机文件访问，减少了 fsck 的运行时间。默认情况下，
				newfs 将其设置为 minfree 保留空间的一半
			*/
			if ((metaspace = atoi(optarg)) < 0)
				errx(1, "%s: bad metadata space %%", optarg);
			if (metaspace == 0)
				/* force to stay zero in mkfs */
				metaspace = -1;
			break;
		case 'm':
			/* 
				正常用户预留空间的百分比；最小可用空间阈值。使用的默认值由 MINFREE <ufs/ffs/fs.h>定义，
				当前为8%
			*/
			if ((minfree = atoi(optarg)) < 0 || minfree > 99)
				errx(1, "%s: bad free space %%", optarg);
			break;
		case 'n':
			/* 不要创建 .snap 文件，tptfs 中肯定是不需要的 */
			nflag = 1;
			break;
		case 'o':
			/*
				（空间或时间）。可以指示文件系统尽量减少分配块所花费的时间，或者尽量减少磁盘上的空间碎片；
				如果minfree的值大于或等于8%，则默认为优化时间。
			*/
			if (strcmp(optarg, "space") == 0)
				opt = FS_OPTSPACE;
			else if (strcmp(optarg, "time") == 0)
				opt = FS_OPTTIME;
			else
				errx(1, 
		"%s: unknown optimization preference: use `space' or `time'",
				    optarg);
			break;
		case 'r':
			/*
				在专用部分中指定的分区末尾的保留空间的大小，以扇区为单位。文件系统不会占用该空间；它可以被
				其他用户使用，比如geom（4）。默认为0
			*/
			errno = 0;
			reserved = strtoimax(optarg, &cp, 0);
			if (errno != 0 || cp == optarg ||
			    *cp != '\0' || reserved < 0)
				errx(1, "%s: bad reserved size", optarg);
			break;
		case 'p':
			/*
				在底层映像是文件的情况下要使用的分区名称（a..h），因此您不能通过文件系统访问各个分区。
				也可以与设备一起使用，例如，newfs-p f /dev/da1s3 相当于 newfs /dev/da1s3f
			*/
			is_file = 1;
			part_name = optarg[0];
			break;

		case 's':
			/*
				文件系统以扇区为单位的大小。此值默认为以特殊方式指定的原始分区的大小减去其末尾的保留空间（请参见-r）。
				大小为0也可用于选择默认值。有效的大小值不能大于默认值，这意味着文件系统无法扩展到保留空间
			*/
			errno = 0;
			fssize = strtoimax(optarg, &cp, 0);
			if (errno != 0 || cp == optarg ||
			    *cp != '\0' || fssize < 0)
				errx(1, "%s: bad file system size", optarg);
			break;
		case 't':
			/*
				启用“修剪启用”标志。如果已启用，并且基础设备支持BIO\u DELETE命令，则文件系统将为
				每个释放的块向基础设备发送删除请求。trim enable标志通常为闪存设备设置，以减少写
				放大，从而减少写限制闪存的磨损，并通常提高长期性能。精简配置的存储还可以通过将未使用
				的块返回到全局池而受益
			*/
			tflag = 1;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	special = argv[0];
	if (!special[0])
		err(1, "empty file/special name");
	cp = strrchr(special, '/');
	if (cp == NULL) {
		/*
		 * No path prefix; try prefixing _PATH_DEV.
		 */
		snprintf(device, sizeof(device), "%s%s", _PATH_DEV, special);
		special = device;	/* 应该是文件的完整路径名 */
	}

	if (is_file) {
		/* bypass ufs_disk_fillout_blank */
		bzero( &disk, sizeof(disk));
		disk.d_bsize = 1;	/* bsize = 1 ?? */
		disk.d_name = special;	/* 指定名称 */
		disk.d_fd = open(special, O_RDONLY);	/* 以只读的方式打开设备文件 */
		if (disk.d_fd < 0 ||
		    (!Nflag && ufs_disk_write(&disk) == -1))
			errx(1, "%s: ", special);		/* 填充 disk 结构字段并且测试是否可以写入 */
	} else if (ufs_disk_fillout_blank(&disk, special) == -1 ||	
	    (!Nflag && ufs_disk_write(&disk) == -1)) {
		if (disk.d_error != NULL)
			errx(1, "%s: %s", special, disk.d_error);
		else
			err(1, "%s", special);
	}
	/* 获取文件状态 */
	if (fstat(disk.d_fd, &st) < 0)
		err(1, "%s", special);
	/* 判断文件是否表示一个字符设备 */
	if ((st.st_mode & S_IFMT) != S_IFCHR) {
		warn("%s: not a character-special device", special);
		is_file = 1;	/* assume it is a file */
		dkname = special;	/* 设置 diskname */
		if (sectorsize == 0)
			sectorsize = 512;	/* 设置扇区大小 */
		mediasize = st.st_size;	/* device size，设备大小 */
		/* set fssize from the partition */
	} else {
	    if (sectorsize == 0)
		if (ioctl(disk.d_fd, DIOCGSECTORSIZE, &sectorsize) == -1)
		    sectorsize = 0;	/* back out on error for safety */
	    if (sectorsize && ioctl(disk.d_fd, DIOCGMEDIASIZE, &mediasize) != -1)
		getfssize(&fssize, special, mediasize / sectorsize, reserved);	/* 获取文件系统大小(in sector) */
	}

	pp = NULL;
	lp = getdisklabel();	/* 获取磁盘标签，主要是分区信息 */
	if (lp != NULL) {
		if (!is_file) /* already set for files */
			part_name = special[strlen(special) - 1];
		if ((part_name < 'a' || part_name - 'a' >= MAXPARTITIONS) &&
				!isdigit(part_name))
			errx(1, "%s: can't figure out file system partition",
					special);
		cp = &part_name;
		/*
			获取分区表信息
		*/
		if (isdigit(*cp))	/* 测试分区名称第一个字符是不是十进制数字 */
			pp = &lp->d_partitions[RAW_PART];
		else
			pp = &lp->d_partitions[*cp - 'a'];
		if (pp->p_size == 0)
			errx(1, "%s: `%c' partition is unavailable",
			    special, *cp);
		if (pp->p_fstype == FS_BOOT)
			errx(1, "%s: `%c' partition overlaps boot program",
			    special, *cp);
		getfssize(&fssize, special, pp->p_size, reserved);	/* 获取分区大小(sectors) */
		if (sectorsize == 0)
			sectorsize = lp->d_secsize;	/* 每个扇区所包含的字节数 */
		if (fsize == 0)
			fsize = pp->p_fsize;	/* 获取fragment大小 */
		if (bsize == 0)
			bsize = pp->p_frag * pp->p_fsize;	/* 通过fragment大小计算块大小，tptfs中是相等的 */
		if (is_file)
			part_ofs = pp->p_offset;	/* 从分区表中获取起始扇区的偏移量 */
	}
	/* 
		当我们获取到的这些参数如果都是小于0的时候，要如何处理的方法 
	*/
	if (sectorsize <= 0)
		errx(1, "%s: no default sector size", special);
	if (fsize <= 0)
		fsize = MAX(DFL_FRAGSIZE, sectorsize);
	if (bsize <= 0)
		bsize = MIN(DFL_BLKSIZE, 8 * fsize);
	if (minfree < MINFREE && opt != FS_OPTSPACE) {
		fprintf(stderr, "Warning: changing optimization to space ");
		fprintf(stderr, "because minfree is less than %d%%\n", MINFREE);
		opt = FS_OPTSPACE;
	}
	/* 设置扇区的大小 */
	realsectorsize = sectorsize;
	if (sectorsize != DEV_BSIZE) {		/* XXX */
		int secperblk = sectorsize / DEV_BSIZE;

		sectorsize = DEV_BSIZE;
		fssize *= secperblk;
		if (pp != NULL)
			pp->p_size *= secperblk;
	}
	/* 处理完了相关的数据之后，开始真正创建文件系统 */
	mkfs(pp, special);
	ufs_disk_close(&disk);
	if (!jflag)
		exit(0);
	if (execlp("tunefs", "newfs", "-j", "enable", special, NULL) < 0)
		err(1, "Cannot enable soft updates journaling, tunefs");
	/* NOT REACHED */
}

void
getfssize(intmax_t *fsz, const char *s, intmax_t disksize, intmax_t reserved)
{
	intmax_t available;

	available = disksize - reserved;
	if (available <= 0)
		errx(1, "%s: reserved not less than device size %jd",
		    s, disksize);
	if (*fsz == 0)
		*fsz = available;
	else if (*fsz > available)
		errx(1, "%s: maximum file system size is %jd",
		    s, available);
}

struct disklabel *
getdisklabel(void)
{
	static struct disklabel lab;
	struct disklabel *lp;

	if (is_file) {
		if (read(disk.d_fd, bootarea, BBSIZE) != BBSIZE)
			err(4, "cannot read bootarea");
		if (bsd_disklabel_le_dec(
		    bootarea + (0 /* labeloffset */ +
				1 /* labelsoffset */ * sectorsize),
		    &lab, MAXPARTITIONS))
			errx(1, "no valid label found");

		lp = &lab;
		return &lab;
	}

	if (disktype) {
		lp = getdiskbyname(disktype);
		if (lp != NULL)
			return (lp);
	}
	return (NULL);
}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: %s [ -fsoptions ] special-device%s\n",
	    getprogname(),
	    " [device-type]");
	fprintf(stderr, "where fsoptions are:\n");
	fprintf(stderr, "\t-E Erase previous disk content\n");
	fprintf(stderr, "\t-J Enable journaling via gjournal\n");
	fprintf(stderr, "\t-L volume label to add to superblock\n");
	fprintf(stderr,
	    "\t-N do not create file system, just print out parameters\n");
	fprintf(stderr, "\t-O file system format: 1 => UFS1, 2 => UFS2\n");
	fprintf(stderr, "\t-R regression test, suppress random factors\n");
	fprintf(stderr, "\t-S sector size\n");
	fprintf(stderr, "\t-T disktype\n");
	fprintf(stderr, "\t-U enable soft updates\n");
	fprintf(stderr, "\t-a maximum contiguous blocks\n");
	fprintf(stderr, "\t-b block size\n");
	fprintf(stderr, "\t-c blocks per cylinders group\n");
	fprintf(stderr, "\t-d maximum extent size\n");
	fprintf(stderr, "\t-e maximum blocks per file in a cylinder group\n");
	fprintf(stderr, "\t-f frag size\n");
	fprintf(stderr, "\t-g average file size\n");
	fprintf(stderr, "\t-h average files per directory\n");
	fprintf(stderr, "\t-i number of bytes per inode\n");
	fprintf(stderr, "\t-j enable soft updates journaling\n");
	fprintf(stderr, "\t-k space to hold for metadata blocks\n");
	fprintf(stderr, "\t-l enable multilabel MAC\n");
	fprintf(stderr, "\t-n do not create .snap directory\n");
	fprintf(stderr, "\t-m minimum free space %%\n");
	fprintf(stderr, "\t-o optimization preference (`space' or `time')\n");
	fprintf(stderr, "\t-p partition name (a..h)\n");
	fprintf(stderr, "\t-r reserved sectors at the end of device\n");
	fprintf(stderr, "\t-s file system size (sectors)\n");
	fprintf(stderr, "\t-t enable TRIM\n");
	exit(1);
}

static int
expand_number_int(const char *buf, int *num)
{
	int64_t num64;
	int rval;

	rval = expand_number(buf, &num64);
	if (rval < 0)
		return (rval);
	if (num64 > INT_MAX || num64 < INT_MIN) {
		errno = ERANGE;
		return (-1);
	}
	*num = (int)num64;
	return (0);
}
