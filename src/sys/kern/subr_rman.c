/*-
 * Copyright 1998 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * The kernel resource manager.  This code is responsible for keeping track
 * of hardware resources which are apportioned out to various drivers.
 * It does not actually assign those resources, and it is not expected
 * that end-device drivers will call into this code directly.  Rather,
 * the code which implements the buses that those devices are attached to,
 * and the code which manages CPU resources, will call this code, and the
 * end-device drivers will make upcalls to that code to actually perform
 * the allocation.
 *
 * There are two sorts of resources managed by this code.  The first is
 * the more familiar array (RMAN_ARRAY) type; resources in this class
 * consist of a sequence of individually-allocatable objects which have
 * been numbered in some well-defined order.  Most of the resources
 * are of this type, as it is the most familiar.  The second type is
 * called a gauge (RMAN_GAUGE), and models fungible resources (i.e.,
 * resources in which each instance is indistinguishable from every
 * other instance).  The principal anticipated application of gauges
 * is in the context of power consumption, where a bus may have a specific
 * power budget which all attached devices share.  RMAN_GAUGE is not
 * implemented yet.
 *
 * For array resources, we make one simplifying assumption: two clients
 * sharing the same resource must use the same range of indices.  That
 * is to say, sharing of overlapping-but-not-identical regions is not
 * permitted.
 */

#include "opt_ddb.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: releng/12.0/sys/kern/subr_rman.c 300317 2016-05-20 17:57:47Z jhb $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/bus.h>		/* XXX debugging */
#include <machine/bus.h>
#include <sys/rman.h>
#include <sys/sysctl.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

/*
 * We use a linked list rather than a bitmap because we need to be able to
 * represent potentially huge objects (like all of a processor's physical
 * address space).  That is also why the indices are defined to have type
 * `unsigned long' -- that being the largest integral type in ISO C (1990).
 * The 1999 version of C allows `long long'; we may need to switch to that
 * at some point in the future, particularly if we want to support 36-bit
 * addresses on IA32 hardware.
 */
struct resource_i {
	/*
		指明隶属于那个resource结构体，resource可以看做时候对resource_i的扩充，增加了
		tag和handle属性
	*/
	struct resource		r_r;
	TAILQ_ENTRY(resource_i)	r_link;
	LIST_ENTRY(resource_i)	r_sharelink;
	LIST_HEAD(, resource_i)	*r_sharehead;
	rman_res_t	r_start;	/* index of the first entry in this resource */
	rman_res_t	r_end;		/* index of the last entry (inclusive) */
	u_int	r_flags;
	void	*r_virtual;	/* virtual address of this resource */
	device_t r_dev;	/* device which has allocated this resource */
	struct rman *r_rm;	/* resource manager from whence this came */
	int	r_rid;		/* optional rid for this resource. */
};

static int rman_debug = 0;
SYSCTL_INT(_debug, OID_AUTO, rman_debug, CTLFLAG_RWTUN,
    &rman_debug, 0, "rman debug");

#define DPRINTF(params) if (rman_debug) printf params

static MALLOC_DEFINE(M_RMAN, "rman", "Resource manager");

/* rman_head 是一个rman类型的队列 */
struct rman_head rman_head;
static struct mtx rman_mtx; /* mutex to protect rman_head */
static int int_rman_release_resource(struct rman *rm, struct resource_i *r);

static __inline struct resource_i *
int_alloc_resource(int malloc_flag)
{
	struct resource_i *r;

	r = malloc(sizeof *r, M_RMAN, malloc_flag | M_ZERO);
	if (r != NULL) {
		r->r_r.__r_i = r;
	}
	return (r);
}

int
rman_init(struct rman *rm)
{
	static int once = 0;

	/* 保证一些init操作只执行一次 */
	if (once == 0) {
		once = 1;
		TAILQ_INIT(&rman_head);

		/* 专门用来保护rman_head的一个锁，这里对其进行初始化操作 */
		mtx_init(&rman_mtx, "rman head", NULL, MTX_DEF);
	}

	if (rm->rm_start == 0 && rm->rm_end == 0)
		rm->rm_end = ~0;
	if (rm->rm_type == RMAN_UNINIT)
		panic("rman_init");
	if (rm->rm_type == RMAN_GAUGE)
		panic("implement RMAN_GAUGE");

	TAILQ_INIT(&rm->rm_list);
	rm->rm_mtx = malloc(sizeof *rm->rm_mtx, M_RMAN, M_NOWAIT | M_ZERO);
	if (rm->rm_mtx == NULL)
		return ENOMEM;
	mtx_init(rm->rm_mtx, "rman", NULL, MTX_DEF);

	mtx_lock(&rman_mtx);
	/* 专门用来管理resource manager的一个双端链表，操作的时候需要加锁 */
	TAILQ_INSERT_TAIL(&rman_head, rm, rm_link);
	mtx_unlock(&rman_mtx);
	return 0;
}

