/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997-2000 Doug Rabson
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
__FBSDID("$FreeBSD: releng/12.0/sys/kern/kern_linker.c 339407 2018-10-17 10:31:08Z bz $");

#include "opt_ddb.h"
#include "opt_kld.h"
#include "opt_hwpmc_hooks.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sysproto.h>
#include <sys/sysent.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/module.h>
#include <sys/mount.h>
#include <sys/linker.h>
#include <sys/eventhandler.h>
#include <sys/fcntl.h>
#include <sys/jail.h>
#include <sys/libkern.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

#include <net/vnet.h>

#include <security/mac/mac_framework.h>

#include "linker_if.h"

#ifdef HWPMC_HOOKS
#include <sys/pmckern.h>
#endif

#ifdef KLD_DEBUG
int kld_debug = 0;
SYSCTL_INT(_debug, OID_AUTO, kld_debug, CTLFLAG_RWTUN,
    &kld_debug, 0, "Set various levels of KLD debug");
#endif

/* 
	These variables are used by kernel debuggers to enumerate loaded files. 
	内核调试器使用这些变量来枚举加载的文件
*/
const int kld_off_address = offsetof(struct linker_file, address);
const int kld_off_filename = offsetof(struct linker_file, filename);
const int kld_off_pathname = offsetof(struct linker_file, pathname);
const int kld_off_next = offsetof(struct linker_file, link.tqe_next);

/*
 * static char *linker_search_path(const char *name, struct mod_depend
 * *verinfo);
 */
static const char 	*linker_basename(const char *path);

/*
 * Find a currently loaded file given its filename. 
 * 通过filename来查找目前已经加载的file
 */
static linker_file_t linker_find_file_by_name(const char* _filename);

/*
 * Find a currently loaded file given its file id.
 * 通过file id来查找已经加载的文件
 */
static linker_file_t linker_find_file_by_id(int _fileid);

/* 
	Metadata from the static kernel - 静态内核中的元数据(driver？？)
	首先声明了两个弱符号
*/
SET_DECLARE(modmetadata_set, struct mod_metadata);

/* 自定义malloc类型 */
MALLOC_DEFINE(M_LINKER, "linker", "kernel linker");

/*
	这个应该是最基本的kernel link file，在link_elf.c文件中进行初始化，具体的阶段是在
	KLD - THIRD，主要完成的任务是解析 .dynamic section，获取动态链接相关信息；对符号表
	也做了一些操作
*/
linker_file_t linker_kernel_file;

static struct sx kld_sx;	/* kernel linker lock */

/*
 * Load counter used by clients to determine if a linker file has been
 * re-loaded. This counter is incremented for each file load. 每次加载文件时，此计数器都会递增
 * loadcnt 应该是记录 linker file 被加载的次数
 */
static int loadcnt;

/* 
	应该是用于管理所有的linker class，linker_class 其实就是kobj_class，它也是包含一个kobj_methods，
	link_elf和link_elf_obj其实都是先往linker_class中注册方法，然后添加linker_class到classes，然后
	kern_linker就可以根据不同的应用场景调用注册进来的的方法
*/
static linker_class_list_t classes;

/* 
	应该是管理所有的 linker file，linker_file 其实并不是目标文件实体，他只是对文件的一个描述，我们可以通过
	该结构体获取目标文件的名称，位置等等信息，然后就可以对目标文件进行链接操作
*/
static linker_file_list_t linker_files;
static int next_file_id = 1;
static int linker_no_more_classes = 0;

/*
	获取下一个linker file的id，每个linker file都有一个唯一的id
*/
#define	LINKER_GET_NEXT_FILE_ID(a) do {					\
	linker_file_t lftmp;						\
									\
	if (!cold)							\
		sx_assert(&kld_sx, SA_XLOCKED);				\
retry:									\
	TAILQ_FOREACH(lftmp, &linker_files, link) {			\
		if (next_file_id == lftmp->id) {			\
			next_file_id++;					\
			goto retry;					\
		}							\
	}								\
	(a) = next_file_id;						\
} while(0)


/* 
	XXX wrong name; we're looking at version provision tags here, not modules 
	我们在这里看到的是版本配置标记，而不是模块
	modlist 里边并没有包含 module元素，仅仅是保留了 module的相关信息，包括module名称、
	版本和隶属于哪个linker file，所以这个链表仅仅是起到了管理 module信息的作用
	进一步，通过该链表查找某个module
*/
typedef TAILQ_HEAD(, modlist) modlisthead_t;
struct modlist {
	TAILQ_ENTRY(modlist) link;	/* chain together all modules - 把所有的 module 连接在一起 */
	linker_file_t   container;	/* 表示这个 modlist 隶属于哪个 linker file？？ */
	const char 	*name;
	int             version;	/* 驱动模块注册的时候也会添加一个版本信息 */
};
typedef struct modlist *modlist_t;

/*
	found_modules 是在 linker_preload()函数中初始化的，所以它应该是管理内核预加载的所有模块
*/
static modlisthead_t found_modules;

/* 向 linker file 增加依赖 */
static int	linker_file_add_dependency(linker_file_t file,
		    linker_file_t dep);

/* 可能是查找本文件中的 symbol table 中的 symbol */
static caddr_t	linker_file_lookup_symbol_internal(linker_file_t file,
		    const char* name, int deps);

static int	linker_load_module(const char *kldname,
		    const char *modname, struct linker_file *parent,
		    const struct mod_depend *verinfo, struct linker_file **lfpp);

static modlist_t modlist_lookup2(const char *name, const struct mod_depend *verinfo);

static void
linker_init(void *arg)
{
	/*
		Shared/exclusive lock 共享独占锁
		linker_init初始化工作主要就是两部分：第一，初始化linker_class队列，用于管理所有的linker class；
		第二，初始化linker_files队列，用于管理所有的linker file
	*/
	sx_init(&kld_sx, "kernel linker");
	TAILQ_INIT(&classes);
	TAILQ_INIT(&linker_files);
}

SYSINIT(linker, SI_SUB_KLD, SI_ORDER_FIRST, linker_init, NULL);

/* 执行的顺序是比较靠后的 */
static void
linker_stop_class_add(void *arg)
{
	linker_no_more_classes = 1;
}

SYSINIT(linker_class, SI_SUB_KLD, SI_ORDER_ANY, linker_stop_class_add, NULL);

int
linker_add_class(linker_class_t lc)
{

	/*
	 * We disallow any class registration past SI_ORDER_ANY
	 * of SI_SUB_KLD.  We bump the reference count to keep the
	 * ops from being freed.
	 * SI_ORDER_ANY是执行顺序的最高等级，也就表示是最后执行，所以任何超过这个等级的
	 * class都不被允许注册
	 */
	if (linker_no_more_classes == 1)
		return (EPERM);
	kobj_class_compile((kobj_class_t) lc);
	((kobj_class_t)lc)->refs++;	/* XXX: kobj_mtx */

	/*
		只要有新的class添加，就需要插入到 classes
	*/
	TAILQ_INSERT_TAIL(&classes, lc, link);
	return (0);
}

static void
linker_file_sysinit(linker_file_t lf)
{
	struct sysinit **start, **stop, **sipp, **xipp, *save;

	/* kld printf */
	KLD_DPF(FILE, ("linker_file_sysinit: calling SYSINITs for %s\n",
	    lf->filename));

	sx_assert(&kld_sx, SA_XLOCKED);

	/*
		找到以 sysinit_set 命名的section(init_main.c)，里边存放的应该都是
	*/
	if (linker_file_lookup_set(lf, "sysinit_set", &start, &stop, NULL) != 0)
		return;
	/*
	 * Perform a bubble sort of the system initialization objects by
	 * their subsystem (primary key) and order (secondary key).
	 * 按子系统（主键）和顺序（次键）对系统初始化对象执行气泡式排序
	 *
	 * Since some things care about execution order, this is the operation
	 * which ensures continued function.
	 * 因为有些事情关心的是执行顺序，所以这是确保连续功能的操作
	 */
	for (sipp = start; sipp < stop; sipp++) {
		for (xipp = sipp + 1; xipp < stop; xipp++) {
			if ((*sipp)->subsystem < (*xipp)->subsystem ||
			    ((*sipp)->subsystem == (*xipp)->subsystem &&
			    (*sipp)->order <= (*xipp)->order))
				continue;	/* skip */
			save = *sipp;
			*sipp = *xipp;
			*xipp = save;
		}
	}

	/*
	 * Traverse the (now) ordered list of system initialization tasks.
	 * Perform each task, and continue on to the next task.
	 */
	sx_xunlock(&kld_sx);
	mtx_lock(&Giant);
	for (sipp = start; sipp < stop; sipp++) {
		if ((*sipp)->subsystem == SI_SUB_DUMMY)
			continue;	/* skip dummy task(s) */

		/* Call function */
		(*((*sipp)->func)) ((*sipp)->udata);
	}
	mtx_unlock(&Giant);
	sx_xlock(&kld_sx);
}

static void
linker_file_sysuninit(linker_file_t lf)
{
	struct sysinit **start, **stop, **sipp, **xipp, *save;

	KLD_DPF(FILE, ("linker_file_sysuninit: calling SYSUNINITs for %s\n",
	    lf->filename));

	sx_assert(&kld_sx, SA_XLOCKED);

	if (linker_file_lookup_set(lf, "sysuninit_set", &start, &stop,
	    NULL) != 0)
		return;

	/*
	 * Perform a reverse bubble sort of the system initialization objects
	 * by their subsystem (primary key) and order (secondary key).
	 *
	 * Since some things care about execution order, this is the operation
	 * which ensures continued function.
	 * 有些操作需要关注执行的顺序
	 */
	for (sipp = start; sipp < stop; sipp++) {
		for (xipp = sipp + 1; xipp < stop; xipp++) {
			if ((*sipp)->subsystem > (*xipp)->subsystem ||
			    ((*sipp)->subsystem == (*xipp)->subsystem &&
			    (*sipp)->order >= (*xipp)->order))
				continue;	/* skip */
			save = *sipp;
			*sipp = *xipp;
			*xipp = save;
		}
	}

	/*
	 * Traverse the (now) ordered list of system initialization tasks.
	 * Perform each task, and continue on to the next task.
	 */
	sx_xunlock(&kld_sx);
	mtx_lock(&Giant);
	for (sipp = start; sipp < stop; sipp++) {
		if ((*sipp)->subsystem == SI_SUB_DUMMY)
			continue;	/* skip dummy task(s) */

		/* Call function */
		(*((*sipp)->func)) ((*sipp)->udata);
	}
	mtx_unlock(&Giant);
	sx_xlock(&kld_sx);
}

