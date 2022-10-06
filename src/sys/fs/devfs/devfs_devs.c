/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000,2004
 *	Poul-Henning Kamp.  All rights reserved.
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
 * From: FreeBSD: src/sys/miscfs/kernfs/kernfs_vfsops.c 1.36
 *
 * $FreeBSD: releng/12.0/sys/fs/devfs/devfs_devs.c 341085 2018-11-27 17:58:25Z markj $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/dirent.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>

#include <sys/kdb.h>

#include <fs/devfs/devfs.h>
#include <fs/devfs/devfs_int.h>

#include <security/mac/mac_framework.h>

/*
 * The one true (but secret) list of active devices in the system.
 * 系统中活动设备的一个真实（但保密）列表，也就是管理都是处于活跃状态的设备。
 * 从代码搜索来看，这应该是一个全局的设备链表
 *
 * Locked by dev_lock()/devmtx
 */
struct cdev_priv_list cdevp_list = TAILQ_HEAD_INITIALIZER(cdevp_list);

struct unrhdr *devfs_inos;

static MALLOC_DEFINE(M_DEVFS2, "DEVFS2", "DEVFS data 2");
static MALLOC_DEFINE(M_DEVFS3, "DEVFS3", "DEVFS data 3");
static MALLOC_DEFINE(M_CDEVP, "DEVFS1", "DEVFS cdev_priv storage");

SYSCTL_NODE(_vfs, OID_AUTO, devfs, CTLFLAG_RW, 0, "DEVFS filesystem");

static unsigned devfs_generation;
SYSCTL_UINT(_vfs_devfs, OID_AUTO, generation, CTLFLAG_RD,
						&devfs_generation, 0, "DEVFS generation number");

unsigned devfs_rule_depth = 1;
SYSCTL_UINT(_vfs_devfs, OID_AUTO, rule_depth, CTLFLAG_RW,
						&devfs_rule_depth, 0, "Max depth of ruleset include");

/*
 * Helper sysctl for devname(3).  We're given a dev_t and return the
 * name, if any, registered by the device driver.
 */
static int
sysctl_devname(SYSCTL_HANDLER_ARGS)
{
	int error;
	dev_t ud;
#ifdef COMPAT_FREEBSD11
	uint32_t ud_compat;
#endif
	struct cdev_priv *cdp;
	struct cdev *dev;

#ifdef COMPAT_FREEBSD11
	if (req->newlen == sizeof(ud_compat))
	{
		error = SYSCTL_IN(req, &ud_compat, sizeof(ud_compat));
		if (error == 0)
			ud = ud_compat == (uint32_t)NODEV ? NODEV : ud_compat;
	}
	else
#endif
		error = SYSCTL_IN(req, &ud, sizeof(ud));
	if (error)
		return (error);
	if (ud == NODEV)
		return (EINVAL);
	dev = NULL;
	dev_lock();
	TAILQ_FOREACH(cdp, &cdevp_list, cdp_list)
	if (cdp->cdp_inode == ud)
	{
		dev = &cdp->cdp_c;
		dev_refl(dev);
		break;
	}
	dev_unlock();
	if (dev == NULL)
		return (ENOENT);
	error = SYSCTL_OUT(req, dev->si_name, strlen(dev->si_name) + 1);
	dev_rel(dev);
	return (error);
}

SYSCTL_PROC(_kern, OID_AUTO, devname,
						CTLTYPE_OPAQUE | CTLFLAG_RW | CTLFLAG_ANYBODY | CTLFLAG_MPSAFE,
						NULL, 0, sysctl_devname, "", "devname(3) handler");

SYSCTL_INT(_debug_sizeof, OID_AUTO, cdev, CTLFLAG_RD,
					 SYSCTL_NULL_INT_PTR, sizeof(struct cdev), "sizeof(struct cdev)");

SYSCTL_INT(_debug_sizeof, OID_AUTO, cdev_priv, CTLFLAG_RD,
					 SYSCTL_NULL_INT_PTR, sizeof(struct cdev_priv), "sizeof(struct cdev_priv)");

/*
	devfs 创建一个 cdev，可作为奇海系统设备文件类的成员函数
*/
struct cdev *
devfs_alloc(int flags)
{
	struct cdev_priv *cdp;
	struct cdev *cdev;
	struct timespec ts;

	cdp = malloc(sizeof *cdp, M_CDEVP, M_ZERO | ((flags & MAKEDEV_NOWAIT) ? M_NOWAIT : M_WAITOK));
	if (cdp == NULL)
		return (NULL);

	/* 初始化 cdp_dirents 指针 */
	cdp->cdp_dirents = &cdp->cdp_dirent0;

	/*
		创建新的 cdev_priv 对象时，会同步创建一个 struct cdev 对象，然后将这个
		cdev 对象，而不是 cdev_priv 返回。cdp_c 是一个结构体对象，不是指针
	*/
	cdev = &cdp->cdp_c;
	LIST_INIT(&cdev->si_children); // 初始化children链表
	vfs_timestamp(&ts);						 // 获取系统当前时间戳
	cdev->si_atime = cdev->si_mtime = cdev->si_ctime = ts;

	return (cdev);
}