/*
	该函数应该是对所划分的资源区域的管理，rm是表示的是resource manager，可能
	是memory，也可能是interrupt。函数中包含一个将resource_i结构体添加到rm list
	中的操作，所以这个函数的会扩大rm管理的资源的范围。如果是向刚刚初始化的总线添加，
	猜测可能就是对该总线的某种资源管理区域进行初始化设置（因为这个时候rm list中应该
	还是没有添加过元素的？），如果list中已经有了元素，这个时候就要判断新增加的资源项与
	原有的资源项的区域是否有重叠？
*/
int
rman_manage_region(struct rman *rm, rman_res_t start, rman_res_t end)
{
	struct resource_i *r, *s, *t;
	int rv = 0;

	DPRINTF(("rman_manage_region: <%s> request: start %#jx, end %#jx\n",
	    rm->rm_descr, start, end));
	
	/*
		首先要来判断我们传入的start和end值是否符合要求，如果我们传入的start的值比
		rm给定的start还要小，或者end比rm给定的end还要大，那就说明我们的参数已经超
		过了rm所能管理的范围，所以这个是不能接受的
	*/
	if (start < rm->rm_start || end > rm->rm_end)
		return EINVAL;

	/* 如果传入的区域大小参数符合要求，那么就分配一块resource_i大小的区域 */
	r = int_alloc_resource(M_NOWAIT);
	if (r == NULL)
		return ENOMEM;
	r->r_start = start;
	r->r_end = end;
	r->r_rm = rm;

	mtx_lock(rm->rm_mtx);

	/* Skip entries before us. */
	TAILQ_FOREACH(s, &rm->rm_list, r_link) {
		/*
			查找end为~0，这种情况貌似要特殊处理，根据set_resource的值来进行判断，
			end = start + count,其中start和count用set的值，end通过这两个值进行计算
			推断: set函数要在该函数之前执行
		*/ 
		if (s->r_end == ~0)
			break;

		/*
			通过下面的逻辑我们可以看到，会存在resource list中有的资源在总线管理的范围之外，
			或者分配的资源是空的？？
			因为r是我们所要分配的资源，如果s -> end + 1 >= r -> r_start，说明r所需要的资源肯定
			是要在s的后边，从这里我们可以推断，rm中resource list它的资源也是按照他们定义的范围往后
			延续的，这样的做的好处就是便于对资源进行管理
		*/
		if (s->r_end + 1 >= r->r_start)
			break;
	}

	/* If we ran off the end of the list, insert at the tail. */
	if (s == NULL) {
		// 貌似是向原来的resource list中添加资源项
		TAILQ_INSERT_TAIL(&rm->rm_list, r, r_link);
	} else {
		/* 
			Check for any overlap with the current region. 
			检查是否与当前的区域重叠，意思就是rm list中已经有元素占用了该区域，
			所以新增加的区域如果跟这部分区域有重叠，就会报冲突
		*/
		if (r->r_start <= s->r_end && r->r_end >= s->r_start) {
			rv = EBUSY;
			goto out;
		}

		/* Check for any overlap with the next region. */
		t = TAILQ_NEXT(s, r_link);
		if (t && r->r_start <= t->r_end && r->r_end >= t->r_start) {
			rv = EBUSY;
			goto out;
		}

		/*
		 * See if this region can be merged with the next region.  If
		 * not, clear the pointer.
		 */
		if (t && (r->r_end + 1 != t->r_start || t->r_flags != 0))
			t = NULL;

		/* 
			See if we can merge with the current region.
			如果前一个resource的end等于后一个resource的start，那么就将两个resource
			合并到一起；如果后一个resource的end又等于后后一个resource的start，那么就
			将着3个resource合并到一起，以此类推，所有的首位地址相连的resource都可以合并
			到一起
		*/
		if (s->r_end + 1 == r->r_start && s->r_flags == 0) {
			/* Can we merge all 3 regions? */
			if (t != NULL) {
				s->r_end = t->r_end;
				TAILQ_REMOVE(&rm->rm_list, t, r_link);
				free(r, M_RMAN);
				free(t, M_RMAN);
			} else {
				s->r_end = r->r_end;
				free(r, M_RMAN);
			}
		} else if (t != NULL) {
			/* Can we merge with just the next region? */
			t->r_start = r->r_start;
			free(r, M_RMAN);
		} else if (s->r_end < r->r_start) {
			TAILQ_INSERT_AFTER(&rm->rm_list, s, r, r_link);
		} else {
			TAILQ_INSERT_BEFORE(s, r, r_link);
		}
	}
out:
	mtx_unlock(rm->rm_mtx);
	return rv;
}

/*
	通过manual page上的解释我们可以看到，该函数是添加region，这里就可以推测:
	nexus中调用这个函数可能就是给bus设置一个IO和中断的region，以后凡是涉及到
	io或者中断操作的都是在这个区域中进行操作，后边如果有新的设备添加，可能会有新的
	region引入，这时候就再次调用这个函数，就可以把新的资源region添加到已有的region
	当中
	从nexus源代码中的逻辑可以看到，调用region函数的时候已经把最大的地址传入，所以
	以后还会有别的资源添加，还有地址空间可以使用吗？
*/
int
rman_init_from_resource(struct rman *rm, struct resource *r)
{
	int rv;

	if ((rv = rman_init(rm)) != 0)
		return (rv);
	return (rman_manage_region(rm, r->__r_i->r_start, r->__r_i->r_end));
}

