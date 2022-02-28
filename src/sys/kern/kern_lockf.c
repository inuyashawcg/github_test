/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2008 Isilon Inc http://www.isilon.com/
 * Authors: Doug Rabson <dfr@rabson.org>
 * Developed with Red Inc: Alfred Perlstein <alfred@freebsd.org>
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
 */
/*-
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Scooter Morris at Genentech Inc.
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
 *	@(#)ufs_lockf.c	8.3 (Berkeley) 1/6/94
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: releng/12.0/sys/kern/kern_lockf.c 333855 2018-05-19 05:04:38Z mmacy $");

#include "opt_debug_lockf.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/hash.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sx.h>
#include <sys/unistd.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include <sys/lockf.h>
#include <sys/taskqueue.h>

#ifdef LOCKF_DEBUG
#include <sys/sysctl.h>

#include <ufs/ufs/extattr.h>
#include <ufs/ufs/quota.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/inode.h>

static int	lockf_debug = 0; /* control debug output */
SYSCTL_INT(_debug, OID_AUTO, lockf_debug, CTLFLAG_RW, &lockf_debug, 0, "");
#endif

static MALLOC_DEFINE(M_LOCKF, "lockf", "Byte-range locking structures");

struct owner_edge;
struct owner_vertex;	// vertex: 顶点，制高点
struct owner_vertex_list;
struct owner_graph;

// 表示没有发现 blocking lockf
#define NOLOCKF (struct lockf_entry *)0
// 表示的应该是当前进程
#define SELF	0x1
// 其他进程
#define OTHERS	0x2
static void	 lf_init(void *);
static int	 lf_hash_owner(caddr_t, struct vnode *, struct flock *, int);
static int	 lf_owner_matches(struct lock_owner *, caddr_t, struct flock *,
    int);
static struct lockf_entry *
		 lf_alloc_lock(struct lock_owner *);
static int	 lf_free_lock(struct lockf_entry *);
static int	 lf_clearlock(struct lockf *, struct lockf_entry *);
static int	 lf_overlaps(struct lockf_entry *, struct lockf_entry *);
static int	 lf_blocks(struct lockf_entry *, struct lockf_entry *);
static void	 lf_free_edge(struct lockf_edge *);
static struct lockf_edge *
		 lf_alloc_edge(void);
static void	 lf_alloc_vertex(struct lockf_entry *);
static int	 lf_add_edge(struct lockf_entry *, struct lockf_entry *);
static void	 lf_remove_edge(struct lockf_edge *);
static void	 lf_remove_outgoing(struct lockf_entry *);
static void	 lf_remove_incoming(struct lockf_entry *);
static int	 lf_add_outgoing(struct lockf *, struct lockf_entry *);
static int	 lf_add_incoming(struct lockf *, struct lockf_entry *);
static int	 lf_findoverlap(struct lockf_entry **, struct lockf_entry *,
    int);
static struct lockf_entry *
		 lf_getblock(struct lockf *, struct lockf_entry *);
static int	 lf_getlock(struct lockf *, struct lockf_entry *, struct flock *);
static void	 lf_insert_lock(struct lockf *, struct lockf_entry *);
static void	 lf_wakeup_lock(struct lockf *, struct lockf_entry *);
static void	 lf_update_dependancies(struct lockf *, struct lockf_entry *,
    int all, struct lockf_entry_list *);
static void	 lf_set_start(struct lockf *, struct lockf_entry *, off_t,
	struct lockf_entry_list*);
static void	 lf_set_end(struct lockf *, struct lockf_entry *, off_t,
	struct lockf_entry_list*);
static int	 lf_setlock(struct lockf *, struct lockf_entry *,
    struct vnode *, void **cookiep);
static int	 lf_cancel(struct lockf *, struct lockf_entry *, void *);
static void	 lf_split(struct lockf *, struct lockf_entry *,
    struct lockf_entry *, struct lockf_entry_list *);
#ifdef LOCKF_DEBUG
static int	 graph_reaches(struct owner_vertex *x, struct owner_vertex *y,
    struct owner_vertex_list *path);
static void	 graph_check(struct owner_graph *g, int checkorder);
static void	 graph_print_vertices(struct owner_vertex_list *set);
#endif
static int	 graph_delta_forward(struct owner_graph *g,
    struct owner_vertex *x, struct owner_vertex *y,
    struct owner_vertex_list *delta);
static int	 graph_delta_backward(struct owner_graph *g,
    struct owner_vertex *x, struct owner_vertex *y,
    struct owner_vertex_list *delta);
static int	 graph_add_indices(int *indices, int n,
    struct owner_vertex_list *set);
static int	 graph_assign_indices(struct owner_graph *g, int *indices,
    int nextunused, struct owner_vertex_list *set);
static int	 graph_add_edge(struct owner_graph *g,
    struct owner_vertex *x, struct owner_vertex *y);
static void	 graph_remove_edge(struct owner_graph *g,
    struct owner_vertex *x, struct owner_vertex *y);
static struct owner_vertex *graph_alloc_vertex(struct owner_graph *g,
    struct lock_owner *lo);
static void	 graph_free_vertex(struct owner_graph *g,
    struct owner_vertex *v);
static struct owner_graph * graph_init(struct owner_graph *g);
#ifdef LOCKF_DEBUG
static void	 lf_print(char *, struct lockf_entry *);
static void	 lf_printlist(char *, struct lockf_entry *);
static void	 lf_print_owner(struct lock_owner *);
#endif

/*
 * This structure is used to keep track of both local and remote lock
 * owners. The lf_owner field of the struct lockf_entry points back at
 * the lock owner structure. Each possible lock owner (local proc for
 * POSIX fcntl locks, local file for BSD flock locks or <pid,sysid>
 * pair for remote locks) is represented by a unique instance of
 * struct lock_owner.
 * 此结构用于跟踪本地和远程锁所有者。结构 lockf_entry 的 lf_owner 字段指向锁所有者结构.
 * 每个可能的锁所有者（POSIX fcntl锁的本地proc、BSD群集锁的本地文件或远程锁的 <pid，sysid>对）
 * 都由一个唯一的 struct lock_owner 实例表示
 *
 * If a lock owner has a lock that blocks some other lock or a lock
 * that is waiting for some other lock, it also has a vertex in the
 * owner_graph below.
 * 如果锁所有者拥有一个阻止其他锁的锁或一个正在等待其他锁的锁，那么它在下面的所有者图
 * 中也有一个顶点
 *
 * Locks:
 * (s)		locked by state->ls_lock
 * (S)		locked by lf_lock_states_lock
 * (g)		locked by lf_owner_graph_lock
 * (c)		const until freeing
 */
#define	LOCK_OWNER_HASH_SIZE	256

struct lock_owner {
	LIST_ENTRY(lock_owner) lo_link; /* (l) hash chain */
	int	lo_refs;	    /* (l) Number of locks referring to this */
	int	lo_flags;	    /* (c) Flags passwd to lf_advlock */
	/*
		lf_advlock 处理过程中利用该字段进行匹配
	*/
	caddr_t	lo_id;		    /* (c) Id value passed to lf_advlock */
	pid_t	lo_pid;		    /* (c) Process Id of the lock owner */
	int	lo_sysid;	    /* (c) System Id of the lock owner */
	int	lo_hash;	    /* (c) Used to lock the appropriate chain */
	struct owner_vertex *lo_vertex; /* (g) entry in deadlock graph */
};

LIST_HEAD(lock_owner_list, lock_owner);

struct lock_owner_chain {
	/*
		hash table 中的元素，应该也是为了解决冲突的问题，把每个元素都设计成链表的格式。
		当我们向链表中添加或者删除元素的时候，就要利用 lock 进行同步
	*/
	struct sx		lock;
	struct lock_owner_list	list;
};

/*
	sx：共享/独占锁，lf_lock_states_lock 对象应该就是管理 lf_lock_states
*/
static struct sx		lf_lock_states_lock;
static struct lockf_list	lf_lock_states; /* (S) */
/*
	全局的锁数组，每个元素都已一个锁拥有者链表。从代码实现来看，这个应该也是一个
	hash table，类似与 vnode 的管理
*/
static struct lock_owner_chain	lf_lock_owners[LOCK_OWNER_HASH_SIZE];

/*
 * Structures for deadlock detection.
		从注释的表述中可以推测一下：lock_owner_list 是所有锁拥有者的链表，可能我们
		进行查找、插入等操作的时候会用用到这个结构。然后系统又给这些所有者构造了图结构，
		防止死锁情况的发生
 *
 * We have two types of directed graph, the first is the set of locks,
 * both active and pending on a vnode. Within this graph, active locks
 * are terminal nodes in the graph (i.e. have no out-going
 * edges). Pending locks have out-going edges to each blocking active
 * lock that prevents the lock from being granted and also to each
 * older pending lock that would block them if it was active. The
 * graph for each vnode is naturally acyclic; new edges are only ever
 * added to or from new nodes (either new pending locks which only add
 * out-going edges or new active locks which only add in-coming edges)
 * therefore they cannot create loops in the lock graph.
 * 
 * 我们有两种类型的有向图，第一种是锁集，在vnode上处于活动状态和挂起状态。在这个图中，
 * 活动锁是图中的终端节点（即没有输出边）。挂起的锁对每个阻止该锁被授予的阻止活动的锁，
 * 以及每个旧的挂起的锁（如果该锁是活动的，则会阻止它们）都有向外的边。每个 vnode 的
 * 图自然是非循环的；新边只会添加到新节点或从新节点添加（新的挂起锁只会添加向外的边，
 * 新的活动锁只会添加向内的边），因此它们无法在锁图中创建循环
 *
 * The second graph is a global graph of lock owners. Each lock owner
 * is a vertex in that graph and an edge is added to the graph
 * whenever an edge is added to a vnode graph, with end points
 * corresponding to owner of the new pending lock and the owner of the
 * lock upon which it waits. In order to prevent deadlock, we only add
 * an edge to this graph if the new edge would not create a cycle.
 * 
 * 第二个图是锁所有者的全局图。每个锁所有者都是该图中的一个顶点，每当向 vnode 图中
 * 添加一条边时，就会向该图中添加一条边，其端点对应于新挂起锁的所有者及其等待的锁的
 * 所有者。为了防止死锁，我们只在新边不会创建循环的情况下向该图添加一条边
 * 
 * The lock owner graph is topologically sorted, i.e. if a node has
 * any outgoing edges, then it has an order strictly less than any
 * node to which it has an outgoing edge. We preserve this ordering
 * (and detect cycles) on edge insertion using Algorithm PK from the
 * paper "A Dynamic Topological Sort Algorithm for Directed Acyclic
 * Graphs" (ACM Journal of Experimental Algorithms, Vol 11, Article
 * No. 1.7)
 * 
 * 锁所有者图是按拓扑排序的，即如果一个节点有任何输出边，那么它的顺序严格小于它有
 * 输出边的任何节点。我们使用“有向无环图的动态拓扑排序算法”（ACM实验算法杂志，
 * 第11卷，第1.7篇）一文中的算法PK在边插入时保持这种排序（并检测循环）
 */