/* 应该是通过文件名查找对应的 dev */
int devfs_dev_exists(const char *name)
{
	/*
		devfs 一些功能函数代码实现上有一个规律，那就是 cdev_priv 经常性的出现在
		函数开头，说明这个结构体应该是有相当的重要性
	*/
	struct cdev_priv *cdp;

	mtx_assert(&devmtx, MA_OWNED);

	/*
		cdevp_list 又是存放的 cdev_priv，所以一定要重点关注。我们一般查找 cdev 的时候是通过
		遍历 cdevp_list 链表来进行的。内核代码软件设计的思路挺多都是这种。首先设计出一个涵盖各种
		成员变量的复杂的结构体，用来全方位描述所要管理的对象；然后在构建一个队列，将所有需要管理的
		对象通过描述结构体来进行统一管理
		当我们在 /dev 下创建设备结点的时候，会将 cdev 通过 cdev_priv 注册到 cdevp_list，也就是说
		cdevp_list 队列中包含着所有 cdev 的一些信息，包括 cdev 对应的名称。所以当我们想要通过名字查找
		/dev 下是否存在某个设备结点的时候，就可以通过遍历这个队列来进行，对应上面的说法

		在奇海中的话，感觉应该不需要遍历这个链表，而是直接通过 tptfs hashtree 查找即可。可替换
	*/
	TAILQ_FOREACH(cdp, &cdevp_list, cdp_list)
	{
		if ((cdp->cdp_flags & CDP_ACTIVE) == 0)
			continue;
		/*
			devfs_pathpath() 函数的作用是查找某个路径a是否包含路径b，下面的操作就是把两个a和b互相调换位置，
			就是判断在父级路径和子级路径中是否存在。实际代码调试过程来看，si_name 中存放的是设备的绝对路径名。
			我们就可以利用其中的信息构造设备文件树：
				- 假如是一级路径，则直接创建设备文件类
				- 如果是多级路径，则先一级一级创建父级目录项，然后最后一个利用最后一个组件名创建设备文件类
			这里是判断一个设备文件是否存在，那么我们可以直接利用 hashtree 查找即可。上级代码调用情况看，
			name = cdev->si_name
		*/
		if (devfs_pathpath(cdp->cdp_c.si_name, name) != 0)
			return (1);
		if (devfs_pathpath(name, cdp->cdp_c.si_name) != 0)
			return (1);
	}
	/*
		假如上述操作仍然没有找到目标对象，则在调用 find()，应该就是全局查找了
	*/
	if (devfs_dir_find(name) != 0)
		return (1);

	return (0);
}

void devfs_free(struct cdev *cdev)
{
	struct cdev_priv *cdp;

	cdp = cdev2priv(cdev);
	// 这里是否需要添加 kc_crfree 版本？
	if (cdev->si_cred != NULL)
		crfree(cdev->si_cred);
	devfs_free_cdp_inode(cdp->cdp_inode);
	/*
		这里释放的应该只是存放指针的空间，而并没有把指针指向的对象实例给释放掉
	*/
	if (cdp->cdp_maxdirent > 0)
		free(cdp->cdp_dirents, M_DEVFS2);
	/*
		cdev 是 cdp 中的一个成员对象，不是指针。所以我们释放掉 cdp 的时候，就会
		把 cdev 所占用的内存同时给释放掉
	*/
	free(cdp, M_CDEVP);
	/*
		包含三个步骤:
			- 释放 struct cdev 部分成员
			- 释放 cdev_priv->cdp_inode
			- 释放 cdev_priv

		其实这些数据都是隶属于 devfs_dirent 结构中，所以该函数可以作为奇海设备文件类的函数成员
	*/
}

