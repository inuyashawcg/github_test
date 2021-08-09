/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1992, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2000
 *	Poul-Henning Kamp.  All rights reserved.
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
 *	@(#)kernfs_vfsops.c	8.10 (Berkeley) 5/14/95
 * From: FreeBSD: src/sys/miscfs/kernfs/kernfs_vfsops.c 1.36
 *
 * $FreeBSD: releng/12.0/sys/fs/devfs/devfs_vfsops.c 333263 2018-05-04 20:54:27Z jamie $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/sx.h>
#include <sys/vnode.h>
#include <sys/limits.h>
#include <sys/jail.h>

#include <fs/devfs/devfs.h>

static struct unrhdr	*devfs_unr;

MALLOC_DEFINE(M_DEVFS, "DEVFS", "DEVFS data");

// 定义mount相关功能函数
static vfs_mount_t	devfs_mount;
static vfs_unmount_t	devfs_unmount;
static vfs_root_t	devfs_root;
static vfs_statfs_t	devfs_statfs;

static const char *devfs_opts[] = {
	"from", "export", "ruleset", NULL
};

/*
 * Mount the filesystem
 * 传入的参数 mp 已经在 mountroot 阶段完成了实例化
 */
static int
devfs_mount(struct mount *mp)
{
	int error;
	struct devfs_mount *fmp;
	struct vnode *rvp;	// 关联的 vnode？
	struct thread *td = curthread;
	int injail, rsnum;

	if (devfs_unr == NULL)
		devfs_unr = new_unrhdr(0, INT_MAX, NULL);

	error = 0;

	if (mp->mnt_flag & MNT_ROOTFS)
		return (EOPNOTSUPP);

	rsnum = 0;
	injail = jailed(td->td_ucred);

	// 查看是否有新的属性需要更新
	if (mp->mnt_optnew != NULL) {
		if (vfs_filteropt(mp->mnt_optnew, devfs_opts))
			return (EINVAL);

		if (vfs_flagopt(mp->mnt_optnew, "export", NULL, 0))
			return (EOPNOTSUPP);

		if (vfs_getopt(mp->mnt_optnew, "ruleset", NULL, NULL) == 0 &&
		    (vfs_scanopt(mp->mnt_optnew, "ruleset", "%d",
		    &rsnum) != 1 || rsnum < 0 || rsnum > 65535)) {
			vfs_mount_error(mp, "%s",
			    "invalid ruleset specification");
			return (EINVAL);
		}

		if (injail && rsnum != 0 &&
		    rsnum != td->td_ucred->cr_prison->pr_devfs_rsnum)
			return (EPERM);
	}

	/* jails enforce their ruleset */
	if (injail)
		rsnum = td->td_ucred->cr_prison->pr_devfs_rsnum;

	if (mp->mnt_flag & MNT_UPDATE) {	// 如果仅仅是升级，而不是真正的挂载
		if (rsnum != 0) {
			fmp = mp->mnt_data;
			if (fmp != NULL) {
				sx_xlock(&fmp->dm_lock);
				devfs_ruleset_set((devfs_rsnum)rsnum, fmp);
				devfs_ruleset_apply(fmp);
				sx_xunlock(&fmp->dm_lock);
			}
		}
		return (0);
	}

	/*
		FreeBSD中有一个关于vnode分配的一个机制，所有的fs共用同所有的vnode，看这里的逻辑推测一下，
		unr 很可能就是管理vnode的，每次分配的时候需要看一下还有哪些vnode的是可以被使用的
		为 devfs_mount 结构体分配内存空间，初始化锁，引用计数设置为1
	*/
	fmp = malloc(sizeof *fmp, M_DEVFS, M_WAITOK | M_ZERO);
	fmp->dm_idx = alloc_unr(devfs_unr);
	sx_init(&fmp->dm_lock, "devfsmount");
	fmp->dm_holdcnt = 1;

	MNT_ILOCK(mp);
	mp->mnt_flag |= MNT_LOCAL;
	mp->mnt_kern_flag |= MNTK_LOOKUP_SHARED | MNTK_EXTENDED_SHARED;
#ifdef MAC
	mp->mnt_flag |= MNT_MULTILABEL;
#endif
	MNT_IUNLOCK(mp);
	/* 
		- devfs_mount 函数执行的过程中，malloc 出了一个 devfs_mount 结构体。所以，tptfs 挂载过程中首先要
			实例化出一个 struct tpt_mount
		- mp 其实是在上级 vfs 中的函数调用特定接口来创建的。所以还需要 malloc 一个 struct mount
		- 调用 vfs 函数接口给文件系统分配一个 id
		- 创建目录。目录项其实是跟 vnode 有着一一对应关系的，所以肯定会在某个过程中将两者进行关联。目前这个阶段
			还未涉及到 vnode 的申请(FreeBSD 应该是向 VFS 统一申请 vnode)
		- 然后执行 root 函数，这个函数大概率是将目录项跟 vnode 进行关联的，要详细阅读其实现逻辑
		- 最后执行 vfs_mountedfrom 函数。其实就是对数据的简单拷贝，没有其他特别的操作
	*/
	fmp->dm_mount = mp;
	mp->mnt_data = (void *) fmp;
	// 每一个文件系统都会对应一个唯一的id
	vfs_getnewfsid(mp);

	/* 
		该函数就是执行 malloc 分配内存空间。所以我们重点修改的就是这里，改变成数据块那种形式。
		这里要特别注意 rootdir，看代码感觉应该是"/"。因为 vmkdir 一共执行了三次 devfs_newdirent
		函数，也就是说它其实是创建了三个目录项
	*/
	fmp->dm_rootdir = devfs_vmkdir(fmp, NULL, 0, NULL, DEVFS_ROOTINO);

	/* 
		给devfs 根目录申请一个vnode并赋值给 rvp
		rvp 直到现在都还没有进行过任何一次操作，这里直接传入两个参数 mp 和 rvp，大致可以推测就是
		将 rootdir 跟 vnode 关联在一起的 
	*/
	error = devfs_root(mp, LK_EXCLUSIVE, &rvp);
	if (error) {
		sx_destroy(&fmp->dm_lock);
		free_unr(devfs_unr, fmp->dm_idx);
		free(fmp, M_DEVFS);
		return (error);
	}

	if (rsnum != 0) {
		sx_xlock(&fmp->dm_lock);
		devfs_ruleset_set((devfs_rsnum)rsnum, fmp);	// 配置 rule set
		sx_xunlock(&fmp->dm_lock);
	}

	VOP_UNLOCK(rvp, 0);

	vfs_mountedfrom(mp, "devfs");

	return (0);
}

void
devfs_unmount_final(struct devfs_mount *fmp)
{
	sx_destroy(&fmp->dm_lock);
	free(fmp, M_DEVFS);
}

static int
devfs_unmount(struct mount *mp, int mntflags)
{
	int error;
	int flags = 0;
	struct devfs_mount *fmp;
	int hold;
	u_int idx;

	fmp = VFSTODEVFS(mp);
	KASSERT(fmp->dm_mount != NULL,
		("devfs_unmount unmounted devfs_mount"));
	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;
	/* There is 1 extra root vnode reference from devfs_mount(). */
	error = vflush(mp, 1, flags, curthread);
	if (error)
		return (error);
	sx_xlock(&fmp->dm_lock);
	devfs_cleanup(fmp);
	devfs_rules_cleanup(fmp);
	fmp->dm_mount = NULL;
	hold = --fmp->dm_holdcnt;
	mp->mnt_data = NULL;
	idx = fmp->dm_idx;
	sx_xunlock(&fmp->dm_lock);
	free_unr(devfs_unr, idx);
	if (hold == 0)
		devfs_unmount_final(fmp);
	return 0;
}

/* Return locked reference to root.  
	从代码逻辑可以看到，devfs_root 函数其实一共就做了一件事，获取vnode
*/

static int
devfs_root(struct mount *mp, int flags, struct vnode **vpp)
{
	int error;
	struct vnode *vp;
	struct devfs_mount *dmp;

	dmp = VFSTODEVFS(mp);
	sx_xlock(&dmp->dm_lock);

	/* 
		从 vnode list 中找到一个free项给devfs用。devfs_mount 函数中，root_dir 已经构造完毕。它包含有
		三个部分：第一个
	*/
	error = devfs_allocv(dmp->dm_rootdir, mp, LK_EXCLUSIVE, &vp);
	if (error)
		return (error);
	vp->v_vflag |= VV_ROOT;	/* 设置 vnode 属性信息，将该vnode标记为文件系统的root */
	*vpp = vp;
	return (0);
}

static int
devfs_statfs(struct mount *mp, struct statfs *sbp)
{

	sbp->f_flags = 0;
	sbp->f_bsize = DEV_BSIZE;
	sbp->f_iosize = DEV_BSIZE;
	sbp->f_blocks = 2;		/* 1K to keep df happy */
	sbp->f_bfree = 0;
	sbp->f_bavail = 0;
	sbp->f_files = 0;
	sbp->f_ffree = 0;
	return (0);
}

static struct vfsops devfs_vfsops = {
	.vfs_mount =		devfs_mount,
	.vfs_root =		devfs_root,
	.vfs_statfs =		devfs_statfs,
	.vfs_unmount =		devfs_unmount,
};

VFS_SET(devfs_vfsops, devfs, VFCF_SYNTHETIC | VFCF_JAIL);
