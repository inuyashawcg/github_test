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
__FBSDID("$FreeBSD: releng/12.0/lib/libufs/inode.c 332415 2018-04-11 19:28:54Z mckusick $");

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/disklabel.h>
#include <sys/stat.h>

#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libufs.h>

/* inode 参数表示的 inode number */
int
getino(struct uufsd *disk, void **dino, ino_t inode, int *mode)
{
	ino_t min, max;
	/* 从下文可以看出，该变量不是块号，而是指向 inode 数据的指针 */
	caddr_t inoblock;
	struct ufs1_dinode *dp1;
	struct ufs2_dinode *dp2;
	struct fs *fs;

	ERROR(disk, NULL);

	fs = &disk->d_fs;	/* superblock */
	/*  判断 inode number 是否在限制范围之内 */
	if (inode >= (ino_t)fs->fs_ipg * fs->fs_ncg) {
		ERROR(disk, "inode number out of range");
		return (-1);
	}
	inoblock = disk->d_inoblock;
	/* 
		限定 inode number 取值范围。因为一个 block 会存放多个 inode 结构体。所以
		推测这两个成员限定的是在这个块中存放的 inode 的 number 的范围
	*/
	min = disk->d_inomin;
	max = disk->d_inomax;

	if (inoblock == NULL) {
		inoblock = malloc(fs->fs_bsize);	/* 分配内存空间 */
		if (inoblock == NULL) {
			ERROR(disk, "unable to allocate inode block");
			return (-1);
		}
		disk->d_inoblock = inoblock;
	}
	if (inode >= min && inode < max)
		goto gotit;
	/*
		首先是通过 inode number 计算出 filesystem block number，然后再利用 fsbtodb 宏
		将 block number 转换成 disk sector number，最后调用 bread 读取数据
	*/
	bread(disk, fsbtodb(fs, ino_to_fsba(fs, inode)), inoblock,
	    fs->fs_bsize);
	disk->d_inomin = min = inode - (inode % INOPB(fs));
	disk->d_inomax = max = min + INOPB(fs);
gotit:	switch (disk->d_ufs) {
	case 1:
		/*
			inoblock 存放的是一整个数据块的内容，里边会包含有多个 inode 结构体。这里的处理方式就是对 inoblock 进行
			强制类型转换，把它从一个 char* 转换成一个 struct ufs1_dinode* (非常不错的编程技巧)。所以，该指针可以
			看作是指向了一个 struct ufs1_dinode 类型的数组。再根据 inode number 计算偏移量
		*/
		dp1 = &((struct ufs1_dinode *)inoblock)[inode - min];
		if (mode != NULL)
			*mode = dp1->di_mode & IFMT;
		if (dino != NULL)
			*dino = dp1;
		return (0);
	case 2:
		dp2 = &((struct ufs2_dinode *)inoblock)[inode - min];
		if (mode != NULL)
			*mode = dp2->di_mode & IFMT;
		if (dino != NULL)
			*dino = dp2;
		return (0);
	default:
		break;
	}
	ERROR(disk, "unknown UFS filesystem type");
	return (-1);
}

int
putino(struct uufsd *disk)
{
	struct fs *fs;

	fs = &disk->d_fs;
	if (disk->d_inoblock == NULL) {
		ERROR(disk, "No inode block allocated");
		return (-1);
	}
	/* 写操作不用像读操作那么麻烦，直接将一整块数据更新就完了 */
	if (bwrite(disk, fsbtodb(fs, ino_to_fsba(&disk->d_fs, disk->d_inomin)),
	    disk->d_inoblock, disk->d_fs.fs_bsize) <= 0)
		return (-1);
	return (0);
}