/*
	查找某个目录下的子目录？ 从代码逻辑来看，其实就是在传入的 dd 中的 de_dlist 目录列表
	对比；dd 有些情况传入的是根目录
*/
struct devfs_dirent *
devfs_find(struct devfs_dirent *dd, const char *name, int namelen, int type)
{
	struct devfs_dirent *de;

	TAILQ_FOREACH(de, &dd->de_dlist, de_list)
	{
		/*
			de_dirent 是 dirent 类型的一个结构体，它存放有directory entry 的 inode number，
			name length 等信息。一般存在与目录结构体中，我们可以通过它获取到目录项的一些信息
			de_dlist 应该表示的是子目录列表，通过目录name和type来判断当前目录是不是我们要找的那个
		*/
		if (namelen != de->de_dirent->d_namlen)
			continue;
		if (type != 0 && type != de->de_dirent->d_type)
			continue;

		/*
		 * The race with finding non-active name is not
		 * completely closed by the check, but it is similar
		 * to the devfs_allocv() in making it unlikely enough.
		 * 通过名称和 namelen 查找一个文件的时候，如果目标文件是字符设备，但是判断其属性发现，该设备
		 * 节点目前处于非活动状态，就直接跳过名称匹配，进行下次查找
		 */
		if (de->de_dirent->d_type == DT_CHR &&
				(de->de_cdp->cdp_flags & CDP_ACTIVE) == 0)
			continue;

		if (bcmp(name, de->de_dirent->d_name, namelen) != 0)
			continue;
		break;
	}
	KASSERT(de == NULL || (de->de_flags & DE_DOOMED) == 0,
					("devfs_find: returning a doomed entry"));
	return (de);
}

/*
	devfs 创建新的目录项，注意里边包含有 dirent，devfs_dirent 就是管理目录的队列中的元素。
	该函数是创建 devfs_dirent 中的 struct dirent 对象。融合进 tptfs 之后，这个函数是否
	还有存在的必要？
*/
struct devfs_dirent *
devfs_newdirent(char *name, int namelen)
{
	int i;
	struct devfs_dirent *de;
	struct dirent d;

	d.d_namlen = namelen;

	/* devfs_dirent + dirent 大小的总和 */
	i = sizeof(*de) + GENERIC_DIRSIZ(&d);
	de = malloc(i, M_DEVFS3, M_WAITOK | M_ZERO);
	de->de_dirent = (struct dirent *)(de + 1);
	de->de_dirent->d_namlen = namelen;
	de->de_dirent->d_reclen = GENERIC_DIRSIZ(&d);
	bcopy(name, de->de_dirent->d_name, namelen);
	dirent_terminate(de->de_dirent);
	vfs_timestamp(&de->de_ctime);
	de->de_mtime = de->de_atime = de->de_ctime;
	de->de_links = 1;
	de->de_holdcnt = 1;
#ifdef MAC
	mac_devfs_init(de);
#endif
	return (de);
}

struct devfs_dirent *
devfs_parent_dirent(struct devfs_dirent *de)
{
	/*
		当de不是一个目录的时候，直接返回 de_dir,说明在字段就是文件所在目录
	*/
	if (de->de_dirent->d_type != DT_DIR)
		return (de->de_dir);
	/*
		下面就是处理当 devfs_dirent 是一个目录的情况。这种操作在 tptfs 中应该是不存在的，
		就直接返回 parent node 或者 null
	*/
	if (de->de_flags & (DE_DOT | DE_DOTDOT))
		return (NULL);

	de = TAILQ_FIRST(&de->de_dlist); /* "." */
	if (de == NULL)
		return (NULL);
	de = TAILQ_NEXT(de, de_list); /* ".." */
	if (de == NULL)
		return (NULL);

	return (de->de_dir);
}

/*
	创建目录，make directory
	dotdot 不仅仅是 .. , devfs_find 函数调用的时候传入的是根目录，调用的场景就是查找目录项的时候没有找到，
	这个时候就要创建一个新的 devfs directory entry
	该函数在 tptfs 中可以被替代
*/
struct devfs_dirent *
devfs_vmkdir(struct devfs_mount *dmp, char *name, int namelen,
						 struct devfs_dirent *dotdot, u_int inode)
{
	struct devfs_dirent *dd;
	struct devfs_dirent *de;

	/* Create the new directory
		注意，这里会创建一个 struct dirent 结构
	*/
	dd = devfs_newdirent(name, namelen);
	TAILQ_INIT(&dd->de_dlist); // 初始化子目录列表
	dd->de_dirent->d_type = DT_DIR;
	dd->de_mode = 0555;
	dd->de_links = 2;
	dd->de_dir = dd;
	/*
		devfs_mount 的时候。inode传入的是 DEVFS_ROOTINO = 2。如果传入的inode不是0，
		那么就直接赋值给 de_inode，如果是0的话，那就调用 alloc_unr 来分配一个。所以unr
		的作用其中一个就是管理 inode number 的分配工作
	*/
	if (inode != 0)
		dd->de_inode = inode;
	else
		dd->de_inode = alloc_unr(devfs_inos);
	/*
	 * "." and ".." are always the two first entries in the
	 * de_dlist list. 一般文件下面都会有 . 和 .. 两个子文件，所以
	 * 从这里可以推测出来 de_dlist 表示的就是子目录链表
	 *
	 * Create the "." entry in the new directory.
	 */
	de = devfs_newdirent(".", 1);
	de->de_dirent->d_type = DT_DIR;
	de->de_flags |= DE_DOT;
	TAILQ_INSERT_TAIL(&dd->de_dlist, de, de_list);
	de->de_dir = dd;

