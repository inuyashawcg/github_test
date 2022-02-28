/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 John Baldwin <jhb@FreeBSD.org>
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
 *
 * $FreeBSD: releng/12.0/sys/sys/rwlock.h 326256 2017-11-27 15:01:59Z pfg $
 */

#ifndef _SYS_RWLOCK_H_
#define _SYS_RWLOCK_H_

#include <sys/_lock.h>
#include <sys/_rwlock.h>
#include <sys/lock_profile.h>
#include <sys/lockstat.h>

#ifdef _KERNEL
#include <sys/pcpu.h>
#include <machine/atomic.h>
#endif

/*
 * The rw_lock field consists of several fields.  The low bit indicates
 * if the lock is locked with a read (shared) or write (exclusive) lock.
 * A value of 0 indicates a write lock, and a value of 1 indicates a read
 * lock.  Bit 1 is a boolean indicating if there are any threads waiting
 * for a read lock.  Bit 2 is a boolean indicating if there are any threads
 * waiting for a write lock.  The rest of the variable's definition is
 * dependent on the value of the first bit.  For a write lock, it is a
 * pointer to the thread holding the lock, similar to the mtx_lock field of
 * mutexes.  For read locks, it is a count of read locks that are held.
 * 
 * rw_lock 其实是一个32位或者64位的一个变量，它本身会包含非常多的属性信息。低位表示锁是
 * 用读（共享）锁还是写（独占）锁锁定的。0 表示一个写锁，1表示一个读锁。bit 1 是一个布尔值，
 * 表示是否有一个进程在等待一个读锁。bit 2 表示是否有进程正在等待一个写锁。变量定义的其余部分
 * 取决于第一位的值。对于写锁，它是指向持有锁的线程的指针，类似于互斥锁的 mtx_lock 字段。
 * 对于读锁，它是持有的读锁的计数。
 *
 * When the lock is not locked by any thread, it is encoded as a read lock
 * with zero waiters.
 * 当锁未被任何线程锁定时，它被编码为具有零等待者的读锁
 * 
 */

#define	RW_LOCK_READ		0x01
#define	RW_LOCK_READ_WAITERS	0x02
#define	RW_LOCK_WRITE_WAITERS	0x04
#define	RW_LOCK_WRITE_SPINNER	0x08
#define	RW_LOCK_WRITER_RECURSED	0x10

#define	RW_LOCK_FLAGMASK						\
	(RW_LOCK_READ | RW_LOCK_READ_WAITERS | RW_LOCK_WRITE_WAITERS |	\
	RW_LOCK_WRITE_SPINNER | RW_LOCK_WRITER_RECURSED)

#define	RW_LOCK_WAITERS		(RW_LOCK_READ_WAITERS | RW_LOCK_WRITE_WAITERS)

#define	RW_OWNER(x)		((x) & ~RW_LOCK_FLAGMASK)
#define	RW_READERS_SHIFT	5
#define	RW_READERS(x)		(RW_OWNER((x)) >> RW_READERS_SHIFT)
#define	RW_READERS_LOCK(x)	((x) << RW_READERS_SHIFT | RW_LOCK_READ)
#define	RW_ONE_READER		(1 << RW_READERS_SHIFT)

#define	RW_UNLOCKED		RW_READERS_LOCK(0)
#define	RW_DESTROYED		(RW_LOCK_READ_WAITERS | RW_LOCK_WRITE_WAITERS)

#ifdef _KERNEL

#define	rw_recurse	lock_object.lo_data

#define	RW_READ_VALUE(x)	((x)->rw_lock)

/* Very simple operations on rw_lock. */

/* Try to obtain a write lock once. 
	尝试获取一次写锁
*/
#define	_rw_write_lock(rw, tid)						\
	atomic_cmpset_acq_ptr(&(rw)->rw_lock, RW_UNLOCKED, (tid))

#define	_rw_write_lock_fetch(rw, vp, tid)				\
	atomic_fcmpset_acq_ptr(&(rw)->rw_lock, vp, (tid))

/* Release a write lock quickly if there are no waiters. */
#define	_rw_write_unlock(rw, tid)					\
	atomic_cmpset_rel_ptr(&(rw)->rw_lock, (tid), RW_UNLOCKED)

#define	_rw_write_unlock_fetch(rw, tid)					\
	atomic_fcmpset_rel_ptr(&(rw)->rw_lock, (tid), RW_UNLOCKED)

/*
 * Full lock operations that are suitable to be inlined in non-debug
 * kernels.  If the lock cannot be acquired or released trivially then
 * the work is deferred to another function.
 * 适合内联在非调试内核中的全锁操作。如果锁不能被获取或释放，那么工作将被推迟到另一个函数。
 */

