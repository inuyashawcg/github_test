/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000,2003 Doug Rabson
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: releng/12.0/sys/kern/subr_kobj.c 326271 2017-11-27 15:20:12Z pfg $");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#ifndef TEST
#include <sys/systm.h>
#endif

#ifdef TEST
#include "usertest.h"
#endif

static MALLOC_DEFINE(M_KOBJ, "kobj", "Kernel object structures");

#ifdef KOBJ_STATS

u_int kobj_lookup_hits;
u_int kobj_lookup_misses;

SYSCTL_UINT(_kern, OID_AUTO, kobj_hits, CTLFLAG_RD,
	   &kobj_lookup_hits, 0, "");
SYSCTL_UINT(_kern, OID_AUTO, kobj_misses, CTLFLAG_RD,
	   &kobj_lookup_misses, 0, "");

#endif

static struct mtx kobj_mtx;
static int kobj_mutex_inited;
static int kobj_next_id = 1;

#define	KOBJ_LOCK()		mtx_lock(&kobj_mtx)
#define	KOBJ_UNLOCK()		mtx_unlock(&kobj_mtx)
#define	KOBJ_ASSERT(what)	mtx_assert(&kobj_mtx, what);

SYSCTL_INT(_kern, OID_AUTO, kobj_methodcount, CTLFLAG_RD,
	   &kobj_next_id, 0, "");

static void
kobj_init_mutex(void *arg)
{
	if (!kobj_mutex_inited) {
		mtx_init(&kobj_mtx, "kobj", NULL, MTX_DEF);
		kobj_mutex_inited = 1;
	}
}

SYSINIT(kobj, SI_SUB_LOCK, SI_ORDER_ANY, kobj_init_mutex, NULL);

/*
 * This method structure is used to initialise new caches. Since the
 * desc pointer is NULL, it is guaranteed never to match any read
 * descriptors.
 */
static const struct kobj_method null_method = {
	0, 0,
};

/* 返回错误值6：设备未被config */
int
kobj_error_method(void)
{

	return ENXIO;
}

/*
	从函数逻辑来看，主要进行了两部分的操作，首先是把kobj_class管理的methods数组中元素的desc->id
	赋值；然后是对ops中的cache(其实也是一个方法数组)进行初始化操作
*/
static void
kobj_class_compile_common(kobj_class_t cls, kobj_ops_t ops)
{
	kobj_method_t *m;
	int i;

	/*
	 * Don't do anything if we are already compiled.
	 */
	if (cls->ops)
		return;

	/*
	 * First register any methods which need it.
	 * 只要methods中的方法的id被设置为0，那么就表示该函数是要被用到的
	 * 通过下面的++操作和赋值操作，可以看出method的id是被重新赋值，貌似是从1开始的
	 * 
	 * 通过驱动实例可以看到，一般都会包含一个 device_method_t 类型的数组， device_method_t其实就是kobj_method_t
	 * 类型。再结合 kobj_class 的定义，可以看到 device_method_t数组其实就是 kobj_class结构体中的 *methods。
	 * 然后分析下面这个循环，功能就是遍历整个数组，然后给每个元素(也就是驱动例程注册的方法)的desc->id重新赋值，应该就是
	 * 注册驱动例程所有要用到的函数
	 */
	for (i = 0, m = cls->methods; m->desc; i++, m++) {
		if (m->desc->id == 0)
			m->desc->id = kobj_next_id++;
	}

	/*
	 * Then initialise the ops table.
	 * 这里涉及到了对cache中函数的操作，都定义成空方法
	 */
	for (i = 0; i < KOBJ_CACHE_SIZE; i++)
		ops->cache[i] = &null_method;
	ops->cls = cls;
	cls->ops = ops;
}

/* 
	先近似认为cls为一个deriver，init函数执行的时候会判断cls的ops是否为空，
	如果为空的话，就要在kobj_class_compile 函数中新建一个ops
*/
void
kobj_class_compile(kobj_class_t cls)
{
	kobj_ops_t ops;

	KOBJ_ASSERT(MA_NOTOWNED);

	/*
	 * Allocate space for the compiled ops table.
	 * 从kobj_init()函数中的逻辑来看，cls已经确定是null
	 */
	ops = malloc(sizeof(struct kobj_ops), M_KOBJ, M_NOWAIT);
	if (!ops)
		panic("%s: out of memory", __func__);

	KOBJ_LOCK();
	
	/*
	 * We may have lost a race for kobj_class_compile here - check
	 * to make sure someone else hasn't already compiled this
	 * class.
	 */
	if (cls->ops) {
		KOBJ_UNLOCK();
		free(ops, M_KOBJ);
		return;
	}

	kobj_class_compile_common(cls, ops);
	KOBJ_UNLOCK();
}

void
kobj_class_compile_static(kobj_class_t cls, kobj_ops_t ops)
{

	KASSERT(kobj_mutex_inited == 0,
	    ("%s: only supported during early cycles", __func__));

	/*
	 * Increment refs to make sure that the ops table is not freed.
	 */
	cls->refs++;
	kobj_class_compile_common(cls, ops);
}

