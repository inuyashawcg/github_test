/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2004
 *	Poul-Henning Kamp.  All rights reserved.
 * Copyright (c) 1989, 1992-1993, 1995
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)kernfs_vnops.c	8.15 (Berkeley) 5/21/95
 * From: FreeBSD: src/sys/miscfs/kernfs/kernfs_vnops.c 1.43
 *
 * $FreeBSD: releng/12.0/sys/fs/devfs/devfs_vnops.c 339186 2018-10-04 23:55:03Z brooks $
 */

/*
 * TODO:
 *	mkdir: want it ?
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/dirent.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/filio.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/ttycom.h>
#include <sys/unistd.h>
#include <sys/vnode.h>

static struct vop_vector devfs_vnodeops;
static struct vop_vector devfs_specops;
static struct fileops devfs_ops_f;

#include <fs/devfs/devfs.h>
#include <fs/devfs/devfs_int.h>

#include <security/mac/mac_framework.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>

static MALLOC_DEFINE(M_CDEVPDATA, "DEVFSP", "Metainfo for cdev-fp data");

// 通过 sysctl 控制相关的参数
struct mtx	devfs_de_interlock;
MTX_SYSINIT(devfs_de_interlock, &devfs_de_interlock, "devfs interlock", MTX_DEF);
struct sx	clone_drain_lock;
SX_SYSINIT(clone_drain_lock, &clone_drain_lock, "clone events drain lock");
struct mtx	cdevpriv_mtx;
MTX_SYSINIT(cdevpriv_mtx, &cdevpriv_mtx, "cdevpriv lock", MTX_DEF);

SYSCTL_DECL(_vfs_devfs);

static int devfs_dotimes;
SYSCTL_INT(_vfs_devfs, OID_AUTO, dotimes, CTLFLAG_RW,
    &devfs_dotimes, 0, "Update timestamps on DEVFS with default precision");

/*
 * Update devfs node timestamp.  Note that updates are unlocked and
 * stat(2) could see partially updated times.
 * 更新时间戳timestamp
 */
static void
devfs_timestamp(struct timespec *tsp)
{
	time_t ts;

	if (devfs_dotimes) {
		vfs_timestamp(tsp);
	} else {
		ts = time_second;
		if (tsp->tv_sec != ts) {
			tsp->tv_sec = ts;
			tsp->tv_nsec = 0;
		}
	}
}

/* 对于file的属性检测 */
static int
devfs_fp_check(struct file *fp, struct cdev **devp, struct cdevsw **dswp,
    int *ref)
{
	/* 获取 vnode 对应的 cdev 的 cdevsw */
	*dswp = devvn_refthread(fp->f_vnode, devp, ref);
	if (*devp != fp->f_data) {
		if (*dswp != NULL)
			dev_relthread(*devp, *ref);
		return (ENXIO);
	}
	KASSERT((*devp)->si_refcount > 0,
	    ("devfs: un-referenced struct cdev *(%s)", devtoname(*devp)));
	if (*dswp == NULL)
		return (ENXIO);
	curthread->td_fpop = fp;
	return (0);
}

/*
	获取cdev的私有数据
	这里扩展一下，当我们对一个文件进行操作的时候，肯定是通过某一个进程或者线程来进行的。这个时候
	文件会被映射到进程的地址空间，td_fpop 成员貌似表示的就是线程所要操作的设备文件
*/ 
int
devfs_get_cdevpriv(void **datap)
{
	struct file *fp;
	struct cdev_privdata *p;
	int error;

	/* 当前线程会关联一个file， file中包含着 cdev private data */
	fp = curthread->td_fpop;
	if (fp == NULL)
		return (EBADF);
	p = fp->f_cdevpriv;
	if (p != NULL) {
		error = 0;
		*datap = p->cdpd_data;
	} else
		error = ENOENT;
	return (error);
}

/* 
	devfs_***_cdevpriv 主要是为了允许cdev驱动程序方法将一些特定于驱动程序的数据与
	设备特殊文件的每个用户进程open（2）相关联
*/
int
devfs_set_cdevpriv(void *priv, d_priv_dtor_t *priv_dtr)
{
	struct file *fp;
	struct cdev_priv *cdp;
	struct cdev_privdata *p;
	int error;

	fp = curthread->td_fpop;  // 这里也是跟上面相同的操作，获取当前线程对应的文件
	if (fp == NULL)
		return (ENOENT);
	cdp = cdev2priv((struct cdev *)fp->f_data);
	p = malloc(sizeof(struct cdev_privdata), M_CDEVPDATA, M_WAITOK);
	p->cdpd_data = priv;
	p->cdpd_dtr = priv_dtr;
	p->cdpd_fp = fp;
	mtx_lock(&cdevpriv_mtx);
	if (fp->f_cdevpriv == NULL) {
		/*
			这里涉及到了 cdp_fdpriv 队列，貌似跟文件描述符有什么联系？
		*/ 
		LIST_INSERT_HEAD(&cdp->cdp_fdpriv, p, cdpd_list);
		fp->f_cdevpriv = p;
		mtx_unlock(&cdevpriv_mtx);
		error = 0;
	} else {
		mtx_unlock(&cdevpriv_mtx);
		free(p, M_CDEVPDATA);
		error = EBUSY;
	}
	return (error);
}

void
devfs_destroy_cdevpriv(struct cdev_privdata *p)
{

	mtx_assert(&cdevpriv_mtx, MA_OWNED);
	KASSERT(p->cdpd_fp->f_cdevpriv == p,
	    ("devfs_destoy_cdevpriv %p != %p", p->cdpd_fp->f_cdevpriv, p));
	p->cdpd_fp->f_cdevpriv = NULL;

	/* cdev_private 从链表中移除， */
	LIST_REMOVE(p, cdpd_list);
	mtx_unlock(&cdevpriv_mtx);
	(p->cdpd_dtr)(p->cdpd_data);
	free(p, M_CDEVPDATA);
}

static void
devfs_fpdrop(struct file *fp)
{
	struct cdev_privdata *p;

	mtx_lock(&cdevpriv_mtx);
	if ((p = fp->f_cdevpriv) == NULL) {
		mtx_unlock(&cdevpriv_mtx);
		return;
	}
	// 从链表中移除 cdev_privdate
	devfs_destroy_cdevpriv(p);
}

/* 
	该函数以及上面三个函数其实都是对 cdev_privdata 数据进行操作，说明这个数据还是相当重要的
*/
void
devfs_clear_cdevpriv(void)
{
	struct file *fp;

	fp = curthread->td_fpop;
	if (fp == NULL)
		return;
	devfs_fpdrop(fp);
}

/*
 * On success devfs_populate_vp() returns with dmp->dm_lock held.
 * populate: 填充，迁移
 * 
 * vnode其实是有vfs统一管理的，所以这里
 */
static int
devfs_populate_vp(struct vnode *vp)
{
	struct devfs_dirent *de;	// 目录信息
	struct devfs_mount *dmp;	// 挂载点信息
	int locked;

	ASSERT_VOP_LOCKED(vp, "devfs_populate_vp");

	/* 
		将 vnode 对应的 mount 强制类型转换成 devfs_mount
		- 这里不是将 mount 结构体进行强制转换，是将 mount->mnt_data 进行强制转换
	*/
	dmp = VFSTODEVFS(vp->v_mount);
	locked = VOP_ISLOCKED(vp);

	sx_xlock(&dmp->dm_lock);
	DEVFS_DMP_HOLD(dmp);	// dm_holdcnt++

	/* Can't call devfs_populate() with the vnode lock held. */
	VOP_UNLOCK(vp, 0);
	devfs_populate(dmp);

	sx_xunlock(&dmp->dm_lock);
	vn_lock(vp, locked | LK_RETRY);
	sx_xlock(&dmp->dm_lock);
	if (DEVFS_DMP_DROP(dmp)) {
		sx_xunlock(&dmp->dm_lock);
		devfs_unmount_final(dmp);
		return (ERESTART);
	}
	if ((vp->v_iflag & VI_DOOMED) != 0) {
		sx_xunlock(&dmp->dm_lock);
		return (ERESTART);
	}

	/* v_data 表示的是 vnode 的私有数据，从此处可以看到，表示的应该是路径信息 */
	de = vp->v_data;
	KASSERT(de != NULL,
	    ("devfs_populate_vp: vp->v_data == NULL but vnode not doomed"));
	if ((de->de_flags & DE_DOOMED) != 0) {
		sx_xunlock(&dmp->dm_lock);
		return (ERESTART);
	}

	return (0);
}

