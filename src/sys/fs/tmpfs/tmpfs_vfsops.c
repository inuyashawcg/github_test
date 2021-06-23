/*	$NetBSD: tmpfs_vfsops.c,v 1.10 2005/12/11 12:24:29 christos Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2005 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Julio M. Merino Vidal, developed as part of Google's Summer of Code
 * 2005 program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Efficient memory file system.
 *
 * tmpfs is a file system that uses FreeBSD's virtual memory
 * sub-system to store file data and metadata in an efficient way.
 * This means that it does not follow the structure of an on-disk file
 * system because it simply does not need to.  Instead, it uses
 * memory-specific data structures and algorithms to automatically
 * allocate and release resources.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD: releng/12.0/sys/fs/tmpfs/tmpfs_vfsops.c 341085 2018-11-27 17:58:25Z markj $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/dirent.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/rwlock.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_param.h>

#include <fs/tmpfs/tmpfs.h>

/*
 * Default permission for root node 根节点默认权限
 */
#define TMPFS_DEFAULT_ROOT_MODE	(S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH)

MALLOC_DEFINE(M_TMPFSMNT, "tmpfs mount", "tmpfs mount structures");
MALLOC_DEFINE(M_TMPFSNAME, "tmpfs name", "tmpfs file names");

static int	tmpfs_mount(struct mount *);
static int	tmpfs_unmount(struct mount *, int);
static int	tmpfs_root(struct mount *, int flags, struct vnode **);
static int	tmpfs_fhtovp(struct mount *, struct fid *, int,
		    struct vnode **);
static int	tmpfs_statfs(struct mount *, struct statfs *);
static void	tmpfs_susp_clean(struct mount *);

/*
	指定 tmpfs 所拥有的一些属性信息，mount的时候会判断挂载的文进系统是不是具有这些属性
*/
static const char *tmpfs_opts[] = {
	"from", "size", "maxfilesize", "inodes", "uid", "gid", "mode", "export",
	"union", "nonc", NULL
};

static const char *tmpfs_updateopts[] = {
	"from", "export", NULL
};

/*
	uma_zcreate 函数在创建存储区的时候会调用该函数，用于初始化一些数据。
	这里的作用对象是 tmpfs_node
*/
static int
tmpfs_node_ctor(void *mem, int size, void *arg, int flags)
{
	struct tmpfs_node *node = (struct tmpfs_node *)mem;

	node->tn_gen++;
	node->tn_size = 0;
	node->tn_status = 0;
	node->tn_flags = 0;
	node->tn_links = 0;
	node->tn_vnode = NULL;
	node->tn_vpstate = 0;

	return (0);
}

/* 仅仅从来设置结点类型，应用场景同上 */
static void
tmpfs_node_dtor(void *mem, int size, void *arg)
{
	struct tmpfs_node *node = (struct tmpfs_node *)mem;
	node->tn_type = VNON;
}

/* tmpfs_node 初始化，应用场景同上 */
static int
tmpfs_node_init(void *mem, int size, int flags)
{
	struct tmpfs_node *node = (struct tmpfs_node *)mem;
	node->tn_id = 0;

	mtx_init(&node->tn_interlock, "tmpfs node interlock", NULL, MTX_DEF);
	node->tn_gen = arc4random();

	return (0);
}

/*
	UMA 机制释放一块内存区域的时候调用该函数，做一些清理工作
*/
static void
tmpfs_node_fini(void *mem, int size)
{
	struct tmpfs_node *node = (struct tmpfs_node *)mem;

	mtx_destroy(&node->tn_interlock);
}