/*
	注册sysctl
*/
static void
linker_file_register_sysctls(linker_file_t lf, bool enable)
{
	struct sysctl_oid **start, **stop, **oidp;

	KLD_DPF(FILE,
	    ("linker_file_register_sysctls: registering SYSCTLs for %s\n",
	    lf->filename));

	sx_assert(&kld_sx, SA_XLOCKED);

	/*
		sysctl是被放到以 sysctl_set 命名的section中，从代码逻辑可以验证之前的假设，
		linker_file_lookup_set() 函数的功能就是查找某个文件中是否有某个section
	*/
	if (linker_file_lookup_set(lf, "sysctl_set", &start, &stop, NULL) != 0)
		return;

	sx_xunlock(&kld_sx);
	sysctl_wlock();

	/*
		从代码逻辑反推，每个section中应该是连续存放一些函数或者结构体，然后我们就可以定义
		对应的指针来逐个进行访问
	*/
	for (oidp = start; oidp < stop; oidp++) {
		if (enable)
			sysctl_register_oid(*oidp);
		else
			sysctl_register_disabled_oid(*oidp);
	}
	sysctl_wunlock();
	sx_xlock(&kld_sx);
}

static void
linker_file_enable_sysctls(linker_file_t lf)
{
	struct sysctl_oid **start, **stop, **oidp;

	KLD_DPF(FILE,
	    ("linker_file_enable_sysctls: enable SYSCTLs for %s\n",
	    lf->filename));

	sx_assert(&kld_sx, SA_XLOCKED);

	/* 首先还是要判断是否有sysctl section */
	if (linker_file_lookup_set(lf, "sysctl_set", &start, &stop, NULL) != 0)
		return;

	sx_xunlock(&kld_sx);
	sysctl_wlock();
	for (oidp = start; oidp < stop; oidp++)
		sysctl_enable_oid(*oidp);	/* 使能每一个sysctl iod */
	sysctl_wunlock();
	sx_xlock(&kld_sx);
}

static void
linker_file_unregister_sysctls(linker_file_t lf)
{
	struct sysctl_oid **start, **stop, **oidp;

	KLD_DPF(FILE, ("linker_file_unregister_sysctls: unregistering SYSCTLs"
	    " for %s\n", lf->filename));

	sx_assert(&kld_sx, SA_XLOCKED);

	/*
		逻辑同上，先查找相应的section，然后执行功能函数
	*/
	if (linker_file_lookup_set(lf, "sysctl_set", &start, &stop, NULL) != 0)
		return;

	sx_xunlock(&kld_sx);
	sysctl_wlock();
	for (oidp = start; oidp < stop; oidp++)
		sysctl_unregister_oid(*oidp);
	sysctl_wunlock();
	sx_xlock(&kld_sx);
}

static int
linker_file_register_modules(linker_file_t lf)
{
	struct mod_metadata **start, **stop, **mdp;
	const moduledata_t *moddata;
	int first_error, error;

	KLD_DPF(FILE, ("linker_file_register_modules: registering modules"
	    " in %s\n", lf->filename));

	sx_assert(&kld_sx, SA_XLOCKED);

	/*
		modmetadata_set 其实就是在 MODULE_DECLARE 宏定义中，所以driver
	*/
	if (linker_file_lookup_set(lf, "modmetadata_set", &start,
	    &stop, NULL) != 0) {
		/*
		 * This fallback should be unnecessary, but if we get booted
		 * from boot2 instead of loader and we are missing our
		 * metadata then we have to try the best we can.
		 * 这种回退应该是不必要的，但是如果我们从boot2引导而不是从loader引导，
		 * 并且丢失了元数据，那么我们必须尽最大努力
		 */
		if (lf == linker_kernel_file) {
			start = SET_BEGIN(modmetadata_set);
			stop = SET_LIMIT(modmetadata_set);
		} else
			return (0);
	}
	first_error = 0;
	for (mdp = start; mdp < stop; mdp++) {
		/* 如果不是 MDT_MODULE 类型，跳过 */
		if ((*mdp)->md_type != MDT_MODULE)
			continue;
		moddata = (*mdp)->md_data;
		KLD_DPF(FILE, ("Registering module %s in %s\n",
		    moddata->name, lf->filename));
		error = module_register(moddata, lf);
		if (error) {
			printf("Module %s failed to register: %d\n",
			    moddata->name, error);
			if (first_error == 0)
				first_error = error;
		}
	}
	return (first_error);
}

static void
linker_init_kernel_modules(void)
{

	sx_xlock(&kld_sx);
	linker_file_register_modules(linker_kernel_file);
	sx_xunlock(&kld_sx);
}

SYSINIT(linker_kernel, SI_SUB_KLD, SI_ORDER_ANY, linker_init_kernel_modules,
    NULL);

static int
linker_load_file(const char *filename, linker_file_t *result)
{
	linker_class_t lc;
	linker_file_t lf;
	int foundfile, error, modules;

	/* 
		Refuse to load modules if securelevel raised 
		如果安全等级提升，则拒绝加载模块
	*/
	if (prison0.pr_securelevel > 0)
		return (EPERM);

	sx_assert(&kld_sx, SA_XLOCKED);
	
	/* 这里再检查一次该文件是否已经加载，即判断是否已经在 linker_files 中 */
	lf = linker_find_file_by_name(filename);
	if (lf) {
		KLD_DPF(FILE, ("linker_load_file: file %s is already loaded,"
		    " incrementing refs\n", filename));
		*result = lf;
		lf->refs++;
		return (0);
	}
	foundfile = 0;
	error = 0;

	/*
	 * We do not need to protect (lock) classes here because there is
	 * no class registration past startup (SI_SUB_KLD, SI_ORDER_ANY)
	 * and there is no class deregistration mechanism at this time.
	 * 
	 * 我们不需要在这里保护（锁定）类，因为在启动之后没有类注册（SI_SUB_KLD，SI_ORDER_ANY），
	 * 而且此时没有类注销机制.代码逻辑就是遍历classes中的所有元素，查找里边每个元素注册的
	 * linker_load_file 函数，然后执行
	 */
	TAILQ_FOREACH(lc, &classes, link) {
		KLD_DPF(FILE, ("linker_load_file: trying to load %s\n",
		    filename));
		error = LINKER_LOAD_FILE(lc, filename, &lf);
		/*
		 * If we got something other than ENOENT, then it exists but
		 * we cannot load it for some other reason.
		 * 如果不是错误“没有对应的文件和文件夹”，那么就可以认为已经找到相应的文件，
		 * 把 foundfile 的值设置为1
		 */
		if (error != ENOENT)
			foundfile = 1;
		if (lf) {
			/*
				向linker file注册modules
			*/
			error = linker_file_register_modules(lf);
			if (error == EEXIST) {
				linker_file_unload(lf, LINKER_UNLOAD_FORCE);
				return (error);
			}
			/*
				注册完成后，判断modules是否为空，然后向linker file注册sysctl，
				再执行linker file 的 sysinit 操作
			*/
			modules = !TAILQ_EMPTY(&lf->modules);
			linker_file_register_sysctls(lf, false);
			linker_file_sysinit(lf);
			lf->flags |= LINKER_FILE_LINKED;

			/*
			 * If all of the modules in this file failed
			 * to load, unload the file and return an
			 * error of ENOEXEC.
			 * 如果在这个文件中的所有modules都加载失败，那就unload这个文件并返回错误码
			 */
			if (modules && TAILQ_EMPTY(&lf->modules)) {
				linker_file_unload(lf, LINKER_UNLOAD_FORCE);
				return (ENOEXEC);
			}
			linker_file_enable_sysctls(lf);
			EVENTHANDLER_INVOKE(kld_load, lf);
			*result = lf;
			return (0);
		}
	}
	/*
	 * Less than ideal, but tells the user whether it failed to load or
	 * the module was not found.
	 * 不太理想，但会告诉用户是加载失败还是找不到模块
	 */
	if (foundfile) {

		/*
		 * If the file type has not been recognized by the last try
		 * printout a message before to fail.
		 * 如果文件类型尚未被上次尝试识别，请打印一条消息以失败
		 */
		if (error == ENOSYS)
			printf("%s: %s - unsupported file type\n",
			    __func__, filename);

		/*
		 * Format not recognized or otherwise unloadable.
		 * When loading a module that is statically built into
		 * the kernel EEXIST percolates back up as the return
		 * value.  Preserve this so that apps like sysinstall
		 * can recognize this special case and not post bogus
		 * dialog boxes.
		 * 
		 * 格式无法识别或无法加载。当加载静态构建到内核中的模块时，EEXIST将作为返回值进行过滤。
		 * 保留这一点，以便sysinstall这样的应用程序可以识别这种特殊情况，而不是发布伪造的对话框。
		 */
		if (error != EEXIST)
			error = ENOEXEC;
	} else
		error = ENOENT;		/* Nothing found */
	return (error);
}

int
linker_reference_module(const char *modname, struct mod_depend *verinfo,
    linker_file_t *result)
{
	modlist_t mod;
	int error;

	/* 查找module是否存在 - found_modules */
	sx_xlock(&kld_sx);
	if ((mod = modlist_lookup2(modname, verinfo)) != NULL) {
		*result = mod->container;
		(*result)->refs++;
		sx_xunlock(&kld_sx);
		return (0);
	}

	/* 找到module之后加载 */
	error = linker_load_module(NULL, modname, NULL, verinfo, result);
	sx_xunlock(&kld_sx);
	return (error);
}

int
linker_release_module(const char *modname, struct mod_depend *verinfo,
    linker_file_t lf)
{
	modlist_t mod;
	int error;

	sx_xlock(&kld_sx);
	
	/* 首先要判断linker_file是否存在 */
	if (lf == NULL) {
		KASSERT(modname != NULL,
		    ("linker_release_module: no file or name"));
		mod = modlist_lookup2(modname, verinfo);
		if (mod == NULL) {
			sx_xunlock(&kld_sx);
			return (ESRCH);
		}
		lf = mod->container;
	} else
		KASSERT(modname == NULL && verinfo == NULL,
		    ("linker_release_module: both file and name"));
	error =	linker_file_unload(lf, LINKER_UNLOAD_NORMAL);
	sx_xunlock(&kld_sx);
	return (error);
}

/*
	这里的filename是basename，找的就是.ko或者不带扩展名的文件
*/
static linker_file_t
linker_find_file_by_name(const char *filename)
{
	linker_file_t lf;
	/* kernel object */
	char *koname;

	/* +4 应该是给 .ko\0 申请位置空间 */
	koname = malloc(strlen(filename) + 4, M_LINKER, M_WAITOK);
	sprintf(koname, "%s.ko", filename);

	sx_assert(&kld_sx, SA_XLOCKED);
	
	/* 在linker_files中查找koname命名的linker file */
	TAILQ_FOREACH(lf, &linker_files, link) {
		if (strcmp(lf->filename, koname) == 0)
			break;
		if (strcmp(lf->filename, filename) == 0)
			break;
	}
	free(koname, M_LINKER);
	return (lf);
}

