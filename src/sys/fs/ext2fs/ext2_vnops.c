/*-
 *  modified for EXT2FS support in Lites 1.1
 *
 *  Aug 1995, Godmar Back (gback@cs.utah.edu)
 *  University of Utah, Department of Computer Science
 */
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
 *	@(#)ufs_vnops.c	8.7 (Berkeley) 2/3/94
 *	@(#)ufs_vnops.c 8.27 (Berkeley) 5/27/95
 * $FreeBSD: releng/12.0/sys/fs/ext2fs/ext2_vnops.c 338150 2018-08-21 18:39:02Z fsu $
 */

#include "opt_suiddir.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/filio.h>
#include <sys/stat.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/endian.h>
#include <sys/priv.h>
#include <sys/rwlock.h>
#include <sys/mount.h>
#include <sys/unistd.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/namei.h>
#include <sys/lockf.h>
#include <sys/event.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/extattr.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vnode_pager.h>

#include "opt_directio.h"

#include <ufs/ufs/dir.h>

#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/inode.h>
#include <fs/ext2fs/ext2_acl.h>
#include <fs/ext2fs/ext2fs.h>
#include <fs/ext2fs/ext2_extern.h>
#include <fs/ext2fs/ext2_dinode.h>
#include <fs/ext2fs/ext2_dir.h>
#include <fs/ext2fs/ext2_mount.h>
#include <fs/ext2fs/ext2_extattr.h>
#include <fs/ext2fs/ext2_extents.h>

static int ext2_makeinode(int mode, struct vnode *, struct vnode **, struct componentname *);
static void ext2_itimes_locked(struct vnode *);

static vop_access_t	ext2_access;
static int ext2_chmod(struct vnode *, int, struct ucred *, struct thread *);
static int ext2_chown(struct vnode *, uid_t, gid_t, struct ucred *,
    struct thread *);
static vop_close_t	ext2_close;
static vop_create_t	ext2_create;
static vop_fsync_t	ext2_fsync;
static vop_getattr_t	ext2_getattr;
static vop_ioctl_t	ext2_ioctl;
static vop_link_t	ext2_link;
static vop_mkdir_t	ext2_mkdir;
static vop_mknod_t	ext2_mknod;
static vop_open_t	ext2_open;
static vop_pathconf_t	ext2_pathconf;
static vop_print_t	ext2_print;
static vop_read_t	ext2_read;
static vop_readlink_t	ext2_readlink;
static vop_remove_t	ext2_remove;
static vop_rename_t	ext2_rename;
static vop_rmdir_t	ext2_rmdir;
static vop_setattr_t	ext2_setattr;
static vop_strategy_t	ext2_strategy;
static vop_symlink_t	ext2_symlink;
static vop_write_t	ext2_write;
static vop_deleteextattr_t	ext2_deleteextattr;
static vop_getextattr_t	ext2_getextattr;
static vop_listextattr_t	ext2_listextattr;
static vop_setextattr_t	ext2_setextattr;
static vop_vptofh_t	ext2_vptofh;
static vop_close_t	ext2fifo_close;
static vop_kqfilter_t	ext2fifo_kqfilter;

/* Global vfs data structures for ext2. */
struct vop_vector ext2_vnodeops = {
	.vop_default =		&default_vnodeops,
	.vop_access =		ext2_access,
	.vop_bmap =		ext2_bmap,
	.vop_cachedlookup =	ext2_lookup,
	.vop_close =		ext2_close,
	.vop_create =		ext2_create,
	.vop_fsync =		ext2_fsync,
	.vop_getpages =		vnode_pager_local_getpages,
	.vop_getpages_async =	vnode_pager_local_getpages_async,
	.vop_getattr =		ext2_getattr,
	.vop_inactive =		ext2_inactive,
	.vop_ioctl =		ext2_ioctl,
	.vop_link =		ext2_link,
	.vop_lookup =		vfs_cache_lookup,
	.vop_mkdir =		ext2_mkdir,
	.vop_mknod =		ext2_mknod,
	.vop_open =		ext2_open,
	.vop_pathconf =		ext2_pathconf,
	.vop_poll =		vop_stdpoll,
	.vop_print =		ext2_print,
	.vop_read =		ext2_read,
	.vop_readdir =		ext2_readdir,
	.vop_readlink =		ext2_readlink,
	.vop_reallocblks =	ext2_reallocblks,
	.vop_reclaim =		ext2_reclaim,
	.vop_remove =		ext2_remove,
	.vop_rename =		ext2_rename,
	.vop_rmdir =		ext2_rmdir,
	.vop_setattr =		ext2_setattr,
	.vop_strategy =		ext2_strategy,
	.vop_symlink =		ext2_symlink,
	.vop_write =		ext2_write,
	.vop_deleteextattr =	ext2_deleteextattr,
	.vop_getextattr =	ext2_getextattr,
	.vop_listextattr =	ext2_listextattr,
	.vop_setextattr =	ext2_setextattr,
#ifdef UFS_ACL
	.vop_getacl =		ext2_getacl,
	.vop_setacl =		ext2_setacl,
	.vop_aclcheck =		ext2_aclcheck,
#endif /* UFS_ACL */
	.vop_vptofh =		ext2_vptofh,
};

struct vop_vector ext2_fifoops = {
	.vop_default =		&fifo_specops,
	.vop_access =		ext2_access,
	.vop_close =		ext2fifo_close,
	.vop_fsync =		ext2_fsync,
	.vop_getattr =		ext2_getattr,
	.vop_inactive =		ext2_inactive,
	.vop_kqfilter =		ext2fifo_kqfilter,
	.vop_pathconf =		ext2_pathconf,
	.vop_print =		ext2_print,
	.vop_read =		VOP_PANIC,
	.vop_reclaim =		ext2_reclaim,
	.vop_setattr =		ext2_setattr,
	.vop_write =		VOP_PANIC,
	.vop_vptofh =		ext2_vptofh,
};

/*
 * A virgin directory (no blushing please).
 * Note that the type and namlen fields are reversed relative to ext2.
 * Also, we don't use `struct odirtemplate', since it would just cause
 * endianness problems.
 */
static struct dirtemplate mastertemplate = {
	0, 12, 1, EXT2_FT_DIR, ".",
	0, DIRBLKSIZ - 12, 2, EXT2_FT_DIR, ".."
};
static struct dirtemplate omastertemplate = {
	0, 12, 1, EXT2_FT_UNKNOWN, ".",
	0, DIRBLKSIZ - 12, 2, EXT2_FT_UNKNOWN, ".."
};

/*
	记录 inode 相关的一些时间信息
*/
static void
ext2_itimes_locked(struct vnode *vp)
{
	struct inode *ip;
	struct timespec ts;

	ASSERT_VI_LOCKED(vp, __func__);
	/*
		修改 inode 与时间相关的字段，最后更新一下属性信息
	*/
	ip = VTOI(vp);
	if ((ip->i_flag & (IN_ACCESS | IN_CHANGE | IN_UPDATE)) == 0)
		return;
	if ((vp->v_type == VBLK || vp->v_type == VCHR))
		ip->i_flag |= IN_LAZYMOD;
	else
		ip->i_flag |= IN_MODIFIED;
	if ((vp->v_mount->mnt_flag & MNT_RDONLY) == 0) {
		vfs_timestamp(&ts);
		if (ip->i_flag & IN_ACCESS) {
			ip->i_atime = ts.tv_sec;
			ip->i_atimensec = ts.tv_nsec;
		}
		if (ip->i_flag & IN_UPDATE) {
			ip->i_mtime = ts.tv_sec;
			ip->i_mtimensec = ts.tv_nsec;
			ip->i_modrev++;
		}
		if (ip->i_flag & IN_CHANGE) {
			ip->i_ctime = ts.tv_sec;
			ip->i_ctimensec = ts.tv_nsec;
		}
	}
	ip->i_flag &= ~(IN_ACCESS | IN_CHANGE | IN_UPDATE);
}

void
ext2_itimes(struct vnode *vp)
{
	/* 调用上面的函数，更新 inode 的时间数据 */
	VI_LOCK(vp);
	ext2_itimes_locked(vp);
	VI_UNLOCK(vp);
}

/*
 * Create a regular file
 * 创建一个常规文件。从文件类型定义来看，ext2 中仍然包含有 block_device 文件，这个还是要注意
 */
static int
ext2_create(struct vop_create_args *ap)
{
	int error;
	/* 
		创建一个 inode。这里的 MAKEIMODE 其实是处理来自用户空间的、应该是所要创建的文件的属性信息，
		后续的处理中如果出现了错误，可能就会导致 ls 命令提示无法识别文件类型的错误，要留意一下
	*/
	error =
	    ext2_makeinode(MAKEIMODE(ap->a_vap->va_type, ap->a_vap->va_mode),
	    ap->a_dvp, ap->a_vpp, ap->a_cnp);
	if (error != 0)
		return (error);
	/* 判断是否利用将名字添加到 name cache */
	if ((ap->a_cnp->cn_flags & MAKEENTRY) != 0)
		cache_enter(ap->a_dvp, *ap->a_vpp, ap->a_cnp);	// 将新添加的目录项放入 namecache 中
	return (0);
}

static int
ext2_open(struct vop_open_args *ap)
{
	/*
		如果打开的文件是字符设备或者块设备，则返回“不支持该操作”
	*/
	if (ap->a_vp->v_type == VBLK || ap->a_vp->v_type == VCHR)
		return (EOPNOTSUPP);

	/*
	 * Files marked append-only must be opened for appending.
	 * 标记为“仅附加”的文件必须打开才能附加
	 */
	if ((VTOI(ap->a_vp)->i_flags & APPEND) &&
	    (ap->a_mode & (FWRITE | O_APPEND)) == FWRITE)
		return (EPERM);
	/*
		为 vnode 分配一个 vm_object 用于管理page
	*/
	vnode_create_vobject(ap->a_vp, VTOI(ap->a_vp)->i_size, ap->a_td);

	return (0);
}

/*
 * Close called.
 *
 * Update the times on the inode. 
 * 更新 inode 的时间信息
 */
static int
ext2_close(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp;

	VI_LOCK(vp);
	if (vp->v_usecount > 1)
		ext2_itimes_locked(vp);
	VI_UNLOCK(vp);
	return (0);
}

/* 一般涉及到 access 的函数，都是对访问权限的检查 */
static int
ext2_access(struct vop_access_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	accmode_t accmode = ap->a_accmode;
	int error;

	if (vp->v_type == VBLK || vp->v_type == VCHR)
		return (EOPNOTSUPP);

	/*
	 * Disallow write attempts on read-only file systems;
	 * unless the file is a socket, fifo, or a block or
	 * character device resident on the file system.
	 * 禁止在只读文件系统上尝试写操作；除非文件是驻留在文件系统上的
	 * 套接字、fifo或块或字符设备
	 * 
	 */
	if (accmode & VWRITE) {
		switch (vp->v_type) {
		case VDIR:
		case VLNK:
		case VREG:
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);
			break;
		default:
			break;
		}
	}

	/* If immutable bit set, nobody gets to write it. 
		如果设置了不可变位，就没有人可以编写它
	*/
	if ((accmode & VWRITE) && (ip->i_flags & (SF_IMMUTABLE | SF_SNAPSHOT)))
		return (EPERM);

	/* 检查 vnode 的访问属性 */
	error = vaccess(vp->v_type, ip->i_mode, ip->i_uid, ip->i_gid,
	    ap->a_accmode, ap->a_cred, NULL);
	return (error);
}

/*
	获取vnode的属性信息(有一个专门的结构体进行存储，vattr)。里边基本上都是赋值操作，
	对结构体的字段进行填充
*/
static int
ext2_getattr(struct vop_getattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	struct vattr *vap = ap->a_vap;	/* vnode属性信息 */

	ext2_itimes(vp);
	/*
	 * Copy from inode table
	 */
	vap->va_fsid = dev2udev(ip->i_devvp->v_rdev);
	vap->va_fileid = ip->i_number;
	vap->va_mode = ip->i_mode & ~IFMT;
	vap->va_nlink = ip->i_nlink;
	vap->va_uid = ip->i_uid;
	vap->va_gid = ip->i_gid;
	vap->va_rdev = ip->i_rdev;
	vap->va_size = ip->i_size;
	vap->va_atime.tv_sec = ip->i_atime;
	vap->va_atime.tv_nsec = E2DI_HAS_XTIME(ip) ? ip->i_atimensec : 0;
	vap->va_mtime.tv_sec = ip->i_mtime;
	vap->va_mtime.tv_nsec = E2DI_HAS_XTIME(ip) ? ip->i_mtimensec : 0;
	vap->va_ctime.tv_sec = ip->i_ctime;
	vap->va_ctime.tv_nsec = E2DI_HAS_XTIME(ip) ? ip->i_ctimensec : 0;
	if E2DI_HAS_XTIME(ip) {
		vap->va_birthtime.tv_sec = ip->i_birthtime;
		vap->va_birthtime.tv_nsec = ip->i_birthnsec;
	}
	vap->va_flags = ip->i_flags;
	vap->va_gen = ip->i_gen;
	vap->va_blocksize = vp->v_mount->mnt_stat.f_iosize;
	vap->va_bytes = dbtob((u_quad_t)ip->i_blocks);
	vap->va_type = IFTOVT(ip->i_mode);
	vap->va_filerev = ip->i_modrev;
	return (0);
}

/*
 * Set attribute vnode op. called from several syscalls
 * 系统调用的时候会用到这个函数，用来设置 vnode 属性
 */
static int
ext2_setattr(struct vop_setattr_args *ap)
{
	struct vattr *vap = ap->a_vap;
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	struct ucred *cred = ap->a_cred;
	struct thread *td = curthread;
	int error;

	/*
	 * Check for unsettable attributes. 检查不可设置的属性
	 */
	if ((vap->va_type != VNON) || (vap->va_nlink != VNOVAL) ||
	    (vap->va_fsid != VNOVAL) || (vap->va_fileid != VNOVAL) ||
	    (vap->va_blocksize != VNOVAL) || (vap->va_rdev != VNOVAL) ||
	    ((int)vap->va_bytes != VNOVAL) || (vap->va_gen != VNOVAL)) {
		return (EINVAL);
	}
	if (vap->va_flags != VNOVAL) {
		/* Disallow flags not supported by ext2fs. 禁用ext2不支持的标志 */
		if (vap->va_flags & ~(SF_APPEND | SF_IMMUTABLE | UF_NODUMP))
			return (EOPNOTSUPP);

		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		/*
		 * Callers may only modify the file flags on objects they
		 * have VADMIN rights for.
		 */
		if ((error = VOP_ACCESS(vp, VADMIN, cred, td)))
			return (error);
		/*
		 * Unprivileged processes and privileged processes in
		 * jail() are not permitted to unset system flags, or
		 * modify flags if any system flags are set.
		 * Privileged non-jail processes may not modify system flags
		 * if securelevel > 0 and any existing system flags are set.
		 */
		if (!priv_check_cred(cred, PRIV_VFS_SYSFLAGS, 0)) {
			if (ip->i_flags & (SF_IMMUTABLE | SF_APPEND)) {
				error = securelevel_gt(cred, 0);
				if (error)
					return (error);
			}
		} else {
			if (ip->i_flags & (SF_IMMUTABLE | SF_APPEND) ||
			    ((vap->va_flags ^ ip->i_flags) & SF_SETTABLE))
				return (EPERM);
		}
		ip->i_flags = vap->va_flags;
		ip->i_flag |= IN_CHANGE;
		if (ip->i_flags & (IMMUTABLE | APPEND))
			return (0);
	}	// end of VNOVAL

	if (ip->i_flags & (IMMUTABLE | APPEND))
		return (EPERM);
	/*
	 * Go through the fields and update iff not VNOVAL.
	 */
	if (vap->va_uid != (uid_t)VNOVAL || vap->va_gid != (gid_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		if ((error = ext2_chown(vp, vap->va_uid, vap->va_gid, cred,
		    td)) != 0)
			return (error);
	}
	if (vap->va_size != VNOVAL) {
		/*
		 * Disallow write attempts on read-only file systems;
		 * unless the file is a socket, fifo, or a block or
		 * character device resident on the file system.
		 */
		switch (vp->v_type) {
		case VDIR:
			return (EISDIR);
		case VLNK:
		case VREG:
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);
			break;
		default:
			break;
		}
		if ((error = ext2_truncate(vp, vap->va_size, 0, cred, td)) != 0)
			return (error);
	}
	if (vap->va_atime.tv_sec != VNOVAL || vap->va_mtime.tv_sec != VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);	/* 只读文件系统不允许写操作 */
		/*
		 * From utimes(2):
		 * If times is NULL, ... The caller must be the owner of
		 * the file, have permission to write the file, or be the
		 * super-user. 
		 * 如果时间为null，调用者一定要是文件的拥有者，拥有写权限，或者是超级用户
		 * 
		 * If times is non-NULL, ... The caller must be the owner of
		 * the file or be the super-user.
		 * 如果时间不为null，调用者一定是文件的所有者或者是超级用户
		 */
		if ((error = VOP_ACCESS(vp, VADMIN, cred, td)) &&
		    ((vap->va_vaflags & VA_UTIMES_NULL) == 0 ||
		    (error = VOP_ACCESS(vp, VWRITE, cred, td))))
			return (error);
		ip->i_flag |= IN_CHANGE | IN_MODIFIED;
		if (vap->va_atime.tv_sec != VNOVAL) {
			ip->i_flag &= ~IN_ACCESS;
			ip->i_atime = vap->va_atime.tv_sec;
			ip->i_atimensec = vap->va_atime.tv_nsec;
		}
		if (vap->va_mtime.tv_sec != VNOVAL) {
			ip->i_flag &= ~IN_UPDATE;
			ip->i_mtime = vap->va_mtime.tv_sec;
			ip->i_mtimensec = vap->va_mtime.tv_nsec;
		}
		ip->i_birthtime = vap->va_birthtime.tv_sec;
		ip->i_birthnsec = vap->va_birthtime.tv_nsec;
		error = ext2_update(vp, 0);
		if (error)
			return (error);
	}
	error = 0;
	if (vap->va_mode != (mode_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		error = ext2_chmod(vp, (int)vap->va_mode, cred, td);	/* 修改文件权限 */
	}
	return (error);
}

/*
 * Change the mode on a file.
 * Inode must be locked before calling.
 * 修改文件的访问权限；inode 在被调用之前要被锁住
 */
static int
ext2_chmod(struct vnode *vp, int mode, struct ucred *cred, struct thread *td)
{
	struct inode *ip = VTOI(vp);
	int error;

	/*
	 * To modify the permissions on a file, must possess VADMIN
	 * for that file. 要修改文件的权限，必须拥有该文件的 VADMIN
	 */
	if ((error = VOP_ACCESS(vp, VADMIN, cred, td)))
		return (error);
	/*
	 * Privileged processes may set the sticky bit on non-directories,
	 * as well as set the setgid bit on a file with a group that the
	 * process is not a member of.
	 * 特权进程可能会在非目录项上设置粘性位，在一个组的文件上并且设置 setgid 位，
	 * 并且进程并不是这个组的成员
	 */
	if (vp->v_type != VDIR && (mode & S_ISTXT)) {
		error = priv_check_cred(cred, PRIV_VFS_STICKYFILE, 0);
		if (error)
			return (EFTYPE);
	}
	if (!groupmember(ip->i_gid, cred) && (mode & ISGID)) {
		error = priv_check_cred(cred, PRIV_VFS_SETGID, 0);
		if (error)
			return (error);
	}
	/* 设置的是 vnode 对应的 inode 的属性信息 */
	ip->i_mode &= ~ALLPERMS;
	ip->i_mode |= (mode & ALLPERMS);
	ip->i_flag |= IN_CHANGE;
	return (0);
}

/*
 * Perform chown operation on inode ip;
 * inode must be locked prior to call.
 * 在inode ip上执行chown操作；调用前必须锁定inode
 */
static int
ext2_chown(struct vnode *vp, uid_t uid, gid_t gid, struct ucred *cred,
    struct thread *td)
{
	/* 操作的主要对象还是 vnode 对应的 inode */
	struct inode *ip = VTOI(vp);
	uid_t ouid;
	gid_t ogid;
	int error = 0;

	if (uid == (uid_t)VNOVAL)
		uid = ip->i_uid;
	if (gid == (gid_t)VNOVAL)
		gid = ip->i_gid;
	/*
	 * To modify the ownership of a file, must possess VADMIN
	 * for that file.
	 */
	if ((error = VOP_ACCESS(vp, VADMIN, cred, td)))
		return (error);
	/*
	 * To change the owner of a file, or change the group of a file
	 * to a group of which we are not a member, the caller must
	 * have privilege.
	 * 更改文件的所有者或者组组所有者的时候，调用者必须要拥有权限
	 */
	if (uid != ip->i_uid || (gid != ip->i_gid &&
	    !groupmember(gid, cred))) {
		error = priv_check_cred(cred, PRIV_VFS_CHOWN, 0);
		if (error)
			return (error);
	}
	ogid = ip->i_gid;
	ouid = ip->i_uid;
	ip->i_gid = gid;
	ip->i_uid = uid;
	ip->i_flag |= IN_CHANGE;
	if ((ip->i_mode & (ISUID | ISGID)) && (ouid != uid || ogid != gid)) {
		if (priv_check_cred(cred, PRIV_VFS_RETAINSUGID, 0) != 0)
			ip->i_mode &= ~(ISUID | ISGID);
	}
	return (0);
}

/*
 * Synch an open file.
 */
/* ARGSUSED */
static int
ext2_fsync(struct vop_fsync_args *ap)
{
	/*
	 * Flush all dirty buffers associated with a vnode.
	 * 刷新 vnode 所关联的所有的脏页；应该是操作的实际对象应该是 vnode 对应的
	 * vm_object
	 */
	vop_stdfsync(ap);	/* 首先执行vfs提供的默认同步函数 */

	/* 对应的应该是数据写回磁盘的操作 */
	return (ext2_update(ap->a_vp, ap->a_waitfor == MNT_WAIT));
}

/*
 * Mknod vnode call
 */
/* ARGSUSED */
static int
ext2_mknod(struct vop_mknod_args *ap)
{
	struct vattr *vap = ap->a_vap;
	struct vnode **vpp = ap->a_vpp;
	struct inode *ip;
	ino_t ino;
	int error;

	/*
		ext2_create 函数中也会调用到这个函数，要关注其实现机制；
		大致的过程就是文件系统会通过一系列算法处理，找一个最为合适的柱面，从中分配一个
		inode 给 对应的 vnode
	*/
	error = ext2_makeinode(MAKEIMODE(vap->va_type, vap->va_mode),
	    ap->a_dvp, vpp, ap->a_cnp);
	if (error)
		return (error);
	ip = VTOI(*vpp);	/* 后续处理还是针对 inode 进行 */
	ip->i_flag |= IN_ACCESS | IN_CHANGE | IN_UPDATE;
	if (vap->va_rdev != VNOVAL) {
		/*
		 * Want to be able to use this to make badblock
		 * inodes, so don't truncate the dev number.
		 * 希望能够使用它来生成badblock inode，所以不要截断dev编号
		 */
		if (!(ip->i_flag & IN_E4EXTENTS))
			ip->i_rdev = vap->va_rdev;
	}
	/*
	 * Remove inode, then reload it through VFS_VGET so it is
	 * checked to see if it is an alias of an existing entry in
	 * the inode cache.	 XXX I don't believe this is necessary now.
	 * 删除inode，然后通过 VFS_VGET 重新加载它，以便检查它是否是 inode 缓存中
	 * 现有项的别名。XXX - 我认为现在没有必要这样做
	 */
	(*vpp)->v_type = VNON;
	ino = ip->i_number;	/* Save this before vgone() invalidates ip. */
	vgone(*vpp);
	vput(*vpp);
	error = VFS_VGET(ap->a_dvp->v_mount, ino, LK_EXCLUSIVE, vpp);
	if (error) {
		*vpp = NULL;
		return (error);
	}
	return (0);
}

static int
ext2_remove(struct vop_remove_args *ap)
{
	struct inode *ip;
	struct vnode *vp = ap->a_vp;	/* 应该表示我们要处理的对象 */
	struct vnode *dvp = ap->a_dvp;	/* 所处理对象所在目录对应的 vnode */
	int error;

	/* 
		首先检查 inode 属性。功能函数中存在许多 VTOI 操作，说明我们对于一下文件或者目录的
		操作的最终载体都是 inode
	*/
	ip = VTOI(vp);
	if ((ip->i_flags & (NOUNLINK | IMMUTABLE | APPEND)) ||
	    (VTOI(dvp)->i_flags & APPEND)) {
		error = EPERM;
		goto out;
	}
	error = ext2_dirremove(dvp, ap->a_cnp);	/* 移除掉一个目录项？ */
	if (error == 0) {
		ip->i_nlink--;	/* 文件的链接计数-- */
		ip->i_flag |= IN_CHANGE;
	}
out:
	return (error);
}

/*
 * link vnode call
 */
static int
ext2_link(struct vop_link_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *tdvp = ap->a_tdvp;
	struct componentname *cnp = ap->a_cnp;
	struct inode *ip;
	int error;

#ifdef INVARIANTS
	if ((cnp->cn_flags & HASBUF) == 0)
		panic("ext2_link: no name");
#endif
	ip = VTOI(vp);	/* 这里又出现了，说明对于文件的处理都是以 inode 为载体进行的 */
	/* 判断文件的最大链接数是否小于规定的最大值 */
	if ((nlink_t)ip->i_nlink >= EXT4_LINK_MAX) {
		error = EMLINK;
		goto out;
	}
	/* 判断文件属性，是否不能被更改 */
	if (ip->i_flags & (IMMUTABLE | APPEND)) {
		error = EPERM;
		goto out;
	}
	ip->i_nlink++;	/* 链接计数++ */
	/*
		更新文件的更改时间属性。与 modify 属性的区别可能是，modify 连文件的内容也被修改了，
		那么其他属性肯定也会变。而这里仅仅是改动了文件的属性，两者范围不同？
	*/
	ip->i_flag |= IN_CHANGE;	
	error = ext2_update(vp, !DOINGASYNC(vp));
	if (!error)
		error = ext2_direnter(ip, tdvp, cnp);
	if (error) {
		ip->i_nlink--;
		ip->i_flag |= IN_CHANGE;
	}
out:
	return (error);
}

/* increase link number*/
static int
ext2_inc_nlink(struct inode *ip)
{

	ip->i_nlink++;

	if (S_ISDIR(ip->i_mode) &&
	    EXT2_HAS_RO_COMPAT_FEATURE(ip->i_e2fs, EXT2F_ROCOMPAT_DIR_NLINK) &&
	    ip->i_nlink > 1) {
		if (ip->i_nlink >= EXT4_LINK_MAX || ip->i_nlink == 2)
			ip->i_nlink = 1;
	} else if (ip->i_nlink > EXT4_LINK_MAX) {
		ip->i_nlink--;
		return (EMLINK);
	}

	return (0);
}

/* delete link number */
static void
ext2_dec_nlink(struct inode *ip)
{

	if (!S_ISDIR(ip->i_mode) || ip->i_nlink > 2)
		ip->i_nlink--;
}

/*
 * Rename system call.
 * 	rename("foo", "bar");
 * is essentially
 *	unlink("bar");
 *	link("foo", "bar");
 *	unlink("foo");
 * but ``atomically''.  Can't do full commit without saving state in the
 * inode on disk which isn't feasible at this time.  Best we can do is
 * always guarantee the target exists.
 * 如果不将状态保存在磁盘上的inode中，则无法执行完全提交，这在此时是不可行的。
 * 我们所能做的就是保证目标的存在
 *
 * Basic algorithm is:
 *
 * 1) Bump link count on source while we're linking it to the
 *    target.  This also ensure the inode won't be deleted out
 *    from underneath us while we work (it may be truncated by
 *    a concurrent `trunc' or `open' for creation).
 * 2) Link source to destination.  If destination already exists,
 *    delete it first.
 * 3) Unlink source reference to inode if still around. If a
 *    directory was moved and the parent of the destination
 *    is different from the source, patch the ".." entry in the
 *    directory.
 */
static int
ext2_rename(struct vop_rename_args *ap)
{
	/*
		tvp: target vnode pointer
		tdvp: target directory vnode pointer
		fvp: file vnode pointer
		fdvp: file directory vnode pointer
	*/
	struct vnode *tvp = ap->a_tvp;
	struct vnode *tdvp = ap->a_tdvp;
	struct vnode *fvp = ap->a_fvp;
	struct vnode *fdvp = ap->a_fdvp;
	struct componentname *tcnp = ap->a_tcnp;
	struct componentname *fcnp = ap->a_fcnp;
	struct inode *ip, *xp, *dp;
	struct dirtemplate *dirbuf;
	int doingdirectory = 0, oldparent = 0, newparent = 0;
	int error = 0;
	u_char namlen;

#ifdef INVARIANTS
	if ((tcnp->cn_flags & HASBUF) == 0 ||
	    (fcnp->cn_flags & HASBUF) == 0)
		panic("ext2_rename: no name");
#endif
	/*
	 * Check for cross-device rename. 检查跨设备重命名
	 * 从注释来看，tdvp 表示的应该是重命名目标文件的 vnode，这里判断的是
	 * 源文件和目标文件的挂载点是否一致
	 * 
	 * 重命名不能跨分区或者跨文件系统进行，用户态貌似不支持目录项硬链接。下面的判断逻辑应该就是
	 * 对这个进行的判断
	 */
	if ((fvp->v_mount != tdvp->v_mount) ||
	    (tvp && (fvp->v_mount != tvp->v_mount))) {
		error = EXDEV;
abortit:
		/*
			vput 输出的 vnode 是加锁的；vrele 输出的 vnode 是不加锁的。当目标文件就是目标目录的时候，
			tdvp 是不加锁的；如果不一样，加锁。tvp 只要存在，就要加锁。也就是目标文件 vnode 如果存在
			就要加锁，而目标目录一般情况下也是要加锁。
			源文件和对应目录都不加锁。是不是说明通过系统调用生成的新文件关联的 vnode 都需要加锁(返回 vfs
			层级时还会被用到)，而源文件和目录后面就用不到了，所以解锁？
		*/
		if (tdvp == tvp)
			vrele(tdvp);
		else
			vput(tdvp);
		if (tvp)
			vput(tvp);
		vrele(fdvp);
		vrele(fvp);
		return (error);
	}

	/*
		NOUNLINK: 文件可能不被允许移动或者重命名
		IMMUTABLE： 文件不能被更改
		APPEND： 对文件的写入只能附加
		目标文件vp如果包含上述属性，那么重命名操作在该文件上是不被允许的，返回操作参数错误
	*/
	if (tvp && ((VTOI(tvp)->i_flags & (NOUNLINK | IMMUTABLE | APPEND)) ||
	    (VTOI(tdvp)->i_flags & APPEND))) {
		error = EPERM;
		goto abortit;
	}
	/*
	 * Renaming a file to itself has no effect.  The upper layers should
	 * not call us in that case.  Temporarily just warn if they do.
	 * 将文件重命名为自身没有效果。上层不应该在这种情况下调用该函数。如果有，就暂时警告一下
	 */
	if (fvp == tvp) {
		printf("ext2_rename: fvp == tvp (can't happen)\n");
		error = 0;
		goto abortit;
	}
	/* 
		现在要开始对fvp进行操作了，所以要加锁进行保护。注意源文件对应的锁要是独占类型的
	*/
	if ((error = vn_lock(fvp, LK_EXCLUSIVE)) != 0)
		goto abortit;
	dp = VTOI(fdvp);	// 此时的 dp 是源文件目录项
	ip = VTOI(fvp);		// 从下面代码实现来看，ip 貌似一直表示的就是源文件
	// 如果是在 tptfs 中对访问 ip 数据加锁保护的话，应该从这里就可以开始了。可以先加读锁，
	// 后面修改属性信息的时候加写锁
	/* 
		判断最大链接数是否超过了限制，并且判断文件系统是否具有只读属性 
	*/
	if (ip->i_nlink >= EXT4_LINK_MAX &&
	    !EXT2_HAS_RO_COMPAT_FEATURE(ip->i_e2fs, EXT2F_ROCOMPAT_DIR_NLINK)) {
		VOP_UNLOCK(fvp, 0);
		error = EMLINK;
		goto abortit;
	}
	if ((ip->i_flags & (NOUNLINK | IMMUTABLE | APPEND))
	    || (dp->i_flags & APPEND)) {
		VOP_UNLOCK(fvp, 0);
		error = EPERM;
		goto abortit;
	}
	/* 判断文件类型是不是目录文件 */
	if ((ip->i_mode & IFMT) == IFDIR) {
		/*
		 * Avoid ".", "..", and aliases of "." for obvious reasons.
		 * 出于明显的原因，请避免使用“.”、“..”和“.”的别名。
		 * 当源文件是一个目录的时候，只要满足下面任意一个条件，就表示我们的操作是错误的
		 */
		if ((fcnp->cn_namelen == 1 && fcnp->cn_nameptr[0] == '.') ||
		    dp == ip || (fcnp->cn_flags | tcnp->cn_flags) & ISDOTDOT ||
		    (ip->i_flag & IN_RENAME)) {
			VOP_UNLOCK(fvp, 0);
			error = EINVAL;
			goto abortit;
		}
		ip->i_flag |= IN_RENAME;
		/*
			指定文件移动之前的所在目录项的 inode number。doingdirectory 可能是表示我们当前操作的
			源文件是一个目录文件。dp 目前还是源文件所在目录
		*/
		oldparent = dp->i_number;
		doingdirectory++;
	}
	/* 
		返回一个未被锁定的对象，并且减少对象的引用计数。到这里好像源目录已经不会被再使用了，
		后面 dp 直接被赋值为了目标目录
	*/
	vrele(fdvp);
	/*
	 * When the target exists, both the directory
	 * and target vnodes are returned locked.
	 * 当目标存在时，目录和目标 vnode 都将返回并锁定。这个时候 dp 变成了目标目录，感觉可以直接加写锁，
	 * 因为后面代码中有挺多属性更新操作。前面对 dp 的访问应该是不用加锁的，i_flags 和 i_number 基本
	 * 不会被修改，都是文件创建的时候差不多就固定下来了。
	 */
	dp = VTOI(tdvp);
	// xp 在这里起到的作用推测是用于判断目标文件是否已经存在
	xp = NULL;
	if (tvp)
		xp = VTOI(tvp);

	/*
	 * 1) Bump link count while we're moving stuff
	 *    around.  If we crash somewhere before
	 *    completing our work, the link count
	 *    may be wrong, but correctable.
	 * 当我们移动东西时，碰撞链接计数。如果我们在完成工作之前在某个地方崩溃，
	 * 链接计数可能是错误的，但可以纠正。
	 * 这里是增加源文件的引用计数，可能是怕在后续的操作中该文件对应的 inode 会被删除。
	 * 修改参数之后立马执行 update()，将更新后的数据同步到磁盘当中
	 */
	ext2_inc_nlink(ip);	// ip 此时是源文件
	ip->i_flag |= IN_CHANGE;
	if ((error = ext2_update(fvp, !DOINGASYNC(fvp))) != 0) {
		VOP_UNLOCK(fvp, 0);
		goto bad;
	}

	/*
	 * If ".." must be changed (ie the directory gets a new
	 * parent) then the source directory must not be in the
	 * directory hierarchy above the target, as this would
	 * orphan everything below the source directory. Also
	 * the user must have write permission in the source so
	 * as to be able to change "..". We must repeat the call
	 * to namei, as the parent directory is unlocked by the
	 * call to checkpath().
	 * 
	 * 如果必须更改“..”（即目录获得新的父目录），则源目录不得位于目标目录之上的目录层次结构中，
	 * 因为这将孤立源目录之下的所有内容。此外，用户必须在源中具有写入权限，才能更改“..”。我们
	 * 必须重复调用 namei，因为调用 checkpath 会解锁父目录。
	 * 注意 vop_access 检查的还是源文件的写权限，不过对应的 cred 和 thread 参数与却是 target
	 */
	error = VOP_ACCESS(fvp, VWRITE, tcnp->cn_cred, tcnp->cn_thread);
	VOP_UNLOCK(fvp, 0);
	/*
		当目标目录不是当前目录的时候，把目标目录的 inode number 赋值给 newparent。ext2fs ino
		是从 2 开始的，所以不可能小于或者等于 0 的。注意，此时的 dp 已经表示的是目标目录
	*/
	if (oldparent != dp->i_number)
		newparent = dp->i_number;	// newparent 初次赋值，说明当前目录与目标目录不一致

	// 当我们处理的是一个目录项并且目标目录是一个新目录时，执行 if 分支代码
	if (doingdirectory && newparent) {
		if (error)	/* write access check above */
			goto bad;
		if (xp != NULL)
			vput(tvp);
		/*
			此时 ip 表示的文件的类型是目录，这里要检查的是 ip 是不是在目标文件的父级路径上。
			如果是的话，那肯定是不能成立的。后面要再次确认目标文件是否存在(relookup() 函数)
		*/
		error = ext2_checkpath(ip, dp, tcnp->cn_cred);
		if (error)
			goto out;
		VREF(tdvp);
		/*
			rename() 函数中前后两个 relookup() 函数的作用要格外关注。第一个就是重新判断当前目标文件是否已经存在了，
			说明了什么问题？说明我们有可能在执行完 checkpath 之后，执行 rename 操作的线程可能会被抢占。然后另外一个
			线程刚好创建了一个同名的文件，所以这里才要重新查找目标文件是否存在。注意一个设计细节，这个 relookup() 刚好
			是在第二阶段处理开始之前。下面的处理思路类似，也是在准备处理源文件之前，重新查找源文件是否已经被删除了。
			tptfs rename() 要如何处理？ 可以简单粗暴一点，只要在执行 rename()，源目录和目标目录直接加写锁，这个时间段
			内不允许对目录进行增删查找，等待命令执行完成之后在放开限制
		*/
		error = relookup(tdvp, &tvp, tcnp);
		if (error)
			goto out;
		vrele(tdvp);
		dp = VTOI(tdvp);
		xp = NULL;
		if (tvp)
			xp = VTOI(tvp);
	}
	// 在此之前，目标文件到底存不存在还不确定，也没有做任何操作
	/*
	 * 2) If target doesn't exist, link the target
	 *    to the source and unlink the source.
	 *    Otherwise, rewrite the target directory
	 *    entry to reference the source inode and
	 *    expunge the original entry's existence.
	 */
	if (xp == NULL) {
		if (dp->i_devvp != ip->i_devvp)
			panic("ext2_rename: EXDEV");
		/*
		 * Account for ".." in new directory.
		 * When source and destination have the same
		 * parent we don't fool with the link count.
		 */
		if (doingdirectory && newparent) {
			error = ext2_inc_nlink(dp);
			if (error)
				goto bad;

			dp->i_flag |= IN_CHANGE;
			error = ext2_update(tdvp, !DOINGASYNC(tdvp));
			if (error)
				goto bad;
		}
		/*
			为 ip 在目标目录文件下添加一个 directory entry。tptfs 中应该是不能这么处理的。
			我们应该先判断是不是在同一个目录下，如果是的话，那么我们不需要添加新的文件结点，
			直接把原来的文件进行重命名就好了。如果不是同一个路径下，修改源文件的指针就好了。
			需要注意的是，对应的 . 和 .. 两个子目录文件也要做相应的修改
			按照现有的设计，其实这两个不用修改，因为我们再查找的时候会在路径解析的时候处理掉 . 和 ..，
			所以现在该不该无所谓。为了稳妥起见，还是把正常需要修改的地方都改掉，因为以后可能
			会有新的处理逻辑用到这两个节点
		*/
		error = ext2_direnter(ip, tdvp, tcnp);
		if (error) {
			if (doingdirectory && newparent) {
				ext2_dec_nlink(dp);
				dp->i_flag |= IN_CHANGE;
				(void)ext2_update(tdvp, 1);
			}
			goto bad;
		}
		vput(tdvp);	// 此时 tdvp 仍是加锁
	} else {
		if (xp->i_devvp != dp->i_devvp || xp->i_devvp != ip->i_devvp)
			panic("ext2_rename: EXDEV");
		/*
		 * Short circuit rename(foo, foo).
		 */
		if (xp->i_number == ip->i_number)
			panic("ext2_rename: same file");
		/*
		 * If the parent directory is "sticky", then the user must
		 * own the parent directory, or the destination of the rename,
		 * otherwise the destination may not be changed (except by
		 * root). This implements append-only directories.
		 */
		if ((dp->i_mode & S_ISTXT) && tcnp->cn_cred->cr_uid != 0 &&
		    tcnp->cn_cred->cr_uid != dp->i_uid &&
		    xp->i_uid != tcnp->cn_cred->cr_uid) {
			error = EPERM;
			goto bad;
		}
		/*
		 * Target must be empty if a directory and have no links
		 * to it. Also, ensure source and target are compatible
		 * (both directories, or both not directories).
		 * 当目标文件是一个目录的时候，我们要保证该文件一定要是空的。并且只有目录才能和
		 * 目录
		 */
		if ((xp->i_mode & IFMT) == IFDIR) {
			/*
				如果目标文件也是一个目录项的话，那这个目标目录项必须要保证是一个空的目录。从 mv 命令的
				实际操作情况来看，当目标文件是一个目录并且不为空的时候，其实是将源目录移动到了目标文件
				目录之下，而不是重名民操作
			*/
			if (!ext2_dirempty(xp, dp->i_number, tcnp->cn_cred)) {
				error = ENOTEMPTY;
				goto bad;
			}
			if (!doingdirectory) {
				error = ENOTDIR;
				goto bad;
			}
			cache_purge(tdvp);
		} else if (doingdirectory) {
			/*
				如果目标文件是目录，但是源文件不是目录。重命名的时候就会报错
			*/
			error = EISDIR;
			goto bad;
		}
		error = ext2_dirrewrite(dp, ip, tcnp);
		if (error)
			goto bad;
		/*
		 * If the target directory is in the same
		 * directory as the source directory,
		 * decrement the link count on the parent
		 * of the target directory.
		 */
		if (doingdirectory && !newparent) {
			ext2_dec_nlink(dp);
			dp->i_flag |= IN_CHANGE;
		}
		vput(tdvp);
		/*
		 * Adjust the link count of the target to
		 * reflect the dirrewrite above.  If this is
		 * a directory it is empty and there are
		 * no links to it, so we can squash the inode and
		 * any space associated with it.  We disallowed
		 * renaming over top of a directory with links to
		 * it above, as the remaining link would point to
		 * a directory without "." or ".." entries.
		 */
		ext2_dec_nlink(xp);
		if (doingdirectory) {
			/*
				当我们处理的源文件是一个目录，并且该目录是空目录的时候(其实就是对应上述判断条件)，
				那么我们就可以释放目标目录对应的磁盘块数据(tptfs 中实际上是没有的，只有链表中的
				entry，没有给目录文件分配具体的数据页)。
			*/
			if (--xp->i_nlink != 0)
				panic("ext2_rename: linked directory");
			error = ext2_truncate(tvp, (off_t)0, IO_SYNC,
			    tcnp->cn_cred, tcnp->cn_thread);
		}
		xp->i_flag |= IN_CHANGE;
		vput(tvp);	// 注意这里，tvp 也是加锁状态
		xp = NULL;
	}

	/*
	 * 3) Unlink the source.
	 */
	fcnp->cn_flags &= ~MODMASK;
	/* 应该就是源文件和对应的目录项都要被锁住 */
	fcnp->cn_flags |= LOCKPARENT | LOCKLEAF;
	VREF(fdvp);	// 对 fdvp 加锁
	error = relookup(fdvp, &fvp, fcnp);
	if (error == 0)
		vrele(fdvp);
	/*
		执行 relookup 函数之前对 fdvp 加锁，完成之后如果没有出错，执行解锁操作。
		这里还出现了 fvp == null 的判断情况。也就是说 fvp 此时也不是一定会存在的。
		前面执行了 relookup 函数，猜测有可能是在 rename 的过程中，有别的线程把
		源文件给删除了，可能对应的 vnode 也就被释放，导致 fvp 为空的情况？
	*/
	if (fvp != NULL) {
		xp = VTOI(fvp);
		dp = VTOI(fdvp);
	} else {
		/*
		 * From name has disappeared.  IN_RENAME is not sufficient
		 * to protect against directory races due to timing windows,
		 * so we can't panic here.
		 */
		vrele(ap->a_fvp);
		return (0);
	}
	/*
	 * Ensure that the directory entry still exists and has not
	 * changed while the new name has been entered. If the source is
	 * a file then the entry may have been unlinked or renamed. In
	 * either case there is no further work to be done. If the source
	 * is a directory then it cannot have been rmdir'ed; its link
	 * count of three would cause a rmdir to fail with ENOTEMPTY.
	 * The IN_RENAME flag ensures that it cannot be moved by another
	 * rename.
	 * 确保在输入新名称时目录条目仍然存在且未更改。如果源文件是文件，则该条目可能已被取消
	 * 链接或重命名。在这两种情况下，都没有进一步的工作要做。如果源是一个目录，那么它不能
	 * 被rmdir'ed；它的链接计数为3会导致rmdir因Enotery而失败。IN_RENAME 标志确保它
	 * 不能被其他重命名移动
	 * 
	 * 此时 xp 又设置成了源文件的 inode，ip 也是表示的是源文件。从注释可以看到，我们要
	 * 保证 directory entry 依然存在并且在创建新的名称的时候不会被改变。tptfs 的处理
	 * 方式应该是直接把源目录给锁住，再把源文件锁住，防止它们被修改。等到重命名操作执行
	 * 完毕之后再解锁
	 */
	if (xp != ip) {
		/*
		 * From name resolves to a different inode.  IN_RENAME is
		 * not sufficient protection against timing window races
		 * so we can't panic here.
		 */
	} else {
		/*
		 * If the source is a directory with a
		 * new parent, the link count of the old
		 * parent directory must be decremented
		 * and ".." set to point to the new parent.
		 * 假如源文件是一个目录文件，并且要移动到一个新的目录的时候，源目录的引用计数要自减并且
		 * 保证源文件的 .. 指向的是新目录。tptfs 中对应需要将 VFileNode 父指针指向新的目录
		 * 对应的文件类实例
		 */
		if (doingdirectory && newparent) {
			ext2_dec_nlink(dp);
			dp->i_flag |= IN_CHANGE;
			dirbuf = malloc(dp->i_e2fs->e2fs_bsize, M_TEMP, M_WAITOK | M_ZERO);
			if (!dirbuf) {
				error = ENOMEM;
				goto bad;
			}
			error = vn_rdwr(UIO_READ, fvp, (caddr_t)dirbuf,
			    ip->i_e2fs->e2fs_bsize, (off_t)0,
			    UIO_SYSSPACE, IO_NODELOCKED | IO_NOMACCHECK,
			    tcnp->cn_cred, NOCRED, NULL, NULL);
			if (error == 0) {
				/* Like ufs little-endian: */
				namlen = dirbuf->dotdot_type;
				if (namlen != 2 ||
				    dirbuf->dotdot_name[0] != '.' ||
				    dirbuf->dotdot_name[1] != '.') {
					ext2_dirbad(xp, (doff_t)12,
					    "rename: mangled dir");
				} else {
					dirbuf->dotdot_ino = newparent;
					/*
					 * dirblock 0 could be htree root,
					 * try both csum update functions.
					 */
					ext2_dirent_csum_set(ip,
					    (struct ext2fs_direct_2 *)dirbuf);
					ext2_dx_csum_set(ip,
					    (struct ext2fs_direct_2 *)dirbuf);
					(void)vn_rdwr(UIO_WRITE, fvp,
					    (caddr_t)dirbuf,
					    ip->i_e2fs->e2fs_bsize,
					    (off_t)0, UIO_SYSSPACE,
					    IO_NODELOCKED | IO_SYNC |
					    IO_NOMACCHECK, tcnp->cn_cred,
					    NOCRED, NULL, NULL);
					cache_purge(fdvp);
				}
			}
			free(dirbuf, M_TEMP);
		}
		// 把源文件对应的目录 entry 移除掉
		error = ext2_dirremove(fdvp, fcnp);
		if (!error) {
			ext2_dec_nlink(xp);
			xp->i_flag |= IN_CHANGE;
		}
		xp->i_flag &= ~IN_RENAME;
	}
	if (dp)
		vput(fdvp);
	if (xp)
		vput(fvp);
	vrele(ap->a_fvp);
	return (error);

bad:
	if (xp)
		vput(ITOV(xp));
	vput(ITOV(dp));
out:
	if (doingdirectory)
		ip->i_flag &= ~IN_RENAME;
	if (vn_lock(fvp, LK_EXCLUSIVE) == 0) {
		ext2_dec_nlink(ip);
		ip->i_flag |= IN_CHANGE;
		ip->i_flag &= ~IN_RENAME;
		vput(fvp);
	} else
		vrele(fvp);
	return (error);
}

#ifdef UFS_ACL
static int
ext2_do_posix1e_acl_inheritance_dir(struct vnode *dvp, struct vnode *tvp,
    mode_t dmode, struct ucred *cred, struct thread *td)
{
	int error;
	struct inode *ip = VTOI(tvp);
	struct acl *dacl, *acl;

	acl = acl_alloc(M_WAITOK);
	dacl = acl_alloc(M_WAITOK);

	/*
	 * Retrieve default ACL from parent, if any.
	 */
	error = VOP_GETACL(dvp, ACL_TYPE_DEFAULT, acl, cred, td);
	switch (error) {
	case 0:
		/*
		 * Retrieved a default ACL, so merge mode and ACL if
		 * necessary.  If the ACL is empty, fall through to
		 * the "not defined or available" case.
		 */
		if (acl->acl_cnt != 0) {
			dmode = acl_posix1e_newfilemode(dmode, acl);
			ip->i_mode = dmode;
			*dacl = *acl;
			ext2_sync_acl_from_inode(ip, acl);
			break;
		}
		/* FALLTHROUGH */

	case EOPNOTSUPP:
		/*
		 * Just use the mode as-is.
		 */
		ip->i_mode = dmode;
		error = 0;
		goto out;

	default:
		goto out;
	}

	error = VOP_SETACL(tvp, ACL_TYPE_ACCESS, acl, cred, td);
	if (error == 0)
		error = VOP_SETACL(tvp, ACL_TYPE_DEFAULT, dacl, cred, td);
	switch (error) {
	case 0:
		break;

	case EOPNOTSUPP:
		/*
		 * XXX: This should not happen, as EOPNOTSUPP above
		 * was supposed to free acl.
		 */
#ifdef DEBUG
		printf("ext2_mkdir: VOP_GETACL() but no VOP_SETACL()\n");
#endif	/* DEBUG */
		break;

	default:
		goto out;
	}

out:
	acl_free(acl);
	acl_free(dacl);

	return (error);
}

static int
ext2_do_posix1e_acl_inheritance_file(struct vnode *dvp, struct vnode *tvp,
    mode_t mode, struct ucred *cred, struct thread *td)
{
	int error;
	struct inode *ip = VTOI(tvp);
	struct acl *acl;

	acl = acl_alloc(M_WAITOK);

	/*
	 * Retrieve default ACL for parent, if any.
	 */
	error = VOP_GETACL(dvp, ACL_TYPE_DEFAULT, acl, cred, td);
	switch (error) {
	case 0:
		/*
		 * Retrieved a default ACL, so merge mode and ACL if
		 * necessary.
		 */
		if (acl->acl_cnt != 0) {
			/*
			 * Two possible ways for default ACL to not
			 * be present.  First, the EA can be
			 * undefined, or second, the default ACL can
			 * be blank.  If it's blank, fall through to
			 * the it's not defined case.
			 */
			mode = acl_posix1e_newfilemode(mode, acl);
			ip->i_mode = mode;
			ext2_sync_acl_from_inode(ip, acl);
			break;
		}
		/* FALLTHROUGH */

	case EOPNOTSUPP:
		/*
		 * Just use the mode as-is.
		 */
		ip->i_mode = mode;
		error = 0;
		goto out;

	default:
		goto out;
	}

	error = VOP_SETACL(tvp, ACL_TYPE_ACCESS, acl, cred, td);
	switch (error) {
	case 0:
		break;

	case EOPNOTSUPP:
		/*
		 * XXX: This should not happen, as EOPNOTSUPP above was
		 * supposed to free acl.
		 */
		printf("ufs_do_posix1e_acl_inheritance_file: VOP_GETACL() "
		    "but no VOP_SETACL()\n");
		/* panic("ufs_do_posix1e_acl_inheritance_file: VOP_GETACL() "
		    "but no VOP_SETACL()"); */
		break;

	default:
		goto out;
	}

out:
	acl_free(acl);

	return (error);
}

#endif /* UFS_ACL */

/*
 * Mkdir system call
 */
static int
ext2_mkdir(struct vop_mkdir_args *ap)
{
	struct m_ext2fs *fs;	/* 超级块 in-memory */
	struct vnode *dvp = ap->a_dvp;	/* 表示的应该是创建文件所在的目录对应的 vnode */
	struct vattr *vap = ap->a_vap;	/* vnode 属性信息 */
	struct componentname *cnp = ap->a_cnp;	/* 组件名称 */
	struct inode *ip, *dp;
	struct vnode *tvp;	/* target vnode */
	struct dirtemplate dirtemplate, *dtp;
	char *buf = NULL;
	int error, dmode;

#ifdef INVARIANTS
	if ((cnp->cn_flags & HASBUF) == 0)
		panic("ext2_mkdir: no name");
#endif
	dp = VTOI(dvp);	/* 文件所在目录对应的 inode，即 directory inode pointer */
	/*
		判断 dp 的链接数是否符合要求，判断超级块是否具有 dir link 属性
	*/
	if ((nlink_t)dp->i_nlink >= EXT4_LINK_MAX &&
	    !EXT2_HAS_RO_COMPAT_FEATURE(dp->i_e2fs, EXT2F_ROCOMPAT_DIR_NLINK)) {
		error = EMLINK;
		goto out;
	}
	/* dmode: directory mode? */
	dmode = vap->va_mode & 0777;
	dmode |= IFDIR;
	/*
	 * Must simulate part of ext2_makeinode here to acquire the inode,
	 * but not have it entered in the parent directory. The entry is
	 * made later after writing "." and ".." entries.
	 * 必须在此处模拟 ext2_makeinode 的一部分以获取 inode，但不能将其输入父目录。
	 * 条目在写入“.”和“..”项之后进行
	 * tvp 表示的是获取到的新的 vnode
	 */
	error = ext2_valloc(dvp, dmode, cnp->cn_cred, &tvp);
	if (error)
		goto out;
	ip = VTOI(tvp);	/* 目标文件对应的 inode */
	fs = ip->i_e2fs;
	ip->i_gid = dp->i_gid;	/* 将父目录 inode 的 gid 赋值给成员 */
#ifdef SUIDDIR
	{
		/*
		 * if we are hacking owners here, (only do this where told to)
		 * and we are not giving it TOO root, (would subvert quotas)
		 * then go ahead and give it to the other user.
		 * The new directory also inherits the SUID bit.
		 * If user's UID and dir UID are the same,
		 * 'give it away' so that the SUID is still forced on.
		 */
		if ((dvp->v_mount->mnt_flag & MNT_SUIDDIR) &&
		    (dp->i_mode & ISUID) && dp->i_uid) {
			dmode |= ISUID;
			ip->i_uid = dp->i_uid;
		} else {
			ip->i_uid = cnp->cn_cred->cr_uid;
		}
	}
#else
	ip->i_uid = cnp->cn_cred->cr_uid;
#endif
	ip->i_flag |= IN_ACCESS | IN_CHANGE | IN_UPDATE;
	ip->i_mode = dmode;
	tvp->v_type = VDIR;	/* Rest init'd in getnewvnode(). */
	ip->i_nlink = 2;
	if (cnp->cn_flags & ISWHITEOUT)
		ip->i_flags |= UF_OPAQUE;
	error = ext2_update(tvp, 1);	/* 上面的代码就是对 inode 和 vnode 属性的设置 */

	/*
	 * Bump link count in parent directory
	 * to reflect work done below.  Should
	 * be done before reference is created
	 * so reparation is possible if we crash.
	 * 在父目录中增加链接计数，以反映下面完成的工作。应该在创建引用之前完成，以便在崩溃时可以进行修复
	 */
	ext2_inc_nlink(dp);
	dp->i_flag |= IN_CHANGE;
	error = ext2_update(dvp, !DOINGASYNC(dvp));
	if (error)
		goto bad;

	/* Initialize directory with "." and ".." from static template. 
		从静态模板用“.”和“..”初始化目录
	*/
	if (EXT2_HAS_INCOMPAT_FEATURE(ip->i_e2fs,
	    EXT2F_INCOMPAT_FTYPE))
		dtp = &mastertemplate;
	else
		dtp = &omastertemplate;
	dirtemplate = *dtp;
	dirtemplate.dot_ino = ip->i_number;
	dirtemplate.dotdot_ino = dp->i_number;	/* 将当前目录的 .. 子目录 inode number 设置为父目录 */
	/*
	 * note that in ext2 DIRBLKSIZ == blocksize, not DEV_BSIZE so let's
	 * just redefine it - for this function only
	 */
#undef  DIRBLKSIZ
#define DIRBLKSIZ  VTOI(dvp)->i_e2fs->e2fs_bsize
	dirtemplate.dotdot_reclen = DIRBLKSIZ - 12;	// 这里要注意
	buf = malloc(DIRBLKSIZ, M_TEMP, M_WAITOK | M_ZERO);
	if (!buf) {
		error = ENOMEM;
		ext2_dec_nlink(dp);
		dp->i_flag |= IN_CHANGE;
		goto bad;
	}
	if (EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM)) {
		dirtemplate.dotdot_reclen -= sizeof(struct ext2fs_direct_tail);
		ext2_init_dirent_tail(EXT2_DIRENT_TAIL(buf, DIRBLKSIZ));
	}
	memcpy(buf, &dirtemplate, sizeof(dirtemplate));
	ext2_dirent_csum_set(ip, (struct ext2fs_direct_2 *)buf);
	error = vn_rdwr(UIO_WRITE, tvp, (caddr_t)buf,
	    DIRBLKSIZ, (off_t)0, UIO_SYSSPACE,
	    IO_NODELOCKED | IO_SYNC | IO_NOMACCHECK, cnp->cn_cred, NOCRED,
	    NULL, NULL);
	if (error) {
		ext2_dec_nlink(dp);
		dp->i_flag |= IN_CHANGE;
		goto bad;
	}
	if (DIRBLKSIZ > VFSTOEXT2(dvp->v_mount)->um_mountp->mnt_stat.f_bsize)
		/* XXX should grow with balloc() */
		panic("ext2_mkdir: blksize");
	else {
		ip->i_size = DIRBLKSIZ;	// i_size = 块大小，并不是目录文件的真实大小
		ip->i_flag |= IN_CHANGE;
	}

#ifdef UFS_ACL
	if (dvp->v_mount->mnt_flag & MNT_ACLS) {
		error = ext2_do_posix1e_acl_inheritance_dir(dvp, tvp, dmode,
		    cnp->cn_cred, cnp->cn_thread);
		if (error)
			goto bad;
	}

#endif /* UFS_ACL */

	/* Directory set up, now install its entry in the parent directory. 
		目录设置，现在将其条目安装在父目录中
	*/
	error = ext2_direnter(ip, dvp, cnp);
	if (error) {
		ext2_dec_nlink(dp);
		dp->i_flag |= IN_CHANGE;
	}
bad:
	/*
	 * No need to do an explicit VOP_TRUNCATE here, vrele will do this
	 * for us because we set the link count to 0.
	 */
	if (error) {
		ip->i_nlink = 0;
		ip->i_flag |= IN_CHANGE;
		vput(tvp);
	} else
		*ap->a_vpp = tvp;
out:
	free(buf, M_TEMP);
	return (error);
#undef  DIRBLKSIZ
#define DIRBLKSIZ  DEV_BSIZE
}

