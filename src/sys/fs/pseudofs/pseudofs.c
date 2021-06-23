/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001 Dag-Erling Coïdan Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: releng/12.0/sys/fs/pseudofs/pseudofs.c 326268 2017-11-27 15:15:37Z pfg $");

#include "opt_pseudofs.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>

#include <fs/pseudofs/pseudofs.h>
#include <fs/pseudofs/pseudofs_internal.h>

static MALLOC_DEFINE(M_PFSNODES, "pfs_nodes", "pseudofs nodes");

SYSCTL_NODE(_vfs, OID_AUTO, pfs, CTLFLAG_RW, 0,
    "pseudofs");

#ifdef PSEUDOFS_TRACE
int pfs_trace;
SYSCTL_INT(_vfs_pfs, OID_AUTO, trace, CTLFLAG_RW, &pfs_trace, 0,
    "enable tracing of pseudofs vnode operations");
#endif

#if PFS_FSNAMELEN != MFSNAMELEN
#error "PFS_FSNAMELEN is not equal to MFSNAMELEN"
#endif

/*
 * Allocate and initialize a node
 * 实例化一个伪文件系统结点
 */
static struct pfs_node *
pfs_alloc_node(struct pfs_info *pi, const char *name, pfs_type_t type)
{
	struct pfs_node *pn;

	KASSERT(strlen(name) < PFS_NAMELEN,
	    ("%s(): node name is too long", __func__));

	/* 申请存储空间 */
	pn = malloc(sizeof *pn,
	    M_PFSNODES, M_WAITOK|M_ZERO);
	/*
		初始化锁，填充name，声明文件类型，填充节点信息
	*/
	mtx_init(&pn->pn_mutex, "pfs_node", NULL, MTX_DEF | MTX_DUPOK);
	strlcpy(pn->pn_name, name, sizeof pn->pn_name);
	pn->pn_type = type;
	pn->pn_info = pi;
	return (pn);
}

/*
 * Add a node to a directory
 * 向一个目录添加一个结点，目录同样也是一个结点
 */
static void
pfs_add_node(struct pfs_node *parent, struct pfs_node *pn)
{
#ifdef INVARIANTS
	struct pfs_node *iter;
#endif

	KASSERT(parent != NULL,
	    ("%s(): parent is NULL", __func__));
	KASSERT(pn->pn_parent == NULL,
	    ("%s(): node already has a parent", __func__));
	KASSERT(parent->pn_info != NULL,
	    ("%s(): parent has no pn_info", __func__));
	KASSERT(parent->pn_type == pfstype_dir ||
	    parent->pn_type == pfstype_procdir ||
	    parent->pn_type == pfstype_root,
	    ("%s(): parent is not a directory", __func__));

#ifdef INVARIANTS
	/* XXX no locking! */
	if (pn->pn_type == pfstype_procdir)
		for (iter = parent; iter != NULL; iter = iter->pn_parent)
			KASSERT(iter->pn_type != pfstype_procdir,
			    ("%s(): nested process directories", __func__));
	for (iter = parent->pn_nodes; iter != NULL; iter = iter->pn_next) {
		KASSERT(strcmp(pn->pn_name, iter->pn_name) != 0,
		    ("%s(): homonymous siblings", __func__));
		if (pn->pn_type == pfstype_procdir)
			KASSERT(iter->pn_type != pfstype_procdir,
			    ("%s(): sibling process directories", __func__));
	}
#endif

	pn->pn_parent = parent;
	pfs_fileno_alloc(pn);	/* 申请一个文件号 */

	pfs_lock(parent);
	/* 将新的结点插入链表(数据结构与算法既视感-。-) */
	pn->pn_next = parent->pn_nodes;
	if ((parent->pn_flags & PFS_PROCDEP) != 0)
		pn->pn_flags |= PFS_PROCDEP;
	parent->pn_nodes = pn;
	pfs_unlock(parent);
}

/*
 * Detach a node from its aprent 将节点与其面板分离
 * 其实就是从将一个结点从链表中删除
 */
static void
pfs_detach_node(struct pfs_node *pn)
{
	struct pfs_node *parent = pn->pn_parent;
	struct pfs_node **iter;

	KASSERT(parent != NULL, ("%s(): node has no parent", __func__));
	KASSERT(parent->pn_info == pn->pn_info,
	    ("%s(): parent has different pn_info", __func__));

	pfs_lock(parent);
	iter = &parent->pn_nodes;
	while (*iter != NULL) {
		if (*iter == pn) {
			*iter = pn->pn_next;
			break;
		}
		iter = &(*iter)->pn_next;
	}
	pn->pn_parent = NULL;
	pfs_unlock(parent);
}