/*
	要点:
		- 内存文件系统需要占用较多的内存空间，所以需要对其进行限制，保证操作系统能够正常运行
*/
static int
tmpfs_mount(struct mount *mp)
{
	/* 计算一个page中能装多少 dirent 和 node */
	const size_t nodes_per_page = howmany(PAGE_SIZE,
	    sizeof(struct tmpfs_dirent) + sizeof(struct tmpfs_node));
	struct tmpfs_mount *tmp;
	struct tmpfs_node *root;
	int error;
	bool nonc;
	/* Size counters. */
	u_quad_t pages;
	off_t nodes_max, size_max, maxfilesize;

	/* Root node attributes. 根节点属性 */
	uid_t root_uid;
	gid_t root_gid;
	mode_t root_mode;

	struct vattr va;

	if (vfs_filteropt(mp->mnt_optnew, tmpfs_opts))
		return (EINVAL);

	if (mp->mnt_flag & MNT_UPDATE) {
		/* Only support update mounts for certain options. */
		if (vfs_filteropt(mp->mnt_optnew, tmpfs_updateopts) != 0)
			return (EOPNOTSUPP);
		if (vfs_flagopt(mp->mnt_optnew, "ro", NULL, 0) !=
		    ((struct tmpfs_mount *)mp->mnt_data)->tm_ronly)
			return (EOPNOTSUPP);
		return (0);
	}

	vn_lock(mp->mnt_vnodecovered, LK_SHARED | LK_RETRY);
	/* 获取挂载点对应的vnode的属性信息，然后跟指定的属性信息列表作对比 */
	error = VOP_GETATTR(mp->mnt_vnodecovered, &va, mp->mnt_cred);
	VOP_UNLOCK(mp->mnt_vnodecovered, 0);
	if (error)
		return (error);

	if (mp->mnt_cred->cr_ruid != 0 ||
	    vfs_scanopt(mp->mnt_optnew, "gid", "%d", &root_gid) != 1)
		root_gid = va.va_gid;
	if (mp->mnt_cred->cr_ruid != 0 ||
	    vfs_scanopt(mp->mnt_optnew, "uid", "%d", &root_uid) != 1)
		root_uid = va.va_uid;
	if (mp->mnt_cred->cr_ruid != 0 ||
	    vfs_scanopt(mp->mnt_optnew, "mode", "%ho", &root_mode) != 1)
		root_mode = va.va_mode;
	if (vfs_getopt_size(mp->mnt_optnew, "inodes", &nodes_max) != 0)
		nodes_max = 0;
	if (vfs_getopt_size(mp->mnt_optnew, "size", &size_max) != 0)
		size_max = 0;
	if (vfs_getopt_size(mp->mnt_optnew, "maxfilesize", &maxfilesize) != 0)
		maxfilesize = 0;
	nonc = vfs_getopt(mp->mnt_optnew, "nonc", NULL, NULL) == 0;

	/* Do not allow mounts if we do not have enough memory to preserve
	 * the minimum reserved pages. 
	 * 如果没有足够的内存来保留最小的保留页，则不允许装载。所以这个函数的作用应该是计算
	 * tmpfs装载之后，操作系统剩余的内存页的数量
	 * */
	if (tmpfs_mem_avail() < TMPFS_PAGES_MINRESERVED)
		return (ENOSPC);

	/* Get the maximum number of memory pages this file system is
	 * allowed to use, based on the maximum size the user passed in
	 * the mount structure.  A value of zero is treated as if the
	 * maximum available space was requested. 
	 * 根据用户在mount结构体中传递的最大size，获取这个文件系统被允许使用的最大内存页数量。
	 * 如果值为零，则视为请求了最大可用空间
	 * */
	if (size_max == 0 || size_max > OFF_MAX - PAGE_SIZE ||
	    (SIZE_MAX < OFF_MAX && size_max / PAGE_SIZE >= SIZE_MAX))
		pages = SIZE_MAX;
	else {
		size_max = roundup(size_max, PAGE_SIZE);
		pages = howmany(size_max, PAGE_SIZE);
	}
	MPASS(pages > 0);

	if (nodes_max <= 3) {
		if (pages < INT_MAX / nodes_per_page)
			nodes_max = pages * nodes_per_page;
		else
			nodes_max = INT_MAX;
	}
	if (nodes_max > INT_MAX)
		nodes_max = INT_MAX;
	MPASS(nodes_max >= 3);

	/* Allocate the tmpfs mount structure and fill it. 实例化 tmpfs_mount 结构体 */
	tmp = (struct tmpfs_mount *)malloc(sizeof(struct tmpfs_mount),
	    M_TMPFSMNT, M_WAITOK | M_ZERO);

	mtx_init(&tmp->tm_allnode_lock, "tmpfs allnode lock", NULL, MTX_DEF);	/* 初始化锁 */
	tmp->tm_nodes_max = nodes_max;	/* 指定挂载点最大结点数 */
	tmp->tm_nodes_inuse = 0;	/* 初始化被使用结点数为0 */
	tmp->tm_refcount = 1;	/* 挂载点的引用次数 */
	tmp->tm_maxfilesize = maxfilesize > 0 ? maxfilesize : OFF_MAX;	/* 文件最大是多少 */
	LIST_INIT(&tmp->tm_nodes_used);	/* 初始化结点链表 */

	tmp->tm_pages_max = pages;	/* 最大能够占用多少内存页 */
	tmp->tm_pages_used = 0;	/* 初始化已经被占用的内存页数量为0 */
	tmp->tm_ino_unr = new_unrhdr(2, INT_MAX, &tmp->tm_allnode_lock);	/* 分配inode编号，调用前面初始化的锁 */
	tmp->tm_dirent_pool = uma_zcreate("TMPFS dirent",
	    sizeof(struct tmpfs_dirent), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);	/* 创建目录UMA结构体 */
	tmp->tm_node_pool = uma_zcreate("TMPFS node",
	    sizeof(struct tmpfs_node), tmpfs_node_ctor, tmpfs_node_dtor,
	    tmpfs_node_init, tmpfs_node_fini, UMA_ALIGN_PTR, 0); /* 创建结点UMA结构体 */
	tmp->tm_ronly = (mp->mnt_flag & MNT_RDONLY) != 0;	/* 设置文件系统访问属性 */
	tmp->tm_nonc = nonc;	/* 设置是否使用 namecache 缓存机制 */

	/* Allocate the root node. 根据上面获取到的属性信息实例化根节点 
		这里申请的结点传入的参数是 tmpfs_node，而不是 vnode，也就是说申请的是tmpfs node，这一点要注意
	*/
	error = tmpfs_alloc_node(mp, tmp, VDIR, root_uid, root_gid,
	    root_mode & ALLPERMS, NULL, NULL, VNOVAL, &root);

	/*
		一旦出错就必须要做相应处理，最典型的就是申请到的资源要释放掉。以后进行软件设计的时候要注意
		这一点，不能忽略
	*/
	if (error != 0 || root == NULL) {
		uma_zdestroy(tmp->tm_node_pool);
		uma_zdestroy(tmp->tm_dirent_pool);
		delete_unrhdr(tmp->tm_ino_unr);
		free(tmp, M_TMPFSMNT);
		return (error);
	}
	KASSERT(root->tn_id == 2,
	    ("tmpfs root with invalid ino: %ju", (uintmax_t)root->tn_id));
	tmp->tm_root = root;	/* 指定文件系统挂载的根节点 */

	MNT_ILOCK(mp);
	mp->mnt_flag |= MNT_LOCAL;
	mp->mnt_kern_flag |= MNTK_LOOKUP_SHARED | MNTK_EXTENDED_SHARED;
	MNT_IUNLOCK(mp);

	/*
		仔细看这一步的操作，是把 tmpfs_mount 赋值给 mount->mnt_data。mnt_data 在注释中说的表示 private data，
		说明该成员存储的应该是不同的文件系统所对应的mount结构体。进一步推测，函数mount指针参数很可能是空的，通过挂载
		相当于是做了一个实例化操作，然后虚拟文件系统再根据其中数据成员的内容判定该文件系统是哪种类型的、挂载到什么地方
		等等，然后再进行 id 分配等操作
	*/
	mp->mnt_data = tmp;
	mp->mnt_stat.f_namemax = MAXNAMLEN;
	/*
		最后会调用 vfs 提供的一些接口，首先是给 tmpfs 分一个文件系统id，然后调用 vfs_mountedfrom，搜索一下可以发现，
		很多文件系统mount操作都有相同的操作，所以这个应该就是 vfs 中的某个管理机制
	*/
	vfs_getnewfsid(mp);	/* 设置 mnt_stat->f_fsid.val[*] */
	vfs_mountedfrom(mp, "tmpfs");	/* 设置 mnt_stat 成员中包含的挂载的文件系统名称 */

	return 0;
}