/*
 * Rmdir system call.
 */
static int
ext2_rmdir(struct vop_rmdir_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	struct componentname *cnp = ap->a_cnp;
	struct inode *ip, *dp;
	int error;

	ip = VTOI(vp);
	dp = VTOI(dvp);

	/*
	 * Verify the directory is empty (and valid).
	 * (Rmdir ".." won't be valid since
	 *  ".." will contain a reference to
	 *  the current directory and thus be
	 *  non-empty.)
	 */
	if (!ext2_dirempty(ip, dp->i_number, cnp->cn_cred)) {
		error = ENOTEMPTY;
		goto out;
	}
	if ((dp->i_flags & APPEND)
	    || (ip->i_flags & (NOUNLINK | IMMUTABLE | APPEND))) {
		error = EPERM;
		goto out;
	}
	/*
	 * Delete reference to directory before purging
	 * inode.  If we crash in between, the directory
	 * will be reattached to lost+found,
	 */
	error = ext2_dirremove(dvp, cnp);
	if (error)
		goto out;
	ext2_dec_nlink(dp);
	dp->i_flag |= IN_CHANGE;
	cache_purge(dvp);
	VOP_UNLOCK(dvp, 0);
	/*
	 * Truncate inode.  The only stuff left
	 * in the directory is "." and "..".
	 */
	ip->i_nlink = 0;
	error = ext2_truncate(vp, (off_t)0, IO_SYNC, cnp->cn_cred,
	    cnp->cn_thread);
	cache_purge(ITOV(ip));
	if (vn_lock(dvp, LK_EXCLUSIVE | LK_NOWAIT) != 0) {
		VOP_UNLOCK(vp, 0);
		vn_lock(dvp, LK_EXCLUSIVE | LK_RETRY);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	}
out:
	return (error);
}

