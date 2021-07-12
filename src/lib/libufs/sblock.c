/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Juli Mallett.  All rights reserved.
 *
 * This software was written by Juli Mallett <jmallett@FreeBSD.org> for the
 * FreeBSD project.  Redistribution and use in source and binary forms, with
 * or without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistribution of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistribution in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: releng/12.0/lib/libufs/sblock.c 330264 2018-03-02 04:34:53Z mckusick $");

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/disklabel.h>
#include <sys/stat.h>

#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <libufs.h>

int
sbread(struct uufsd *disk)
{
	struct fs *fs;

	ERROR(disk, NULL);
	/* 通过设备文件读取磁盘超级块数据 */
	if ((errno = sbget(disk->d_fd, &fs, -1)) != 0) {
		switch (errno) {
		case EIO:
			ERROR(disk, "non-existent or truncated superblock");
			break;
		case ENOENT:
			ERROR(disk, "no usable known superblock found");
			break;
		case ENOSPC:
			ERROR(disk, "failed to allocate space for superblock "
			    "information");
			break;
		case EINVAL:
			ERROR(disk, "The previous newfs operation on this "
			    "volume did not complete.\nYou must complete "
			    "newfs before using this volume.");
			break;
		default:
			ERROR(disk, "unknown superblock read error");
			errno = EIO;
			break;
		}
		disk->d_ufs = 0;
		return (-1);
	}
	/* 将数据存放到 fs 当中 */
	memcpy(&disk->d_fs, fs, fs->fs_sbsize);
	free(fs);
	fs = &disk->d_fs;
	/* 判断文件系统类型 */
	if (fs->fs_magic == FS_UFS1_MAGIC)
		disk->d_ufs = 1;
	if (fs->fs_magic == FS_UFS2_MAGIC)
		disk->d_ufs = 2;
	/* 
		主要获取超级块中的三个参数：
			- block size
			- 超级块地址
			- 超级块 csum 数据
	*/
	disk->d_bsize = fs->fs_fsize / fsbtodb(fs, 1);
	disk->d_sblock = fs->fs_sblockloc / disk->d_bsize;
	disk->d_sbcsum = fs->fs_csp;
	return (0);
}

int
sbwrite(struct uufsd *disk, int all)
{
	struct fs *fs;
	int rv;

	ERROR(disk, NULL);
	/* 该函数仅仅是测试了一下设备能够正常开启关闭 */
	rv = ufs_disk_write(disk);
	if (rv == -1) {
		ERROR(disk, "failed to open disk for writing");
		return (-1);
	}

	fs = &disk->d_fs;
	/* 这里应该才是真正执行超级块写入的地方 */
	if ((errno = sbput(disk->d_fd, fs, all ? fs->fs_ncg : 0)) != 0) {
		switch (errno) {
		case EIO:
			ERROR(disk, "failed to write superblock");
			break;
		default:
			ERROR(disk, "unknown superblock write error");
			errno = EIO;
			break;
		}
		return (-1);
	}
	return (0);
}

/*
 * These are the low-level functions that actually read and write
 * the superblock and its associated data. The actual work is done by
 * the functions ffs_sbget and ffs_sbput in /sys/ufs/ffs/ffs_subr.c.
 * 下面这两个函数是真正实现超级块和关联数据读写操作的函数。实际工作是由 ffs 中的
 * 功能函数来做的
 */
static int use_pread(void *devfd, off_t loc, void **bufp, int size);
static int use_pwrite(void *devfd, off_t loc, void *buf, int size);

/*
 * Read a superblock from the devfd device allocating memory returned
 * in fsp. Also read the superblock summary information.
 * 从 devfd (/dev 目录下的设备文件) 中读取超级块并将数据放到 fsp 申请的那块内存中。
 * 并且读取超级块的摘要信息
 */
int
sbget(int devfd, struct fs **fsp, off_t sblockloc)
{
	/* 
		会调用到 ffs 中的提供的功能函数，但是最终的读取操作是由 use_pread 函数来完成的
	*/
	return (ffs_sbget(&devfd, fsp, sblockloc, "user", use_pread));
}

/*
 * A read function for use by user-level programs using libufs.
 * 一种用于用户级程序使用libufs的读取函数
 * ext2 文件系统中没有用于专门读取超级块数据的函数，只能有pread函数直接读取超级块
 * 数据，存放到buf当中，强制类型转换成 tptfs_super_block 类型，然后再进行其他的
 * 一些操作(追踪一下 ufs 的函数执行过程，其实大致步骤就是这样)
 */
static int
use_pread(void *devfd, off_t loc, void **bufp, int size)
{
	int fd;

	fd = *(int *)devfd;
	if ((*bufp = malloc(size)) == NULL)
		return (ENOSPC);
	if (pread(fd, *bufp, size, loc) != size)
		return (EIO);
	return (0);
}

/*
 * Write a superblock to the devfd device from the memory pointed to by fs.
 * Also write out the superblock summary information but do not free the
 * summary information memory.
 * 从 fs 指向的内存中将超级块数据写入 devfd 所关联的设备。同时写出超级块摘要信息，但不要
 * 释放摘要信息内存
 *
 * Additionally write out numaltwrite of the alternate superblocks. Use
 * fs->fs_ncg to write out all of the alternate superblocks.
 * 另外，写一个或多个交替的超级块。使用 fs->fs_ncg 写出所有备用超级块
 * 
 * ufs 中会包含有多个块组和多个超级块的备份，numaltwrite 参数应该表示的就是超级块数据
 * 不仅仅是保存一个位置(块组？)，而是要同时写到多个位置(块组)，然后再判断每个写入操作是否
 * 都成功了
 */
int
sbput(int devfd, struct fs *fs, int numaltwrite)
{
	struct csum *savedcsp;
	off_t savedactualloc;
	int i, error;
	/* 调用 ffs 提供的方法来实现写操作 */
	if ((error = ffs_sbput(&devfd, fs, fs->fs_sblockactualloc,
	     use_pwrite)) != 0)
		return (error);
	if (numaltwrite == 0)
		return (0);
	savedactualloc = fs->fs_sblockactualloc;
	savedcsp = fs->fs_csp;
	fs->fs_csp = NULL;
	/* 
		磁盘中的每个块组都包含有一个超级块对象，我们一般访问的都是第一个块组中的超级块，
		其他都是当做备份来处理。numaltwrite 表示的应该就是我们要写出多少个备用超级块，
		也就是排除掉第一个块组还要写出多少个
	*/
	for (i = 0; i < numaltwrite; i++) {
		fs->fs_sblockactualloc = dbtob(fsbtodb(fs, cgsblock(fs, i)));
		if ((error = ffs_sbput(&devfd, fs, fs->fs_sblockactualloc,
		     use_pwrite)) != 0) {
			fs->fs_sblockactualloc = savedactualloc;
			fs->fs_csp = savedcsp;
			return (-1);
		}
	}
	fs->fs_sblockactualloc = savedactualloc;
	fs->fs_csp = savedcsp;
	return (0);
}

/*
 * A write function for use by user-level programs using sbput in libufs.
 * 一种用于用户级程序的写入函数，该函数使用 libufs 中的 sbput
 */
static int
use_pwrite(void *devfd, off_t loc, void *buf, int size)
{
	int fd;

	fd = *(int *)devfd;
	if (pwrite(fd, buf, size, loc) != size)
		return (EIO);
	return (0);
}
