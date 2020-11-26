/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998-2000 Doug Rabson
 * Copyright (c) 2004 Peter Wemm
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
__FBSDID("$FreeBSD: releng/12.0/sys/kern/link_elf_obj.c 339953 2018-10-31 14:03:48Z bz $");

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/fcntl.h>
#include <sys/vnode.h>
#include <sys/linker.h>

#include <machine/elf.h>

#include <net/vnet.h>

#include <security/mac/mac_framework.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_object.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>

#include <sys/link_elf.h>

#ifdef DDB_CTF
#include <sys/zlib.h>
#endif

#include "linker_if.h"

/*
	用于表示 SHT_PROGBITS 类型的 section 在section table 中的entry？？
	下面 SHT_REL 和 SHT_RELA 类似
*/
typedef struct {
	void		*addr;
	Elf_Off		size;
	int		flags;
	/*
		参考下文代码实现，sec保存的是该section在原有文件的section header中的index
	*/
	int		sec;	/* Original section */
	char		*name;
} Elf_progent;

typedef struct {
	Elf_Rel		*rel;	/* 推断应该是 REL table 的地址(section table) */
	int		nrel;	/* REL table中entry的数量 */
	int		sec;	/* Elf32_Shdr->sh_info */
} Elf_relent;

typedef struct {
	Elf_Rela	*rela;
	int		nrela;
	int		sec;
} Elf_relaent;

/*
	结构体中的元素主要是各种类型的table，link_elf.c文件中的结构体元素类型更多一些，
	要关注一下两者之间的具体差异在哪里
*/
typedef struct elf_file {
	struct linker_file lf;		/* Common fields */

	/* 
		预加载标识，在link_elf.c文件中同样有一个该类型的结构体，这里专门用来处理预加载？？
	*/
	int		preloaded;
	caddr_t		address;	/* Relocation address 重定位的基地址 */
	vm_object_t	object;		/* VM object to hold file pages */
	Elf_Shdr	*e_shdr;

	Elf_progent	*progtab;
	u_int		nprogtab;

	Elf_relaent	*relatab;
	/* 
		根据下面的代码逻辑推断，该成员表示未被loader加载的 RELA section的数量
	*/
	u_int		nrelatab;	

	Elf_relent	*reltab;
	int		nreltab;

	Elf_Sym		*ddbsymtab;	/* The symbol table we are using 我们要使用的符号表 */
	long		ddbsymcnt;	/* Number of symbols 符号的数量 */
	caddr_t		ddbstrtab;	/* String table 我们要使用的字符串表 */
	long		ddbstrcnt;	/* number of bytes in string table 字符串表的大小 */

	caddr_t		shstrtab;	/* Section name string table */
	long		shstrcnt;	/* number of bytes in string table */

	caddr_t		ctftab;		/* CTF table */
	long		ctfcnt;		/* number of bytes in CTF table */
	caddr_t		ctfoff;		/* CTF offset table */

	/* 类型表？？ */
	caddr_t		typoff;		/* Type offset table */
	long		typlen;		/* Number of type entries. */

} *elf_file_t;

#include <kern/kern_ctf.c>

static int	link_elf_link_preload(linker_class_t cls,
		    const char *, linker_file_t *);
static int	link_elf_link_preload_finish(linker_file_t);
static int	link_elf_load_file(linker_class_t, const char *, linker_file_t *);
static int	link_elf_lookup_symbol(linker_file_t, const char *,
		    c_linker_sym_t *);
static int	link_elf_symbol_values(linker_file_t, c_linker_sym_t,
		    linker_symval_t *);
static int	link_elf_search_symbol(linker_file_t, caddr_t value,
		    c_linker_sym_t *sym, long *diffp);

static void	link_elf_unload_file(linker_file_t);
static int	link_elf_lookup_set(linker_file_t, const char *,
		    void ***, void ***, int *);
static int	link_elf_each_function_name(linker_file_t,
		    int (*)(const char *, void *), void *);
static int	link_elf_each_function_nameval(linker_file_t,
				linker_function_nameval_callback_t,
				void *);
static int	link_elf_reloc_local(linker_file_t, bool);
static long	link_elf_symtab_get(linker_file_t, const Elf_Sym **);
static long	link_elf_strtab_get(linker_file_t, caddr_t *);

static int	elf_obj_lookup(linker_file_t lf, Elf_Size symidx, int deps,
		    Elf_Addr *);

/*
	对比两个elf文件中的方法表，可以观察到该文件中注册了一个 link_elf_link_preload 函数，
	但是另外一个文件中是没有的，所以这里的作用很可能是跟预加载相关

	linux动态链接库预加载机制
		在linux操作系统的动态链接库加载过程中，动态链接器会读取LD_PRELOAD环境变量的值和默认配置文件/etc/ld.so.preload的
		文件内容，并将读取到的动态链接库进行预加载，即使程序不依赖这些动态链接库，LD_PRELOAD环境变量和/etc/ld.so.preload
		配置文件中指定的动态链接库依然会被装载,它们的优先级比LD_LIBRARY_PATH环境变量所定义的链接库查找路径的文件优先级要高，
		所以能够提前于用户调用的动态库载入
	
	全局符号介入
		全局符号介入指的是应用程序调用库函数时，调用的库函数如果在多个动态链接库中都存在，即存在同名函数，那么链接器只会保留第一个
		链接的函数，而忽略后面链接进来的函数，所以只要预加载的全局符号中有和后加载的普通共享库中全局符号重名，那么就会覆盖后装载的
		共享库以及目标文件里的全局符号
*/
static kobj_method_t link_elf_methods[] = {
	KOBJMETHOD(linker_lookup_symbol,	link_elf_lookup_symbol),
	KOBJMETHOD(linker_symbol_values,	link_elf_symbol_values),
	KOBJMETHOD(linker_search_symbol,	link_elf_search_symbol),
	KOBJMETHOD(linker_unload,		link_elf_unload_file),
	KOBJMETHOD(linker_load_file,		link_elf_load_file),
	KOBJMETHOD(linker_link_preload,		link_elf_link_preload),
	KOBJMETHOD(linker_link_preload_finish,	link_elf_link_preload_finish),
	KOBJMETHOD(linker_lookup_set,		link_elf_lookup_set),
	KOBJMETHOD(linker_each_function_name,	link_elf_each_function_name),
	KOBJMETHOD(linker_each_function_nameval, link_elf_each_function_nameval),
	KOBJMETHOD(linker_ctf_get,		link_elf_ctf_get),
	KOBJMETHOD(linker_symtab_get, 		link_elf_symtab_get),
	KOBJMETHOD(linker_strtab_get, 		link_elf_strtab_get),
	{ 0, 0 }
};

/*
	ELFCLASS32表示的是32位架构，ELF header中的 e_ident[EI_CLASS] 也是通过它来设置；
	这个非常类似driver代码结构， link_elf_methods 其实就是 kobj_method_t类型，也就
	相当于driver中的driver method
*/
static struct linker_class link_elf_class = {
#if ELF_TARG_CLASS == ELFCLASS32
	"elf32_obj",
#else
	"elf64_obj",
#endif
	link_elf_methods, sizeof(struct elf_file)
};

static int	relocate_file(elf_file_t ef);
static void	elf_obj_cleanup_globals_cache(elf_file_t);

static void
link_elf_error(const char *filename, const char *s)
{
	if (filename == NULL)
		printf("kldload: %s\n", s);
	else
		printf("kldload: %s: %s\n", filename, s);
}

static void
link_elf_init(void *arg)
{

	linker_add_class(&link_elf_class);
}

SYSINIT(link_elf_obj, SI_SUB_KLD, SI_ORDER_SECOND, link_elf_init, NULL);