	/* Create the ".." entry in the new directory.
		一般我们执行 cd .. 的时候就是退回到上一层目录，这里应该就是起到了这样一个功能。
		.. 应该就是在本目录中保存的对上级目录的一个链接，所以后边会把 dotdot -> de_links++
	*/
	de = devfs_newdirent("..", 2);
	de->de_dirent->d_type = DT_DIR;
	de->de_flags |= DE_DOTDOT;
	TAILQ_INSERT_TAIL(&dd->de_dlist, de, de_list);
	if (dotdot == NULL)
	{
		de->de_dir = dd;
	}
	else
	{
		de->de_dir = dotdot;
		sx_assert(&dmp->dm_lock, SX_XLOCKED);
		TAILQ_INSERT_TAIL(&dotdot->de_dlist, dd, de_list);
		dotdot->de_links++;
		/*
			当 .. 目录也创建完成之后，将 dev rules 应用在目标文件中。奇海中应该是不需要这些
			规则的，所有的设备都应该是可见并且可访问的
		*/
		devfs_rules_apply(dmp, dd);
	}

#ifdef MAC
	mac_devfs_create_directory(dmp->dm_mount, name, namelen, dd);
#endif
	return (dd);
}

/*
	该函数出现的地方只有两个，一个是 devfs_delete，另外一个是 rm_empty_dir。后者在 tptfs 中准备
	删除掉，所以我们只需要考虑前者调用情况。暂时将 delete 函数只承担释放功能，从链表删除就跟从父目录
	子链表 remove 操作放到一起
*/
void devfs_dirent_free(struct devfs_dirent *de)
{
	struct vnode *vp;

	vp = de->de_vnode;
	/*
		注意 devfs_de_interlock 的使用场景，很多情况下都是设置 de_vnode 或者 v_data 的
		时候使用的，要确定一下这个锁是不是只用于处理这种情况
	*/
	mtx_lock(&devfs_de_interlock);
	if (vp != NULL && vp->v_data == de)
		vp->v_data = NULL;
	mtx_unlock(&devfs_de_interlock);
	/*
		这里应该是对应奇海中的文件类释放
	*/
	free(de, M_DEVFS3);
}

/*
 * Removes a directory if it is empty. Also empty parent directories are
 * removed recursively. 删除空目录。空的父目录也会被递归删除
 */
static void
devfs_rmdir_empty(struct devfs_mount *dm, struct devfs_dirent *de)
{
	struct devfs_dirent *dd, *de_dot, *de_dotdot;

	sx_assert(&dm->dm_lock, SX_XLOCKED);

	for (;;)
	{
		KASSERT(de->de_dirent->d_type == DT_DIR,
						("devfs_rmdir_empty: de is not a directory"));

		if ((de->de_flags & DE_DOOMED) != 0 || de == dm->dm_rootdir)
			return;

		de_dot = TAILQ_FIRST(&de->de_dlist);
		KASSERT(de_dot != NULL, ("devfs_rmdir_empty: . missing"));
		de_dotdot = TAILQ_NEXT(de_dot, de_list);
		KASSERT(de_dotdot != NULL, ("devfs_rmdir_empty: .. missing"));
		/* Return if the directory is not empty. */
		if (TAILQ_NEXT(de_dotdot, de_list) != NULL)
			return;

		dd = devfs_parent_dirent(de);
		KASSERT(dd != NULL, ("devfs_rmdir_empty: NULL dd"));
		TAILQ_REMOVE(&de->de_dlist, de_dot, de_list);
		TAILQ_REMOVE(&de->de_dlist, de_dotdot, de_list);
		TAILQ_REMOVE(&dd->de_dlist, de, de_list);
		DEVFS_DE_HOLD(dd);
		devfs_delete(dm, de, DEVFS_DEL_NORECURSE);
		devfs_delete(dm, de_dot, DEVFS_DEL_NORECURSE);
		devfs_delete(dm, de_dotdot, DEVFS_DEL_NORECURSE);
		if (DEVFS_DE_DROP(dd))
		{
			devfs_dirent_free(dd);
			return;
		}

		de = dd;
	}
}

/*
 * The caller needs to hold the dm for the duration of the call since
 * dm->dm_lock may be temporary dropped.
 *
 * 在 devfs_delete() 函数执行之前，devfs_dirent 已经在 remove() 或者 purge()
 * 函数中从父目录链表被删除掉了，所以此处我们只需要关注 dirent 的资源释放。该函数传入
 * flags 貌似只有 DEVFS_DEL_NORECURSE 和 0，首先 tptfs 中不需要对空目录执行自动
 * 删除操作，所以 DEVFS_DEL_NORECURSE 宏是可以删除的；0 的话就表示 vnode 没有处在
 * 加锁状态，那基本上所有的情况都是未加锁状态
 */