/*
 * Add . and .. to a directory
 * 添加 . 和 .. 两个目录到当前目录当中
 */
static void
pfs_fixup_dir(struct pfs_node *parent)
{
	struct pfs_node *pn;

	pn = pfs_alloc_node(parent->pn_info, ".", pfstype_this);
	pfs_add_node(parent, pn);
	pn = pfs_alloc_node(parent->pn_info, "..", pfstype_parent);
	pfs_add_node(parent, pn);
}

/*
 * Create a directory
 * 下面的函数是创建一个结点文件，这里是创建一个结点目录，本质上是一样的，都是结点
 */
struct pfs_node	*
pfs_create_dir(struct pfs_node *parent, const char *name,
	       pfs_attr_t attr, pfs_vis_t vis, pfs_destroy_t destroy,
	       int flags)
{
	struct pfs_node *pn;

	pn = pfs_alloc_node(parent->pn_info, name,
	    (flags & PFS_PROCDEP) ? pfstype_procdir : pfstype_dir);
	pn->pn_attr = attr;
	pn->pn_vis = vis;
	pn->pn_destroy = destroy;
	pn->pn_flags = flags;
	pfs_add_node(parent, pn);
	pfs_fixup_dir(pn);

	return (pn);
}

/*
 * Create a file
 */
struct pfs_node	*
pfs_create_file(struct pfs_node *parent, const char *name, pfs_fill_t fill,
		pfs_attr_t attr, pfs_vis_t vis, pfs_destroy_t destroy,
		int flags)
{
	struct pfs_node *pn;

	pn = pfs_alloc_node(parent->pn_info, name, pfstype_file);
	pn->pn_fill = fill;
	pn->pn_attr = attr;
	pn->pn_vis = vis;
	pn->pn_destroy = destroy;
	pn->pn_flags = flags;
	pfs_add_node(parent, pn);
	/* 少了添加 dot 和 dotdot 的步骤 */
	return (pn);
}

/*
 * Create a symlink
 */
struct pfs_node	*
pfs_create_link(struct pfs_node *parent, const char *name, pfs_fill_t fill,
		pfs_attr_t attr, pfs_vis_t vis, pfs_destroy_t destroy,
		int flags)
{
	struct pfs_node *pn;
	/* 跟上面函数仅仅是文件类型不同，一个 file，一个是 symlink */
	pn = pfs_alloc_node(parent->pn_info, name, pfstype_symlink);
	pn->pn_fill = fill;
	pn->pn_attr = attr;
	pn->pn_vis = vis;
	pn->pn_destroy = destroy;
	pn->pn_flags = flags;
	pfs_add_node(parent, pn);

	return (pn);
}

/*
 * Locate a node by name 按照名称定位结点
 */
struct pfs_node *
pfs_find_node(struct pfs_node *parent, const char *name)
{
	struct pfs_node *pn;

	pfs_lock(parent);
	for (pn = parent->pn_nodes; pn != NULL; pn = pn->pn_next)
		if (strcmp(pn->pn_name, name) == 0)
			break;
	pfs_unlock(parent);
	return (pn);
}

/*
 * Destroy a node and all its descendants.  If the node to be destroyed
 * has a parent, the parent's mutex must be held.
 * 销毁节点及其所有子节点。如果要销毁的节点有父节点，则必须保留父节点的互斥锁
 */
int
pfs_destroy(struct pfs_node *pn)
{
	struct pfs_node *iter;

	KASSERT(pn != NULL,
	    ("%s(): node is NULL", __func__));
	KASSERT(pn->pn_info != NULL,
	    ("%s(): node has no pn_info", __func__));

	if (pn->pn_parent)
		pfs_detach_node(pn);

	/* destroy children */
	if (pn->pn_type == pfstype_dir ||
	    pn->pn_type == pfstype_procdir ||
	    pn->pn_type == pfstype_root) {
		pfs_lock(pn);
		while (pn->pn_nodes != NULL) {
			iter = pn->pn_nodes;
			pn->pn_nodes = iter->pn_next;
			iter->pn_parent = NULL;
			pfs_unlock(pn);
			pfs_destroy(iter);
			pfs_lock(pn);
		}
		pfs_unlock(pn);
	}

	/* revoke vnodes and fileno */
	pfs_purge(pn);

	/* callback to free any private resources */
	if (pn->pn_destroy != NULL)
		pn_destroy(pn);

	/* destroy the node */
	pfs_fileno_free(pn);
	mtx_destroy(&pn->pn_mutex);
	free(pn, M_PFSNODES);

	return (0);
}