// 查找kobj_class_t cls中的methods，不包括其他的部分
static kobj_method_t*
kobj_lookup_method_class(kobj_class_t cls, kobjop_desc_t desc)
{
	kobj_method_t *methods = cls->methods;
	kobj_method_t *ce;

	for (ce = methods; ce && ce->desc; ce++) {
		if (ce->desc == desc) {
			return ce;
		}
	}

	return NULL;
}

static kobj_method_t*
kobj_lookup_method_mi(kobj_class_t cls,
		      kobjop_desc_t desc)
{
	kobj_method_t *ce;
	kobj_class_t *basep;

	// 在kobj_class中查找方法
	ce = kobj_lookup_method_class(cls, desc);
	if (ce)
		return ce;

	// baseclasses保存的可以认为是kobj_class的多基类，通过递归调用查找其中的方法
	basep = cls->baseclasses;
	if (basep) {
		for (; *basep; basep++) {
			ce = kobj_lookup_method_mi(*basep, desc);
			if (ce)
				return ce;
		}
	}

	return NULL;
}

kobj_method_t*
kobj_lookup_method(kobj_class_t cls,
		   kobj_method_t **cep,
		   kobjop_desc_t desc)
{
	kobj_method_t *ce;

	// 如果通过递归调用还是没有找到对应的方法，那么就调用default方法
	ce = kobj_lookup_method_mi(cls, desc);
	if (!ce)
		ce = &desc->deflt;
	if (cep)
		*cep = ce;
	return ce;
}

void
kobj_class_free(kobj_class_t cls)
{
	void* ops = NULL;

	KOBJ_ASSERT(MA_NOTOWNED);
	KOBJ_LOCK();

	/*
	 * Protect against a race between kobj_create and
	 * kobj_delete.
	 * 如果cls的引用计数为0了，那就证明现在已经没有任何一个对象再使用它了，可以free掉
	 */
	if (cls->refs == 0) {
		/*
		 * For now we don't do anything to unregister any methods
		 * which are no longer used.
		 */

		/*
		 * Free memory and clean up.
		 */
		ops = cls->ops;
		cls->ops = NULL;
	}
	
	KOBJ_UNLOCK();

	if (ops)
		free(ops, M_KOBJ);
}

kobj_t
kobj_create(kobj_class_t cls,
	    struct malloc_type *mtype,
	    int mflags)
{
	kobj_t obj;

	/*
	 * Allocate and initialise the new object.
	 */
	obj = malloc(cls->size, mtype, mflags | M_ZERO);
	if (!obj)
		return NULL;
	kobj_init(obj, cls);

	return obj;
}

static void
kobj_init_common(kobj_t obj, kobj_class_t cls)
{

	obj->ops = cls->ops;
	cls->refs++;
}

/*
	驱动方面，可以近似认为obj表示一个device，cls表示一个driver
*/
void
kobj_init(kobj_t obj, kobj_class_t cls)
{
	KOBJ_ASSERT(MA_NOTOWNED);
  retry:
	KOBJ_LOCK();

	/*
	 * Consider compiling the class' method table.
	 * 当ops为空的时候执行compile操作
	 */
	if (!cls->ops) {
		/*
		 * kobj_class_compile doesn't want the lock held
		 * because of the call to malloc - we drop the lock
		 * and re-try.
		 */
		KOBJ_UNLOCK();
		kobj_class_compile(cls);
		goto retry;
	}

	/* 
		obj是device的一个子集，cls就相当于driver。因为device不仅仅包含obj，还包含别的量，
		所以kobj_init_common()函数就相当于把device中包含的obj中的ops设置成driver的ops，
		然后把driver的引用计数++
	*/
	kobj_init_common(obj, cls);

	KOBJ_UNLOCK();
}

// 静态？？
void
kobj_init_static(kobj_t obj, kobj_class_t cls)
{

	KASSERT(kobj_mutex_inited == 0,
	    ("%s: only supported during early cycles", __func__));

	kobj_init_common(obj, cls);
}

void
kobj_delete(kobj_t obj, struct malloc_type *mtype)
{
	kobj_class_t cls = obj->ops->cls;
	int refs;

	/*
	 * Consider freeing the compiled method table for the class
	 * after its last instance is deleted. As an optimisation, we
	 * should defer this for a short while to avoid thrashing.
	 */
	KOBJ_ASSERT(MA_NOTOWNED);
	KOBJ_LOCK();

	cls->refs--;
	refs = cls->refs;
	KOBJ_UNLOCK();

	/*
		这里仅仅是把cls，也就是kobj_class部分给free掉了，并没有对cache中保存的函数进行任何操作
	*/
	if (!refs)
		kobj_class_free(cls);

	obj->ops = NULL;
	if (mtype)
		free(obj, mtype);
}
