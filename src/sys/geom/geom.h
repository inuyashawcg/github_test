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
 *
 * $FreeBSD: releng/12.0/sys/geom/geom.h 330977 2018-03-15 09:16:10Z avg $
 */

#ifndef _GEOM_GEOM_H_
#define _GEOM_GEOM_H_

#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/queue.h>
#include <sys/ioccom.h>
#include <sys/conf.h>
#include <sys/module.h>

struct g_class;
struct g_geom;
struct g_consumer;
struct g_provider;
struct g_stat;
struct thread;
struct bio;
struct sbuf;
struct gctl_req;
struct g_configargs;
struct disk_zone_args;

typedef int g_config_t (struct g_configargs *ca);
typedef void g_ctl_req_t (struct gctl_req *, struct g_class *cp, char const *verb);
typedef int g_ctl_create_geom_t (struct gctl_req *, struct g_class *cp, struct g_provider *pp);
typedef int g_ctl_destroy_geom_t (struct gctl_req *, struct g_class *cp, struct g_geom *gp);
typedef int g_ctl_config_geom_t (struct gctl_req *, struct g_geom *gp, const char *verb);
typedef void g_init_t (struct g_class *mp);
typedef void g_fini_t (struct g_class *mp);
typedef struct g_geom * g_taste_t (struct g_class *, struct g_provider *, int flags);
typedef int g_ioctl_t(struct g_provider *pp, u_long cmd, void *data, int fflag, struct thread *td);
#define G_TF_NORMAL		0
#define G_TF_INSIST		1
#define G_TF_TRANSPARENT	2
typedef int g_access_t (struct g_provider *, int, int, int);
/* XXX: not sure about the thread arg */
typedef void g_orphan_t (struct g_consumer *);

typedef void g_start_t (struct bio *);
typedef void g_spoiled_t (struct g_consumer *);
typedef void g_attrchanged_t (struct g_consumer *, const char *attr);
typedef void g_provgone_t (struct g_provider *);
typedef void g_dumpconf_t (struct sbuf *, const char *indent, struct g_geom *,
    struct g_consumer *, struct g_provider *);
typedef void g_resize_t(struct g_consumer *cp);

/*
 * The g_class structure describes a transformation class.  In other words
 * all BSD disklabel handlers share one g_class, all MBR handlers share
 * one common g_class and so on.
 * g_class 用于描述一个转换类。换句话说，所有的BSD磁盘标签处理程序共享一个 g_class，所有的
 * MBR 共享一个共同的 g_class
 * 
 * Certain operations are instantiated(实例化) on the class, most notably the
 * taste and config_geom functions.
 * 特定的操作用于实例化类，最典型的就是 taste和 config_geom
 * 只有name字段是必须要设置的，其他的字段都是可选择的
 */
struct g_class {
	const char		*name;	/* class name */
	u_int			version;
	u_int			spare0;	// 闲置的、备用的

	/*
		指向用于teste事件处理的函数的指针。如果它是非空的，则在三种情况下调用它：
			- 在类激活的时候，所有的提供者都要执行 taste 操作
			- 当新的 provider 建立的时候，需要执行 teste 操作
			- 在 provider 的最后一次写入操作关闭之后，需要重新执行 taste(在发送第一次写入打开事件“破坏”时)
	*/
	g_taste_t		*taste;
	g_config_t		*config;	/* 该字段已经不再使用，它的功能已经被 ctlreq 字段替代 */
	g_ctl_req_t		*ctlreq;	/* 用于处理来自用户空间的应用所关联的事件 */
	g_init_t		*init;	/* 指向在类注册后立即调用的函数的指针 */
	g_fini_t		*fini;	/* 指向在类注销之前调用的函数的指针 */
	/*
		指向一个 class 上每一个 geom 卸载时都会调用的函数。当这个字段没有被设置的时候，类是不能被卸载的
	*/
	g_ctl_destroy_geom_t	*destroy_geom;
	/*
	 * Default values for geom methods
	 */
	g_start_t		*start;
	g_spoiled_t		*spoiled;
	g_attrchanged_t		*attrchanged;
	g_dumpconf_t		*dumpconf;
	g_access_t		*access;
	g_orphan_t		*orphan;
	g_ioctl_t		*ioctl;
	g_provgone_t		*providergone;
	g_resize_t		*resize;
	void			*spare1;
	void			*spare2;
	/*
	 * The remaining elements are private
	 * 其余的元素是私有的
	 */
	LIST_ENTRY(g_class)	class;
	LIST_HEAD(,g_geom)	geom;
};

/*
 * The g_geom_alias is a list node for aliases for the geom name
 * for device node creation.
 * g_geom_alias 是用于创建设备节点的geom名称别名的列表节点
 */