/*
	数据结构与算法中，图包含有顶点(vertex)和边(edge)。顶点不是某一个特定的节点，而是所有的
	节点都被认为是一个顶点；边就表示两个顶点之间的连线，应该可以认为是指向其他顶点的指针
*/
struct owner_vertex;

struct owner_edge {
	LIST_ENTRY(owner_edge) e_outlink; /* (g) link from's out-edge list */
	LIST_ENTRY(owner_edge) e_inlink;  /* (g) link to's in-edge list */
	int		e_refs;		  /* (g) number of times added */
	struct owner_vertex *e_from;	  /* (c) out-going from here */
	struct owner_vertex *e_to;	  /* (c) in-coming to here */
};
LIST_HEAD(owner_edge_list, owner_edge);

struct owner_vertex {
	TAILQ_ENTRY(owner_vertex) v_link; /* (g) workspace for edge insertion */
	uint32_t	v_gen;		  /* (g) workspace for edge insertion */
	int		v_order;	  /* (g) order of vertex in graph */
	struct owner_edge_list v_outedges;/* (g) list of out-edges */
	struct owner_edge_list v_inedges; /* (g) list of in-edges */
	struct lock_owner *v_owner;	  /* (c) corresponding lock owner */
};
TAILQ_HEAD(owner_vertex_list, owner_vertex);

struct owner_graph {
	struct owner_vertex** g_vertices; /* (g) pointers to vertices */
	int		g_size;		  /* (g) number of vertices */
	int		g_space;	  /* (g) space allocated for vertices */
	int		*g_indexbuf;	  /* (g) workspace for loop detection */
	uint32_t	g_gen;		  /* (g) increment when re-ordering */
};

static struct sx		lf_owner_graph_lock;	// lf_owner_graph 同步管理
static struct owner_graph	lf_owner_graph;

/*
 * Initialise various structures and locks.
 */
static void
lf_init(void *dummy)
{
	int i;

	sx_init(&lf_lock_states_lock, "lock states lock");
	LIST_INIT(&lf_lock_states);

	// 对全局的 lock owner 数组中的所有元素进行初始化
	for (i = 0; i < LOCK_OWNER_HASH_SIZE; i++) {
		sx_init(&lf_lock_owners[i].lock, "lock owners lock");
		LIST_INIT(&lf_lock_owners[i].list);
	}

	sx_init(&lf_owner_graph_lock, "owner graph lock");
	graph_init(&lf_owner_graph);	// 初始化 lockf owner 图
}
SYSINIT(lf_init, SI_SUB_LOCK, SI_ORDER_FIRST, lf_init, NULL);

/*
 * Generate a hash value for a lock owner.
 */
static int
lf_hash_owner(caddr_t id, struct vnode *vp, struct flock *fl, int flags)
{
	uint32_t h;

	if (flags & F_REMOTE) {
		// 处理 lock owner 是 remote NFS client 的情况
		h = HASHSTEP(0, fl->l_pid);
		h = HASHSTEP(h, fl->l_sysid);
	} else if (flags & F_FLOCK) {
		h = ((uintptr_t) id) >> 7;
	} else {
		h = ((uintptr_t) vp) >> 7;
	}

	return (h % LOCK_OWNER_HASH_SIZE);
}

/*
 * Return true if a lock owner matches the details passed to
 * lf_advlock.
 * 如果锁所有者与传递给 lf_advlock 的详细信息匹配，则返回true
 */
static int
lf_owner_matches(struct lock_owner *lo, caddr_t id, struct flock *fl,
    int flags)
{
	if (flags & F_REMOTE) {
		return lo->lo_pid == fl->l_pid
			&& lo->lo_sysid == fl->l_sysid;
	} else {
		return lo->lo_id == id;
	}
}

static struct lockf_entry *
lf_alloc_lock(struct lock_owner *lo)
{
	struct lockf_entry *lf;

	lf = malloc(sizeof(struct lockf_entry), M_LOCKF, M_WAITOK|M_ZERO);

#ifdef LOCKF_DEBUG
	if (lockf_debug & 4)
		printf("Allocated lock %p\n", lf);
#endif
	// 当 lock_owner 不为空的时候，执行 if 代码分支
	if (lo) {
		sx_xlock(&lf_lock_owners[lo->lo_hash].lock);
		lo->lo_refs++;
		sx_xunlock(&lf_lock_owners[lo->lo_hash].lock);
		lf->lf_owner = lo;
		// 一个 owner 可能会对应多个 entry，对应 lo_refs 增删？
	}

	return (lf);
}

// 释放掉一个 lockf_entry
static int
lf_free_lock(struct lockf_entry *lock)
{
	struct sx *chainlock;

	KASSERT(lock->lf_refs > 0, ("lockf_entry negative ref count %p", lock));
	if (--lock->lf_refs > 0)
		return (0);
	/*
	 * Adjust the lock_owner reference count and
	 * reclaim the entry if this is the last lock
	 * for that owner.
	 * 调整锁所有者引用计数，如果这是该所有者的最后一个锁，则收回该条目。
	 * 从这里可以看出，每个 owner 是可以对应到多个 lock 的
	 */
	struct lock_owner *lo = lock->lf_owner;
	if (lo) {
		KASSERT(LIST_EMPTY(&lock->lf_outedges),
		    ("freeing lock with dependencies"));
		KASSERT(LIST_EMPTY(&lock->lf_inedges),
		    ("freeing lock with dependants"));
		chainlock = &lf_lock_owners[lo->lo_hash].lock;
		sx_xlock(chainlock);
		KASSERT(lo->lo_refs > 0, ("lock owner refcount"));
		lo->lo_refs--;	// 释放掉一个lock的时候，对应的 owner 引用计数减一
		if (lo->lo_refs == 0) {
			// 当 owner 引用计数也变成0的时候，owner 也需要被释放
#ifdef LOCKF_DEBUG
			if (lockf_debug & 1)
				printf("lf_free_lock: freeing lock owner %p\n",
				    lo);
#endif
			if (lo->lo_vertex) {
				sx_xlock(&lf_owner_graph_lock);
				graph_free_vertex(&lf_owner_graph,
				    lo->lo_vertex);
				sx_xunlock(&lf_owner_graph_lock);
			}
			LIST_REMOVE(lo, lo_link);
			free(lo, M_LOCKF);
#ifdef LOCKF_DEBUG
			if (lockf_debug & 4)
				printf("Freed lock owner %p\n", lo);
#endif
		}
		sx_unlock(chainlock);
	}
	if ((lock->lf_flags & F_REMOTE) && lock->lf_vnode) {
		vrele(lock->lf_vnode);
		lock->lf_vnode = NULL;
	}
#ifdef LOCKF_DEBUG
	if (lockf_debug & 4)
		printf("Freed lock %p\n", lock);
#endif
	free(lock, M_LOCKF);
	return (1);
}

/*
 * Advisory record locking support
 */