/*
	利用id从linker_files队列中 linker file
*/
static linker_file_t
linker_find_file_by_id(int fileid)
{
	linker_file_t lf;

	sx_assert(&kld_sx, SA_XLOCKED);
	TAILQ_FOREACH(lf, &linker_files, link)
		if (lf->id == fileid && lf->flags & LINKER_FILE_LINKED)
			break;
	return (lf);
}

/* 遍历 linker_files */
int
linker_file_foreach(linker_predicate_t *predicate, void *context)
{
	linker_file_t lf;
	int retval = 0;

	sx_xlock(&kld_sx);
	TAILQ_FOREACH(lf, &linker_files, link) {
		retval = predicate(lf, context);
		if (retval != 0)
			break;
	}
	sx_xunlock(&kld_sx);
	return (retval);
}

/*
	 创建一个linker_file并初始化
*/
linker_file_t
linker_make_file(const char *pathname, linker_class_t lc)
{
	linker_file_t lf;
	const char *filename;

	if (!cold)
		sx_assert(&kld_sx, SA_XLOCKED);
	
	/* 获取basename，不包含路径信息 */
	filename = linker_basename(pathname);

	KLD_DPF(FILE, ("linker_make_file: new file, filename='%s' for pathname='%s'\n", filename, pathname));

	/*
		linker_file 头部是kobj_class，首先创建一个kobj
	*/
	lf = (linker_file_t)kobj_create((kobj_class_t)lc, M_LINKER, M_WAITOK);
	if (lf == NULL)
		return (NULL);
	lf->ctors_addr = 0;
	lf->ctors_size = 0;
	lf->refs = 1;
	lf->userrefs = 0;
	lf->flags = 0;
	lf->filename = strdup(filename, M_LINKER);
	lf->pathname = strdup(pathname, M_LINKER);
	LINKER_GET_NEXT_FILE_ID(lf->id);
	lf->ndeps = 0;
	lf->deps = NULL;
	lf->loadcnt = ++loadcnt;
	STAILQ_INIT(&lf->common);
	TAILQ_INIT(&lf->modules);

	/* 所有的linker file也是要统一管理的 */
	TAILQ_INSERT_TAIL(&linker_files, lf, link);
	return (lf);
}

int
linker_file_unload(linker_file_t file, int flags)
{
	module_t mod, next;
	modlist_t ml, nextml;
	struct common_symbol *cp;
	int error, i;

	/* 
		Refuse to unload modules if securelevel raised. 
		FreeBSD中监督机制(参考jail)，用来限制一些可能带来危险的操作
	*/
	if (prison0.pr_securelevel > 0)
		return (EPERM);

	sx_assert(&kld_sx, SA_XLOCKED);
	KLD_DPF(FILE, ("linker_file_unload: lf->refs=%d\n", file->refs));

	/* Easy case of just dropping a reference. */
	if (file->refs > 1) {
		file->refs--;
		return (0);
	}

	/* Give eventhandlers a chance to prevent the unload. */
	error = 0;
	EVENTHANDLER_INVOKE(kld_unload_try, file, &error);
	if (error != 0)
		return (EBUSY);

	KLD_DPF(FILE, ("linker_file_unload: file is unloading,"
	    " informing modules\n"));

	/*
	 * Quiesce all the modules to give them a chance to veto the unload.
	 * 停止所有模块，让它们有机会否决卸载
	 */
	MOD_SLOCK;
	for (mod = TAILQ_FIRST(&file->modules); mod;
	     mod = module_getfnext(mod)) {
		
		/* module_quiesce: 即将卸载某个module，执行相应的函数来判断卸载是否可行 */
		error = module_quiesce(mod);
		if (error != 0 && flags != LINKER_UNLOAD_FORCE) {
			KLD_DPF(FILE, ("linker_file_unload: module %s"
			    " vetoed unload\n", module_getname(mod)));
			/*
			 * XXX: Do we need to tell all the quiesced modules
			 * that they can resume work now via a new module
			 * event?
			 */
			MOD_SUNLOCK;
			return (error);
		}
	}
	MOD_SUNLOCK;

	/*
	 * Inform any modules associated with this file that they are
	 * being unloaded.
	 * 通知所有与该file相关联的module它们正在被卸载
	 */
	MOD_XLOCK;
	for (mod = TAILQ_FIRST(&file->modules); mod; mod = next) {
		next = module_getfnext(mod);
		MOD_XUNLOCK;

		/*
		 * Give the module a chance to veto the unload.
		 */
		if ((error = module_unload(mod)) != 0) {
#ifdef KLD_DEBUG
			MOD_SLOCK;
			KLD_DPF(FILE, ("linker_file_unload: module %s"
			    " failed unload\n", module_getname(mod)));
			MOD_SUNLOCK;
#endif
			return (error);
		}
		MOD_XLOCK;
		module_release(mod);
	}
	MOD_XUNLOCK;

	/* 
		release掉所有module之后，还要在 found_modules清除相关记录 
		Q&A：如果module在别的地方正在被用到，要如何处理，还是说同一个module会注册
		多次？如果是注册多次，前面的release操作要如何来解释？？
	*/
	TAILQ_FOREACH_SAFE(ml, &found_modules, link, nextml) {
		if (ml->container == file) {
			TAILQ_REMOVE(&found_modules, ml, link);
			free(ml, M_LINKER);
		}
	}

	/*
	 * Don't try to run SYSUNINITs if we are unloaded due to a
	 * link error.
	 */
	if (file->flags & LINKER_FILE_LINKED) {
		file->flags &= ~LINKER_FILE_LINKED;
		linker_file_unregister_sysctls(file);
		linker_file_sysuninit(file);
	}
	TAILQ_REMOVE(&linker_files, file, link);	/* 从linker_files队列中移除该file */

	/*
		linker_file 结构体中的 deps中的元素也是 struct linker_file 类型
		Q&A：当一个linker_file unload的时候，它依赖的linker_file也要被unload的？？
	*/
	if (file->deps) {
		for (i = 0; i < file->ndeps; i++)
			linker_file_unload(file->deps[i], flags);
		free(file->deps, M_LINKER);
		file->deps = NULL;
	}
	while ((cp = STAILQ_FIRST(&file->common)) != NULL) {
		STAILQ_REMOVE_HEAD(&file->common, link);
		free(cp, M_LINKER);
	}

	/*
		如果该 linker_file是 linker_elf_file格式，那么它就需要执行额外的步骤，因为elf格式的file
		还包含了其他的元素，它们也是需要执行特定操作
	*/
	LINKER_UNLOAD(file);

	EVENTHANDLER_INVOKE(kld_unload, file->filename, file->address,
	    file->size);

	if (file->filename) {
		free(file->filename, M_LINKER);
		file->filename = NULL;
	}
	if (file->pathname) {
		free(file->pathname, M_LINKER);
		file->pathname = NULL;
	}
	kobj_delete((kobj_t) file, M_LINKER);
	return (0);
}

int
linker_ctf_get(linker_file_t file, linker_ctf_t *lc)
{
	return (LINKER_CTF_GET(file, lc));
}

static int
linker_file_add_dependency(linker_file_t file, linker_file_t dep)
{
	linker_file_t *newdeps;

	sx_assert(&kld_sx, SA_XLOCKED);
	file->deps = realloc(file->deps, (file->ndeps + 1) * sizeof(*newdeps),
	    M_LINKER, M_WAITOK | M_ZERO);
	file->deps[file->ndeps] = dep;
	file->ndeps++;
	KLD_DPF(FILE, ("linker_file_add_dependency:"
	    " adding %s as dependency for %s\n", 
	    dep->filename, file->filename));
	return (0);
}

/*
 * Locate a linker set and its contents.  This is a helper function to avoid
 * linker_if.h exposure elsewhere.  Note: firstp and lastp are really void **.
 * This function is used in this file so we can avoid having lots of (void **)
 * casts.
 * 
 * 查找链接器集及其内容。这是一个帮助函数，可避免在其他地方暴露linker_if.h。注意：firstp
 * 和lastp实际上是void**。这个函数在这个文件中使用，所以我们可以避免有很多（void**）类型转换
 */
int
linker_file_lookup_set(linker_file_t file, const char *name,
    void *firstp, void *lastp, int *countp)
{

	sx_assert(&kld_sx, SA_LOCKED);
	return (LINKER_LOOKUP_SET(file, name, firstp, lastp, countp));
}

/*
 * List all functions in a file.
 */
int
linker_file_function_listall(linker_file_t lf,
    linker_function_nameval_callback_t callback_func, void *arg)
{
	return (LINKER_EACH_FUNCTION_NAMEVAL(lf, callback_func, arg));
}

caddr_t
linker_file_lookup_symbol(linker_file_t file, const char *name, int deps)
{
	caddr_t sym;
	int locked;

	locked = sx_xlocked(&kld_sx);
	if (!locked)
		sx_xlock(&kld_sx);
	sym = linker_file_lookup_symbol_internal(file, name, deps);
	if (!locked)
		sx_xunlock(&kld_sx);
	return (sym);
}

