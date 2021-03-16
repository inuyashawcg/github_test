/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1985, 1989, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)namei.h	8.5 (Berkeley) 1/9/95
 * $FreeBSD: releng/12.0/sys/sys/namei.h 331621 2018-03-27 15:20:03Z brooks $
 */

#ifndef _SYS_NAMEI_H_
#define	_SYS_NAMEI_H_

#include <sys/caprights.h>
#include <sys/filedesc.h>
#include <sys/queue.h>
#include <sys/_uio.h>

/* 
	componentname: 组件名称
	它的命名所要表达的意思就是一个完整的文件名包含有路径和文件名，其实可以看作是被'/'分割开来的
	各个部分组成的，这个 componentname 其实就是表示各个组成部分的名称。而 nameidata 中的路径
	和长度信息表示的是整个路径和其长度信息。从这里也可以看出，虚拟文件系统对于路径的解析其实就是把
	整个路径分割成一个个小块，每个小块应该是有一个对应的vnode，我们查找的时候应该就是遍历vnode树，
	判断是否存在，进而推断某个目录或者文件是否存在
*/
struct componentname {
	/*
	 * Arguments to lookup.
	 */
	/*
		namei 会有不同的操作类型，包括查找、创建、销毁等等，所以 cn_nameiop 成员应该就是
		表示结构体所应用的场景
	*/
	u_long	cn_nameiop;	/* namei operation */
	u_int64_t cn_flags;	/* flags to namei */
	struct	thread *cn_thread;/* thread requesting lookup */
	struct	ucred *cn_cred;	/* credentials 证书 */
	int	cn_lkflags;	/* Lock flags LK_EXCLUSIVE or LK_SHARED */
	/*
	 * Shared between lookup and commit routines. 在查找和提交例程之间共享
	 */
	char	*cn_pnbuf;	/* pathname buffer 路径名缓存 */
	char	*cn_nameptr;	/* pointer to looked up name 指向查找名的指针 */
	long	cn_namelen;	/* length of looked up component */
};

/* 声明了vnode的队列，貌似是用于name的搜索 */
struct nameicap_tracker;
TAILQ_HEAD(nameicap_tracker_head, nameicap_tracker);

/*
 * Encapsulation of namei parameters. namei参数的封装
 */
struct nameidata {
	/*
	 * Arguments to namei/lookup.
	 */
	const	char *ni_dirp;		/* pathname pointer 指向路径名指针，查找文件？？ */
	enum	uio_seg ni_segflg;	/* location of pathname 标定指向的是用户空间还是内核空间 */
	cap_rights_t ni_rightsneeded;	/* rights required to look up vnode 请求查找vnode的权限 */
	/*
	 * Arguments to lookup.
	 */
	struct  vnode *ni_startdir;	/* starting directory 起始目录 */
	struct	vnode *ni_rootdir;	/* logical root directory 逻辑根目录 */
	struct	vnode *ni_topdir;	/* logical top directory 逻辑顶层目录 */
	int	ni_dirfd;		/* starting directory for *at functions *at函数的起始目录 */
	int	ni_lcf;			/* local call flags 本地呼叫标志 */
	/*
	 * Results: returned from namei
	 */
	struct filecaps ni_filecaps;	/* rights the *at base has */
	/*
	 * Results: returned from/manipulated by lookup
	 */
	struct	vnode *ni_vp;		/* vnode of result */
	struct	vnode *ni_dvp;		/* vnode of intermediate directory 中间目录的vnode */
	/*
	 * Shared between namei and lookup/commit routines.
	 */
	size_t	ni_pathlen;		/* remaining chars in path 路径中的剩余字符 */
	char	*ni_next;		/* next location in pathname 路径名中的下一个位置 */

	// 当我们沿着路径进行查询的时候，可能最后遇到的是一个符号链接，这个参数应该就是记录这个的
	u_int	ni_loopcnt;		/* count of symlinks encountered 遇到的符号链接计数 */
	/*
	 * Lookup parameters: this structure describes the subset of
	 * information from the nameidata structure that is passed
	 * through the VOP interface.
	 * 查找参数：此结构描述通过VOP接口传递的nameidata结构中的信息子集
	 * 
	 * nameidata 跟 componentname 是包含关系，componentname 主要是name的相关属性和缓存处理等
	 */
	struct componentname ni_cnd;
	struct nameicap_tracker_head ni_cap_tracker;
};

#ifdef _KERNEL
/*
 * namei operations
 */
#define	LOOKUP		0	/* perform name lookup only */
#define	CREATE		1	/* setup for file creation */
#define	DELETE		2	/* setup for file deletion */
#define	RENAME		3	/* setup for file renaming */
#define	OPMASK		3	/* mask for operation */
/*
 * namei operational modifier flags, stored in ni_cnd.flags
 */