/*
	vnode pointer to component name？
	获取当前所在目录的名称？？
*/
static int
devfs_vptocnp(struct vop_vptocnp_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode **dvp = ap->a_vpp;
	struct devfs_mount *dmp;
	char *buf = ap->a_buf;
	int *buflen = ap->a_buflen;
	struct devfs_dirent *dd, *de;
	int i, error;

	dmp = VFSTODEVFS(vp->v_mount);

	error = devfs_populate_vp(vp);
	if (error != 0)
		return (error);

	i = *buflen;
	dd = vp->v_data;

	if (vp->v_type == VCHR) {
		i -= strlen(dd->de_cdp->cdp_c.si_name);
		if (i < 0) {
			error = ENOMEM;
			goto finished;
		}
		bcopy(dd->de_cdp->cdp_c.si_name, buf + i,
		    strlen(dd->de_cdp->cdp_c.si_name));
		de = dd->de_dir;
	} else if (vp->v_type == VDIR) {
		if (dd == dmp->dm_rootdir) {
			*dvp = vp;
			vref(*dvp);
			goto finished;
		}
		i -= dd->de_dirent->d_namlen;
		if (i < 0) {
			error = ENOMEM;
			goto finished;
		}
		bcopy(dd->de_dirent->d_name, buf + i,
		    dd->de_dirent->d_namlen);
		de = dd;
	} else {
		error = ENOENT;
		goto finished;
	}
	*buflen = i;
	de = devfs_parent_dirent(de);
	if (de == NULL) {
		error = ENOENT;
		goto finished;
	}
	mtx_lock(&devfs_de_interlock);
	*dvp = de->de_vnode;
	if (*dvp != NULL) {
		VI_LOCK(*dvp);
		mtx_unlock(&devfs_de_interlock);
		vholdl(*dvp);
		VI_UNLOCK(*dvp);
		vref(*dvp);
		vdrop(*dvp);
	} else {
		mtx_unlock(&devfs_de_interlock);
		error = ENOENT;
	}
finished:
	sx_xunlock(&dmp->dm_lock);
	return (error);
}

/*
 * Construct the fully qualified path name relative to the mountpoint.
 * If a NULL cnp is provided, no '/' is appended to the resulting path.
 * 构造相对于装入点的完全限定路径名。如果提供了空cnp，则结果路径中不会追加“/”。感觉
 * 应该还是相对于mount pointer 的路径，不是绝对路径
 */
char *
devfs_fqpn(char *buf, struct devfs_mount *dmp, struct devfs_dirent *dd,
    struct componentname *cnp)
{
	int i;
	struct devfs_dirent *de;

	sx_assert(&dmp->dm_lock, SA_LOCKED);

	i = SPECNAMELEN;
	buf[i] = '\0';
	if (cnp != NULL)
		i -= cnp->cn_namelen;
	if (i < 0)
		 return (NULL);
	if (cnp != NULL)
		bcopy(cnp->cn_nameptr, buf + i, cnp->cn_namelen);
	de = dd;
	while (de != dmp->dm_rootdir) {
		if (cnp != NULL || i < SPECNAMELEN) {
			i--;
			if (i < 0)
				 return (NULL);
			buf[i] = '/';
		}
		i -= de->de_dirent->d_namlen;
		if (i < 0)
			 return (NULL);
		bcopy(de->de_dirent->d_name, buf + i,
		    de->de_dirent->d_namlen);
		de = devfs_parent_dirent(de);
		if (de == NULL)
			return (NULL);
	}
	return (buf + i);
}

static int
devfs_allocv_drop_refs(int drop_dm_lock, struct devfs_mount *dmp,
	struct devfs_dirent *de)
{
	int not_found;

	not_found = 0;
	if (de->de_flags & DE_DOOMED)
		not_found = 1;
	if (DEVFS_DE_DROP(de)) {
		KASSERT(not_found == 1, ("DEVFS de dropped but not doomed"));
		devfs_dirent_free(de);
	}
	if (DEVFS_DMP_DROP(dmp)) {
		KASSERT(not_found == 1,
			("DEVFS mount struct freed before dirent"));
		not_found = 2;
		sx_xunlock(&dmp->dm_lock);
		devfs_unmount_final(dmp);
	}
	if (not_found == 1 || (drop_dm_lock && not_found != 2))
		sx_unlock(&dmp->dm_lock);
	return (not_found);
}

static void
devfs_insmntque_dtr(struct vnode *vp, void *arg)
{
	struct devfs_dirent *de;

	de = (struct devfs_dirent *)arg;
	mtx_lock(&devfs_de_interlock);
	vp->v_data = NULL;
	de->de_vnode = NULL;
	mtx_unlock(&devfs_de_interlock);
	vgone(vp);
	vput(vp);
}

/*
 * devfs_allocv shall be entered with dmp->dm_lock held, and it drops
 * it on return.
 * devfs allocate vnode ??
 */
int
devfs_allocv(struct devfs_dirent *de, struct mount *mp, int lockmode,
    struct vnode **vpp)
{
	int error;
	struct vnode *vp;
	struct cdev *dev;
	struct devfs_mount *dmp;
	struct cdevsw *dsw;

	dmp = VFSTODEVFS(mp);
	if (de->de_flags & DE_DOOMED) {
		sx_xunlock(&dmp->dm_lock);
		return (ENOENT);
	}
loop:
	DEVFS_DE_HOLD(de);
	DEVFS_DMP_HOLD(dmp);
	mtx_lock(&devfs_de_interlock);

	/* 当目录项对应的vnode为空的时候， 需要申请新的vnode */
	vp = de->de_vnode;
	if (vp != NULL) {
		VI_LOCK(vp);
		mtx_unlock(&devfs_de_interlock);
		sx_xunlock(&dmp->dm_lock);
		vget(vp, lockmode | LK_INTERLOCK | LK_RETRY, curthread);
		sx_xlock(&dmp->dm_lock);
		if (devfs_allocv_drop_refs(0, dmp, de)) {
			vput(vp);
			return (ENOENT); // 没有对应的文件或者目录
		}
		else if ((vp->v_iflag & VI_DOOMED) != 0) {
			mtx_lock(&devfs_de_interlock);
			if (de->de_vnode == vp) {
				de->de_vnode = NULL;
				vp->v_data = NULL;
			}
			mtx_unlock(&devfs_de_interlock);
			vput(vp);
			goto loop;
		}
		sx_xunlock(&dmp->dm_lock);
		*vpp = vp;
		return (0);
	}
	mtx_unlock(&devfs_de_interlock);
	if (de->de_dirent->d_type == DT_CHR) {
		if (!(de->de_cdp->cdp_flags & CDP_ACTIVE)) {
			devfs_allocv_drop_refs(1, dmp, de);
			return (ENOENT);
		}
		dev = &de->de_cdp->cdp_c;
	} else {
		dev = NULL;
	}

	/* devfs申请vnode */
	error = getnewvnode("devfs", mp, &devfs_vnodeops, &vp);
	if (error != 0) {
		devfs_allocv_drop_refs(1, dmp, de);
		printf("devfs_allocv: failed to allocate new vnode\n");
		return (error);
	}

	if (de->de_dirent->d_type == DT_CHR) {
		vp->v_type = VCHR;
		VI_LOCK(vp);
		dev_lock();
		dev_refl(dev);
		/* XXX: v_rdev should be protect by vnode lock */
		vp->v_rdev = dev;
		KASSERT(vp->v_usecount == 1,
		    ("%s %d (%d)\n", __func__, __LINE__, vp->v_usecount));
		dev->si_usecount += vp->v_usecount;
		/* Special casing of ttys for deadfs.  Probably redundant. */
		dsw = dev->si_devsw;
		if (dsw != NULL && (dsw->d_flags & D_TTY) != 0)
			vp->v_vflag |= VV_ISTTY;
		dev_unlock();
		VI_UNLOCK(vp);
		if ((dev->si_flags & SI_ETERNAL) != 0)
			vp->v_vflag |= VV_ETERNALDEV;
		vp->v_op = &devfs_specops;
	} else if (de->de_dirent->d_type == DT_DIR) {
		vp->v_type = VDIR;
	} else if (de->de_dirent->d_type == DT_LNK) {
		vp->v_type = VLNK;
	} else {
		vp->v_type = VBAD;
	}
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY | LK_NOWITNESS);
	VN_LOCK_ASHARE(vp);
	mtx_lock(&devfs_de_interlock);
	vp->v_data = de;
	de->de_vnode = vp;
	mtx_unlock(&devfs_de_interlock);
	error = insmntque1(vp, mp, devfs_insmntque_dtr, de);
	if (error != 0) {
		(void) devfs_allocv_drop_refs(1, dmp, de);
		return (error);
	}
	if (devfs_allocv_drop_refs(0, dmp, de)) {
		vput(vp);
		return (ENOENT);
	}
