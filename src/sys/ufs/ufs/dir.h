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
 *	@(#)dir.h	8.2 (Berkeley) 1/21/94
 * $FreeBSD: releng/12.0/sys/ufs/ufs/dir.h 326023 2017-11-20 19:43:44Z pfg $
 */

#ifndef _UFS_UFS_DIR_H_
#define	_UFS_UFS_DIR_H_

/*
 * Theoretically, directories can be more than 2Gb in length, however, in
 * practice this seems unlikely. So, we define the type doff_t as a 32-bit
 * quantity to keep down the cost of doing lookup on a 32-bit machine.
 * 
 * 理论上，目录的长度可以超过 2GB，但实际上这似乎不太可能。因此，我们将 doff_t 类型定义为
 * 32位数量，以降低在32位机器上执行查找的成本
 */
#define	doff_t		int32_t
#define	MAXDIRSIZE	(0x7fffffff)

/*
 * A directory consists of some number of blocks of DIRBLKSIZ
 * bytes, where DIRBLKSIZ is chosen such that it can be transferred
 * to disk in a single atomic operation (e.g. 512 bytes on most machines).
 * 一个目录由一定数量的 DIRBLKSIZ 字节块组成，其中 DIRBLKSIZ 的选择使得它可以在单个原子
 * 操作中传输到磁盘（例如在大多数机器上是512字节）
 *
 * Each DIRBLKSIZ byte block contains some number of directory entry
 * structures, which are of variable length.  Each directory entry has
 * a struct direct at the front of it, containing its inode number,
 * the length of the entry, and the length of the name contained in
 * the entry.  These are followed by the name padded to a 4 byte boundary
 * with null bytes.  All names are guaranteed null terminated.
 * The maximum length of a name in a directory is UFS_MAXNAMLEN.
 * 每一个 DIRBLKSIZ 大小的字节块都会包含有一些目录条目结构体，这些结构体是可变长度的。每一个
 * 目录条目的前面都包含有一个目录结构体，包含有 inode number，条目的长度，条目名称的长度等等
 * 信息。后面是名称，用空字节填充到4字节边界。所有名称都保证以null结尾。
 * 目录中名称的最大长度是UFS_MAXNAMLEN
 *
 * The macro DIRSIZ(fmt, dp) gives the amount of space required to represent
 * a directory entry.  Free space in a directory is represented by
 * entries which have dp->d_reclen > DIRSIZ(fmt, dp).  All DIRBLKSIZ bytes
 * in a directory block are claimed by the directory entries.  This
 * usually results in the last entry in a directory having a large
 * dp->d_reclen.  When entries are deleted from a directory, the
 * space is returned to the previous entry in the same directory
 * block by increasing its dp->d_reclen.  If the first entry of
 * a directory block is free, then its dp->d_ino is set to 0.
 * Entries other than the first in a directory do not normally have
 * dp->d_ino set to 0.
 * 宏 DIRSIZ(fmt, dp) 给出表示一个目录条目所需要的空间的大小。目录中的可用空间由
 * dp->d_reclen>DIRSIZ（fmt，dp）的条目表示。目录块中的所有DIRBLKSIZ字节都由目录项声明。
 * 这通常会导致目录中的最后一个条目有一个大的dp->d_reclen。如果目录块的第一个条目是空闲的，
 * 则其 dp->d_ino 设置为0。除目录中第一个条目外，其他条目通常不会将 dp->d_ino 设置为0
 */
#define	DIRBLKSIZ	DEV_BSIZE
#define	UFS_MAXNAMLEN	255

/*
	ufs 目录项在磁盘上是以片(chunk)为单位进行分配的。目录片的大小是可选择的，遵循的原则是每次分配
	的目录片只需要一次操作就可以传送到磁盘上。每个目录片可以划分成长度不等的若干个目录项，所以文件名
	可以是几乎任意长度的。但是目录项不能跨越多个目录片；
	direct 结构的第二个成员表示的是目录项的大小，这个其实可以用于对目录项的检索当中。第一个目录项
	加上它本身的长度，就可以找到第二个目录项所在的位置，以此类推
*/
struct	direct {
	u_int32_t d_ino;		/* inode number of entry */
	u_int16_t d_reclen;		/* length of this record */
	u_int8_t  d_type; 		/* file type, see below */
	u_int8_t  d_namlen;		/* length of string in d_name */
	char	  d_name[UFS_MAXNAMLEN + 1];
					/* name with length <= UFS_MAXNAMLEN */
};

/*
 * File types
 */
#define	DT_UNKNOWN	 0
#define	DT_FIFO		 1
#define	DT_CHR		 2
#define	DT_DIR		 4
#define	DT_BLK		 6
#define	DT_REG		 8
#define	DT_LNK		10
#define	DT_SOCK		12
#define	DT_WHT		14

/*
 * Convert between stat structure types and directory types.
 */
#define	IFTODT(mode)	(((mode) & 0170000) >> 12)
#define	DTTOIF(dirtype)	((dirtype) << 12)

/*
 * The DIRSIZ macro gives the minimum record length which will hold
 * the directory entry.  This requires the amount of space in struct direct
 * without the d_name field, plus enough space for the name with a terminating
 * null byte (dp->d_namlen+1), rounded up to a 4 byte boundary.
 *
 * DIRSIZ 宏给出保存目录项的最小记录长度。这需要 struct direct 中不带 d_name字段的空间量，
 * 再加上足够的空间来容纳带有终止空字节（dp->d_namlen+1）的名称，四舍五入到4字节边界
 */
#define	DIRECTSIZ(namlen)						\
	((__offsetof(struct direct, d_name) +				\
	  ((namlen)+1)*sizeof(((struct direct *)0)->d_name[0]) + 3) & ~3)
#if (BYTE_ORDER == LITTLE_ENDIAN)
#define	DIRSIZ(oldfmt, dp) \
    ((oldfmt) ? DIRECTSIZ((dp)->d_type) : DIRECTSIZ((dp)->d_namlen))
#else
#define	DIRSIZ(oldfmt, dp) \
    DIRECTSIZ((dp)->d_namlen)
#endif
#define	OLDDIRFMT	1
#define	NEWDIRFMT	0

/*
 * Template for manipulating directories.  Should use struct direct's,
 * but the name field is UFS_MAXNAMLEN - 1, and this just won't do.
 * 操作目录的模板。应该使用struct direct，但是name字段是 UFS_MAXNAMLEN-1，这样做不行
 */
struct dirtemplate {
	u_int32_t	dot_ino;
	int16_t		dot_reclen;
	u_int8_t	dot_type;
	u_int8_t	dot_namlen;
	char		dot_name[4];	/* must be multiple of 4 */
	u_int32_t	dotdot_ino;
	int16_t		dotdot_reclen;
	u_int8_t	dotdot_type;
	u_int8_t	dotdot_namlen;
	char		dotdot_name[4];	/* ditto */
};

/*
 * This is the old format of directories, sanz type element.
 */
struct odirtemplate {
	u_int32_t	dot_ino;
	int16_t		dot_reclen;
	u_int16_t	dot_namlen;
	char		dot_name[4];	/* must be multiple of 4 */
	u_int32_t	dotdot_ino;
	int16_t		dotdot_reclen;
	u_int16_t	dotdot_namlen;
	char		dotdot_name[4];	/* ditto */
};
#endif /* !_DIR_H_ */