/*
 * symlink -- make a symbolic link
 */
static int
ext2_symlink(struct vop_symlink_args *ap)
{
	struct vnode *vp, **vpp = ap->a_vpp;
	struct inode *ip;
	int len, error;

	error = ext2_makeinode(IFLNK | ap->a_vap->va_mode, ap->a_dvp,
	    vpp, ap->a_cnp);
	if (error)
		return (error);
	vp = *vpp;
	len = strlen(ap->a_target);
	if (len < vp->v_mount->mnt_maxsymlinklen) {
		ip = VTOI(vp);
		bcopy(ap->a_target, (char *)ip->i_shortlink, len);
		ip->i_size = len;
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
	} else
		error = vn_rdwr(UIO_WRITE, vp, ap->a_target, len, (off_t)0,
		    UIO_SYSSPACE, IO_NODELOCKED | IO_NOMACCHECK,
		    ap->a_cnp->cn_cred, NOCRED, NULL, NULL);
	if (error)
		vput(vp);
	return (error);
}

/*
 * Return target name of a symbolic link
 */
static int
ext2_readlink(struct vop_readlink_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	int isize;

	isize = ip->i_size;
	if (isize < vp->v_mount->mnt_maxsymlinklen) {
		uiomove((char *)ip->i_shortlink, isize, ap->a_uio);
		return (0);
	}
	return (VOP_READ(vp, ap->a_uio, 0, ap->a_cred));
}