/* Acquire a write lock. */
#define	__rw_wlock(rw, tid, file, line) do {				\
	uintptr_t _tid = (uintptr_t)(tid);				\
	uintptr_t _v = RW_UNLOCKED;					\
									\
	if (__predict_false(LOCKSTAT_PROFILE_ENABLED(rw__acquire) ||	\
	    !_rw_write_lock_fetch((rw), &_v, _tid)))			\
		_rw_wlock_hard((rw), _v, (file), (line));		\
} while (0)

/* Release a write lock. */
#define	__rw_wunlock(rw, tid, file, line) do {				\
	uintptr_t _v = (uintptr_t)(tid);				\
									\
	if (__predict_false(LOCKSTAT_PROFILE_ENABLED(rw__release) ||	\
	    !_rw_write_unlock_fetch((rw), &_v)))			\
		_rw_wunlock_hard((rw), _v, (file), (line));		\
} while (0)

/*
 * Function prototypes.  Routines that start with _ are not part of the
 * external API and should not be called directly.  Wrapper macros should
 * be used instead.
 */
void	_rw_init_flags(volatile uintptr_t *c, const char *name, int opts);
void	_rw_destroy(volatile uintptr_t *c);
void	rw_sysinit(void *arg);
void	rw_sysinit_flags(void *arg);
int	_rw_wowned(const volatile uintptr_t *c);
void	_rw_wlock_cookie(volatile uintptr_t *c, const char *file, int line);
int	__rw_try_wlock_int(struct rwlock *rw LOCK_FILE_LINE_ARG_DEF);
int	__rw_try_wlock(volatile uintptr_t *c, const char *file, int line);
void	_rw_wunlock_cookie(volatile uintptr_t *c, const char *file, int line);
void	__rw_rlock_int(struct rwlock *rw LOCK_FILE_LINE_ARG_DEF);
void	__rw_rlock(volatile uintptr_t *c, const char *file, int line);
int	__rw_try_rlock_int(struct rwlock *rw LOCK_FILE_LINE_ARG_DEF);
int	__rw_try_rlock(volatile uintptr_t *c, const char *file, int line);
void	_rw_runlock_cookie_int(struct rwlock *rw LOCK_FILE_LINE_ARG_DEF);
void	_rw_runlock_cookie(volatile uintptr_t *c, const char *file, int line);
void	__rw_wlock_hard(volatile uintptr_t *c, uintptr_t v
	    LOCK_FILE_LINE_ARG_DEF);
void	__rw_wunlock_hard(volatile uintptr_t *c, uintptr_t v
	    LOCK_FILE_LINE_ARG_DEF);
int	__rw_try_upgrade_int(struct rwlock *rw LOCK_FILE_LINE_ARG_DEF);
int	__rw_try_upgrade(volatile uintptr_t *c, const char *file, int line);
void	__rw_downgrade_int(struct rwlock *rw LOCK_FILE_LINE_ARG_DEF);
void	__rw_downgrade(volatile uintptr_t *c, const char *file, int line);
#if defined(INVARIANTS) || defined(INVARIANT_SUPPORT)
void	__rw_assert(const volatile uintptr_t *c, int what, const char *file,
	    int line);
#endif

/*
 * Top-level macros to provide lock cookie once the actual rwlock is passed.
 * They will also prevent passing a malformed object to the rwlock KPI by
 * failing compilation as the rw_lock reserved member will not be found.
 */
#define	rw_init(rw, n)							\
	_rw_init_flags(&(rw)->rw_lock, n, 0)
#define	rw_init_flags(rw, n, o)						\
	_rw_init_flags(&(rw)->rw_lock, n, o)
/*
	This functions destroys a lock previously initialized with rw_init(). 
	The rw lock must be unlocked.
*/
#define	rw_destroy(rw)							\
	_rw_destroy(&(rw)->rw_lock)
/*
	This function returns a non-zero value if the current thread owns an 
	exclusive lock on rw.
*/
#define	rw_wowned(rw)							\
	_rw_wowned(&(rw)->rw_lock)
#define	_rw_wlock(rw, f, l)						\
	_rw_wlock_cookie(&(rw)->rw_lock, f, l)
#define	_rw_try_wlock(rw, f, l)						\
	__rw_try_wlock(&(rw)->rw_lock, f, l)
#define	_rw_wunlock(rw, f, l)						\
	_rw_wunlock_cookie(&(rw)->rw_lock, f, l)
#define	_rw_try_rlock(rw, f, l)						\
	__rw_try_rlock(&(rw)->rw_lock, f, l)
#if LOCK_DEBUG > 0
#define	_rw_rlock(rw, f, l)						\
	__rw_rlock(&(rw)->rw_lock, f, l)