/*
	Q&A: symbol的作用是什么？
		symbol的中文翻译是符号，linker中有一个过程叫符号决议，函数应该是对应的这个过程？？
*/
static caddr_t
linker_file_lookup_symbol_internal(linker_file_t file, const char *name,
    int deps)
{
	/*
		从上述定义可以看到，c_linker_sym_t 表示一个const char*，linker_symval_t
		相当于是对 linker_sym_t(不是c打头，char*类型) 的扩充，所以它们两个的核心应该
		就是字符串类型的标识，也就是symbol
	*/
	c_linker_sym_t sym;
	linker_symval_t symval;
	caddr_t address;
	size_t common_size = 0;
	int i;

	sx_assert(&kld_sx, SA_XLOCKED);
	KLD_DPF(SYM, ("linker_file_lookup_symbol: file=%p, name=%s, deps=%d\n",
	    file, name, deps));

	/*
		sym: 用来保存获取到的symbol，通过传入的name参数进行查找
		symval：保存symbol value，包含有name、value和size
	*/
	if (LINKER_LOOKUP_SYMBOL(file, name, &sym) == 0) {
		LINKER_SYMBOL_VALUES(file, sym, &symval);
		if (symval.value == 0)
			/*
			 * For commons, first look them up in the
			 * dependencies and only allocate space if not found
			 * there.
			 */
			common_size = symval.size;
		else {
			KLD_DPF(SYM, ("linker_file_lookup_symbol: symbol"
			    ".value=%p\n", symval.value));
			return (symval.value);
		}
	}

	/* 
		在当前linker file中未找到了symbol并且存在依赖项；或者找到了但是symval.value = 0，
		然后就要执行下面的步骤
		推测：如果symval.value = 0，可能就表示某个变量或者函数是外部引用，这个时候就要去依赖项
		中找相关的定义
	*/
	if (deps) {
		/*
			ndeps 表示linker file依赖项的总数，执行递归查找。这个应该就是对应
			符号决议的执行过程。在当前的目标文件中涉及到一些外部变量的引用，这个时候就要
			去查找依赖项中是否含有这个符号；
		*/
		for (i = 0; i < file->ndeps; i++) {
			address = linker_file_lookup_symbol_internal(
			    file->deps[i], name, 0);
			if (address) {
				KLD_DPF(SYM, ("linker_file_lookup_symbol:"
				    " deps value=%p\n", address));
				return (address);
			}
		}
	}

	/*
		在依赖项中仍然没有找到symbol，接着执行下面的代码。common_size > 0其实就表示前面已经赋值过，
		推测：这种情况可能是我在当前文件中找到了这个符号，但是该符号表示的是一个外部引用，然后在所有
		的依赖项中还是没有找到对应的符号，最后在common队列中查找(common应该是保存的已知公共符号)；如果
		还是没有找到，那就新创建一个以name命名的common_symbol，然后插入到common队列末尾
	*/
	if (common_size > 0) {
		/*
		 * This is a common symbol which was not found in the
		 * dependencies.  We maintain a simple common symbol table in
		 * the file object.
		 * 这是在依赖项中找不到的公共符号。我们在file对象中维护一个简单的公共符号表
		 */
		struct common_symbol *cp;

		STAILQ_FOREACH(cp, &file->common, link) {
			if (strcmp(cp->name, name) == 0) {
				KLD_DPF(SYM, ("linker_file_lookup_symbol:"
				    " old common value=%p\n", cp->address));
				return (cp->address);
			}
		}
		/*
		 * Round the symbol size up to align. 字节对齐
		 */
		common_size = (common_size + sizeof(int) - 1) & -sizeof(int);
		cp = malloc(sizeof(struct common_symbol)
		    + common_size + strlen(name) + 1, M_LINKER,
		    M_WAITOK | M_ZERO);
		cp->address = (caddr_t)(cp + 1);
		cp->name = cp->address + common_size;
		strcpy(cp->name, name);
		bzero(cp->address, common_size);

		STAILQ_INSERT_TAIL(&file->common, cp, link);

		KLD_DPF(SYM, ("linker_file_lookup_symbol: new common"
		    " value=%p\n", cp->address));
		return (cp->address);
	}
	KLD_DPF(SYM, ("linker_file_lookup_symbol: fail\n"));
	return (0);
}

/*
 * Both DDB and stack(9) rely on the kernel linker to provide forward and
 * backward lookup of symbols.  However, DDB and sometimes stack(9) need to
 * do this in a lockfree manner.  We provide a set of internal helper
 * routines to perform these operations without locks, and then wrappers that
 * optionally lock.
 *
 * linker_debug_lookup() is ifdef DDB as currently it's only used by DDB.
 * 调试器相关
 */
#ifdef DDB
static int
linker_debug_lookup(const char *symstr, c_linker_sym_t *sym)
{
	linker_file_t lf;

	TAILQ_FOREACH(lf, &linker_files, link) {
		if (LINKER_LOOKUP_SYMBOL(lf, symstr, sym) == 0)
			return (0);
	}
	return (ENOENT);
}
#endif

/*
	Q&A: 参数value表示的是什么，symbval->value?
*/
static int
linker_debug_search_symbol(caddr_t value, c_linker_sym_t *sym, long *diffp)
{
	linker_file_t lf;
	c_linker_sym_t best, es;
	u_long diff, bestdiff, off;

	best = 0;
	off = (uintptr_t)value;
	bestdiff = off;

	/*
		Q&A: diff越小越好，应该可以理解为差别越小越好，差别表示的是什么？
		从上层函数调用来看，diffp表示的是一个offset值，所以这里的意思是偏移量越小越好？？
	*/
	TAILQ_FOREACH(lf, &linker_files, link) {
		if (LINKER_SEARCH_SYMBOL(lf, value, &es, &diff) != 0)
			continue;
		if (es != 0 && diff < bestdiff) {
			best = es;
			bestdiff = diff;
		}
		if (bestdiff == 0)
			break;
	}

	if (best) {
		*sym = best;
		*diffp = bestdiff;
		return (0);
	} else {
		*sym = 0;
		*diffp = off;
		return (ENOENT);
	}
}

/*
	判断linker_files所管理的所有linker file中是否含有某个symbol value，
	然后应该是要获取sym的symval的值(传指针)
*/
static int
linker_debug_symbol_values(c_linker_sym_t sym, linker_symval_t *symval)
{
	linker_file_t lf;

	TAILQ_FOREACH(lf, &linker_files, link) {
		if (LINKER_SYMBOL_VALUES(lf, sym, symval) == 0)
			return (0);
	}
	return (ENOENT);
}

/*
	首先通过value和offset定位到某一个symbol，然后再通过symbol获取symval.name
*/
static int
linker_debug_search_symbol_name(caddr_t value, char *buf, u_int buflen,
    long *offset)
{
	linker_symval_t symval;
	c_linker_sym_t sym;
	int error;

	*offset = 0;
	error = linker_debug_search_symbol(value, &sym, offset);
	if (error)
		return (error);
	error = linker_debug_symbol_values(sym, &symval);
	if (error)
		return (error);
	strlcpy(buf, symval.name, buflen);
	return (0);
}

/*
 * DDB Helpers.  DDB has to look across multiple files with their own symbol
 * tables and string tables. DDB必须查看多个具有自己的符号表和字符串表的文件
 *
 * Note that we do not obey list locking protocols here.  We really don't need
 * DDB to hang because somebody's got the lock held.  We'll take the chance
 * that the files list is inconsistent instead.
 */
#ifdef DDB
int
linker_ddb_lookup(const char *symstr, c_linker_sym_t *sym)
{

	return (linker_debug_lookup(symstr, sym));
}
#endif

int
linker_ddb_search_symbol(caddr_t value, c_linker_sym_t *sym, long *diffp)
{

	return (linker_debug_search_symbol(value, sym, diffp));
}

int
linker_ddb_symbol_values(c_linker_sym_t sym, linker_symval_t *symval)
{

	return (linker_debug_symbol_values(sym, symval));
}

int
linker_ddb_search_symbol_name(caddr_t value, char *buf, u_int buflen,
    long *offset)
{

	return (linker_debug_search_symbol_name(value, buf, buflen, offset));
}

/*
 * stack(9) helper for non-debugging environemnts.  Unlike DDB helpers, we do
 * obey locking protocols, and offer a significantly less complex interface.
 * 
 * 把symval->name放到buf中
 */
int
linker_search_symbol_name(caddr_t value, char *buf, u_int buflen,
    long *offset)
{
	int error;

	sx_slock(&kld_sx);
	/* linker查找symbol name的时候还要借助调试器？？ */
	error = linker_debug_search_symbol_name(value, buf, buflen, offset);
	sx_sunlock(&kld_sx);
	return (error);
}

/*
 * Syscalls.
 * 执行后获取到的file id赋值给fileid，然后在sys_kldload函数中用于设置td相关属性
 * file在sys_kldload函数中传入的是pathname
 * kld： loadable kernel module
 */
int
kern_kldload(struct thread *td, const char *file, int *fileid)
{
	const char *kldname, *modname;
	linker_file_t lf;
	int error;

	/* 检查thread安全等级和特权 */
	if ((error = securelevel_gt(td->td_ucred, 0)) != 0)
		return (error);

	/* PRIV_KLD_LOAD: 表示可加载内核模块权限 */
	if ((error = priv_check(td, PRIV_KLD_LOAD)) != 0)
		return (error);

	/*
	 * It is possible that kldloaded module will attach a new ifnet,
	 * so vnet context must be set when this ocurs.
	 * 
	 * kldloaded模块可能会附加一个新的ifnet，所以当这个发生时必须设置vnet上下文
	 * 
	 * VNET是虚拟化网络堆栈的技术名称。基本思想是将全局资源（尤其是变量）更改为每个
	 * 网络堆栈资源，并让函数、sysctl、eventhandlers等在正确实例的上下文中访问和
	 * 处理它们。每个（虚拟）网络堆栈都连接到一个监督模块，vnet0是基本系统的无限制默认
	 * 网络堆栈
	 */
	CURVNET_SET(TD_TO_VNET(td));

	/*
	 * If file does not contain a qualified name or any dot in it
	 * (kldname.ko, or kldname.ver.ko) treat it as an interface
	 * name.
	 * 
	 * 如果文件中不包含限定名或任何点(kldname.ko，或kldname.ver.ko)将其视为接口名称.
	 * strchr表示获取某个字符第一次出现的位置，所以 kldname 表示的应该是包含路径名的文件
	 * 名称，或者是包含扩展名： 
	 * 		/###/###  
	 * 		./###/###
	 * 		/###/###.##
	 * 		###.##
	 * modname 应该就是不包含文件路径和扩展名: ###	
	 */
	if (strchr(file, '/') || strchr(file, '.')) {
		kldname = file;
		modname = NULL;
	} else {
		kldname = NULL;
		modname = file;
	}
	/* 带x就表示独占锁 */
	sx_xlock(&kld_sx);

	/*
		将获取到的linker file保存到lf(linker_file**类型)，parent和verinfo都是NULL
	*/
	error = linker_load_module(kldname, modname, NULL, NULL, &lf);
	if (error) {
		sx_xunlock(&kld_sx);
		goto done;
	}
	lf->userrefs++;
	if (fileid != NULL)
		*fileid = lf->id;
	sx_xunlock(&kld_sx);

done:
	CURVNET_RESTORE();
	return (error);
}

/*
	kldload命令？
*/
int
sys_kldload(struct thread *td, struct kldload_args *uap)
{
	char *pathname = NULL;
	int error, fileid;

	td->td_retval[0] = -1;

	pathname = malloc(MAXPATHLEN, M_TEMP, M_WAITOK);

	/*
		从用户空间 uap->file 拷贝数据到内核空间 pathname(kldload)
	*/
	error = copyinstr(uap->file, pathname, MAXPATHLEN, NULL);
	if (error == 0) {
		error = kern_kldload(td, pathname, &fileid);
		if (error == 0)
			td->td_retval[0] = fileid;  /* pcb相关 */
	}
	free(pathname, M_TEMP);
	return (error);
}