static int
link_elf_link_preload(linker_class_t cls, const char *filename,
    linker_file_t *result)
{
	Elf_Ehdr *hdr;		/* ELF header */
	Elf_Shdr *shdr;		/* Section header */
	Elf_Sym *es;		/* symbol table entry  */
	void *modptr, *baseptr, *sizeptr;
	char *type;
	elf_file_t ef;
	linker_file_t lf;
	Elf_Addr off;
	int error, i, j, pb, ra, rl, shstrindex, symstrindex, symtabindex;

	/* Look to see if we have the file preloaded */
	modptr = preload_search_by_name(filename);
	if (modptr == NULL)
		return ENOENT;

	/*
		preload_search_info 用于获取预加载文件的属性，例如 MODINFO_TYPE 就指明了我们所要获取
		的属性的类型是type，还有address、ELF header、section header等
	*/
	type = (char *)preload_search_info(modptr, MODINFO_TYPE);
	baseptr = preload_search_info(modptr, MODINFO_ADDR);
	sizeptr = preload_search_info(modptr, MODINFO_SIZE);
	hdr = (Elf_Ehdr *)preload_search_info(modptr, MODINFO_METADATA |
	    MODINFOMD_ELFHDR);
	shdr = (Elf_Shdr *)preload_search_info(modptr, MODINFO_METADATA |
	    MODINFOMD_SHDR);

	/* 判断文件类型是不是我们所需要的 */
	if (type == NULL || (strcmp(type, "elf" __XSTRING(__ELF_WORD_SIZE)
	    " obj module") != 0 &&
	    strcmp(type, "elf obj module") != 0)) {
		return (EFTYPE);
	}

	/* 判断ELF header、section header等是否存在 */
	if (baseptr == NULL || sizeptr == NULL || hdr == NULL ||
	    shdr == NULL)
		return (EINVAL);

	/*
		上述条件都满足之后，我们就创建一个linker file(kernel)，并指定一个linker class
	*/
	lf = linker_make_file(filename, &link_elf_class);
	if (lf == NULL)
		return (ENOMEM);

	/* 对lf强制类型转换，其实对ef的操作就是对lf的操作 */
	ef = (elf_file_t)lf;
	ef->preloaded = 1;
	ef->address = *(caddr_t *)baseptr;
	lf->address = *(caddr_t *)baseptr;
	lf->size = *(size_t *)sizeptr;

	/* 
		检查ELF Header中所包含的信息是否正确 
	*/
	if (hdr->e_ident[EI_CLASS] != ELF_TARG_CLASS ||
	    hdr->e_ident[EI_DATA] != ELF_TARG_DATA ||
	    hdr->e_ident[EI_VERSION] != EV_CURRENT ||
	    hdr->e_version != EV_CURRENT ||
	    hdr->e_type != ET_REL ||
	    hdr->e_machine != ELF_TARG_MACH) {
		error = EFTYPE;
		goto out;
	}
	ef->e_shdr = shdr;

	/* 
		Scan the section header for information and table sizing.
		开始对 section header 做相关处理，
		初始化 symbol table index 和 string table index 为-1
		e_shnum 表示section header entry的数量，也就是表示有多少个section
	*/
	symtabindex = -1;
	symstrindex = -1;

	/*
		通过for循环扫描section header中所有的entry，主要就是为了统计未被加载的
		section的数量和symbol table/string index 
	*/
	for (i = 0; i < hdr->e_shnum; i++) {
		switch (shdr[i].sh_type) {
		case SHT_PROGBITS:
		case SHT_NOBITS:
#ifdef __amd64__
		case SHT_X86_64_UNWIND:
#endif
			/* 
				Ignore sections not loaded by the loader.
				Q&A：sh_addr == 0就表示loader不会加载？？
				参考一下symbol table建立的过程，对于file自身包含的变量或者函数的定义，我们会为其分配一个符号，
				并且可以通过一些方法(需要进一步思考)获取到它的地址(运行时地址还是相对地址？)，但是如果我们是引用
				外部模块的变量或者函数，那么我们将无法获取到它的地址的，这个就需要链接的时候再进一步确定，应该是
				编译器会给其分配一个0地址，然后生成一条提示(好像是保存在.rel.text段)，告诉链接器这个变量的地址
				需要修改，链接器链接的时候再通过一些步骤得到对应的地址，然后对这些0地址再进行修改
			*/
			if (shdr[i].sh_addr == 0)
				break;
			ef->nprogtab++;
			break;
		case SHT_SYMTAB:
			/* 
				如果section的类型是SHT_SYMTAB，那么sh_link就表示section header entry在
				string table中的index
			*/
			symtabindex = i;
			symstrindex = shdr[i].sh_link;
			break;
		case SHT_REL:
			/*
			 * Ignore relocation tables for sections not
			 * loaded by the loader. 
			 * 与上边的处理方式是一致的
			 */
			if (shdr[shdr[i].sh_info].sh_addr == 0)
				break;
			ef->nreltab++;
			break;
		case SHT_RELA:
			if (shdr[shdr[i].sh_info].sh_addr == 0)
				break;
			ef->nrelatab++;
			break;
		}
	}

	shstrindex = hdr->e_shstrndx;
	if (ef->nprogtab == 0 || symstrindex < 0 ||
	    symstrindex >= hdr->e_shnum ||
	    shdr[symstrindex].sh_type != SHT_STRTAB || shstrindex == 0 ||
	    shstrindex >= hdr->e_shnum ||
	    shdr[shstrindex].sh_type != SHT_STRTAB) {
		printf("%s: bad/missing section headers\n", filename);
		error = ENOEXEC;
		goto out;
	}

	/* 
		Allocate space for tracking the load chunks 
		注意：malloc中的参数是 nprogtab nreltab nrelatab，这里是为那些地址为0的entry
		来分配空间
	*/
	if (ef->nprogtab != 0)
		ef->progtab = malloc(ef->nprogtab * sizeof(*ef->progtab),
		    M_LINKER, M_WAITOK | M_ZERO);
	if (ef->nreltab != 0)
		ef->reltab = malloc(ef->nreltab * sizeof(*ef->reltab),
		    M_LINKER, M_WAITOK | M_ZERO);
	if (ef->nrelatab != 0)
		ef->relatab = malloc(ef->nrelatab * sizeof(*ef->relatab),
		    M_LINKER, M_WAITOK | M_ZERO);
	if ((ef->nprogtab != 0 && ef->progtab == NULL) ||
	    (ef->nreltab != 0 && ef->reltab == NULL) ||
	    (ef->nrelatab != 0 && ef->relatab == NULL)) {
		error = ENOMEM;
		goto out;
	}

	/* XXX, relocate the sh_addr fields saved by the loader. */
	off = 0;

	/*
		遍历所有的section并且sh_addr，找到其中最小的 sh_addr 作为 offset
	*/
	for (i = 0; i < hdr->e_shnum; i++) {
		if (shdr[i].sh_addr != 0 && (off == 0 || shdr[i].sh_addr < off))
			off = shdr[i].sh_addr;
	}

	/*
		sh_addr最小的section可以看到是放在第一个位置的，然后每一个section根据与第一个
		section的距离来进行加载，说明这个时候它们之间的位置关系是已经计算好了的(计算的话
		就必须要知道每个section的大小，可能还需要经过其他的一些处理，然后确定sh_addr，
		要不然感觉不能这么简单的进行排列，所以这一步是在哪个阶段完成的？)。ef->address就相当
		与是section加载的基地址，第一个section的第一个byte应该就是就是位于该地址
		考虑一下多个section entry 的 sh_addr 都为0的情况？？
	*/
	for (i = 0; i < hdr->e_shnum; i++) {
		if (shdr[i].sh_addr != 0)
			shdr[i].sh_addr = shdr[i].sh_addr - off +
			    (Elf_Addr)ef->address;
	}

	/*
		计算symbol数量，获取symbol table在内存中的地址
		获取string table大小和内存地址(ddb)
	*/
	ef->ddbsymcnt = shdr[symtabindex].sh_size / sizeof(Elf_Sym);	
	ef->ddbsymtab = (Elf_Sym *)shdr[symtabindex].sh_addr;
	ef->ddbstrcnt = shdr[symstrindex].sh_size;
	ef->ddbstrtab = (char *)shdr[symstrindex].sh_addr;
	ef->shstrcnt = shdr[shstrindex].sh_size;
	ef->shstrtab = (char *)shdr[shstrindex].sh_addr;

	/* Now fill out progtab and the relocation tables. 填充progtab和重定位表*/
	pb = 0;		/* progtab index */
	rl = 0;		/* REL section index */
	ra = 0;		/* RELA section index */
	for (i = 0; i < hdr->e_shnum; i++) {
		switch (shdr[i].sh_type) {
		case SHT_PROGBITS:
		case SHT_NOBITS:
#ifdef __amd64__
		case SHT_X86_64_UNWIND:
#endif
			if (shdr[i].sh_addr == 0)
				break;	/* 对于 sh_addr 为0的 section，不做处理  */
			ef->progtab[pb].addr = (void *)shdr[i].sh_addr;
			if (shdr[i].sh_type == SHT_PROGBITS)
				ef->progtab[pb].name = "<<PROGBITS>>";
#ifdef __amd64__
			else if (shdr[i].sh_type == SHT_X86_64_UNWIND)
				ef->progtab[pb].name = "<<UNWIND>>";
#endif
			else
				ef->progtab[pb].name = "<<NOBITS>>";
			ef->progtab[pb].size = shdr[i].sh_size;
			ef->progtab[pb].sec = i;	/* 把该section在原有文件的section header中的index赋值给sec */
			if (ef->shstrtab && shdr[i].sh_name != 0)
				ef->progtab[pb].name =
				    ef->shstrtab + shdr[i].sh_name;

			if (ef->progtab[pb].name != NULL && 
			    !strcmp(ef->progtab[pb].name, DPCPU_SETNAME)) {
				void *dpcpu;

				/* 多CPU操作 */
				dpcpu = dpcpu_alloc(shdr[i].sh_size);
				if (dpcpu == NULL) {
					printf("%s: pcpu module space is out "
					    "of space; cannot allocate %#jx "
					    "for %s\n", __func__,
					    (uintmax_t)shdr[i].sh_size,
					    filename);
					error = ENOSPC;
					goto out;
				}
				memcpy(dpcpu, ef->progtab[pb].addr,
				    ef->progtab[pb].size);
				dpcpu_copy(dpcpu, shdr[i].sh_size);
				ef->progtab[pb].addr = dpcpu;
#ifdef VIMAGE
			} else if (ef->progtab[pb].name != NULL &&
			    !strcmp(ef->progtab[pb].name, VNET_SETNAME)) {
				void *vnet_data;

				vnet_data = vnet_data_alloc(shdr[i].sh_size);
				if (vnet_data == NULL) {
					printf("%s: vnet module space is out "
					    "of space; cannot allocate %#jx "
					    "for %s\n", __func__,
					    (uintmax_t)shdr[i].sh_size,
					    filename);
					error = ENOSPC;
					goto out;
				}
				memcpy(vnet_data, ef->progtab[pb].addr,
				    ef->progtab[pb].size);
				vnet_data_copy(vnet_data, shdr[i].sh_size);
				ef->progtab[pb].addr = vnet_data;
#endif
			} else if (ef->progtab[pb].name != NULL &&
			    !strcmp(ef->progtab[pb].name, ".ctors")) {
				lf->ctors_addr = ef->progtab[pb].addr;
				lf->ctors_size = shdr[i].sh_size;
			}

			/* Update all symbol values with the offset. */
			for (j = 0; j < ef->ddbsymcnt; j++) {
				es = &ef->ddbsymtab[j];
				if (es->st_shndx != i)
					continue;
				es->st_value += (Elf_Addr)ef->progtab[pb].addr;
			}
			pb++;
			break;
		case SHT_REL:
			if (shdr[shdr[i].sh_info].sh_addr == 0)
				break;
			ef->reltab[rl].rel = (Elf_Rel *)shdr[i].sh_addr;
			ef->reltab[rl].nrel = shdr[i].sh_size / sizeof(Elf_Rel);
			ef->reltab[rl].sec = shdr[i].sh_info;
			rl++;
			break;
		case SHT_RELA:
			if (shdr[shdr[i].sh_info].sh_addr == 0)
				break;
			ef->relatab[ra].rela = (Elf_Rela *)shdr[i].sh_addr;
			ef->relatab[ra].nrela =
			    shdr[i].sh_size / sizeof(Elf_Rela);
			ef->relatab[ra].sec = shdr[i].sh_info;
			ra++;
			break;
		}
	}

	/* 
		从代码逻辑可以看到，ef(elf_file_t)主要的工作就是收集，填充结构体内的各个成员，
		应该是一个管理者。下面再判断一下这些section是否都是存在的，如果不存在，直接go out，
		说明这些section是实现预加载必不可少的，要重点关注
	*/
	if (pb != ef->nprogtab) {
		printf("%s: lost progbits\n", filename);
		error = ENOEXEC;
		goto out;
	}
	if (rl != ef->nreltab) {
		printf("%s: lost reltab\n", filename);
		error = ENOEXEC;
		goto out;
	}
	if (ra != ef->nrelatab) {
		printf("%s: lost relatab\n", filename);
		error = ENOEXEC;
		goto out;
	}

	/* Local intra-module relocations 本地模块内重定位 */
	/*
		说明还有别的地方也需要重定位，这里要确认该操作是属于哪个阶段的步骤？？
	*/
	error = link_elf_reloc_local(lf, false);
	if (error != 0)
		goto out;
	*result = lf;
	return (0);

out:
	/* preload not done this way */
	linker_file_unload(lf, LINKER_UNLOAD_FORCE);
	return (error);
}