#define	_rw_runlock(rw, f, l)						\
	_rw_runlock_cookie(&(rw)->rw_lock, f, l)
#else
#define	_rw_rlock(rw, f, l)						\
	__rw_rlock_int((struct rwlock *)rw)
#define	_rw_runlock(rw, f, l)						\
	_rw_runlock_cookie_int((struct rwlock *)rw)
#endif
#if LOCK_DEBUG > 0
#define	_rw_wlock_hard(rw, v, f, l)					\
	__rw_wlock_hard(&(rw)->rw_lock, v, f, l)
#define	_rw_wunlock_hard(rw, v, f, l)					\
	__rw_wunlock_hard(&(rw)->rw_lock, v, f, l)
#define	_rw_try_upgrade(rw, f, l)					\
	__rw_try_upgrade(&(rw)->rw_lock, f, l)
#define	_rw_downgrade(rw, f, l)						\
	__rw_downgrade(&(rw)->rw_lock, f, l)
#else
#define	_rw_wlock_hard(rw, v, f, l)					\
	__rw_wlock_hard(&(rw)->rw_lock, v)
#define	_rw_wunlock_hard(rw, v, f, l)					\
	__rw_wunlock_hard(&(rw)->rw_lock, v)
#define	_rw_try_upgrade(rw, f, l)					\
	__rw_try_upgrade_int(rw)
#define	_rw_downgrade(rw, f, l)						\
	__rw_downgrade_int(rw)
#endif
#if defined(INVARIANTS) || defined(INVARIANT_SUPPORT)
/*
	his function allows assertions specified in what to be made about rw.	
	If the assertions are not true and the kernel is compiled with options
	INVARIANTS and options INVARIANT_SUPPORT, the kernel will panic.
*/
#define	_rw_assert(rw, w, f, l)						\
	__rw_assert(&(rw)->rw_lock, w, f, l)
#endif


/*
 * Public interface for lock operations.
 */

#ifndef LOCK_DEBUG
#error LOCK_DEBUG not defined, include <sys/lock.h> before <sys/rwlock.h>
#endif
#if LOCK_DEBUG > 0 || defined(RWLOCK_NOINLINE)
#define	rw_wlock(rw)		_rw_wlock((rw), LOCK_FILE, LOCK_LINE)
#define	rw_wunlock(rw)		_rw_wunlock((rw), LOCK_FILE, LOCK_LINE)
#else
/*
	Lock rw as	a writer.  If there are	any shared owners of the lock, the current thread	blocks.
	The rw_wlock() function can be called recursively only if rw has been initialized with the 
	RW_RECURSE option enabled.
	将rw锁定为编写器。如果存在锁的任何共享所有者，则当前线程将阻塞。只有在启用了 RW_RECURSE 选项的情况下初始化rw时，
	才能递归调用 rw_wlock 函数
*/
#define	rw_wlock(rw)							\
	__rw_wlock((rw), curthread, LOCK_FILE, LOCK_LINE)
/*
	This function releases an exclusive lock previously acquired by rw_wlock().
*/
#define	rw_wunlock(rw)							\
	__rw_wunlock((rw), curthread, LOCK_FILE, LOCK_LINE)
#endif
/*
	Lock rw as a reader. If any thread holds this lock exclusively, the current thread blocks,
	and its	priority is propagated to the exclusive holder. The rw_rlock() function	can be called
	when the thread has	already	acquired reader	access on rw. This is called "recursing on a lock".

	将rw锁定为读卡器。如果任何线程以独占方式持有此锁，则当前线程将阻塞，其优先级将传播到独占持有者。当线程已经获得rw上的
	读卡器访问权限时，可以调用rw_rlock（）函数。这被称为“在锁上递归”
*/
#define	rw_rlock(rw)		_rw_rlock((rw), LOCK_FILE, LOCK_LINE)
/*
	This function releases a shared lock previously acquired by rw_rlock().
*/
#define	rw_runlock(rw)		_rw_runlock((rw), LOCK_FILE, LOCK_LINE)
/*
	Try to lock rw as a reader. This function	will return true if the operation succeeds, 
	otherwise 0 will be returned.
*/
#define	rw_try_rlock(rw)	_rw_try_rlock((rw), LOCK_FILE, LOCK_LINE)
/*
	Attempt to upgrade a single shared lock to an exclusive lock. The current thread must hold a 
	shared lock	of rw. This will only succeed if the current thread holds the only shared lock on rw,
	and it only holds a single shared lock. If the attempt succeeds rw_try_upgrade() will return a 
	non-zero value, and	the current thread will hold an exclusive lock. If the attempt fails rw_try_upgrade()
	will return zero,	and the	current	thread will still hold a shared lock.
*/
#define	rw_try_upgrade(rw)	_rw_try_upgrade((rw), LOCK_FILE, LOCK_LINE)
/*
	Try to lock rw as a writer. This function	will return true if the operation succeeds, 
	otherwise 0 will be returned.
*/
#define	rw_try_wlock(rw)	_rw_try_wlock((rw), LOCK_FILE, LOCK_LINE)
/*
	Convert an exclusive lock into a single shared lock. The current thread must hold 
	an exclusive lock	of rw.
*/
#define	rw_downgrade(rw)	_rw_downgrade((rw), LOCK_FILE, LOCK_LINE)
/*
	This function releases a shared lock previously acquired by rw_rlock() or an exclusive lock 
	previously acquired by rw_wlock().
*/
#define	rw_unlock(rw)	do {						\
	if (rw_wowned(rw))						\
		rw_wunlock(rw);						\
	else								\
		rw_runlock(rw);						\
} while (0)
/*
	Atomically	release	rw while waiting for an	event. For more details on the parameters to 
	this function, see sleep(9).
*/
#define	rw_sleep(chan, rw, pri, wmesg, timo)				\
	_sleep((chan), &(rw)->lock_object, (pri), (wmesg),		\
	    tick_sbt * (timo), 0, C_HARDCLOCK)