int
lf_advlockasync(struct vop_advlockasync_args *ap, struct lockf **statep,
    u_quad_t size)
{
	struct lockf *state;
	struct flock *fl = ap->a_fl;	// 查找或者构造时使用
	struct lockf_entry *lock;
	struct vnode *vp = ap->a_vp;
	caddr_t id = ap->a_id;	// id 可能是进程地址
	int flags = ap->a_flags;
	int hash;
	struct lock_owner *lo;
	off_t start, end, oadd;
	int error;

	/*
	 * Handle the F_UNLKSYS case first - no need to mess about
	 * creating a lock owner for this one.
	 * 不需要为这个创建锁所有者。F_UNLCKSYS 表示的是清除给定系统 id 的锁
	 */
	if (ap->a_op == F_UNLCKSYS) {
		lf_clearremotesys(fl->l_sysid);
		return (0);
	}

	/*
	 * Convert the flock structure into a start and end.
	 		将 flock 结构中存储的信息转换成对文件访问的起始和终止位置
	 */
	switch (fl->l_whence) {
	case SEEK_SET:	// 起始位置
	case SEEK_CUR:	// 当前位置
		/*
		 * Caller is responsible for adding any necessary offset
		 * when SEEK_CUR is used.
		 * 调用方负责在使用 SEEK_CUR 时添加任何必要的偏移量
		 */
		start = fl->l_start;
		break;

	case SEEK_END:
		if (size > OFF_MAX ||
		    (fl->l_start > 0 && size > OFF_MAX - fl->l_start))
			return (EOVERFLOW);
		start = size + fl->l_start;
		break;

	default:
		return (EINVAL);
	}
	if (start < 0)
		return (EINVAL);
	if (fl->l_len < 0) {
		if (start == 0)
			return (EINVAL);
		end = start - 1;
		start += fl->l_len;
		if (start < 0)
			return (EINVAL);
	} else if (fl->l_len == 0) {
		end = OFF_MAX;
	} else {
		oadd = fl->l_len - 1;
		if (oadd > OFF_MAX - start)
			return (EOVERFLOW);
		end = start + oadd;
	}

retry_setlock:

	/*
	 * Avoid the common case of unlocking when inode has no locks.
	 		避免在 inode 没有锁时解锁的常见情况
	 */
	if (ap->a_op != F_SETLK && (*statep) == NULL) {
		VI_LOCK(vp);
		if ((*statep) == NULL) {
			fl->l_type = F_UNLCK;
			VI_UNLOCK(vp);
			return (0);
		}
		VI_UNLOCK(vp);
	}

	/*
	 * Map our arguments to an existing lock owner or create one
	 * if this is the first time we have seen this owner.
	 * 将我们的参数映射到现有的锁所有者，如果这是我们第一次见到这个所有者，则创建一个
	 */
	hash = lf_hash_owner(id, vp, fl, flags);
	sx_xlock(&lf_lock_owners[hash].lock);
	/*
		遍历 lock owner 的哈希表，查找该对象是否已经存在
	*/
	LIST_FOREACH(lo, &lf_lock_owners[hash].list, lo_link)
		if (lf_owner_matches(lo, id, fl, flags))
			break;
	if (!lo) {
		/*
		 * We initialise the lock with a reference
		 * count which matches the new lockf_entry
		 * structure created below.
		 * 如果 lock owner 在当前的哈希表中不存在，那么就要重新创建一个对象
		 */
		lo = malloc(sizeof(struct lock_owner), M_LOCKF,
		    M_WAITOK|M_ZERO);
#ifdef LOCKF_DEBUG
		if (lockf_debug & 4)
			printf("Allocated lock owner %p\n", lo);
#endif

		lo->lo_refs = 1;
		lo->lo_flags = flags;
		lo->lo_id = id;
		lo->lo_hash = hash;
		if (flags & F_REMOTE) {
			lo->lo_pid = fl->l_pid;
			lo->lo_sysid = fl->l_sysid;
		} else if (flags & F_FLOCK) {
			lo->lo_pid = -1;
			lo->lo_sysid = 0;
		} else {
			struct proc *p = (struct proc *) id;
			lo->lo_pid = p->p_pid;
			lo->lo_sysid = 0;
		}
		lo->lo_vertex = NULL;

#ifdef LOCKF_DEBUG
		if (lockf_debug & 1) {
			printf("lf_advlockasync: new lock owner %p ", lo);
			lf_print_owner(lo);
			printf("\n");
		}
#endif
		// 并且将重新创建的对象插入到哈希表中
		LIST_INSERT_HEAD(&lf_lock_owners[hash].list, lo, lo_link);
	} else {
		/*
		 * We have seen this lock owner before, increase its
		 * reference count to account for the new lockf_entry
		 * structure we create below.
		 * 如果该对象已经存在，我们则仅仅是增加 lock owner 的引用计数。说明 lock owner 的引用计数
		 * 对应的就是它所关联的 lock_entry 的数量
		 */
		lo->lo_refs++;
	}
	sx_xunlock(&lf_lock_owners[hash].lock);

	/*
	 * Create the lockf structure. We initialise the lf_owner
	 * field here instead of in lf_alloc_lock() to avoid paying
	 * the lf_lock_owners_lock tax twice.
	 * 创建锁结构。我们在这里初始化 lf_owner 字段，而不是在 lf_alloc_lock 中，
	 * 以避免支付两次 lf_lock_owner_lock 税
	 * 该函数的其中一个使用场景是在系统 open 一个文件的时候调用的，其中应该会包含打开文件的方式，即共享还是独占。
	 * 上面判断完 lock_owner 对象之后紧接着就开始创建 lock_entry，说明 lock_entry 是文件锁实现的一个基本
	 * 数据结构
	 */
	lock = lf_alloc_lock(NULL);
	lock->lf_refs = 1;
	lock->lf_start = start;
	lock->lf_end = end;
	lock->lf_owner = lo;
	lock->lf_vnode = vp;
	if (flags & F_REMOTE) {
		/*
		 * For remote locks, the caller may release its ref to
		 * the vnode at any time - we have to ref it here to
		 * prevent it from being recycled unexpectedly.
		 * 对于远程锁，调用方可以随时释放对vnode的引用 - 我们必须在这里引用它，
		 * 以防止它意外地被回收
		 */
		vref(vp);
	}

	/*
	 * XXX The problem is that VTOI is ufs specific, so it will
	 * break LOCKF_DEBUG for all other FS's other than UFS because
	 * it casts the vnode->data ptr to struct inode *.
	 */
/*	lock->lf_inode = VTOI(ap->a_vp); */
	lock->lf_inode = (struct inode *)0;
	lock->lf_type = fl->l_type;
	LIST_INIT(&lock->lf_outedges);
	LIST_INIT(&lock->lf_inedges);
	lock->lf_async_task = ap->a_task;
	lock->lf_flags = ap->a_flags;

	/*
	 * Do the requested operation. First find our state structure
	 * and create a new one if necessary - the caller's *statep
	 * variable and the state's ls_threads count is protected by
	 * the vnode interlock.
	 * 执行请求的操作。首先找到我们的状态结构，并在必要时创建一个新的结构 - 
	 * 调用者的 *statep 变量和状态的 ls_threads 受 vnode interlock 保护
	 */
	VI_LOCK(vp);
	if (vp->v_iflag & VI_DOOMED) {
		VI_UNLOCK(vp);
		lf_free_lock(lock);
		return (ENOENT);
	}

	/*
	 * Allocate a state structure if necessary.
	 		前面的代码用于判断我们是否需要创建一个 lockf_entry (根据 flock 结构体中的信息)。
			感觉 flock 只是用于传递锁对应的信息，而真正用于锁操作的应该是 lockf_entry / lockf;
			下面的代码应该就是判断是否要新创建一个 lockf 
	 */
	state = *statep;
	if (state == NULL) {
		struct lockf *ls;

		VI_UNLOCK(vp);

		ls = malloc(sizeof(struct lockf), M_LOCKF, M_WAITOK|M_ZERO);
		sx_init(&ls->ls_lock, "ls_lock");
		LIST_INIT(&ls->ls_active);
		LIST_INIT(&ls->ls_pending);
		ls->ls_threads = 1;

		sx_xlock(&lf_lock_states_lock);
		LIST_INSERT_HEAD(&lf_lock_states, ls, ls_link);
		sx_xunlock(&lf_lock_states_lock);

		/*
		 * Cope if we lost a race with some other thread while
		 * trying to allocate memory.
		 * 如果我们在试图分配内存时与其他线程的竞争中失败了，那么就要处理这个问题
		 */
		VI_LOCK(vp);
		if (vp->v_iflag & VI_DOOMED) {
			VI_UNLOCK(vp);
			sx_xlock(&lf_lock_states_lock);
			LIST_REMOVE(ls, ls_link);
			sx_xunlock(&lf_lock_states_lock);
			sx_destroy(&ls->ls_lock);
			free(ls, M_LOCKF);
			lf_free_lock(lock);
			return (ENOENT);
		}
		if ((*statep) == NULL) {
			state = *statep = ls;
			VI_UNLOCK(vp);
		} else {
			state = *statep;
			state->ls_threads++;
			VI_UNLOCK(vp);

			sx_xlock(&lf_lock_states_lock);
			LIST_REMOVE(ls, ls_link);
			sx_xunlock(&lf_lock_states_lock);
			sx_destroy(&ls->ls_lock);
			free(ls, M_LOCKF);
		}
	} else {
		state->ls_threads++;
		VI_UNLOCK(vp);
	}

	sx_xlock(&state->ls_lock);
	/*
	 * Recheck the doomed vnode after state->ls_lock is
	 * locked. lf_purgelocks() requires that no new threads add
	 * pending locks when vnode is marked by VI_DOOMED flag.
	 */
	VI_LOCK(vp);
	if (vp->v_iflag & VI_DOOMED) {
		state->ls_threads--;
		wakeup(state);
		VI_UNLOCK(vp);
		sx_xunlock(&state->ls_lock);
		lf_free_lock(lock);
		return (ENOENT);
	}
	VI_UNLOCK(vp);

	switch (ap->a_op) {
	case F_SETLK:
		error = lf_setlock(state, lock, vp, ap->a_cookiep);
		break;

	case F_UNLCK:
		error = lf_clearlock(state, lock);
		lf_free_lock(lock);
		break;

	case F_GETLK:
		error = lf_getlock(state, lock, fl);
		lf_free_lock(lock);
		break;

	case F_CANCEL:
		if (ap->a_cookiep)
			error = lf_cancel(state, lock, *ap->a_cookiep);
		else
			error = EINVAL;
		lf_free_lock(lock);
		break;

	default:
		lf_free_lock(lock);
		error = EINVAL;
		break;
	}

#ifdef DIAGNOSTIC
	/*
	 * Check for some can't happen stuff. In this case, the active
	 * lock list becoming disordered or containing mutually
	 * blocking locks. We also check the pending list for locks
	 * which should be active (i.e. have no out-going edges).
	 */
	LIST_FOREACH(lock, &state->ls_active, lf_link) {
		struct lockf_entry *lf;
		if (LIST_NEXT(lock, lf_link))
			KASSERT((lock->lf_start
				<= LIST_NEXT(lock, lf_link)->lf_start),
			    ("locks disordered"));
		LIST_FOREACH(lf, &state->ls_active, lf_link) {
			if (lock == lf)
				break;
			KASSERT(!lf_blocks(lock, lf),
			    ("two conflicting active locks"));
			if (lock->lf_owner == lf->lf_owner)
				KASSERT(!lf_overlaps(lock, lf),
				    ("two overlapping locks from same owner"));
		}
	}
	LIST_FOREACH(lock, &state->ls_pending, lf_link) {
		KASSERT(!LIST_EMPTY(&lock->lf_outedges),
		    ("pending lock which should be active"));
	}
#endif
	sx_xunlock(&state->ls_lock);

	VI_LOCK(vp);

	state->ls_threads--;
	if (LIST_EMPTY(&state->ls_active) && state->ls_threads == 0) {
		KASSERT(LIST_EMPTY(&state->ls_pending),
		    ("freeable state with pending locks"));
	} else {
		wakeup(state);
	}

	VI_UNLOCK(vp);

	if (error == EDOOFUS) {
		KASSERT(ap->a_op == F_SETLK, ("EDOOFUS"));
		goto retry_setlock;
	}
	return (error);
}

int
lf_advlock(struct vop_advlock_args *ap, struct lockf **statep, u_quad_t size)
{
	struct vop_advlockasync_args a;

	a.a_vp = ap->a_vp;
	a.a_id = ap->a_id;
	a.a_op = ap->a_op;
	a.a_fl = ap->a_fl;
	a.a_flags = ap->a_flags;
	a.a_task = NULL;
	a.a_cookiep = NULL;

	return (lf_advlockasync(&a, statep, size));
}

void
lf_purgelocks(struct vnode *vp, struct lockf **statep)
{
	struct lockf *state;
	struct lockf_entry *lock, *nlock;

	/*
	 * For this to work correctly, the caller must ensure that no
	 * other threads enter the locking system for this vnode,
	 * e.g. by checking VI_DOOMED. We wake up any threads that are
	 * sleeping waiting for locks on this vnode and then free all
	 * the remaining locks.
	 */
	VI_LOCK(vp);
	KASSERT(vp->v_iflag & VI_DOOMED,
	    ("lf_purgelocks: vp %p has not vgone yet", vp));
	state = *statep;
	if (state == NULL) {
		VI_UNLOCK(vp);
		return;
	}
	*statep = NULL;
	if (LIST_EMPTY(&state->ls_active) && state->ls_threads == 0) {
		KASSERT(LIST_EMPTY(&state->ls_pending),
		    ("freeing state with pending locks"));
		VI_UNLOCK(vp);
		goto out_free;
	}
	state->ls_threads++;
	VI_UNLOCK(vp);

	sx_xlock(&state->ls_lock);
	sx_xlock(&lf_owner_graph_lock);
	LIST_FOREACH_SAFE(lock, &state->ls_pending, lf_link, nlock) {
		LIST_REMOVE(lock, lf_link);
		lf_remove_outgoing(lock);
		lf_remove_incoming(lock);

		/*
		 * If its an async lock, we can just free it
		 * here, otherwise we let the sleeping thread
		 * free it.
		 */
		if (lock->lf_async_task) {
			lf_free_lock(lock);
		} else {
			lock->lf_flags |= F_INTR;
			wakeup(lock);
		}
	}
	sx_xunlock(&lf_owner_graph_lock);
	sx_xunlock(&state->ls_lock);

	/*
	 * Wait for all other threads, sleeping and otherwise
	 * to leave.
	 */
	VI_LOCK(vp);
	while (state->ls_threads > 1)
		msleep(state, VI_MTX(vp), 0, "purgelocks", 0);
	VI_UNLOCK(vp);

	/*
	 * We can just free all the active locks since they
	 * will have no dependencies (we removed them all
	 * above). We don't need to bother locking since we
	 * are the last thread using this state structure.
	 */
	KASSERT(LIST_EMPTY(&state->ls_pending),
	    ("lock pending for %p", state));
	LIST_FOREACH_SAFE(lock, &state->ls_active, lf_link, nlock) {
		LIST_REMOVE(lock, lf_link);
		lf_free_lock(lock);
	}
out_free:
	sx_xlock(&lf_lock_states_lock);
	LIST_REMOVE(state, ls_link);
	sx_xunlock(&lf_lock_states_lock);
	sx_destroy(&state->ls_lock);
	free(state, M_LOCKF);
}