int
rman_fini(struct rman *rm)
{
	struct resource_i *r;

	mtx_lock(rm->rm_mtx);
	TAILQ_FOREACH(r, &rm->rm_list, r_link) {
		if (r->r_flags & RF_ALLOCATED) {
			mtx_unlock(rm->rm_mtx);
			return EBUSY;
		}
	}

	/*
	 * There really should only be one of these if we are in this
	 * state and the code is working properly, but it can't hurt.
	 */
	while (!TAILQ_EMPTY(&rm->rm_list)) {
		r = TAILQ_FIRST(&rm->rm_list);
		TAILQ_REMOVE(&rm->rm_list, r, r_link);
		free(r, M_RMAN);
	}
	mtx_unlock(rm->rm_mtx);
	mtx_lock(&rman_mtx);
	TAILQ_REMOVE(&rman_head, rm, rm_link);
	mtx_unlock(&rman_mtx);
	mtx_destroy(rm->rm_mtx);
	free(rm->rm_mtx, M_RMAN);

	return 0;
}

int
rman_first_free_region(struct rman *rm, rman_res_t *start, rman_res_t *end)
{
	struct resource_i *r;

	mtx_lock(rm->rm_mtx);
	TAILQ_FOREACH(r, &rm->rm_list, r_link) {
		if (!(r->r_flags & RF_ALLOCATED)) {
			*start = r->r_start;
			*end = r->r_end;
			mtx_unlock(rm->rm_mtx);
			return (0);
		}
	}
	mtx_unlock(rm->rm_mtx);
	return (ENOENT);
}

int
rman_last_free_region(struct rman *rm, rman_res_t *start, rman_res_t *end)
{
	struct resource_i *r;

	mtx_lock(rm->rm_mtx);
	TAILQ_FOREACH_REVERSE(r, &rm->rm_list, resource_head, r_link) {
		if (!(r->r_flags & RF_ALLOCATED)) {
			*start = r->r_start;
			*end = r->r_end;
			mtx_unlock(rm->rm_mtx);
			return (0);
		}
	}
	mtx_unlock(rm->rm_mtx);
	return (ENOENT);
}

/* Shrink or extend one or both ends of an allocated resource. */
int
rman_adjust_resource(struct resource *rr, rman_res_t start, rman_res_t end)
{
	struct resource_i *r, *s, *t, *new;
	struct rman *rm;

	/* Not supported for shared resources. */
	r = rr->__r_i;
	if (r->r_flags & RF_SHAREABLE)
		return (EINVAL);

	/*
	 * This does not support wholesale moving of a resource.  At
	 * least part of the desired new range must overlap with the
	 * existing resource.
	 */
	if (end < r->r_start || r->r_end < start)
		return (EINVAL);

	/*
	 * Find the two resource regions immediately adjacent to the
	 * allocated resource.
	 */
	rm = r->r_rm;
	mtx_lock(rm->rm_mtx);
#ifdef INVARIANTS
	TAILQ_FOREACH(s, &rm->rm_list, r_link) {
		if (s == r)
			break;
	}
	if (s == NULL)
		panic("resource not in list");
#endif
	s = TAILQ_PREV(r, resource_head, r_link);
	t = TAILQ_NEXT(r, r_link);
	KASSERT(s == NULL || s->r_end + 1 == r->r_start,
	    ("prev resource mismatch"));
	KASSERT(t == NULL || r->r_end + 1 == t->r_start,
	    ("next resource mismatch"));

	/*
	 * See if the changes are permitted.  Shrinking is always allowed,
	 * but growing requires sufficient room in the adjacent region.
	 */
	if (start < r->r_start && (s == NULL || (s->r_flags & RF_ALLOCATED) ||
	    s->r_start > start)) {
		mtx_unlock(rm->rm_mtx);
		return (EBUSY);
	}
	if (end > r->r_end && (t == NULL || (t->r_flags & RF_ALLOCATED) ||
	    t->r_end < end)) {
		mtx_unlock(rm->rm_mtx);
		return (EBUSY);
	}

	/*
	 * While holding the lock, grow either end of the resource as
	 * needed and shrink either end if the shrinking does not require
	 * allocating a new resource.  We can safely drop the lock and then
	 * insert a new range to handle the shrinking case afterwards.
	 */
	if (start < r->r_start ||
	    (start > r->r_start && s != NULL && !(s->r_flags & RF_ALLOCATED))) {
		KASSERT(s->r_flags == 0, ("prev is busy"));
		r->r_start = start;
		if (s->r_start == start) {
			TAILQ_REMOVE(&rm->rm_list, s, r_link);
			free(s, M_RMAN);
		} else
			s->r_end = start - 1;
	}
	if (end > r->r_end ||
	    (end < r->r_end && t != NULL && !(t->r_flags & RF_ALLOCATED))) {
		KASSERT(t->r_flags == 0, ("next is busy"));
		r->r_end = end;
		if (t->r_end == end) {
			TAILQ_REMOVE(&rm->rm_list, t, r_link);
			free(t, M_RMAN);
		} else
			t->r_start = end + 1;
	}
	mtx_unlock(rm->rm_mtx);

	/*
	 * Handle the shrinking cases that require allocating a new
	 * resource to hold the newly-free region.  We have to recheck
	 * if we still need this new region after acquiring the lock.
	 */
	if (start > r->r_start) {
		new = int_alloc_resource(M_WAITOK);
		new->r_start = r->r_start;
		new->r_end = start - 1;
		new->r_rm = rm;
		mtx_lock(rm->rm_mtx);
		r->r_start = start;
		s = TAILQ_PREV(r, resource_head, r_link);
		if (s != NULL && !(s->r_flags & RF_ALLOCATED)) {
			s->r_end = start - 1;
			free(new, M_RMAN);
		} else
			TAILQ_INSERT_BEFORE(r, new, r_link);
		mtx_unlock(rm->rm_mtx);
	}
	if (end < r->r_end) {
		new = int_alloc_resource(M_WAITOK);
		new->r_start = end + 1;
		new->r_end = r->r_end;
		new->r_rm = rm;
		mtx_lock(rm->rm_mtx);
		r->r_end = end;
		t = TAILQ_NEXT(r, r_link);
		if (t != NULL && !(t->r_flags & RF_ALLOCATED)) {
			t->r_start = end + 1;
			free(new, M_RMAN);
		} else
			TAILQ_INSERT_AFTER(&rm->rm_list, r, new, r_link);
		mtx_unlock(rm->rm_mtx);
	}
	return (0);
}

