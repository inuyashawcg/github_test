/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003 Poul-Henning Kamp
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Poul-Henning Kamp
 * and NAI Labs, the Security Research Division of Network Associates, Inc.
 * under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the
 * DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: releng/12.0/sys/geom/geom_disk.h 327996 2018-01-15 11:20:00Z avg $
 */

#ifndef _GEOM_GEOM_DISK_H_
#define _GEOM_GEOM_DISK_H_

#define	DISK_RR_UNKNOWN		0
#define	DISK_RR_NON_ROTATING	1
#define	DISK_RR_MIN		0x0401
#define	DISK_RR_MAX		0xfffe

#ifdef _KERNEL 

#include <sys/queue.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>
#include <sys/disk.h>

#define G_DISK_CLASS_NAME	"DISK"

struct disk;

typedef	int	disk_open_t(struct disk *);
typedef	int	disk_close_t(struct disk *);
typedef	void	disk_strategy_t(struct bio *bp);
typedef	int	disk_getattr_t(struct bio *bp);
typedef	void	disk_gone_t(struct disk *);
typedef	int	disk_ioctl_t(struct disk *, u_long cmd, void *data,
			int fflag, struct thread *td);
		/* NB: disk_ioctl_t SHALL be cast'able to d_ioctl_t */

struct g_geom;
struct devstat;

typedef enum {
	DISK_INIT_NONE,
	DISK_INIT_START,
	DISK_INIT_DONE
} disk_init_level;

struct disk_alias {
	LIST_ENTRY(disk_alias)	da_next;
	const char		*da_alias;
};

struct disk {
	/* Fields which are private to geom_disk */
	struct g_geom		*d_geom;
	struct devstat		*d_devstat;
	int			d_goneflag;
	int			d_destroyed;
	disk_init_level		d_init_level;

	/* Shared fields */
	u_int			d_flags;
	const char		*d_name;	// 存储设备的名字，必须指定
	u_int			d_unit;	// 存储设备的单位号，必须指定
	struct bio_queue_head	*d_queue;	/* 指向一个I/O请求队列 */
	struct mtx		*d_lock;

	/* Disk methods  */
	disk_open_t		*d_open;
	disk_close_t		*d_close;

	/*
		指定存储设备的策略例程。策略例程用于处理以块为中心的读、写或者其他输入输出操作。
		因此，所有的disk结构都应定义该字段
	*/
	disk_strategy_t		*d_strategy;
	disk_ioctl_t		*d_ioctl;	// 该字段可选择性定义

	/*
		指定存储设备的转储例程。内核报错的时候会调用该例程将物理内存的内容转储到存储设备之上。
		要注意该字段也是可选的，因而很可能是未定义的
	*/
	dumper_t		*d_dump;
	disk_getattr_t		*d_getattr;
	disk_gone_t		*d_gone;

	/* Info fields from driver to geom_disk.c. Valid when open 
		从driver到geom_disk.c的信息字段。打开时有效
	*/
	u_int			d_sectorsize;	// 表示存储设备的扇区的大小，必须指定
	off_t			d_mediasize;	// 媒体字节的大小？必须指定
	u_int			d_fwsectors;	/* 指定此存储设备上的扇区数，必须要指定 */
	u_int			d_fwheads;	/* 指定此存储设备上的磁头数，必须要指定 */
	u_int			d_maxsize;	// 表示存储设备中一条输入输出操作的最大字节数，必须指定
	off_t			d_delmaxsize;
	u_int			d_stripeoffset;
	u_int			d_stripesize;
	char			d_ident[DISK_IDENT_SIZE];	// 存储设备的序列号，可选的
	char			d_descr[DISK_IDENT_SIZE];
	uint16_t		d_hba_vendor;
	uint16_t		d_hba_device;
	uint16_t		d_hba_subvendor;
	uint16_t		d_hba_subdevice;
	uint16_t		d_rotation_rate;

	/* Fields private to the driver */
	void			*d_drv1;	/* 驱动程序私有数据指针，一般指向softc结构体 */

	/* Fields private to geom_disk, to be moved on next version bump */
	LIST_HEAD(,disk_alias)	d_aliases;
};

#define	DISKFLAG_RESERVED		0x0001	/* Was NEEDSGIANT */
#define	DISKFLAG_OPEN			0x0002
#define	DISKFLAG_CANDELETE		0x0004
#define	DISKFLAG_CANFLUSHCACHE		0x0008
#define	DISKFLAG_UNMAPPED_BIO		0x0010
#define	DISKFLAG_DIRECT_COMPLETION	0x0020
#define	DISKFLAG_CANZONE		0x0080
#define	DISKFLAG_WRITE_PROTECT		0x0100

struct disk *disk_alloc(void);
void disk_create(struct disk *disk, int version);
void disk_destroy(struct disk *disk);
void disk_gone(struct disk *disk);
void disk_attr_changed(struct disk *dp, const char *attr, int flag);
void disk_media_changed(struct disk *dp, int flag);
void disk_media_gone(struct disk *dp, int flag);
int disk_resize(struct disk *dp, int flag);
void disk_add_alias(struct disk *disk, const char *);

#define DISK_VERSION_00		0x58561059
#define DISK_VERSION_01		0x5856105a
#define DISK_VERSION_02		0x5856105b
#define DISK_VERSION_03		0x5856105c
#define DISK_VERSION_04		0x5856105d
#define DISK_VERSION_05		0x5856105e
#define DISK_VERSION		DISK_VERSION_05

#endif /* _KERNEL */
#endif /* _GEOM_GEOM_DISK_H_ */