/*
 * Calculate the logical to physical mapping if not done already,
 * then call the device strategy routine.
 *
 * In order to be able to swap to a file, the ext2_bmaparray() operation may not
 * deadlock on memory.  See ext2_bmap() for details.
 */
static int
ext2_strategy(struct vop_strategy_args *ap)
{
	struct buf *bp = ap->a_bp;
	struct vnode *vp = ap->a_vp;
	struct bufobj *bo;
	daddr_t blkno;
	int error;

	if (vp->v_type == VBLK || vp->v_type == VCHR)
		panic("ext2_strategy: spec");
	if (bp->b_blkno == bp->b_lblkno) {

		if (VTOI(ap->a_vp)->i_flag & IN_E4EXTENTS)
			error = ext4_bmapext(vp, bp->b_lblkno, &blkno, NULL, NULL);
		else
			error = ext2_bmaparray(vp, bp->b_lblkno, &blkno, NULL, NULL);

		bp->b_blkno = blkno;
		if (error) {
			bp->b_error = error;
			bp->b_ioflags |= BIO_ERROR;
			bufdone(bp);
			return (0);
		}
		if ((long)bp->b_blkno == -1)
			vfs_bio_clrbuf(bp);
	}
	if ((long)bp->b_blkno == -1) {
		bufdone(bp);
		return (0);
	}
	bp->b_iooffset = dbtob(bp->b_blkno);
	bo = VFSTOEXT2(vp->v_mount)->um_bo;
	BO_STRATEGY(bo, bp);
	return (0);
}