#define	SHARE_TYPE(f)	(f & (RF_SHAREABLE | RF_PREFETCHABLE))

/* 
	该函数是rman的逻辑主体，它尝试在rm管理的区域中找到一块连续的区域供device使用，
	需要满足 start + count - 1 <= end，flag表示数据对齐的要求，bound表示对边界
	的限制，通常情况下设置为0，表示对于边界没有任何限制。RF_SHAREABLE如果被设置的话
	就会分配一个共享区域，否则分配一个独占的区域
	end值表示resource所能接受的最高的ending value，意思可能是resource的end最大不能
	超过指定的值，所以只需要保证计算出来的end值小于指定的值就可以；start表示resource所能
	接受的最小的值，也就是bus分配给resource的start是可以大于这个指定的start的值的。从这
	可以看出，resource的分配其实是一个动态变化的过程，前后都有可以调整的裕度值，这个函数所
	要做的工作也就是从rm管理的资源项中找到一个符合要求的，又满足一定限制条件的资源项分配给
	device
	如果共享段已经存在，那总线就会将设备添加到consumers当中
*/
struct resource *
rman_reserve_resource_bound(struct rman *rm, rman_res_t start, rman_res_t end,
			    rman_res_t count, rman_res_t bound, u_int flags,
			    device_t dev)
{
	u_int new_rflags;
	struct resource_i *r, *s, *rv;
	rman_res_t rstart, rend, amask, bmask; 	/* amask：align mask； bmask: bound mask */

	rv = NULL;

	DPRINTF(("rman_reserve_resource_bound: <%s> request: [%#jx, %#jx], "
	       "length %#jx, flags %x, device %s\n", rm->rm_descr, start, end,
	       count, flags,
	       dev == NULL ? "<null>" : device_get_nameunit(dev)));
	KASSERT((flags & RF_FIRSTSHARE) == 0,
	    ("invalid flags %#x", flags));

	/*
		 RF_FIRSTSHARE           0x0020              0000 0000 0010 0000
		~RF_FIRSTSHARE                               1111 1111 1101 1111     
		flag & ~RF_FIRSTSHARE ==>> 把 flag 的第6个bit位置0

		 RF_ALLOCATED			0x0001				 0000 0000 0000 0001
		|RF_ALLOCATED ==>> 把处理后的 flag 的第1个bit位再置1

		所以 new_rflags 的格式为: xxxx xxxx xx0x xxx1								 
	*/
	new_rflags = (flags & ~RF_FIRSTSHARE) | RF_ALLOCATED;

	mtx_lock(rm->rm_mtx);

	/*
		指向resource list的起始位置元素，rm_list管理着resource manager中包含的resource，
		所以其实就是在resource list中找到一个元素，能够满足设备的需要
	*/
	r = TAILQ_FIRST(&rm->rm_list);
	if (r == NULL) {
	    DPRINTF(("NULL list head\n"));
	} else {
	    DPRINTF(("rman_reserve_resource_bound: trying %#jx <%#jx,%#jx>\n",
		    r->r_end, start, count-1));
	}

	/*
		循环条件中可以看出，如果元素的end值小于指定的start + count - 1，需要继续查找，也就是
		这种情况下的resource是不符合要求的，resource的end一定要大于计算值，这样才能把请求的
		资源给包含起来，所以r就表示第一个符合要求的区域
		rm_list是resource_i类型的队列，里边的每一个元素都是对resource manager所管理的每一块资源
		的描述，所以我们可以通过遍历rm_list来查找合适的资源区域
	*/
	for (r = TAILQ_FIRST(&rm->rm_list);
	     r && r->r_end < start + count - 1;
	     r = TAILQ_NEXT(r, r_link)) {
		;
		DPRINTF(("rman_reserve_resource_bound: tried %#jx <%#jx,%#jx>\n",
			r->r_end, start, count-1));
	}

	if (r == NULL) {
		DPRINTF(("could not find a region\n"));
		goto out;
	}

	/*
		flags = xxxx xxxx xxxx xxxx 
		RF_ALIGNMENT(flags)  ->  (((flags) & RF_ALIGNMENT_MASK) >> RF_ALIGNMENT_SHIFT)
		RF_ALIGNMENT_MASK = 0x003F << RF_ALIGNMENT_SHIFT = 0x003F << 10
			0000 0000 0011 1111 << 10  = 1111 1100 0000 0000  相当于是把flags前10个bit置0
		RF_ALIGNMENT(flags) = xxxx xx00 0000 0000 >> 10 = 0000 0000 00xx xxxx,相当于是取出
		了flags的 bit10-bit15 
	*/
	amask = (1ull << RF_ALIGNMENT(flags)) - 1;
	KASSERT(start <= RM_MAX_END - amask,
	    ("start (%#jx) + amask (%#jx) would wrap around", start, amask));

	/* 
		If bound is 0, bmask will also be 0 
		所有正整数的按位取反是其本身+1的负数，这里是先减去1然后取反，bmask = -bound
		-1取反结果为0
	*/
	bmask = ~(bound - 1);

	/*
	 * First try to find an acceptable totally-unshared region.
	 * 优先查找非共享区域。r目前指向的是第一个满足要求的region，然后又赋值给s，所以目前s也是
	 * 指向该region。后续如果需要继续查找的话，就是通过s指针来进行
	 */
	for (s = r; s; s = TAILQ_NEXT(s, r_link)) {
		DPRINTF(("considering [%#jx, %#jx]\n", s->r_start, s->r_end));
		/*
		 * The resource list is sorted, so there is no point in
		 * searching further once r_start is too large.
		 * 资源列表已经排序，因此一旦r_start过大，就没有不要再查找
		 * 如果s还是表示rm_list中的元素，s -> r_start 如果大于end - (count - 1)，
		 * count一定，那么分配的resource的end肯定是要超过所要求的最大end值的
		 */
		if (s->r_start > end - (count - 1)) {
			DPRINTF(("s->r_start (%#jx) + count - 1> end (%#jx)\n",
			    s->r_start, end));
			break;
		}
		if (s->r_start > RM_MAX_END - amask) {
			DPRINTF(("s->r_start (%#jx) + amask (%#jx) too large\n",
			    s->r_start, amask));
			break;
		}
		/* 如果是共享区域就直接跳过 */
		if (s->r_flags & RF_ALLOCATED) {
			DPRINTF(("region is allocated\n"));
			continue;
		}

		/*
			从r_start和start中选一个比较大的那个值作为resource start的值，说明存在一种情况就是
			我们请求的资源项start < r_start，相当于是已经越界了，所以要关注这种情况下的解决办法
			(前面只是判断了start+count， 并没有对start进行比较)
		*/
		rstart = ummax(s->r_start, start);
		/*
		 * Try to find a region by adjusting to boundary and alignment
		 * until both conditions are staisfied. This is not an optimal
		 * algorithm, but in most cases it isn't really bad, either.
		 * 
		 * 调增rstart，rend的值，使得其满足region边界要求？
		 */
		do {
			rstart = (rstart + amask) & ~amask;
			if (((rstart ^ (rstart + count - 1)) & bmask) != 0)
				rstart += bound - (rstart & ~bmask);
		} while ((rstart & amask) != 0 && rstart < end &&
		    rstart < s->r_end);
		rend = ummin(s->r_end, ummax(rstart + count - 1, end));
		if (rstart > rend) {
			DPRINTF(("adjusted start exceeds end\n"));
			continue;
		}
		DPRINTF(("truncated region: [%#jx, %#jx]; size %#jx (requested %#jx)\n",
		       rstart, rend, (rend - rstart + 1), count));

		if ((rend - rstart + 1) >= count) {
			DPRINTF(("candidate region: [%#jx, %#jx], size %#jx\n",
			       rstart, rend, (rend - rstart + 1)));
			/* 
				如果s刚好能够容纳请求的资源区域，那就直接返回rv(其实就是s)
			*/
			if ((s->r_end - s->r_start + 1) == count) {
				DPRINTF(("candidate region is entire chunk\n"));
				rv = s;
				rv->r_flags = new_rflags;
				rv->r_dev = dev;
				goto out;
			}

			/*
			 * If s->r_start < rstart and
			 *    s->r_end > rstart + count - 1, then
			 * we need to split the region into three pieces
			 * (the middle one will get returned to the user).
			 * Otherwise, we are allocating at either the
			 * beginning or the end of s, so we only need to
			 * split it in two.  The first case requires
			 * two new allocations; the second requires but one.
			 * 也就是说下面的情况不是完全匹配的类型，可能会进一步对原有的region进行拆分
			 */
			rv = int_alloc_resource(M_NOWAIT);		/* 这一阶段之前，rv一直是null，rv指向新分配的region */
			if (rv == NULL)
				goto out;
			rv->r_start = rstart;
			rv->r_end = rstart + count - 1;
			rv->r_flags = new_rflags;
			rv->r_dev = dev;
			rv->r_rm = rm;

			if (s->r_start < rv->r_start && s->r_end > rv->r_end) {
				/* 需要拆分成三块区域的情况 */
				DPRINTF(("splitting region in three parts: "
				       "[%#jx, %#jx]; [%#jx, %#jx]; [%#jx, %#jx]\n",
				       s->r_start, rv->r_start - 1,
				       rv->r_start, rv->r_end,
				       rv->r_end + 1, s->r_end));
				/*
				 * We are allocating in the middle.
				 */
				r = int_alloc_resource(M_NOWAIT);
				if (r == NULL) {
					free(rv, M_RMAN);
					rv = NULL;
					goto out;
				}

				/* 
					r代表拆分后的第3块区域，rv表示中间的区域，s还是表示第1块区域 
				*/
				r->r_start = rv->r_end + 1;
				r->r_end = s->r_end;
				r->r_flags = s->r_flags;
				r->r_rm = rm;
				s->r_end = rv->r_start - 1;
				TAILQ_INSERT_AFTER(&rm->rm_list, s, rv,
						     r_link);
				TAILQ_INSERT_AFTER(&rm->rm_list, rv, r,
						     r_link);
			} else if (s->r_start == rv->r_start) {
				DPRINTF(("allocating from the beginning\n"));
				/*
				 * We are allocating at the beginning.
				 */
				s->r_start = rv->r_end + 1;
				TAILQ_INSERT_BEFORE(s, rv, r_link);
			} else {
				DPRINTF(("allocating at the end\n"));
				/*
				 * We are allocating at the end.
				 */
				s->r_end = rv->r_start - 1;
				TAILQ_INSERT_AFTER(&rm->rm_list, s, rv,
						     r_link);
			}
			goto out;
		}
	}

	/*
	 * Now find an acceptable shared region, if the client's requirements
	 * allow sharing.  By our implementation restriction, a candidate
	 * region must match exactly by both size and sharing type in order
	 * to be considered compatible with the client's request.  (The
	 * former restriction could probably be lifted without too much
	 * additional work, but this does not seem warranted.)
	 * 候选区域一定要满足size和sharing类型这两种属性，才能被认为与客户端的请求兼容
	 */
	DPRINTF(("no unshared regions found\n"));
	if ((flags & RF_SHAREABLE) == 0)
		goto out;

	for (s = r; s && s->r_end <= end; s = TAILQ_NEXT(s, r_link)) {
		if (SHARE_TYPE(s->r_flags) == SHARE_TYPE(flags) &&
		    s->r_start >= start &&
		    (s->r_end - s->r_start + 1) == count &&
		    (s->r_start & amask) == 0 &&
		    ((s->r_start ^ s->r_end) & bmask) == 0) {
			rv = int_alloc_resource(M_NOWAIT);
			if (rv == NULL)
				goto out;
			rv->r_start = s->r_start;
			rv->r_end = s->r_end;
			rv->r_flags = new_rflags;
			rv->r_dev = dev;
			rv->r_rm = rm;
			if (s->r_sharehead == NULL) {
				s->r_sharehead = malloc(sizeof *s->r_sharehead,
						M_RMAN, M_NOWAIT | M_ZERO);
				if (s->r_sharehead == NULL) {
					free(rv, M_RMAN);
					rv = NULL;
					goto out;
				}
				LIST_INIT(s->r_sharehead);
				LIST_INSERT_HEAD(s->r_sharehead, s,
						 r_sharelink);
				s->r_flags |= RF_FIRSTSHARE;
			}
			rv->r_sharehead = s->r_sharehead;
			LIST_INSERT_HEAD(s->r_sharehead, rv, r_sharelink);
			goto out;
		}
	}
	/*
	 * We couldn't find anything.
	 */

out:
	mtx_unlock(rm->rm_mtx);
	return (rv == NULL ? NULL : &rv->r_r);
}