#ifdef MAC
	mac_devfs_vnode_associate(mp, de, vp);
#endif
	sx_xunlock(&dmp->dm_lock);
	*vpp = vp;
	return (0);
}

/* 从代码逻辑可以看出，该函数主要就是调用 vaccess 函数来检查权限 */
static int
devfs_access(struct vop_access_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct devfs_dirent *de;	// directory entry?
	struct proc *p;
	int error;

	de = vp->v_data;
	/* 如果vnode的类型是目录，执行if分支 */
	if (vp->v_type == VDIR)
		de = de->de_dir;

	// 操作权限检查
	error = vaccess(vp->v_type, de->de_mode, de->de_uid, de->de_gid,
	    ap->a_accmode, ap->a_cred, NULL);
	if (error == 0)
		return (0);
	if (error != EACCES)
		return (error);

	// 如果权限不够，则执行下面的分支代码
	p = ap->a_td->td_proc;
	/* We do, however, allow access to the controlling terminal */
	PROC_LOCK(p);
	if (!(p->p_flag & P_CONTROLT)) {
		PROC_UNLOCK(p);
		return (error);
	}
	if (p->p_session->s_ttydp == de->de_cdp)
		error = 0;
	PROC_UNLOCK(p);
	return (error);
}

_Static_assert(((FMASK | FCNTLFLAGS) & (FLASTCLOSE | FREVOKE)) == 0,
    "devfs-only flag reuse failed");

static int
devfs_close(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp, *oldvp;
	struct thread *td = ap->a_td;
	struct proc *p;
	struct cdev *dev = vp->v_rdev;
	struct cdevsw *dsw;
	int dflags, error, ref, vp_locked;

	/*
	 * XXX: Don't call d_close() if we were called because of
	 * XXX: insmntque1() failure.
	 */
	if (vp->v_data == NULL)
		return (0);

	/*
	 * Hack: a tty device that is a controlling terminal
	 * has a reference from the session structure.
	 * We cannot easily tell that a character device is
	 * a controlling terminal, unless it is the closing
	 * process' controlling terminal.  In that case,
	 * if the reference count is 2 (this last descriptor
	 * plus the session), release the reference from the session.
	 */
	if (td != NULL) {
		p = td->td_proc;
		PROC_LOCK(p);
		if (vp == p->p_session->s_ttyvp) {
			PROC_UNLOCK(p);
			oldvp = NULL;
			sx_xlock(&proctree_lock);
			if (vp == p->p_session->s_ttyvp) {
				SESS_LOCK(p->p_session);
				VI_LOCK(vp);
				if (count_dev(dev) == 2 &&
				    (vp->v_iflag & VI_DOOMED) == 0) {
					p->p_session->s_ttyvp = NULL;
					p->p_session->s_ttydp = NULL;
					oldvp = vp;
				}
				VI_UNLOCK(vp);
				SESS_UNLOCK(p->p_session);
			}
			sx_xunlock(&proctree_lock);
			if (oldvp != NULL)
				vrele(oldvp);
		} else
			PROC_UNLOCK(p);
	}
	/*
	 * We do not want to really close the device if it
	 * is still in use unless we are trying to close it
	 * forcibly. Since every use (buffer, vnode, swap, cmap)
	 * holds a reference to the vnode, and because we mark
	 * any other vnodes that alias this device, when the
	 * sum of the reference counts on all the aliased
	 * vnodes descends to one, we are on last close.
	 */
	dsw = dev_refthread(dev, &ref);
	if (dsw == NULL)
		return (ENXIO);
	dflags = 0;
	VI_LOCK(vp);
	if (vp->v_iflag & VI_DOOMED) {
		/* Forced close. */
		dflags |= FREVOKE | FNONBLOCK;
	} else if (dsw->d_flags & D_TRACKCLOSE) {
		/* Keep device updated on status. */
	} else if (count_dev(dev) > 1) {
		VI_UNLOCK(vp);
		dev_relthread(dev, ref);
		return (0);
	}
	if (count_dev(dev) == 1)
		dflags |= FLASTCLOSE;
	vholdl(vp);
	VI_UNLOCK(vp);
	vp_locked = VOP_ISLOCKED(vp);
	VOP_UNLOCK(vp, 0);
	KASSERT(dev->si_refcount > 0,
	    ("devfs_close() on un-referenced struct cdev *(%s)", devtoname(dev)));
	error = dsw->d_close(dev, ap->a_fflag | dflags, S_IFCHR, td);
	dev_relthread(dev, ref);
	vn_lock(vp, vp_locked | LK_RETRY);
	vdrop(vp);
	return (error);
}

static int
devfs_close_f(struct file *fp, struct thread *td)
{
	int error;
	struct file *fpop;

	/*
	 * NB: td may be NULL if this descriptor is closed due to
	 * garbage collection from a closed UNIX domain socket.
	 */
	fpop = curthread->td_fpop;
	curthread->td_fpop = fp;
	error = vnops.fo_close(fp, td);
	curthread->td_fpop = fpop;

	/*
	 * The f_cdevpriv cannot be assigned non-NULL value while we
	 * are destroying the file.
	 */
	if (fp->f_cdevpriv != NULL)
		devfs_fpdrop(fp);
	return (error);
}

// 获取属性
static int
devfs_getattr(struct vop_getattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;	// 属性信息
	struct devfs_dirent *de;	// 目录信息
	struct devfs_mount *dmp;
	struct cdev *dev;
	struct timeval boottime;
	int error;

	error = devfs_populate_vp(vp);
	if (error != 0)
		return (error);

	dmp = VFSTODEVFS(vp->v_mount);
	sx_xunlock(&dmp->dm_lock);

	de = vp->v_data;	// v_data 表示的是私有数据，每种fs的内容应该是不一样的
	KASSERT(de != NULL, ("Null dirent in devfs_getattr vp=%p", vp));
	if (vp->v_type == VDIR) {
		de = de->de_dir;
		KASSERT(de != NULL,
		    ("Null dir dirent in devfs_getattr vp=%p", vp));
	}
	vap->va_uid = de->de_uid;
	vap->va_gid = de->de_gid;
	vap->va_mode = de->de_mode;
	if (vp->v_type == VLNK)
		vap->va_size = strlen(de->de_symlink);
	else if (vp->v_type == VDIR)
		vap->va_size = vap->va_bytes = DEV_BSIZE;
	else
		vap->va_size = 0;
	if (vp->v_type != VDIR)
		vap->va_bytes = 0;
	vap->va_blocksize = DEV_BSIZE;
	vap->va_type = vp->v_type;

	getboottime(&boottime);
#define fix(aa)							\
	do {							\
		if ((aa).tv_sec <= 3600) {			\
			(aa).tv_sec = boottime.tv_sec;		\
			(aa).tv_nsec = boottime.tv_usec * 1000; \
		}						\
	} while (0)

	if (vp->v_type != VCHR)  {
		fix(de->de_atime);
		vap->va_atime = de->de_atime;
		fix(de->de_mtime);
		vap->va_mtime = de->de_mtime;
		fix(de->de_ctime);
		vap->va_ctime = de->de_ctime;
	} else {
		dev = vp->v_rdev;
		fix(dev->si_atime);
		vap->va_atime = dev->si_atime;
		fix(dev->si_mtime);
		vap->va_mtime = dev->si_mtime;
		fix(dev->si_ctime);
		vap->va_ctime = dev->si_ctime;

		vap->va_rdev = cdev2priv(dev)->cdp_inode;
	}
	vap->va_gen = 0;
	vap->va_flags = 0;
	vap->va_filerev = 0;
	vap->va_nlink = de->de_links;
	vap->va_fileid = de->de_inode;

	return (error);
}

/* ARGSUSED */
static int
devfs_ioctl_f(struct file *fp, u_long com, void *data, struct ucred *cred, struct thread *td)
{
	struct file *fpop;
	int error;

	fpop = td->td_fpop;
	td->td_fpop = fp;
	error = vnops.fo_ioctl(fp, com, data, cred, td);
	td->td_fpop = fpop;
	return (error);
}