/*
 * Return non-zero if locks 'x' and 'y' overlap.
		当 x 和 y 有重叠部分的时候，要返回非零值
 */
static int
lf_overlaps(struct lockf_entry *x, struct lockf_entry *y)
{

	return (x->lf_start <= y->lf_end && x->lf_end >= y->lf_start);
}

/*
 * Return non-zero if lock 'x' is blocked by lock 'y' (or vice versa).
		如果锁 x 被锁 y 阻塞了，那么就要返回非零值
 */
static int
lf_blocks(struct lockf_entry *x, struct lockf_entry *y)
{
	/*
		同时满足下面三个条件时，我们可以认定 x 是被 y 阻塞了：
			- 两者的 lockf_owner 不一样
			- x 和 y 其中有一个锁类型是写锁
			- x 和 y 的加锁区域有重叠部分
	*/
	return x->lf_owner != y->lf_owner
		&& (x->lf_type == F_WRLCK || y->lf_type == F_WRLCK)
		&& lf_overlaps(x, y);
}

/*
 * Allocate a lock edge from the free list
 */
static struct lockf_edge *
lf_alloc_edge(void)
{

	return (malloc(sizeof(struct lockf_edge), M_LOCKF, M_WAITOK|M_ZERO));
}

/*
 * Free a lock edge.
 */
static void
lf_free_edge(struct lockf_edge *e)
{

	free(e, M_LOCKF);
}


/*
 * Ensure that the lock's owner has a corresponding vertex in the
 * owner graph.
 */
static void
lf_alloc_vertex(struct lockf_entry *lock)
{
	struct owner_graph *g = &lf_owner_graph;

	if (!lock->lf_owner->lo_vertex)
		lock->lf_owner->lo_vertex =
			graph_alloc_vertex(g, lock->lf_owner);
}

/*
 * Attempt to record an edge from lock x to lock y. Return EDEADLK if
 * the new edge would cause a cycle in the owner graph.
 */
static int
lf_add_edge(struct lockf_entry *x, struct lockf_entry *y)
{
	struct owner_graph *g = &lf_owner_graph;
	struct lockf_edge *e;
	int error;

#ifdef DIAGNOSTIC
	LIST_FOREACH(e, &x->lf_outedges, le_outlink)
		KASSERT(e->le_to != y, ("adding lock edge twice"));
#endif

	/*
	 * Make sure the two owners have entries in the owner graph.
	 */
	lf_alloc_vertex(x);
	lf_alloc_vertex(y);

	error = graph_add_edge(g, x->lf_owner->lo_vertex,
	    y->lf_owner->lo_vertex);
	if (error)
		return (error);

	e = lf_alloc_edge();
	LIST_INSERT_HEAD(&x->lf_outedges, e, le_outlink);
	LIST_INSERT_HEAD(&y->lf_inedges, e, le_inlink);
	e->le_from = x;
	e->le_to = y;

	return (0);
}

/*
 * Remove an edge from the lock graph.
 */
static void
lf_remove_edge(struct lockf_edge *e)
{
	struct owner_graph *g = &lf_owner_graph;
	struct lockf_entry *x = e->le_from;
	struct lockf_entry *y = e->le_to;

	graph_remove_edge(g, x->lf_owner->lo_vertex, y->lf_owner->lo_vertex);
	LIST_REMOVE(e, le_outlink);
	LIST_REMOVE(e, le_inlink);
	e->le_from = NULL;
	e->le_to = NULL;
	lf_free_edge(e);
}

/*
 * Remove all out-going edges from lock x.
 */
static void
lf_remove_outgoing(struct lockf_entry *x)
{
	struct lockf_edge *e;

	while ((e = LIST_FIRST(&x->lf_outedges)) != NULL) {
		lf_remove_edge(e);
	}
}

/*
 * Remove all in-coming edges from lock x.
 */
static void
lf_remove_incoming(struct lockf_entry *x)
{
	struct lockf_edge *e;

	while ((e = LIST_FIRST(&x->lf_inedges)) != NULL) {
		lf_remove_edge(e);
	}
}

/*
 * Walk the list of locks for the file and create an out-going edge
 * from lock to each blocking lock.
 */
static int
lf_add_outgoing(struct lockf *state, struct lockf_entry *lock)
{
	struct lockf_entry *overlap;
	int error;

	LIST_FOREACH(overlap, &state->ls_active, lf_link) {
		/*
		 * We may assume that the active list is sorted by
		 * lf_start.
		 */
		if (overlap->lf_start > lock->lf_end)
			break;
		if (!lf_blocks(lock, overlap))
			continue;

		/*
		 * We've found a blocking lock. Add the corresponding
		 * edge to the graphs and see if it would cause a
		 * deadlock.
		 */
		error = lf_add_edge(lock, overlap);

		/*
		 * The only error that lf_add_edge returns is EDEADLK.
		 * Remove any edges we added and return the error.
		 */
		if (error) {
			lf_remove_outgoing(lock);
			return (error);
		}
	}

	/*
	 * We also need to add edges to sleeping locks that block
	 * us. This ensures that lf_wakeup_lock cannot grant two
	 * mutually blocking locks simultaneously and also enforces a
	 * 'first come, first served' fairness model. Note that this
	 * only happens if we are blocked by at least one active lock
	 * due to the call to lf_getblock in lf_setlock below.
	 */
	LIST_FOREACH(overlap, &state->ls_pending, lf_link) {
		if (!lf_blocks(lock, overlap))
			continue;
		/*
		 * We've found a blocking lock. Add the corresponding
		 * edge to the graphs and see if it would cause a
		 * deadlock.
		 */
		error = lf_add_edge(lock, overlap);

		/*
		 * The only error that lf_add_edge returns is EDEADLK.
		 * Remove any edges we added and return the error.
		 */
		if (error) {
			lf_remove_outgoing(lock);
			return (error);
		}
	}

	return (0);
}

/*
 * Walk the list of pending locks for the file and create an in-coming
 * edge from lock to each blocking lock.
 */
static int
lf_add_incoming(struct lockf *state, struct lockf_entry *lock)
{
	struct lockf_entry *overlap;
	int error;

	sx_assert(&state->ls_lock, SX_XLOCKED);
	if (LIST_EMPTY(&state->ls_pending))
		return (0);

	error = 0;
	sx_xlock(&lf_owner_graph_lock);
	LIST_FOREACH(overlap, &state->ls_pending, lf_link) {
		if (!lf_blocks(lock, overlap))
			continue;

		/*
		 * We've found a blocking lock. Add the corresponding
		 * edge to the graphs and see if it would cause a
		 * deadlock.
		 */
		error = lf_add_edge(overlap, lock);

		/*
		 * The only error that lf_add_edge returns is EDEADLK.
		 * Remove any edges we added and return the error.
		 */
		if (error) {
			lf_remove_incoming(lock);
			break;
		}
	}
	sx_xunlock(&lf_owner_graph_lock);
	return (error);
}

/*
 * Insert lock into the active list, keeping list entries ordered by
 * increasing values of lf_start.
 */
static void
lf_insert_lock(struct lockf *state, struct lockf_entry *lock)
{
	struct lockf_entry *lf, *lfprev;

	if (LIST_EMPTY(&state->ls_active)) {
		LIST_INSERT_HEAD(&state->ls_active, lock, lf_link);
		return;
	}

	// 需要比较 lf_start 字段的大小判断插入的位置，应该是从小到大排列
	lfprev = NULL;
	LIST_FOREACH(lf, &state->ls_active, lf_link) {
		if (lf->lf_start > lock->lf_start) {
			LIST_INSERT_BEFORE(lf, lock, lf_link);
			return;
		}
		lfprev = lf;
	}
	LIST_INSERT_AFTER(lfprev, lock, lf_link);
}

/*
 * Wake up a sleeping lock and remove it from the pending list now
 * that all its dependencies have been resolved. The caller should
 * arrange for the lock to be added to the active list, adjusting any
 * existing locks for the same owner as needed.
 */
static void
lf_wakeup_lock(struct lockf *state, struct lockf_entry *wakelock)
{

	/*
	 * Remove from ls_pending list and wake up the caller
	 * or start the async notification, as appropriate.
	 */
	LIST_REMOVE(wakelock, lf_link);
#ifdef LOCKF_DEBUG
	if (lockf_debug & 1)
		lf_print("lf_wakeup_lock: awakening", wakelock);
#endif /* LOCKF_DEBUG */
	if (wakelock->lf_async_task) {
		taskqueue_enqueue(taskqueue_thread, wakelock->lf_async_task);
	} else {
		wakeup(wakelock);
	}
}

/*
 * Re-check all dependent locks and remove edges to locks that we no
 * longer block. If 'all' is non-zero, the lock has been removed and
 * we must remove all the dependencies, otherwise it has simply been
 * reduced but remains active. Any pending locks which have been been
 * unblocked are added to 'granted'
 * 
 * 从代码实现来看，应该是更新一下图结构相关字段，保证死锁不会发生
 */
static void
lf_update_dependancies(struct lockf *state, struct lockf_entry *lock, int all,
	struct lockf_entry_list *granted)
{
	struct lockf_edge *e, *ne;
	struct lockf_entry *deplock;

	LIST_FOREACH_SAFE(e, &lock->lf_inedges, le_inlink, ne) {
		deplock = e->le_from;
		if (all || !lf_blocks(lock, deplock)) {
			sx_xlock(&lf_owner_graph_lock);
			lf_remove_edge(e);
			sx_xunlock(&lf_owner_graph_lock);
			if (LIST_EMPTY(&deplock->lf_outedges)) {
				lf_wakeup_lock(state, deplock);
				LIST_INSERT_HEAD(granted, deplock, lf_link);
			}
		}
	}
}

/*
 * Set the start of an existing active lock, updating dependencies and
 * adding any newly woken locks to 'granted'.
 * 设置现有活动锁的开始，更新依赖项，并将任何新唤醒的锁添加到 "granted"
 */
static void
lf_set_start(struct lockf *state, struct lockf_entry *lock, off_t new_start,
	struct lockf_entry_list *granted)
{

	KASSERT(new_start >= lock->lf_start, ("can't increase lock"));
	lock->lf_start = new_start;
	LIST_REMOVE(lock, lf_link);
	lf_insert_lock(state, lock);
	lf_update_dependancies(state, lock, FALSE, granted);
}

/*
 * Set the end of an existing active lock, updating dependencies and
 * adding any newly woken locks to 'granted'.
 */
static void
lf_set_end(struct lockf *state, struct lockf_entry *lock, off_t new_end,
	struct lockf_entry_list *granted)
{

	KASSERT(new_end <= lock->lf_end, ("can't increase lock"));
	lock->lf_end = new_end;
	lf_update_dependancies(state, lock, FALSE, granted);
}

