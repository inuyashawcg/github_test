/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Poul-Henning Kamp
 * All rights reserved.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: releng/12.0/sys/kern/vfs_hash.c 326271 2017-11-27 15:20:12Z pfg $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/rwlock.h>
#include <sys/vnode.h>

static MALLOC_DEFINE(M_VFS_HASH, "vfs_hash", "VFS hash table");

static LIST_HEAD(vfs_hash_head, vnode)	*vfs_hash_tbl;	// vnode 链表
static LIST_HEAD(,vnode)		vfs_hash_side;
static u_long				vfs_hash_mask;
static struct rwlock			vfs_hash_lock;	// 全局读写锁

static void
vfs_hashinit(void *dummy __unused)
{
	// 初始化 hash table，desiredvnodes 表示的是数量
	vfs_hash_tbl = hashinit(desiredvnodes, M_VFS_HASH, &vfs_hash_mask);
	rw_init(&vfs_hash_lock, "vfs hash"); // hash table 锁初始化
	LIST_INIT(&vfs_hash_side);	// hash table 中的每一个元素又是一个 vnode list？
}

/* Must be SI_ORDER_SECOND so desiredvnodes is available */
SYSINIT(vfs_hash, SI_SUB_VFS, SI_ORDER_SECOND, vfs_hashinit, NULL);

u_int
vfs_hash_index(struct vnode *vp)
{
	/*
		在 struct vnode 的定义的注释中可以看到，vnode hash = mount + inode，v_hash表示
		的是这个含义？每个挂载点都会随机生成一个 hashseed，这样就保证即使两个文件系统中的 vnode
		计算出了相同的 hash 值，通过加上 hashseed 之后仍然能够保证 hash 值是唯一的，不会发生冲突
	*/
	return (vp->v_hash + vp->v_mount->mnt_hashseed);
}

/*
	返回的应该是一个vnode list，mount->mnt_hashseed + hash 计算出的值表示的是 hash table
	的下标，然后下标对应的元素是一个指向 vnode 链表的一个指针
*/
static struct vfs_hash_head *
vfs_hash_bucket(const struct mount *mp, u_int hash)
{

	return (&vfs_hash_tbl[(hash + mp->mnt_hashseed) & vfs_hash_mask]);
}

/*
	主要逻辑就是通过 hash 值在哈希表中找到某个 vnode，所以可以推断一下，hash table 里边其实就是存放的 vnode，
	我们要找的时候，就利用 mount 和外部提供的某个 hash 值来确定它位于哪个元素(vnode list)，然后在根据相应的
	属性进行匹配
	在 ext2 文件系统中，hash 传入的是 inode number。
*/
int
vfs_hash_get(const struct mount *mp, u_int hash, int flags, struct thread *td,
    struct vnode **vpp, vfs_hash_cmp_t *fn, void *arg)
{
	struct vnode *vp;
	int error;

	while (1) {
		rw_rlock(&vfs_hash_lock);
		LIST_FOREACH(vp, vfs_hash_bucket(mp, hash), v_hashlist) {
			/*
				vfs hashtable 中的 hash 索引的计算方式比较简单，可能就是 inode number + mount hashseed。
				这种的话就很容易造成冲突。这里采用的方式就是遍历整个链表中的元素，对比每个元素中的的 hash 和 mount
				结构是否一致，最终确定哪个元素是我们要找的目标对象
			*/
			if (vp->v_hash != hash)
				continue;
			if (vp->v_mount != mp)
				continue;
			if (fn != NULL && fn(vp, arg))
				continue;

			/* 遍历整个 vnode list，找到符合上述条件的 vnode */
			vhold(vp);
			rw_runlock(&vfs_hash_lock);
			error = vget(vp, flags | LK_VNHELD, td);	// 从 freelist 中移除 vp
			if (error == ENOENT && (flags & LK_NOWAIT) == 0)
				break;
			if (error)
				return (error);
			*vpp = vp;
			return (0);
		}	// end foreach

		if (vp == NULL) {
			rw_runlock(&vfs_hash_lock);
			*vpp = NULL;
			return (0);
		}
	}	// end while
}

// 貌似只有 nfs client 中才会用到
void
vfs_hash_ref(const struct mount *mp, u_int hash, struct thread *td,
    struct vnode **vpp, vfs_hash_cmp_t *fn, void *arg)
{
	struct vnode *vp;