struct g_geom_alias {
	LIST_ENTRY(g_geom_alias) ga_next;
	const char		*ga_alias;
};

#define G_VERSION_00	0x19950323
#define G_VERSION_01	0x20041207	/* add fflag to g_ioctl_t */
#define G_VERSION	G_VERSION_01

/*
 * The g_geom is an instance of a g_class.
 * g_geom 是 g_class 的一个实体
 * 
 * 成员中包含了两个管理消费者和提供者的管理队列，所以 geom 应该是起到的是管理的作用，
 * 类比 devclass
 * 
 * geom（不要混淆“geom”和“geom”）是geom类的一个实例。例如：在典型的i386 FreeBSD系统中，
 * 每个磁盘将有一个MBR类geom。geom的名称其实并不重要，它只用于XML转储和调试目的。可以有许多
 * geom具有相同的名称
 */
struct g_geom {
	char			*name;
	struct g_class		*class;	/* 对应的g_class，感觉像是基类的设定 */
	LIST_ENTRY(g_geom)	geom;
	LIST_HEAD(,g_consumer)	consumer;	/* 消费者队列 */
	LIST_HEAD(,g_provider)	provider;	/* 生产者队列 */
	TAILQ_ENTRY(g_geom)	geoms;	/* XXX: better name */
	int			rank;

	/*
		如果我们要使用在这个 geom 中的 providers，那么我们就必须要设置 start 字段；
		如果我们打算使用 geom 中的 consumers，那么我们必须要设置 orphan 和 access
	*/
	g_start_t		*start;	/* 用于处理 I/O 的函数指针 */
	g_spoiled_t		*spoiled;	/* 用于释放consumer */
	g_attrchanged_t		*attrchanged;
	g_dumpconf_t		*dumpconf;
	g_access_t		*access;	/* access control */
	g_orphan_t		*orphan;	/* 指向用于通知孤立 consumer 的函数的指针 */
	g_ioctl_t		*ioctl;	/* 处理ioctl请求 */
	g_provgone_t		*providergone;
	g_resize_t		*resize;
	void			*spare0;
	void			*spare1;
	void			*softc;	/* Field for private use */
	unsigned		flags;
#define	G_GEOM_WITHER		0x01
#define	G_GEOM_VOLATILE_BIO	0x02
#define	G_GEOM_IN_ACCESS	0x04
#define	G_GEOM_ACCESS_WAIT	0x08
	LIST_HEAD(,g_geom_alias) aliases;	/* geom 别名队列？ */
};

/*
 * The g_bioq is a queue of struct bio's.
 * XXX: possibly collection point for statistics.
 * XXX: should (possibly) be collapsed with sys/bio.h::bio_queue_head.
 */
struct g_bioq {
	TAILQ_HEAD(, bio)	bio_queue;	/* buffer I/O queue */
	struct mtx		bio_queue_lock;	/* 队列锁 */
	int			bio_queue_length;	/* 队列的长度 */
};

/*
 * A g_consumer is an attachment point for a g_provider.  One g_consumer
 * can only be attached to one g_provider, but multiple g_consumers
 * can be attached to one g_provider.
 * 消费者是提供者的一个连接点，一个消费者只能被连接到一个提供者，但是多个消费者可以连接到
 * 一个提供者
 */

struct g_consumer {
	struct g_geom		*geom;	// 指向所属的geom
	LIST_ENTRY(g_consumer)	consumer;	// consumer管理队列
	struct g_provider	*provider;	// 指向所属的 provider
	LIST_ENTRY(g_consumer)	consumers;	/* XXX: better name */
	int			acr, acw, ace;
	int			flags;
#define G_CF_SPOILED		0x1
#define G_CF_ORPHAN		0x4
#define G_CF_DIRECT_SEND	0x10
#define G_CF_DIRECT_RECEIVE	0x20
	struct devstat		*stat;	// 设备描述
	u_int			nstart, nend;

	/* Two fields for the implementing class to use 
		实现类要使用的两个字段
	*/
	void			*private;
	u_int			index;
};

/*
 * A g_provider is a "logical disk". 提供者是一个逻辑磁盘
 * 消费者和提供者的数据结构非常相似，继承于同一个基类？
 */
struct g_provider {
	char			*name;
	LIST_ENTRY(g_provider)	provider;	// 管理队列
	struct g_geom		*geom;	// 指向所属的 geom
	LIST_HEAD(,g_consumer)	consumers;	// 消费者队列，类似总线管理设备
	int			acr, acw, ace;
	int			error;
	TAILQ_ENTRY(g_provider)	orphan;
	off_t			mediasize;
	u_int			sectorsize;
	u_int			stripesize;
	u_int			stripeoffset;
	struct devstat		*stat;	// 设备描述
	u_int			nstart, nend;
	u_int			flags;
#define G_PF_WITHER		0x2
#define G_PF_ORPHAN		0x4
#define	G_PF_ACCEPT_UNMAPPED	0x8
#define G_PF_DIRECT_SEND	0x10
#define G_PF_DIRECT_RECEIVE	0x20