/*
 * Add a lock to the active list, updating or removing any current
 * locks owned by the same owner and processing any pending locks that
 * become unblocked as a result. This code is also used for unlock
 * since the logic for updating existing locks is identical.
 * 向活跃锁链表中增加一个锁对象，升级或者移除掉同一个拥有者当前所拥有的任意锁，并且处理
 * 结果会变成 unblocked 状态的任何正在挂起的锁。此代码也用于解锁，因为更新现有锁的逻辑
 * 是相同的
 *
 * As a result of processing the new lock, we may unblock existing
 * pending locks as a result of downgrading/unlocking. We simply
 * activate the newly granted locks by looping.
 * 作为处理新锁的结果，我们可能会由于降级/解锁而取消阻止现有的挂起锁。我们只需通过
 * 循环激活新授予的锁
 *
 * Since the new lock already has its dependencies set up, we always
 * add it to the list (unless its an unlock request). This may
 * fragment the lock list in some pathological cases but its probably
 * not a real problem.
 * 因为新锁已经设置了依赖项，所以我们总是将其添加到列表中（除非是解锁请求）。在某些
 * 病理病例中，这可能会导致锁列表出现碎片，但这可能不是一个真正的问题
 */
static void
lf_activate_lock(struct lockf *state, struct lockf_entry *lock)
{
	struct lockf_entry *overlap, *lf;
	struct lockf_entry_list granted;
	int ovcase;

	// 创建一个授予锁链表，并将 lock 插入
	LIST_INIT(&granted);
	LIST_INSERT_HEAD(&granted, lock, lf_link);

	while (!LIST_EMPTY(&granted)) {
		lock = LIST_FIRST(&granted);
		LIST_REMOVE(lock, lf_link);

		/*
		 * Skip over locks owned by other processes.  Handle
		 * any locks that overlap and are owned by ourselves.
		 * 跳过其他进程拥有的锁。指出任何重叠并且为我们所有的锁(当前进程管理的锁？)
		 * 可以看出，lockf 中不仅仅包括当前进程管理的锁，还包括其他进程管理的锁。
		 * 注意这里处理的是 ls_active 链表中的元素
		 */
		overlap = LIST_FIRST(&state->ls_active);
		for (;;) {
			ovcase = lf_findoverlap(&overlap, lock, SELF);

#ifdef LOCKF_DEBUG
			if (ovcase && (lockf_debug & 2)) {
				printf("lf_setlock: overlap %d", ovcase);
				lf_print("", overlap);
			}
#endif
			/*
			 * Six cases:
			 *	0) no overlap
			 *	1) overlap == lock
			 *	2) overlap contains lock
			 *	3) lock contains overlap
			 *	4) overlap starts before lock
			 *	5) overlap ends after lock
			 */
			switch (ovcase) {
			case 0: /* no overlap */
				break;

			case 1: /* overlap == lock */
				/*
				 * We have already setup the
				 * dependants for the new lock, taking
				 * into account a possible downgrade
				 * or unlock. Remove the old lock.
				 * 考虑到可能的降级或解锁，我们已经为新锁设置了从属项。
				 * 移除旧锁
				 */
				LIST_REMOVE(overlap, lf_link);
				lf_update_dependancies(state, overlap, TRUE,
					&granted);
				lf_free_lock(overlap);
				break;

			case 2: /* overlap contains lock */
				/*
				 * Just split the existing lock.
				 */
				lf_split(state, overlap, lock, &granted);
				break;

			case 3: /* lock contains overlap */
				/*
				 * Delete the overlap and advance to
				 * the next entry in the list.
				 * 删除重叠并前进到列表中的下一个条目
				 */
				lf = LIST_NEXT(overlap, lf_link);
				LIST_REMOVE(overlap, lf_link);
				lf_update_dependancies(state, overlap, TRUE,
					&granted);
				lf_free_lock(overlap);
				overlap = lf;
				continue;

			case 4: /* overlap starts before lock */
				/*
				 * Just update the overlap end and
				 * move on.
				 */
				lf_set_end(state, overlap, lock->lf_start - 1,
				    &granted);
				overlap = LIST_NEXT(overlap, lf_link);
				continue;

			case 5: /* overlap ends after lock */
				/*
				 * Change the start of overlap and
				 * re-insert.
				 */
				lf_set_start(state, overlap, lock->lf_end + 1,
				    &granted);
				break;
			}
			break;
		}
#ifdef LOCKF_DEBUG
		if (lockf_debug & 1) {
			if (lock->lf_type != F_UNLCK)
				lf_print("lf_activate_lock: activated", lock);
			else
				lf_print("lf_activate_lock: unlocked", lock);
			lf_printlist("lf_activate_lock", lock);
		}
#endif /* LOCKF_DEBUG */
		if (lock->lf_type != F_UNLCK)
			lf_insert_lock(state, lock);
	}
}

/*
 * Cancel a pending lock request, either as a result of a signal or a
 * cancel request for an async lock.
 */
static void
lf_cancel_lock(struct lockf *state, struct lockf_entry *lock)
{
	struct lockf_entry_list granted;

	/*
	 * Note it is theoretically possible that cancelling this lock
	 * may allow some other pending lock to become
	 * active. Consider this case:
	 *
	 * Owner	Action		Result		Dependencies
	 * 
	 * A:		lock [0..0]	succeeds	
	 * B:		lock [2..2]	succeeds	
	 * C:		lock [1..2]	blocked		C->B
	 * D:		lock [0..1]	blocked		C->B,D->A,D->C
	 * A:		unlock [0..0]			C->B,D->C
	 * C:		cancel [1..2]	
	 */

	LIST_REMOVE(lock, lf_link);

	/*
	 * Removing out-going edges is simple.
	 */
	sx_xlock(&lf_owner_graph_lock);
	lf_remove_outgoing(lock);
	sx_xunlock(&lf_owner_graph_lock);

	/*
	 * Removing in-coming edges may allow some other lock to
	 * become active - we use lf_update_dependancies to figure
	 * this out.
	 */
	LIST_INIT(&granted);
	lf_update_dependancies(state, lock, TRUE, &granted);
	lf_free_lock(lock);

	/*
	 * Feed any newly active locks to lf_activate_lock.
	 */
	while (!LIST_EMPTY(&granted)) {
		lock = LIST_FIRST(&granted);
		LIST_REMOVE(lock, lf_link);
		lf_activate_lock(state, lock);
	}
}

/*
 * Set a byte-range lock.
 */
static int
lf_setlock(struct lockf *state, struct lockf_entry *lock, struct vnode *vp,
    void **cookiep)
{
	static char lockstr[] = "lockf";
	int error, priority, stops_deferred;

#ifdef LOCKF_DEBUG
	if (lockf_debug & 1)
		lf_print("lf_setlock", lock);
#endif /* LOCKF_DEBUG */

	/*
	 * Set the priority
	 */
	priority = PLOCK;
	/*
		当 lockf 的类型是写锁的时候，优先级+4，是否说明当读锁写锁同时存在的时候，写锁将首先执行？
	*/
	if (lock->lf_type == F_WRLCK)
		priority += 4;
	if (!(lock->lf_flags & F_NOINTR))
		priority |= PCATCH;
	/*
	 * Scan lock list for this file looking for locks that would block us.
	 		扫描此文件的锁列表，查找会阻止我们的锁。也就是说 lock 在 if 代码分支中不会被
			立刻执行，而是会被别的锁给阻塞掉。所以我们要判断 lf_flags 是否满足该使用场景
	 */
	if (lf_getblock(state, lock)) {
		/*
		 * Free the structure and return if nonblocking.
		 		如果 lockf_entry->lf_flags 设置为不支持等待并且没有设置回调 task，
				则返回 try again 错误码
		 */
		if ((lock->lf_flags & F_WAIT) == 0
		    && lock->lf_async_task == NULL) {
			lf_free_lock(lock);
			error = EAGAIN;
			goto out;
		}

		/*
		 * For flock type locks, we must first remove
		 * any shared locks that we hold before we sleep
		 * waiting for an exclusive lock.
		 * 对于 flock 类型，我们必须先移除所有共享的锁，然后再睡觉
		 * 等待独占锁。需要注意的是这里判断了 lockf 是否为一个独占锁
		 */
		if ((lock->lf_flags & F_FLOCK) &&
		    lock->lf_type == F_WRLCK) {
			lock->lf_type = F_UNLCK;
			lf_activate_lock(state, lock);
			lock->lf_type = F_WRLCK;
		}

		/*
		 * We are blocked. Create edges to each blocking lock,
		 * checking for deadlock using the owner graph. For
		 * simplicity, we run deadlock detection for all
		 * locks, posix and otherwise.
		 */
		sx_xlock(&lf_owner_graph_lock);
		error = lf_add_outgoing(state, lock);
		sx_xunlock(&lf_owner_graph_lock);

		if (error) {
#ifdef LOCKF_DEBUG
			if (lockf_debug & 1)
				lf_print("lf_setlock: deadlock", lock);
#endif
			lf_free_lock(lock);
			goto out;
		}

		/*
		 * We have added edges to everything that blocks
		 * us. Sleep until they all go away.
		 * 我们要为所有阻塞我们的东西增加边。一直睡眠到它们都走了
		 */
		LIST_INSERT_HEAD(&state->ls_pending, lock, lf_link);
#ifdef LOCKF_DEBUG
		if (lockf_debug & 1) {
			struct lockf_edge *e;
			LIST_FOREACH(e, &lock->lf_outedges, le_outlink) {
				lf_print("lf_setlock: blocking on", e->le_to);
				lf_printlist("lf_setlock", e->le_to);
			}
		}
#endif /* LOCKF_DEBUG */

		if ((lock->lf_flags & F_WAIT) == 0) {
			/*
			 * The caller requested async notification -
			 * this callback happens when the blocking
			 * lock is released, allowing the caller to
			 * make another attempt to take the lock.
			 * 调用者请求的异步通知——此回调在释放阻塞锁时发生，
			 * 允许调用者再次尝试获取锁
			 */
			*cookiep = (void *) lock;
			error = EINPROGRESS;
			goto out;
		}

		lock->lf_refs++;
		stops_deferred = sigdeferstop(SIGDEFERSTOP_ERESTART);
		error = sx_sleep(lock, &state->ls_lock, priority, lockstr, 0);
		sigallowstop(stops_deferred);
		if (lf_free_lock(lock)) {
			error = EDOOFUS;
			goto out;
		}

		/*
		 * We may have been awakened by a signal and/or by a
		 * debugger continuing us (in which cases we must
		 * remove our lock graph edges) and/or by another
		 * process releasing a lock (in which case our edges
		 * have already been removed and we have been moved to
		 * the active list). We may also have been woken by
		 * lf_purgelocks which we report to the caller as
		 * EINTR. In that case, lf_purgelocks will have
		 * removed our lock graph edges.
		 *
		 * Note that it is possible to receive a signal after
		 * we were successfully woken (and moved to the active
		 * list) but before we resumed execution. In this
		 * case, our lf_outedges list will be clear. We
		 * pretend there was no error.
		 *
		 * Note also, if we have been sleeping long enough, we
		 * may now have incoming edges from some newer lock
		 * which is waiting behind us in the queue.
		 */
		if (lock->lf_flags & F_INTR) {
			error = EINTR;
			lf_free_lock(lock);
			goto out;
		}
		if (LIST_EMPTY(&lock->lf_outedges)) {
			error = 0;
		} else {
			lf_cancel_lock(state, lock);
			goto out;
		}
#ifdef LOCKF_DEBUG
		if (lockf_debug & 1) {
			lf_print("lf_setlock: granted", lock);
		}
#endif
		goto out;
	}
	/*
	 * It looks like we are going to grant the lock. First add
	 * edges from any currently pending lock that the new lock
	 * would block.
	 */
	error = lf_add_incoming(state, lock);
	if (error) {
#ifdef LOCKF_DEBUG
		if (lockf_debug & 1)
			lf_print("lf_setlock: deadlock", lock);
#endif
		lf_free_lock(lock);
		goto out;
	}

	/*
	 * No blocks!!  Add the lock.  Note that we will
	 * downgrade or upgrade any overlapping locks this
	 * process already owns.
	 */
	lf_activate_lock(state, lock);
	error = 0;
out:
	return (error);
}