static int
devfs_ioctl(struct vop_ioctl_args *ap)
{
	struct fiodgname_arg *fgn;
	struct vnode *vpold, *vp;
	struct cdevsw *dsw;
	struct thread *td;
	struct cdev *dev;
	int error, ref, i;
	const char *p;
	u_long com;

	vp = ap->a_vp;
	com = ap->a_command;
	td = ap->a_td;

	dsw = devvn_refthread(vp, &dev, &ref);
	if (dsw == NULL)
		return (ENXIO);
	KASSERT(dev->si_refcount > 0,
	    ("devfs: un-referenced struct cdev *(%s)", devtoname(dev)));

	if (com == FIODTYPE) {
		*(int *)ap->a_data = dsw->d_flags & D_TYPEMASK;
		error = 0;
		goto out;
	} else if (com == FIODGNAME) {
		fgn = ap->a_data;
		p = devtoname(dev);
		i = strlen(p) + 1;
		if (i > fgn->len)
			error = EINVAL;
		else
			error = copyout(p, fgn->buf, i);
		goto out;
	}

	error = dsw->d_ioctl(dev, com, ap->a_data, ap->a_fflag, td);

out:
	dev_relthread(dev, ref);
	if (error == ENOIOCTL)
		error = ENOTTY;

	if (error == 0 && com == TIOCSCTTY) {
		/* Do nothing if reassigning same control tty */
		sx_slock(&proctree_lock);
		if (td->td_proc->p_session->s_ttyvp == vp) {
			sx_sunlock(&proctree_lock);
			return (0);
		}

		vpold = td->td_proc->p_session->s_ttyvp;
		VREF(vp);
		SESS_LOCK(td->td_proc->p_session);
		td->td_proc->p_session->s_ttyvp = vp;
		td->td_proc->p_session->s_ttydp = cdev2priv(dev);
		SESS_UNLOCK(td->td_proc->p_session);

		sx_sunlock(&proctree_lock);

		/* Get rid of reference to old control tty */
		if (vpold)
			vrele(vpold);
	}
	return (error);
}

/* ARGSUSED */
static int
devfs_kqfilter_f(struct file *fp, struct knote *kn)
{
	struct cdev *dev;
	struct cdevsw *dsw;
	int error, ref;
	struct file *fpop;
	struct thread *td;

	td = curthread;
	fpop = td->td_fpop;
	error = devfs_fp_check(fp, &dev, &dsw, &ref);
	if (error)
		return (error);
	error = dsw->d_kqfilter(dev, kn);
	td->td_fpop = fpop;
	dev_relthread(dev, ref);
	return (error);
}

static inline int
devfs_prison_check(struct devfs_dirent *de, struct thread *td)
{
	struct cdev_priv *cdp;
	struct ucred *dcr;
	struct proc *p;
	int error;

	cdp = de->de_cdp;
	if (cdp == NULL)
		return (0);
	dcr = cdp->cdp_c.si_cred;
	if (dcr == NULL)
		return (0);

	error = prison_check(td->td_ucred, dcr);
	if (error == 0)
		return (0);
	/* We do, however, allow access to the controlling terminal */
	p = td->td_proc;
	PROC_LOCK(p);
	if (!(p->p_flag & P_CONTROLT)) {
		PROC_UNLOCK(p);
		return (error);
	}
	if (p->p_session->s_ttydp == cdp)
		error = 0;
	PROC_UNLOCK(p);
	return (error);
}

static int
devfs_lookupx(struct vop_lookup_args *ap, int *dm_unlock)
{
	struct componentname *cnp;
	struct vnode *dvp, **vpp; // vnode 指针，vnode 指针的指针
	struct thread *td;
	struct devfs_dirent *de, *dd;
	struct devfs_dirent **dde;
	struct devfs_mount *dmp;
	struct mount *mp;
	struct cdev *cdev;
	int error, flags, nameiop, dvplocked;
	char specname[SPECNAMELEN + 1], *pname;

	cnp = ap->a_cnp;
	vpp = ap->a_vpp;
	dvp = ap->a_dvp;
	pname = cnp->cn_nameptr;
	td = cnp->cn_thread;
	flags = cnp->cn_flags;
	nameiop = cnp->cn_nameiop;
	mp = dvp->v_mount;
	dmp = VFSTODEVFS(mp);
	dd = dvp->v_data;	// vnode -> v_data，不同的文件系统可能有不同的数据定义
	*vpp = NULLVP;

	if ((flags & ISLASTCN) && nameiop == RENAME)
		return (EOPNOTSUPP);

	/* 如果不是目录类型就报错，说明这里操作的对象是目录文件 */
	if (dvp->v_type != VDIR)
		return (ENOTDIR);

	/* 如果是 ..目录或者vnode对应的是该文件系统的root，报错 */
	if ((flags & ISDOTDOT) && (dvp->v_vflag & VV_ROOT))
		return (EIO);

	error = VOP_ACCESS(dvp, VEXEC, cnp->cn_cred, td);
	if (error)
		return (error);

	if (cnp->cn_namelen == 1 && *pname == '.') {
		if ((flags & ISLASTCN) && nameiop != LOOKUP)
			return (EINVAL);
		*vpp = dvp;
		VREF(dvp);
		return (0);
	}

	if (flags & ISDOTDOT) {
		if ((flags & ISLASTCN) && nameiop != LOOKUP)
			return (EINVAL);
		de = devfs_parent_dirent(dd);
		if (de == NULL)
			return (ENOENT);
		dvplocked = VOP_ISLOCKED(dvp);
		VOP_UNLOCK(dvp, 0);
		error = devfs_allocv(de, mp, cnp->cn_lkflags & LK_TYPE_MASK,
		    vpp);
		*dm_unlock = 0;
		vn_lock(dvp, dvplocked | LK_RETRY);
		return (error);
	}

	dd = dvp->v_data;
	de = devfs_find(dd, cnp->cn_nameptr, cnp->cn_namelen, 0);
	while (de == NULL) {	/* While(...) so we can use break */

		if (nameiop == DELETE)
			return (ENOENT);

		/*
		 * OK, we didn't have an entry for the name we were asked for
		 * so we try to see if anybody can create it on demand.
		 */
		pname = devfs_fqpn(specname, dmp, dd, cnp);
		if (pname == NULL)
			break;

		cdev = NULL;
		DEVFS_DMP_HOLD(dmp);
		sx_xunlock(&dmp->dm_lock);
		sx_slock(&clone_drain_lock);
		EVENTHANDLER_INVOKE(dev_clone,
		    td->td_ucred, pname, strlen(pname), &cdev);
		sx_sunlock(&clone_drain_lock);

		if (cdev == NULL)
			sx_xlock(&dmp->dm_lock);
		else if (devfs_populate_vp(dvp) != 0) {
			*dm_unlock = 0;
			sx_xlock(&dmp->dm_lock);
			if (DEVFS_DMP_DROP(dmp)) {
				sx_xunlock(&dmp->dm_lock);
				devfs_unmount_final(dmp);
			} else
				sx_xunlock(&dmp->dm_lock);
			dev_rel(cdev);
			return (ENOENT);
		}
		if (DEVFS_DMP_DROP(dmp)) {
			*dm_unlock = 0;
			sx_xunlock(&dmp->dm_lock);
			devfs_unmount_final(dmp);
			if (cdev != NULL)
				dev_rel(cdev);
			return (ENOENT);
		}

		if (cdev == NULL)
			break;

		dev_lock();
		dde = &cdev2priv(cdev)->cdp_dirents[dmp->dm_idx];
		if (dde != NULL && *dde != NULL)
			de = *dde;
		dev_unlock();
		dev_rel(cdev);
		break;
	}

	if (de == NULL || de->de_flags & DE_WHITEOUT) {
		if ((nameiop == CREATE || nameiop == RENAME) &&
		    (flags & (LOCKPARENT | WANTPARENT)) && (flags & ISLASTCN)) {
			cnp->cn_flags |= SAVENAME;
			return (EJUSTRETURN);
		}
		return (ENOENT);
	}

	if (devfs_prison_check(de, td))
		return (ENOENT);

	if ((cnp->cn_nameiop == DELETE) && (flags & ISLASTCN)) {
		error = VOP_ACCESS(dvp, VWRITE, cnp->cn_cred, td);
		if (error)
			return (error);
		if (*vpp == dvp) {
			VREF(dvp);
			*vpp = dvp;
			return (0);
		}
	}
	error = devfs_allocv(de, mp, cnp->cn_lkflags & LK_TYPE_MASK, vpp);
	*dm_unlock = 0;
	return (error);
}

static int
devfs_lookup(struct vop_lookup_args *ap)
{
	int j;
	struct devfs_mount *dmp;
	int dm_unlock;

	/*
		注意这里的操作逻辑，查找的时候会传入查找的参数，这个参数中包含有 vnode，
	*/
	if (devfs_populate_vp(ap->a_dvp) != 0)
		return (ENOTDIR);

	dmp = VFSTODEVFS(ap->a_dvp->v_mount);
	dm_unlock = 1;
	j = devfs_lookupx(ap, &dm_unlock);
	if (dm_unlock == 1)
		sx_xunlock(&dmp->dm_lock);
	return (j);
}

