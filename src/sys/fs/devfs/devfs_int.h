/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Poul-Henning Kamp.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Neither the name of the University nor the names of its contributors
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
 * $FreeBSD: releng/12.0/sys/fs/devfs/devfs_int.h 326268 2017-11-27 15:15:37Z pfg $
 */

/*
 * This file documents a private interface and it SHALL only be used
 * by kern/kern_conf.c and fs/devfs/...
 */

#ifndef _FS_DEVFS_DEVFS_INT_H_
#define	_FS_DEVFS_DEVFS_INT_H_

#include <sys/queue.h>

#ifdef _KERNEL

struct devfs_dirent;
struct devfs_mount;

struct cdev_privdata {
	struct file		*cdpd_fp;	// 对应的file
	void			*cdpd_data;	// cdev private data 
	void			(*cdpd_dtr)(void *);	// 对应的函数指针
	LIST_ENTRY(cdev_privdata) cdpd_list;	// cdev_privdata 也是队列管理？
};

/* 
	cdev 私有数据？ cdevp_list 是用来存放 cdev_priv 结构体的一个队列，cdev_priv 中又包含
	有指向 cdev 的指针。说明 cdev_priv 就是一个对 cdev 进行全方位管理的这么一个结构体，所以
	devfs 在创建 cdev 的时候会一定会创建一个 cdev_priv
*/
struct cdev_priv {
	struct cdev		cdp_c;	// 指向对应的cdev
	TAILQ_ENTRY(cdev_priv)	cdp_list;	// 这个是 cdev_priv 队列管理，下面得是data管理

	u_int			cdp_inode;	// 对应inode id?

	u_int			cdp_flags;
#define CDP_ACTIVE		(1 << 0)
#define CDP_SCHED_DTR		(1 << 1)
#define	CDP_UNREF_DTR		(1 << 2)

	u_int			cdp_inuse;	// in use 使用中标志？
	u_int			cdp_maxdirent;
	/*
		cdp_dirents 保存的可能是所有子目录的指针变量。比如 /dev 可以认为是一个cdev，
		它下面肯定会有很多子目录，该成员可能就用来管理这些目录
	*/
	struct devfs_dirent	**cdp_dirents;
	struct devfs_dirent	*cdp_dirent0;

	TAILQ_ENTRY(cdev_priv)	cdp_dtr_list;	// cdev_priv 也是队列管理？
	void			(*cdp_dtr_cb)(void *);
	void			*cdp_dtr_cb_arg;

	/* 
		cdp_fdpriv 是 cdev_privdata 管理队列的 header 指针
	*/
	LIST_HEAD(, cdev_privdata) cdp_fdpriv;	// fd: 文件描述符相关
};

#define	cdev2priv(c)	__containerof(c, struct cdev_priv, cdp_c)

struct cdev	*devfs_alloc(int);
int	devfs_dev_exists(const char *);
void	devfs_free(struct cdev *);
void	devfs_create(struct cdev *);
void	devfs_destroy(struct cdev *);
void	devfs_destroy_cdevpriv(struct cdev_privdata *);

int	devfs_dir_find(const char *);
void	devfs_dir_ref_de(struct devfs_mount *, struct devfs_dirent *);
void	devfs_dir_unref_de(struct devfs_mount *, struct devfs_dirent *);
int	devfs_pathpath(const char *, const char *);

extern struct unrhdr *devfs_inos;
extern struct mtx devmtx;
extern struct mtx devfs_de_interlock;
extern struct sx clone_drain_lock;
extern struct mtx cdevpriv_mtx;
extern TAILQ_HEAD(cdev_priv_list, cdev_priv) cdevp_list;

#endif /* _KERNEL */

#endif /* !_FS_DEVFS_DEVFS_INT_H_ */