static void
link_elf_invoke_ctors(caddr_t addr, size_t size)
{
	void (**ctor)(void);
	size_t i, cnt;

	if (addr == NULL || size == 0)
		return;
	cnt = size / sizeof(*ctor);
	ctor = (void *)addr;
	for (i = 0; i < cnt; i++) {
		if (ctor[i] != NULL)
			(*ctor[i])();
	}
}

static int
link_elf_link_preload_finish(linker_file_t lf)
{
	elf_file_t ef;
	int error;

	ef = (elf_file_t)lf;
	error = relocate_file(ef);
	if (error)
		return (error);

	/* Notify MD code that a module is being loaded. */
	error = elf_cpu_load_file(lf);
	if (error)
		return (error);

#if defined(__i386__) || defined(__amd64__)
	/* Now ifuncs. */
	error = link_elf_reloc_local(lf, true);
	if (error != 0)
		return (error);
#endif

	/* Invoke .ctors */
	link_elf_invoke_ctors(lf->ctors_addr, lf->ctors_size);
	return (0);
}

static int
link_elf_load_file(linker_class_t cls, const char *filename,
    linker_file_t *result)
{
	struct nameidata *nd;
	struct thread *td = curthread;	/* XXX */
	Elf_Ehdr *hdr;
	Elf_Shdr *shdr;
	Elf_Sym *es;
	int nbytes, i, j;
	vm_offset_t mapbase;
	size_t mapsize;
	int error = 0;
	ssize_t resid;
	int flags;
	elf_file_t ef;
	linker_file_t lf;
	int symtabindex;
	int symstrindex;
	int shstrindex;
	int nsym;
	int pb, rl, ra;
	int alignmask;

	shdr = NULL;
	lf = NULL;
	mapsize = 0;
	hdr = NULL;

	nd = malloc(sizeof(struct nameidata), M_TEMP, M_WAITOK);
	NDINIT(nd, LOOKUP, FOLLOW, UIO_SYSSPACE, filename, td);
	flags = FREAD;
	error = vn_open(nd, &flags, 0, NULL);
	if (error) {
		free(nd, M_TEMP);
		return error;
	}
	NDFREE(nd, NDF_ONLY_PNBUF);
	if (nd->ni_vp->v_type != VREG) {
		error = ENOEXEC;
		goto out;
	}
#ifdef MAC
	error = mac_kld_check_load(td->td_ucred, nd->ni_vp);
	if (error) {
		goto out;
	}
#endif

	/* Read the elf header from the file. */
	hdr = malloc(sizeof(*hdr), M_LINKER, M_WAITOK);
	error = vn_rdwr(UIO_READ, nd->ni_vp, (void *)hdr, sizeof(*hdr), 0,
	    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED,
	    &resid, td);
	if (error)
		goto out;
	if (resid != 0){
		error = ENOEXEC;
		goto out;
	}

	if (!IS_ELF(*hdr)) {
		error = ENOEXEC;
		goto out;
	}

	if (hdr->e_ident[EI_CLASS] != ELF_TARG_CLASS
	    || hdr->e_ident[EI_DATA] != ELF_TARG_DATA) {
		link_elf_error(filename, "Unsupported file layout");
		error = ENOEXEC;
		goto out;
	}
	if (hdr->e_ident[EI_VERSION] != EV_CURRENT
	    || hdr->e_version != EV_CURRENT) {
		link_elf_error(filename, "Unsupported file version");
		error = ENOEXEC;
		goto out;
	}
	if (hdr->e_type != ET_REL) {
		error = ENOSYS;
		goto out;
	}
	if (hdr->e_machine != ELF_TARG_MACH) {
		link_elf_error(filename, "Unsupported machine");
		error = ENOEXEC;
		goto out;
	}

	lf = linker_make_file(filename, &link_elf_class);
	if (!lf) {
		error = ENOMEM;
		goto out;
	}
	ef = (elf_file_t) lf;
	ef->nprogtab = 0;
	ef->e_shdr = 0;
	ef->nreltab = 0;
	ef->nrelatab = 0;

	/* Allocate and read in the section header */
	nbytes = hdr->e_shnum * hdr->e_shentsize;
	if (nbytes == 0 || hdr->e_shoff == 0 ||
	    hdr->e_shentsize != sizeof(Elf_Shdr)) {
		error = ENOEXEC;
		goto out;
	}
	shdr = malloc(nbytes, M_LINKER, M_WAITOK);
	ef->e_shdr = shdr;
	error = vn_rdwr(UIO_READ, nd->ni_vp, (caddr_t)shdr, nbytes,
	    hdr->e_shoff, UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred,
	    NOCRED, &resid, td);
	if (error)
		goto out;
	if (resid) {
		error = ENOEXEC;
		goto out;
	}

	/* Scan the section header for information and table sizing. */
	nsym = 0;
	symtabindex = -1;
	symstrindex = -1;
	for (i = 0; i < hdr->e_shnum; i++) {
		if (shdr[i].sh_size == 0)
			continue;
		switch (shdr[i].sh_type) {
		case SHT_PROGBITS:
		case SHT_NOBITS:
#ifdef __amd64__
		case SHT_X86_64_UNWIND:
#endif
			if ((shdr[i].sh_flags & SHF_ALLOC) == 0)
				break;
			ef->nprogtab++;
			break;
		case SHT_SYMTAB:
			nsym++;
			symtabindex = i;
			symstrindex = shdr[i].sh_link;
			break;
		case SHT_REL:
			/*
			 * Ignore relocation tables for unallocated
			 * sections.
			 */
			if ((shdr[shdr[i].sh_info].sh_flags & SHF_ALLOC) == 0)
				break;
			ef->nreltab++;
			break;
		case SHT_RELA:
			if ((shdr[shdr[i].sh_info].sh_flags & SHF_ALLOC) == 0)
				break;
			ef->nrelatab++;
			break;
		case SHT_STRTAB:
			break;
		}
	}
	if (ef->nprogtab == 0) {
		link_elf_error(filename, "file has no contents");
		error = ENOEXEC;
		goto out;
	}
	if (nsym != 1) {
		/* Only allow one symbol table for now */
		link_elf_error(filename,
		    "file must have exactly one symbol table");
		error = ENOEXEC;
		goto out;
	}
	if (symstrindex < 0 || symstrindex > hdr->e_shnum ||
	    shdr[symstrindex].sh_type != SHT_STRTAB) {
		link_elf_error(filename, "file has invalid symbol strings");
		error = ENOEXEC;
		goto out;
	}

	/* Allocate space for tracking the load chunks */
	if (ef->nprogtab != 0)
		ef->progtab = malloc(ef->nprogtab * sizeof(*ef->progtab),
		    M_LINKER, M_WAITOK | M_ZERO);
	if (ef->nreltab != 0)
		ef->reltab = malloc(ef->nreltab * sizeof(*ef->reltab),
		    M_LINKER, M_WAITOK | M_ZERO);
	if (ef->nrelatab != 0)
		ef->relatab = malloc(ef->nrelatab * sizeof(*ef->relatab),
		    M_LINKER, M_WAITOK | M_ZERO);

	if (symtabindex == -1) {
		link_elf_error(filename, "lost symbol table index");
		error = ENOEXEC;
		goto out;
	}
	/* Allocate space for and load the symbol table */
	ef->ddbsymcnt = shdr[symtabindex].sh_size / sizeof(Elf_Sym);
	ef->ddbsymtab = malloc(shdr[symtabindex].sh_size, M_LINKER, M_WAITOK);
	error = vn_rdwr(UIO_READ, nd->ni_vp, (void *)ef->ddbsymtab,
	    shdr[symtabindex].sh_size, shdr[symtabindex].sh_offset,
	    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED,
	    &resid, td);
	if (error)
		goto out;
	if (resid != 0){
		error = EINVAL;
		goto out;
	}

	if (symstrindex == -1) {
		link_elf_error(filename, "lost symbol string index");
		error = ENOEXEC;
		goto out;
	}
	/* Allocate space for and load the symbol strings */
	ef->ddbstrcnt = shdr[symstrindex].sh_size;
	ef->ddbstrtab = malloc(shdr[symstrindex].sh_size, M_LINKER, M_WAITOK);
	error = vn_rdwr(UIO_READ, nd->ni_vp, ef->ddbstrtab,
	    shdr[symstrindex].sh_size, shdr[symstrindex].sh_offset,
	    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED,
	    &resid, td);
	if (error)
		goto out;
	if (resid != 0){
		error = EINVAL;
		goto out;
	}

	/* Do we have a string table for the section names?  */
	shstrindex = -1;
	if (hdr->e_shstrndx != 0 &&
	    shdr[hdr->e_shstrndx].sh_type == SHT_STRTAB) {
		shstrindex = hdr->e_shstrndx;
		ef->shstrcnt = shdr[shstrindex].sh_size;
		ef->shstrtab = malloc(shdr[shstrindex].sh_size, M_LINKER,
		    M_WAITOK);
		error = vn_rdwr(UIO_READ, nd->ni_vp, ef->shstrtab,
		    shdr[shstrindex].sh_size, shdr[shstrindex].sh_offset,
		    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED,
		    &resid, td);
		if (error)
			goto out;
		if (resid != 0){
			error = EINVAL;
			goto out;
		}
	}

	/* Size up code/data(progbits) and bss(nobits). */
	alignmask = 0;
	for (i = 0; i < hdr->e_shnum; i++) {
		if (shdr[i].sh_size == 0)
			continue;
		switch (shdr[i].sh_type) {
		case SHT_PROGBITS:
		case SHT_NOBITS:
#ifdef __amd64__
		case SHT_X86_64_UNWIND:
#endif
			if ((shdr[i].sh_flags & SHF_ALLOC) == 0)
				break;
			alignmask = shdr[i].sh_addralign - 1;
			mapsize += alignmask;
			mapsize &= ~alignmask;
			mapsize += shdr[i].sh_size;
			break;
		}
	}

	/*
	 * We know how much space we need for the text/data/bss/etc.
	 * This stuff needs to be in a single chunk so that profiling etc
	 * can get the bounds and gdb can associate offsets with modules
	 */
	ef->object = vm_object_allocate(OBJT_DEFAULT,
	    round_page(mapsize) >> PAGE_SHIFT);
	if (ef->object == NULL) {
		error = ENOMEM;
		goto out;
	}
	ef->address = (caddr_t) vm_map_min(kernel_map);

	/*
	 * In order to satisfy amd64's architectural requirements on the
	 * location of code and data in the kernel's address space, request a
	 * mapping that is above the kernel.  
	 */
#ifdef __amd64__
	mapbase = KERNBASE;
#else
	mapbase = VM_MIN_KERNEL_ADDRESS;
#endif
	error = vm_map_find(kernel_map, ef->object, 0, &mapbase,
	    round_page(mapsize), 0, VMFS_OPTIMAL_SPACE, VM_PROT_ALL,
	    VM_PROT_ALL, 0);
	if (error) {
		vm_object_deallocate(ef->object);
		ef->object = 0;
		goto out;
	}

	/* Wire the pages */
	error = vm_map_wire(kernel_map, mapbase,
	    mapbase + round_page(mapsize),
	    VM_MAP_WIRE_SYSTEM|VM_MAP_WIRE_NOHOLES);
	if (error != KERN_SUCCESS) {
		error = ENOMEM;
		goto out;
	}

	/* Inform the kld system about the situation */
	lf->address = ef->address = (caddr_t)mapbase;
	lf->size = mapsize;

	/*
	 * Now load code/data(progbits), zero bss(nobits), allocate space for
	 * and load relocs
	 */
	pb = 0;
	rl = 0;
	ra = 0;
	alignmask = 0;
	for (i = 0; i < hdr->e_shnum; i++) {
		if (shdr[i].sh_size == 0)
			continue;
		switch (shdr[i].sh_type) {
		case SHT_PROGBITS:
		case SHT_NOBITS:
#ifdef __amd64__
		case SHT_X86_64_UNWIND:
#endif
			if ((shdr[i].sh_flags & SHF_ALLOC) == 0)
				break;
			alignmask = shdr[i].sh_addralign - 1;
			mapbase += alignmask;
			mapbase &= ~alignmask;
			if (ef->shstrtab != NULL && shdr[i].sh_name != 0) {
				ef->progtab[pb].name =
				    ef->shstrtab + shdr[i].sh_name;
				if (!strcmp(ef->progtab[pb].name, ".ctors")) {
					lf->ctors_addr = (caddr_t)mapbase;
					lf->ctors_size = shdr[i].sh_size;
				}
			} else if (shdr[i].sh_type == SHT_PROGBITS)
				ef->progtab[pb].name = "<<PROGBITS>>";
#ifdef __amd64__
			else if (shdr[i].sh_type == SHT_X86_64_UNWIND)
				ef->progtab[pb].name = "<<UNWIND>>";
#endif
			else
				ef->progtab[pb].name = "<<NOBITS>>";
			if (ef->progtab[pb].name != NULL && 
			    !strcmp(ef->progtab[pb].name, DPCPU_SETNAME)) {
				ef->progtab[pb].addr =
				    dpcpu_alloc(shdr[i].sh_size);
				if (ef->progtab[pb].addr == NULL) {
					printf("%s: pcpu module space is out "
					    "of space; cannot allocate %#jx "
					    "for %s\n", __func__,
					    (uintmax_t)shdr[i].sh_size,
					    filename);
				}
			}
#ifdef VIMAGE
			else if (ef->progtab[pb].name != NULL &&
			    !strcmp(ef->progtab[pb].name, VNET_SETNAME)) {
				ef->progtab[pb].addr =
				    vnet_data_alloc(shdr[i].sh_size);
				if (ef->progtab[pb].addr == NULL) {
					printf("%s: vnet module space is out "
					    "of space; cannot allocate %#jx "
					    "for %s\n", __func__,
					    (uintmax_t)shdr[i].sh_size,
					    filename);
				}
			}
#endif
			else
				ef->progtab[pb].addr =
				    (void *)(uintptr_t)mapbase;
			if (ef->progtab[pb].addr == NULL) {
				error = ENOSPC;
				goto out;
			}
			ef->progtab[pb].size = shdr[i].sh_size;
			ef->progtab[pb].sec = i;
			if (shdr[i].sh_type == SHT_PROGBITS
#ifdef __amd64__
			    || shdr[i].sh_type == SHT_X86_64_UNWIND
#endif
			    ) {
				error = vn_rdwr(UIO_READ, nd->ni_vp,
				    ef->progtab[pb].addr,
				    shdr[i].sh_size, shdr[i].sh_offset,
				    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred,
				    NOCRED, &resid, td);
				if (error)
					goto out;
				if (resid != 0){
					error = EINVAL;
					goto out;
				}
				/* Initialize the per-cpu or vnet area. */
				if (ef->progtab[pb].addr != (void *)mapbase &&
				    !strcmp(ef->progtab[pb].name, DPCPU_SETNAME))
					dpcpu_copy(ef->progtab[pb].addr,
					    shdr[i].sh_size);
#ifdef VIMAGE
				else if (ef->progtab[pb].addr !=
				    (void *)mapbase &&
				    !strcmp(ef->progtab[pb].name, VNET_SETNAME))
					vnet_data_copy(ef->progtab[pb].addr,
					    shdr[i].sh_size);
#endif
			} else
				bzero(ef->progtab[pb].addr, shdr[i].sh_size);

			/* Update all symbol values with the offset. */
			for (j = 0; j < ef->ddbsymcnt; j++) {
				es = &ef->ddbsymtab[j];
				if (es->st_shndx != i)
					continue;
				es->st_value += (Elf_Addr)ef->progtab[pb].addr;
			}
			mapbase += shdr[i].sh_size;
			pb++;
			break;
		case SHT_REL:
			if ((shdr[shdr[i].sh_info].sh_flags & SHF_ALLOC) == 0)
				break;
			ef->reltab[rl].rel = malloc(shdr[i].sh_size, M_LINKER,
			    M_WAITOK);
			ef->reltab[rl].nrel = shdr[i].sh_size / sizeof(Elf_Rel);
			ef->reltab[rl].sec = shdr[i].sh_info;
			error = vn_rdwr(UIO_READ, nd->ni_vp,
			    (void *)ef->reltab[rl].rel,
			    shdr[i].sh_size, shdr[i].sh_offset,
			    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED,
			    &resid, td);
			if (error)
				goto out;
			if (resid != 0){
				error = EINVAL;
				goto out;
			}
			rl++;
			break;
		case SHT_RELA:
			if ((shdr[shdr[i].sh_info].sh_flags & SHF_ALLOC) == 0)
				break;
			ef->relatab[ra].rela = malloc(shdr[i].sh_size, M_LINKER,
			    M_WAITOK);
			ef->relatab[ra].nrela =
			    shdr[i].sh_size / sizeof(Elf_Rela);
			ef->relatab[ra].sec = shdr[i].sh_info;
			error = vn_rdwr(UIO_READ, nd->ni_vp,
			    (void *)ef->relatab[ra].rela,
			    shdr[i].sh_size, shdr[i].sh_offset,
			    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED,
			    &resid, td);
			if (error)
				goto out;
			if (resid != 0){
				error = EINVAL;
				goto out;
			}
			ra++;
			break;
		}
	}
	if (pb != ef->nprogtab) {
		link_elf_error(filename, "lost progbits");
		error = ENOEXEC;
		goto out;
	}
	if (rl != ef->nreltab) {
		link_elf_error(filename, "lost reltab");
		error = ENOEXEC;
		goto out;
	}
	if (ra != ef->nrelatab) {
		link_elf_error(filename, "lost relatab");
		error = ENOEXEC;
		goto out;
	}
	if (mapbase != (vm_offset_t)ef->address + mapsize) {
		printf(
		    "%s: mapbase 0x%lx != address %p + mapsize 0x%lx (0x%lx)\n",
		    filename != NULL ? filename : "<none>",
		    (u_long)mapbase, ef->address, (u_long)mapsize,
		    (u_long)(vm_offset_t)ef->address + mapsize);
		error = ENOMEM;
		goto out;
	}

	/* Local intra-module relocations */
	error = link_elf_reloc_local(lf, false);
	if (error != 0)
		goto out;

	/* Pull in dependencies */
	VOP_UNLOCK(nd->ni_vp, 0);
	error = linker_load_dependencies(lf);
	vn_lock(nd->ni_vp, LK_EXCLUSIVE | LK_RETRY);
	if (error)
		goto out;

	/* External relocations */
	error = relocate_file(ef);
	if (error)
		goto out;

	/* Notify MD code that a module is being loaded. */
	error = elf_cpu_load_file(lf);
	if (error)
		goto out;

#if defined(__i386__) || defined(__amd64__)
	/* Now ifuncs. */
	error = link_elf_reloc_local(lf, true);
	if (error != 0)
		goto out;
#endif

	/* Invoke .ctors */
	link_elf_invoke_ctors(lf->ctors_addr, lf->ctors_size);

	*result = lf;

out:
	VOP_UNLOCK(nd->ni_vp, 0);
	vn_close(nd->ni_vp, FREAD, td->td_ucred, td);
	free(nd, M_TEMP);
	if (error && lf)
		linker_file_unload(lf, LINKER_UNLOAD_FORCE);
	free(hdr, M_LINKER);

	return error;
}