/*
 * Print out the contents of an inode.
 */
static int
ext2_print(struct vop_print_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);

	vn_printf(ip->i_devvp, "\tino %ju", (uintmax_t)ip->i_number);
	if (vp->v_type == VFIFO)
		fifo_printinfo(vp);
	printf("\n");
	return (0);
}

/*
 * Close wrapper for fifos.
 *
 * Update the times on the inode then do device close.
 */
static int
ext2fifo_close(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp;

	VI_LOCK(vp);
	if (vp->v_usecount > 1)
		ext2_itimes_locked(vp);
	VI_UNLOCK(vp);
	return (fifo_specops.vop_close(ap));
}

/*
 * Kqfilter wrapper for fifos.
 *
 * Fall through to ext2 kqfilter routines if needed
 */
static int
ext2fifo_kqfilter(struct vop_kqfilter_args *ap)
{
	int error;

	error = fifo_specops.vop_kqfilter(ap);
	if (error)
		error = vfs_kqfilter(ap);
	return (error);
}

/*
 * Return POSIX pathconf information applicable to ext2 filesystems.
 * 返回适用于ext2文件系统的POSIX pathconf信息
 */
static int
ext2_pathconf(struct vop_pathconf_args *ap)
{
	int error = 0;

	switch (ap->a_name) {
	case _PC_LINK_MAX:
		if (EXT2_HAS_RO_COMPAT_FEATURE(VTOI(ap->a_vp)->i_e2fs,
		    EXT2F_ROCOMPAT_DIR_NLINK))
			*ap->a_retval = INT_MAX;
		else
			*ap->a_retval = EXT4_LINK_MAX;
		break;
	case _PC_NAME_MAX:
		*ap->a_retval = NAME_MAX;
		break;
	case _PC_PIPE_BUF:
		if (ap->a_vp->v_type == VDIR || ap->a_vp->v_type == VFIFO)
			*ap->a_retval = PIPE_BUF;
		else
			error = EINVAL;
		break;
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		break;
	case _PC_NO_TRUNC:
		*ap->a_retval = 1;
		break;

#ifdef UFS_ACL
	case _PC_ACL_EXTENDED:
		if (ap->a_vp->v_mount->mnt_flag & MNT_ACLS)
			*ap->a_retval = 1;
		else
			*ap->a_retval = 0;
		break;
	case _PC_ACL_PATH_MAX:
		if (ap->a_vp->v_mount->mnt_flag & MNT_ACLS)
			*ap->a_retval = ACL_MAX_ENTRIES;
		else
			*ap->a_retval = 3;
		break;
#endif /* UFS_ACL */

	case _PC_MIN_HOLE_SIZE:
		*ap->a_retval = ap->a_vp->v_mount->mnt_stat.f_iosize;
		break;
	case _PC_PRIO_IO:
		*ap->a_retval = 0;
		break;
	case _PC_SYNC_IO:
		*ap->a_retval = 0;
		break;
	case _PC_ALLOC_SIZE_MIN:
		*ap->a_retval = ap->a_vp->v_mount->mnt_stat.f_bsize;
		break;
	case _PC_FILESIZEBITS:
		*ap->a_retval = 64;
		break;
	case _PC_REC_INCR_XFER_SIZE:
		*ap->a_retval = ap->a_vp->v_mount->mnt_stat.f_iosize;
		break;
	case _PC_REC_MAX_XFER_SIZE:
		*ap->a_retval = -1;	/* means ``unlimited'' */
		break;
	case _PC_REC_MIN_XFER_SIZE:
		*ap->a_retval = ap->a_vp->v_mount->mnt_stat.f_iosize;
		break;
	case _PC_REC_XFER_ALIGN:
		*ap->a_retval = PAGE_SIZE;
		break;
	case _PC_SYMLINK_MAX:
		*ap->a_retval = MAXPATHLEN;
		break;

	default:
		error = vop_stdpathconf(ap);
		break;
	}
	return (error);
}

