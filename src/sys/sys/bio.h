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
 *	@(#)buf.h	8.9 (Berkeley) 3/30/95
 * $FreeBSD: releng/12.0/sys/sys/bio.h 335066 2018-06-13 16:48:07Z imp $
 */

#ifndef _SYS_BIO_H_
#define	_SYS_BIO_H_

#include <sys/queue.h>
#include <sys/disk_zone.h>

/* bio_cmd 
	其实就是说明这个 bio 到底是用来干嘛的，里边存放了哪些用途的数据
*/
#define BIO_READ	0x01	/* Read I/O data */
#define BIO_WRITE	0x02	/* Write I/O data */

/* 表示某一特定范围的数据不再使用，并且可以根据基础技术的支持将其擦除或释放 */
#define BIO_DELETE	0x03	/* TRIM or free blocks, i.e. mark as unused */
#define BIO_GETATTR	0x04	/* Get GEOM attributes of object 获取 GEOM 的属性 */
#define BIO_FLUSH	0x05	/* Commit outstanding I/O now 告诉底层提供者刷新它们的写缓存 */
#define BIO_CMD0	0x06	/* Available for local hacks */
#define BIO_CMD1	0x07	/* Available for local hacks */
#define BIO_CMD2	0x08	/* Available for local hacks */
#define BIO_ZONE	0x09	/* Zone command */

/* bio_flags */
/* Request failed (error value is stored in bio_error field) */
#define BIO_ERROR	0x01	/* An error occurred processing this bio. */
#define BIO_DONE	0x02	/* This bio is finished. Request finished */
#define BIO_ONQUEUE	0x04	/* This bio is in a queue & not yet taken. */
/*
 * This bio must be executed after all previous bios in the queue have been
 * executed, and before any successive bios can be executed.
 */
#define BIO_ORDERED	0x08
#define	BIO_UNMAPPED	0x10
#define	BIO_TRANSIENT_MAPPING	0x20
#define	BIO_VLIST	0x40

#ifdef _KERNEL
struct disk;
struct bio;
struct vm_map;

/* Empty classifier tag, to prevent further classification. 
	空分类器标签，防止进一步分类
*/
#define	BIO_NOTCLASSIFIED		(void *)(~0UL)

typedef void bio_task_t(void *);

/*
 * The bio structure describes an I/O operation in the kernel.
 * bio: block device I/O operation
 * bio 结构用于表示一个以块为中心的输入输出请求。粗略来说，当内核需要向存储设备写数据或者
 * 从其中读取数据时，它就会组装一个bio结构以描述该操作并将其传递给合适的驱动程序
 * 新接收到的bio将由策略例程来处理。Linux中会对新产生的request(包含bio)进行排序和合并，
 * 用于提高磁盘处理数据的效率，这里的作用应该也是类似的。
 */
struct bio {
	uint16_t bio_cmd;		/* I/O operation. I/O请求命令，即输入输出标志 */
	uint16_t bio_flags;		/* General flags. 通用标志 */
	uint16_t bio_cflags;		/* Private use by the consumer. 消费者私用 */
	uint16_t bio_pflags;		/* Private use by the provider. 提供者私用 */
	struct cdev *bio_dev;		/* Device to do I/O on. 表示执行输入输出的设备 */
	struct disk *bio_disk;		/* Valid below geom_disk.c only 指向disk的指针 */
	off_t	bio_offset;		/* Offset into file. 文件中的请求位置 - Offset into	provider */
	long	bio_bcount;		/* Valid bytes in buffer. 有效字节数 */
	caddr_t	bio_data;		/* Memory, superblocks, indirect etc. - Pointer to data buffer 需要传输的数据 */
	struct vm_page **bio_ma;	/* Or unmapped. */
	int	bio_ma_offset;		/* Offset in the first page of bio_ma. */
	int	bio_ma_n;		/* Number of pages in bio_ma. */
	int	bio_error;		/* Errno for BIO_ERROR. */
	long	bio_resid;		/* Remaining I/O in bytes. 遗留的输入输出(以字节为单位) */

	/* Pointer to function	which will be called when the requestis finished. */
	void	(*bio_done)(struct bio *);	/* biodone 处理程序 */
	void	*bio_driver1;		/* Private use by the provider. */
	void	*bio_driver2;		/* Private use by the provider. */
	void	*bio_caller1;		/* Private use by the consumer. */
	void	*bio_caller2;		/* Private use by the consumer. */
	TAILQ_ENTRY(bio) bio_queue;	/* Disksort queue. bioq链接 */
	const char *bio_attribute;	/* Attribute for BIO_[GS]ETATTR */
	struct  disk_zone_args bio_zone;/* Used for BIO_ZONE */

	/* Consumer to	use for	request	(attached to provider stored
		    in bio_to field) (typically	read-only for a	class) 
	*/
	struct g_consumer *bio_from;	/* GEOM linkage */

	/* Destination	provider (typically read-only for a class) */
	struct g_provider *bio_to;	/* GEOM linkage */
	off_t	bio_length;		/* Like bio_bcount - Request length in bytes.有效的数据长度 */

	/*
		Number of bytes completed, but they	may not	be completed
		    from the front of the request.
	*/
	off_t	bio_completed;		/* Inverse of bio_resid 与bio_resid相反，表示的应该是处理完的数据长度 */

	/* Number of bio clones (typically read-only for a class) */
	u_int	bio_children;		/* Number of spawned bios - 衍生出的bio，调用 GEOM bio_clone 函数的时候会增加计数 */
	u_int	bio_inbed;		/* Children safely home by now - Number of finished bio clones 子主目录个数？ */
	struct bio *bio_parent;		/* Pointer to parent bio 指向父节点的指针 */
	struct bintime bio_t0;		/* Time request started 输入输出请求的启动时间 */

	bio_task_t *bio_task;		/* Task_queue handler - bio_taskqueue处理程序 */
	void	*bio_task_arg;		/* Argument to above - bio_task的参数 */

	void	*bio_classifier1;	/* Classifier tag. 分类器标志 */
	void	*bio_classifier2;	/* Classifier tag. 分类器标志 */

#ifdef DIAGNOSTIC
	void	*_bio_caller1;
	void	*_bio_caller2;
	uint8_t	_bio_cflags;
#endif
#if defined(BUF_TRACKING) || defined(FULL_BUF_TRACKING)
	struct buf *bio_track_bp;	/* Parent buf for tracking */
#endif

	/* XXX: these go away when bio chaining is introduced */
	daddr_t bio_pblkno;               /* physical block number 物理块号 */
};

struct uio;
struct devstat;

struct bio_queue_head {
	TAILQ_HEAD(bio_queue, bio) queue;
	off_t last_offset;
	struct	bio *insert_point;
	int total;
	int batched;
};

extern struct vm_map *bio_transient_map;
extern int bio_transient_maxcnt;

void biodone(struct bio *bp);
/*
	biodone 函数会通知内核服务已经完成；biofinish 函数会将bp设置为所返回的错误码。也就是说，
	biofinish 函数可以告诉内核bp究竟是无效请求或是已经成功服务或者未成功服务等等
*/
void biofinish(struct bio *bp, struct devstat *stat, int error);
int biowait(struct bio *bp, const char *wchan);

#if defined(BUF_TRACKING) || defined(FULL_BUF_TRACKING)
void biotrack_buf(struct bio *bp, const char *location);

static __inline void
biotrack(struct bio *bp, const char *location)
{

	if (bp->bio_track_bp != NULL)
		biotrack_buf(bp, location);
}
#else
static __inline void
biotrack(struct bio *bp __unused, const char *location __unused)
{
}
#endif

/*
	所有的驱动程序都会维护一个块输入输出队列(block I/O queue，bio queue)以存放尚未处理的
	以块为中心的输入输出请求。一般而言，这些请求将按照设备偏移升序或者降序排序以便在对它们进行
	处理的时，磁头只做单向运动(这样才能使得性能最大化)
*/
void bioq_disksort(struct bio_queue_head *ap, struct bio *bp);
struct bio *bioq_first(struct bio_queue_head *head);
struct bio *bioq_takefirst(struct bio_queue_head *head);
void bioq_flush(struct bio_queue_head *head, struct devstat *stp, int error);
void bioq_init(struct bio_queue_head *head);
void bioq_insert_head(struct bio_queue_head *head, struct bio *bp);
void bioq_insert_tail(struct bio_queue_head *head, struct bio *bp);
void bioq_remove(struct bio_queue_head *head, struct bio *bp);

int	physio(struct cdev *dev, struct uio *uio, int ioflag);
#define physread physio
#define physwrite physio

#endif /* _KERNEL */

#endif /* !_SYS_BIO_H_ */
