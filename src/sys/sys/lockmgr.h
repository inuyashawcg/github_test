/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Attilio Rao <attilio@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice(s), this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified other than the possible 
 *    addition of one or more copyright notices.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice(s), this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * $FreeBSD: releng/12.0/sys/sys/lockmgr.h 326256 2017-11-27 15:01:59Z pfg $
 */

#ifndef	_SYS_LOCKMGR_H_
#define	_SYS_LOCKMGR_H_

#include <sys/_lock.h>
#include <sys/_lockmgr.h>
#include <sys/_mutex.h>
#include <sys/_rwlock.h>

#define	LK_SHARE			0x01
#define	LK_SHARED_WAITERS		0x02
#define	LK_EXCLUSIVE_WAITERS		0x04
#define	LK_EXCLUSIVE_SPINNERS		0x08	// 自旋锁？
#define	LK_ALL_WAITERS							\
	(LK_SHARED_WAITERS | LK_EXCLUSIVE_WAITERS)
#define	LK_FLAGMASK							\
	(LK_SHARE | LK_ALL_WAITERS | LK_EXCLUSIVE_SPINNERS)

#define	LK_HOLDER(x)			((x) & ~LK_FLAGMASK)
#define	LK_SHARERS_SHIFT		4
#define	LK_SHARERS(x)			(LK_HOLDER(x) >> LK_SHARERS_SHIFT)
#define	LK_SHARERS_LOCK(x)		((x) << LK_SHARERS_SHIFT | LK_SHARE)
#define	LK_ONE_SHARER			(1 << LK_SHARERS_SHIFT)
#define	LK_UNLOCKED			LK_SHARERS_LOCK(0)
#define	LK_KERNPROC			((uintptr_t)(-1) & ~LK_FLAGMASK)

#ifdef _KERNEL

#if !defined(LOCK_FILE) || !defined(LOCK_LINE)
#error	"LOCK_FILE and LOCK_LINE not defined, include <sys/lock.h> before"
#endif

struct thread;
#define	lk_recurse	lock_object.lo_data

/*
 * Function prototipes.  Routines that start with an underscore are not part
 * of the public interface and might be wrappered with a macro.
 */
int	 __lockmgr_args(struct lock *lk, u_int flags, struct lock_object *ilk,
	    const char *wmesg, int prio, int timo, const char *file, int line);
int	 lockmgr_lock_fast_path(struct lock *lk, u_int flags,
	    struct lock_object *ilk, const char *file, int line);
int	 lockmgr_unlock_fast_path(struct lock *lk, u_int flags,
	    struct lock_object *ilk);
#if defined(INVARIANTS) || defined(INVARIANT_SUPPORT)
void	 _lockmgr_assert(const struct lock *lk, int what, const char *file, int line);
#endif
void	 _lockmgr_disown(struct lock *lk, const char *file, int line);

void	 lockallowrecurse(struct lock *lk);
void	 lockallowshare(struct lock *lk);
void	 lockdestroy(struct lock *lk);
void	 lockdisablerecurse(struct lock *lk);
void	 lockdisableshare(struct lock *lk);
void	 lockinit(struct lock *lk, int prio, const char *wmesg, int timo,
	    int flags);
#ifdef DDB
int	 lockmgr_chain(struct thread *td, struct thread **ownerp);
#endif
void	 lockmgr_printinfo(const struct lock *lk);
int	 lockstatus(const struct lock *lk);

/*
 * As far as the ilk can be a static NULL pointer these functions need a
 * strict prototype in order to safely use the lock_object member.
 */
static __inline int
_lockmgr_args(struct lock *lk, u_int flags, struct mtx *ilk, const char *wmesg,
    int prio, int timo, const char *file, int line)
{

	return (__lockmgr_args(lk, flags, (ilk != NULL) ? &ilk->lock_object :
	    NULL, wmesg, prio, timo, file, line));
}

static __inline int
_lockmgr_args_rw(struct lock *lk, u_int flags, struct rwlock *ilk,
    const char *wmesg, int prio, int timo, const char *file, int line)
{

	return (__lockmgr_args(lk, flags, (ilk != NULL) ? &ilk->lock_object :
	    NULL, wmesg, prio, timo, file, line));
}

/*
 * Define aliases in order to complete lockmgr KPI.
 */
#define	lockmgr(lk, flags, ilk)						\
	_lockmgr_args((lk), (flags), (ilk), LK_WMESG_DEFAULT,		\
	    LK_PRIO_DEFAULT, LK_TIMO_DEFAULT, LOCK_FILE, LOCK_LINE)
#define	lockmgr_args(lk, flags, ilk, wmesg, prio, timo)			\
	_lockmgr_args((lk), (flags), (ilk), (wmesg), (prio), (timo),	\
	    LOCK_FILE, LOCK_LINE)
#define	lockmgr_args_rw(lk, flags, ilk, wmesg, prio, timo)		\
	_lockmgr_args_rw((lk), (flags), (ilk), (wmesg), (prio), (timo),	\
	    LOCK_FILE, LOCK_LINE)
#define	lockmgr_disown(lk)						\
	_lockmgr_disown((lk), LOCK_FILE, LOCK_LINE)
