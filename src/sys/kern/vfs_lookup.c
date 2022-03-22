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
 *	@(#)vfs_lookup.c	8.4 (Berkeley) 2/16/94
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: releng/12.0/sys/kern/vfs_lookup.c 338835 2018-09-20 18:25:26Z mjg $");

#include "opt_capsicum.h"
#include "opt_ktrace.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/capsicum.h>
#include <sys/fcntl.h>
#include <sys/jail.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/filedesc.h>
#include <sys/proc.h>
#include <sys/sdt.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>
#ifdef KTRACE
#include <sys/ktrace.h>
#endif

#include <security/audit/audit.h>
#include <security/mac/mac_framework.h>

#include <vm/uma.h>

#define	NAMEI_DIAGNOSTIC 1
#undef NAMEI_DIAGNOSTIC

SDT_PROVIDER_DECLARE(vfs);
SDT_PROBE_DEFINE3(vfs, namei, lookup, entry, "struct vnode *", "char *",
    "unsigned long");
SDT_PROBE_DEFINE2(vfs, namei, lookup, return, "int", "struct vnode *");

/* Allocation zone for namei. */
uma_zone_t namei_zone;

/* Placeholder vnode for mp traversal. 
	mp 遍历用的占位符 vnode？
*/
static struct vnode *vp_crossmp;

static int
crossmp_vop_islocked(struct vop_islocked_args *ap)
{

	return (LK_SHARED);
}

static int
crossmp_vop_lock1(struct vop_lock1_args *ap)
{
	struct vnode *vp;
	struct lock *lk __unused;
	const char *file __unused;
	int flags, line __unused;

	vp = ap->a_vp;
	lk = vp->v_vnlock;
	flags = ap->a_flags;
	file = ap->a_file;
	line = ap->a_line;

	if ((flags & LK_SHARED) == 0)
		panic("invalid lock request for crossmp");

	WITNESS_CHECKORDER(&lk->lock_object, LOP_NEWORDER, file, line,
	    flags & LK_INTERLOCK ? &VI_MTX(vp)->lock_object : NULL);
	WITNESS_LOCK(&lk->lock_object, 0, file, line);
	if ((flags & LK_INTERLOCK) != 0)
		VI_UNLOCK(vp);
	LOCK_LOG_LOCK("SLOCK", &lk->lock_object, 0, 0, ap->a_file, line);
	return (0);
}

static int
crossmp_vop_unlock(struct vop_unlock_args *ap)
{
	struct vnode *vp;
	struct lock *lk __unused;
	int flags;

	vp = ap->a_vp;
	lk = vp->v_vnlock;
	flags = ap->a_flags;

	if ((flags & LK_INTERLOCK) != 0)
		VI_UNLOCK(vp);
	WITNESS_UNLOCK(&lk->lock_object, 0, LOCK_FILE, LOCK_LINE);
	LOCK_LOG_LOCK("SUNLOCK", &lk->lock_object, 0, 0, LOCK_FILE,
	    LOCK_LINE);
	return (0);
}

static struct vop_vector crossmp_vnodeops = {
	.vop_default =		&default_vnodeops,
	.vop_islocked =		crossmp_vop_islocked,
	.vop_lock1 =		crossmp_vop_lock1,
	.vop_unlock =		crossmp_vop_unlock,
};

/*
	namei capture tracker：名字捕获追踪器？其实就是 vnode 队列
*/
struct nameicap_tracker {
	struct vnode *dp;
	TAILQ_ENTRY(nameicap_tracker) nm_link;
};

/* Zone for cap mode tracker elements used for dotdot capability checks. 
	用于 dotdot 功能检查的 cap 模式跟踪器元素的区域
*/
static uma_zone_t nt_zone;

static void
nameiinit(void *dummy __unused)
{

	namei_zone = uma_zcreate("NAMEI", MAXPATHLEN, NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);
	nt_zone = uma_zcreate("rentr", sizeof(struct nameicap_tracker),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
	/*
		从定义可以看出，vp_crossmp 是一个静态的 vnode 对象，注册的 vnodeops 都是一些锁操作
		相关的函数
	*/
	getnewvnode("crossmp", NULL, &crossmp_vnodeops, &vp_crossmp);
}
SYSINIT(vfs, SI_SUB_VFS, SI_ORDER_SECOND, nameiinit, NULL);

static int lookup_cap_dotdot = 1;
SYSCTL_INT(_vfs, OID_AUTO, lookup_cap_dotdot, CTLFLAG_RWTUN,
    &lookup_cap_dotdot, 0,
    "enables \"..\" components in path lookup in capability mode");
static int lookup_cap_dotdot_nonlocal = 1;
SYSCTL_INT(_vfs, OID_AUTO, lookup_cap_dotdot_nonlocal, CTLFLAG_RWTUN,
    &lookup_cap_dotdot_nonlocal, 0,
    "enables \"..\" components in path lookup in capability mode "
    "on non-local mount");

static void
nameicap_tracker_add(struct nameidata *ndp, struct vnode *dp)
{
	struct nameicap_tracker *nt;
	/*
		当 dp 不是代表对应一个目录项的 vnode 的时候，返回。说明 tracker 处理的是目录项？
	*/
	if ((ndp->ni_lcf & NI_LCF_CAP_DOTDOT) == 0 || dp->v_type != VDIR)
		return;
	nt = uma_zalloc(nt_zone, M_WAITOK);	// 为 tracker 申请一块内存
	vhold(dp);	// 增加 tracker 的引用计数
	nt->dp = dp;	// 指定对应的 vnode
	/*
		nameidata 中包含了一个 tracker 的链表，这里执行的是插入操作。该函数会在 lookup 函数执行的
		时候被调用，所以推测它的功能就是保留所有查找过程中所申请的所有节点的 vnode。这样在后续查找 vnode
		的过程中就可以通过遍历该链表获得
	*/
	TAILQ_INSERT_TAIL(&ndp->ni_cap_tracker, nt, nm_link);
}

static void
nameicap_cleanup(struct nameidata *ndp)
{
	struct nameicap_tracker *nt, *nt1;

	KASSERT(TAILQ_EMPTY(&ndp->ni_cap_tracker) ||
	    (ndp->ni_lcf & NI_LCF_CAP_DOTDOT) != 0, ("not strictrelative"));
	/*
		遍历 nameidata 中的 tracker list，每个元素对应的 vnode 引用计数减一，并释放掉
		所有元素对应的内存
	*/
	TAILQ_FOREACH_SAFE(nt, &ndp->ni_cap_tracker, nm_link, nt1) {
		TAILQ_REMOVE(&ndp->ni_cap_tracker, nt, nm_link);
		// vhold 和 vdrop 会对 vnode 加锁
		vdrop(nt->dp);
		uma_zfree(nt_zone, nt);
	}
}

/*
 * For dotdot lookups in capability mode, only allow the component
 * lookup to succeed if the resulting directory was already traversed
 * during the operation.  Also fail dotdot lookups for non-local
 * filesystems, where external agents might assist local lookups to
 * escape the compartment.
 * 留意注释中第一句话的表述，仅当结果目录项已经被 traversed(横穿，横渡，越过) 之后，我们才会
 * 允许组件查找成功。意思应该就是当我们检测到路径中含有 .. 组件的时候，对应的上级目录必须已经
 * 被查找过了，否则将不允许返回成功；
 * 非本地文件系统可能指的是 NFS？
 * 
 * dp 表示的是 directory vnode pointer，当我们处理到 .. 组件的时候，dp 应该会指向当前文件的
 * 父节点，然后我们要检查父节点是否保存在 tracker 链表当中。如果在的话才能返回成功，否则就是失败
 */
static int
nameicap_check_dotdot(struct nameidata *ndp, struct vnode *dp)
{
	struct nameicap_tracker *nt;
	struct mount *mp;

	if ((ndp->ni_lcf & NI_LCF_CAP_DOTDOT) == 0 || dp == NULL ||
	    dp->v_type != VDIR)
		return (0);
	mp = dp->v_mount;
	/*
		cap: capacity，能力、容量。MNT_LOCAL 表示文件系统存储在本地，表明文件系统是磁盘文件系统？
		从代码逻辑可以看出，vfs 对于 .. 目录项的查找有比较严格的条件要求，dp 应该就是对应到 .. 目录项
		关联的 vnode 结构。
		lookup_cap_dotdot_nonlocal 表示的应该是查找非本地存储(非持久化内存)文件系统的 .. 目录，遍历
		所有的 tracker 链表元素，对比与dp是否一致
		非本地可能不是表示非持久化，可能是类似 NFS 的文件系统
	*/
	if (lookup_cap_dotdot_nonlocal == 0 && mp != NULL &&
	    (mp->mnt_flag & MNT_LOCAL) == 0)
		return (ENOTCAPABLE);
	TAILQ_FOREACH_REVERSE(nt, &ndp->ni_cap_tracker, nameicap_tracker_head,
	    nm_link) {
		if (dp == nt->dp)
			return (0);
	}
	// ENOTCAPABLE 表达的意思好像是不具备这种能力
	return (ENOTCAPABLE);
}

static void
namei_cleanup_cnp(struct componentname *cnp)
{
	/*
		namei_zone 其实对应的是 nameidata 中的名称缓存
	*/
	uma_zfree(namei_zone, cnp->cn_pnbuf);
#ifdef DIAGNOSTIC
	cnp->cn_pnbuf = NULL;
	cnp->cn_nameptr = NULL;
#endif
}

/*
	当文件查找路径是从根目录开始(绝对路径)的时候，处理根节点的情况
*/
static int
namei_handle_root(struct nameidata *ndp, struct vnode **dpp)
{
	struct componentname *cnp;
	/*
		nameidata 结构中的路径名缓存其实是放在 componentname 当中的，所以这里处理路径名数据的时候，
		传入的其实是 ni_cnd 成员
	*/
	cnp = &ndp->ni_cnd;
	/*
		如果 nameidata 仅支持相对路径方式，那么该函数就会返回不支持这种能力。下面的代码分支其实就是处理
		绝对路径查找
	*/
	if ((ndp->ni_lcf & NI_LCF_STRICTRELATIVE) != 0) {
#ifdef KTRACE
		if (KTRPOINT(curthread, KTR_CAPFAIL))
			ktrcapfail(CAPFAIL_LOOKUP, NULL, NULL);
#endif
		return (ENOTCAPABLE);
	}
	/*
		while 循环已经改变了 nameidata 中对应字段中指针指向的位置。循环结束之后应该就是
		指向了我们所要查找的文件路径除去 "/" 的起始位置
	*/
	while (*(cnp->cn_nameptr) == '/') {
		cnp->cn_nameptr++;
		ndp->ni_pathlen--;
	}
	/*
		将 rootdir 对应的 vnode 结构赋值给 dpp，说明 nameidata 当前已经知道 rootdir 对应的 vnode
		的指针？ 从上层的调用函数逻辑可以看出，ni_rootdir 要么是rootnode，要么是进程描述符中保存的根目录
		对应的 vnode
	*/
	*dpp = ndp->ni_rootdir;
	vrefact(*dpp);
	return (0);
}

/*
 * Convert a pathname into a pointer to a locked vnode.
 * 将路径名转换为指向锁定 vnode 的指针
 *
 * The FOLLOW flag is set when symbolic links are to be followed
 * when they occur at the end of the name translation process.
 * Symbolic links are always followed for all other pathname
 * components other than the last.
 * 当符号链接出现在名称转换过程的末尾时，将在符号链接后面设置 FOLLOW 标志。
 * 对于除最后一个以外的所有其他路径名组件，始终遵循符号链接
 *
 * The segflg defines whether the name is to be copied from user
 * space or kernel space.
 * segflg 定义是从用户空间还是从内核空间复制名称
 *
 * Overall outline of namei:
 *
 *	copy in name
 *	get starting directory
 *	while (!done && !error) {
 *		call lookup to search path.
 *		if symbolic link, massage name in buffer and continue
 *	}

	 该函数会增加 vnode 的引用计数，所以在函数执行完毕之后，还需要调用 vput 或者 vrele 函数来
	 减少引用计数 (LOCKLEAF 分区采用哪一个)
 */
int
namei(struct nameidata *ndp)
{
	// 对应进程结构的文件描述符成员
	struct filedesc *fdp;	/* pointer to file descriptor state */
	char *cp;		/* pointer into pathname argument */

	/* 貌似对于目录或者文件的访问，都是通过关联的 vnode 进行的 */
	struct vnode *dp;	/* the directory we are searching 我们正在查找的目录 */
	struct iovec aiov;		/* uio for reading symbolic links */
	struct componentname *cnp;
	struct thread *td;
	struct proc *p;
	cap_rights_t rights;
	struct uio auio;
	int error, linklen, startdir_used;

	// 注意这里，cnp 其实就是 ndp->ni_cnd，后面对于 cnp 的操作其实就是对 ni_cnd 的操作
	cnp = &ndp->ni_cnd;
	td = cnp->cn_thread;
	p = td->td_proc;
	ndp->ni_cnd.cn_cred = ndp->ni_cnd.cn_thread->td_ucred;	// 线程对应的通行证
	KASSERT(cnp->cn_cred && p, ("namei: bad cred/proc"));
	KASSERT((cnp->cn_nameiop & (~OPMASK)) == 0,
	    ("namei: nameiop contaminated with flags"));
	KASSERT((cnp->cn_flags & OPMASK) == 0,
	    ("namei: flags contaminated with nameiops"));
	MPASS(ndp->ni_startdir == NULL || ndp->ni_startdir->v_type == VDIR ||
	    ndp->ni_startdir->v_type == VBAD);

	// 进程对应的文件描述符，这里初始化 fdp，后面会用到
	fdp = p->p_fd;
	TAILQ_INIT(&ndp->ni_cap_tracker); // 初始化 tracker 队列
	ndp->ni_lcf = 0;

	/* We will set this ourselves if we need it. */
	cnp->cn_flags &= ~TRAILINGSLASH; // 表示路径以 '/' 结束

	/*
	 * Get a buffer for the name to be translated, and copy the
	 * name into the buffer. 为要翻译的名称获取一个缓冲区，并将名称复制到缓冲区中
	 * 将 nameidata 中的数据拷贝到 componentname 当中(下面的代码为 componentname 分配了存储空间)
	 */
	if ((cnp->cn_flags & HASBUF) == 0)	// 当 componentname 没有缓冲区时，要为其申请一块内存
		cnp->cn_pnbuf = uma_zalloc(namei_zone, M_WAITOK);
	/*
		内核空间和用户空间的数据拷贝方式不一样。ni_dirp 是 nameidata 指向路径名的指针，下面的操作就是将
		路径名数据拷贝到 cnp->cn_pnbuf 缓冲区。从这里看，感觉 componentname 表示可能不是一个组件名称，
		而是整个路径的名称，需要进一步验证猜想；
		从这里可以看出，nameidata 中的指针指向的是路径信息实际存储位置，而 componentname 中的相应字段
		只是用做缓存，可以加快查找速度
	*/
	if (ndp->ni_segflg == UIO_SYSSPACE)
		error = copystr(ndp->ni_dirp, cnp->cn_pnbuf, MAXPATHLEN,
		    &ndp->ni_pathlen);
	else
		error = copyinstr(ndp->ni_dirp, cnp->cn_pnbuf, MAXPATHLEN,
		    &ndp->ni_pathlen);

	/*
	 * Don't allow empty pathnames. 不允许空的路径
	 */
	if (error == 0 && *cnp->cn_pnbuf == '\0')
		error = ENOENT;

#ifdef CAPABILITY_MODE
	/*
	 * In capability mode, lookups must be restricted to happen in
	 * the subtree with the root specified by the file descriptor:
	 * 在功能模式下，必须限制查找发生在具有由文件描述符指定的根的子树中
	 * 
	 * - The root must be real file descriptor, not the pseudo-descriptor
	 *   AT_FDCWD.
	 * - The passed path must be relative and not absolute.
	 * - If lookup_cap_dotdot is disabled, path must not contain the
	 *   '..' components.
	 * - If lookup_cap_dotdot is enabled, we verify that all '..'
	 *   components lookups result in the directories which were
	 *   previously walked by us, which prevents an escape from
	 *   the relative root.
	 */
	if (error == 0 && IN_CAPABILITY_MODE(td) &&
	    (cnp->cn_flags & NOCAPCHECK) == 0) {
		ndp->ni_lcf |= NI_LCF_STRICTRELATIVE;
		if (ndp->ni_dirfd == AT_FDCWD) {
#ifdef KTRACE
			if (KTRPOINT(td, KTR_CAPFAIL))
				ktrcapfail(CAPFAIL_LOOKUP, NULL, NULL);
#endif
			error = ECAPMODE;
		}
	}
#endif
	if (error != 0) {
		namei_cleanup_cnp(cnp);	// 如果数据拷贝出错，则释放掉 componentname 的存储空间
		/*
			返回前将 ndp->ni_vp 设置为了空，说明这个函数最终目的就是获取对应文件的 vnode 并赋值
			给 ndp->ni_vp
		*/
		ndp->ni_vp = NULL;
		return (error);
	}
	ndp->ni_loopcnt = 0; // 查找过程中遇到的符号链接文件个数设置为0
#ifdef KTRACE
	if (KTRPOINT(td, KTR_NAMEI)) {
		KASSERT(cnp->cn_thread == curthread,
		    ("namei not using curthread"));
		ktrnamei(cnp->cn_pnbuf);
	}
#endif
	/*
	 * Get starting point for the translation. 
	 * 获取翻译的起点，应该就是路径的起始目录。操作进程文件描述符的时候，首先要进行锁操作
	 */
	FILEDESC_SLOCK(fdp);	// slock: shared lock
	ndp->ni_rootdir = fdp->fd_rdir;	// 文件描述符中保存的根目录
	vrefact(ndp->ni_rootdir);		// 貌似仅仅是增加引用计数，而不会进行加锁解锁
	ndp->ni_topdir = fdp->fd_jdir;	// rootdir 和 topdir 可能是同一个

	/*
	 * If we are auditing the kernel pathname, save the user pathname.
	 * 如果我们正在审核内核路径名，请保存用户路径名
	 */
	if (cnp->cn_flags & AUDITVNODE1)
		AUDIT_ARG_UPATH1(td, ndp->ni_dirfd, cnp->cn_pnbuf);
	if (cnp->cn_flags & AUDITVNODE2)
		AUDIT_ARG_UPATH2(td, ndp->ni_dirfd, cnp->cn_pnbuf);

	startdir_used = 0;	// 起始目录使用次数？
	dp = NULL;

	// 路径名指针指向缓存 (componentname)
	cnp->cn_nameptr = cnp->cn_pnbuf;
	if (cnp->cn_pnbuf[0] == '/') {
		/*
			绝对路径处理，namei_handle_root 函数返回指向一个二级目录名字的指针
		*/
		error = namei_handle_root(ndp, &dp);
	} else {
		/*
			下面的代码分支用来处理相对路径。首先就是判断 nameidata 起始目录项对应的 vnode 是否为空。
			如果不为空，则填充 dp，更新 startdir_used。说明 dp 是用于存储路径搜索起始目录的 vnode
		*/
		if (ndp->ni_startdir != NULL) {
			dp = ndp->ni_startdir;
			startdir_used = 1;
		} else if (ndp->ni_dirfd == AT_FDCWD) {
			dp = fdp->fd_cdir;
			vrefact(dp);
		} else {
			rights = ndp->ni_rightsneeded;
			cap_rights_set(&rights, CAP_LOOKUP);	// 设置 nameidata rights

			if (cnp->cn_flags & AUDITVNODE1)
				AUDIT_ARG_ATFD1(ndp->ni_dirfd);
			if (cnp->cn_flags & AUDITVNODE2)
				AUDIT_ARG_ATFD2(ndp->ni_dirfd);
			/*
				当上述两种情况都不存在的时候，就只能调用 fgetvp_rights 来获取 dp。注意其中传入了 rights 和
				ndp->ni_filecaps，推测是利用这两个参数来定位到目标 file，进而获取对应的 vnode
			*/
			error = fgetvp_rights(td, ndp->ni_dirfd,
			    &rights, &ndp->ni_filecaps, &dp);
			/*
				如果上面的检查出现错误，则报告 Not a directory 错误
			*/
			if (error == EINVAL)
				error = ENOTDIR;
#ifdef CAPABILITIES
			/*
			 * If file descriptor doesn't have all rights,
			 * all lookups relative to it must also be
			 * strictly relative.
			 * 如果文件描述符没有所有权限，那么所有与之相关的查找也必须是严格相对的
			 */
			CAP_ALL(&rights);
			if (!cap_rights_contains(&ndp->ni_filecaps.fc_rights,
			    &rights) ||
			    ndp->ni_filecaps.fc_fcntls != CAP_FCNTL_ALL ||
			    ndp->ni_filecaps.fc_nioctls != -1) {
				ndp->ni_lcf |= NI_LCF_STRICTRELATIVE;
			}
#endif
		}
		/* 
			dp 表示的是根目录或者路径搜索起始目录，要注意不是普通文件
		*/
		if (error == 0 && dp->v_type != VDIR)
			error = ENOTDIR;
	}	// 绝对路径和相对路径处理初步完成
	FILEDESC_SUNLOCK(fdp);
	/*
		当起始目录不为空并且 startdir_used 为0的时候，要将起始目录的的引用计数减一；
		vrele 接收一个 unlocked vnode，返回仍然是一个 unlocked vnode
	*/
	if (ndp->ni_startdir != NULL && !startdir_used)
		vrele(ndp->ni_startdir);
	if (error != 0) {
		if (dp != NULL)
			vrele(dp);
		goto out;
	}
	/*
		当 nameidata 支持相对路径查找，并且 lookup_cap_dotdot 不为0的时候，对 nameidata
		ni_lcf 属性进行设置
	*/
	if ((ndp->ni_lcf & NI_LCF_STRICTRELATIVE) != 0 &&
	    lookup_cap_dotdot != 0)
		ndp->ni_lcf |= NI_LCF_CAP_DOTDOT;
	SDT_PROBE3(vfs, namei, lookup, entry, dp, cnp->cn_pnbuf,
	    cnp->cn_flags);

	/*
		for 循环会首先更新 ndp->ni_startdir，推测是每次循环都会处理一个级别的目录项，然后进行
		下一个目录项的处理，所以才会每次都更新一下 nameidata 中的成员
	*/
	for (;;) {
		ndp->ni_startdir = dp;
		error = lookup(ndp);
		if (error != 0)
			goto out;
		/*
		 * If not a symbolic link, we're done.
		 		如果不是符号链接的话，我们就算完成了
		 */
		if ((cnp->cn_flags & ISSYMLINK) == 0) {
			vrele(ndp->ni_rootdir);
			if ((cnp->cn_flags & (SAVENAME | SAVESTART)) == 0) {
				namei_cleanup_cnp(cnp);
			} else
				cnp->cn_flags |= HASBUF;
			nameicap_cleanup(ndp);
			SDT_PROBE2(vfs, namei, lookup, return, 0, ndp->ni_vp);
			return (0);
		}
		/*
			链接文件的深度其实是有限制的，超过这个就会报错
		*/
		if (ndp->ni_loopcnt++ >= MAXSYMLINKS) {
			error = ELOOP;
			break;
		}
#ifdef MAC
		if ((cnp->cn_flags & NOMACCHECK) == 0) {
			error = mac_vnode_check_readlink(td->td_ucred,
			    ndp->ni_vp);
			if (error != 0)
				break;
		}
#endif
		if (ndp->ni_pathlen > 1)
			cp = uma_zalloc(namei_zone, M_WAITOK);
		else
			cp = cnp->cn_pnbuf;
		aiov.iov_base = cp;
		aiov.iov_len = MAXPATHLEN;
		auio.uio_iov = &aiov;
		auio.uio_iovcnt = 1;
		auio.uio_offset = 0;
		auio.uio_rw = UIO_READ;
		auio.uio_segflg = UIO_SYSSPACE;
		auio.uio_td = td;
		auio.uio_resid = MAXPATHLEN;
		error = VOP_READLINK(ndp->ni_vp, &auio, cnp->cn_cred);
		if (error != 0) {
			if (ndp->ni_pathlen > 1)
				uma_zfree(namei_zone, cp);
			break;
		}
		linklen = MAXPATHLEN - auio.uio_resid;
		if (linklen == 0) {
			if (ndp->ni_pathlen > 1)
				uma_zfree(namei_zone, cp);
			error = ENOENT;
			break;
		}
		if (linklen + ndp->ni_pathlen > MAXPATHLEN) {
			if (ndp->ni_pathlen > 1)
				uma_zfree(namei_zone, cp);
			error = ENAMETOOLONG;
			break;
		}
		if (ndp->ni_pathlen > 1) {
			/*
				C语言中的定义：void bcopy(const void *src, void *dest, int n)
				意思就是把原有路径缓存中的剩余字符拷贝到 cp + linklen 处，说明链接文件可能会出现在路径中的任意位置
			*/
			bcopy(ndp->ni_next, cp + linklen, ndp->ni_pathlen);
			uma_zfree(namei_zone, cnp->cn_pnbuf);
			cnp->cn_pnbuf = cp;	// 更新 cnp 中的路径缓存地址
		} else
			cnp->cn_pnbuf[linklen] = '\0';
		ndp->ni_pathlen += linklen;	// 更新路径中的剩余字节数
		vput(ndp->ni_vp);
		dp = ndp->ni_dvp;	// dp 更新为当前目录
		/*
		 * Check if root directory should replace current directory.
		 		如果链接文件中存放的是绝对路径，则从 rootdir 重新开始查找
		 */
		cnp->cn_nameptr = cnp->cn_pnbuf;
		if (*(cnp->cn_nameptr) == '/') {
			vrele(dp);
			error = namei_handle_root(ndp, &dp);
			if (error != 0)
				goto out;
		}
	}	// end for

	vput(ndp->ni_vp);
	ndp->ni_vp = NULL;
	vrele(ndp->ni_dvp);
out:
	vrele(ndp->ni_rootdir);
	namei_cleanup_cnp(cnp);
	/*
		该函数会对 tracker 队列进行清理，所以在 namei 函数之外，tracker 队列应该是没有任何作用的
	*/
	nameicap_cleanup(ndp);
	SDT_PROBE2(vfs, namei, lookup, return, error, NULL);
	return (error);
}

static int
compute_cn_lkflags(struct mount *mp, int lkflags, int cnflags)
{

	if (mp == NULL || ((lkflags & LK_SHARED) &&
	    (!(mp->mnt_kern_flag & MNTK_LOOKUP_SHARED) ||
	    ((cnflags & ISDOTDOT) &&
	    (mp->mnt_kern_flag & MNTK_LOOKUP_EXCL_DOTDOT))))) {
		lkflags &= ~LK_SHARED;
		lkflags |= LK_EXCLUSIVE;
	}
	lkflags |= LK_NODDLKTREAT;
	return (lkflags);
}

static __inline int
needs_exclusive_leaf(struct mount *mp, int flags)
{
	/*
	 * Intermediate nodes can use shared locks, we only need to
	 * force an exclusive lock for leaf nodes.
	 * 中间节点可以使用共享锁，我们只需要为叶节点强制一个独占锁
	 */
	if ((flags & (ISLASTCN | LOCKLEAF)) != (ISLASTCN | LOCKLEAF))
		return (0);

	/* Always use exclusive locks if LOCKSHARED isn't set. 
			如果未设置LOCKSHARED，请始终使用独占锁
	*/
	if (!(flags & LOCKSHARED))
		return (1);

	/*
	 * For lookups during open(), if the mount point supports
	 * extended shared operations, then use a shared lock for the
	 * leaf node, otherwise use an exclusive lock.
	 * 对于 open 期间的查找，如果装载点支持扩展共享操作，则对叶节点使用共享锁，
	 * 否则使用独占锁
	 */
	if ((flags & ISOPEN) != 0)
		return (!MNT_EXTENDED_SHARED(mp));

	/*
	 * Lookup requests outside of open() that specify LOCKSHARED
	 * only need a shared lock on the leaf vnode.
	 * 在 open 之外指定 LOCKSHARED 的查找请求只需要叶 vnode 上的共享锁
	 */
	return (0);
}

/*
 * Search a pathname.
 * This is a very central and rather complicated routine.
 * 这是一个非常核心和相当复杂的程序
 *
 * The pathname is pointed to by ni_ptr and is of length ni_pathlen.
 * The starting directory is taken from ni_startdir. The pathname is
 * descended until done, or a symbolic link is encountered. The variable
 * ni_more is clear if the path is completed; it is set to one if a
 * symbolic link needing interpretation is encountered.
 * ni_ptr 指向路径名字符数据，ni_pathlen 表示的路径名长度。ni_startdir 表示的是路径起始目录。
 *
 * The flag argument is LOOKUP, CREATE, RENAME, or DELETE depending on
 * whether the name is to be looked up, created, renamed, or deleted.
 * When CREATE, RENAME, or DELETE is specified, information usable in
 * creating, renaming, or deleting a directory entry may be calculated.
 * If flag has LOCKPARENT or'ed into it, the parent directory is returned
 * locked. If flag has WANTPARENT or'ed into it, the parent directory is
 * returned unlocked. Otherwise the parent directory is not returned. If
 * the target of the pathname exists and LOCKLEAF is or'ed into the flag
 * the target is returned locked, otherwise it is returned unlocked.
 * When creating or renaming and LOCKPARENT is specified, the target may not
 * be ".".  When deleting and LOCKPARENT is specified, the target may be ".".
 *
 * Overall outline of lookup: 查找的总体轮廓
 *
 * dirloop:
 *	identify next component of name at ndp->ni_ptr
 *	handle degenerate case where name is null string 处理名称为空字符串的退化情况
 *	if .. and crossing mount points and on mounted filesys, find parent
 *	call VOP_LOOKUP routine for next component name
 *	    directory vnode returned in ni_dvp, unlocked unless LOCKPARENT set
 *	    component vnode returned in ni_vp (if it exists), locked.
 *	if result vnode is mounted on and crossing mount points,
 *	    find mounted on vnode
 *	if more components of name, do next level at dirloop
 *	return the answer in ni_vp, locked if LOCKLEAF set
 *	    if LOCKPARENT set, return locked parent in ni_dvp
 *	    if WANTPARENT set, return unlocked parent in ni_dvp
 */

/*
	通过 namei 函数测处理，我们已经获取到了当前目录项对应的 vnode。如果是绝对路径的话，那就是根节点的 vnode，
	也就是进程描述符中的 rootdir；如果是相对路径，那就是进程描述符中的当前目录 curdir。lookup 函数解析路径并
	获取第一个组件名，其实就是当前目录下的一个文件节点
*/
int
lookup(struct nameidata *ndp)
{
	char *cp;			/* pointer into pathname argument */
	char *prev_ni_next;		/* saved ndp->ni_next */
	struct vnode *dp = NULL;	/* the directory we are searching */
	struct vnode *tdp;		/* saved dp - target directory pointer，目标目录项的 vnode */
	struct mount *mp;		/* mount table entry */
	struct prison *pr;
	size_t prev_ni_pathlen;		/* saved ndp->ni_pathlen 用于保存路径长度 */
	int docache;			/* == 0 do not cache last component 不要缓存最后一个组件(所要查找的文件名？) */
	int wantparent;			/* 1 => wantparent or lockparent flag 判断父目录要不要加锁 */
	int rdonly;			/* lookup read-only flag bit 是否是只读的 */
	int error = 0;
	int dpunlocked = 0;		/* dp has already been unlocked - 目录 vnode 已经被解锁了 */
	int relookup = 0;		/* do not consume the path component 不要使用路径组件 */
	struct componentname *cnp = &ndp->ni_cnd;
	int lkflags_save;
	int ni_dvp_unlocked;
	
	/*
	 * Setup: break out flag bits into variables.
	 		设置：将标志位分解为变量。它应该是标记中间目录是否加锁的标志，这个应该是把 flags 中的属性放到
			变量当中，应该是为了方便判断
	 */
	ni_dvp_unlocked = 0;
	/*
		这个操作很奇怪，应该就是只要包含这两个属性的其中一个就设置 wantparent 标志位
		LOCKPARENT：返回加锁状态的 parent vnode
		WANTPARENT：返回不加锁状态的 parent vnode
	*/
	wantparent = cnp->cn_flags & (LOCKPARENT | WANTPARENT);
	KASSERT(cnp->cn_nameiop == LOOKUP || wantparent,
	    ("CREATE, DELETE, RENAME require LOCKPARENT or WANTPARENT."));
	/*
		判断是否需要缓存。从代码逻辑可以看到，默认情况是需要根据 cn_flags 来判断是不是需要进行
		名称缓存。当我们所要执行的操作是 delete；或者指定父节点 vnode 需要加锁，并且不是执行
		创建和查找操作，此时则不需要进行名称缓存
	*/
	docache = (cnp->cn_flags & NOCACHE) ^ NOCACHE;
	if (cnp->cn_nameiop == DELETE ||
	    (wantparent && cnp->cn_nameiop != CREATE &&
	     cnp->cn_nameiop != LOOKUP))
		docache = 0;
	rdonly = cnp->cn_flags & RDONLY;
	cnp->cn_flags &= ~ISSYMLINK;	// 遇到链接文件需要找到真实的文件？应该是初始化为非链接文件
	ndp->ni_dvp = NULL;	// 首先将路径中间目录项对应的 vnode
	/*
	 * We use shared locks until we hit the parent of the last cn then
	 * we adjust based on the requesting flags.
	 * 我们使用共享锁，直到到达最后一个 cn 的父级，然后根据请求的标志进行调整。也就是说路径中间的组件
	 * 对应的 vnode 都是利用共享锁进行锁操作，只有到最后一个目录的时候，在根据 flag 进行调整
	 */
	cnp->cn_lkflags = LK_SHARED;
	/*
		从 namei 函数调用情况可以知道，ni_startdir 在 for 循环中都是被指定过的。所以，这里获取到的就是
		需要处理的当前目录项，它会随着路径信息的处理而不断更新
	*/
	dp = ndp->ni_startdir;
	ndp->ni_startdir = NULLVP;	// 将 start directory 置空，等待下次更新
	vn_lock(dp,
	    compute_cn_lkflags(dp->v_mount, cnp->cn_lkflags | LK_RETRY,
	    cnp->cn_flags));	// 对 vnode 进行加锁

dirloop:
	/*
	 * Search a new directory. dirloop 循环中 cnp->flags 每次循环都要计算一下，在奇海系统中
	 		我们是通过绝对路径查找，所以应该只需要在处理完路径之后计算一次就可以
	 *
	 * The last component of the filename is left accessible via
	 * cnp->cn_nameptr for callers that need the name. Callers needing
	 * the name set the SAVENAME flag. When done, they assume
	 * responsibility for freeing the pathname buffer.
	 * 对于需要名称的呼叫者，可以通过 cnp->cn_nameptr 访问文件名的最后一个组件。
	 * 需要名称的呼叫者设置 SAVENAME 标志。完成后，它们将负责释放路径名缓冲区
	 * 
	 * 在 namei 函数中，如果是绝对路径，cn_nameptr 已经不指向 '/'，而是指向它的下一个
	 * 组件的起始位置。这个需要考虑的情况就比较多了。首先我们查找的既可能是目录文件，也可能
	 * 是普通文件；dot 和 dotdot 的情况；路径仅仅是根目录，等等
	 */
	for (cp = cnp->cn_nameptr; *cp != 0 && *cp != '/'; cp++)
		continue;
	/*
		componentname 中的路径名长度已经发生了改变，此时 cp 应该是指向 '/' 或者 '\0'；
		cnp->cn_nameptr 在 for 循环中是没有发生变化的；
		这里把 namelen 进行了更新，下面代码中就没有再进行赋值操作。namelen 代表的是什么？其实是我们从路径中解析出的
		第一个组件名字的长度，它应该就是当前目录中的一个子目录项的名字，该项可能是文件，也可能是一个目录。当前目录的 vnode
		也已经知道，这样就可以定位到目录对应的 inode，再进一步就可以知道我们所要查找的子目录项对应的 inode。这样我们
		就得到了子目录项的信息(通过 VOP_LOOKUP 函数)。然后我们就可以为子目录项分配一个 vnode？ 把名字加入到名称缓存
		当中，下次查找的时候就可以加快查找速度。
		下面代码中调用了 VOP_LOOKUP 函数，namelen 就是这里指定的长度，说明操作系统对路径名中的各个组件都进行了相同操作，
		整个解析下来每个组件都会拥有自己的 vnode 节点。所以，我们只要查找过一个文件，对应路径上的所有组件都会被分配一个 vnode，
		名字也可能会加载到缓存当中
	*/
	cnp->cn_namelen = cp - cnp->cn_nameptr;	// 获取一个组件名长度
	if (cnp->cn_namelen > NAME_MAX) {
		error = ENAMETOOLONG;
		goto bad;
	}
#ifdef NAMEI_DIAGNOSTIC
	{ char c = *cp;
	*cp = '\0';
	printf("{%s}: ", cnp->cn_nameptr);
	*cp = c; }
#endif
	prev_ni_pathlen = ndp->ni_pathlen;	// 此次处理之前的路径名长度，这个变量会在 relookup 情况下使用
	ndp->ni_pathlen -= cnp->cn_namelen;	// 更新 nameidata 的路径长度，减去的是一个路径组件名称
	KASSERT(ndp->ni_pathlen <= PATH_MAX,
	    ("%s: ni_pathlen underflow to %zd\n", __func__, ndp->ni_pathlen));
	prev_ni_next = ndp->ni_next;
	ndp->ni_next = cp;	// cp 指向的位置在上面for循环中已经发生了改变，移动了一个路径组件名字的长度
	// 从代码逻辑来看，此时 cp 指向的是字符 '/'

	/*
	 * Replace multiple slashes by a single slash and trailing slashes
	 * by a null.  This must be done before VOP_LOOKUP() because some
	 * fs's don't know about trailing slashes.  Remember if there were
	 * trailing slashes to handle symlinks, existing non-directories
	 * and non-existing files that won't be directories specially later.
	 * 
	 * 用一个斜杠替换多个斜杠，用空斜杠替换后面的斜杠。这必须在 VOP_LOOKUP 之前完成，
	 * 因为有些 fs 不知道后面的斜杠。请记住，如果后面有斜杠来处理符号链接、现有的非目录和
	 * 不存在的文件,这些文件稍后将不会成为目录
	 * 应该就是对尾部'/'的处理，不同的文件系统处理的方式不一样，要注意区分
	 */
	while (*cp == '/' && (cp[1] == '/' || cp[1] == '\0')) {
		cp++;
		ndp->ni_pathlen--;
		if (*cp == '\0') {
			/*
				cp 在此之前指向的是 '/' 字符，然后进入 while 循环并自加1，此时cp指向的是'/'后面的一个字符。
				如果该字符为 '\0'，说明 componentname 路径名其实是以字符 '/'为结尾，所以就设置 cn_flags
				字段，添加 TRAILINGSLASH 属性
			*/
			*ndp->ni_next = '\0';
			cnp->cn_flags |= TRAILINGSLASH;
		}
	}	// end while

	/*
		上面的 while 循环就是处理当遇到路径名中为 ...///////... 或者 '/\0' 的情况。当处理到路径末尾或者
		是不为 '/' 的字符时，退出循环；
		nameidata 处理路径名的时候应该是包含有两个对象，一个是当前处理的组件名称，然后是下一次要处理的组件。
		将组件名起始位置赋值给 ni_next 成员
	*/
	ndp->ni_next = cp;	// 此时 cp 有可能就是 '/0'

	cnp->cn_flags |= MAKEENTRY;	// 添加名称缓存属性
	// 这里应该就是把所有情况统一处理，处理 cp 为 '\0' 的情况
	if (*cp == '\0' && docache == 0)
		cnp->cn_flags &= ~MAKEENTRY;	// 不要将名字添加到名称缓存

	/* 判断是不是 dotdot 目录 */
	if (cnp->cn_namelen == 2 &&
	    cnp->cn_nameptr[1] == '.' && cnp->cn_nameptr[0] == '.')
		cnp->cn_flags |= ISDOTDOT;
	else
		cnp->cn_flags &= ~ISDOTDOT;

	/*
		判断是不是路径名的最后一个组件。因为后面对于 dot 和 dotdot 的处理过程中需要判断它们是不是路径中的
		最后组件，所以这一步一定要放到它们的处理逻辑之前
	*/
	if (*ndp->ni_next == 0)
		cnp->cn_flags |= ISLASTCN;
	else
		cnp->cn_flags &= ~ISLASTCN;

	/*
		如果是最后一个组件，并且 namelen 为1，name 为 '.'，执行的操作是删除或者重命名，则返回无效参数。
		上面处理完 dotdot，这里多加一步处理 dot。dot 目录不能执行删除和重命名操作。这里注意 cnp->cn_nameptr，
		该成员在上述处理过程中一直保持不变，也就是处理单独一个 dot 目录的情况
	*/
	if ((cnp->cn_flags & ISLASTCN) != 0 &&
	    cnp->cn_namelen == 1 && cnp->cn_nameptr[0] == '.' &&
	    (cnp->cn_nameiop == DELETE || cnp->cn_nameiop == RENAME)) {
		error = EINVAL;
		goto bad;
	}

	/* 将当前目录对应的 tracker 添加到队列当中 */
	nameicap_tracker_add(ndp, dp);

	/*
	 * Check for degenerate(退化的) name (e.g. / or "")
	 * which is a way of talking about a directory,
	 * e.g. like "/." or ".".
	 * cnp->cn_nameptr 指向的是所要查找的组件名称，按理说不应该是空，即使是到了最后一个组件，也应该是cp指针
	 * 为空，cn_nameptr 应该还是指向组件名称的起始位置，除非该组件本身就是空的，比如 /a/b/c/ 这种以 / 作为
	 * 结尾的路径名。这个可以认为是我们所要访问的就是c这个目录项，所以代码分支中会出现 ndp->ni_vp = dp
	 */
	if (cnp->cn_nameptr[0] == '\0') {
		if (dp->v_type != VDIR) {
			error = ENOTDIR;
			goto bad;
		}
		if (cnp->cn_nameiop != LOOKUP) {
			error = EISDIR;
			goto bad;
		}
		/*
			wantparent 存在的时候要注意 vnode 状态，可能是需要增加其引用计数并加锁
		*/
		if (wantparent) {
			ndp->ni_dvp = dp;
			VREF(dp);	// 增加 vnode 引用计数并使得它处于加锁状态
		}
		ndp->ni_vp = dp;

		if (cnp->cn_flags & AUDITVNODE1)
			AUDIT_ARG_VNODE1(dp);
		else if (cnp->cn_flags & AUDITVNODE2)
			AUDIT_ARG_VNODE2(dp);
		/*
			当我们访问的是一个以"/"结尾的目录时， 根据 LOCKPARENT | LOCKLEAF 两个标志去判断是否需要
			对 dp 加锁。因为此时 ni_dvp 字段跟 ni_vp 字段表示的是同一个 vnode，所以只需要解锁一次
		*/
		if (!(cnp->cn_flags & (LOCKPARENT | LOCKLEAF)))
			VOP_UNLOCK(dp, 0);
		/* XXX This should probably move to the top of function. */
		if (cnp->cn_flags & SAVESTART)
			panic("lookup: SAVESTART");
		goto success;
	}	// 当 componentname 中路径名指针指向字符 '\0' 时的处理逻辑，说明路径名已经解析完毕，
	// 或者传入的字符本身就是空

	/*
	 * Handle "..": five special cases.
	 * 0. If doing a capability lookup and lookup_cap_dotdot is
	 *    disabled, return ENOTCAPABLE.
	 * 	  如果禁用了执行功能 lookup 和 lookup_cap_dotdot，则返回 ENOTCAPABLE
	 * 
	 * 1. Return an error if this is the last component of
	 *    the name and the operation is DELETE or RENAME.
	 *    如果这是名称的最后一个组件，并且操作是 DELETE 或 RENAME，则返回错误。这个机制
	 * 		与 dot 目录一致
	 * 
	 * 2. If at root directory (e.g. after chroot)
	 *    or at absolute root directory
	 *    then ignore it so can't get out.
	 *    如果在根目录（例如chroot之后）或绝对根目录，则忽略它，这样就无法退出
	 * 
	 * 3. If this vnode is the root of a mounted
	 *    filesystem, then replace it with the
	 *    vnode which was mounted on so we take the
	 *    .. in the other filesystem.
	 *    如果这个vnode是一个挂载的文件系统的根，那么用挂载的 vnode 替换它，
	 * 		这样我们就采用 .. 在另一个文件系统中
	 * 
	 * 4. If the vnode is the top directory of
	 *    the jail or chroot, don't let them out. 
	 * 		如果 vnode 是 jail 或 chroot 的顶级目录，请不要将它们释放
	 * 
	 * 5. If doing a capability lookup and lookup_cap_dotdot is
	 *    enabled, return ENOTCAPABLE if the lookup would escape
	 *    from the initial file descriptor directory.  Checks are
	 *    done by ensuring that namei() already traversed the
	 *    result of dotdot lookup.
	 *    如果执行功能查找并且启用了 lookup_cap_dotdot，则如果查找将从初始文件描述符目录转义，
	 * 		则返回 ENOTCAPABLE。检查是通过确保 namei() 已经遍历 dotdot lookup 的结果来完成的
	 */
	if (cnp->cn_flags & ISDOTDOT) {
		/* 
			如果 nameidata 支持相对路径查找，但是不具备 .. 目录项的查找能力，则出错
		*/
		if ((ndp->ni_lcf & (NI_LCF_STRICTRELATIVE | NI_LCF_CAP_DOTDOT))
		    == NI_LCF_STRICTRELATIVE) {
#ifdef KTRACE
			if (KTRPOINT(curthread, KTR_CAPFAIL))
				ktrcapfail(CAPFAIL_LOOKUP, NULL, NULL);
#endif
			error = ENOTCAPABLE;
			goto bad;
		}
		/*
			这个操作类似与 dot 目录，当 dotdot 是最后一个组件，但是要执行删除或者重命名的操作时，报错
		*/ 
		if ((cnp->cn_flags & ISLASTCN) != 0 &&
		    (cnp->cn_nameiop == DELETE || cnp->cn_nameiop == RENAME)) {
			error = EINVAL;
			goto bad;
		}
		/*
			.. 操作涉及到的情况会比较复杂，因为我们不知道上级目录的特性到底是什么样的，或者是传入路径的上层
			依然是一个 ..(这可能也是此处要用一个for循环来处理的原因)
		*/
		for (;;) {
			/*
				首先遍历 cnp->cr_prison，判断 dp 是不是 pr->pr_root 节点。如果是的话就直接退出循环
			*/
			for (pr = cnp->cn_cred->cr_prison; pr != NULL;
			     pr = pr->pr_parent)
				if (dp == pr->pr_root)
					break;

			if (dp == ndp->ni_rootdir || 
			    dp == ndp->ni_topdir || 
			    dp == rootvnode ||
			    pr != NULL ||
			    ((dp->v_vflag & VV_ROOT) != 0 &&
			     (cnp->cn_flags & NOCROSSMOUNT) != 0)) {
				/*
					当 dp 已经是顶层目录、根目录或者是 rootvnode 的时候，或者pr不为0的时候，亦或 dp 是某个
					文件系统的根节点对应的 vnode、并且设定不能跨文件系统查找的时候，直接赋值
				*/
				ndp->ni_dvp = dp;
				ndp->ni_vp = dp;
				VREF(dp);	// 增加 dp 的引用计数并且会加锁。这里处理的是挂载点的情况，也就是文件系统根节点
				goto nextname;	// 通过绝对路径或者是在另外一个文件系统中查找文件
			}
			/*
				该 if 条件判断的是 dp 不是一个根节点，说明就是一个正常的结点。如果是根节点的话，它必定是某个
				文件系统所挂载点。所以后面的代码分支就是处理该节点是根节点，即某个文件系统的挂载点时候的情况。
				mnt_vnodecovered 表示的是在原文件系统中，该目录对应的 vnode
			*/
			if ((dp->v_vflag & VV_ROOT) == 0)
				break;
			if (dp->v_iflag & VI_DOOMED) {	/* forced unmount */
				error = ENOENT;
				goto bad;
			}
			/*
				tdp 保存的是当前文件系统该节点对应的 vnode，后面的话需要将 dp 更新为原文件系统该节点对应的 vnode。
				应该就是处理挂载点的时候会用到这个
			*/
			tdp = dp;	
			dp = dp->v_mount->mnt_vnodecovered;
			/*
				该操作应该就类似于平时对多线程的同步处理机制。vnode 其实就是对所有的文件系统和进程都是共享的，可能当某一个
				进程需要访问某个文件进而处理到挂载点的时候，我们就需要对挂载点对应的 vnode 进行加锁操作，防止被其他进程在
				此期间进行修改。因为当前继承要使用它了，所以就同时增加它的引用计数
			*/
			VREF(dp);	// 增加引用计数并给 vnode 加锁
			/*
				原有文件系统对应的 vnode 由于挂载的缘故现在就不再需要使用它了，所以我们就将其解锁，这样其他的进程就可以访问
				该 vnode，同时再减少 vnode 的引用计数
			*/
			vput(tdp);	// 输入是一个已经加锁的vnode，然后减少应用计数并返回一个 unlocked 状态的 vnode
			vn_lock(dp,
			    compute_cn_lkflags(dp->v_mount, cnp->cn_lkflags |
			    LK_RETRY, ISDOTDOT));
			error = nameicap_check_dotdot(ndp, dp);
			if (error != 0) {
#ifdef KTRACE
				if (KTRPOINT(curthread, KTR_CAPFAIL))
					ktrcapfail(CAPFAIL_LOOKUP, NULL, NULL);
#endif
				goto bad;
			}
		}	// end for
	}	// end if，处理 .. 目录

	/*
	 * We now have a segment name to search for, and a directory to search.
	 * 我们现在有一个要搜索的段名和一个要搜索的目录。Linux/FreeBSD 应该都是支持 union mount，也就是说
	 * 多个文件系统可以同时挂载到同一个挂载点下，并且按照类似于栈的形式分布。最先挂载的文件系统位于栈底，
	 * 最后挂载的文件系统位于栈顶，也就是我们当前所看到的文件系统。union loop 应该就是对此类型 mount 进行
	 * 处理
	 */
unionlookup:
#ifdef MAC
	if ((cnp->cn_flags & NOMACCHECK) == 0) {
		error = mac_vnode_check_lookup(cnp->cn_thread->td_ucred, dp,
		    cnp);
		if (error)
			goto bad;
	}
#endif
	ndp->ni_dvp = dp;
	ndp->ni_vp = NULL;
	ASSERT_VOP_LOCKED(dp, "lookup");
	/*
	 * If we have a shared lock we may need to upgrade the lock for the
	 * last operation.
	 * 如果处理到当前路径的最后一个组件，并且 flag 中包含有 LOCKPARENT 属性，才会执行下面
	 * 的代码分支
	 */
	if ((cnp->cn_flags & LOCKPARENT) && (cnp->cn_flags & ISLASTCN) &&
	    dp != vp_crossmp && VOP_ISLOCKED(dp) == LK_SHARED)
		vn_lock(dp, LK_UPGRADE|LK_RETRY);
	if ((dp->v_iflag & VI_DOOMED) != 0) {
		error = ENOENT;
		goto bad;
	}
	/*
	 * If we're looking up the last component and we need an exclusive
	 * lock, adjust our lkflags.
	 * 最后一个组件的话也是需要判断是否需要设置独占锁
	 */
	if (needs_exclusive_leaf(dp->v_mount, cnp->cn_flags))
		cnp->cn_lkflags = LK_EXCLUSIVE;
#ifdef NAMEI_DIAGNOSTIC
	vn_printf(dp, "lookup in ");
#endif
	lkflags_save = cnp->cn_lkflags;
	cnp->cn_lkflags = compute_cn_lkflags(dp->v_mount, cnp->cn_lkflags,
	    cnp->cn_flags);
	error = VOP_LOOKUP(dp, &ndp->ni_vp, cnp);
	cnp->cn_lkflags = lkflags_save;	// 先根据 vnode 属性计算 lock flags，执行完 vop_lookup 之后再还原
	/*
		当调用文件系统底层查找函数出现错误的时候，执行下面的代码分支
	*/
	if (error != 0) {
		KASSERT(ndp->ni_vp == NULL, ("leaf should be empty"));
#ifdef NAMEI_DIAGNOSTIC
		printf("not found\n");
#endif
		if ((error == ENOENT) &&
		    (dp->v_vflag & VV_ROOT) && (dp->v_mount != NULL) &&
		    (dp->v_mount->mnt_flag & MNT_UNION)) {
			tdp = dp;
			dp = dp->v_mount->mnt_vnodecovered;
			VREF(dp);
			/*
				vput 接受一个 locked vnode，处理完成后返回的是一个 locked vnode。说明挂载点当前的 vnode
				是处于一个加锁的状态。当我们要处理 union mount 情形的时候，就需要对其进行解锁，减少引用计数，
				然后在处理下一个层级挂载的文件系统的根节点 vnode。先执行 vref 然后判断是否需要升级或者降级？
			*/
			vput(tdp);
			vn_lock(dp,
			    compute_cn_lkflags(dp->v_mount, cnp->cn_lkflags |
			    LK_RETRY, cnp->cn_flags));
			nameicap_tracker_add(ndp, dp);
			goto unionlookup;
		}

		// 该错误貌似只有在 autofs 中才会出现
		if (error == ERELOOKUP) {
			vref(dp);
			ndp->ni_vp = dp;
			error = 0;
			relookup = 1;
			goto good;
		}
		/*
			当返回值不为 EJUSTRETURN 的时候，直接 goto bad。说明 EJUSTRETURN 表示的是正常的情况。从文件系统
			lookup 函数的实现我们可以发现，只有当我们创建或者重命名、并且处理的对象刚好是路径中最后一个组件的时候，
			函数才会返回该值
		*/
		if (error != EJUSTRETURN)
			goto bad;
		/*
		 * At this point, we know we're at the end of the
		 * pathname.  If creating / renaming, we can consider
		 * allowing the file or directory to be created / renamed,
		 * provided we're not on a read-only filesystem.
		 */
		if (rdonly) {
			error = EROFS;
			goto bad;
		}
		/* trailing slash only allowed for directories */
		if ((cnp->cn_flags & TRAILINGSLASH) &&
		    !(cnp->cn_flags & WILLBEDIR)) {
			error = ENOENT;
			goto bad;
		}
		if ((cnp->cn_flags & LOCKPARENT) == 0)
			VOP_UNLOCK(dp, 0);
		/*
		 * We return with ni_vp NULL to indicate that the entry
		 * doesn't currently exist, leaving a pointer to the
		 * (possibly locked) directory vnode in ndp->ni_dvp.
		 */
		if (cnp->cn_flags & SAVESTART) {
			ndp->ni_startdir = ndp->ni_dvp;
			VREF(ndp->ni_startdir);
		}
		goto success;
	}

good:
#ifdef NAMEI_DIAGNOSTIC
	printf("found\n");
#endif
	dp = ndp->ni_vp;

	/*
	 * Check to see if the vnode has been mounted on;
	 * if so find the root of the mounted filesystem.
	 */
	while (dp->v_type == VDIR && (mp = dp->v_mountedhere) &&
	       (cnp->cn_flags & NOCROSSMOUNT) == 0) {
		if (vfs_busy(mp, 0))
			continue;
		vput(dp);
		if (dp != ndp->ni_dvp)
			vput(ndp->ni_dvp);
		else
			vrele(ndp->ni_dvp);
		vrefact(vp_crossmp);
		ndp->ni_dvp = vp_crossmp;
		error = VFS_ROOT(mp, compute_cn_lkflags(mp, cnp->cn_lkflags,
		    cnp->cn_flags), &tdp);
		vfs_unbusy(mp);
		if (vn_lock(vp_crossmp, LK_SHARED | LK_NOWAIT))
			panic("vp_crossmp exclusively locked or reclaimed");
		if (error) {
			dpunlocked = 1;
			goto bad2;
		}
		ndp->ni_vp = dp = tdp;
	}

	/*
	 * Check for symbolic link
	 */
	if ((dp->v_type == VLNK) &&
	    ((cnp->cn_flags & FOLLOW) || (cnp->cn_flags & TRAILINGSLASH) ||
	     *ndp->ni_next == '/')) {
		cnp->cn_flags |= ISSYMLINK;
		if (dp->v_iflag & VI_DOOMED) {
			/*
			 * We can't know whether the directory was mounted with
			 * NOSYMFOLLOW, so we can't follow safely.
			 */
			error = ENOENT;
			goto bad2;
		}
		if (dp->v_mount->mnt_flag & MNT_NOSYMFOLLOW) {
			error = EACCES;
			goto bad2;
		}
		/*
		 * Symlink code always expects an unlocked dvp.
		 		处理链接文件的代码一般都是要求 directory vnode 处于解锁状态
		 */
		if (ndp->ni_dvp != ndp->ni_vp) {
			VOP_UNLOCK(ndp->ni_dvp, 0);
			ni_dvp_unlocked = 1;
		}
		goto success;
	}

nextname:
	/*
	 * Not a symbolic link that we will follow.  Continue with the
	 * next component if there is any; otherwise, we're done.
	 */
	KASSERT((cnp->cn_flags & ISLASTCN) || *ndp->ni_next == '/',
	    ("lookup: invalid path state."));
	/*
		relookup 貌似只有在 autofs_lookup 中才会返回这个错误，所以推测这个可能就是针对
		该文件系统所做的一个特殊处理
	*/
	if (relookup) {
		relookup = 0;
		ndp->ni_pathlen = prev_ni_pathlen;
		ndp->ni_next = prev_ni_next;
		if (ndp->ni_dvp != dp)
			vput(ndp->ni_dvp);
		else
			vrele(ndp->ni_dvp);
		goto dirloop;
	}
	if (cnp->cn_flags & ISDOTDOT) {
		error = nameicap_check_dotdot(ndp, ndp->ni_vp);
		if (error != 0) {
#ifdef KTRACE
			if (KTRPOINT(curthread, KTR_CAPFAIL))
				ktrcapfail(CAPFAIL_LOOKUP, NULL, NULL);
#endif
			goto bad2;
		}
	}
	if (*ndp->ni_next == '/') {
		cnp->cn_nameptr = ndp->ni_next;
		while (*cnp->cn_nameptr == '/') {
			cnp->cn_nameptr++;
			ndp->ni_pathlen--;
		}
		if (ndp->ni_dvp != dp)
			vput(ndp->ni_dvp);
		else
			vrele(ndp->ni_dvp);
		goto dirloop;
	}
	/*
		注意，这里是 dirloop 最后一次出现的地方，也就是说后面的代码不会再返回到循环当中处理
		路径信息。所以下面的代码就是对路径信息处理完成后进行的一些属性的操作
	*/
	/*
	 * If we're processing a path with a trailing slash,
	 * check that the end result is a directory.
	 */
	if ((cnp->cn_flags & TRAILINGSLASH) && dp->v_type != VDIR) {
		error = ENOTDIR;
		goto bad2;
	}
	/*
	 */
	if (rdonly &&
	    (cnp->cn_nameiop == DELETE || cnp->cn_nameiop == RENAME)) {
		error = EROFS;
		goto bad2;
	}
	if (cnp->cn_flags & SAVESTART) {
		ndp->ni_startdir = ndp->ni_dvp;
		VREF(ndp->ni_startdir);
	}
	/*
		从前面的代码可以看到，wantparent 其实是两个 flag 组合而成的，所以如果 wantparent 标志不成立
		的话，其实就是这两个 flag 都是不支持的，所以 ni_dvp_unlocked = 2。else if 分支应该就是只判断
		LOCKPARENT 不支持的情况，所以才令 ni_dvp_unlocked = 1。后面的话就根据该变量来进行不同的错误
		操作
	*/
	if (!wantparent) {
		ni_dvp_unlocked = 2;
		if (ndp->ni_dvp != dp)
			vput(ndp->ni_dvp);
		else
			vrele(ndp->ni_dvp);
	} else if ((cnp->cn_flags & LOCKPARENT) == 0 && ndp->ni_dvp != dp) {
		VOP_UNLOCK(ndp->ni_dvp, 0);
		ni_dvp_unlocked = 1;
	}

	if (cnp->cn_flags & AUDITVNODE1)
		AUDIT_ARG_VNODE1(dp);
	else if (cnp->cn_flags & AUDITVNODE2)
		AUDIT_ARG_VNODE2(dp);

	if ((cnp->cn_flags & LOCKLEAF) == 0)
		VOP_UNLOCK(dp, 0);
success:
	/*
	 * Because of shared lookup we may have the vnode shared locked, but
	 * the caller may want it to be exclusively locked.
	 * 因为 shared lookup 的缘故我们可能会有一个 vnode 处于 shared locked 状态。但是调用者可能希望
	 * 它是一个独占锁的状态，此时我们就要对该对象的锁操作进行升级，从共享锁升级为独占锁。此时的 dp 已经是
	 * ndp->ni_vp 字段。
	 * if 分支处理的是当调用者希望返回独占锁对象、但是当前该对象却是非独占锁状态的时候，要升级为独占锁
	 */
	if (needs_exclusive_leaf(dp->v_mount, cnp->cn_flags) &&
	    VOP_ISLOCKED(dp) != LK_EXCLUSIVE) {
		vn_lock(dp, LK_UPGRADE | LK_RETRY);
		if (dp->v_iflag & VI_DOOMED) {
			error = ENOENT;
			goto bad2;
		}
	}
	return (0);

bad2:
	if (ni_dvp_unlocked != 2) {
		if (dp != ndp->ni_dvp && !ni_dvp_unlocked)
			vput(ndp->ni_dvp);
		else
			vrele(ndp->ni_dvp);
	}
bad:
	if (!dpunlocked)
		vput(dp);
	ndp->ni_vp = NULL;
	return (error);
}

/*
 * relookup - lookup a path name component
 *    Used by lookup to re-acquire things.
 */
int
relookup(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp)
{
	struct vnode *dp = NULL;		/* the directory we are searching */
	int wantparent;			/* 1 => wantparent or lockparent flag */
	int rdonly;			/* lookup read-only flag bit */
	int error = 0;

	KASSERT(cnp->cn_flags & ISLASTCN,
	    ("relookup: Not given last component."));
	/*
	 * Setup: break out flag bits into variables.
	 */
	wantparent = cnp->cn_flags & (LOCKPARENT|WANTPARENT);
	KASSERT(wantparent, ("relookup: parent not wanted."));
	rdonly = cnp->cn_flags & RDONLY;
	cnp->cn_flags &= ~ISSYMLINK;
	dp = dvp;
	cnp->cn_lkflags = LK_EXCLUSIVE;
	vn_lock(dp, LK_EXCLUSIVE | LK_RETRY);

	/*
	 * Search a new directory.
	 *
	 * The last component of the filename is left accessible via
	 * cnp->cn_nameptr for callers that need the name. Callers needing
	 * the name set the SAVENAME flag. When done, they assume
	 * responsibility for freeing the pathname buffer.
	 */
#ifdef NAMEI_DIAGNOSTIC
	printf("{%s}: ", cnp->cn_nameptr);
#endif

	/*
	 * Check for "" which represents the root directory after slash
	 * removal.
	 */
	if (cnp->cn_nameptr[0] == '\0') {
		/*
		 * Support only LOOKUP for "/" because lookup()
		 * can't succeed for CREATE, DELETE and RENAME.
		 */
		KASSERT(cnp->cn_nameiop == LOOKUP, ("nameiop must be LOOKUP"));
		KASSERT(dp->v_type == VDIR, ("dp is not a directory"));

		if (!(cnp->cn_flags & LOCKLEAF))
			VOP_UNLOCK(dp, 0);
		*vpp = dp;
		/* XXX This should probably move to the top of function. */
		if (cnp->cn_flags & SAVESTART)
			panic("lookup: SAVESTART");
		return (0);
	}

	if (cnp->cn_flags & ISDOTDOT)
		panic ("relookup: lookup on dot-dot");

	/*
	 * We now have a segment name to search for, and a directory to search.
	 */
#ifdef NAMEI_DIAGNOSTIC
	vn_printf(dp, "search in ");
#endif
	if ((error = VOP_LOOKUP(dp, vpp, cnp)) != 0) {
		KASSERT(*vpp == NULL, ("leaf should be empty"));
		if (error != EJUSTRETURN)
			goto bad;
		/*
		 * If creating and at end of pathname, then can consider
		 * allowing file to be created.
		 */
		if (rdonly) {
			error = EROFS;
			goto bad;
		}
		/* ASSERT(dvp == ndp->ni_startdir) */
		if (cnp->cn_flags & SAVESTART)
			VREF(dvp);
		if ((cnp->cn_flags & LOCKPARENT) == 0)
			VOP_UNLOCK(dp, 0);
		/*
		 * We return with ni_vp NULL to indicate that the entry
		 * doesn't currently exist, leaving a pointer to the
		 * (possibly locked) directory vnode in ndp->ni_dvp.
		 */
		return (0);
	}

	dp = *vpp;

	/*
	 * Disallow directory write attempts on read-only filesystems.
	 */
	if (rdonly &&
	    (cnp->cn_nameiop == DELETE || cnp->cn_nameiop == RENAME)) {
		if (dvp == dp)
			vrele(dvp);
		else
			vput(dvp);
		error = EROFS;
		goto bad;
	}
	/*
	 * Set the parent lock/ref state to the requested state.
	 */
	if ((cnp->cn_flags & LOCKPARENT) == 0 && dvp != dp) {
		if (wantparent)
			VOP_UNLOCK(dvp, 0);
		else
			vput(dvp);
	} else if (!wantparent)
		vrele(dvp);
	/*
	 * Check for symbolic link
	 */
	KASSERT(dp->v_type != VLNK || !(cnp->cn_flags & FOLLOW),
	    ("relookup: symlink found.\n"));

	/* ASSERT(dvp == ndp->ni_startdir) */
	if (cnp->cn_flags & SAVESTART)
		VREF(dvp);
	
	if ((cnp->cn_flags & LOCKLEAF) == 0)
		VOP_UNLOCK(dp, 0);
	return (0);
bad:
	vput(dp);
	*vpp = NULL;
	return (error);
}

/*
	初始化 nameidata。假设我们要打开一个文件，经历的大致流程是： 应用程序或终端指令 -> syscall ->
	vfs -> fs。所以这里的 nameidata 中填充的数据应该就是调用 syscall 时我们所输入的数据，比如路径信息、
	文件名信息等等，相当于是对数据做了一个打包处理。
	做软件设计的时候可以借鉴这种处理方式，将一些需要同时传输的数据进行打包，组合成一个结构体类型，这样我们
	再设计函数的时候传参就会变得更加简洁，阅读起来也比较方便
	op 参数传入的是操作类型，比如 LOOKUP，应该就是查找目标文件的 vnode
*/
void
NDINIT_ALL(struct nameidata *ndp, u_long op, u_long flags, enum uio_seg segflg,
    const char *namep, int dirfd, struct vnode *startdir, cap_rights_t *rightsp,
    struct thread *td)
{
	// 首先设置 componentname 成员属性
	ndp->ni_cnd.cn_nameiop = op;
	ndp->ni_cnd.cn_flags = flags;
	ndp->ni_segflg = segflg;	// 指定是用户空间还是内核空间
	ndp->ni_dirp = namep;	// 路径信息
	ndp->ni_dirfd = dirfd;	// 从调用情况来看，这里传入的参数为 -100
	ndp->ni_startdir = startdir;	// 实际调用情况来看，startdir = NULL
	if (rightsp != NULL)
		ndp->ni_rightsneeded = *rightsp;
	else
		cap_rights_init(&ndp->ni_rightsneeded);
	filecaps_init(&ndp->ni_filecaps);
	ndp->ni_cnd.cn_thread = td;
}

/*
 * Free data allocated by namei(); see namei(9) for details.
 */
void
NDFREE(struct nameidata *ndp, const u_int flags)
{
	int unlock_dvp;
	int unlock_vp;

	unlock_dvp = 0;
	unlock_vp = 0;

	if (!(flags & NDF_NO_FREE_PNBUF) &&
	    (ndp->ni_cnd.cn_flags & HASBUF)) {
		uma_zfree(namei_zone, ndp->ni_cnd.cn_pnbuf);
		ndp->ni_cnd.cn_flags &= ~HASBUF;
	}
	if (!(flags & NDF_NO_VP_UNLOCK) &&
	    (ndp->ni_cnd.cn_flags & LOCKLEAF) && ndp->ni_vp)
		unlock_vp = 1;
	if (!(flags & NDF_NO_VP_RELE) && ndp->ni_vp) {
		if (unlock_vp) {
			vput(ndp->ni_vp);
			unlock_vp = 0;
		} else
			vrele(ndp->ni_vp);
		ndp->ni_vp = NULL;
	}
	if (unlock_vp)
		VOP_UNLOCK(ndp->ni_vp, 0);
	if (!(flags & NDF_NO_DVP_UNLOCK) &&
	    (ndp->ni_cnd.cn_flags & LOCKPARENT) &&
	    ndp->ni_dvp != ndp->ni_vp)
		unlock_dvp = 1;
	if (!(flags & NDF_NO_DVP_RELE) &&
	    (ndp->ni_cnd.cn_flags & (LOCKPARENT|WANTPARENT))) {
		if (unlock_dvp) {
			vput(ndp->ni_dvp);
			unlock_dvp = 0;
		} else
			vrele(ndp->ni_dvp);
		ndp->ni_dvp = NULL;
	}
	if (unlock_dvp)
		VOP_UNLOCK(ndp->ni_dvp, 0);
	if (!(flags & NDF_NO_STARTDIR_RELE) &&
	    (ndp->ni_cnd.cn_flags & SAVESTART)) {
		vrele(ndp->ni_startdir);
		ndp->ni_startdir = NULL;
	}
}

/*
 * Determine if there is a suitable alternate filename under the specified
 * prefix for the specified path.  If the create flag is set, then the
 * alternate prefix will be used so long as the parent directory exists.
 * This is used by the various compatibility ABIs so that Linux binaries prefer
 * files under /compat/linux for example.  The chosen path (whether under
 * the prefix or under /) is returned in a kernel malloc'd buffer pointed
 * to by pathbuf.  The caller is responsible for free'ing the buffer from
 * the M_TEMP bucket if one is returned.
 */
int
kern_alternate_path(struct thread *td, const char *prefix, const char *path,
    enum uio_seg pathseg, char **pathbuf, int create, int dirfd)
{
	struct nameidata nd, ndroot;
	char *ptr, *buf, *cp;
	size_t len, sz;
	int error;

	buf = (char *) malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
	*pathbuf = buf;

	/* Copy the prefix into the new pathname as a starting point. */
	len = strlcpy(buf, prefix, MAXPATHLEN);
	if (len >= MAXPATHLEN) {
		*pathbuf = NULL;
		free(buf, M_TEMP);
		return (EINVAL);
	}
	sz = MAXPATHLEN - len;
	ptr = buf + len;

	/* Append the filename to the prefix. */
	if (pathseg == UIO_SYSSPACE)
		error = copystr(path, ptr, sz, &len);
	else
		error = copyinstr(path, ptr, sz, &len);

	if (error) {
		*pathbuf = NULL;
		free(buf, M_TEMP);
		return (error);
	}

	/* Only use a prefix with absolute pathnames. */
	if (*ptr != '/') {
		error = EINVAL;
		goto keeporig;
	}

	if (dirfd != AT_FDCWD) {
		/*
		 * We want the original because the "prefix" is
		 * included in the already opened dirfd.
		 */
		bcopy(ptr, buf, len);
		return (0);
	}

	/*
	 * We know that there is a / somewhere in this pathname.
	 * Search backwards for it, to find the file's parent dir
	 * to see if it exists in the alternate tree. If it does,
	 * and we want to create a file (cflag is set). We don't
	 * need to worry about the root comparison in this case.
	 */

	if (create) {
		for (cp = &ptr[len] - 1; *cp != '/'; cp--);
		*cp = '\0';

		NDINIT(&nd, LOOKUP, NOFOLLOW, UIO_SYSSPACE, buf, td);
		error = namei(&nd);
		*cp = '/';
		if (error != 0)
			goto keeporig;
	} else {
		NDINIT(&nd, LOOKUP, NOFOLLOW, UIO_SYSSPACE, buf, td);

		error = namei(&nd);
		if (error != 0)
			goto keeporig;

		/*
		 * We now compare the vnode of the prefix to the one
		 * vnode asked. If they resolve to be the same, then we
		 * ignore the match so that the real root gets used.
		 * This avoids the problem of traversing "../.." to find the
		 * root directory and never finding it, because "/" resolves
		 * to the emulation root directory. This is expensive :-(
		 */
		NDINIT(&ndroot, LOOKUP, FOLLOW, UIO_SYSSPACE, prefix,
		    td);

		/* We shouldn't ever get an error from this namei(). */
		error = namei(&ndroot);
		if (error == 0) {
			if (nd.ni_vp == ndroot.ni_vp)
				error = ENOENT;

			NDFREE(&ndroot, NDF_ONLY_PNBUF);
			vrele(ndroot.ni_vp);
		}
	}

	NDFREE(&nd, NDF_ONLY_PNBUF);
	vrele(nd.ni_vp);

keeporig:
	/* If there was an error, use the original path name. */
	if (error)
		bcopy(ptr, buf, len);
	return (error);
}