void devfs_delete(struct devfs_mount *dm, struct devfs_dirent *de, int flags)
{
	struct devfs_dirent *dd;
	struct vnode *vp;

	KASSERT((de->de_flags & DE_DOOMED) == 0,
					("devfs_delete doomed dirent"));
	de->de_flags |= DE_DOOMED;

	/*
		如果不是非递归删除，也就是执行递归删除的情况。dd 会在目标文件处理完成之后判断是否为空，
		如果是的话，根据 devfs 设计，是需要将空目录删除的。devfs 融入 tptfs 之后应该不用
		执行此步骤
	*/
	if ((flags & DEVFS_DEL_NORECURSE) == 0)
	{
		dd = devfs_parent_dirent(de);
		if (dd != NULL)
			DEVFS_DE_HOLD(dd);
		if (de->de_flags & DE_USER)
		{
			KASSERT(dd != NULL, ("devfs_delete: NULL dd"));
			devfs_dir_unref_de(dm, dd);
		}
	}
	else
		dd = NULL;
	/*
		注意，这里用到了 devfs_de_interlock，刚好是要用到 vnode 对象
	*/
	mtx_lock(&devfs_de_interlock);
	vp = de->de_vnode;
	if (vp != NULL)
	{
		VI_LOCK(vp); // lock vnode interlock
		mtx_unlock(&devfs_de_interlock);
		vholdl(vp);
		sx_unlock(&dm->dm_lock);
		if ((flags & DEVFS_DEL_VNLOCKED) == 0)
			vn_lock(vp, LK_EXCLUSIVE | LK_INTERLOCK | LK_RETRY);
		else
			VI_UNLOCK(vp);
		vgone(vp);
		if ((flags & DEVFS_DEL_VNLOCKED) == 0)
			VOP_UNLOCK(vp, 0);
		vdrop(vp);
		sx_xlock(&dm->dm_lock);
	}
	else
		mtx_unlock(&devfs_de_interlock);
	/*
		如果文件是链接文件的话，要把存放文件名的字段占用空间释放掉
	*/
	if (de->de_symlink)
	{
		free(de->de_symlink, M_DEVFS);
		de->de_symlink = NULL;
	}
#ifdef MAC
	mac_devfs_destroy(de);
#endif
	if (de->de_inode > DEVFS_ROOTINO)
	{
		// 申请的 inode number 也要释放掉并置零
		devfs_free_cdp_inode(de->de_inode);
		de->de_inode = 0;
	}
	/*
		删除操作也是要先判断 entry，对应到 tptfs 中的文件类，的引用计数是否为0。如果是的话
		才会真正 free 掉设备文件对象锁占用的空间资源
	*/
	if (DEVFS_DE_DROP(de))
		devfs_dirent_free(de);

	if (dd != NULL)
	{
		if (DEVFS_DE_DROP(dd))
			devfs_dirent_free(dd);
		else
			devfs_rmdir_empty(dm, dd);
	}
}

/*
 * Called on unmount. 在unmount的时候调用
 * Recursively removes the entire tree.	递归删除整个树
 * The caller needs to hold the dm for the duration of the call.
 * 呼叫者需要在通话期间保持 dm
 */
static void
devfs_purge(struct devfs_mount *dm, struct devfs_dirent *dd)
{
	struct devfs_dirent *de;

	sx_assert(&dm->dm_lock, SX_XLOCKED);

	DEVFS_DE_HOLD(dd);
	for (;;)
	{
		/*
		 * Use TAILQ_LAST() to remove "." and ".." last.
		 * We might need ".." to resolve a path in
		 * devfs_dir_unref_de().
		 * 最后再删除 . 和 .. ，因为我们还可能需要用到它们来解决路径问题
		 */
		de = TAILQ_LAST(&dd->de_dlist, devfs_dlist_head);
		if (de == NULL)
			break; // 知道de为空，跳出循环
		TAILQ_REMOVE(&dd->de_dlist, de, de_list);
		if (de->de_flags & DE_USER)
			devfs_dir_unref_de(dm, dd);
		if (de->de_flags & (DE_DOT | DE_DOTDOT))
			devfs_delete(dm, de, DEVFS_DEL_NORECURSE);
		else if (de->de_dirent->d_type == DT_DIR) // 当de表示一个目录的时候，递归删除
			devfs_purge(dm, de);
		else
			devfs_delete(dm, de, DEVFS_DEL_NORECURSE);
	}
	if (DEVFS_DE_DROP(dd))
		devfs_dirent_free(dd);
	else if ((dd->de_flags & DE_DOOMED) == 0)
		devfs_delete(dm, dd, DEVFS_DEL_NORECURSE);
}

