/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2002 Poul-Henning Kamp
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Poul-Henning Kamp
 * and NAI Labs, the Security Research Division of Network Associates, Inc.
 * under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the
 * DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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

/*
 * XXX: How do we in general know that objects referenced in events
 * have not been destroyed before we get around to handle the event ?
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: releng/12.0/sys/geom/geom_event.c 327430 2017-12-31 09:23:52Z cperciva $");

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <geom/geom.h>
#include <geom/geom_int.h>

#include <machine/stdarg.h>

TAILQ_HEAD(event_tailq_head, g_event);

/* 事件全局队列 */
static struct event_tailq_head g_events = TAILQ_HEAD_INITIALIZER(g_events);
static u_int g_pending_events;	/* pending: 待办事项 */

/* 
	provider 全局队列？
	从下文的逻辑来看，貌似被执行禁用操作的 g_provider 都会被注册到这里
*/
static TAILQ_HEAD(,g_provider) g_doorstep = TAILQ_HEAD_INITIALIZER(g_doorstep);
static struct mtx g_eventlock;	/* 全局事件锁 */
static int g_wither_work;	/* 全局销毁操作标志符 */

#define G_N_EVENTREFS		20

/* 事件管理队列 */
struct g_event {
	TAILQ_ENTRY(g_event)	events;	/* 事件管理队列 */
	g_event_t		*func;	/* 关联的功能函数 */
	void			*arg;
	int			flag;
	void			*ref[G_N_EVENTREFS];	/* 引用数组，应该是要以null作为结束 */
};

#define EV_DONE		0x80000
#define EV_WAKEUP	0x40000
#define EV_CANCELED	0x20000
#define EV_INPROGRESS	0x10000

/*
	貌似在系统出现异常的时候会调用这个函数，估计就是用来提示当前还有一些 GEOM 事件没有被处理掉
*/
void
g_waitidle(void)
{

	g_topology_assert_not();

	mtx_lock(&g_eventlock);
	TSWAIT("GEOM events");	/* 打印log信息使用的宏定义 */
	/* 当 g_events 不为空的时候，说明还有一些没有被处理的事件 */
	while (!TAILQ_EMPTY(&g_events))
		msleep(&g_pending_events, &g_eventlock, PPAUSE,
		    "g_waitidle", hz/5);
	TSUNWAIT("GEOM events");
	mtx_unlock(&g_eventlock);
	curthread->td_pflags &= ~TDP_GEOM;
}

#if 0
void
g_waitidlelock(void)
{

	g_topology_assert();
	mtx_lock(&g_eventlock);
	while (!TAILQ_EMPTY(&g_events)) {
		g_topology_unlock();
		msleep(&g_pending_events, &g_eventlock, PPAUSE,
		    "g_waitidlel", hz/5);
		g_topology_lock();
	}
	mtx_unlock(&g_eventlock);
}
#endif

/* provider 关联属性信息？ */
struct g_attrchanged_args {
	struct g_provider *pp;
	const char *attr;
};

/*
	从后面的代码实现的逻辑来看，这是 provider 属性修改事件的具体的处理函数
*/
static void
g_attr_changed_event(void *arg, int flag)
{
	struct g_attrchanged_args *args;
	struct g_provider *pp;
	struct g_consumer *cp;
	struct g_consumer *next_cp;

	/* 更新提供者的属性信息 */
	args = arg;
	pp = args->pp;

	g_topology_assert();
	if (flag != EV_CANCEL && g_shutdown == 0) {

		/*
		 * Tell all consumers of the change.
		 * 一旦 provider 的属性被修改了，就要通知所有关联的 consumer。每个 consumer 都会对应一个 geom 实例，
		 * 然后再更新 geom 的注册的方法
		 */
		LIST_FOREACH_SAFE(cp, &pp->consumers, consumers, next_cp) {
			if (cp->geom->attrchanged != NULL)
				cp->geom->attrchanged(cp, args->attr);
		}
	}
	g_free(args);	/* 说明 arg 是通过 g_malloc 实例化的 */
}