	while (1) {
		rw_rlock(&vfs_hash_lock);
		LIST_FOREACH(vp, vfs_hash_bucket(mp, hash), v_hashlist) {
			if (vp->v_hash != hash)
				continue;
			if (vp->v_mount != mp)
				continue;
			if (fn != NULL && fn(vp, arg))
				continue;
			vhold(vp);
			rw_runlock(&vfs_hash_lock);
			vref(vp);
			vdrop(vp);
			*vpp = vp;
			return;
		}
		if (vp == NULL) {
			rw_runlock(&vfs_hash_lock);
			*vpp = NULL;
			return;
		}
	}
}

void
vfs_hash_remove(struct vnode *vp)
{

	rw_wlock(&vfs_hash_lock);
	LIST_REMOVE(vp, v_hashlist);
	rw_wunlock(&vfs_hash_lock);
}

int
vfs_hash_insert(struct vnode *vp, u_int hash, int flags, struct thread *td,
    struct vnode **vpp, vfs_hash_cmp_t *fn, void *arg)
{
	struct vnode *vp2;
	int error;

	*vpp = NULL;
	while (1) {
		rw_wlock(&vfs_hash_lock);
		/*
			首先要遍历找到的那个 vnode 链表，判断链表中是否已经存在
		*/
		LIST_FOREACH(vp2,
		    vfs_hash_bucket(vp->v_mount, hash), v_hashlist) {
			if (vp2->v_hash != hash)
				continue;
			if (vp2->v_mount != vp->v_mount)
				continue;
			if (fn != NULL && fn(vp2, arg))
				continue;
			vhold(vp2);
			rw_wunlock(&vfs_hash_lock);
			error = vget(vp2, flags | LK_VNHELD, td);
			if (error == ENOENT && (flags & LK_NOWAIT) == 0)
				break;
			rw_wlock(&vfs_hash_lock);
			LIST_INSERT_HEAD(&vfs_hash_side, vp, v_hashlist);
			rw_wunlock(&vfs_hash_lock);
			vput(vp);
			if (!error)
				*vpp = vp2;
			return (error);
		}	// end foreach
		if (vp2 == NULL)
			break;
			
	}	// end while
	vp->v_hash = hash;
	LIST_INSERT_HEAD(vfs_hash_bucket(vp->v_mount, hash), vp, v_hashlist);
	rw_wunlock(&vfs_hash_lock);
	return (0);
}

void
vfs_hash_rehash(struct vnode *vp, u_int hash)
{

	rw_wlock(&vfs_hash_lock);
	LIST_REMOVE(vp, v_hashlist);
	LIST_INSERT_HEAD(vfs_hash_bucket(vp->v_mount, hash), vp, v_hashlist);
	vp->v_hash = hash;
	rw_wunlock(&vfs_hash_lock);
}

void
vfs_hash_changesize(int newmaxvnodes)
{
	struct vfs_hash_head *vfs_hash_newtbl, *vfs_hash_oldtbl;
	u_long vfs_hash_newmask, vfs_hash_oldmask;
	struct vnode *vp;
	int i;

	vfs_hash_newtbl = hashinit(newmaxvnodes, M_VFS_HASH,
		&vfs_hash_newmask);
	/* If same hash table size, nothing to do */
	if (vfs_hash_mask == vfs_hash_newmask) {
		free(vfs_hash_newtbl, M_VFS_HASH);
		return;
	}
	/*
	 * Move everything from the old hash table to the new table.
	 * None of the vnodes in the table can be recycled because to
	 * do so, they have to be removed from the hash table.
	 */
	rw_wlock(&vfs_hash_lock);
	vfs_hash_oldtbl = vfs_hash_tbl;
	vfs_hash_oldmask = vfs_hash_mask;
	vfs_hash_tbl = vfs_hash_newtbl;
	vfs_hash_mask = vfs_hash_newmask;
	for (i = 0; i <= vfs_hash_oldmask; i++) {
		while ((vp = LIST_FIRST(&vfs_hash_oldtbl[i])) != NULL) {
			LIST_REMOVE(vp, v_hashlist);
			LIST_INSERT_HEAD(
			    vfs_hash_bucket(vp->v_mount, vp->v_hash),
			    vp, v_hashlist);
		}
	}
	rw_wunlock(&vfs_hash_lock);
	free(vfs_hash_oldtbl, M_VFS_HASH);
}