/*
 * Each cdev_priv has an array of pointers to devfs_dirent which is indexed
 * by the mount points dm_idx.
 * 每个 cdev_priv 都有一个指向 devfs_dirent 的指针数组，devfs_dirent 由装入点 dm_idx 索引
 *
 * This function extends the array when necessary, taking into account that
 * the default array is 1 element and not malloc'ed.
 * 考虑到默认数组是1个元素而不是malloc'ed，此函数在必要时扩展数组
 */
static void
devfs_metoo(struct cdev_priv *cdp, struct devfs_mount *dm)
{
	struct devfs_dirent **dep;
	int siz;

	siz = (dm->dm_idx + 1) * sizeof *dep;
	dep = malloc(siz, M_DEVFS2, M_WAITOK | M_ZERO);
	dev_lock();
	if (dm->dm_idx <= cdp->cdp_maxdirent)
	{
		/* We got raced */
		dev_unlock();
		free(dep, M_DEVFS2);
		return;
	}
	memcpy(dep, cdp->cdp_dirents, (cdp->cdp_maxdirent + 1) * sizeof *dep);
	if (cdp->cdp_maxdirent > 0)
		free(cdp->cdp_dirents, M_DEVFS2);
	cdp->cdp_dirents = dep;
	/*
	 * XXX: if malloc told us how much we actually got this could
	 * XXX: be optimized.
	 */
	cdp->cdp_maxdirent = dm->dm_idx;
	dev_unlock();
}

/*
 * The caller needs to hold the dm for the duration of the call.
		populate loop： 循环填充
		参数中有一个 cleanup，所以它应该是包含有两种操作类型，第一种就是循环填充，
		第二种可能就是循环删除
 */