#define	lockmgr_recursed(lk)						\
	((lk)->lk_recurse != 0)
#define	lockmgr_rw(lk, flags, ilk)					\
	_lockmgr_args_rw((lk), (flags), (ilk), LK_WMESG_DEFAULT,	\
	    LK_PRIO_DEFAULT, LK_TIMO_DEFAULT, LOCK_FILE, LOCK_LINE)
#ifdef INVARIANTS
#define	lockmgr_assert(lk, what)					\
	_lockmgr_assert((lk), (what), LOCK_FILE, LOCK_LINE)
#else
#define	lockmgr_assert(lk, what)
#endif

/*
 * Flags for lockinit().
 */
#define	LK_INIT_MASK	0x0000FF
/*
	Allow recursion on an exclusive lock.  For every lock there must be a release.
*/
#define	LK_CANRECURSE	0x000001	// Allow recursive exclusive locks.
#define	LK_NODUP	0x000002	// witness(4) should log messages about	duplicate locks being acquired.
#define	LK_NOPROFILE	0x000004	// Disable lock	profiling for this lock.
#define	LK_NOSHARE	0x000008	// Allow exclusive locks only.
#define	LK_NOWITNESS	0x000010	// Instruct witness(4) to ignore this lock.
#define	LK_QUIET	0x000020	// Disable ktr(4) logging for this lock.
#define	LK_ADAPTIVE	0x000040
#define	LK_IS_VNODE	0x000080	/* Tell WITNESS about a VNODE lock */

/*
 * Additional attributes to be used in lockmgr().
 */
#define	LK_EATTR_MASK	0x00FF00
/*
	Unlock the interlock (which	should be locked already).
	也就是说一个 interlock 已经被锁定了，其他地方需要对这个所进行解锁，
	然后就传入了这么一个 flag。其他地方如果检测到的话，就要解锁后使用数据
*/
#define	LK_INTERLOCK	0x000100
/*
	Do not allow the call to sleep.  This can be used to test the	lock.
*/
#define	LK_NOWAIT	0x000200
#define	LK_RETRY	0x000400
/*
	Fail if operation has slept.
*/
#define	LK_SLEEPFAIL	0x000800
#define	LK_TIMELOCK	0x001000	// Use timo during a sleep; otherwise, 0 is used.
#define	LK_NODDLKTREAT	0x002000
#define	LK_VNHELD	0x004000

/*
 * Operations for lockmgr().
 */
#define	LK_TYPE_MASK	0xFF0000
/*
	Downgrade exclusive	lock to	a shared lock.	Downgrading a shared lock is not permitted.  
	If	an exclusive lock has been recursed, the	system will panic(9).
*/
#define	LK_DOWNGRADE	0x010000	// Exclusive-to-shared downgrade.
/*
	Wait for all activity on the lock to end, then mark it decommissioned. 
	This is used before freeing	a lock that is part of a piece of memory that
	is about to	be freed.  (As documented in <sys/lockmgr.h>.)
*/
#define	LK_DRAIN	0x020000	// Wait for all lock activity to end.
#define	LK_EXCLOTHER	0x040000	// exclother：互斥的，排他的
/*
	Acquire an exclusive lock.	If an exclusive	locks already held, 
	and LK_CANRECURSE is not set, the system will	panic(9).
*/
#define	LK_EXCLUSIVE	0x080000	/* 专用的 */
/*
	Release the	lock.  Releasing a lock	that is	not held can cause a panic(9).
*/
#define	LK_RELEASE	0x100000	// Release any type of lock.
/*
	Acquire a shared lock.  If an exclusive lock is currently held, 
	EDEADLK will be returned.
*/
#define	LK_SHARED	0x200000	// Shared lock.
/*
	Upgrade a shared lock to an	exclusive lock.	 If this call fails, the shared	lock 
	is	lost, even if the	LK_NOWAIT flag is specified.  During the upgrade, the shared 
	lock could be temporarily dropped.  Attempts to upgrade an exclusive lock will cause 
	a panic(9).
*/
#define	LK_UPGRADE	0x400000	// Shared-to-exclusive upgrade.
/*
	try	to upgrade a shared lock to an exclusive lock. The	failure	to upgrade 
	does	not result in the dropping of	the shared lock	ownership.
*/
#define	LK_TRYUPGRADE	0x800000

#define	LK_TOTAL_MASK	(LK_INIT_MASK | LK_EATTR_MASK | LK_TYPE_MASK)

/*
 * Default values for lockmgr_args().
 */
#define	LK_WMESG_DEFAULT	(NULL)
#define	LK_PRIO_DEFAULT		(0)
#define	LK_TIMO_DEFAULT		(0)

/*
 * Assertion flags.
 */
#if defined(INVARIANTS) || defined(INVARIANT_SUPPORT)
#define	KA_LOCKED	LA_LOCKED
#define	KA_SLOCKED	LA_SLOCKED
#define	KA_XLOCKED	LA_XLOCKED
#define	KA_UNLOCKED	LA_UNLOCKED
#define	KA_RECURSED	LA_RECURSED
#define	KA_NOTRECURSED	LA_NOTRECURSED
#endif

#endif /* _KERNEL */

#endif /* !_SYS_LOCKMGR_H_ */