/*
 * Vnode operation to remove a named attribute.
 */
static int
ext2_deleteextattr(struct vop_deleteextattr_args *ap)
{
	struct inode *ip;
	struct m_ext2fs *fs;
	int error;

	ip = VTOI(ap->a_vp);
	fs = ip->i_e2fs;

	if (!EXT2_HAS_COMPAT_FEATURE(ip->i_e2fs, EXT2F_COMPAT_EXT_ATTR))
		return (EOPNOTSUPP);

	if (ap->a_vp->v_type == VCHR || ap->a_vp->v_type == VBLK)
		return (EOPNOTSUPP);

	error = extattr_check_cred(ap->a_vp, ap->a_attrnamespace,
	    ap->a_cred, ap->a_td, VWRITE);
	if (error)
		return (error);

	error = ENOATTR;

	if (EXT2_INODE_SIZE(fs) != E2FS_REV0_INODE_SIZE) {
		error = ext2_extattr_inode_delete(ip, ap->a_attrnamespace, ap->a_name);
		if (error != ENOATTR)
			return (error);
	}

	if (ip->i_facl)
		error = ext2_extattr_block_delete(ip, ap->a_attrnamespace, ap->a_name);

	return (error);
}

/*
 * Vnode operation to retrieve a named extended attribute.
 */