/* ARGSUSED2 */
static int
tmpfs_unmount(struct mount *mp, int mntflags)
{
	struct tmpfs_mount *tmp;
	struct tmpfs_node *node;
	int error, flags;

	/*
		MNT_FORCE：强制 unmount
	*/
	flags = (mntflags & MNT_FORCE) != 0 ? FORCECLOSE : 0;
	tmp = VFS_TO_TMPFS(mp);

	/* Stop writers 停止向挂载点写入数据 */
	error = vfs_write_suspend_umnt(mp);
	if (error != 0)
		return (error);
	/*
	 * At this point, nodes cannot be destroyed by any other
	 * thread because write suspension is started.
	 * 此时，节点无法被任何其他线程销毁，因为写入暂停已启动
	 */

	for (;;) {
		error = vflush(mp, 0, flags, curthread);
		if (error != 0) {
			vfs_write_resume(mp, VR_START_WRITE);
			return (error);
		}
		MNT_ILOCK(mp);
		if (mp->mnt_nvnodelistsize == 0) {
			MNT_IUNLOCK(mp);
			break;
		}
		MNT_IUNLOCK(mp);
		if ((mntflags & MNT_FORCE) == 0) {
			vfs_write_resume(mp, VR_START_WRITE);
			return (EBUSY);
		}
	}

	TMPFS_LOCK(tmp);
	while ((node = LIST_FIRST(&tmp->tm_nodes_used)) != NULL) {
		TMPFS_NODE_LOCK(node);
		if (node->tn_type == VDIR)
			tmpfs_dir_destroy(tmp, node);
		if (tmpfs_free_node_locked(tmp, node, true))
			TMPFS_LOCK(tmp);
		else
			TMPFS_NODE_UNLOCK(node);
	}

	mp->mnt_data = NULL;
	tmpfs_free_tmp(tmp);
	vfs_write_resume(mp, VR_START_WRITE);

	MNT_ILOCK(mp);
	mp->mnt_flag &= ~MNT_LOCAL;
	MNT_IUNLOCK(mp);

	return (0);
}