static void
link_elf_unload_file(linker_file_t file)
{
	elf_file_t ef = (elf_file_t) file;
	u_int i;

	/* Notify MD code that a module is being unloaded. */
	elf_cpu_unload_file(file);

	if (ef->progtab) {
		for (i = 0; i < ef->nprogtab; i++) {
			if (ef->progtab[i].size == 0)
				continue;
			if (ef->progtab[i].name == NULL)
				continue;
			if (!strcmp(ef->progtab[i].name, DPCPU_SETNAME))
				dpcpu_free(ef->progtab[i].addr,
				    ef->progtab[i].size);
#ifdef VIMAGE
			else if (!strcmp(ef->progtab[i].name, VNET_SETNAME))
				vnet_data_free(ef->progtab[i].addr,
				    ef->progtab[i].size);
#endif
		}
	}
	if (ef->preloaded) {
		free(ef->reltab, M_LINKER);
		free(ef->relatab, M_LINKER);
		free(ef->progtab, M_LINKER);
		free(ef->ctftab, M_LINKER);
		free(ef->ctfoff, M_LINKER);
		free(ef->typoff, M_LINKER);
		if (file->pathname != NULL)
			preload_delete_name(file->pathname);
		return;
	}

	for (i = 0; i < ef->nreltab; i++)
		free(ef->reltab[i].rel, M_LINKER);
	for (i = 0; i < ef->nrelatab; i++)
		free(ef->relatab[i].rela, M_LINKER);
	free(ef->reltab, M_LINKER);
	free(ef->relatab, M_LINKER);
	free(ef->progtab, M_LINKER);

	if (ef->object) {
		vm_map_remove(kernel_map, (vm_offset_t) ef->address,
		    (vm_offset_t) ef->address +
		    (ef->object->size << PAGE_SHIFT));
	}
	free(ef->e_shdr, M_LINKER);
	free(ef->ddbsymtab, M_LINKER);
	free(ef->ddbstrtab, M_LINKER);
	free(ef->shstrtab, M_LINKER);
	free(ef->ctftab, M_LINKER);
	free(ef->ctfoff, M_LINKER);
	free(ef->typoff, M_LINKER);
}