static int
ext2_getextattr(struct vop_getextattr_args *ap)
{
	struct inode *ip;
	struct m_ext2fs *fs;
	int error;

	ip = VTOI(ap->a_vp);
	fs = ip->i_e2fs;

	if (!EXT2_HAS_COMPAT_FEATURE(ip->i_e2fs, EXT2F_COMPAT_EXT_ATTR))
		return (EOPNOTSUPP);

	if (ap->a_vp->v_type == VCHR || ap->a_vp->v_type == VBLK)
		return (EOPNOTSUPP);

	error = extattr_check_cred(ap->a_vp, ap->a_attrnamespace,
	    ap->a_cred, ap->a_td, VREAD);
	if (error)
		return (error);

	if (ap->a_size != NULL)
		*ap->a_size = 0;

	error = ENOATTR;

	if (EXT2_INODE_SIZE(fs) != E2FS_REV0_INODE_SIZE) {
		error = ext2_extattr_inode_get(ip, ap->a_attrnamespace,
		    ap->a_name, ap->a_uio, ap->a_size);
		if (error != ENOATTR)
			return (error);
	}

	if (ip->i_facl)
		error = ext2_extattr_block_get(ip, ap->a_attrnamespace,
		    ap->a_name, ap->a_uio, ap->a_size);

	return (error);
}

/*
 * Vnode operation to retrieve extended attributes on a vnode.
 */
static int
ext2_listextattr(struct vop_listextattr_args *ap)
{
	struct inode *ip;
	struct m_ext2fs *fs;
	int error;

	ip = VTOI(ap->a_vp);
	fs = ip->i_e2fs;

	if (!EXT2_HAS_COMPAT_FEATURE(ip->i_e2fs, EXT2F_COMPAT_EXT_ATTR))
		return (EOPNOTSUPP);

	if (ap->a_vp->v_type == VCHR || ap->a_vp->v_type == VBLK)
		return (EOPNOTSUPP);

	error = extattr_check_cred(ap->a_vp, ap->a_attrnamespace,
	    ap->a_cred, ap->a_td, VREAD);
	if (error)
		return (error);

	if (ap->a_size != NULL)
		*ap->a_size = 0;

	if (EXT2_INODE_SIZE(fs) != E2FS_REV0_INODE_SIZE) {
		error = ext2_extattr_inode_list(ip, ap->a_attrnamespace,
		    ap->a_uio, ap->a_size);
		if (error)
			return (error);
	}

	if (ip->i_facl)
		error = ext2_extattr_block_list(ip, ap->a_attrnamespace,
		    ap->a_uio, ap->a_size);

	return (error);
}

/*
 * Vnode operation to set a named attribute.
 */
static int
ext2_setextattr(struct vop_setextattr_args *ap)
{
	struct inode *ip;
	struct m_ext2fs *fs;
	int error;

	ip = VTOI(ap->a_vp);
	fs = ip->i_e2fs;

	if (!EXT2_HAS_COMPAT_FEATURE(ip->i_e2fs, EXT2F_COMPAT_EXT_ATTR))
		return (EOPNOTSUPP);

	if (ap->a_vp->v_type == VCHR || ap->a_vp->v_type == VBLK)
		return (EOPNOTSUPP);

	error = extattr_check_cred(ap->a_vp, ap->a_attrnamespace,
	    ap->a_cred, ap->a_td, VWRITE);
	if (error)
		return (error);

	error = ext2_extattr_valid_attrname(ap->a_attrnamespace, ap->a_name);
	if (error)
		return (error);

	if (EXT2_INODE_SIZE(fs) != E2FS_REV0_INODE_SIZE) {
		error = ext2_extattr_inode_set(ip, ap->a_attrnamespace,
		    ap->a_name, ap->a_uio);
		if (error != ENOSPC)
			return (error);
	}

	error = ext2_extattr_block_set(ip, ap->a_attrnamespace,
	    ap->a_name, ap->a_uio);

	return (error);
}

/*
 * Vnode pointer to File handle
 */
/* ARGSUSED */
static int
ext2_vptofh(struct vop_vptofh_args *ap)
{
	struct inode *ip;
	struct ufid *ufhp;

	ip = VTOI(ap->a_vp);
	ufhp = (struct ufid *)ap->a_fhp;
	ufhp->ufid_len = sizeof(struct ufid);
	ufhp->ufid_ino = ip->i_number;
	ufhp->ufid_gen = ip->i_gen;
	return (0);
}

/*
 * Initialize the vnode associated with a new inode, handle aliased
 * vnodes.
 */
int
ext2_vinit(struct mount *mntp, struct vop_vector *fifoops, struct vnode **vpp)
{
	struct inode *ip;
	struct vnode *vp;

	vp = *vpp;
	ip = VTOI(vp);
	vp->v_type = IFTOVT(ip->i_mode);
	if (vp->v_type == VFIFO)
		vp->v_op = fifoops;

	if (ip->i_number == EXT2_ROOTINO)
		vp->v_vflag |= VV_ROOT;
	ip->i_modrev = init_va_filerev();
	*vpp = vp;
	return (0);
}

/*
 * Allocate a new inode.
 * 通过 ext2_valloc 函数申请一个新的 vnode，然后根据父目录项对应的 inode 的信息初始化其对应的
 * inode 的信息(gid 和 uid 之类的)
 */
static int
ext2_makeinode(int mode, struct vnode *dvp, struct vnode **vpp,
    struct componentname *cnp)
{
	struct inode *ip, *pdir;	/* present/parent directory inode ? */
	struct vnode *tvp;	/* target vnode pointer */
	int error;

	pdir = VTOI(dvp);	/* 获取父目录对应的 inode？ */
#ifdef INVARIANTS
	if ((cnp->cn_flags & HASBUF) == 0)
		panic("ext2_makeinode: no name");
#endif
	*vpp = NULL;	/* vpp初始化为空 */
	if ((mode & IFMT) == 0)
		mode |= IFREG;	/* 创建一个常规文件 */
	/* 
		组件名称中包含有认证信息，mode 是根据 vnode 属性计算出来的模式值。
		dvp 表示的应该是所要当前目录所对应的 vnode(directory vnode)，新获取的 inode 对应到
		tvp(target vnode pointer)
		这里也会将刚才处理过的 mode 传入 valloc 函数中
	*/
	error = ext2_valloc(dvp, mode, cnp->cn_cred, &tvp);
	if (error) {
		return (error);
	}
	ip = VTOI(tvp);	/* target vnode 对应的 inode，应该就是 alloc 函数申请的 */
	ip->i_gid = pdir->i_gid;	/* 貌似都是这种用法，利用父目录对应的 inode 给新的 inode gid 赋值 */
	/* 后续对父目录 inode 的操作基本上就没了，主要还是对 target inode 操作 */
#ifdef SUIDDIR
	{
		/*
		 * if we are
		 * not the owner of the directory,
		 * and we are hacking owners here, (only do this where told to)
		 * and we are not giving it TOO root, (would subvert quotas)
		 * then go ahead and give it to the other user.
		 * Note that this drops off the execute bits for security.
		 */
		if ((dvp->v_mount->mnt_flag & MNT_SUIDDIR) &&
		    (pdir->i_mode & ISUID) &&
		    (pdir->i_uid != cnp->cn_cred->cr_uid) && pdir->i_uid) {
			ip->i_uid = pdir->i_uid;
			mode &= ~07111;
		} else {
			ip->i_uid = cnp->cn_cred->cr_uid;
		}
	}
#else
	ip->i_uid = cnp->cn_cred->cr_uid;
#endif
	ip->i_flag |= IN_ACCESS | IN_CHANGE | IN_UPDATE;
	ip->i_mode = mode;
	tvp->v_type = IFTOVT(mode);	/* Rest init'd in getnewvnode(). */
	ip->i_nlink = 1;
	if ((ip->i_mode & ISGID) && !groupmember(ip->i_gid, cnp->cn_cred)) {
		if (priv_check_cred(cnp->cn_cred, PRIV_VFS_RETAINSUGID, 0))
			ip->i_mode &= ~ISGID;
	}

	if (cnp->cn_flags & ISWHITEOUT)
		ip->i_flags |= UF_OPAQUE;

	/*
	 * Make sure inode goes to disk before directory entry.
	 * 确保inode在目录条目之前转到磁盘
	 */
	error = ext2_update(tvp, !DOINGASYNC(tvp));
	if (error)
		goto bad;

#ifdef UFS_ACL
	if (dvp->v_mount->mnt_flag & MNT_ACLS) {
		error = ext2_do_posix1e_acl_inheritance_file(dvp, tvp, mode,
		    cnp->cn_cred, cnp->cn_thread);
		if (error)
			goto bad;
	}
#endif /* UFS_ACL */
	/*
		要注意这里的实现，ip 表示的是通过 valloc 申请到的 vnode 所对应的 inode (通过 malloc 实例化)。
		dvp 则表示的是当前目录对应的 vnode，其实主要是为了把 mount，超级块和一些属性信息传递给目标 inode
	*/
	error = ext2_direnter(ip, dvp, cnp);
	if (error)
		goto bad;

	*vpp = tvp;
	return (0);

bad:
	/*
	 * Write error occurred trying to update the inode
	 * or the directory so must deallocate the inode.
	 * 尝试更新 inode 或目录时发生写入错误，因此必须取消分配 inode
	 */
	ip->i_nlink = 0;
	ip->i_flag |= IN_CHANGE;
	vput(tvp);
	return (error);
}

/*
 * Vnode op for reading.
 */