struct resource *
rman_reserve_resource(struct rman *rm, rman_res_t start, rman_res_t end,
		      rman_res_t count, u_int flags, device_t dev)
{

	return (rman_reserve_resource_bound(rm, start, end, count, 0, flags,
	    dev));
}

int
rman_activate_resource(struct resource *re)
{
	struct resource_i *r;
	struct rman *rm;

	r = re->__r_i;
	rm = r->r_rm;
	mtx_lock(rm->rm_mtx);
	r->r_flags |= RF_ACTIVE;
	mtx_unlock(rm->rm_mtx);
	return 0;
}

int
rman_deactivate_resource(struct resource *r)
{
	struct rman *rm;

	rm = r->__r_i->r_rm;
	mtx_lock(rm->rm_mtx);
	r->__r_i->r_flags &= ~RF_ACTIVE;
	mtx_unlock(rm->rm_mtx);
	return 0;
}

static int
int_rman_release_resource(struct rman *rm, struct resource_i *r)
{
	struct resource_i *s, *t;

	if (r->r_flags & RF_ACTIVE)
		r->r_flags &= ~RF_ACTIVE;

	/*
	 * Check for a sharing list first.  If there is one, then we don't
	 * have to think as hard.
	 */
	if (r->r_sharehead) {
		/*
		 * If a sharing list exists, then we know there are at
		 * least two sharers.
		 *
		 * If we are in the main circleq, appoint someone else.
		 */
		LIST_REMOVE(r, r_sharelink);
		s = LIST_FIRST(r->r_sharehead);
		if (r->r_flags & RF_FIRSTSHARE) {
			s->r_flags |= RF_FIRSTSHARE;
			TAILQ_INSERT_BEFORE(r, s, r_link);
			TAILQ_REMOVE(&rm->rm_list, r, r_link);
		}

		/*
		 * Make sure that the sharing list goes away completely
		 * if the resource is no longer being shared at all.
		 */
		if (LIST_NEXT(s, r_sharelink) == NULL) {
			free(s->r_sharehead, M_RMAN);
			s->r_sharehead = NULL;
			s->r_flags &= ~RF_FIRSTSHARE;
		}
		goto out;
	}

	/*
	 * Look at the adjacent resources in the list and see if our
	 * segment can be merged with any of them.  If either of the
	 * resources is allocated or is not exactly adjacent then they
	 * cannot be merged with our segment.
	 */
	s = TAILQ_PREV(r, resource_head, r_link);
	if (s != NULL && ((s->r_flags & RF_ALLOCATED) != 0 ||
	    s->r_end + 1 != r->r_start))
		s = NULL;
	t = TAILQ_NEXT(r, r_link);
	if (t != NULL && ((t->r_flags & RF_ALLOCATED) != 0 ||
	    r->r_end + 1 != t->r_start))
		t = NULL;

	if (s != NULL && t != NULL) {
		/*
		 * Merge all three segments.
		 */
		s->r_end = t->r_end;
		TAILQ_REMOVE(&rm->rm_list, r, r_link);
		TAILQ_REMOVE(&rm->rm_list, t, r_link);
		free(t, M_RMAN);
	} else if (s != NULL) {
		/*
		 * Merge previous segment with ours.
		 */
		s->r_end = r->r_end;
		TAILQ_REMOVE(&rm->rm_list, r, r_link);
	} else if (t != NULL) {
		/*
		 * Merge next segment with ours.
		 */
		t->r_start = r->r_start;
		TAILQ_REMOVE(&rm->rm_list, r, r_link);
	} else {
		/*
		 * At this point, we know there is nothing we
		 * can potentially merge with, because on each
		 * side, there is either nothing there or what is
		 * there is still allocated.  In that case, we don't
		 * want to remove r from the list; we simply want to
		 * change it to an unallocated region and return
		 * without freeing anything.
		 */
		r->r_flags &= ~RF_ALLOCATED;
		r->r_dev = NULL;
		return 0;
	}

out:
	free(r, M_RMAN);
	return 0;
}