int
kern_kldunload(struct thread *td, int fileid, int flags)
{
	linker_file_t lf;
	int error = 0;

	if ((error = securelevel_gt(td->td_ucred, 0)) != 0)
		return (error);

	if ((error = priv_check(td, PRIV_KLD_UNLOAD)) != 0)
		return (error);

	CURVNET_SET(TD_TO_VNET(td));
	sx_xlock(&kld_sx);

	/* 在linker file的全局队列中查找 */
	lf = linker_find_file_by_id(fileid);
	if (lf) {
		KLD_DPF(FILE, ("kldunload: lf->userrefs=%d\n", lf->userrefs));

		if (lf->userrefs == 0) {
			/*
			 * XXX: maybe LINKER_UNLOAD_FORCE should override ?
			 */
			printf("kldunload: attempt to unload file that was"
			    " loaded by the kernel\n");
			error = EBUSY;
		} else {
			lf->userrefs--;
			error = linker_file_unload(lf, flags);

			/* 如果unload出错，还需要执行refs++ */
			if (error)
				lf->userrefs++;
		}
	} else
		error = ENOENT;
	sx_xunlock(&kld_sx);

	CURVNET_RESTORE();
	return (error);
}

/* kldunload命令 */
int
sys_kldunload(struct thread *td, struct kldunload_args *uap)
{

	return (kern_kldunload(td, uap->fileid, LINKER_UNLOAD_NORMAL));
}

/* kldunload -f */
int
sys_kldunloadf(struct thread *td, struct kldunloadf_args *uap)
{

	if (uap->flags != LINKER_UNLOAD_NORMAL &&
	    uap->flags != LINKER_UNLOAD_FORCE)
		return (EINVAL);
	return (kern_kldunload(td, uap->fileid, uap->flags));
}

int
sys_kldfind(struct thread *td, struct kldfind_args *uap)
{
	char *pathname;
	const char *filename;
	linker_file_t lf;
	int error;

#ifdef MAC
	error = mac_kld_check_stat(td->td_ucred);
	if (error)
		return (error);
#endif

	td->td_retval[0] = -1;

	pathname = malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
	if ((error = copyinstr(uap->file, pathname, MAXPATHLEN, NULL)) != 0)
		goto out;

	/* 可以考虑一下这里的filename带不带扩展名 */
	filename = linker_basename(pathname);
	sx_xlock(&kld_sx);
	lf = linker_find_file_by_name(filename);
	if (lf)
		td->td_retval[0] = lf->id;
	else
		error = ENOENT;
	sx_xunlock(&kld_sx);
out:
	free(pathname, M_TEMP);
	return (error);
}

int
sys_kldnext(struct thread *td, struct kldnext_args *uap)
{
	linker_file_t lf;
	int error = 0;

#ifdef MAC
	error = mac_kld_check_stat(td->td_ucred);
	if (error)
		return (error);
#endif

	sx_xlock(&kld_sx);
	if (uap->fileid == 0)
		lf = TAILQ_FIRST(&linker_files);
	else {
		lf = linker_find_file_by_id(uap->fileid);
		if (lf == NULL) {
			error = ENOENT;
			goto out;
		}
		lf = TAILQ_NEXT(lf, link);
	}

	/* Skip partially loaded files. 跳过部分加载的文件 */
	while (lf != NULL && !(lf->flags & LINKER_FILE_LINKED))
		lf = TAILQ_NEXT(lf, link);

	if (lf)
		td->td_retval[0] = lf->id;
	else
		td->td_retval[0] = 0;
out:
	sx_xunlock(&kld_sx);
	return (error);
}

/* kldstat 命令 */
int
sys_kldstat(struct thread *td, struct kldstat_args *uap)
{
	struct kld_file_stat *stat;
	int error, version;

	/*
	 * Check the version of the user's structure.
	 */
	if ((error = copyin(&uap->stat->version, &version, sizeof(version)))
	    != 0)
		return (error);
	if (version != sizeof(struct kld_file_stat_1) &&
	    version != sizeof(struct kld_file_stat))
		return (EINVAL);

	stat = malloc(sizeof(*stat), M_TEMP, M_WAITOK | M_ZERO);
	error = kern_kldstat(td, uap->fileid, stat);
	if (error == 0)
		error = copyout(stat, uap->stat, version);
	free(stat, M_TEMP);
	return (error);
}

int
kern_kldstat(struct thread *td, int fileid, struct kld_file_stat *stat)
{
	linker_file_t lf;
	int namelen;
#ifdef MAC
	int error;

	error = mac_kld_check_stat(td->td_ucred);
	if (error)
		return (error);
#endif

	sx_xlock(&kld_sx);
	lf = linker_find_file_by_id(fileid);
	if (lf == NULL) {
		sx_xunlock(&kld_sx);
		return (ENOENT);
	}

	/* Version 1 fields: */
	namelen = strlen(lf->filename) + 1;
	if (namelen > sizeof(stat->name))
		namelen = sizeof(stat->name);
	bcopy(lf->filename, &stat->name[0], namelen);
	stat->refs = lf->refs;
	stat->id = lf->id;
	stat->address = lf->address;
	stat->size = lf->size;
	/* Version 2 fields: */
	namelen = strlen(lf->pathname) + 1;
	if (namelen > sizeof(stat->pathname))
		namelen = sizeof(stat->pathname);
	bcopy(lf->pathname, &stat->pathname[0], namelen);
	sx_xunlock(&kld_sx);

	td->td_retval[0] = 0;
	return (0);
}

#ifdef DDB
DB_COMMAND(kldstat, db_kldstat)
{
	linker_file_t lf;

#define	POINTER_WIDTH	((int)(sizeof(void *) * 2 + 2))
	db_printf("Id Refs Address%*c Size     Name\n", POINTER_WIDTH - 7, ' ');
#undef	POINTER_WIDTH
	TAILQ_FOREACH(lf, &linker_files, link) {
		if (db_pager_quit)
			return;
		db_printf("%2d %4d %p %-8zx %s\n", lf->id, lf->refs,
		    lf->address, lf->size, lf->filename);
	}
}
#endif /* DDB */

int
sys_kldfirstmod(struct thread *td, struct kldfirstmod_args *uap)
{
	linker_file_t lf;
	module_t mp;
	int error = 0;

#ifdef MAC
	error = mac_kld_check_stat(td->td_ucred);
	if (error)
		return (error);
#endif

	sx_xlock(&kld_sx);
	lf = linker_find_file_by_id(uap->fileid);
	if (lf) {
		MOD_SLOCK;
		mp = TAILQ_FIRST(&lf->modules);
		if (mp != NULL)
			td->td_retval[0] = module_getid(mp);
		else
			td->td_retval[0] = 0;
		MOD_SUNLOCK;
	} else
		error = ENOENT;
	sx_xunlock(&kld_sx);
	return (error);
}

int
sys_kldsym(struct thread *td, struct kldsym_args *uap)
{
	char *symstr = NULL;
	c_linker_sym_t sym;
	linker_symval_t symval;
	linker_file_t lf;
	struct kld_sym_lookup lookup;
	int error = 0;

#ifdef MAC
	error = mac_kld_check_stat(td->td_ucred);
	if (error)
		return (error);
#endif

	if ((error = copyin(uap->data, &lookup, sizeof(lookup))) != 0)
		return (error);
	if (lookup.version != sizeof(lookup) ||
	    uap->cmd != KLDSYM_LOOKUP)
		return (EINVAL);
	symstr = malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
	if ((error = copyinstr(lookup.symname, symstr, MAXPATHLEN, NULL)) != 0)
		goto out;
	sx_xlock(&kld_sx);
	if (uap->fileid != 0) {
		lf = linker_find_file_by_id(uap->fileid);
		if (lf == NULL)
			error = ENOENT;
		else if (LINKER_LOOKUP_SYMBOL(lf, symstr, &sym) == 0 &&
		    LINKER_SYMBOL_VALUES(lf, sym, &symval) == 0) {
			lookup.symvalue = (uintptr_t) symval.value;
			lookup.symsize = symval.size;
			error = copyout(&lookup, uap->data, sizeof(lookup));
		} else
			error = ENOENT;
	} else {
		TAILQ_FOREACH(lf, &linker_files, link) {
			if (LINKER_LOOKUP_SYMBOL(lf, symstr, &sym) == 0 &&
			    LINKER_SYMBOL_VALUES(lf, sym, &symval) == 0) {
				lookup.symvalue = (uintptr_t)symval.value;
				lookup.symsize = symval.size;
				error = copyout(&lookup, uap->data,
				    sizeof(lookup));
				break;
			}
		}
		if (lf == NULL)
			error = ENOENT;
	}
	sx_xunlock(&kld_sx);
out:
	free(symstr, M_TEMP);
	return (error);
}

/*
 * Preloaded module support
 */

static modlist_t
modlist_lookup(const char *name, int ver)
{
	modlist_t mod;

	/*
		遍历整个 found_modules(管理所有已知的module)，查看是否有符合version
		的module元素存在
	*/
	TAILQ_FOREACH(mod, &found_modules, link) {
		if (strcmp(mod->name, name) == 0 &&
		    (ver == 0 || mod->version == ver))
			return (mod);
	}
	return (NULL);
}

/*
	name参数应该就只是文件名称，没有路径，也不包含扩展名，mod_depend是module所
	支持的版本的信息
*/
static modlist_t
modlist_lookup2(const char *name, const struct mod_depend *verinfo)
{
	modlist_t mod, bestmod;
	int ver;

	/*
		如果没有版本信息，调用 modlist_lookup来进行处理；如果有版本信息的话，
		找到最佳匹配的 module
	*/
	if (verinfo == NULL)
		return (modlist_lookup(name, 0));

	bestmod = NULL;

	/*
		首先要对名字进行匹配，然后比较参数中提供的版本信息和 module_list中保存的
		最佳匹配版本是否一致。如果一致，就返回；如果不一致，接着判断参数提供的版本
		是否在 module要求的最高版本和最低版本之间。如果仍然满足，那就再比较ver跟
		当前 bestmod谁的版本信息更高，如果ver更高，那就更新bestmod，直到循环结束

		其实就可以概括为如果参数给出的版本信息刚好是最佳版本，那就直接返回；如果没有
		最佳匹配版本，那就找一个支持最新版本的module
	*/
	TAILQ_FOREACH(mod, &found_modules, link) {
		if (strcmp(mod->name, name) != 0)
			continue;
		ver = mod->version;
		if (ver == verinfo->md_ver_preferred)
			return (mod);
		if (ver >= verinfo->md_ver_minimum &&
		    ver <= verinfo->md_ver_maximum &&
		    (bestmod == NULL || ver > bestmod->version))
			bestmod = mod;
	}
	return (bestmod);
}

/*  */
static modlist_t
modlist_newmodule(const char *modname, int version, linker_file_t container)
{
	modlist_t mod;

	mod = malloc(sizeof(struct modlist), M_LINKER, M_NOWAIT | M_ZERO);
	if (mod == NULL)
		panic("no memory for module list");
	mod->container = container;
	mod->name = modname;
	mod->version = version;
	TAILQ_INSERT_TAIL(&found_modules, mod, link);	/* 向found_modules中插入新的module */
	return (mod);
}