	/* Two fields for the implementing class to use */
	void			*private;
	u_int			index;
};

/*
 * Descriptor of a classifier. We can register a function and
 * an argument, which is called by g_io_request() on bio's
 * that are not previously classified.
 * 分类器的描述符。我们可以注册一个函数和一个参数，该函数和参数由之前未分类的
 * bio上的 g_io_request 调用
 */
struct g_classifier_hook {
	TAILQ_ENTRY(g_classifier_hook) link;
	int			(*func)(void *arg, struct bio *bp);
	void			*arg;
};

/* BIO_GETATTR("GEOM::setstate") argument values. */
#define G_STATE_FAILED		0
#define G_STATE_REBUILD		1
#define G_STATE_RESYNC		2
#define G_STATE_ACTIVE		3

/* geom_dev.c */
struct cdev;
void g_dev_print(void);
void g_dev_physpath_changed(void);
struct g_provider *g_dev_getprovider(struct cdev *dev);

/* geom_dump.c */
void g_trace(int level, const char *, ...);
#	define G_T_TOPOLOGY	1
#	define G_T_BIO		2
#	define G_T_ACCESS	4


/* geom_event.c */
typedef void g_event_t(void *, int flag);
#define EV_CANCEL	1
int g_post_event(g_event_t *func, void *arg, int flag, ...);
int g_waitfor_event(g_event_t *func, void *arg, int flag, ...);
void g_cancel_event(void *ref);
int g_attr_changed(struct g_provider *pp, const char *attr, int flag);
int g_media_changed(struct g_provider *pp, int flag);
int g_media_gone(struct g_provider *pp, int flag);
void g_orphan_provider(struct g_provider *pp, int error);
void g_waitidlelock(void);

/* geom_subr.c */
int g_access(struct g_consumer *cp, int nread, int nwrite, int nexcl);
int g_attach(struct g_consumer *cp, struct g_provider *pp);
int g_compare_names(const char *namea, const char *nameb);
void g_destroy_consumer(struct g_consumer *cp);
void g_destroy_geom(struct g_geom *pp);
void g_destroy_provider(struct g_provider *pp);
void g_detach(struct g_consumer *cp);
void g_error_provider(struct g_provider *pp, int error);
struct g_provider *g_provider_by_name(char const *arg);
void g_geom_add_alias(struct g_geom *gp, const char *alias);
int g_getattr__(const char *attr, struct g_consumer *cp, void *var, int len);
#define g_getattr(a, c, v) g_getattr__((a), (c), (v), sizeof *(v))
int g_handleattr(struct bio *bp, const char *attribute, const void *val,
    int len);
int g_handleattr_int(struct bio *bp, const char *attribute, int val);
int g_handleattr_off_t(struct bio *bp, const char *attribute, off_t val);
int g_handleattr_uint16_t(struct bio *bp, const char *attribute, uint16_t val);
int g_handleattr_str(struct bio *bp, const char *attribute, const char *str);
struct g_consumer * g_new_consumer(struct g_geom *gp);
struct g_geom * g_new_geomf(struct g_class *mp, const char *fmt, ...)
    __printflike(2, 3);
struct g_provider * g_new_providerf(struct g_geom *gp, const char *fmt, ...)
    __printflike(2, 3);
void g_resize_provider(struct g_provider *pp, off_t size);
int g_retaste(struct g_class *mp);
void g_spoil(struct g_provider *pp, struct g_consumer *cp);
int g_std_access(struct g_provider *pp, int dr, int dw, int de);
void g_std_done(struct bio *bp);
void g_std_spoiled(struct g_consumer *cp);
void g_wither_geom(struct g_geom *gp, int error);
void g_wither_geom_close(struct g_geom *gp, int error);
void g_wither_provider(struct g_provider *pp, int error);

#if defined(DIAGNOSTIC) || defined(DDB)
int g_valid_obj(void const *ptr);
#endif
#ifdef DIAGNOSTIC
#define G_VALID_CLASS(foo) \
    KASSERT(g_valid_obj(foo) == 1, ("%p is not a g_class", foo))
#define G_VALID_GEOM(foo) \
    KASSERT(g_valid_obj(foo) == 2, ("%p is not a g_geom", foo))
#define G_VALID_CONSUMER(foo) \
    KASSERT(g_valid_obj(foo) == 3, ("%p is not a g_consumer", foo))
#define G_VALID_PROVIDER(foo) \
    KASSERT(g_valid_obj(foo) == 4, ("%p is not a g_provider", foo))
