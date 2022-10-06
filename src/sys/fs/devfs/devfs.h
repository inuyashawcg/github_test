/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2000
 *	Poul-Henning Kamp.  All rights reserved.
 * Copyright (c) 2002
 *	Dima Dorfman.  All rights reserved.
 *
 * This code is derived from software donated to Berkeley by
 * Jan-Simon Pendry.
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
 *	@(#)kernfs.h	8.6 (Berkeley) 3/29/95
 * From: FreeBSD: src/sys/miscfs/kernfs/kernfs.h 1.14
 *
 * $FreeBSD: releng/12.0/sys/fs/devfs/devfs.h 326268 2017-11-27 15:15:37Z pfg $
 */

#ifndef _FS_DEVFS_DEVFS_H_
#define	_FS_DEVFS_DEVFS_H_

#define	DEVFS_MAGIC	0xdb0a087a

/*
 * Identifiers.  The ruleset and rule numbers are 16-bit values.  The
 * "rule ID" is a combination of the ruleset and rule number; it
 * should be able to univocally describe a rule in the system.  In
 * this implementation, the upper 16 bits of the rule ID is the
 * ruleset number; the lower 16 bits, the rule number within the
 * aforementioned ruleset.
 * 
 * 标识符。规则集和规则编号是16位值。“规则ID”是规则集和规则编号的组合；它应该能够以
 * 单音形式描述系统中的规则。在此实现中，规则ID的上16位是规则集编号；下16位是上述规则
 * 集中的规则编号
 */
typedef uint16_t devfs_rnum;	// rule number
typedef uint16_t devfs_rsnum;	// rule set number
typedef uint32_t devfs_rid;

/*
 * Identifier manipulators. 标识符操纵器
 */
#define	rid2rsn(rid)	((rid) >> 16)
#define	rid2rn(rid)	((rid) & 0xffff)
#define	mkrid(rsn, rn)	((rn) | ((rsn) << 16))

/*
 * Plain DEVFS rule.  This gets shared between kernel and userland
 * verbatim, so it shouldn't contain any pointers or other kernel- or
 * userland-specific values.
 * 普通 DEVFS 规则。这在内核和 userland 之间逐字共享，因此它不应该包含任何指针或其他
 * 内核或 userland 特定的值
 */
struct devfs_rule {
	uint32_t dr_magic;			/* Magic number. */
	devfs_rid dr_id;			/* Identifier. */

	/*
	 * Conditions under which this rule should be applied.  These
	 * are ANDed together since OR can be simulated by using
	 * multiple rules.  dr_icond determines which of the other
	 * variables we should process.
	 * 适用本规则的条件。由于可以使用多个规则进行模拟，因此这些规则被合并在一起。
	 * dr_icond 决定了我们应该处理的其他变量
	 */
	int	dr_icond;
#define	DRC_DSWFLAGS	0x001
#define	DRC_PATHPTRN	0x002
	int	dr_dswflags;			/* cdevsw flags to match. 要匹配的 cdevsw 标志 */
#define	DEVFS_MAXPTRNLEN	200
	char	dr_pathptrn[DEVFS_MAXPTRNLEN];	/* Pattern to match path. 匹配路径的模式 */

	/*
	 * Things to change.  dr_iacts determines which of the other
	 * variables we should process. 需要改变的事情。dr_iacts 决定了我们应该处理哪些其他变量。
	 */
	int	dr_iacts;
#define	DRA_BACTS	0x001
#define	DRA_UID		0x002
#define	DRA_GID		0x004
#define	DRA_MODE	0x008
#define	DRA_INCSET	0x010
	int	dr_bacts;			/* Boolean (on/off) action. */
#define	DRB_HIDE	0x001			/* Hide entry (DE_WHITEOUT). */
#define	DRB_UNHIDE	0x002			/* Unhide entry. */
	uid_t	dr_uid;
	gid_t	dr_gid;
	mode_t	dr_mode;
	devfs_rsnum dr_incset;			/* Included ruleset. */
};

/*
 * Rule-related ioctls.
 */
#define	DEVFSIO_RADD		_IOWR('D', 0, struct devfs_rule)
#define	DEVFSIO_RDEL		_IOW('D', 1, devfs_rid)
#define	DEVFSIO_RAPPLY		_IOW('D', 2, struct devfs_rule)
#define	DEVFSIO_RAPPLYID	_IOW('D', 3, devfs_rid)
#define	DEVFSIO_RGETNEXT       	_IOWR('D', 4, struct devfs_rule)

#define	DEVFSIO_SUSE		_IOW('D', 10, devfs_rsnum)
#define	DEVFSIO_SAPPLY		_IOW('D', 11, devfs_rsnum)
#define	DEVFSIO_SGETNEXT	_IOWR('D', 12, devfs_rsnum)

/* XXX: DEVFSIO_RS_GET_INFO for refcount, active if any, etc. */

#ifdef _KERNEL

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_DEVFS);
#endif

struct componentname;

TAILQ_HEAD(devfs_dlist_head, devfs_dirent);