/*
 * Remove a byte-range lock on an inode.
 *
 * Generally, find the lock (or an overlap to that lock)
 * and remove it (or shrink it), then wakeup anyone we can.
 */
static int
lf_clearlock(struct lockf *state, struct lockf_entry *unlock)
{
	struct lockf_entry *overlap;

	overlap = LIST_FIRST(&state->ls_active);

	if (overlap == NOLOCKF)
		return (0);
#ifdef LOCKF_DEBUG
	if (unlock->lf_type != F_UNLCK)
		panic("lf_clearlock: bad type");
	if (lockf_debug & 1)
		lf_print("lf_clearlock", unlock);
#endif /* LOCKF_DEBUG */

	lf_activate_lock(state, unlock);

	return (0);
}

/*
 * Check whether there is a blocking lock, and if so return its
 * details in '*fl'.
 */
static int
lf_getlock(struct lockf *state, struct lockf_entry *lock, struct flock *fl)
{
	struct lockf_entry *block;

#ifdef LOCKF_DEBUG
	if (lockf_debug & 1)
		lf_print("lf_getlock", lock);
#endif /* LOCKF_DEBUG */

	if ((block = lf_getblock(state, lock))) {
		fl->l_type = block->lf_type;
		fl->l_whence = SEEK_SET;
		fl->l_start = block->lf_start;
		if (block->lf_end == OFF_MAX)
			fl->l_len = 0;
		else
			fl->l_len = block->lf_end - block->lf_start + 1;
		fl->l_pid = block->lf_owner->lo_pid;
		fl->l_sysid = block->lf_owner->lo_sysid;
	} else {
		fl->l_type = F_UNLCK;
	}
	return (0);
}

/*
 * Cancel an async lock request.
 */
static int
lf_cancel(struct lockf *state, struct lockf_entry *lock, void *cookie)
{
	struct lockf_entry *reallock;

	/*
	 * We need to match this request with an existing lock
	 * request.
	 */
	LIST_FOREACH(reallock, &state->ls_pending, lf_link) {
		if ((void *) reallock == cookie) {
			/*
			 * Double-check that this lock looks right
			 * (maybe use a rolling ID for the cancel
			 * cookie instead?)
			 */
			if (!(reallock->lf_vnode == lock->lf_vnode
				&& reallock->lf_start == lock->lf_start
				&& reallock->lf_end == lock->lf_end)) {
				return (ENOENT);
			}

			/*
			 * Make sure this lock was async and then just
			 * remove it from its wait lists.
			 */
			if (!reallock->lf_async_task) {
				return (ENOENT);
			}

			/*
			 * Note that since any other thread must take
			 * state->ls_lock before it can possibly
			 * trigger the async callback, we are safe
			 * from a race with lf_wakeup_lock, i.e. we
			 * can free the lock (actually our caller does
			 * this).
			 */
			lf_cancel_lock(state, reallock);
			return (0);
		}
	}

	/*
	 * We didn't find a matching lock - not much we can do here.
	 */
	return (ENOENT);
}

/*
 * Walk the list of locks for an inode and
 * return the first blocking lock.
 * 浏览 inode 的锁列表并返回第一个阻塞锁。注意这里的表述是 inode，而不是 vnode
 */
static struct lockf_entry *
lf_getblock(struct lockf *state, struct lockf_entry *lock)
{
	struct lockf_entry *overlap;

	LIST_FOREACH(overlap, &state->ls_active, lf_link) {
		/*
		 * We may assume that the active list is sorted by
		 * lf_start.
		 * 从代码实现来看，ls_active 是按照从小到达的方式进行排列的。函数中局部变量的命名页很有讲究，
		 * overlap 意思就是重叠。也就是说，一个锁会不会阻塞另外一个锁，其中一个判断标准就是两个锁管理
		 * 的数据区会不会存在重叠。
		 * 此处逻辑可能是首先判断第一个元素跟给定 entry 是否存在重叠。如果直接 overlap->lf_start > 
		 * lock->lf_end，那就说明 lock 所管理的区域比第一个区域还要靠前，所以后面 entry 管理的区域
		 * 跟 lock 更不可能会有重叠部分，所以直接返回(不判断是读锁还是写锁)？
		 * 如果不是的，再执行 lf_blocks 执行更进一步的判定。
		 * 
		 */
		if (overlap->lf_start > lock->lf_end)
			break;
		if (!lf_blocks(lock, overlap))
			continue;
		return (overlap);
	}
	return (NOLOCKF);
}

/*
 * Walk the list of locks for an inode to find an overlapping lock (if
 * any) and return a classification of that overlap.
 * 浏览inode的锁列表，找到重叠的锁（如果有），并返回该重叠的分类
 *
 * Arguments:
 *	*overlap	The place in the lock list to start looking
 *	lock		The lock which is being tested
 *	type		Pass 'SELF' to test only locks with the same
 *			owner as lock, or 'OTHER' to test only locks
 *			with a different owner
 *
 * Returns one of six values:
 *	0) no overlap
 *	1) overlap == lock
 *	2) overlap contains lock
 *	3) lock contains overlap
 *	4) overlap starts before lock
 *	5) overlap ends after lock
 *
 * If there is an overlapping lock, '*overlap' is set to point at the
 * overlapping lock.
 *
 * NOTE: this returns only the FIRST overlapping lock.  There
 *	 may be more than one.
		链表中可能存在多个 overlap，该函数貌似只返回第一个
 */
static int
lf_findoverlap(struct lockf_entry **overlap, struct lockf_entry *lock, int type)
{
	struct lockf_entry *lf;
	off_t start, end;
	int res;

	if ((*overlap) == NOLOCKF) {
		return (0);
	}
#ifdef LOCKF_DEBUG
	if (lockf_debug & 2)
		lf_print("lf_findoverlap: looking for overlap in", lock);
#endif /* LOCKF_DEBUG */
	start = lock->lf_start;
	end = lock->lf_end;
	res = 0;
	/*
		while 循环刚好对应注释中列出的6种情况
	*/
	while (*overlap) {
		lf = *overlap;
		if (lf->lf_start > end)
			break;
		if (((type & SELF) && lf->lf_owner != lock->lf_owner) ||
		    ((type & OTHERS) && lf->lf_owner == lock->lf_owner)) {
			*overlap = LIST_NEXT(lf, lf_link);
			continue;
		}
#ifdef LOCKF_DEBUG
		if (lockf_debug & 2)
			lf_print("\tchecking", lf);
#endif /* LOCKF_DEBUG */
		/*
		 * OK, check for overlap
		 *
		 * Six cases:
		 *	0) no overlap
		 *	1) overlap == lock
		 *	2) overlap contains lock
		 *	3) lock contains overlap
		 *	4) overlap starts before lock
		 *	5) overlap ends after lock
		 */
		if (start > lf->lf_end) {
			/* Case 0 */
#ifdef LOCKF_DEBUG
			if (lockf_debug & 2)
				printf("no overlap\n");
#endif /* LOCKF_DEBUG */
			*overlap = LIST_NEXT(lf, lf_link);
			continue;
		}
		if (lf->lf_start == start && lf->lf_end == end) {
			/* Case 1 */
#ifdef LOCKF_DEBUG
			if (lockf_debug & 2)
				printf("overlap == lock\n");
#endif /* LOCKF_DEBUG */
			res = 1;
			break;
		}
		if (lf->lf_start <= start && lf->lf_end >= end) {
			/* Case 2 */
#ifdef LOCKF_DEBUG
			if (lockf_debug & 2)
				printf("overlap contains lock\n");
#endif /* LOCKF_DEBUG */
			res = 2;
			break;
		}
		if (start <= lf->lf_start && end >= lf->lf_end) {
			/* Case 3 */
#ifdef LOCKF_DEBUG
			if (lockf_debug & 2)
				printf("lock contains overlap\n");
#endif /* LOCKF_DEBUG */
			res = 3;
			break;
		}
		if (lf->lf_start < start && lf->lf_end >= start) {
			/* Case 4 */
#ifdef LOCKF_DEBUG
			if (lockf_debug & 2)
				printf("overlap starts before lock\n");
#endif /* LOCKF_DEBUG */
			res = 4;
			break;
		}
		if (lf->lf_start > start && lf->lf_end > end) {
			/* Case 5 */
#ifdef LOCKF_DEBUG
			if (lockf_debug & 2)
				printf("overlap ends after lock\n");
#endif /* LOCKF_DEBUG */
			res = 5;
			break;
		}
		panic("lf_findoverlap: default");
	}
	return (res);
}

/*
 * Split an the existing 'lock1', based on the extent of the lock
 * described by 'lock2'. The existing lock should cover 'lock2'
 * entirely.
 * 根据“lock2”描述的锁的范围，拆分现有的“lock1”。现有锁应完全覆盖“锁2”
 *
 * Any pending locks which have been been unblocked are added to
 * 'granted'
 */
static void
lf_split(struct lockf *state, struct lockf_entry *lock1,
    struct lockf_entry *lock2, struct lockf_entry_list *granted)
{
	struct lockf_entry *splitlock;

#ifdef LOCKF_DEBUG
	if (lockf_debug & 2) {
		lf_print("lf_split", lock1);
		lf_print("splitting from", lock2);
	}
#endif /* LOCKF_DEBUG */
	/*
	 * Check to see if we don't need to split at all.
	 */
	if (lock1->lf_start == lock2->lf_start) {
		lf_set_start(state, lock1, lock2->lf_end + 1, granted);
		return;
	}
	if (lock1->lf_end == lock2->lf_end) {
		lf_set_end(state, lock1, lock2->lf_start - 1, granted);
		return;
	}
	/*
	 * Make a new lock consisting of the last part of
	 * the encompassing lock.
	 */
	splitlock = lf_alloc_lock(lock1->lf_owner);
	memcpy(splitlock, lock1, sizeof *splitlock);
	splitlock->lf_refs = 1;
	if (splitlock->lf_flags & F_REMOTE)
		vref(splitlock->lf_vnode);

	/*
	 * This cannot cause a deadlock since any edges we would add
	 * to splitlock already exist in lock1. We must be sure to add
	 * necessary dependencies to splitlock before we reduce lock1
	 * otherwise we may accidentally grant a pending lock that
	 * was blocked by the tail end of lock1.
	 */
	splitlock->lf_start = lock2->lf_end + 1;
	LIST_INIT(&splitlock->lf_outedges);
	LIST_INIT(&splitlock->lf_inedges);
	lf_add_incoming(state, splitlock);