/*
 * Mount a pseudofs instance
 */
int
pfs_mount(struct pfs_info *pi, struct mount *mp)
{
	struct statfs *sbp;

	if (mp->mnt_flag & MNT_UPDATE)
		return (EOPNOTSUPP);

	MNT_ILOCK(mp);
	mp->mnt_flag |= MNT_LOCAL;
	MNT_IUNLOCK(mp);
	mp->mnt_data = pi;
	vfs_getnewfsid(mp);

	sbp = &mp->mnt_stat;
	vfs_mountedfrom(mp, pi->pi_name);
	sbp->f_bsize = PAGE_SIZE;
	sbp->f_iosize = PAGE_SIZE;
	sbp->f_blocks = 1;
	sbp->f_bfree = 0;
	sbp->f_bavail = 0;
	sbp->f_files = 1;
	sbp->f_ffree = 0;

	return (0);
}

/*
 * Compatibility shim for old mount(2) system call
 */
int
pfs_cmount(struct mntarg *ma, void *data, uint64_t flags)
{
	int error;

	error = kernel_mount(ma, flags);
	return (error);
}

/*
 * Unmount a pseudofs instance
 */
int
pfs_unmount(struct mount *mp, int mntflags)
{
	int error;

	error = vflush(mp, 0, (mntflags & MNT_FORCE) ?  FORCECLOSE : 0,
	    curthread);
	return (error);
}

/*
 * Return a root vnode
 */
int
pfs_root(struct mount *mp, int flags, struct vnode **vpp)
{
	struct pfs_info *pi;

	pi = (struct pfs_info *)mp->mnt_data;
	return (pfs_vncache_alloc(mp, vpp, pi->pi_root, NO_PID));
}

/*
 * Return filesystem stats
 */
int
pfs_statfs(struct mount *mp, struct statfs *sbp)
{
	/* no-op:  always called with mp->mnt_stat */
	return (0);
}

/*
 * Initialize a pseudofs instance
 */
int
pfs_init(struct pfs_info *pi, struct vfsconf *vfc)
{
	struct pfs_node *root;
	int error;
	/*
		初始化锁和文件号管理结构体
	*/
	pfs_fileno_init(pi);

	/* set up the root directory 
		建立根节点 "/"，指定文件类型为 pfstype_root
	*/
	root = pfs_alloc_node(pi, "/", pfstype_root);
	pi->pi_root = root;
	pfs_fileno_alloc(root);	/* 分配 file number */
	pfs_fixup_dir(root);	/* 添加 . 和 .. 目录到当前目录下 */

	/* construct file hierarchy */
	error = (pi->pi_init)(pi, vfc);
	if (error) {
		pfs_destroy(root);
		pi->pi_root = NULL;
		return (error);
	}

	if (bootverbose)
		printf("%s registered\n", pi->pi_name);
	return (0);
}

/*
 * Destroy a pseudofs instance
 * 销毁一个 pseudofs 文件系统实例
 */
int
pfs_uninit(struct pfs_info *pi, struct vfsconf *vfc)
{
	int error;

	pfs_destroy(pi->pi_root);
	pi->pi_root = NULL;
	pfs_fileno_uninit(pi);
	if (bootverbose)
		printf("%s unregistered\n", pi->pi_name);
	error = (pi->pi_uninit)(pi, vfc);
	return (error);
}

/*
 * Handle load / unload events
 */
static int
pfs_modevent(module_t mod, int evt, void *arg)
{
	switch (evt) {
	case MOD_LOAD:
		pfs_vncache_load();
		break;
	case MOD_UNLOAD:
	case MOD_SHUTDOWN:
		pfs_vncache_unload();
		break;
	default:
		return EOPNOTSUPP;
		break;
	}
	return 0;
}

/*
 * Module declaration
 */
static moduledata_t pseudofs_data = {
	"pseudofs",
	pfs_modevent,
	NULL
};
DECLARE_MODULE(pseudofs, pseudofs_data, SI_SUB_EXEC, SI_ORDER_FIRST);
MODULE_VERSION(pseudofs, 1);