static const char *
symbol_name(elf_file_t ef, Elf_Size r_info)
{
	const Elf_Sym *ref;

	if (ELF_R_SYM(r_info)) {
		ref = ef->ddbsymtab + ELF_R_SYM(r_info);
		return ef->ddbstrtab + ref->st_name;
	} else
		return NULL;
}

/*
	函数功能应该就是找到elf文件中的指定的 PROGBITS类型的section，获取到程序运行所需要的数据
*/
static Elf_Addr
findbase(elf_file_t ef, int sec)
{
	int i;
	Elf_Addr base = 0;

	/*
		prog table应该是位于 PROGBITS 类型的section当中，它里边保存的是跟程序运行相关的代码或者数据；
		这里就是通过遍历elf文件中的progtab(也就是所有PROGBITS类型的section)来找到我们所需要的那个，
		然后把地址赋值给base，然后返回base
	*/
	for (i = 0; i < ef->nprogtab; i++) {
		if (sec == ef->progtab[i].sec) {
			base = (Elf_Addr)ef->progtab[i].addr;
			break;
		}
	}
	return base;
}

static int
relocate_file(elf_file_t ef)
{
	const Elf_Rel *rellim;
	const Elf_Rel *rel;
	const Elf_Rela *relalim;
	const Elf_Rela *rela;
	const char *symname;
	const Elf_Sym *sym;
	int i;
	Elf_Size symidx;
	Elf_Addr base;


	/* Perform relocations without addend if there are any: */
	for (i = 0; i < ef->nreltab; i++) {
		rel = ef->reltab[i].rel;
		if (rel == NULL) {
			link_elf_error(ef->lf.filename, "lost a reltab!");
			return (ENOEXEC);
		}
		rellim = rel + ef->reltab[i].nrel;
		base = findbase(ef, ef->reltab[i].sec);
		if (base == 0) {
			link_elf_error(ef->lf.filename, "lost base for reltab");
			return (ENOEXEC);
		}
		for ( ; rel < rellim; rel++) {
			symidx = ELF_R_SYM(rel->r_info);
			if (symidx >= ef->ddbsymcnt)
				continue;
			sym = ef->ddbsymtab + symidx;
			/* Local relocs are already done */
			if (ELF_ST_BIND(sym->st_info) == STB_LOCAL)
				continue;
			if (elf_reloc(&ef->lf, base, rel, ELF_RELOC_REL,
			    elf_obj_lookup)) {
				symname = symbol_name(ef, rel->r_info);
				printf("link_elf_obj: symbol %s undefined\n",
				    symname);
				return (ENOENT);
			}
		}
	}

	/* Perform relocations with addend if there are any: */
	for (i = 0; i < ef->nrelatab; i++) {
		rela = ef->relatab[i].rela;
		if (rela == NULL) {
			link_elf_error(ef->lf.filename, "lost a relatab!");
			return (ENOEXEC);
		}
		relalim = rela + ef->relatab[i].nrela;
		base = findbase(ef, ef->relatab[i].sec);
		if (base == 0) {
			link_elf_error(ef->lf.filename,
			    "lost base for relatab");
			return (ENOEXEC);
		}
		for ( ; rela < relalim; rela++) {
			symidx = ELF_R_SYM(rela->r_info);
			if (symidx >= ef->ddbsymcnt)
				continue;
			sym = ef->ddbsymtab + symidx;
			/* Local relocs are already done */
			if (ELF_ST_BIND(sym->st_info) == STB_LOCAL)
				continue;
			if (elf_reloc(&ef->lf, base, rela, ELF_RELOC_RELA,
			    elf_obj_lookup)) {
				symname = symbol_name(ef, rela->r_info);
				printf("link_elf_obj: symbol %s undefined\n",
				    symname);
				return (ENOENT);
			}
		}
	}

	/*
	 * Only clean SHN_FBSD_CACHED for successful return.  If we
	 * modified symbol table for the object but found an
	 * unresolved symbol, there is no reason to roll back.
	 */
	elf_obj_cleanup_globals_cache(ef);

	return (0);
}