	lf_set_end(state, lock1, lock2->lf_start - 1, granted);

	/*
	 * OK, now link it in
	 */
	lf_insert_lock(state, splitlock);
}

struct lockdesc {
	STAILQ_ENTRY(lockdesc) link;
	struct vnode *vp;
	struct flock fl;
};
STAILQ_HEAD(lockdesclist, lockdesc);

int
lf_iteratelocks_sysid(int sysid, lf_iterator *fn, void *arg)
{
	struct lockf *ls;
	struct lockf_entry *lf;
	struct lockdesc *ldesc;
	struct lockdesclist locks;
	int error;

	/*
	 * In order to keep the locking simple, we iterate over the
	 * active lock lists to build a list of locks that need
	 * releasing. We then call the iterator for each one in turn.
	 *
	 * We take an extra reference to the vnode for the duration to
	 * make sure it doesn't go away before we are finished.
	 */
	STAILQ_INIT(&locks);
	sx_xlock(&lf_lock_states_lock);
	LIST_FOREACH(ls, &lf_lock_states, ls_link) {
		sx_xlock(&ls->ls_lock);
		LIST_FOREACH(lf, &ls->ls_active, lf_link) {
			if (lf->lf_owner->lo_sysid != sysid)
				continue;

			ldesc = malloc(sizeof(struct lockdesc), M_LOCKF,
			    M_WAITOK);
			ldesc->vp = lf->lf_vnode;
			vref(ldesc->vp);
			ldesc->fl.l_start = lf->lf_start;
			if (lf->lf_end == OFF_MAX)
				ldesc->fl.l_len = 0;
			else
				ldesc->fl.l_len =
					lf->lf_end - lf->lf_start + 1;
			ldesc->fl.l_whence = SEEK_SET;
			ldesc->fl.l_type = F_UNLCK;
			ldesc->fl.l_pid = lf->lf_owner->lo_pid;
			ldesc->fl.l_sysid = sysid;
			STAILQ_INSERT_TAIL(&locks, ldesc, link);
		}
		sx_xunlock(&ls->ls_lock);
	}
	sx_xunlock(&lf_lock_states_lock);

	/*
	 * Call the iterator function for each lock in turn. If the
	 * iterator returns an error code, just free the rest of the
	 * lockdesc structures.
	 */
	error = 0;
	while ((ldesc = STAILQ_FIRST(&locks)) != NULL) {
		STAILQ_REMOVE_HEAD(&locks, link);
		if (!error)
			error = fn(ldesc->vp, &ldesc->fl, arg);
		vrele(ldesc->vp);
		free(ldesc, M_LOCKF);
	}

	return (error);
}

int
lf_iteratelocks_vnode(struct vnode *vp, lf_iterator *fn, void *arg)
{
	struct lockf *ls;
	struct lockf_entry *lf;
	struct lockdesc *ldesc;
	struct lockdesclist locks;
	int error;

	/*
	 * In order to keep the locking simple, we iterate over the
	 * active lock lists to build a list of locks that need
	 * releasing. We then call the iterator for each one in turn.
	 *
	 * We take an extra reference to the vnode for the duration to
	 * make sure it doesn't go away before we are finished.
	 */
	STAILQ_INIT(&locks);
	VI_LOCK(vp);
	ls = vp->v_lockf;
	if (!ls) {
		VI_UNLOCK(vp);
		return (0);
	}
	ls->ls_threads++;
	VI_UNLOCK(vp);

	sx_xlock(&ls->ls_lock);
	LIST_FOREACH(lf, &ls->ls_active, lf_link) {
		ldesc = malloc(sizeof(struct lockdesc), M_LOCKF,
		    M_WAITOK);
		ldesc->vp = lf->lf_vnode;
		vref(ldesc->vp);
		ldesc->fl.l_start = lf->lf_start;
		if (lf->lf_end == OFF_MAX)
			ldesc->fl.l_len = 0;
		else
			ldesc->fl.l_len =
				lf->lf_end - lf->lf_start + 1;
		ldesc->fl.l_whence = SEEK_SET;
		ldesc->fl.l_type = F_UNLCK;
		ldesc->fl.l_pid = lf->lf_owner->lo_pid;
		ldesc->fl.l_sysid = lf->lf_owner->lo_sysid;
		STAILQ_INSERT_TAIL(&locks, ldesc, link);
	}
	sx_xunlock(&ls->ls_lock);
	VI_LOCK(vp);
	ls->ls_threads--;
	wakeup(ls);
	VI_UNLOCK(vp);

	/*
	 * Call the iterator function for each lock in turn. If the
	 * iterator returns an error code, just free the rest of the
	 * lockdesc structures.
	 */
	error = 0;
	while ((ldesc = STAILQ_FIRST(&locks)) != NULL) {
		STAILQ_REMOVE_HEAD(&locks, link);
		if (!error)
			error = fn(ldesc->vp, &ldesc->fl, arg);
		vrele(ldesc->vp);
		free(ldesc, M_LOCKF);
	}

	return (error);
}

static int
lf_clearremotesys_iterator(struct vnode *vp, struct flock *fl, void *arg)
{

	VOP_ADVLOCK(vp, 0, F_UNLCK, fl, F_REMOTE);
	return (0);
}

void
lf_clearremotesys(int sysid)
{

	KASSERT(sysid != 0, ("Can't clear local locks with F_UNLCKSYS"));
	lf_iteratelocks_sysid(sysid, lf_clearremotesys_iterator, NULL);
}

int
lf_countlocks(int sysid)
{
	int i;
	struct lock_owner *lo;
	int count;

	count = 0;
	for (i = 0; i < LOCK_OWNER_HASH_SIZE; i++) {
		sx_xlock(&lf_lock_owners[i].lock);
		LIST_FOREACH(lo, &lf_lock_owners[i].list, lo_link)
			if (lo->lo_sysid == sysid)
				count += lo->lo_refs;
		sx_xunlock(&lf_lock_owners[i].lock);
	}

	return (count);
}

#ifdef LOCKF_DEBUG

/*
 * Return non-zero if y is reachable from x using a brute force
 * search. If reachable and path is non-null, return the route taken
 * in path.
 */
static int
graph_reaches(struct owner_vertex *x, struct owner_vertex *y,
    struct owner_vertex_list *path)
{
	struct owner_edge *e;

	if (x == y) {
		if (path)
			TAILQ_INSERT_HEAD(path, x, v_link);
		return 1;
	}

	LIST_FOREACH(e, &x->v_outedges, e_outlink) {
		if (graph_reaches(e->e_to, y, path)) {
			if (path)
				TAILQ_INSERT_HEAD(path, x, v_link);
			return 1;
		}
	}
	return 0;
}

/*
 * Perform consistency checks on the graph. Make sure the values of
 * v_order are correct. If checkorder is non-zero, check no vertex can
 * reach any other vertex with a smaller order.
 */
static void
graph_check(struct owner_graph *g, int checkorder)
{
	int i, j;

	for (i = 0; i < g->g_size; i++) {
		if (!g->g_vertices[i]->v_owner)
			continue;
		KASSERT(g->g_vertices[i]->v_order == i,
		    ("lock graph vertices disordered"));
		if (checkorder) {
			for (j = 0; j < i; j++) {
				if (!g->g_vertices[j]->v_owner)
					continue;
				KASSERT(!graph_reaches(g->g_vertices[i],
					g->g_vertices[j], NULL),
				    ("lock graph vertices disordered"));
			}
		}
	}
}

static void
graph_print_vertices(struct owner_vertex_list *set)
{
	struct owner_vertex *v;

	printf("{ ");
	TAILQ_FOREACH(v, set, v_link) {
		printf("%d:", v->v_order);
		lf_print_owner(v->v_owner);
		if (TAILQ_NEXT(v, v_link))
			printf(", ");
	}
	printf(" }\n");
}

#endif

/*
 * Calculate the sub-set of vertices v from the affected region [y..x]
 * where v is reachable from y. Return -1 if a loop was detected
 * (i.e. x is reachable from y, otherwise the number of vertices in
 * this subset.
 */
static int
graph_delta_forward(struct owner_graph *g, struct owner_vertex *x,
    struct owner_vertex *y, struct owner_vertex_list *delta)
{
	uint32_t gen;
	struct owner_vertex *v;
	struct owner_edge *e;
	int n;

	/*
	 * We start with a set containing just y. Then for each vertex
	 * v in the set so far unprocessed, we add each vertex that v
	 * has an out-edge to and that is within the affected region
	 * [y..x]. If we see the vertex x on our travels, stop
	 * immediately.
	 */
	TAILQ_INIT(delta);
	TAILQ_INSERT_TAIL(delta, y, v_link);
	v = y;
	n = 1;
	gen = g->g_gen;
	while (v) {
		LIST_FOREACH(e, &v->v_outedges, e_outlink) {
			if (e->e_to == x)
				return -1;
			if (e->e_to->v_order < x->v_order
			    && e->e_to->v_gen != gen) {
				e->e_to->v_gen = gen;
				TAILQ_INSERT_TAIL(delta, e->e_to, v_link);
				n++;
			}
		}
		v = TAILQ_NEXT(v, v_link);
	}

	return (n);
}

/*
 * Calculate the sub-set of vertices v from the affected region [y..x]
 * where v reaches x. Return the number of vertices in this subset.
 */
static int
graph_delta_backward(struct owner_graph *g, struct owner_vertex *x,
    struct owner_vertex *y, struct owner_vertex_list *delta)
{
	uint32_t gen;
	struct owner_vertex *v;
	struct owner_edge *e;
	int n;

	/*
	 * We start with a set containing just x. Then for each vertex
	 * v in the set so far unprocessed, we add each vertex that v
	 * has an in-edge from and that is within the affected region
	 * [y..x].
	 */
	TAILQ_INIT(delta);
	TAILQ_INSERT_TAIL(delta, x, v_link);
	v = x;
	n = 1;
	gen = g->g_gen;
	while (v) {
		LIST_FOREACH(e, &v->v_inedges, e_inlink) {
			if (e->e_from->v_order > y->v_order
			    && e->e_from->v_gen != gen) {
				e->e_from->v_gen = gen;
				TAILQ_INSERT_HEAD(delta, e->e_from, v_link);
				n++;
			}
		}
		v = TAILQ_PREV(v, owner_vertex_list, v_link);
	}

	return (n);
}

static int
graph_add_indices(int *indices, int n, struct owner_vertex_list *set)
{
	struct owner_vertex *v;
	int i, j;

	TAILQ_FOREACH(v, set, v_link) {
		for (i = n;
		     i > 0 && indices[i - 1] > v->v_order; i--)
			;
		for (j = n - 1; j >= i; j--)
			indices[j + 1] = indices[j];
		indices[i] = v->v_order;
		n++;
	}

	return (n);
}

static int
graph_assign_indices(struct owner_graph *g, int *indices, int nextunused,
    struct owner_vertex_list *set)
{
	struct owner_vertex *v, *vlowest;