/*
	用来创建特殊的字符设备，貌似还可以用来创建FIFO
*/
static int
devfs_mknod(struct vop_mknod_args *ap)
{
	struct componentname *cnp;
	struct vnode *dvp, **vpp;
	struct devfs_dirent *dd, *de;
	struct devfs_mount *dmp;
	int error;

	/*
	 * The only type of node we should be creating here is a
	 * character device, for anything else return EOPNOTSUPP.
	 */
	if (ap->a_vap->va_type != VCHR)
		return (EOPNOTSUPP);
	dvp = ap->a_dvp;
	dmp = VFSTODEVFS(dvp->v_mount);

	cnp = ap->a_cnp;
	vpp = ap->a_vpp;
	dd = dvp->v_data;

	error = ENOENT;
	sx_xlock(&dmp->dm_lock);
	TAILQ_FOREACH(de, &dd->de_dlist, de_list) {
		if (cnp->cn_namelen != de->de_dirent->d_namlen)
			continue;
		if (de->de_dirent->d_type == DT_CHR &&
		    (de->de_cdp->cdp_flags & CDP_ACTIVE) == 0)
			continue;
		if (bcmp(cnp->cn_nameptr, de->de_dirent->d_name,
		    de->de_dirent->d_namlen) != 0)
			continue;
		if (de->de_flags & DE_WHITEOUT)
			break;
		goto notfound;
	}
	if (de == NULL)
		goto notfound;
	de->de_flags &= ~DE_WHITEOUT;
	error = devfs_allocv(de, dvp->v_mount, LK_EXCLUSIVE, vpp);
	return (error);
notfound:
	sx_xunlock(&dmp->dm_lock);
	return (error);
}

/* ARGSUSED 
	打开已有文件或者创建一个文件，应该是要调用 lookup 函数。这里好像还有些讲究，比如说对于一些有状态的
	文件系统，加锁然后调用create函数就可以了；对于一些无状态的文件系统(比如nfs)，那就不能给目录上锁，
	在调用 create 函数的时候要重新扫描这个目录，保证lookup之前没有创建该文件
*/
static int
devfs_open(struct vop_open_args *ap)
{
	struct thread *td = ap->a_td;
	struct vnode *vp = ap->a_vp;
	struct cdev *dev = vp->v_rdev;
	struct file *fp = ap->a_fp;
	int error, ref, vlocked;
	struct cdevsw *dsw;
	struct file *fpop;
	struct mtx *mtxp;

	if (vp->v_type == VBLK)
		return (ENXIO);

	if (dev == NULL)
		return (ENXIO);

	/* Make this field valid before any I/O in d_open. */
	if (dev->si_iosize_max == 0)
		dev->si_iosize_max = DFLTPHYS;

	dsw = dev_refthread(dev, &ref);
	if (dsw == NULL)
		return (ENXIO);
	if (fp == NULL && dsw->d_fdopen != NULL) {
		dev_relthread(dev, ref);
		return (ENXIO);
	}

	vlocked = VOP_ISLOCKED(vp);
	VOP_UNLOCK(vp, 0);

	fpop = td->td_fpop;
	td->td_fpop = fp;
	if (fp != NULL) {
		fp->f_data = dev;
		fp->f_vnode = vp;
	}
	if (dsw->d_fdopen != NULL)
		error = dsw->d_fdopen(dev, ap->a_mode, td, fp);
	else
		error = dsw->d_open(dev, ap->a_mode, S_IFCHR, td);
	/* Clean up any cdevpriv upon error. */
	if (error != 0)
		devfs_clear_cdevpriv();
	td->td_fpop = fpop;

	vn_lock(vp, vlocked | LK_RETRY);
	dev_relthread(dev, ref);
	if (error != 0) {
		if (error == ERESTART)
			error = EINTR;
		return (error);
	}

#if 0	/* /dev/console */
	KASSERT(fp != NULL, ("Could not vnode bypass device on NULL fp"));
#else
	if (fp == NULL)
		return (error);
#endif
	if (fp->f_ops == &badfileops)
		finit(fp, fp->f_flag, DTYPE_VNODE, dev, &devfs_ops_f);
	mtxp = mtx_pool_find(mtxpool_sleep, fp);

	/*
	 * Hint to the dofilewrite() to not force the buffer draining
	 * on the writer to the file.  Most likely, the write would
	 * not need normal buffers.
	 */
	mtx_lock(mtxp);
	fp->f_vnread_flags |= FDEVFS_VNODE;
	mtx_unlock(mtxp);
	return (error);
}

static int
devfs_pathconf(struct vop_pathconf_args *ap)
{

	switch (ap->a_name) {
	case _PC_FILESIZEBITS:
		*ap->a_retval = 64;
		return (0);
	case _PC_NAME_MAX:
		*ap->a_retval = NAME_MAX;
		return (0);
	case _PC_LINK_MAX:
		*ap->a_retval = INT_MAX;
		return (0);
	case _PC_SYMLINK_MAX:
		*ap->a_retval = MAXPATHLEN;
		return (0);
	case _PC_MAX_CANON:
		if (ap->a_vp->v_vflag & VV_ISTTY) {
			*ap->a_retval = MAX_CANON;
			return (0);
		}
		return (EINVAL);
	case _PC_MAX_INPUT:
		if (ap->a_vp->v_vflag & VV_ISTTY) {
			*ap->a_retval = MAX_INPUT;
			return (0);
		}
		return (EINVAL);
	case _PC_VDISABLE:
		if (ap->a_vp->v_vflag & VV_ISTTY) {
			*ap->a_retval = _POSIX_VDISABLE;
			return (0);
		}
		return (EINVAL);
	case _PC_MAC_PRESENT:
#ifdef MAC
		/*
		 * If MAC is enabled, devfs automatically supports
		 * trivial non-persistant label storage.
		 */
		*ap->a_retval = 1;
#else
		*ap->a_retval = 0;
#endif
		return (0);
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		return (0);
	default:
		return (vop_stdpathconf(ap));
	}
	/* NOTREACHED */
}

/* ARGSUSED 
	poll 操作允许进程检查一个对象是否可读或者可写
*/
static int
devfs_poll_f(struct file *fp, int events, struct ucred *cred, struct thread *td)
{
	struct cdev *dev;
	struct cdevsw *dsw;
	int error, ref;
	struct file *fpop;

	fpop = td->td_fpop;
	error = devfs_fp_check(fp, &dev, &dsw, &ref);
	if (error != 0) {
		error = vnops.fo_poll(fp, events, cred, td);
		return (error);
	}
	error = dsw->d_poll(dev, events, td);
	td->td_fpop = fpop;
	dev_relthread(dev, ref);
	return(error);
}

/*
 * Print out the contents of a special device vnode.
 */
static int
devfs_print(struct vop_print_args *ap)
{

	printf("\tdev %s\n", devtoname(ap->a_vp->v_rdev));
	return (0);
}

static int
devfs_read_f(struct file *fp, struct uio *uio, struct ucred *cred,
    int flags, struct thread *td)
{
	struct cdev *dev;
	int ioflag, error, ref;
	ssize_t resid;
	struct cdevsw *dsw;
	struct file *fpop;

	if (uio->uio_resid > DEVFS_IOSIZE_MAX)
		return (EINVAL);
	fpop = td->td_fpop;
	error = devfs_fp_check(fp, &dev, &dsw, &ref);
	if (error != 0) {
		error = vnops.fo_read(fp, uio, cred, flags, td);
		return (error);
	}
	resid = uio->uio_resid;
	ioflag = fp->f_flag & (O_NONBLOCK | O_DIRECT);
	if (ioflag & O_DIRECT)
		ioflag |= IO_DIRECT;

	foffset_lock_uio(fp, uio, flags | FOF_NOLOCK);
	error = dsw->d_read(dev, uio, ioflag);
	if (uio->uio_resid != resid || (error == 0 && resid != 0))
		devfs_timestamp(&dev->si_atime);
	td->td_fpop = fpop;
	dev_relthread(dev, ref);

	foffset_unlock_uio(fp, uio, flags | FOF_NOLOCK | FOF_NEXTOFF);
	return (error);
}