/*
	This function returns non-zero if rw has been initialized, and zero otherwise.
*/
#define	rw_initialized(rw)	lock_initialized(&(rw)->lock_object)

struct rw_args {
	void		*ra_rw;
	const char 	*ra_desc;
	int		ra_flags;
};

#define	RW_SYSINIT_FLAGS(name, rw, desc, flags)				\
	static struct rw_args name##_args = {				\
		(rw),							\
		(desc),							\
		(flags),						\
	};								\
	SYSINIT(name##_rw_sysinit, SI_SUB_LOCK, SI_ORDER_MIDDLE,	\
	    rw_sysinit, &name##_args);					\
	SYSUNINIT(name##_rw_sysuninit, SI_SUB_LOCK, SI_ORDER_MIDDLE,	\
	    _rw_destroy, __DEVOLATILE(void *, &(rw)->rw_lock))

#define	RW_SYSINIT(name, rw, desc)	RW_SYSINIT_FLAGS(name, rw, desc, 0)

/*
 * Options passed to rw_init_flags().
 */
/*
	Witness should not log messages about duplicate locks being acquired.
*/
#define	RW_DUPOK	0x01
/*
	Do not profile this lock. 不要配置此锁
*/
#define	RW_NOPROFILE	0x02
/*
	Instruct witness(4) to ignore this lock.
*/
#define	RW_NOWITNESS	0x04
/*
	Do not log any operations for this lock via ktr(4).
*/
#define	RW_QUIET	0x08
/*
	Allow threads to recursively	acquire	exclusive locks for rw
	允许线程递归调用此锁
*/
#define	RW_RECURSE	0x10
/*
	If the kernel has been compiled with option INVARIANTS, rw_init_flags() 
	will assert that the rw has not been initialized multiple times without 
	intervening calls to rw_destroy() unless this option is specified.
*/
#define	RW_NEW		0x20

/*
 * The INVARIANTS-enabled rw_assert() functionality.
 *
 * The constants need to be defined for INVARIANT_SUPPORT infrastructure
 * support as _rw_assert() itself uses them and the latter implies that
 * _rw_assert() must build.
 */
#if defined(INVARIANTS) || defined(INVARIANT_SUPPORT)
/*
	Assert that current thread holds either a shared or exclusive lock of rw.
*/
#define	RA_LOCKED		LA_LOCKED
/*
	Assert that current thread holds a shared lock of rw.
*/
#define	RA_RLOCKED		LA_SLOCKED
/*
	Assert that current thread holds an exclusive	lock of rw.
*/
#define	RA_WLOCKED		LA_XLOCKED
/*
	Assert that current thread holds neither a shared nor exclusive	lock of	rw.
*/
#define	RA_UNLOCKED		LA_UNLOCKED
/*
	In addition, one of the following optional flags may be specified
	with RA_LOCKED, RA_RLOCKED, or RA_WLOCKED:
		RA_RECURSED  Assert that the current thread holds a recursive lock of rw.
		RA_NOTRECURSED  Assert that the current thread does not hold a recursive lock of rw.
*/
#define	RA_RECURSED		LA_RECURSED
#define	RA_NOTRECURSED		LA_NOTRECURSED
#endif

#ifdef INVARIANTS
#define	rw_assert(rw, what)	_rw_assert((rw), (what), LOCK_FILE, LOCK_LINE)
#else
#define	rw_assert(rw, what)
#endif

#endif /* _KERNEL */
#endif /* !_SYS_RWLOCK_H_ */