	while (!TAILQ_EMPTY(set)) {
		vlowest = NULL;
		TAILQ_FOREACH(v, set, v_link) {
			if (!vlowest || v->v_order < vlowest->v_order)
				vlowest = v;
		}
		TAILQ_REMOVE(set, vlowest, v_link);
		vlowest->v_order = indices[nextunused];
		g->g_vertices[vlowest->v_order] = vlowest;
		nextunused++;
	}

	return (nextunused);
}

static int
graph_add_edge(struct owner_graph *g, struct owner_vertex *x,
    struct owner_vertex *y)
{
	struct owner_edge *e;
	struct owner_vertex_list deltaF, deltaB;
	int nF, n, vi, i;
	int *indices;
	int nB __unused;

	sx_assert(&lf_owner_graph_lock, SX_XLOCKED);

	LIST_FOREACH(e, &x->v_outedges, e_outlink) {
		if (e->e_to == y) {
			e->e_refs++;
			return (0);
		}
	}

#ifdef LOCKF_DEBUG
	if (lockf_debug & 8) {
		printf("adding edge %d:", x->v_order);
		lf_print_owner(x->v_owner);
		printf(" -> %d:", y->v_order);
		lf_print_owner(y->v_owner);
		printf("\n");
	}
#endif
	if (y->v_order < x->v_order) {
		/*
		 * The new edge violates the order. First find the set
		 * of affected vertices reachable from y (deltaF) and
		 * the set of affect vertices affected that reach x
		 * (deltaB), using the graph generation number to
		 * detect whether we have visited a given vertex
		 * already. We re-order the graph so that each vertex
		 * in deltaB appears before each vertex in deltaF.
		 *
		 * If x is a member of deltaF, then the new edge would
		 * create a cycle. Otherwise, we may assume that
		 * deltaF and deltaB are disjoint.
		 */
		g->g_gen++;
		if (g->g_gen == 0) {
			/*
			 * Generation wrap.
			 */
			for (vi = 0; vi < g->g_size; vi++) {
				g->g_vertices[vi]->v_gen = 0;
			}
			g->g_gen++;
		}
		nF = graph_delta_forward(g, x, y, &deltaF);
		if (nF < 0) {
#ifdef LOCKF_DEBUG
			if (lockf_debug & 8) {
				struct owner_vertex_list path;
				printf("deadlock: ");
				TAILQ_INIT(&path);
				graph_reaches(y, x, &path);
				graph_print_vertices(&path);
			}
#endif
			return (EDEADLK);
		}

#ifdef LOCKF_DEBUG
		if (lockf_debug & 8) {
			printf("re-ordering graph vertices\n");
			printf("deltaF = ");
			graph_print_vertices(&deltaF);
		}
#endif

		nB = graph_delta_backward(g, x, y, &deltaB);

#ifdef LOCKF_DEBUG
		if (lockf_debug & 8) {
			printf("deltaB = ");
			graph_print_vertices(&deltaB);
		}
#endif

		/*
		 * We first build a set of vertex indices (vertex
		 * order values) that we may use, then we re-assign
		 * orders first to those vertices in deltaB, then to
		 * deltaF. Note that the contents of deltaF and deltaB
		 * may be partially disordered - we perform an
		 * insertion sort while building our index set.
		 */
		indices = g->g_indexbuf;
		n = graph_add_indices(indices, 0, &deltaF);
		graph_add_indices(indices, n, &deltaB);

		/*
		 * We must also be sure to maintain the relative
		 * ordering of deltaF and deltaB when re-assigning
		 * vertices. We do this by iteratively removing the
		 * lowest ordered element from the set and assigning
		 * it the next value from our new ordering.
		 */
		i = graph_assign_indices(g, indices, 0, &deltaB);
		graph_assign_indices(g, indices, i, &deltaF);

#ifdef LOCKF_DEBUG
		if (lockf_debug & 8) {
			struct owner_vertex_list set;
			TAILQ_INIT(&set);
			for (i = 0; i < nB + nF; i++)
				TAILQ_INSERT_TAIL(&set,
				    g->g_vertices[indices[i]], v_link);
			printf("new ordering = ");
			graph_print_vertices(&set);
		}
#endif
	}

	KASSERT(x->v_order < y->v_order, ("Failed to re-order graph"));

#ifdef LOCKF_DEBUG
	if (lockf_debug & 8) {
		graph_check(g, TRUE);
	}
#endif

	e = malloc(sizeof(struct owner_edge), M_LOCKF, M_WAITOK);

	LIST_INSERT_HEAD(&x->v_outedges, e, e_outlink);
	LIST_INSERT_HEAD(&y->v_inedges, e, e_inlink);
	e->e_refs = 1;
	e->e_from = x;
	e->e_to = y;

	return (0);
}

/*
 * Remove an edge x->y from the graph.
 */
static void
graph_remove_edge(struct owner_graph *g, struct owner_vertex *x,
    struct owner_vertex *y)
{
	struct owner_edge *e;

	sx_assert(&lf_owner_graph_lock, SX_XLOCKED);

	LIST_FOREACH(e, &x->v_outedges, e_outlink) {
		if (e->e_to == y)
			break;
	}
	KASSERT(e, ("Removing non-existent edge from deadlock graph"));

	e->e_refs--;
	if (e->e_refs == 0) {
#ifdef LOCKF_DEBUG
		if (lockf_debug & 8) {
			printf("removing edge %d:", x->v_order);
			lf_print_owner(x->v_owner);
			printf(" -> %d:", y->v_order);
			lf_print_owner(y->v_owner);
			printf("\n");
		}
#endif
		LIST_REMOVE(e, e_outlink);
		LIST_REMOVE(e, e_inlink);
		free(e, M_LOCKF);
	}
}

/*
 * Allocate a vertex from the free list. Return ENOMEM if there are
 * none.
 */
static struct owner_vertex *
graph_alloc_vertex(struct owner_graph *g, struct lock_owner *lo)
{
	struct owner_vertex *v;

	sx_assert(&lf_owner_graph_lock, SX_XLOCKED);

	v = malloc(sizeof(struct owner_vertex), M_LOCKF, M_WAITOK);
	if (g->g_size == g->g_space) {
		g->g_vertices = realloc(g->g_vertices,
		    2 * g->g_space * sizeof(struct owner_vertex *),
		    M_LOCKF, M_WAITOK);
		free(g->g_indexbuf, M_LOCKF);
		g->g_indexbuf = malloc(2 * g->g_space * sizeof(int),
		    M_LOCKF, M_WAITOK);
		g->g_space = 2 * g->g_space;
	}
	v->v_order = g->g_size;
	v->v_gen = g->g_gen;
	g->g_vertices[g->g_size] = v;
	g->g_size++;

	LIST_INIT(&v->v_outedges);
	LIST_INIT(&v->v_inedges);
	v->v_owner = lo;

	return (v);
}

static void
graph_free_vertex(struct owner_graph *g, struct owner_vertex *v)
{
	struct owner_vertex *w;
	int i;

	sx_assert(&lf_owner_graph_lock, SX_XLOCKED);
	
	KASSERT(LIST_EMPTY(&v->v_outedges), ("Freeing vertex with edges"));
	KASSERT(LIST_EMPTY(&v->v_inedges), ("Freeing vertex with edges"));

	/*
	 * Remove from the graph's array and close up the gap,
	 * renumbering the other vertices.
	 */
	for (i = v->v_order + 1; i < g->g_size; i++) {
		w = g->g_vertices[i];
		w->v_order--;
		g->g_vertices[i - 1] = w;
	}
	g->g_size--;

	free(v, M_LOCKF);
}

static struct owner_graph *
graph_init(struct owner_graph *g)
{

	g->g_vertices = malloc(10 * sizeof(struct owner_vertex *),
	    M_LOCKF, M_WAITOK);
	g->g_size = 0;
	g->g_space = 10;
	g->g_indexbuf = malloc(g->g_space * sizeof(int), M_LOCKF, M_WAITOK);
	g->g_gen = 0;

	return (g);
}

#ifdef LOCKF_DEBUG
/*
 * Print description of a lock owner
 */
static void
lf_print_owner(struct lock_owner *lo)
{

	if (lo->lo_flags & F_REMOTE) {
		printf("remote pid %d, system %d",
		    lo->lo_pid, lo->lo_sysid);
	} else if (lo->lo_flags & F_FLOCK) {
		printf("file %p", lo->lo_id);
	} else {
		printf("local pid %d", lo->lo_pid);
	}
}

/*
 * Print out a lock.
 */
static void
lf_print(char *tag, struct lockf_entry *lock)
{

	printf("%s: lock %p for ", tag, (void *)lock);
	lf_print_owner(lock->lf_owner);
	if (lock->lf_inode != (struct inode *)0)
		printf(" in ino %ju on dev <%s>,",
		    (uintmax_t)lock->lf_inode->i_number,
		    devtoname(ITODEV(lock->lf_inode)));
	printf(" %s, start %jd, end ",
	    lock->lf_type == F_RDLCK ? "shared" :
	    lock->lf_type == F_WRLCK ? "exclusive" :
	    lock->lf_type == F_UNLCK ? "unlock" : "unknown",
	    (intmax_t)lock->lf_start);
	if (lock->lf_end == OFF_MAX)
		printf("EOF");
	else
		printf("%jd", (intmax_t)lock->lf_end);
	if (!LIST_EMPTY(&lock->lf_outedges))
		printf(" block %p\n",
		    (void *)LIST_FIRST(&lock->lf_outedges)->le_to);
	else
		printf("\n");
}

static void
lf_printlist(char *tag, struct lockf_entry *lock)
{
	struct lockf_entry *lf, *blk;
	struct lockf_edge *e;

	if (lock->lf_inode == (struct inode *)0)
		return;

	printf("%s: Lock list for ino %ju on dev <%s>:\n",
	    tag, (uintmax_t)lock->lf_inode->i_number,
	    devtoname(ITODEV(lock->lf_inode)));
	LIST_FOREACH(lf, &lock->lf_vnode->v_lockf->ls_active, lf_link) {
		printf("\tlock %p for ",(void *)lf);
		lf_print_owner(lock->lf_owner);
		printf(", %s, start %jd, end %jd",
		    lf->lf_type == F_RDLCK ? "shared" :
		    lf->lf_type == F_WRLCK ? "exclusive" :
		    lf->lf_type == F_UNLCK ? "unlock" :
		    "unknown", (intmax_t)lf->lf_start, (intmax_t)lf->lf_end);
		LIST_FOREACH(e, &lf->lf_outedges, le_outlink) {
			blk = e->le_to;
			printf("\n\t\tlock request %p for ", (void *)blk);
			lf_print_owner(blk->lf_owner);
			printf(", %s, start %jd, end %jd",
			    blk->lf_type == F_RDLCK ? "shared" :
			    blk->lf_type == F_WRLCK ? "exclusive" :
			    blk->lf_type == F_UNLCK ? "unlock" :
			    "unknown", (intmax_t)blk->lf_start,
			    (intmax_t)blk->lf_end);
			if (!LIST_EMPTY(&blk->lf_inedges))
				panic("lf_printlist: bad list");
		}
		printf("\n");
	}
}
#endif /* LOCKF_DEBUG */