static int
devfs_populate_loop(struct devfs_mount *dm, int cleanup)
{
	struct cdev_priv *cdp; // cdev 的私有数据

	/*
		dd: dirent of directory.
		dt: dirent of target file.
	*/
	struct devfs_dirent *de;
	struct devfs_dirent *dd, *dt;
	struct cdev *pdev;
	int de_flags, depth, j;
	char *q, *s;

	sx_assert(&dm->dm_lock, SX_XLOCKED); // devfs_mount 此时应该持有独占锁
	dev_lock();

	/*
		cdevp_list 中存放的是所有创建的cdev的私有数据，应该是可以通过判断其中的
		一些属性值来检测 cdev 的状态
	*/
	TAILQ_FOREACH(cdp, &cdevp_list, cdp_list)
	{

		KASSERT(cdp->cdp_dirents != NULL, ("NULL cdp_dirents"));

		/*
		 * If we are unmounting, or the device has been destroyed,
		 * clean up our dirent.
		 * 当没有挂载，或者device已经被销毁(不活跃？？)，则直接销毁相应的目录项。从代码中可以看到，
		 * cdev directory entry 是有数量限制的；这里比较的是 devfs_mount 和 cdev_priv -> cdp_maxdirent
		 * 进行比较，然后对 cdp_dirents 中的元素判断是否为null，元素表示挂载点的私有数据？
		 */
		if ((cleanup || !(cdp->cdp_flags & CDP_ACTIVE)) &&
				dm->dm_idx <= cdp->cdp_maxdirent &&
				cdp->cdp_dirents[dm->dm_idx] != NULL)
		{
			de = cdp->cdp_dirents[dm->dm_idx];
			cdp->cdp_dirents[dm->dm_idx] = NULL;
			KASSERT(cdp == de->de_cdp,
							("%s %d %s %p %p", __func__, __LINE__,
							 cdp->cdp_c.si_name, cdp, de->de_cdp));
			KASSERT(de->de_dir != NULL, ("Null de->de_dir"));
			dev_unlock();
			/*
				从父目录将 de(目标文件)删除
			*/
			TAILQ_REMOVE(&de->de_dir->de_dlist, de, de_list);
			de->de_cdp = NULL;
			de->de_inode = 0;
			devfs_delete(dm, de, 0);
			dev_lock();
			cdp->cdp_inuse--;
			dev_unlock();
			return (1);
		}
		/*
		 * GC any lingering devices
		 */
		if (!(cdp->cdp_flags & CDP_ACTIVE))
		{
			if (cdp->cdp_inuse > 0)
				continue;
			TAILQ_REMOVE(&cdevp_list, cdp, cdp_list);
			dev_unlock();
			dev_rel(&cdp->cdp_c);
			return (1);
		}
		/*
		 * Don't create any new dirents if we are unmounting
		 */
		if (cleanup)
			continue;
		KASSERT((cdp->cdp_flags & CDP_ACTIVE), ("Bogons, I tell ya'!"));

		if (dm->dm_idx <= cdp->cdp_maxdirent &&
				cdp->cdp_dirents[dm->dm_idx] != NULL)
		{
			de = cdp->cdp_dirents[dm->dm_idx];
			KASSERT(cdp == de->de_cdp, ("inconsistent cdp")); //不一致的 cdev_privdata
			continue;
		}
		/*
			从上面的代码逻辑可以看出，这个循环貌似是要从 cdevp_list 里边寻找 devfs_mount 所对应的
			devfs_dirent 的 cdev_privdata？？ 找到了以后，执行下面的操作
		*/

		cdp->cdp_inuse++;
		dev_unlock();

		if (dm->dm_idx > cdp->cdp_maxdirent)
			devfs_metoo(cdp, dm);

		dd = dm->dm_rootdir; // 根目录
		s = cdp->cdp_c.si_name;

		/*
			从逻辑上来看，这里把完整的文件路径进行了拆分，会把"/"中间的目录名单独提取出来，在根目录的 de_dlist
			中进行遍历查找。如果找到的话就继续解析路径，提取路径名，再查找，直到解析完毕；所以其中一旦有某一个目录
			没有找到，应该是要做相应处理的，报错或者是重新创建一个。也可以大致推测一下这个函数的功能是干什么
		*/
		for (;;)
		{
			for (q = s; *q != '/' && *q != '\0'; q++)
				continue;
			if (*q != '/')
				break;													// 只是单个名称，没有对应的路径信息
			de = devfs_find(dd, s, q - s, 0); // q - s 路径名
			if (de == NULL)										// 如果当前目录中没有这个目录，那就要重新创建一个
				de = devfs_vmkdir(dm, s, q - s, dd, 0);
			else if (de->de_dirent->d_type == DT_LNK)
			{ // 如果是链接文件，那就找对应的目录实体
				de = devfs_find(dd, s, q - s, DT_DIR);
				if (de == NULL)
					de = devfs_vmkdir(dm, s, q - s, dd, 0);
				de->de_flags |= DE_COVERED; // 把 flag 设置成覆盖类型？？
			}
			s = q + 1;
			dd = de;
			KASSERT(dd->de_dirent->d_type == DT_DIR &&
									(dd->de_flags & (DE_DOT | DE_DOTDOT)) == 0,
							("%s: invalid directory (si_name=%s)",
							 __func__, cdp->cdp_c.si_name));

		} // end of for

		/*
			每个循环值处理一个 cdev_privdata。前面 for 循环是用于处理路径中的目录组件。执行到这里其实就对应了
			路径中的最后一个元素。从处理逻辑来看，最后一个元素要么是一个链接文件，要么是一个设备文件。所以，感觉
			cdev_privdata 中的 cdev 对应的是最后一个组件，跟中间的目录关系不大。作为链接文件的时候，更多的
			应该是承担存放路径信息的功能
		*/
		de_flags = 0;
		de = devfs_find(dd, s, q - s, DT_LNK);
		if (de != NULL)
			de_flags |= DE_COVERED;

		de = devfs_newdirent(s, q - s);
		if (cdp->cdp_c.si_flags & SI_ALIAS)
		{
			de->de_uid = 0;
			de->de_gid = 0;
			de->de_mode = 0755;
			de->de_dirent->d_type = DT_LNK;
			pdev = cdp->cdp_c.si_parent;
			dt = dd;
			depth = 0;

			/* 计算文件层次深度 */
			while (dt != dm->dm_rootdir &&
						 (dt = devfs_parent_dirent(dt)) != NULL)
				depth++;

			j = depth * 3 + strlen(pdev->si_name) + 1;
			de->de_symlink = malloc(j, M_DEVFS, M_WAITOK);
			de->de_symlink[0] = 0;
			while (depth-- > 0)
				strcat(de->de_symlink, "../");
			strcat(de->de_symlink, pdev->si_name);
		}
		else
		{
			de->de_uid = cdp->cdp_c.si_uid;
			de->de_gid = cdp->cdp_c.si_gid;
			de->de_mode = cdp->cdp_c.si_mode;
			de->de_dirent->d_type = DT_CHR;
		}
		de->de_flags |= de_flags;
		de->de_inode = cdp->cdp_inode;
		de->de_cdp = cdp;
#ifdef MAC
		mac_devfs_create_device(cdp->cdp_c.si_cred, dm->dm_mount,
														&cdp->cdp_c, de);
#endif
		de->de_dir = dd;
		TAILQ_INSERT_TAIL(&dd->de_dlist, de, de_list);
		devfs_rules_apply(dm, de);
		dev_lock();
		/* XXX: could check that cdp is still active here */
		KASSERT(cdp->cdp_dirents[dm->dm_idx] == NULL,
						("%s %d\n", __func__, __LINE__));
		cdp->cdp_dirents[dm->dm_idx] = de;
		KASSERT(de->de_cdp != (void *)0xdeadc0de,
						("%s %d\n", __func__, __LINE__));
		dev_unlock();
		return (1);
	} // end of foreach loop

	dev_unlock();
	return (0);
}