/*
	mod_metadata 应该是linker file中的一个section，我们通过定义这个section的start和stop来对该段
	进行遍历，找到特定的元素(参考driver)
*/
static void
linker_addmodules(linker_file_t lf, struct mod_metadata **start,
    struct mod_metadata **stop, int preload)
{
	struct mod_metadata *mp, **mdp;
	const char *modname;
	int ver;

	for (mdp = start; mdp < stop; mdp++) {
		mp = *mdp;
		if (mp->md_type != MDT_VERSION)
			continue;
		modname = mp->md_cval;
		ver = ((const struct mod_version *)mp->md_data)->mv_version;
		if (modlist_lookup(modname, ver) != NULL) {
			printf("module %s already present!\n", modname);
			/* XXX what can we do? this is a build error. :-( */
			continue;
		}
		modlist_newmodule(modname, ver, lf);
	}
}

/* 
	执行的顺序是 middle，third之后进行的，这时候 symbol table 和 .dynamic section都已经
	做了相应的处理，后边应该就可以使用其中的一些信息
*/
static void
linker_preload(void *arg)
{
	caddr_t modptr;
	const char *modname, *nmodname;
	char *modtype;
	linker_file_t lf, nlf;
	linker_class_t lc;
	int error;
	linker_file_list_t loaded_files;
	linker_file_list_t depended_files;
	struct mod_metadata *mp, *nmp;
	struct mod_metadata **start, **stop, **mdp, **nmdp;
	const struct mod_depend *verinfo;
	int nver;
	int resolves;
	modlist_t mod;
	struct sysinit **si_start, **si_stop;

	/* 初始化管理链表 */
	TAILQ_INIT(&loaded_files);
	TAILQ_INIT(&depended_files);
	TAILQ_INIT(&found_modules);
	error = 0;

	modptr = NULL;
	sx_xlock(&kld_sx);	/* linker lock，只针对linker？？ */

	/*
		通过gdb调试发现，x86_64下需要预加载一共是两个文件：
			/boot/kernel/kernel
			/boot/entropy
	*/
	while ((modptr = preload_search_next_name(modptr)) != NULL) {
		modname = (char *)preload_search_info(modptr, MODINFO_NAME);
		modtype = (char *)preload_search_info(modptr, MODINFO_TYPE);
		if (modname == NULL) {
			printf("Preloaded module at %p does not have a"
			    " name!\n", modptr);
			continue;
		}
		if (modtype == NULL) {
			printf("Preloaded module at %p does not have a type!\n",
			    modptr);
			continue;
		}
		if (bootverbose)
			printf("Preloaded %s \"%s\" at %p.\n", modtype, modname,
			    modptr);
		lf = NULL;

		/* 
			首先遍历classes进行一轮preload，将执行完操作获取到的linker file添加到loaded_files 中；
			classes中包含了两种类型，link_elf和link_elf_obj，所以说它会执行两次preload，要比较一下
			两次 preload 的异同；second 步骤首先是对 ELF Header、section table和symbol table等等
			做了一些判断，看看属性是否正确，table是否存在，然后就是做了一些重定位的操作；third 步骤目前看
			主要是对动态链接相关的，比如 dynamic symbol table 处理和 .dynamic section 的解析等；但是
			它们都涉及到了重定位，所以还是要比较一下两者流程上有什么异同 
		*/
		TAILQ_FOREACH(lc, &classes, link) {
			error = LINKER_LINK_PRELOAD(lc, modname, &lf);
			if (!error)
				break;
			lf = NULL;
		}
		/* 把两个派生出的 linker file 都放到 loaded_files */
		if (lf)
			TAILQ_INSERT_TAIL(&loaded_files, lf, loaded);
	}

	/*
	 * First get a list of stuff in the kernel.
	 * 首先在内核中获取一个内容列表
	 * 可以参考link_elf_obj和link_elf文件中对应函数的逻辑，其实就是在 linker_kernel_file 关联的文件的
	 * prog类型的section中找到以 "modmetadata_set" 命名的link_set(驱动模块貌似就是以这个命名的，然后经
	 * link_set中代码的处理，添加到某一个section中，很可能是.data section)
	 * 
	 * 与 linker_kernel_file 关联的 link_elf_class 是 link_elf.c文件中的那个，可以参考 link_elf_init
	 * 函数代码实现；lookup_set 其实就是查找以 modmetadata_set 命名的set，找到set的范围(会包含有很多module，
	 * 目前来看都是通过 SYSINIT 宏加入的)。遍历整个set，把其中的 module 全部提取出来，放到 found_modules中
	 * 
	 * 该文件应该就是为了找到以 MDT_SETNAME 命名的 segment或者section，得到起始和终止地址
	 */
	if (linker_file_lookup_set(linker_kernel_file, MDT_SETNAME, &start,
	    &stop, NULL) == 0)
		linker_addmodules(linker_kernel_file, start, stop, 1);	// 填充found_modules

	/*
	 * This is a once-off kinky bubble sort to resolve relocation
	 * dependency requirements.
	 * 这是一种解决重定位依赖性需求的一次性古怪的冒泡排序，然后对loaded_files进行遍历
	 */
restart:
	TAILQ_FOREACH(lf, &loaded_files, loaded) {
		error = linker_file_lookup_set(lf, MDT_SETNAME, &start,
		    &stop, NULL);
		/*
		 * First, look to see if we would successfully link with this
		 * stuff.
		 */
		resolves = 1;	/* unless we know otherwise */
		if (!error) {
			for (mdp = start; mdp < stop; mdp++) {
				mp = *mdp;
				/* 
					当 module 的类型不是 MDT_DEPEND，继续执行下一个循环，也就是说这里要处理
					MDT_DEPEND 类型的 module；前面 MODULE_DECLARE 的时候，会有另外一个宏定义,
					MDT_DEPEND，通过注释可以看到，这个宏定义的 module 是依赖于 kernel module，
					所以这里应该是对这一类的 module 进行处理 
				*/
				if (mp->md_type != MDT_DEPEND)
					continue;

				/* 找到了MDT_DEPEND类型的module，执行下面的步骤 */
				modname = mp->md_cval;	/* string label */
				verinfo = mp->md_data;	/* specific data */

				for (nmdp = start; nmdp < stop; nmdp++) {
					nmp = *nmdp;
					/* 逻辑同上，查找 MDT_VERSION 类型的 module */
					if (nmp->md_type != MDT_VERSION)
						continue;
					nmodname = nmp->md_cval;
					if (strcmp(modname, nmodname) == 0)
						break;
				}	// enf inside for

				if (nmdp < stop)   /* it's a self reference */
					continue;

				/*
				 * ok, the module isn't here yet, we
				 * are not finished
				 */
				if (modlist_lookup2(modname, verinfo) == NULL)
					resolves = 0;
			}
		}
		/*
		 * OK, if we found our modules, we can link.  So, "provide"
		 * the modules inside and add it to the end of the link order
		 * list.
		 * 如果通过上述步骤，还是没有找到相应的 module，那就再继续执行下面的步骤
		 */
		if (resolves) {
			if (!error) {
				for (mdp = start; mdp < stop; mdp++) {
					mp = *mdp;
					/* 
						仍然查找 MDT_VERSION 类型的 module，把找到的 module 的 md_data 成员
						强制类型转换为 mod_version，得到 version 信息，再通过 modname 和 version
						查找
					*/
					if (mp->md_type != MDT_VERSION)
						continue;
					modname = mp->md_cval;
					nver = ((const struct mod_version *)
					    mp->md_data)->mv_version;

					if (modlist_lookup(modname,
					    nver) != NULL) {
						printf("module %s already"
						    " present!\n", modname);
						TAILQ_REMOVE(&loaded_files,
						    lf, loaded);
						linker_file_unload(lf,
						    LINKER_UNLOAD_FORCE);
						/* we changed tailq next ptr */
						goto restart;
					}
					modlist_newmodule(modname, nver, lf);
				}
			}
			TAILQ_REMOVE(&loaded_files, lf, loaded);
			TAILQ_INSERT_TAIL(&depended_files, lf, loaded);
			/*
			 * Since we provided modules, we need to restart the
			 * sort so that the previous files that depend on us
			 * have a chance. Also, we've busted the tailq next
			 * pointer with the REMOVE.
			 */
			goto restart;
		}
	}

	/*
	 * At this point, we check to see what could not be resolved..
	 */
	while ((lf = TAILQ_FIRST(&loaded_files)) != NULL) {
		TAILQ_REMOVE(&loaded_files, lf, loaded);
		printf("KLD file %s is missing dependencies\n", lf->filename);
		linker_file_unload(lf, LINKER_UNLOAD_FORCE);
	}

	/*
	 * We made it. Finish off the linking in the order we determined.
	 */
	TAILQ_FOREACH_SAFE(lf, &depended_files, loaded, nlf) {
		if (linker_kernel_file) {
			linker_kernel_file->refs++;
			error = linker_file_add_dependency(lf,
			    linker_kernel_file);
			if (error)
				panic("cannot add dependency");
		}
		error = linker_file_lookup_set(lf, MDT_SETNAME, &start,
		    &stop, NULL);
		if (!error) {
			for (mdp = start; mdp < stop; mdp++) {
				mp = *mdp;
				if (mp->md_type != MDT_DEPEND)
					continue;
				modname = mp->md_cval;
				verinfo = mp->md_data;
				mod = modlist_lookup2(modname, verinfo);
				if (mod == NULL) {
					printf("KLD file %s - cannot find "
					    "dependency \"%s\"\n",
					    lf->filename, modname);
					goto fail;
				}
				/* Don't count self-dependencies */
				if (lf == mod->container)
					continue;
				mod->container->refs++;
				error = linker_file_add_dependency(lf,
				    mod->container);
				if (error)
					panic("cannot add dependency");
			}
		}
		/*
		 * Now do relocation etc using the symbol search paths
		 * established by the dependencies
		 */
		error = LINKER_LINK_PRELOAD_FINISH(lf);
		if (error) {
			printf("KLD file %s - could not finalize loading\n",
			    lf->filename);
			goto fail;
		}
		linker_file_register_modules(lf);
		if (!TAILQ_EMPTY(&lf->modules))
			lf->flags |= LINKER_FILE_MODULES;
		if (linker_file_lookup_set(lf, "sysinit_set", &si_start,
		    &si_stop, NULL) == 0)
			sysinit_add(si_start, si_stop);
		linker_file_register_sysctls(lf, true);
		lf->flags |= LINKER_FILE_LINKED;
		continue;
fail:
		TAILQ_REMOVE(&depended_files, lf, loaded);
		linker_file_unload(lf, LINKER_UNLOAD_FORCE);
	}
	sx_xunlock(&kld_sx);
	/* woohoo! we made it! */
}

SYSINIT(preload, SI_SUB_KLD, SI_ORDER_MIDDLE, linker_preload, NULL);