static int
link_elf_lookup_symbol(linker_file_t lf, const char *name, c_linker_sym_t *sym)
{
	/* 都是上来先做一个类型强转 */
	elf_file_t ef = (elf_file_t) lf;
	const Elf_Sym *symp;
	const char *strp;
	int i;

	/*
		遍历symbol table，根据传入的name来查找对应的symbol
	*/
	for (i = 0, symp = ef->ddbsymtab; i < ef->ddbsymcnt; i++, symp++) {
		strp = ef->ddbstrtab + symp->st_name;
		if (symp->st_shndx != SHN_UNDEF && strcmp(name, strp) == 0) {
			*sym = (c_linker_sym_t) symp;
			return 0;
		}
	}
	return ENOENT;
}

/*
	获取已知symbol的symbol value
*/
static int
link_elf_symbol_values(linker_file_t lf, c_linker_sym_t sym,
    linker_symval_t *symval)
{
	elf_file_t ef;
	const Elf_Sym *es;
	caddr_t val;

	ef = (elf_file_t) lf;	/* 类型强转 */
	es = (const Elf_Sym*) sym;
	val = (caddr_t)es->st_value;

	/*
		当es位于ddbsymtab区间范围之内，比较的是指针的大小
		symval获取三个参数：name、value和size，然后返回
	*/
	if (es >= ef->ddbsymtab && es < (ef->ddbsymtab + ef->ddbsymcnt)) {
		symval->name = ef->ddbstrtab + es->st_name;
		val = (caddr_t)es->st_value;
		if (ELF_ST_TYPE(es->st_info) == STT_GNU_IFUNC)
			val = ((caddr_t (*)(void))val)();
		symval->value = val;
		symval->size = es->st_size;
		return 0;
	}
	return ENOENT;
}