/*
 * The caller needs to hold the dm for the duration of the call.
 */
void devfs_populate(struct devfs_mount *dm)
{
	unsigned gen;

	sx_assert(&dm->dm_lock, SX_XLOCKED);
	gen = devfs_generation;
	if (dm->dm_generation == gen)
		return;
	while (devfs_populate_loop(dm, 0))
		continue;
	dm->dm_generation = gen;
}

/*
 * The caller needs to hold the dm for the duration of the call.
		该函数只有在 umount 的时候才会被调用。其中包括了一个 devfs_purge()。
		它的作用就是递归删除整个设备文件树
 */
void devfs_cleanup(struct devfs_mount *dm)
{

	sx_assert(&dm->dm_lock, SX_XLOCKED);
	while (devfs_populate_loop(dm, 1))
		continue;
	devfs_purge(dm, dm->dm_rootdir);
}

/*
 * devfs_create() and devfs_destroy() are called from kern_conf.c and
 * in both cases the devlock() mutex is held, so no further locking
 * is necessary and no sleeping allowed.
 *
 * kern_conf.c 文件中包含有 make_dev 函数，也就是字符设备驱动中常常使用的创建设备
 * 文件的函数。这个函数在最后会给 devfs_create 函数传入一个填充好的 cdev 指针，然后
 * 该函数就把这个 cdev 注册到全局的管理队列 cdevp_list，推测 /dev 下的设备列表就是
 * 通过遍历整个队列中的元素获得的
 */

void devfs_create(struct cdev *dev)
{
	struct cdev_priv *cdp;

	mtx_assert(&devmtx, MA_OWNED);

	/* cdev 关联的 private data */
	cdp = cdev2priv(dev);
	cdp->cdp_flags |= CDP_ACTIVE;

	/*
		devfs_inos 本身就是一个unr，管理alloc内存分配，alloc_unrl 表示的就是从
		devfs_inos 中的应该是free list中拿到一个可用的对象进行分配，感觉就仅仅是
		一个数字，可能仅仅代表的就是编号一类的东西？？
	 */
	cdp->cdp_inode = alloc_unrl(devfs_inos);
	dev_refl(dev); // reference count + 1

	TAILQ_INSERT_TAIL(&cdevp_list, cdp, cdp_list);
	/*
		devfs_generation 是一个全局的静态变量，当我们创建一个设备节点的时候，该变量自增。
		说明我们可以根据该变量的值，判断系统中一共创建了多少个设备文件；
		下面的 devfs_destroy() 函数也会使得该变量自增，应该是为了说明系统又新增了多少次
		删除设备节点的操作。下文会通过 devfs_mount->dm_generation 与该值的判断，来对
		cdevp_list 进行对应操作
	*/
	devfs_generation++;
}

void devfs_destroy(struct cdev *dev)
{
	struct cdev_priv *cdp;

	mtx_assert(&devmtx, MA_OWNED);
	cdp = cdev2priv(dev);
	cdp->cdp_flags &= ~CDP_ACTIVE;
	devfs_generation++;
}

ino_t devfs_alloc_cdp_inode(void)
{

	return (alloc_unr(devfs_inos));
}

void devfs_free_cdp_inode(ino_t ino)
{

	if (ino > 0)
		free_unr(devfs_inos, ino);
}

static void
devfs_devs_init(void *junk __unused)
{
	/*
		表示要分配的元素数量限制，从命名来看，很可能就是inode的数量的范围。结构体中也有相应的链表，表示
		分配的元素和释放的元素，inode table？
		每一个文件都会对应一个 inode，inode中会有一个随机分配的 identify number 来标识它。初始化函数
		好像仅仅分配了一个 inode number。devfs 应该就是 /dev，本质上也是一个文件，所以它也会对应一个
		inode，这里应该就是给 /dev 的 inode 分配一个随机数。
		这一步就是为 devfs 创建一个新的 unrhdr 结构体来管理其中 number 的分配
	*/
	devfs_inos = new_unrhdr(DEVFS_ROOTINO + 1, INT_MAX, &devmtx);
}

SYSINIT(devfs_devs, SI_SUB_DEVFS, SI_ORDER_FIRST, devfs_devs_init, NULL);