/*
	readdir 操作将一个目录的特定于文件系统的格式转换成一个应用程序所能识别的标准目录项列表。
	需要注意的是对目录内容的解释是由层次型文件系统的管理层提供的
*/
static int
devfs_readdir(struct vop_readdir_args *ap)
{
	int error;
	struct uio *uio;
	struct dirent *dp;
	struct devfs_dirent *dd;
	struct devfs_dirent *de;
	struct devfs_mount *dmp;
	off_t off;
	int *tmp_ncookies = NULL;

	if (ap->a_vp->v_type != VDIR)
		return (ENOTDIR);

	uio = ap->a_uio;
	if (uio->uio_offset < 0)
		return (EINVAL);

	/*
	 * XXX: This is a temporary hack to get around this filesystem not
	 * supporting cookies. We store the location of the ncookies pointer
	 * in a temporary variable before calling vfs_subr.c:vfs_read_dirent()
	 * and set the number of cookies to 0. We then set the pointer to
	 * NULL so that vfs_read_dirent doesn't try to call realloc() on 
	 * ap->a_cookies. Later in this function, we restore the ap->a_ncookies
	 * pointer to its original location before returning to the caller.
	 */
	if (ap->a_ncookies != NULL) {
		tmp_ncookies = ap->a_ncookies;
		*ap->a_ncookies = 0;
		ap->a_ncookies = NULL;
	}

	dmp = VFSTODEVFS(ap->a_vp->v_mount);
	if (devfs_populate_vp(ap->a_vp) != 0) {
		if (tmp_ncookies != NULL)
			ap->a_ncookies = tmp_ncookies;
		return (EIO);
	}
	error = 0;
	de = ap->a_vp->v_data;
	off = 0;
	TAILQ_FOREACH(dd, &de->de_dlist, de_list) {
		KASSERT(dd->de_cdp != (void *)0xdeadc0de, ("%s %d\n", __func__, __LINE__));
		if (dd->de_flags & (DE_COVERED | DE_WHITEOUT))
			continue;
		if (devfs_prison_check(dd, uio->uio_td))
			continue;
		if (dd->de_dirent->d_type == DT_DIR)
			de = dd->de_dir;
		else
			de = dd;
		dp = dd->de_dirent;
		MPASS(dp->d_reclen == GENERIC_DIRSIZ(dp));
		if (dp->d_reclen > uio->uio_resid)
			break;
		dp->d_fileno = de->de_inode;
		if (off >= uio->uio_offset) {
			error = vfs_read_dirent(ap, dp, off);
			if (error)
				break;
		}
		off += dp->d_reclen;
	}
	sx_xunlock(&dmp->dm_lock);
	uio->uio_offset = off;

	/*
	 * Restore ap->a_ncookies if it wasn't originally NULL in the first
	 * place.
	 */
	if (tmp_ncookies != NULL)
		ap->a_ncookies = tmp_ncookies;

	return (error);
}

/*
	readlink 操作返回一个符号链接的内容
*/
static int
devfs_readlink(struct vop_readlink_args *ap)
{
	struct devfs_dirent *de;

	de = ap->a_vp->v_data;
	return (uiomove(de->de_symlink, strlen(de->de_symlink), ap->a_uio));
}

static int
devfs_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp;
	struct devfs_dirent *de;

	vp = ap->a_vp;
	mtx_lock(&devfs_de_interlock);
	de = vp->v_data;
	if (de != NULL) {
		de->de_vnode = NULL;
		vp->v_data = NULL;
	}
	mtx_unlock(&devfs_de_interlock);
	vnode_destroy_vobject(vp);
	return (0);
}

static int
devfs_reclaim_vchr(struct vop_reclaim_args *ap)
{
	struct vnode *vp;
	struct cdev *dev;

	vp = ap->a_vp;
	MPASS(vp->v_type == VCHR);

	devfs_reclaim(ap);

	VI_LOCK(vp);
	dev_lock();
	dev = vp->v_rdev;
	vp->v_rdev = NULL;
	if (dev != NULL)
		dev->si_usecount -= vp->v_usecount;
	dev_unlock();
	VI_UNLOCK(vp);
	if (dev != NULL)
		dev_rel(dev);
	return (0);
}

static int
devfs_remove(struct vop_remove_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp = ap->a_vp;
	struct devfs_dirent *dd;
	struct devfs_dirent *de, *de_covered;
	struct devfs_mount *dmp = VFSTODEVFS(vp->v_mount);

	ASSERT_VOP_ELOCKED(dvp, "devfs_remove");
	ASSERT_VOP_ELOCKED(vp, "devfs_remove");

	sx_xlock(&dmp->dm_lock);
	dd = ap->a_dvp->v_data;
	de = vp->v_data;
	if (de->de_cdp == NULL) {
		TAILQ_REMOVE(&dd->de_dlist, de, de_list);
		if (de->de_dirent->d_type == DT_LNK) {
			de_covered = devfs_find(dd, de->de_dirent->d_name,
			    de->de_dirent->d_namlen, 0);
			if (de_covered != NULL)
				de_covered->de_flags &= ~DE_COVERED;
		}
		/* We need to unlock dvp because devfs_delete() may lock it. */
		VOP_UNLOCK(vp, 0);
		if (dvp != vp)
			VOP_UNLOCK(dvp, 0);
		devfs_delete(dmp, de, 0);
		sx_xunlock(&dmp->dm_lock);
		if (dvp != vp)
			vn_lock(dvp, LK_EXCLUSIVE | LK_RETRY);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	} else {
		de->de_flags |= DE_WHITEOUT;
		sx_xunlock(&dmp->dm_lock);
	}
	return (0);
}

/*
 * Revoke is called on a tty when a terminal session ends.  The vnode
 * is orphaned by setting v_op to deadfs so we need to let go of it
 * as well so that we create a new one next time around.
 *
 */
static int
devfs_revoke(struct vop_revoke_args *ap)
{
	struct vnode *vp = ap->a_vp, *vp2;
	struct cdev *dev;
	struct cdev_priv *cdp;
	struct devfs_dirent *de;
	u_int i;

	KASSERT((ap->a_flags & REVOKEALL) != 0, ("devfs_revoke !REVOKEALL"));

	dev = vp->v_rdev;
	cdp = cdev2priv(dev);
 
	dev_lock();
	cdp->cdp_inuse++;
	dev_unlock();

	vhold(vp);
	vgone(vp);
	vdrop(vp);

	VOP_UNLOCK(vp,0);
 loop:
	for (;;) {
		mtx_lock(&devfs_de_interlock);
		dev_lock();
		vp2 = NULL;
		for (i = 0; i <= cdp->cdp_maxdirent; i++) {
			de = cdp->cdp_dirents[i];
			if (de == NULL)
				continue;

			vp2 = de->de_vnode;
			if (vp2 != NULL) {
				dev_unlock();
				VI_LOCK(vp2);
				mtx_unlock(&devfs_de_interlock);
				if (vget(vp2, LK_EXCLUSIVE | LK_INTERLOCK,
				    curthread))
					goto loop;
				vhold(vp2);
				vgone(vp2);
				vdrop(vp2);
				vput(vp2);
				break;
			} 
		}
		if (vp2 != NULL) {
			continue;
		}
		dev_unlock();
		mtx_unlock(&devfs_de_interlock);
		break;
	}
	dev_lock();
	cdp->cdp_inuse--;
	if (!(cdp->cdp_flags & CDP_ACTIVE) && cdp->cdp_inuse == 0) {
		TAILQ_REMOVE(&cdevp_list, cdp, cdp_list);
		dev_unlock();
		dev_rel(&cdp->cdp_c);
	} else
		dev_unlock();

	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	return (0);
}

/*
	ioctl 操作向一台特殊的设备发送控制请求
*/
static int
devfs_rioctl(struct vop_ioctl_args *ap)
{
	struct vnode *vp;
	struct devfs_mount *dmp;
	int error;

	vp = ap->a_vp;
	vn_lock(vp, LK_SHARED | LK_RETRY);
	if (vp->v_iflag & VI_DOOMED) {
		VOP_UNLOCK(vp, 0);
		return (EBADF);
	}
	dmp = VFSTODEVFS(vp->v_mount);
	sx_xlock(&dmp->dm_lock);
	VOP_UNLOCK(vp, 0);
	DEVFS_DMP_HOLD(dmp);
	devfs_populate(dmp);
	if (DEVFS_DMP_DROP(dmp)) {
		sx_xunlock(&dmp->dm_lock);
		devfs_unmount_final(dmp);
		return (ENOENT);
	}
	error = devfs_rules_ioctl(dmp, ap->a_command, ap->a_data, ap->a_td);
	sx_xunlock(&dmp->dm_lock);
	return (error);
}

static int
devfs_rread(struct vop_read_args *ap)
{

	if (ap->a_vp->v_type != VDIR)
		return (EINVAL);
	return (VOP_READDIR(ap->a_vp, ap->a_uio, ap->a_cred, NULL, NULL, NULL));
}