int
rman_release_resource(struct resource *re)
{
	int rv;
	struct resource_i *r;
	struct rman *rm;

	r = re->__r_i;
	rm = r->r_rm;
	mtx_lock(rm->rm_mtx);
	rv = int_rman_release_resource(rm, r);
	mtx_unlock(rm->rm_mtx);
	return (rv);
}

uint32_t
rman_make_alignment_flags(uint32_t size)
{
	int i;

	/*
	 * Find the hightest bit set, and add one if more than one bit
	 * set.  We're effectively computing the ceil(log2(size)) here.
	 */
	for (i = 31; i > 0; i--)
		if ((1 << i) & size)
			break;
	if (~(1 << i) & size)
		i++;

	return(RF_ALIGNMENT_LOG2(i));
}

void
rman_set_start(struct resource *r, rman_res_t start)
{

	r->__r_i->r_start = start;
}

rman_res_t
rman_get_start(struct resource *r)
{

	return (r->__r_i->r_start);
}

void
rman_set_end(struct resource *r, rman_res_t end)
{

	r->__r_i->r_end = end;
}

rman_res_t
rman_get_end(struct resource *r)
{

	return (r->__r_i->r_end);
}

rman_res_t
rman_get_size(struct resource *r)
{

	return (r->__r_i->r_end - r->__r_i->r_start + 1);
}