static int
link_elf_search_symbol(linker_file_t lf, caddr_t value,
    c_linker_sym_t *sym, long *diffp)
{
	elf_file_t ef = (elf_file_t) lf;
	u_long off = (uintptr_t) (void *) value;
	u_long diff = off;
	u_long st_value;
	const Elf_Sym *es;
	const Elf_Sym *best = NULL;
	int i;

	for (i = 0, es = ef->ddbsymtab; i < ef->ddbsymcnt; i++, es++) {
		if (es->st_name == 0)
			continue;
		st_value = es->st_value;
		if (off >= st_value) {
			if (off - st_value < diff) {
				diff = off - st_value;
				best = es;
				if (diff == 0)
					break;
			} else if (off - st_value == diff) {
				best = es;
			}
		}
	}
	if (best == NULL)
		*diffp = off;
	else
		*diffp = diff;
	*sym = (c_linker_sym_t) best;

	return 0;
}

/*
 * Look up a linker set on an ELF system.
 */
static int
link_elf_lookup_set(linker_file_t lf, const char *name,
    void ***startp, void ***stopp, int *countp)
{
	elf_file_t ef = (elf_file_t)lf;
	void **start, **stop;
	int i, count;

	/* Relative to section number */
	for (i = 0; i < ef->nprogtab; i++) {
		if ((strncmp(ef->progtab[i].name, "set_", 4) == 0) &&
		    strcmp(ef->progtab[i].name + 4, name) == 0) {
			start  = (void **)ef->progtab[i].addr;
			stop = (void **)((char *)ef->progtab[i].addr +
			    ef->progtab[i].size);
			count = stop - start;
			if (startp)
				*startp = start;
			if (stopp)
				*stopp = stop;
			if (countp)
				*countp = count;
			return (0);
		}
	}
	return (ESRCH);
}

static int
link_elf_each_function_name(linker_file_t file,
    int (*callback)(const char *, void *), void *opaque)
{
	elf_file_t ef = (elf_file_t)file;
	const Elf_Sym *symp;
	int i, error;
	
	/* Exhaustive search */
	for (i = 0, symp = ef->ddbsymtab; i < ef->ddbsymcnt; i++, symp++) {
		if (symp->st_value != 0 &&
		    (ELF_ST_TYPE(symp->st_info) == STT_FUNC ||
		    ELF_ST_TYPE(symp->st_info) == STT_GNU_IFUNC)) {
			error = callback(ef->ddbstrtab + symp->st_name, opaque);
			if (error)
				return (error);
		}
	}
	return (0);
}

static int
link_elf_each_function_nameval(linker_file_t file,
    linker_function_nameval_callback_t callback, void *opaque)
{
	linker_symval_t symval;
	elf_file_t ef = (elf_file_t)file;
	const Elf_Sym* symp;
	int i, error;

	/* Exhaustive search */
	for (i = 0, symp = ef->ddbsymtab; i < ef->ddbsymcnt; i++, symp++) {
		if (symp->st_value != 0 &&
		    (ELF_ST_TYPE(symp->st_info) == STT_FUNC ||
		    ELF_ST_TYPE(symp->st_info) == STT_GNU_IFUNC)) {
			error = link_elf_symbol_values(file,
			    (c_linker_sym_t)symp, &symval);
			if (error)
				return (error);
			error = callback(file, i, &symval, opaque);
			if (error)
				return (error);
		}
	}
	return (0);
}

static void
elf_obj_cleanup_globals_cache(elf_file_t ef)
{
	Elf_Sym *sym;
	Elf_Size i;

	for (i = 0; i < ef->ddbsymcnt; i++) {
		sym = ef->ddbsymtab + i;
		if (sym->st_shndx == SHN_FBSD_CACHED) {
			sym->st_shndx = SHN_UNDEF;
			sym->st_value = 0;
		}
	}
}

/*
 * Symbol lookup function that can be used when the symbol index is known (ie
 * in relocations). It uses the symbol index instead of doing a fully fledged
 * hash table based lookup when such is valid. For example for local symbols.
 * This is not only more efficient, it's also more correct. It's not always
 * the case that the symbol can be found through the hash table.
 * 查找symbol的功能函数，这里只包含了已知symbol index的情况，不包括通过hash table查找。采用直接
 * 查找的方法效率跟准确性都会比较高，所以一般local symbol查找最好是采用这种方式
 */
static int
elf_obj_lookup(linker_file_t lf, Elf_Size symidx, int deps, Elf_Addr *res)
{
	elf_file_t ef = (elf_file_t)lf;
	Elf_Sym *sym;
	const char *symbol;
	Elf_Addr res1;

	/* Don't even try to lookup the symbol if the index is bogus. */
	if (symidx >= ef->ddbsymcnt) {
		*res = 0;
		return (EINVAL);
	}

	/* 通过传入的symidx找到要使用的符号 */
	sym = ef->ddbsymtab + symidx;

	/* Quick answer if there is a definition included. 
		如果我们查找的这个symbol不是 SHN_UNDEF 属性，仅仅做了一些类型判断，然后获取
		symbol的虚拟地址，返回。
	*/
	if (sym->st_shndx != SHN_UNDEF) {
		res1 = (Elf_Addr)sym->st_value;	/* st_value 一般保存的是虚拟地址 */
		if (ELF_ST_TYPE(sym->st_info) == STT_GNU_IFUNC)
			res1 = ((Elf_Addr (*)(void))res1)();
		*res = res1;
		return (0);
	}

	/* If we get here, then it is undefined and needs a lookup. 
		处理 SHN_UNDEF 属性的symbol
	*/
	switch (ELF_ST_BIND(sym->st_info)) {
	case STB_LOCAL:
		/* Local, but undefined? huh? */
		*res = 0;
		return (EINVAL);

	case STB_GLOBAL:
	case STB_WEAK:
		/* Relative to Data or Function name 
			从ddbstrtab中找到symbol的名字
		*/
		symbol = ef->ddbstrtab + sym->st_name;

		/* Force a lookup failure if the symbol name is bogus. */
		if (*symbol == 0) {
			*res = 0;
			return (EINVAL);
		}
		res1 = (Elf_Addr)linker_file_lookup_symbol(lf, symbol, deps);	/* 获取symbol地址 */

		/*
		 * Cache global lookups during module relocation. The failure
		 * case is particularly expensive for callers, who must scan
		 * through the entire globals table doing strcmp(). Cache to
		 * avoid doing such work repeatedly.
		 *
		 * After relocation is complete, undefined globals will be
		 * restored to SHN_UNDEF in elf_obj_cleanup_globals_cache(),
		 * above.
		 * 
		 * 在模块重定位期间缓存全局查找。对于调用方来说，失败的情况尤其昂贵，因为调用方
		 * 必须执行strcmp（）扫描整个globals表。缓存以避免重复执行此类工作
		 * 重新定位完成后，未定义的全局项将恢复到上面elf_obj_cleanup_globals_cache（）中的SHN_UNDEF
		 */
		if (res1 != 0) {
			sym->st_shndx = SHN_FBSD_CACHED;
			sym->st_value = res1;
			*res = res1;
			return (0);
		} else if (ELF_ST_BIND(sym->st_info) == STB_WEAK) {
			sym->st_value = 0;
			*res = 0;
			return (0);
		}
		return (EINVAL);

	default:
		return (EINVAL);
	}
}

