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
__FBSDID("$FreeBSD: releng/12.0/lib/libufs/type.c 332266 2018-04-08 06:59:42Z mckusick $");

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/disklabel.h>
#include <sys/stat.h>

#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include <errno.h>
#include <fcntl.h>
#include <fstab.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libufs.h>

/* Internally, track the 'name' value, it's ours. */
#define	MINE_NAME	0x01
/* Track if its fd points to a writable device. */
#define	MINE_WRITE	0x02

int
ufs_disk_close(struct uufsd *disk)
{
	ERROR(disk, NULL);
	close(disk->d_fd);
	disk->d_fd = -1;
	if (disk->d_inoblock != NULL) {
		free(disk->d_inoblock);
		disk->d_inoblock = NULL;
	}
	if (disk->d_mine & MINE_NAME) {
		free((char *)(uintptr_t)disk->d_name);
		disk->d_name = NULL;
	}
	if (disk->d_sbcsum != NULL) {
		free(disk->d_sbcsum);
		disk->d_sbcsum = NULL;
	}
	return (0);
}

int
ufs_disk_fillout(struct uufsd *disk, const char *name)
{
	if (ufs_disk_fillout_blank(disk, name) == -1) {
		return (-1);
	}
	if (sbread(disk) == -1) {
		ERROR(disk, "could not read superblock to fill out disk");
		ufs_disk_close(disk);
		return (-1);
	}
	return (0);
}

int
ufs_disk_fillout_blank(struct uufsd *disk, const char *name)
{
	struct stat st;
	struct fstab *fs;
	struct statfs sfs;
	const char *oname;
	char dev[MAXPATHLEN];
	int fd, ret;

	ERROR(disk, NULL);
	/* 通过name获取到文件的属性信息，保存至stat */
	oname = name;
again:	if ((ret = stat(name, &st)) < 0) {
		if (*name != '/') {
			snprintf(dev, sizeof(dev), "%s%s", _PATH_DEV, name);
			name = dev;
			goto again;
		}
		/*
		 * The given object doesn't exist, but don't panic just yet -
		 * it may be still mount point listed in /etc/fstab, but without
		 * existing corresponding directory.
		 * 给定的对象不存在，但不要惊慌-它可能仍然是 /etc/fstab 中列出的挂载点，但没有
		 * 相应的目录
		 */
		name = oname;
	}
	/* 判断文件是不是 regular file 类型 */
	if (ret >= 0 && S_ISREG(st.st_mode)) {
		/* Possibly a disk image, give it a try.  */
		;
	} else if (ret >= 0 && S_ISCHR(st.st_mode)) {
		/* This is what we need, do nothing. */
		;	/* getfsfile 是由 file table 提供的函数，可直接使用 */
	} else if ((fs = getfsfile(name)) != NULL) {
		/*
		 * The given mount point is listed in /etc/fstab.
		 * It is possible that someone unmounted file system by hand
		 * and different file system is mounted on this mount point,
		 * but we still prefer /etc/fstab entry, because on the other
		 * hand, there could be /etc/fstab entry for this mount
		 * point, but file system is not mounted yet (eg. noauto) and
		 * statfs(2) will point us at different file system.
		 */
		name = fs->fs_spec;
	} else if (ret >= 0 && S_ISDIR(st.st_mode)) {
		/*
		 * The mount point is not listed in /etc/fstab, so it may be
		 * file system mounted by hand.
		 */
		if (statfs(name, &sfs) < 0) {
			ERROR(disk, "could not find special device");
			return (-1);
		}
		strlcpy(dev, sfs.f_mntfromname, sizeof(dev));
		name = dev;
	} else {
		ERROR(disk, "could not find special device");
		return (-1);
	}
	fd = open(name, O_RDONLY);
	if (fd == -1) {
		ERROR(disk, "could not open special device");
		return (-1);
	}

	disk->d_bsize = 1;
	disk->d_ccg = 0;
	disk->d_fd = fd;
	disk->d_inoblock = NULL;
	disk->d_inomin = 0;
	disk->d_inomax = 0;
	disk->d_lcg = 0;
	disk->d_mine = 0;
	disk->d_ufs = 0;
	disk->d_error = NULL;
	disk->d_sbcsum = NULL;

	if (oname != name) {
		name = strdup(name);
		if (name == NULL) {
			ERROR(disk, "could not allocate memory for disk name");
			return (-1);
		}
		disk->d_mine |= MINE_NAME;
	}
	disk->d_name = name;

	return (0);
}

/*
	其中一个应用场景就是对超级块数据的写入
*/
int
ufs_disk_write(struct uufsd *disk)
{
	int fd;

	ERROR(disk, NULL);
	/* 首先判断是否为写入操作 */
	if (disk->d_mine & MINE_WRITE)
		return (0);
	/* 判断设备能否正常打开*/
	fd = open(disk->d_name, O_RDWR);
	if (fd < 0) {
		ERROR(disk, "failed to open disk for writing");
		return (-1);
	}
	/* 
		如果打开正常，再执行关闭操作，没有数据写入。所以这里应该只是单纯判断一下
		设备是否正常。最后更新 disk 结构体中的成员
	*/
	close(disk->d_fd);
	disk->d_fd = fd;
	disk->d_mine |= MINE_WRITE;

	return (0);
}