u_int
rman_get_flags(struct resource *r)
{

	return (r->__r_i->r_flags);
}

void
rman_set_virtual(struct resource *r, void *v)
{

	r->__r_i->r_virtual = v;
}

void *
rman_get_virtual(struct resource *r)
{

	return (r->__r_i->r_virtual);
}

void
rman_set_bustag(struct resource *r, bus_space_tag_t t)
{

	r->r_bustag = t;
}

bus_space_tag_t
rman_get_bustag(struct resource *r)
{

	return (r->r_bustag);
}

void
rman_set_bushandle(struct resource *r, bus_space_handle_t h)
{

	r->r_bushandle = h;
}

bus_space_handle_t
rman_get_bushandle(struct resource *r)
{

	return (r->r_bushandle);
}

void
rman_set_mapping(struct resource *r, struct resource_map *map)
{

	KASSERT(rman_get_size(r) == map->r_size,
	    ("rman_set_mapping: size mismatch"));
	rman_set_bustag(r, map->r_bustag);
	rman_set_bushandle(r, map->r_bushandle);
	rman_set_virtual(r, map->r_vaddr);
}

void
rman_get_mapping(struct resource *r, struct resource_map *map)
{

	map->r_bustag = rman_get_bustag(r);
	map->r_bushandle = rman_get_bushandle(r);
	map->r_size = rman_get_size(r);
	map->r_vaddr = rman_get_virtual(r);
}

void
rman_set_rid(struct resource *r, int rid)
{

	r->__r_i->r_rid = rid;
}

int
rman_get_rid(struct resource *r)
{

	return (r->__r_i->r_rid);
}

void
rman_set_device(struct resource *r, device_t dev)
{

	r->__r_i->r_dev = dev;
}