#else
#define G_VALID_CLASS(foo) do { } while (0)
#define G_VALID_GEOM(foo) do { } while (0)
#define G_VALID_CONSUMER(foo) do { } while (0)
#define G_VALID_PROVIDER(foo) do { } while (0)
#endif

int g_modevent(module_t, int, void *);

/* geom_io.c */
struct bio * g_clone_bio(struct bio *);
struct bio * g_duplicate_bio(struct bio *);
void g_destroy_bio(struct bio *);
void g_io_deliver(struct bio *bp, int error);
int g_io_getattr(const char *attr, struct g_consumer *cp, int *len, void *ptr);
int g_io_zonecmd(struct disk_zone_args *zone_args, struct g_consumer *cp);
int g_io_flush(struct g_consumer *cp);
int g_register_classifier(struct g_classifier_hook *hook);
void g_unregister_classifier(struct g_classifier_hook *hook);
void g_io_request(struct bio *bp, struct g_consumer *cp);
struct bio *g_new_bio(void);
struct bio *g_alloc_bio(void);
void g_reset_bio(struct bio *);
void * g_read_data(struct g_consumer *cp, off_t offset, off_t length, int *error);
int g_write_data(struct g_consumer *cp, off_t offset, void *ptr, off_t length);
int g_delete_data(struct g_consumer *cp, off_t offset, off_t length);
void g_print_bio(struct bio *bp);
int g_use_g_read_data(void *, off_t, void **, int);
int g_use_g_write_data(void *, off_t, void *, int);

/* geom_kern.c / geom_kernsim.c */

#ifdef _KERNEL

extern struct sx topology_lock;

/* linux 中包含有一个dump命令，用于备份文件系统，这里应该也是类似的作用 */
struct g_kerneldump {
	off_t		offset;
	off_t		length;
	struct dumperinfo di;
};

MALLOC_DECLARE(M_GEOM);

static __inline void *
g_malloc(int size, int flags)
{
	void *p;

	p = malloc(size, M_GEOM, flags);
	return (p);
}

static __inline void
g_free(void *ptr)
{

#ifdef DIAGNOSTIC
	if (sx_xlocked(&topology_lock)) {
		KASSERT(g_valid_obj(ptr) == 0,
		    ("g_free(%p) of live object, type %d", ptr,
		    g_valid_obj(ptr)));
	}
#endif
	free(ptr, M_GEOM);
}

#define g_topology_lock() 					\
	do {							\
		sx_xlock(&topology_lock);			\
	} while (0)

#define g_topology_try_lock()	sx_try_xlock(&topology_lock)

#define g_topology_unlock()					\
	do {							\
		sx_xunlock(&topology_lock);			\
	} while (0)

#define g_topology_assert()					\
	do {							\
		sx_assert(&topology_lock, SX_XLOCKED);		\
	} while (0)

#define g_topology_assert_not()					\
	do {							\
		sx_assert(&topology_lock, SX_UNLOCKED);		\
	} while (0)

#define g_topology_sleep(chan, timo)				\
	sx_sleep(chan, &topology_lock, 0, "gtopol", timo)

/* 
	注意宏定义中的 g_modevent，里面会包含有一个添加 g_class 的操作，g_class 中包含的
	主要就是一些函数定义(类似于vfs operations)。这里的设计思想感觉像是把某一类，或者说是
	针对某些对象的功能函数做成一个模块加载到内核当中，其他模块如果要使用这些方法的话，就到
	g_class全局链表中查找对应的模块
	该宏定义既可以用编译方式，也可以用 kld 的方式去加载 GEOM 模块，这是注册类的官方的方法
*/
#define DECLARE_GEOM_CLASS(class, name) 			\
	static moduledata_t name##_mod = {			\
		#name, g_modevent, &class			\
	};							\
	DECLARE_MODULE(name, name##_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);

int g_is_geom_thread(struct thread *td);

#endif /* _KERNEL */

/* geom_ctl.c */
int gctl_set_param(struct gctl_req *req, const char *param, void const *ptr, int len);
void gctl_set_param_err(struct gctl_req *req, const char *param, void const *ptr, int len);
void *gctl_get_param(struct gctl_req *req, const char *param, int *len);
char const *gctl_get_asciiparam(struct gctl_req *req, const char *param);
void *gctl_get_paraml(struct gctl_req *req, const char *param, int len);
int gctl_error(struct gctl_req *req, const char *fmt, ...) __printflike(2, 3);
struct g_class *gctl_get_class(struct gctl_req *req, char const *arg);
struct g_geom *gctl_get_geom(struct gctl_req *req, struct g_class *mpr, char const *arg);
struct g_provider *gctl_get_provider(struct gctl_req *req, char const *arg);

#endif /* _GEOM_GEOM_H_ */