#define	LOCKLEAF	0x0004	/* lock vnode on return */
#define	LOCKPARENT	0x0008	/* want parent vnode returned locked */
#define	WANTPARENT	0x0010	/* want parent vnode returned unlocked */
#define	NOCACHE		0x0020	/* name must not be left in cache 名称不能留在缓存中 */
#define	FOLLOW		0x0040	/* follow symbolic links */
#define	LOCKSHARED	0x0100	/* Shared lock leaf */
#define	NOFOLLOW	0x0000	/* do not follow symbolic links (pseudo) */
#define	MODMASK		0x01fc	/* mask of operational modifiers */
/*
 * Namei parameter descriptors.
 *
 * SAVENAME may be set by either the callers of namei or by VOP_LOOKUP.
 * If the caller of namei sets the flag (for example execve wants to
 * know the name of the program that is being executed), then it must
 * free the buffer. If VOP_LOOKUP sets the flag, then the buffer must
 * be freed by either the commit routine or the VOP_ABORT routine.
 * SAVESTART is set only by the callers of namei. It implies SAVENAME
 * plus the addition of saving the parent directory that contains the
 * name in ni_startdir. It allows repeated calls to lookup for the
 * name being sought. The caller is responsible for releasing the
 * buffer and for vrele'ing ni_startdir.
 */
#define	RDONLY		0x00000200 /* lookup with read-only semantics */
#define	HASBUF		0x00000400 /* has allocated pathname buffer */
#define	SAVENAME	0x00000800 /* save pathname buffer */
#define	SAVESTART	0x00001000 /* save starting directory */
#define	ISDOTDOT	0x00002000 /* current component name is .. */
#define	MAKEENTRY	0x00004000 /* entry is to be added to name cache 条目将被添加到名称缓存 */
#define	ISLASTCN	0x00008000 /* this is last component of pathname 这是路径名的最后一个组件 */
#define	ISSYMLINK	0x00010000 /* symlink needs interpretation */
#define	ISWHITEOUT	0x00020000 /* found whiteout */
#define	DOWHITEOUT	0x00040000 /* do whiteouts */
#define	WILLBEDIR	0x00080000 /* new files will be dirs; allow trailing / */
#define	ISUNICODE	0x00100000 /* current component name is unicode*/
#define	ISOPEN		0x00200000 /* caller is opening; return a real vnode. */
#define	NOCROSSMOUNT	0x00400000 /* do not cross mount points */
#define	NOMACCHECK	0x00800000 /* do not perform MAC checks */
#define	AUDITVNODE1	0x04000000 /* audit the looked up vnode information */
#define	AUDITVNODE2	0x08000000 /* audit the looked up vnode information */
#define	TRAILINGSLASH	0x10000000 /* path ended in a slash 路径以斜线结束 */
#define	NOCAPCHECK	0x20000000 /* do not perform capability checks */
#define	PARAMASK	0x3ffffe00 /* mask of parameter descriptors */

/*
 * Flags in ni_lcf, valid for the duration of the namei call.
 */
#define	NI_LCF_STRICTRELATIVE	0x0001	/* relative lookup only */
#define	NI_LCF_CAP_DOTDOT	0x0002	/* ".." in strictrelative case */

/*
 * Initialization of a nameidata structure.
 */
#define	NDINIT(ndp, op, flags, segflg, namep, td)			\
	NDINIT_ALL(ndp, op, flags, segflg, namep, AT_FDCWD, NULL, 0, td)
#define	NDINIT_AT(ndp, op, flags, segflg, namep, dirfd, td)		\
	NDINIT_ALL(ndp, op, flags, segflg, namep, dirfd, NULL, 0, td)
#define	NDINIT_ATRIGHTS(ndp, op, flags, segflg, namep, dirfd, rightsp, td) \
	NDINIT_ALL(ndp, op, flags, segflg, namep, dirfd, NULL, rightsp, td)
#define	NDINIT_ATVP(ndp, op, flags, segflg, namep, vp, td)		\
	NDINIT_ALL(ndp, op, flags, segflg, namep, AT_FDCWD, vp, 0, td)

void NDINIT_ALL(struct nameidata *ndp, u_long op, u_long flags,
    enum uio_seg segflg, const char *namep, int dirfd, struct vnode *startdir,
    cap_rights_t *rightsp, struct thread *td);

#define NDF_NO_DVP_RELE		0x00000001
#define NDF_NO_DVP_UNLOCK	0x00000002
#define NDF_NO_DVP_PUT		0x00000003
#define NDF_NO_VP_RELE		0x00000004
#define NDF_NO_VP_UNLOCK	0x00000008
#define NDF_NO_VP_PUT		0x0000000c
#define NDF_NO_STARTDIR_RELE	0x00000010
#define NDF_NO_FREE_PNBUF	0x00000020
#define NDF_ONLY_PNBUF		(~NDF_NO_FREE_PNBUF)

void NDFREE(struct nameidata *, const u_int);

int	namei(struct nameidata *ndp);
int	lookup(struct nameidata *ndp);
int	relookup(struct vnode *dvp, struct vnode **vpp,
	    struct componentname *cnp);
#endif

/*
 * Stats on usefulness of namei caches.
 * namei缓存的有用性统计
 */
struct nchstats {
	long	ncs_goodhits;		/* hits that we can really use */
	long	ncs_neghits;		/* negative hits that we can use */
	long	ncs_badhits;		/* hits we must drop */
	long	ncs_falsehits;		/* hits with id mismatch */
	long	ncs_miss;		/* misses */
	long	ncs_long;		/* long names that ignore cache */
	long	ncs_pass2;		/* names found with passes == 2 */
	long	ncs_2passes;		/* number of times we attempt it */
};

extern struct nchstats nchstats;

#endif /* !_SYS_NAMEI_H_ */