/*
 * Handle preload files that failed to load any modules.
 */
static void
linker_preload_finish(void *arg)
{
	linker_file_t lf, nlf;

	sx_xlock(&kld_sx);
	TAILQ_FOREACH_SAFE(lf, &linker_files, link, nlf) {
		/*
		 * If all of the modules in this file failed to load, unload
		 * the file and return an error of ENOEXEC.  (Parity with
		 * linker_load_file.)
		 */
		if ((lf->flags & LINKER_FILE_MODULES) != 0 &&
		    TAILQ_EMPTY(&lf->modules)) {
			linker_file_unload(lf, LINKER_UNLOAD_FORCE);
			continue;
		}

		lf->flags &= ~LINKER_FILE_MODULES;
		lf->userrefs++;	/* so we can (try to) kldunload it */
	}
	sx_xunlock(&kld_sx);
}

/*
 * Attempt to run after all DECLARE_MODULE SYSINITs.  Unfortunately they can be
 * scheduled at any subsystem and order, so run this as late as possible.  init
 * becomes runnable in SI_SUB_KTHREAD_INIT, so go slightly before that.
 */
SYSINIT(preload_finish, SI_SUB_KTHREAD_INIT - 100, SI_ORDER_MIDDLE,
    linker_preload_finish, NULL);

/*
 * Search for a not-loaded module by name.
 *
 * Modules may be found in the following locations:
 *
 * - preloaded (result is just the module name) - on disk (result is full path
 * to module)
 *
 * If the module name is qualified in any way (contains path, etc.) the we
 * simply return a copy of it.
 *
 * The search path can be manipulated via sysctl.  Note that we use the ';'
 * character as a separator to be consistent with the bootloader.
 */

static char linker_hintfile[] = "linker.hints";
static char linker_path[MAXPATHLEN] = "/boot/kernel;/boot/modules";

SYSCTL_STRING(_kern, OID_AUTO, module_path, CTLFLAG_RWTUN, linker_path,
    sizeof(linker_path), "module load search path");

TUNABLE_STR("module_path", linker_path, sizeof(linker_path));

/* 文件扩展名列表 */
static char *linker_ext_list[] = {
	"",
	".ko",
	NULL
};

/*
 * Check if file actually exists either with or without extension listed in
 * the linker_ext_list. (probably should be generic for the rest of the
 * kernel)
 * 查找文件(可能会包含有linker_ext_list中的扩展名)是否存在
 * 从上级函数调用情况来看，name指的是不包含路径的文件名
 */
static char *
linker_lookup_file(const char *path, int pathlen, const char *name,
    int namelen, struct vattr *vap)
{
	/*
		vap和nd都是跟vnode相关，好像每一个每一个文件或者文件夹都会包含一些跟
		vnode相关的属性
	*/
	struct nameidata nd;
	struct thread *td = curthread;	/* XXX */
	char *result, **cpp, *sep;
	int error, len, extlen, reclen, flags;
	enum vtype type;

	extlen = 0;
	for (cpp = linker_ext_list; *cpp; cpp++) {
		len = strlen(*cpp);
		if (len > extlen)
			extlen = len;
	}
	extlen++;		/* trailing '\0' 最大的扩展名长度 + 1 */

	/* 如果文件路径的最后不为"/",需要把"/"给加上 */
	sep = (path[pathlen - 1] != '/') ? "/" : "";

	/* 
		计算出路径+文件名+最大扩展名的长度，然后再 + 1(给末尾\0留一个位置)，
		就是计算出了完整的路径
	*/
	reclen = pathlen + strlen(sep) + namelen + extlen + 1;
	result = malloc(reclen, M_LINKER, M_WAITOK);
	for (cpp = linker_ext_list; *cpp; cpp++) {
		snprintf(result, reclen, "%.*s%s%.*s%s", pathlen, path, sep,
		    namelen, name, *cpp);
		/*
		 * Attempt to open the file, and return the path if
		 * we succeed and it's a regular file.
		 * 测试文件是否是普通文件并且可以成功打开
		 */
		NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, result, td);
		flags = FREAD;
		error = vn_open(&nd, &flags, 0, NULL);
		if (error == 0) {
			NDFREE(&nd, NDF_ONLY_PNBUF);
			type = nd.ni_vp->v_type;
			if (vap)
				VOP_GETATTR(nd.ni_vp, vap, td->td_ucred);
			VOP_UNLOCK(nd.ni_vp, 0);
			vn_close(nd.ni_vp, FREAD, td->td_ucred, td);
			if (type == VREG)
				return (result);
		}
	}
	free(result, M_LINKER);
	return (NULL);
}

#define	INT_ALIGN(base, ptr)	ptr =					\
	(base) + roundup2((ptr) - (base), sizeof(int))

/*
 * Lookup KLD which contains requested module in the "linker.hints" file. If
 * version specification is available, then try to find the best KLD.
 * Otherwise just find the latest one.
 * 
 * 查找“中包含请求的模块的KLD”链接器提示“文件。如果版本规范可用，那么尝试找到最好的KLD。否则就找最新的
 * 
 * 从该注释可以看出，hint机制所实现的功能就是给操作系统一个提示，说明一下这个地方倾向于使用某个函数
 * 或者某个模块等
 */
static char *
linker_hints_lookup(const char *path, int pathlen, const char *modname,
    int modnamelen, const struct mod_depend *verinfo)
{
	struct thread *td = curthread;	/* XXX */
	struct ucred *cred = td ? td->td_ucred : NULL;
	struct nameidata nd;
	struct vattr vattr, mattr;
	u_char *hints = NULL;
	u_char *cp, *recptr, *bufend, *result, *best, *pathbuf, *sep;
	int error, ival, bestver, *intp, found, flags, clen, blen;
	ssize_t reclen;

	result = NULL;
	bestver = found = 0;

	sep = (path[pathlen - 1] != '/') ? "/" : "";
	reclen = imax(modnamelen, strlen(linker_hintfile)) + pathlen +
	    strlen(sep) + 1;
	pathbuf = malloc(reclen, M_LINKER, M_WAITOK);
	snprintf(pathbuf, reclen, "%.*s%s%s", pathlen, path, sep,
	    linker_hintfile);

	NDINIT(&nd, LOOKUP, NOFOLLOW, UIO_SYSSPACE, pathbuf, td);
	flags = FREAD;
	error = vn_open(&nd, &flags, 0, NULL);
	if (error)
		goto bad;
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (nd.ni_vp->v_type != VREG)
		goto bad;
	best = cp = NULL;
	error = VOP_GETATTR(nd.ni_vp, &vattr, cred);
	if (error)
		goto bad;
	/*
	 * XXX: we need to limit this number to some reasonable value
	 */
	if (vattr.va_size > LINKER_HINTS_MAX) {
		printf("hints file too large %ld\n", (long)vattr.va_size);
		goto bad;
	}
	hints = malloc(vattr.va_size, M_TEMP, M_WAITOK);
	error = vn_rdwr(UIO_READ, nd.ni_vp, (caddr_t)hints, vattr.va_size, 0,
	    UIO_SYSSPACE, IO_NODELOCKED, cred, NOCRED, &reclen, td);
	if (error)
		goto bad;
	VOP_UNLOCK(nd.ni_vp, 0);
	vn_close(nd.ni_vp, FREAD, cred, td);
	nd.ni_vp = NULL;
	if (reclen != 0) {
		printf("can't read %zd\n", reclen);
		goto bad;
	}
	intp = (int *)hints;
	ival = *intp++;
	if (ival != LINKER_HINTS_VERSION) {
		printf("hints file version mismatch %d\n", ival);
		goto bad;
	}
	bufend = hints + vattr.va_size;
	recptr = (u_char *)intp;
	clen = blen = 0;
	while (recptr < bufend && !found) {
		intp = (int *)recptr;
		reclen = *intp++;
		ival = *intp++;
		cp = (char *)intp;
		switch (ival) {
		case MDT_VERSION:
			clen = *cp++;
			if (clen != modnamelen || bcmp(cp, modname, clen) != 0)
				break;
			cp += clen;
			INT_ALIGN(hints, cp);
			ival = *(int *)cp;
			cp += sizeof(int);
			clen = *cp++;
			if (verinfo == NULL ||
			    ival == verinfo->md_ver_preferred) {
				found = 1;
				break;
			}
			if (ival >= verinfo->md_ver_minimum &&
			    ival <= verinfo->md_ver_maximum &&
			    ival > bestver) {
				bestver = ival;
				best = cp;
				blen = clen;
			}
			break;
		default:
			break;
		}
		recptr += reclen + sizeof(int);
	}
	/*
	 * Finally check if KLD is in the place
	 */
	if (found)
		result = linker_lookup_file(path, pathlen, cp, clen, &mattr);
	else if (best)
		result = linker_lookup_file(path, pathlen, best, blen, &mattr);

	/*
	 * KLD is newer than hints file. What we should do now?
	 */
	if (result && timespeccmp(&mattr.va_mtime, &vattr.va_mtime, >))
		printf("warning: KLD '%s' is newer than the linker.hints"
		    " file\n", result);
bad:
	free(pathbuf, M_LINKER);
	if (hints)
		free(hints, M_TEMP);
	if (nd.ni_vp != NULL) {
		VOP_UNLOCK(nd.ni_vp, 0);
		vn_close(nd.ni_vp, FREAD, cred, td);
	}
	/*
	 * If nothing found or hints is absent - fallback to the old
	 * way by using "kldname[.ko]" as module name.
	 */
	if (!found && !bestver && result == NULL)
		result = linker_lookup_file(path, pathlen, modname,
		    modnamelen, NULL);
	return (result);
}

/*
 * Lookup KLD which contains requested module in the all directories.
 */
static char *
linker_search_module(const char *modname, int modnamelen,
    const struct mod_depend *verinfo)
{
	char *cp, *ep, *result;

	/*
	 * traverse the linker path
	 */
	for (cp = linker_path; *cp; cp = ep + 1) {
		/* find the end of this component */
		for (ep = cp; (*ep != 0) && (*ep != ';'); ep++);
		result = linker_hints_lookup(cp, ep - cp, modname,
		    modnamelen, verinfo);
		if (result != NULL)
			return (result);
		if (*ep == 0)
			break;
	}
	return (NULL);
}

/*
 * Search for module in all directories listed in the linker_path.
 */
static char *
linker_search_kld(const char *name)
{
	char *cp, *ep, *result;
	int len;

	/* 
		qualified at all? 
		如果kldname是包含有完整路径的filename，就直接返回filename，如果没有包含
		完整的路径，只是###.ko这种类型，就到默认路径下去查找
	*/
	if (strchr(name, '/'))
		return (strdup(name, M_LINKER)); 	/* M_LINKER：自定义的malloc类型 */

	/* traverse the linker path */
	len = strlen(name);
	for (ep = linker_path; *ep; ep++) {
		cp = ep;
		/* 
			find the end of this component 
			/boot/kernel   /boot/modules
		*/
		for (; *ep != 0 && *ep != ';'; ep++);
		result = linker_lookup_file(cp, ep - cp, name, len, NULL);
		if (result != NULL)
			return (result);
	}
	return (NULL);
}