static void
link_elf_fix_link_set(elf_file_t ef)
{
	static const char startn[] = "__start_";
	static const char stopn[] = "__stop_";
	Elf_Sym *sym;
	const char *sym_name, *linkset_name;
	Elf_Addr startp, stopp;
	Elf_Size symidx;	/* symbol index */
	int start, i;

	startp = stopp = 0;

	/* 
		对符号表进行遍历 
	*/
	for (symidx = 1 /* zero entry is special */;
		symidx < ef->ddbsymcnt; symidx++) {
		sym = ef->ddbsymtab + symidx;
		/*
			SHN_UNDEF:
				此节表索引表示符号未定义。当链接编辑器将此对象文件与另一个定义指定符号的文件组合时，
				此文件对符号的引用将链接到实际定义
			下面的逻辑是当symbol的 st_shndx 不是 SHN_UNDEF 类型的时候，执行continue，说明后边
			要处理的类型都是 SHN_UNDEF 类型的，再根据上述解释可以看到，SHN_UNDEF 表示的应该就是外部
			引用，所以才会说当与外部的文件链接的时候，我们才会将对符号的引用链接到实际的定义

			再进一步看，这个函数是嵌套在 link_elf_reloc_local 函数当中的，从函数名字看是进行本地重定位，
			可能不涉及外部引用，所以这里利用这个函数做一些操作，使外部引用不会影响到本地重定位？？
		*/
		if (sym->st_shndx != SHN_UNDEF)
			continue;

		/* 
			所以下面的操作都是针对 SHN_UNDEF 类型的symbol (引用的外部的量？？)
			sym->st_name 其实是一个index，ef->ddbstrtab 加上这个index就可以获得相应的symbol name
		*/
		sym_name = ef->ddbstrtab + sym->st_name;
		if (strncmp(sym_name, startn, sizeof(startn) - 1) == 0) {
			start = 1;
			/* 
				相当于是把前面 __start_ 给过滤掉，这就跟linker_set.h文件中的一些宏定义对照上 
			*/
			linkset_name = sym_name + sizeof(startn) - 1;
		}
		else if (strncmp(sym_name, stopn, sizeof(stopn) - 1) == 0) {
			start = 0;
			linkset_name = sym_name + sizeof(stopn) - 1;
		}
		else
			continue;

		for (i = 0; i < ef->nprogtab; i++) {
			if (strcmp(ef->progtab[i].name, linkset_name) == 0) {
				startp = (Elf_Addr)ef->progtab[i].addr;
				stopp = (Elf_Addr)(startp + ef->progtab[i].size);
				break;
			}
		}
		if (i == ef->nprogtab)
			continue;

		sym->st_value = start ? startp : stopp;
		sym->st_shndx = i;
	}
}

/*
	本地重定位，从代码逻辑可以看出，我们遍历REL和RELA类型的section中的entry，但是真正用来处理
	的却是entry中 r_info(r_info：重定位的类型+符号索引)所指向的symbol。所以，重定位最重要的
	一个环节其实是符号解析
*/
static int
link_elf_reloc_local(linker_file_t lf, bool ifuncs)
{
	elf_file_t ef = (elf_file_t)lf;
	const Elf_Rel *rellim;
	const Elf_Rel *rel;
	const Elf_Rela *relalim;
	const Elf_Rela *rela;
	const Elf_Sym *sym;
	Elf_Addr base;
	int i;
	Elf_Size symidx;

	/*
		首先是对所有的外部符号引用进行了处理？？
		然后是RLE 和 RELA 两种类型的重定位表
	*/
	link_elf_fix_link_set(ef);

	/* Perform relocations without addend if there are any: */
	for (i = 0; i < ef->nreltab; i++) {
		rel = ef->reltab[i].rel;
		if (rel == NULL) {
			link_elf_error(ef->lf.filename, "lost a reltab");
			return (ENOEXEC);
		}
		/* rel limit，意思就是该指针指向table结尾的entry */
		rellim = rel + ef->reltab[i].nrel;
		base = findbase(ef, ef->reltab[i].sec);
		if (base == 0) {
			link_elf_error(ef->lf.filename, "lost base for reltab");
			return (ENOEXEC);
		}
		for ( ; rel < rellim; rel++) {
			/*
				ELF_R_SYM 宏主要实现了拼接的功能，形式为 ELF32/64_R_SYM(rel->r_info),
				就是为了获取重定位以后需要指向的符号，再从结果来看，这里获取到的是一个index
			*/
			symidx = ELF_R_SYM(rel->r_info);
			if (symidx >= ef->ddbsymcnt)
				continue;

			/* 获取到重定位后需要指向的符号 */
			sym = ef->ddbsymtab + symidx;	

			/* 
				Only do local relocs 本地重定位，所以要判断symbol的属性
				ELF32_ST_BIND: 获取symbol绑定属性，这个函数是要处理local重定位，所以要判断一下symbol绑定的属性
				是不是 STB_LOCAL(属性就表示是本地的，还是外部的)
			*/
			if (ELF_ST_BIND(sym->st_info) != STB_LOCAL)
				continue;
			
			/*
				再判断symbol的类型，symbol表示的是函数、变量等等
			*/
			if ((ELF_ST_TYPE(sym->st_info) == STT_GNU_IFUNC ||
			    elf_is_ifunc_reloc(rel->r_info)) == ifuncs)
				elf_reloc_local(lf, base, rel, ELF_RELOC_REL,
				    elf_obj_lookup);
		}
		/*
			从上述代码可以看到，REL/RELA section中的entry其实存储就是关于symbol的信息，所以这两个表
			可以理解为有哪些symbol需要重定位，然后我就把它放到里边，然后我们重定位的时候遍历这些表就行了
		*/
	}	//end RLE

	/* Perform relocations with addend if there are any: */
	for (i = 0; i < ef->nrelatab; i++) {
		rela = ef->relatab[i].rela;
		if (rela == NULL) {
			link_elf_error(ef->lf.filename, "lost a relatab!");
			return (ENOEXEC);
		}
		relalim = rela + ef->relatab[i].nrela;
		base = findbase(ef, ef->relatab[i].sec);
		if (base == 0) {
			link_elf_error(ef->lf.filename, "lost base for reltab");
			return (ENOEXEC);
		}
		for ( ; rela < relalim; rela++) {
			symidx = ELF_R_SYM(rela->r_info);
			if (symidx >= ef->ddbsymcnt)
				continue;
			sym = ef->ddbsymtab + symidx;
			/* Only do local relocs */
			if (ELF_ST_BIND(sym->st_info) != STB_LOCAL)
				continue;
			if ((ELF_ST_TYPE(sym->st_info) == STT_GNU_IFUNC ||
			    elf_is_ifunc_reloc(rela->r_info)) == ifuncs)
				elf_reloc_local(lf, base, rela, ELF_RELOC_RELA,
				    elf_obj_lookup);
		}
	}	// end RELA
	return (0);
}

static long
link_elf_symtab_get(linker_file_t lf, const Elf_Sym **symtab)
{
    elf_file_t ef = (elf_file_t)lf;
    
    *symtab = ef->ddbsymtab;
    
    if (*symtab == NULL)
        return (0);

    return (ef->ddbsymcnt);
}
    
static long
link_elf_strtab_get(linker_file_t lf, caddr_t *strtab)
{
    elf_file_t ef = (elf_file_t)lf;

    *strtab = ef->ddbstrtab;

    if (*strtab == NULL)
        return (0);

    return (ef->ddbstrcnt);
}