void
tmpfs_free_tmp(struct tmpfs_mount *tmp)
{

	MPASS(tmp->tm_refcount > 0);
	tmp->tm_refcount--;
	if (tmp->tm_refcount > 0) {
		TMPFS_UNLOCK(tmp);	/* 当引用计数大于0时，返回；后续肯定是要有其他操作 */
		return;
	}
	TMPFS_UNLOCK(tmp);

	/* UMA 和 unrhdr 释放过程参考这里 */
	uma_zdestroy(tmp->tm_dirent_pool);
	uma_zdestroy(tmp->tm_node_pool);

	clear_unrhdr(tmp->tm_ino_unr);
	delete_unrhdr(tmp->tm_ino_unr);

	mtx_destroy(&tmp->tm_allnode_lock);	/* 释放锁 */
	MPASS(tmp->tm_pages_used == 0);	
	MPASS(tmp->tm_nodes_inuse == 0);

	free(tmp, M_TMPFSMNT);	/* free 掉 tmpfs_mount */
}

static int
tmpfs_root(struct mount *mp, int flags, struct vnode **vpp)
{
	int error;
	/*
		tmpfs_alloc_vp 会创建一个新的 vnode，然后保存到 vpp
	*/
	error = tmpfs_alloc_vp(mp, VFS_TO_TMPFS(mp)->tm_root, flags, vpp);
	if (error == 0)
		(*vpp)->v_vflag |= VV_ROOT;
	return (error);
}

static int
tmpfs_fhtovp(struct mount *mp, struct fid *fhp, int flags,
    struct vnode **vpp)
{
	struct tmpfs_fid *tfhp;
	struct tmpfs_mount *tmp;
	struct tmpfs_node *node;
	int error;

	tmp = VFS_TO_TMPFS(mp);

	/* 
		强制类型转换: fid -> tmpfs_fid。对照两个结构体的成员构成，两者之间的对应关系应该是
		fid_len -> tf_len;
		fid_data0 -> tf_pad;
		char fid_data[16] -> tf_id + tf_gen，后面两个成员数据长度加起来应该是 16 bytes
	*/
	tfhp = (struct tmpfs_fid *)fhp;
	if (tfhp->tf_len != sizeof(struct tmpfs_fid))
		return (EINVAL);

	if (tfhp->tf_id >= tmp->tm_nodes_max)
		return (EINVAL);

	TMPFS_LOCK(tmp);	/* 对 tmpfs_mount 加锁，然后才能访问其中的成员链表 */
	LIST_FOREACH(node, &tmp->tm_nodes_used, tn_entries) {
		if (node->tn_id == tfhp->tf_id &&
		    node->tn_gen == tfhp->tf_gen) {
			tmpfs_ref_node(node);
			/*
				可能的应用场景就是: 用户通过 fid 参数来访问某个结点，然后VFS经过处理定位到了是 tmpfs 文件系统中的
				结点，然后调用该函数匹配结点。如果找到了这个结点，并且处于被使用状态，引用计数++，表示又多了一个访问者
			*/
			break;
		}
	}
	TMPFS_UNLOCK(tmp);