device_t
rman_get_device(struct resource *r)
{

	return (r->__r_i->r_dev);
}

int
rman_is_region_manager(struct resource *r, struct rman *rm)
{

	return (r->__r_i->r_rm == rm);
}

/*
 * Sysctl interface for scanning the resource lists.
 *
 * We take two input parameters; the index into the list of resource
 * managers, and the resource offset into the list.
 */
static int
sysctl_rman(SYSCTL_HANDLER_ARGS)
{
	int			*name = (int *)arg1;
	u_int			namelen = arg2;
	int			rman_idx, res_idx;
	struct rman		*rm;
	struct resource_i	*res;
	struct resource_i	*sres;
	struct u_rman		urm;
	struct u_resource	ures;
	int			error;

	if (namelen != 3)
		return (EINVAL);

	if (bus_data_generation_check(name[0]))
		return (EINVAL);
	rman_idx = name[1];
	res_idx = name[2];

	/*
	 * Find the indexed resource manager
	 */
	mtx_lock(&rman_mtx);
	TAILQ_FOREACH(rm, &rman_head, rm_link) {
		if (rman_idx-- == 0)
			break;
	}
	mtx_unlock(&rman_mtx);
	if (rm == NULL)
		return (ENOENT);

	/*
	 * If the resource index is -1, we want details on the
	 * resource manager.
	 */
	if (res_idx == -1) {
		bzero(&urm, sizeof(urm));
		urm.rm_handle = (uintptr_t)rm;
		if (rm->rm_descr != NULL)
			strlcpy(urm.rm_descr, rm->rm_descr, RM_TEXTLEN);
		urm.rm_start = rm->rm_start;
		urm.rm_size = rm->rm_end - rm->rm_start + 1;
		urm.rm_type = rm->rm_type;

		error = SYSCTL_OUT(req, &urm, sizeof(urm));
		return (error);
	}

	/*
	 * Find the indexed resource and return it.
	 */
	mtx_lock(rm->rm_mtx);
	TAILQ_FOREACH(res, &rm->rm_list, r_link) {
		if (res->r_sharehead != NULL) {
			LIST_FOREACH(sres, res->r_sharehead, r_sharelink)
				if (res_idx-- == 0) {
					res = sres;
					goto found;
				}
		}
		else if (res_idx-- == 0)
				goto found;
	}
	mtx_unlock(rm->rm_mtx);
	return (ENOENT);

found:
	bzero(&ures, sizeof(ures));
	ures.r_handle = (uintptr_t)res;
	ures.r_parent = (uintptr_t)res->r_rm;
	ures.r_device = (uintptr_t)res->r_dev;
	if (res->r_dev != NULL) {
		if (device_get_name(res->r_dev) != NULL) {
			snprintf(ures.r_devname, RM_TEXTLEN,
			    "%s%d",
			    device_get_name(res->r_dev),
			    device_get_unit(res->r_dev));
		} else {
			strlcpy(ures.r_devname, "nomatch",
			    RM_TEXTLEN);
		}
	} else {
		ures.r_devname[0] = '\0';
	}
	ures.r_start = res->r_start;
	ures.r_size = res->r_end - res->r_start + 1;
	ures.r_flags = res->r_flags;

	mtx_unlock(rm->rm_mtx);
	error = SYSCTL_OUT(req, &ures, sizeof(ures));
	return (error);
}

static SYSCTL_NODE(_hw_bus, OID_AUTO, rman, CTLFLAG_RD, sysctl_rman,
    "kernel resource manager");

#ifdef DDB
static void
dump_rman_header(struct rman *rm)
{

	if (db_pager_quit)
		return;
	db_printf("rman %p: %s (0x%jx-0x%jx full range)\n",
	    rm, rm->rm_descr, (rman_res_t)rm->rm_start, (rman_res_t)rm->rm_end);
}

static void
dump_rman(struct rman *rm)
{
	struct resource_i *r;
	const char *devname;

	if (db_pager_quit)
		return;
	TAILQ_FOREACH(r, &rm->rm_list, r_link) {
		if (r->r_dev != NULL) {
			devname = device_get_nameunit(r->r_dev);
			if (devname == NULL)
				devname = "nomatch";
		} else
			devname = NULL;
		db_printf("    0x%jx-0x%jx (RID=%d) ",
		    r->r_start, r->r_end, r->r_rid);
		if (devname != NULL)
			db_printf("(%s)\n", devname);
		else
			db_printf("----\n");
		if (db_pager_quit)
			return;
	}
}

DB_SHOW_COMMAND(rman, db_show_rman)
{

	if (have_addr) {
		dump_rman_header((struct rman *)addr);
		dump_rman((struct rman *)addr);
	}
}

DB_SHOW_COMMAND(rmans, db_show_rmans)
{
	struct rman *rm;

	TAILQ_FOREACH(rm, &rman_head, rm_link) {
		dump_rman_header(rm);
	}
}

DB_SHOW_ALL_COMMAND(rman, db_show_all_rman)
{
	struct rman *rm;

	TAILQ_FOREACH(rm, &rman_head, rm_link) {
		dump_rman_header(rm);
		dump_rman(rm);
	}
}
DB_SHOW_ALIAS(allrman, db_show_all_rman);
#endif