int
g_attr_changed(struct g_provider *pp, const char *attr, int flag)
{
	struct g_attrchanged_args *args;
	int error;

	args = g_malloc(sizeof *args, flag);	/* 对应上面的函数中的 g_free */
	if (args == NULL)
		return (ENOMEM);
	args->pp = pp;
	args->attr = attr;
	/*
		向全局的事件队列中注册一个关于 provider 属性修改的事件
	*/
	error = g_post_event(g_attr_changed_event, args, flag, pp, NULL);
	if (error != 0)
		g_free(args);
	return (error);
}

/*
	孤立一个 provider。可以理解为我现在把系统中的一个磁盘或者分区 unmount，那 GEOM 就会
	执行孤立函数，那么使用这个磁盘的文件系统(消费者)就会被逐步注销掉。最后整个 provider 也会
	被注销，最终将磁盘从 GEOM 中移除
*/
void
g_orphan_provider(struct g_provider *pp, int error)
{

	/* G_VALID_PROVIDER(pp)  We likely lack topology lock */
	g_trace(G_T_TOPOLOGY, "g_orphan_provider(%p(%s), %d)",
	    pp, pp->name, error);
	KASSERT(error != 0,
	    ("g_orphan_provider(%p(%s), 0) error must be non-zero\n",
	     pp, pp->name));
	
	pp->error = error;
	mtx_lock(&g_eventlock);
	KASSERT(!(pp->flags & G_PF_ORPHAN),
	    ("g_orphan_provider(%p(%s)), already an orphan", pp, pp->name));
	pp->flags |= G_PF_ORPHAN;
	TAILQ_INSERT_TAIL(&g_doorstep, pp, orphan);	/* 将被禁用的 provider 注册到这里 */
	mtx_unlock(&g_eventlock);
	wakeup(&g_wait_event);
}

/*
 * This function is called once on each provider which the event handler
 * finds on its g_doorstep.
 * 对于事件处理程序在其 g_doorstep 上找到的每个提供程序，将调用此函数一次
 * 从代码实现的逻辑来看，就是把 provider 所关联的所有的 consumer 全部销毁，然后再把
 * provider 销毁
 */

static void
g_orphan_register(struct g_provider *pp)
{
	struct g_consumer *cp, *cp2;
	int wf;

	g_topology_assert();
	G_VALID_PROVIDER(pp);
	g_trace(G_T_TOPOLOGY, "g_orphan_register(%s)", pp->name);

	g_cancel_event(pp);

	wf = pp->flags & G_PF_WITHER;
	pp->flags &= ~G_PF_WITHER;

	/*
	 * Tell all consumers the bad news.
	 * Don't be surprised if they self-destruct.
	 * 告诉所有消费者坏消息。如果他们自毁了，不要惊讶
	 */
	LIST_FOREACH_SAFE(cp, &pp->consumers, consumers, cp2) {
		KASSERT(cp->geom->orphan != NULL,
		    ("geom %s has no orphan, class %s",
		    cp->geom->name, cp->geom->class->name));
		/*
		 * XXX: g_dev_orphan method does deferred destroying
		 * and it is possible, that other event could already
		 * call the orphan method. Check consumer's flags to
		 * do not schedule it twice.
		 */
		if (cp->flags & G_CF_ORPHAN)
			continue;
		cp->flags |= G_CF_ORPHAN;
		cp->geom->orphan(cp);
	}
	if (LIST_EMPTY(&pp->consumers) && wf)
		g_destroy_provider(pp);
	else
		pp->flags |= wf;
#ifdef notyet
	cp = LIST_FIRST(&pp->consumers);
	if (cp != NULL)
		return;
	if (pp->geom->flags & G_GEOM_WITHER)
		g_destroy_provider(pp);
#endif
}