static int
devfs_setattr(struct vop_setattr_args *ap)
{
	struct devfs_dirent *de;
	struct vattr *vap;
	struct vnode *vp;
	struct thread *td;
	int c, error;
	uid_t uid;
	gid_t gid;

	vap = ap->a_vap;
	vp = ap->a_vp;
	td = curthread;
	if ((vap->va_type != VNON) ||
	    (vap->va_nlink != VNOVAL) ||
	    (vap->va_fsid != VNOVAL) ||
	    (vap->va_fileid != VNOVAL) ||
	    (vap->va_blocksize != VNOVAL) ||
	    (vap->va_flags != VNOVAL && vap->va_flags != 0) ||
	    (vap->va_rdev != VNOVAL) ||
	    ((int)vap->va_bytes != VNOVAL) ||
	    (vap->va_gen != VNOVAL)) {
		return (EINVAL);
	}

	error = devfs_populate_vp(vp);
	if (error != 0)
		return (error);

	de = vp->v_data;
	if (vp->v_type == VDIR)
		de = de->de_dir;

	c = 0;
	if (vap->va_uid == (uid_t)VNOVAL)
		uid = de->de_uid;
	else
		uid = vap->va_uid;
	if (vap->va_gid == (gid_t)VNOVAL)
		gid = de->de_gid;
	else
		gid = vap->va_gid;
	if (uid != de->de_uid || gid != de->de_gid) {
		if ((ap->a_cred->cr_uid != de->de_uid) || uid != de->de_uid ||
		    (gid != de->de_gid && !groupmember(gid, ap->a_cred))) {
			error = priv_check(td, PRIV_VFS_CHOWN);
			if (error != 0)
				goto ret;
		}
		de->de_uid = uid;
		de->de_gid = gid;
		c = 1;
	}

	if (vap->va_mode != (mode_t)VNOVAL) {
		if (ap->a_cred->cr_uid != de->de_uid) {
			error = priv_check(td, PRIV_VFS_ADMIN);
			if (error != 0)
				goto ret;
		}
		de->de_mode = vap->va_mode;
		c = 1;
	}

	if (vap->va_atime.tv_sec != VNOVAL || vap->va_mtime.tv_sec != VNOVAL) {
		error = vn_utimes_perm(vp, vap, ap->a_cred, td);
		if (error != 0)
			goto ret;
		if (vap->va_atime.tv_sec != VNOVAL) {
			if (vp->v_type == VCHR)
				vp->v_rdev->si_atime = vap->va_atime;
			else
				de->de_atime = vap->va_atime;
		}
		if (vap->va_mtime.tv_sec != VNOVAL) {
			if (vp->v_type == VCHR)
				vp->v_rdev->si_mtime = vap->va_mtime;
			else
				de->de_mtime = vap->va_mtime;
		}
		c = 1;
	}

	if (c) {
		if (vp->v_type == VCHR)
			vfs_timestamp(&vp->v_rdev->si_ctime);
		else
			vfs_timestamp(&de->de_mtime);
	}

ret:
	sx_xunlock(&VFSTODEVFS(vp->v_mount)->dm_lock);
	return (error);
}

#ifdef MAC
static int
devfs_setlabel(struct vop_setlabel_args *ap)
{
	struct vnode *vp;
	struct devfs_dirent *de;

	vp = ap->a_vp;
	de = vp->v_data;

	mac_vnode_relabel(ap->a_cred, vp, ap->a_label);
	mac_devfs_update(vp->v_mount, de, vp);

	return (0);
}
#endif

static int
devfs_stat_f(struct file *fp, struct stat *sb, struct ucred *cred, struct thread *td)
{

	return (vnops.fo_stat(fp, sb, cred, td));
}

/*
	主要是用来创建符号链接
*/
static int
devfs_symlink(struct vop_symlink_args *ap)
{
	int i, error;
	struct devfs_dirent *dd;
	struct devfs_dirent *de, *de_covered, *de_dotdot;
	struct devfs_mount *dmp;

	error = priv_check(curthread, PRIV_DEVFS_SYMLINK); // 首先检查设备文件的系统权限
	if (error)
		return(error);
	dmp = VFSTODEVFS(ap->a_dvp->v_mount);
	if (devfs_populate_vp(ap->a_dvp) != 0)
		return (ENOENT);

	dd = ap->a_dvp->v_data;
	de = devfs_newdirent(ap->a_cnp->cn_nameptr, ap->a_cnp->cn_namelen);
	de->de_flags = DE_USER;
	de->de_uid = 0;
	de->de_gid = 0;
	de->de_mode = 0755;
	de->de_inode = alloc_unr(devfs_inos);
	de->de_dir = dd;
	de->de_dirent->d_type = DT_LNK;
	i = strlen(ap->a_target) + 1;
	de->de_symlink = malloc(i, M_DEVFS, M_WAITOK);
	bcopy(ap->a_target, de->de_symlink, i);
#ifdef MAC
	mac_devfs_create_symlink(ap->a_cnp->cn_cred, dmp->dm_mount, dd, de);
#endif
	de_covered = devfs_find(dd, de->de_dirent->d_name,
	    de->de_dirent->d_namlen, 0);
	if (de_covered != NULL) {
		if ((de_covered->de_flags & DE_USER) != 0) {
			devfs_delete(dmp, de, DEVFS_DEL_NORECURSE);
			sx_xunlock(&dmp->dm_lock);
			return (EEXIST);
		}
		KASSERT((de_covered->de_flags & DE_COVERED) == 0,
		    ("devfs_symlink: entry %p already covered", de_covered));
		de_covered->de_flags |= DE_COVERED;
	}

	de_dotdot = TAILQ_FIRST(&dd->de_dlist);		/* "." */
	de_dotdot = TAILQ_NEXT(de_dotdot, de_list);	/* ".." */
	TAILQ_INSERT_AFTER(&dd->de_dlist, de_dotdot, de, de_list);
	devfs_dir_ref_de(dmp, dd);
	devfs_rules_apply(dmp, de);

	return (devfs_allocv(de, ap->a_dvp->v_mount, LK_EXCLUSIVE, ap->a_vpp));
}

/*
	truncate： 截断
	文件系统中对于目录可以执行 truncate 操作，能够将目录变小，加快搜索的速度
*/
static int
devfs_truncate_f(struct file *fp, off_t length, struct ucred *cred, struct thread *td)
{

	return (vnops.fo_truncate(fp, length, cred, td));
}

/*
	uio 在用户空间和内核空间进行数据传输
*/
static int
devfs_write_f(struct file *fp, struct uio *uio, struct ucred *cred,
    int flags, struct thread *td)
{
	struct cdev *dev;
	int error, ioflag, ref;
	ssize_t resid;
	struct cdevsw *dsw;
	struct file *fpop;

	// 首先判断需要传输数据的剩余字节数是否符合文件系统的设计要求
	if (uio->uio_resid > DEVFS_IOSIZE_MAX)
		return (EINVAL);
	fpop = td->td_fpop;	// 线程对应的 cdev 设备文件？
	error = devfs_fp_check(fp, &dev, &dsw, &ref);
	if (error != 0) {
		error = vnops.fo_write(fp, uio, cred, flags, td);
		return (error);
	}
	KASSERT(uio->uio_td == td, ("uio_td %p is not td %p", uio->uio_td, td));
	ioflag = fp->f_flag & (O_NONBLOCK | O_DIRECT | O_FSYNC);
	if (ioflag & O_DIRECT)
		ioflag |= IO_DIRECT;
	foffset_lock_uio(fp, uio, flags | FOF_NOLOCK);

	resid = uio->uio_resid;

	/*
		执行write函数后会更新文件指针的位置，可能是由于这个原因，后面会更新 td->td_fpop。
		这个write函数很可能会出现在一个循环当中，然后判断 uio->resid 的值是否为0
	*/ 
	error = dsw->d_write(dev, uio, ioflag);	
	if (uio->uio_resid != resid || (error == 0 && resid != 0)) {
		devfs_timestamp(&dev->si_ctime);
		dev->si_mtime = dev->si_ctime;
	}
	td->td_fpop = fpop;
	dev_relthread(dev, ref);

	foffset_unlock_uio(fp, uio, flags | FOF_NOLOCK | FOF_NEXTOFF);
	return (error);
}

/*
	将文件映射到进程的地址空间。其中addr表示的应该就是映射到的进程地址空间的起始位置，
	size表示大小
*/
static int
devfs_mmap_f(struct file *fp, vm_map_t map, vm_offset_t *addr, vm_size_t size,
    vm_prot_t prot, vm_prot_t cap_maxprot, int flags, vm_ooffset_t foff,
    struct thread *td)
{
	struct cdev *dev;
	struct cdevsw *dsw;
	struct mount *mp;
	struct vnode *vp;
	struct file *fpop;
	vm_object_t object;
	vm_prot_t maxprot;
	int error, ref;

	vp = fp->f_vnode;	// file 会对应一个 vnode