/* 
	dirent: directory entry? FreeBSD文件系统好像是将文件和目录进行了分类，单独处理。
	感觉还是因为每个文件系统对于文件的管理方式有差异。这里其实就是基类和派生类的问题， dirent
	可以当做是所有文件系统中的目录项的一个基类，不同的文件系统对目录的特殊化管理就可以在此基础上
	进行派生
*/ 
struct devfs_dirent {
	struct cdev_priv	*de_cdp;
	int			de_inode;	// 对应的inode结点id？
	int			de_flags;
#define	DE_WHITEOUT	0x01
#define	DE_DOT		0x02	// .
#define	DE_DOTDOT	0x04	// ..
#define	DE_DOOMED	0x08	// 回收？
/*
	这里有一个应用场景，当我们通过 name 查找目录的时候，发现这个以这个name命名的文件是一个链接文件，
	那么我们还要进一步去查找对应的目录文件实体；如果没有找到，那就需要重新创建一个目录，然后把这个
	目录文件的 flag 设置成这个类型
*/
#define	DE_COVERED	0x10	// 覆盖？
#define	DE_USER		0x20
	int			de_holdcnt;
	/*
		/usr/src/sys/sys/dirent.h 文件中有关于 dirent 结构体的定义，它一般会
		出现在目录项的头部位置，里面会存放目录项的inode number、size和name length等信息
	*/
	struct dirent 		*de_dirent;
	TAILQ_ENTRY(devfs_dirent) de_list;
	struct devfs_dlist_head	de_dlist;	// 子目录列表
	struct devfs_dirent	*de_dir;	// entry 所在的目录
	int			de_links;
	mode_t			de_mode;
	uid_t			de_uid;
	gid_t			de_gid;
	struct label		*de_label;	// 好像表示的是存储的格式
	struct timespec 	de_atime;	// 应该表示的是文件创建、修改的时间
	struct timespec 	de_mtime;
	struct timespec 	de_ctime;
	struct vnode 		*de_vnode;	// 对应的vnode
	char 			*de_symlink;	// 符号链接
};

/*
	devfs 挂载点配置信息
	这里的设计思路跟上述类似，里边也是包含了一个 mount 结构体指针。为什么要这么来设计呢，应该就是借鉴
	C++ 中的类继承机制。不同的文件系统挂载点所包含的信息也是不同的，所以 Qihai 对 mount 进行封装时，
	还是以 mount 作为基类，不同的文件系统的挂载点对其进行派生 
*/ 
struct devfs_mount {
	u_int			dm_idx;	// mount index
	struct mount		*dm_mount;	// 对应的mount结构体
	struct devfs_dirent	*dm_rootdir;	// 挂载点所在的根目录
	unsigned		dm_generation;
	int			dm_holdcnt;
	struct sx		dm_lock;	// shared mutex 共享锁
	devfs_rsnum		dm_ruleset;
};

#define DEVFS_ROOTINO 2

extern unsigned devfs_rule_depth;	// 表示目录深度限制？

/* 把mount结构体中的 mnt_data 强制转换成 devfs_mount 类型 */
#define VFSTODEVFS(mp)	((struct devfs_mount *)((mp)->mnt_data))

#define DEVFS_DE_HOLD(de)	((de)->de_holdcnt++)
#define DEVFS_DE_DROP(de)	(--(de)->de_holdcnt == 0)

#define DEVFS_DMP_HOLD(dmp)	((dmp)->dm_holdcnt++)
#define DEVFS_DMP_DROP(dmp)	(--(dmp)->dm_holdcnt == 0)

#define	DEVFS_DEL_VNLOCKED	0x01
#define	DEVFS_DEL_NORECURSE	0x02

void	devfs_rules_apply(struct devfs_mount *, struct devfs_dirent *);
void	devfs_rules_cleanup(struct devfs_mount *);
int	devfs_rules_ioctl(struct devfs_mount *, u_long, caddr_t,
	    struct thread *);
void	devfs_ruleset_set(devfs_rsnum rsnum, struct devfs_mount *dm);
void	devfs_ruleset_apply(struct devfs_mount *dm);
int	devfs_allocv(struct devfs_dirent *, struct mount *, int,
	    struct vnode **);
char	*devfs_fqpn(char *, struct devfs_mount *, struct devfs_dirent *,
	    struct componentname *);
void	devfs_delete(struct devfs_mount *, struct devfs_dirent *, int);
void	devfs_dirent_free(struct devfs_dirent *);
void	devfs_populate(struct devfs_mount *);
void	devfs_cleanup(struct devfs_mount *);
void	devfs_unmount_final(struct devfs_mount *);
struct devfs_dirent	*devfs_newdirent(char *, int);
struct devfs_dirent	*devfs_parent_dirent(struct devfs_dirent *);
struct devfs_dirent	*devfs_vmkdir(struct devfs_mount *, char *, int,
			    struct devfs_dirent *, u_int);
struct devfs_dirent	*devfs_find(struct devfs_dirent *, const char *, int,
			    int);

#endif /* _KERNEL */

#endif /* !_FS_DEVFS_DEVFS_H_ */