static int
one_event(void)
{
	struct g_event *ep;
	struct g_provider *pp;

	g_topology_assert();
	mtx_lock(&g_eventlock);
	TAILQ_FOREACH(pp, &g_doorstep, orphan) {
		/*
			当 nstart == nend 的时候，break；猜测这两个参数应该表示的是 provider 所对应的 I/O request标志，
			当这两个参数相等的时候，说明 I/O 请求已经处理完毕，可以被销毁了
		*/
		if (pp->nstart == pp->nend)
			break;
	}
	if (pp != NULL) {
		G_VALID_PROVIDER(pp);
		/* 从队列中将其移除掉 */
		TAILQ_REMOVE(&g_doorstep, pp, orphan);
		mtx_unlock(&g_eventlock);
		g_orphan_register(pp);	/* 执行清理操作 */
		return (1);
	}

	/*
		如果上面有事情可以做，那就做上面的事情，完成后返回；如果发现上面无事可做，或者说已经处理完毕了，
		那就在事件队列中找一个事件出来进行处理
	*/
	ep = TAILQ_FIRST(&g_events);
	if (ep == NULL) {
		wakeup(&g_pending_events);
		return (0);
	}
	if (ep->flag & EV_INPROGRESS) {
		mtx_unlock(&g_eventlock);
		return (1);
	}
	ep->flag |= EV_INPROGRESS;
	mtx_unlock(&g_eventlock);
	g_topology_assert();
	ep->func(ep->arg, 0);
	g_topology_assert();
	mtx_lock(&g_eventlock);
	TSRELEASE("GEOM events");
	TAILQ_REMOVE(&g_events, ep, events);	/* 从事件队列中移除 */
	ep->flag &= ~EV_INPROGRESS;	/* 标志符设置为正在处理 */
	if (ep->flag & EV_WAKEUP) {
		ep->flag |= EV_DONE;
		mtx_unlock(&g_eventlock);
		wakeup(ep);	/* 唤醒处理这个事件的线程 */
	} else {
		mtx_unlock(&g_eventlock);
		g_free(ep);
	}
	return (1);
}

/*
	在 g_init 函数中会被调用到
*/
void
g_run_events()
{

	for (;;) {
		g_topology_lock();
		/*
			从 one_event 代码逻辑可以看出，当没有事件可以处理的时候，函数才会返回0。这个
			时候函数会跳出循环，将对应的线程睡眠。所以只要有事情可做，这个循环就会一直持续下去，
			直到把所有的事件全部处理完毕
		*/
		while (one_event())
			;
		mtx_assert(&g_eventlock, MA_OWNED);
		if (g_wither_work) {
			g_wither_work = 0;
			mtx_unlock(&g_eventlock);
			g_wither_washer();
			g_topology_unlock();
		} else {
			g_topology_unlock();
			msleep(&g_wait_event, &g_eventlock, PRIBIO | PDROP,
			    "-", TAILQ_EMPTY(&g_doorstep) ? 0 : hz / 10);	/* 线程进入睡眠，等待被唤醒 */
		}
	}
	/* NOTREACHED */
}

/*
	函数的作用是：取消由 ref 标识的所有事件。cancellation 相当于调用请求的函数，
	将请求的参数和参数标志设置为 EV_CANCEL
*/
void
g_cancel_event(void *ref)
{
	struct g_event *ep, *epn;
	struct g_provider *pp;
	u_int n;

	mtx_lock(&g_eventlock);
	TAILQ_FOREACH(pp, &g_doorstep, orphan) {
		if (pp != ref)	/* 处理禁用 provider 的事件 */
			continue;
		TAILQ_REMOVE(&g_doorstep, pp, orphan);
		break;
	}
	TAILQ_FOREACH_SAFE(ep, &g_events, events, epn) {
		if (ep->flag & EV_INPROGRESS)
			continue;
		for (n = 0; n < G_N_EVENTREFS; n++) {
			if (ep->ref[n] == NULL)
				break;
			if (ep->ref[n] != ref)
				continue;
			TSRELEASE("GEOM events");
			TAILQ_REMOVE(&g_events, ep, events);
			ep->func(ep->arg, EV_CANCEL);
			mtx_assert(&g_eventlock, MA_OWNED);
			if (ep->flag & EV_WAKEUP) {
				ep->flag |= (EV_DONE|EV_CANCELED);
				wakeup(ep);
			} else {
				g_free(ep);
			}
			break;
		}
	}
	if (TAILQ_EMPTY(&g_events))
		wakeup(&g_pending_events);
	mtx_unlock(&g_eventlock);
}