	if (node != NULL) {
		/* 如果找到了，就为该结点分配一个 vnode */
		error = tmpfs_alloc_vp(mp, node, LK_EXCLUSIVE, vpp);
		tmpfs_free_node(tmp, node);	/* node 其实只是一个局部变量，使用完之后就可以释放掉 */
	} else
		error = EINVAL;
	return (error);
}

/* ARGSUSED2 */
static int
tmpfs_statfs(struct mount *mp, struct statfs *sbp)
{
	struct tmpfs_mount *tmp;
	size_t used;

	tmp = VFS_TO_TMPFS(mp);

	/* 文件系统片段和数据传送块的大小都设置为 PAGE_SIZE */
	sbp->f_iosize = PAGE_SIZE;
	sbp->f_bsize = PAGE_SIZE;

	used = tmpfs_pages_used(tmp);
	if (tmp->tm_pages_max != ULONG_MAX)
		 sbp->f_blocks = tmp->tm_pages_max;
	else
		 sbp->f_blocks = used + tmpfs_mem_avail();
	if (sbp->f_blocks <= used)
		sbp->f_bavail = 0;
	else
		sbp->f_bavail = sbp->f_blocks - used;
	sbp->f_bfree = sbp->f_bavail;
	used = tmp->tm_nodes_inuse;
	sbp->f_files = tmp->tm_nodes_max;
	if (sbp->f_files <= used)
		sbp->f_ffree = 0;
	else
		sbp->f_ffree = sbp->f_files - used;
	/* sbp->f_owner = tmp->tn_uid; */

	return 0;
}

static int
tmpfs_sync(struct mount *mp, int waitfor)
{
	struct vnode *vp, *mvp;
	struct vm_object *obj;
	/*
		MNT_SUSPEND：同步后挂起文件系统
	*/
	if (waitfor == MNT_SUSPEND) {
		MNT_ILOCK(mp);
		mp->mnt_kern_flag |= MNTK_SUSPEND2 | MNTK_SUSPENDED;
		MNT_IUNLOCK(mp);
	} else if (waitfor == MNT_LAZY) {
		/*
		 * Handle lazy updates of mtime from writes to mmaped
		 * regions.  Use MNT_VNODE_FOREACH_ALL instead of
		 * MNT_VNODE_FOREACH_ACTIVE, since unmap of the
		 * tmpfs-backed vnode does not call vinactive(), due
		 * to vm object type is OBJT_SWAP.
		 */
		MNT_VNODE_FOREACH_ALL(vp, mp, mvp) {
			if (vp->v_type != VREG) {
				VI_UNLOCK(vp);
				continue;
			}
			obj = vp->v_object;
			KASSERT((obj->flags & (OBJ_TMPFS_NODE | OBJ_TMPFS)) ==
			    (OBJ_TMPFS_NODE | OBJ_TMPFS), ("non-tmpfs obj"));

			/*
			 * Unlocked read, avoid taking vnode lock if
			 * not needed.  Lost update will be handled on
			 * the next call.
			 * 未锁定读取，如果不需要，请避免使用vnode锁。下次调用时将处理丢失的更新
			 * OBJ_TMPFS_DIRTY 表示的应该是脏页，如果没有脏页，就继续返回循环起始位置；
			 * 如果是脏页，就执行后续操作
			 */
			if ((obj->flags & OBJ_TMPFS_DIRTY) == 0) {
				VI_UNLOCK(vp);
				continue;
			}
			if (vget(vp, LK_EXCLUSIVE | LK_RETRY | LK_INTERLOCK,
			    curthread) != 0)
				continue;
			tmpfs_check_mtime(vp);
			vput(vp);
		}
	}
	return (0);
}

/*
 * The presence of a susp_clean method tells the VFS to track writes.
 * susp_clean 方法的存在告诉 VFS 跟踪写操作
 */
static void
tmpfs_susp_clean(struct mount *mp __unused)
{
}

/*
 * tmpfs vfs operations.
 */

struct vfsops tmpfs_vfsops = {
	.vfs_mount =			tmpfs_mount,
	.vfs_unmount =			tmpfs_unmount,
	.vfs_root =			tmpfs_root,
	.vfs_statfs =			tmpfs_statfs,
	.vfs_fhtovp =			tmpfs_fhtovp,
	.vfs_sync =			tmpfs_sync,
	.vfs_susp_clean =		tmpfs_susp_clean,
};
VFS_SET(tmpfs_vfsops, tmpfs, VFCF_JAIL);