static int
ext2_read(struct vop_read_args *ap)
{
	struct vnode *vp;
	struct inode *ip;
	struct uio *uio;
	struct m_ext2fs *fs;
	struct buf *bp;
	daddr_t lbn, nextlbn;	/* logical block number */
	off_t bytesinfile;	/* 在文件中的偏移量(以字节为单位) */
	long size, xfersize, blkoffset;
	int error, orig_resid, seqcount;
	int ioflag;

	vp = ap->a_vp;
	uio = ap->a_uio;
	ioflag = ap->a_ioflag;

	seqcount = ap->a_ioflag >> IO_SEQSHIFT;
	ip = VTOI(vp);

#ifdef INVARIANTS
	if (uio->uio_rw != UIO_READ)
		panic("%s: mode", "ext2_read");

	if (vp->v_type == VLNK) {
		if ((int)ip->i_size < vp->v_mount->mnt_maxsymlinklen)
			panic("%s: short symlink", "ext2_read");
	} else if (vp->v_type != VREG && vp->v_type != VDIR)
		panic("%s: type %d", "ext2_read", vp->v_type);
#endif
	orig_resid = uio->uio_resid;	/* 剩余未处理的字节数 */
	KASSERT(orig_resid >= 0, ("ext2_read: uio->uio_resid < 0"));
	if (orig_resid == 0)
		return (0);	/* 处理完毕，返回0 */
	KASSERT(uio->uio_offset >= 0, ("ext2_read: uio->uio_offset < 0"));
	fs = ip->i_e2fs;
	if (uio->uio_offset < ip->i_size &&
	    uio->uio_offset >= fs->e2fs_maxfilesize)
		return (EOVERFLOW);	/* 如果传输的数据量大于规定的最大文件size，返回错误码 */

	for (error = 0, bp = NULL; uio->uio_resid > 0; bp = NULL) {
		if ((bytesinfile = ip->i_size - uio->uio_offset) <= 0)
			break;
		lbn = lblkno(fs, uio->uio_offset);
		nextlbn = lbn + 1;
		size = blksize(fs, ip, lbn);
		blkoffset = blkoff(fs, uio->uio_offset);
		/*
			blkoffset 表示的应该是 uio_offset 在块中的偏移，uio_resid 表示的应该是在一个块中剩余需要处理的
			数据，而不是整个文件剩余的需要被处理的数据量。bytesinfile 表示的才是整个文件剩余数据量，如果它比 
			xfersize 还要小，说明文件整体需要处理的数据量已经不足以填满最后一个数据块、blkoffset之后剩余的空间，
			此时就将 xfersize 设置为 bytesinfile
		*/
		xfersize = fs->e2fs_fsize - blkoffset;
		if (uio->uio_resid < xfersize)
			xfersize = uio->uio_resid;
		if (bytesinfile < xfersize)
			xfersize = bytesinfile;

		/*
			当文件系统支持 cluster_read 属性的时候，可以采用对应的功能函数。也就是说不采用也是可以
			实现的。tptfs 直接将其省略掉。
			seqcount 应该是跟磁盘数据块读写有关系，为了提高效率将数据块按照一定的顺序进行排序。该变量应该是
			起到这个作用
		*/
		if (lblktosize(fs, nextlbn) >= ip->i_size)
			error = bread(vp, lbn, size, NOCRED, &bp);
		else if ((vp->v_mount->mnt_flag & MNT_NOCLUSTERR) == 0) {
			error = cluster_read(vp, ip->i_size, lbn, size,
			    NOCRED, blkoffset + uio->uio_resid, seqcount,
			    0, &bp);
		} else if (seqcount > 1) {
			u_int nextsize = blksize(fs, ip, nextlbn);

			error = breadn(vp, lbn,
			    size, &nextlbn, &nextsize, 1, NOCRED, &bp);
		} else
			error = bread(vp, lbn, size, NOCRED, &bp);
		if (error) {
			brelse(bp);
			bp = NULL;
			break;
		}

		/*
		 * We should only get non-zero b_resid when an I/O error
		 * has occurred, which should cause us to break above.
		 * However, if the short read did not cause an error,
		 * then we want to ensure that we do not uiomove bad
		 * or uninitialized data.
		 */
		size -= bp->b_resid;
		if (size < xfersize) {
			if (size == 0)
				break;
			xfersize = size;
		}
		error = uiomove((char *)bp->b_data + blkoffset,
		    (int)xfersize, uio);
		if (error)
			break;
		vfs_bio_brelse(bp, ioflag);
	}

	/*
	 * This can only happen in the case of an error because the loop
	 * above resets bp to NULL on each iteration and on normal
	 * completion has not set a new value into it. so it must have come
	 * from a 'break' statement
	 */
	if (bp != NULL)
		vfs_bio_brelse(bp, ioflag);

	if ((error == 0 || uio->uio_resid != orig_resid) &&
	    (vp->v_mount->mnt_flag & (MNT_NOATIME | MNT_RDONLY)) == 0)
		ip->i_flag |= IN_ACCESS;
	return (error);
}

static int
ext2_ioctl(struct vop_ioctl_args *ap)
{

	switch (ap->a_command) {
	case FIOSEEKDATA:
	case FIOSEEKHOLE:
		return (vn_bmap_seekhole(ap->a_vp, ap->a_command,
		    (off_t *)ap->a_data, ap->a_cred));
	default:
		return (ENOTTY);
	}
}

/*
 * Vnode op for writing.
 */
static int
ext2_write(struct vop_write_args *ap)
{
	struct vnode *vp;
	struct uio *uio;
	struct inode *ip;
	struct m_ext2fs *fs;
	struct buf *bp;
	daddr_t lbn;
	off_t osize;
	int blkoffset, error, flags, ioflag, resid, size, seqcount, xfersize;

	ioflag = ap->a_ioflag;
	uio = ap->a_uio;
	vp = ap->a_vp;

	seqcount = ioflag >> IO_SEQSHIFT;
	ip = VTOI(vp);

#ifdef INVARIANTS
	if (uio->uio_rw != UIO_WRITE)
		panic("%s: mode", "ext2_write");
#endif

	switch (vp->v_type) {
	case VREG:
		if (ioflag & IO_APPEND)
			uio->uio_offset = ip->i_size;
		if ((ip->i_flags & APPEND) && uio->uio_offset != ip->i_size)
			return (EPERM);
		/* FALLTHROUGH */
	case VLNK:
		break;
	case VDIR:
		/* XXX differs from ffs -- this is called from ext2_mkdir(). */
		if ((ioflag & IO_SYNC) == 0)
			panic("ext2_write: nonsync dir write");
		break;
	default:
		panic("ext2_write: type %p %d (%jd,%jd)", (void *)vp,
		    vp->v_type, (intmax_t)uio->uio_offset,
		    (intmax_t)uio->uio_resid);
	}

	KASSERT(uio->uio_resid >= 0, ("ext2_write: uio->uio_resid < 0"));
	KASSERT(uio->uio_offset >= 0, ("ext2_write: uio->uio_offset < 0"));
	fs = ip->i_e2fs;
	if ((uoff_t)uio->uio_offset + uio->uio_resid > fs->e2fs_maxfilesize)
		return (EFBIG);
	/*
	 * Maybe this should be above the vnode op call, but so long as
	 * file servers have no limits, I don't think it matters.
	 * 也许这应该在vnode op调用之上，但是只要文件服务器没有限制，我认为这无关紧要
	 */
	/*
		文件大小限制判断
	*/
	if (vn_rlimit_fsize(vp, uio, uio->uio_td))
		return (EFBIG);

	resid = uio->uio_resid;
	osize = ip->i_size;
	if (seqcount > BA_SEQMAX)
		flags = BA_SEQMAX << BA_SEQSHIFT;
	else
		flags = seqcount << BA_SEQSHIFT;
	if ((ioflag & IO_SYNC) && !DOINGASYNC(vp))
		flags |= IO_SYNC;

	for (error = 0; uio->uio_resid > 0;) {
		lbn = lblkno(fs, uio->uio_offset);	/* 求出逻辑块号(offset / block_size)，可以理解为定位到要修改的块 */
		blkoffset = blkoff(fs, uio->uio_offset);	/* 求出在数据块中的偏移 */
		xfersize = fs->e2fs_fsize - blkoffset;	/* 整个数据块被分割后剩下的区域大小 */
		/*
			如果剩余需要处理的数据的字节数小于剩余块区域的大小，那就直接对剩下的块区域进行赋值，
			也就是说明我们还需要处理的数据在最后一个块中就可存放完成，而不需要再重新申请数据块
		*/
		if (uio->uio_resid < xfersize)
			xfersize = uio->uio_resid;
		/*
			i_size 表示的是文件以字节为单位的大小。这里需要考虑一种情况，就是文件占用的数据块中，最后一个数据块是没有
			填满的，所以通过数据块计算的文件的大小是要大于文件的实际大小的。这里其实就相当于是对文件的大小进行了扩展
		*/
		if (uio->uio_offset + xfersize > ip->i_size)
			vnode_pager_setsize(vp, uio->uio_offset + xfersize);

		/*
		 * We must perform a read-before-write if the transfer size
		 * does not cover the entire buffer.
		 * 如果传输大小不能覆盖整个缓冲区，则必须先读后写。缓冲区应该也是以页作为最小操作单元。如果我们的数据操作
		 * 是在某个文件的中间，但是这个文件中的一些内容已经在这个页上存在，我们需要将其进行替换，可能就需要先把原有
		 * 的数据读出来，把内容替换之后在重新写入磁盘对应的位置
		 */
		if (fs->e2fs_bsize > xfersize)
			flags |= BA_CLRBUF;
		else
			flags &= ~BA_CLRBUF;
		error = ext2_balloc(ip, lbn, blkoffset + xfersize,
		    ap->a_cred, &bp, flags);
		if (error != 0)
			break;

		if ((ioflag & (IO_SYNC | IO_INVAL)) == (IO_SYNC | IO_INVAL))
			bp->b_flags |= B_NOCACHE;

		if (uio->uio_offset + xfersize > ip->i_size)
			ip->i_size = uio->uio_offset + xfersize;
		size = blksize(fs, ip, lbn) - bp->b_resid;

		if (size < xfersize)
			xfersize = size;

		error =
		    uiomove((char *)bp->b_data + blkoffset, (int)xfersize, uio);
		/*
		 * If the buffer is not already filled and we encounter an
		 * error while trying to fill it, we have to clear out any
		 * garbage data from the pages instantiated for the buffer.
		 * If we do not, a failed uiomove() during a write can leave
		 * the prior contents of the pages exposed to a userland mmap.
		 * 如果缓冲区还没有被填充，并且在尝试填充时遇到错误，那么我们必须从为缓冲区实例化的
		 * 页面中清除所有垃圾数据。如果不这样做，则在写入期间失败的 uiomove 会将页面的先前
		 * 内容暴露给userland mmap
		 *
		 * Note that we need only clear buffers with a transfer size
		 * equal to the block size because buffers with a shorter
		 * transfer size were cleared above by the call to ext2_balloc()
		 * with the BA_CLRBUF flag set.
		 * 请注意，我们只需要清除传输大小等于块大小的缓冲区，因为传输大小较短的缓冲区是通过
		 * 调用 ext2_balloc 并设置 BA_CLRBUF 标志来清除的
		 *
		 * If the source region for uiomove identically mmaps the
		 * buffer, uiomove() performed the NOP copy, and the buffer
		 * content remains valid because the page fault handler
		 * validated the pages.
		 */
		if (error != 0 && (bp->b_flags & B_CACHE) == 0 &&
		    fs->e2fs_bsize == xfersize)
			vfs_bio_clrbuf(bp);

		vfs_bio_set_flags(bp, ioflag);

		/*
		 * If IO_SYNC each buffer is written synchronously.  Otherwise
		 * if we have a severe page deficiency write the buffer
		 * asynchronously.  Otherwise try to cluster, and if that
		 * doesn't do it then either do an async write (if O_DIRECT),
		 * or a delayed write (if not).
		 */
		if (ioflag & IO_SYNC) {
			(void)bwrite(bp);
		} else if (vm_page_count_severe() ||
			    buf_dirty_count_severe() ||
		    (ioflag & IO_ASYNC)) {
			bp->b_flags |= B_CLUSTEROK;
			bawrite(bp);
		} else if (xfersize + blkoffset == fs->e2fs_fsize) {
			if ((vp->v_mount->mnt_flag & MNT_NOCLUSTERW) == 0) {
				bp->b_flags |= B_CLUSTEROK;
				cluster_write(vp, bp, ip->i_size, seqcount, 0);
			} else {
				bawrite(bp);
			}
		} else if (ioflag & IO_DIRECT) {
			bp->b_flags |= B_CLUSTEROK;
			bawrite(bp);
		} else {
			bp->b_flags |= B_CLUSTEROK;
			bdwrite(bp);
		}
		if (error || xfersize == 0)
			break;
	}	// end of for uio

	/*
	 * If we successfully wrote any data, and we are not the superuser
	 * we clear the setuid and setgid bits as a precaution against
	 * tampering.
	 */
	if ((ip->i_mode & (ISUID | ISGID)) && resid > uio->uio_resid &&
	    ap->a_cred) {
		if (priv_check_cred(ap->a_cred, PRIV_VFS_RETAINSUGID, 0))
			ip->i_mode &= ~(ISUID | ISGID);
	}
	if (error) {
		if (ioflag & IO_UNIT) {
			(void)ext2_truncate(vp, osize,
			    ioflag & IO_SYNC, ap->a_cred, uio->uio_td);
			uio->uio_offset -= resid - uio->uio_resid;
			uio->uio_resid = resid;
		}
	}
	if (uio->uio_resid != resid) {
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		if (ioflag & IO_SYNC)
			error = ext2_update(vp, 1);
	}
	return (error);
}