static int
g_post_event_x(g_event_t *func, void *arg, int flag, int wuflag, struct g_event **epp, va_list ap)
{
	struct g_event *ep;
	void *p;
	u_int n;

	/* 对参数进行标准化输出处理 */
	g_trace(G_T_TOPOLOGY, "g_post_event_x(%p, %p, %d, %d)",
	    func, arg, flag, wuflag);
	KASSERT(wuflag == 0 || wuflag == EV_WAKEUP,
	    ("Wrong wuflag in g_post_event_x(0x%x)", wuflag));
	ep = g_malloc(sizeof *ep, flag | M_ZERO);	/* 创建 event 实例 */
	if (ep == NULL)
		return (ENOMEM);
	ep->flag = wuflag;

	/* 用一个循环来处理可变参数，每一个参数都放到 ref 数组当中 */
	for (n = 0; n < G_N_EVENTREFS; n++) {
		p = va_arg(ap, void *);
		if (p == NULL)
			break;
		g_trace(G_T_TOPOLOGY, "  ref %p", p);
		ep->ref[n] = p;
	}

	KASSERT(p == NULL, ("Too many references to event"));
	ep->func = func;
	ep->arg = arg;
	mtx_lock(&g_eventlock);
	TSHOLD("GEOM events");
	TAILQ_INSERT_TAIL(&g_events, ep, events);	/* 将事件插入到全局事件管理队列 */
	mtx_unlock(&g_eventlock);
	wakeup(&g_wait_event);	/* 唤醒休眠在该标志符上的所有线程 */
	if (epp != NULL)
		*epp = ep;
	curthread->td_pflags |= TDP_GEOM;
	return (0);
}

/*
	GEOM 层拥有它自己的 event queue 来通知 classes 有关重要事件的信息。g_post_event 函数
	告诉 GEOM 层从事件队列中去调用 func 和 arg 参数。参数表示 flag 会传递到函数内存的 malloc
	用于内存分配。允许的 flag 包括 M_WAITOK 和 M_NOWAIT。剩下的参数则作为用于识别事件的索引。
	可以使用任何给定的引用作为 g_cancel_event 的参数来取消事件。引用列表最终会以 NULL 作为结束
*/
int
g_post_event(g_event_t *func, void *arg, int flag, ...)
{
	va_list ap;
	int i;

	KASSERT(flag == M_WAITOK || flag == M_NOWAIT,
	    ("Wrong flag to g_post_event"));
	va_start(ap, flag);
	i = g_post_event_x(func, arg, flag, 0, NULL, ap);
	va_end(ap);
	return (i);
}

/* 注销操作，可能会清空整个 geom，慎用 */
void
g_do_wither()
{

	mtx_lock(&g_eventlock);
	g_wither_work = 1;
	mtx_unlock(&g_eventlock);
	wakeup(&g_wait_event);
}

/*
 * XXX: It might actually be useful to call this function with topology held.
 * XXX: This would ensure that the event gets created before anything else
 * XXX: changes.  At present all users have a handle on things in some other
 * XXX: way, so this remains an XXX for now.
 * g_waitfor_event 函数是 g_post_event 函数的阻塞版本。它等待事件完成或取消，然后返回
 * 该函数不能被一个事件所调用，否则会导致死锁
 */

int
g_waitfor_event(g_event_t *func, void *arg, int flag, ...)
{
	va_list ap;
	struct g_event *ep;
	int error;

	g_topology_assert_not();
	KASSERT(flag == M_WAITOK || flag == M_NOWAIT,
	    ("Wrong flag to g_post_event"));
	va_start(ap, flag);
	error = g_post_event_x(func, arg, flag, EV_WAKEUP, &ep, ap);
	va_end(ap);
	if (error)
		return (error);

	mtx_lock(&g_eventlock);

	/* EV_DONE 应该是表示事件已经完成的标志 */
	while (!(ep->flag & EV_DONE))
		msleep(ep, &g_eventlock, PRIBIO, "g_waitfor_event", hz);
	if (ep->flag & EV_CANCELED)
		error = EAGAIN;
	mtx_unlock(&g_eventlock);

	g_free(ep);
	return (error);
}

void
g_event_init()
{

	mtx_init(&g_eventlock, "GEOM orphanage", NULL, MTX_DEF);
}