/*
	linker_search_path
	提取文件名+扩展名
*/
static const char *
linker_basename(const char *path)
{
	const char *filename;

	filename = strrchr(path, '/');
	if (filename == NULL)
		return path;
	if (filename[1])
		filename++;
	return (filename);
}

#ifdef HWPMC_HOOKS
/*
 * Inform hwpmc about the set of kernel modules currently loaded.
 * 通知hwpmc当前加载的内核模块集
 */
void *
linker_hwpmc_list_objects(void)
{
	linker_file_t lf;
	struct pmckern_map_in *kobase;
	int i, nmappings;

	nmappings = 0;
	sx_slock(&kld_sx);
	TAILQ_FOREACH(lf, &linker_files, link)
		nmappings++;

	/* Allocate nmappings + 1 entries. */
	kobase = malloc((nmappings + 1) * sizeof(struct pmckern_map_in),
	    M_LINKER, M_WAITOK | M_ZERO);
	i = 0;
	TAILQ_FOREACH(lf, &linker_files, link) {

		/* Save the info for this linker file. */
		kobase[i].pm_file = lf->filename;
		kobase[i].pm_address = (uintptr_t)lf->address;
		i++;
	}
	sx_sunlock(&kld_sx);

	KASSERT(i > 0, ("linker_hpwmc_list_objects: no kernel objects?"));

	/* The last entry of the malloced area comprises of all zeros. */
	KASSERT(kobase[i].pm_file == NULL,
	    ("linker_hwpmc_list_objects: last object not NULL"));

	return ((void *)kobase);
}
#endif

/*
 * Find a file which contains given module and load it, if "parent" is not
 * NULL, register a reference to it.
 * parameter：
 * 		lfpp：linker file pointer pointer
 */
static int
linker_load_module(const char *kldname, const char *modname,
    struct linker_file *parent, const struct mod_depend *verinfo,
    struct linker_file **lfpp)
{
	/* 
		GDB调试传入的参数：
			kldname: intpm.ko
			modname: 0x0
			parent: 0x0
			verinfo: 0x0
	*/
	linker_file_t lfdep;
	const char *filename;
	char *pathname;
	int error;

	/* 
		好像是编译的时候配置一些选项(SA_XLOCKED)，sx_assert 就是对这些配置进行检测，
		如果不符合就会报错
	*/
	sx_assert(&kld_sx, SA_XLOCKED);

	/*
		如果没有给出module name，那就通过kldname进行查找；如果给出了module name，
		就通过module name来进行查找。目的还是要获取pathname，需要着重比较一下 linker_search_kld
		和 linker_search_module 处理逻辑上的区别
	*/
	if (modname == NULL) {
		/*
 		 * We have to load KLD
 		 */
		KASSERT(verinfo == NULL, ("linker_load_module: verinfo"
		    " is not NULL"));

		/* 在/boot/modules 和 /boot/kernel 中查找需要加载的文件 */
		pathname = linker_search_kld(kldname);
	} else {
		if (modlist_lookup2(modname, verinfo) != NULL)
			return (EEXIST);
		if (kldname != NULL)
			pathname = strdup(kldname, M_LINKER);
		else if (rootvnode == NULL)
			pathname = NULL;
		else
			/*
			 * Need to find a KLD with required module
			 */
			pathname = linker_search_module(modname,
			    strlen(modname), verinfo);
	}

	/*
		如果找到了相应的文件并且通过测试，函数就会返回文件的完整路径；
		如果没有找到文件或者测试失败，那就返回NULL
	*/
	if (pathname == NULL)
		return (ENOENT);

	/*
	 * Can't load more than one file with the same basename XXX:
	 * Actually it should be possible to have multiple KLDs with
	 * the same basename but different path because they can
	 * provide different versions of the same modules.
	 * 
	 * 不能加载多个具有相同基名XXX的文件：实际上应该可以有多个basename相同但路径不同的kld，
	 * 因为它们可以提供相同模块的不同版本
	 */
	filename = linker_basename(pathname);

	/*
		如果找到了，返回该文件已经存在；如果没有找到，就根据 linker file 的 dependence 来创建
		相应的module
		从这里可以看出，其实我们所需要链接的文件都会保存到 linker_files 中统一管理，也方便以后对于
		已经链接的文件的查找
	*/
	if (linker_find_file_by_name(filename))
		error = EEXIST;
	else do {
		error = linker_load_file(pathname, &lfdep);
		if (error)
			break;
		if (modname && verinfo &&
		    modlist_lookup2(modname, verinfo) == NULL) {
			linker_file_unload(lfdep, LINKER_UNLOAD_FORCE);
			error = ENOENT;
			break;
		}
		if (parent) {
			error = linker_file_add_dependency(parent, lfdep);
			if (error)
				break;
		}
		if (lfpp)
			*lfpp = lfdep;
	} while (0);
	free(pathname, M_LINKER);
	return (error);
}

/*
 * This routine is responsible for finding dependencies of userland initiated
 * kldload(2)'s of files.
 */
int
linker_load_dependencies(linker_file_t lf)
{
	linker_file_t lfdep;
	struct mod_metadata **start, **stop, **mdp, **nmdp;
	struct mod_metadata *mp, *nmp;
	const struct mod_depend *verinfo;
	modlist_t mod;
	const char *modname, *nmodname;
	int ver, error = 0;

	/*
	 * All files are dependent on /kernel.
	 * 所有文件都依赖于 /kernel
	 */
	sx_assert(&kld_sx, SA_XLOCKED);
	if (linker_kernel_file) {
		linker_kernel_file->refs++;
		error = linker_file_add_dependency(lf, linker_kernel_file);
		if (error)
			return (error);
	}
	/* 还是需要对linker_set进行解析，找到所有的module */
	if (linker_file_lookup_set(lf, MDT_SETNAME, &start, &stop,
	    NULL) != 0)
		return (0);

	for (mdp = start; mdp < stop; mdp++) {
		mp = *mdp;
		if (mp->md_type != MDT_VERSION)
			continue;
		modname = mp->md_cval;
		ver = ((const struct mod_version *)mp->md_data)->mv_version;
		mod = modlist_lookup(modname, ver);
		if (mod != NULL) {
			printf("interface %s.%d already present in the KLD"
			    " '%s'!\n", modname, ver,
			    mod->container->filename);
			return (EEXIST);
		}
	}
	/*
		对比所有这些关于module的操作，基本上都是涉及 MDT_DEPEND 和 MDT_VERSION；这里做一个推测，
		linker file中的 module 有些可能是要依赖与其他模块的，所以才会有 module container；
		当我们发现我们需要的 module 在另外一个 file 中出现了，这个时候就要把 module container 
		加入到该文件的 dependencies 中。所以，我们需要建立一个全局的数组来管理所有的 module，这样
		一个文件才能方便查找 它所需要的 module 和对应的 container。
		另一方面也看出了加载顺序的重要性，假如加载顺序设计的不合理，一个file需要某个module，结果这个
		module 还没有加载，这个时候就会出现错误

		补充说明一下，MDT_VERSION 可能就表示 linker file 所依赖的 module 的版本信息，下面的操作可能
		就是对比 DEPEND 和 VERSION 所对应的 module name 是不是一致，如果是一致的话，就说明 linker
		file 依赖某一个版本的、以modname命名的 module，然后去 module_list 中进行查找；如果找到了，
		获取这个 module 的 container，然后把这个 container 添加到 linker file 的依赖项中

		再进一步推测，之前 linker_set 中定义的那些宏，作用可能就是声明一个全局符号，说明我依赖那些版本的
		module，然后我们对symbol进行解析的时候就可以知道我们需要那些依赖项，然后重定位之前加载它们；跟
		MDT_DEPEND 在一起的还有 MDT_MODULE 这个宏，这样可以进一步推测一下：如果以 MDT_DEPEND 作为
		标识符，那么它声明的全局符号就表示我依赖这个module；如果是以 MDT_MODULE 作为标识符的，那么它声明
		的全局符号就表示我提供这个module
	*/
	for (mdp = start; mdp < stop; mdp++) {
		mp = *mdp;
		if (mp->md_type != MDT_DEPEND)
			continue;
		modname = mp->md_cval;
		verinfo = mp->md_data;
		nmodname = NULL;
		for (nmdp = start; nmdp < stop; nmdp++) {
			nmp = *nmdp;
			if (nmp->md_type != MDT_VERSION)
				continue;
			nmodname = nmp->md_cval;
			if (strcmp(modname, nmodname) == 0)
				break;
		}
		if (nmdp < stop)/* early exit, it's a self reference */
			continue;
		mod = modlist_lookup2(modname, verinfo);
		if (mod) {	/* woohoo, it's loaded already */
			lfdep = mod->container;
			lfdep->refs++;
			error = linker_file_add_dependency(lf, lfdep);
			if (error)
				break;
			continue;
		}
		error = linker_load_module(NULL, modname, lf, verinfo, NULL);
		if (error) {
			printf("KLD %s: depends on %s - not available or"
			    " version mismatch\n", lf->filename, modname);
			break;
		}
	}

	if (error)
		return (error);
	linker_addmodules(lf, start, stop, 0);
	return (error);
}

static int
sysctl_kern_function_list_iterate(const char *name, void *opaque)
{
	struct sysctl_req *req;

	req = opaque;
	return (SYSCTL_OUT(req, name, strlen(name) + 1));
}

/*
 * Export a nul-separated, double-nul-terminated list of all function names
 * in the kernel.
 * 导出内核中所有以nul分隔、双nul结尾的函数名列表
 */
static int
sysctl_kern_function_list(SYSCTL_HANDLER_ARGS)
{
	linker_file_t lf;
	int error;

#ifdef MAC
	error = mac_kld_check_stat(req->td->td_ucred);
	if (error)
		return (error);
#endif
	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);
	sx_xlock(&kld_sx);
	TAILQ_FOREACH(lf, &linker_files, link) {
		error = LINKER_EACH_FUNCTION_NAME(lf,
		    sysctl_kern_function_list_iterate, req);
		if (error) {
			sx_xunlock(&kld_sx);
			return (error);
		}
	}
	sx_xunlock(&kld_sx);
	return (SYSCTL_OUT(req, "", 1));
}

SYSCTL_PROC(_kern, OID_AUTO, function_list, CTLTYPE_OPAQUE | CTLFLAG_RD,
    NULL, 0, sysctl_kern_function_list, "", "kernel function list");