	/*
	 * Ensure that file and memory protections are
	 * compatible. 确保文件和内存保护兼容，应该就是为了确认挂载点的保护机制是否正常
	 * 
	 * 上面首先根据 file 结构体找到对应的 vnode，再从 vnode 找到对应的 mount，从这一步我们就可以
	 * 判断处该 file 是属于哪个文件系统
	 */
	mp = vp->v_mount;	
	if (mp != NULL && (mp->mnt_flag & MNT_NOEXEC) != 0) {
		maxprot = VM_PROT_NONE;
		if ((prot & VM_PROT_EXECUTE) != 0)
			return (EACCES);
	} else
		maxprot = VM_PROT_EXECUTE;
	if ((fp->f_flag & FREAD) != 0)
		maxprot |= VM_PROT_READ;
	else if ((prot & VM_PROT_READ) != 0)
		return (EACCES);

	/*
	 * If we are sharing potential changes via MAP_SHARED and we
	 * are trying to get write permission although we opened it
	 * without asking for it, bail out.
	 * 如果我们通过 MAP_SHARED 共享潜在的更改，并试图获得写入权限，尽管我们未经请求就打开了它，请退出
	 *
	 * Note that most character devices always share mappings.
	 * The one exception is that D_MMAP_ANON devices
	 * (i.e. /dev/zero) permit private writable mappings.
	 * 请注意，大多数字符设备总是共享映射。一个例外是，D_MMAP_ANON 设备（即/dev/zero）允许私有可写映射
	 *
	 * Rely on vm_mmap_cdev() to fail invalid MAP_PRIVATE requests
	 * as well as updating maxprot to permit writing for
	 * D_MMAP_ANON devices rather than doing that here.
	 * 
	 */
	if ((flags & MAP_SHARED) != 0) {
		if ((fp->f_flag & FWRITE) != 0)
			maxprot |= VM_PROT_WRITE;
		else if ((prot & VM_PROT_WRITE) != 0)
			return (EACCES);
	}
	maxprot &= cap_maxprot;

	fpop = td->td_fpop;
	error = devfs_fp_check(fp, &dev, &dsw, &ref);
	if (error != 0)
		return (error);

	error = vm_mmap_cdev(td, size, prot, &maxprot, &flags, dev, dsw, &foff,
	    &object);
	td->td_fpop = fpop;
	dev_relthread(dev, ref);
	if (error != 0)
		return (error);

	// addr 参数表示的是文件映射到的虚拟地址？
	error = vm_mmap_object(map, addr, size, prot, maxprot, flags, object,
	    foff, FALSE, td);
	if (error != 0)
		vm_object_deallocate(object);
	return (error);
}

dev_t
dev2udev(struct cdev *x)
{
	if (x == NULL)
		return (NODEV);
	return (cdev2priv(x)->cdp_inode);
}

/* 
	***_f 表示的应该就是对 file 的操作，下面定义的是对vnode的操作，要注意两者的区别，
	因为 devfs 是跟设备驱动紧密联系的，写驱动程序的时候我们可以通过 echo 命令将数据写入
	文件，这里调用的应该就是 fileops 提供的函数。观察 devfs_write_f 函数可以发现，其中
	包含了 d_write，这个是 cdevsw 中包含一个函数，是驱动程序提供给用户的一个接口，所以要
	考虑一下两者之间的关系到底是什么样的。
	个人理解是两者所负责的功能是不一样的，fileops 主要针对的是关于文件所支持的操作。驱动程序
	会在 /dev 下生成一个设备驱动文件，本质上是一个文件，所以我们对它的操作其实就是对文件的操作，
	对文件的操作那就要遵守文件所支持的一些函数接口，也正是 fileops。

	大致流程猜测会是这样： 第一步应该是 syscall，请求打开某一个函数；第二步应该就会跳转到 vfs
	中提供的 open 函数，然后 vfs 判断该文件属于哪个文件系统，然后就会调用那个文件系统提供的 open
	函数在执行具体的操作。通过源码发现，这个过程跟 vnode 有强关联，所以要弄清楚整个机制的实现，肯定
	是要搞清楚 vnode 在不同阶段的行为。还有一个思想要明确，那就是操作系统对于文件(大一点来说就是数据)
	的处理肯定是离不开内存的，一提到内存首先就要想到的是内存映射，所以文件系统的内存映射机制也是一定要
	理清楚的。这里出现了一个 devfs_mmap_f 函数，其作用就是将文件映射到进程虚拟地址空间。我们通过 echo
	等命令对文件进行写操作其实就是往这块地址空间中写数据。然后 cdevsw 的 write 函数应该就是把这块空间
	中的数据获取到，写入另外一个虚拟地址空间？这个可能就涉及到了内核地址空间和用户地址空间的相互映射。
	驱动程序的物理地址映射到的是内核地址空间的虚拟地址，但是文件系统操作的可能是用户态的地址空间，所以可能
	还需要再做一步转换
*/
static struct fileops devfs_ops_f = {
	.fo_read =	devfs_read_f,
	.fo_write =	devfs_write_f,
	.fo_truncate =	devfs_truncate_f,
	.fo_ioctl =	devfs_ioctl_f,
	.fo_poll =	devfs_poll_f,
	.fo_kqfilter =	devfs_kqfilter_f,
	.fo_stat =	devfs_stat_f,
	.fo_close =	devfs_close_f,
	.fo_chmod =	vn_chmod,
	.fo_chown =	vn_chown,
	.fo_sendfile =	vn_sendfile,
	.fo_seek =	vn_seek,
	.fo_fill_kinfo = vn_fill_kinfo,
	.fo_mmap =	devfs_mmap_f,
	.fo_flags =	DFLAG_PASSABLE | DFLAG_SEEKABLE
};

/* Vops for non-CHR vnodes in /dev. 
	这里定义的是对非字符设备的操作，下面定义的是对字符设备的操作，两者有共通的地方
*/
static struct vop_vector devfs_vnodeops = {
	.vop_default =		&default_vnodeops,

	.vop_access =		devfs_access,
	.vop_getattr =		devfs_getattr,
	.vop_ioctl =		devfs_rioctl,
	.vop_lookup =		devfs_lookup,
	.vop_mknod =		devfs_mknod,
	.vop_pathconf =		devfs_pathconf,
	.vop_read =		devfs_rread,
	.vop_readdir =		devfs_readdir,
	.vop_readlink =		devfs_readlink,
	.vop_reclaim =		devfs_reclaim,
	.vop_remove =		devfs_remove,
	.vop_revoke =		devfs_revoke,
	.vop_setattr =		devfs_setattr,
#ifdef MAC
	.vop_setlabel =		devfs_setlabel,
#endif
	.vop_symlink =		devfs_symlink,
	.vop_vptocnp =		devfs_vptocnp,
};

/* Vops for VCHR vnodes in /dev. */
static struct vop_vector devfs_specops = {
	.vop_default =		&default_vnodeops,

	.vop_access =		devfs_access,
	.vop_bmap =		VOP_PANIC,
	.vop_close =		devfs_close,
	.vop_create =		VOP_PANIC,
	.vop_fsync =		vop_stdfsync,
	.vop_getattr =		devfs_getattr,
	.vop_ioctl =		devfs_ioctl,
	.vop_link =		VOP_PANIC,
	.vop_mkdir =		VOP_PANIC,
	.vop_mknod =		VOP_PANIC,
	.vop_open =		devfs_open,
	.vop_pathconf =		devfs_pathconf,
	.vop_poll =		dead_poll,
	.vop_print =		devfs_print,
	.vop_read =		dead_read,
	.vop_readdir =		VOP_PANIC,
	.vop_readlink =		VOP_PANIC,
	.vop_reallocblks =	VOP_PANIC,
	.vop_reclaim =		devfs_reclaim_vchr,
	.vop_remove =		devfs_remove,
	.vop_rename =		VOP_PANIC,
	.vop_revoke =		devfs_revoke,
	.vop_rmdir =		VOP_PANIC,
	.vop_setattr =		devfs_setattr,
#ifdef MAC
	.vop_setlabel =		devfs_setlabel,
#endif
	.vop_strategy =		VOP_PANIC,
	.vop_symlink =		VOP_PANIC,
	.vop_vptocnp =		devfs_vptocnp,
	.vop_write =		dead_write,
};

/*
 * Our calling convention to the device drivers used to be that we passed
 * vnode.h IO_* flags to read()/write(), but we're moving to fcntl.h O_ 
 * flags instead since that's what open(), close() and ioctl() takes and
 * we don't really want vnode.h in device drivers.
 * We solved the source compatibility by redefining some vnode flags to
 * be the same as the fcntl ones and by sending down the bitwise OR of
 * the respective fcntl/vnode flags.  These CTASSERTS make sure nobody
 * pulls the rug out under this.
 */
CTASSERT(O_NONBLOCK == IO_NDELAY);
CTASSERT(O_FSYNC == IO_SYNC);
