/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996-1998 John D. Polstra.
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
 * $FreeBSD: releng/12.0/sys/sys/elf32.h 326256 2017-11-27 15:01:59Z pfg $
 */

#ifndef _SYS_ELF32_H_
#define _SYS_ELF32_H_ 1

#include <sys/elf_common.h>

/*
 * ELF definitions common to all 32-bit architectures.
 */

typedef uint32_t	Elf32_Addr;
typedef uint16_t	Elf32_Half;
typedef uint32_t	Elf32_Off;
typedef int32_t		Elf32_Sword;
typedef uint32_t	Elf32_Word;
typedef uint64_t	Elf32_Lword;

typedef Elf32_Word	Elf32_Hashelt;

/* Non-standard class-dependent datatype used for abstraction. */
typedef Elf32_Word	Elf32_Size;
typedef Elf32_Sword	Elf32_Ssize;


/*
 * ELF header.
 * 包含了program header table和section header table的相关信息
 */
typedef struct {
	/*
		初始字节将文件标记为目标文件，并提供与机器无关的数据，用于解码和解释文件的内容
	*/
	unsigned char	e_ident[EI_NIDENT];	/* File identification. */
	Elf32_Half	e_type;		/* File type. 文件类型 */
	Elf32_Half	e_machine;	/* Machine architecture. 体系结构-Intel Mips... */
	/*
		值1表示原始文件格式；扩展名将创建具有更高数字的新版本。EV_CURRENT的值，尽管上面给出了1，但会根据需要改变以反映当前版本号
	*/
	Elf32_Word	e_version;	/* ELF format version. */
	/*
		此成员提供系统首先将控制传输到的虚拟地址，从而启动进程。如果文件没有关联的入口点，则此成员保留零
		推测应该是系统将控制权交给program时的地址？
	*/
	Elf32_Addr	e_entry;	/* Entry point. */
	/*
		 program header table 的offset， program header table告诉操作系统如何构建一个进程映像，作用非常重要
	*/
	Elf32_Off	e_phoff;	/* Program header file offset. */
	Elf32_Off	e_shoff;	/* Section header file offset. 从文件起始位置到section header table */
	Elf32_Word	e_flags;	/* Architecture-specific flags. 体系架构相关 */
	Elf32_Half	e_ehsize;	/* Size of ELF header in bytes. ELF Header的大小(bytes) */

	/* program header table 中的entry的大小，每个entry的大小都是一致的 */
	Elf32_Half	e_phentsize;	/* Size of program header entry. */
	/*
		如果e_phnum的值为PN_XNUM（即0xffff），则表示program header的数量超过了它所能存储的最大值，因此它的值
		另外保存在索引号为SHN_UNDEF（即0）的section的Elf32_Shdr. sh_info的位置
	*/
	Elf32_Half	e_phnum;	/* Number of program header entries. */
	/* 
		section header table entry 的大小，类比上面；由于每个section header的大小是固定的，而它们的名字和属性
		不可能是一样长的，所以需要一个专门的string section来保存它们的名称属性，而用来描述这个string section的
		section header在section表中的位置就由 e_shstrndx 来确定，实现快速查找
		位置 = e_shoff + e_shentsize * e_shstrndx
	*/
	Elf32_Half	e_shentsize;	/* Size of section header entry. */
	Elf32_Half	e_shnum;	/* Number of section header entries. */
	/* 
		此成员保存与section name string table关联的项的节标题表索引
		指向保存section name strings的section，还是通过index获取名称？后者可能性大一些
	*/
	Elf32_Half	e_shstrndx;	/* Section name strings section. */
} Elf32_Ehdr;


/*
 * Shared object information, found in SHT_MIPS_LIBLIST.
 */
typedef struct {
	Elf32_Word l_name;		/* The name of a shared object. */
	Elf32_Word l_time_stamp;	/* 32-bit timestamp. */
	Elf32_Word l_checksum;		/* Checksum of visible symbols, sizes. */
	Elf32_Word l_version;		/* Interface version string index. */
	Elf32_Word l_flags;		/* Flags (LL_*). */
} Elf32_Lib;


/*
 * Section header.
 * Section header table用于查找所有的section(凡是涉及到table的，应该是都是对应类型的一个数组，
 * 动态链接时肯定会涉及到对这些数组的操作)，array中的元素都是Elf32_Shdr类型，也就是对于每个section，
 * section header都会采用 Elf32_Shdr 一个结构体来进行管理
 * 
 * 在ELF文件中，每个section header都要一个索引号，从1开始，依次递增。不过section header表的开头必须
 * 定义一个SHT_NULL类型的表项，它的索引号为SHN_UNDEF(也就是0)。由于section header使用相同的数据结构
 * 来定义，并且它们的大小相同，所以所有的section header依次存储在一起看起来就是一个表，因此可以叫它section header表。
 * 类似的，所有program header看起来也是一个表。
 */
typedef struct {
	/*
		可以注意到，结构体中有关name的，已知的都是保存一个string table中的index，而不会自己真正去
		保存一个name string(string table 貌似是在一个section当中)
		string table一般用于保存symbol和section的名称，字符串表索引可以引用节中的任何字节。一个字符串
		可能出现多次；对子字符串的引用可能存在；一个字符串可能被多次引用。也允许使用未引用的字符串

		sh_name 表示section的名称。由于每个section名称长度都是不一样的，为了节约空间，就把所有的section的
		名称都放在了一个特定的名叫 .shstrtab 的section(ELF header中的 Elf32_Ehdr. e_shstrndx 就是用来
		快速查找它的)中，所以这里的 sh_name 的值就表示在这个特定section中的偏移量，通过它可以获取一个字符串，
		也就是我们所需要的section的名称。0的话表示无名称，一般用在类型为 SHT_NULL 的section当中
		每个ELF文件中都有一个 SHT_NULL 类型的section，它只有section header，没有相应的section数据，并且
		section header中的数据都为0，没有意义
	*/
	Elf32_Word	sh_name;	/* Section name (index into the section header string table). */
	Elf32_Word	sh_type;	/* Section type. 参考elf_common.h中的定义 */
	Elf32_Word	sh_flags;	/* Section flags. section支持描述各种属性的1位标志 */
	/*
		当这个section出现在进程映像当中的时候，这个成员会给出该section的第一个byte所处的位置，否则值为0
		表示该section在进程空间中的虚拟地址(首地址)，如果该成员为0，就表示该section不会出现在进程空间中
	*/
	Elf32_Addr	sh_addr;	/* Address in memory image. */
	/*
		描述了从文件起始位置到section第一个byte的偏移量。下面描述的一个节类型SHT_NOBITS不占用文件中的空间，
		它的sh_offset成员定位文件中的概念位置。
	*/
	Elf32_Off	sh_offset;	/* Offset in file. */
	/*
		此成员以字节为单位给出节的大小。除非节类型是SHT_NOBITS，否则该节将占用文件中sh_size字节。SHT_NOBITS
		类型的节的大小可能不为零，但它在文件中不占用任何空间。
	*/
	Elf32_Word	sh_size;	/* Size in bytes. */
	/*
		此成员持有一个节头表索引链接(section header table index link)，其解释取决于节类型
		个人理解应该是这个section有关联的section，sh_link 就是它所关联的section在section header table中
		的index？？ - 应该不是这样理解的，这里可以参考一下ELF_Format.pdf文档中19页的相关表述，不同类型的section
		中 sh_link 所表示的内容是不一样的，sh_info也是同样的机制

		sh_link表示链接到其他section header的索引号，sh_info表示附加的段信息。这两个成员的值只对一些类型有效，
		其他类型的section则都为0。
	*/
	Elf32_Word	sh_link;	/* Index of a related section. */
	Elf32_Word	sh_info;	/* Depends on section type. */
	/*
		有些部分有地址对齐限制。例如，如果节包含双字，则系统必须确保整个节的双字对齐。也就是说，sh_addr的值必须等于0，
		与sh_addralign的值相模(取模/取余)。目前只允许0和2的正整数幂。值0和1表示section没有对齐约束
	*/
	Elf32_Word	sh_addralign;	/* Alignment in bytes. */
	/*
		有些部分包含固定大小的条目表，例如符号表。对于这样的节，该成员给出每个条目的大小（以字节为单位）。如果节不包含
		固定大小条目的表，则成员包含0。因为一些section包含的是一个table，table的话基本上就是一个数组，数组元素的大小
		一般是固定的，这个成员应该就是表示table element的大小
	*/
	Elf32_Word	sh_entsize;	/* Size of each entry in section. */
} Elf32_Shdr;


/*
 * Program header.
 * ELF文件中包含有 Program header Table，本质上是一个 Elf32_Phdr 类型的数组，它所描述的对象是segment，而不是section
 * segment是将类型相同的section组合在一起形成的，方便链接
 * 一些entry用来描述process segments，其他的entry给出了一些附加信息，但是对process image没有什么实质性的帮助
 * 
 * 可执行文件和共享对象文件有一个基址，它是与程序目标文件的内存映像关联的最低虚拟地址。基址的一个用途是在动态链接期间重新定位程序的内存映像；
 * 一个可执行的文件或者共享文件在运行时的基地址通过三个值来计算：程序可加载段的内存加载地址、最大页大小和最低虚拟地址；要计算基址，需要确定
 * 与PT_LOAD segment 的最低p_vaddr值相关联的内存地址。然后通过将内存地址截短为最大页面大小的最接近倍数来获得基址。根据加载到内存中的文件类型，
 * 内存地址可能与p_vaddr值匹配，也可能不匹配
 * 
 * .bss section虽然在文件中不占用空间，但它有助于段的内存映像。通常，这些未初始化的数据驻留在段的末尾，从而使相关程序头元素中的p_memsz大于
 * p_filesz
 * setion header用于描述section的特性，而program header则是用于描述segment的特性。目标文件(.o结尾)不存在program header，因为它不能
 * 运行，也就是说只有可执行的文件才有program header。一个segment包含有一个或者多个现有的section，相当于是从程序运行的角度来看这些section，
 * 也就是说segment本质上还是section
 */
typedef struct {
	Elf32_Word	p_type;		/* Entry type. 描述segment或者解释该元素的相关信息，参考elf_common.h文件定义 */
	Elf32_Off	p_offset;	/* File offset of contents. 描述从文件开头的偏移量，segment的第一个字节位于该位置处*/
	Elf32_Addr	p_vaddr;	/* Virtual address in memory image. 该segment的第一个byte在虚拟内存中的地址 */
	/*
		在与物理寻址相关的系统上，此成员为段的物理地址保留。由于System V忽略应用程序的物理寻址，
		此成员对可执行文件和共享对象具有未指定的内容

		在现代常见的体系架构中，很少直接使用物理地址，所以这里p_paddr的值与p_vaddr相同
	*/
	Elf32_Addr	p_paddr;	/* Physical address (not used). */
	/*
		通过p_offset和p_filesz两个成员就可以获得相应segment中的所有内容，所以这里就不再需要section header的支持，但需要ELF文件头中的信息
		来确定program header表（每个program header的大小相同）的开头位置，因此ELF文件头（它包含在第一个LOAD segment中）也要加载到内存中
	*/
	Elf32_Word	p_filesz;	/* Size of contents in file. 该segment在 file image中所占大小，bytes */
	Elf32_Word	p_memsz;	/* Size of contents in memory. 该segment在 memory image中所占大小，bytes*/
	Elf32_Word	p_flags;	/* Access permission flags. */
	/*
		p_align表示segment的对齐方式(也就是p_offset和p_filesz对p_align取模的余数为0)。它的值可能是2的倍数，0和1表示不采用对齐方式；
		p_align的对齐方式不仅针对虚拟地址（p_vaddr），在ELF文件中的偏移量（p_offset）也要采用与虚拟地址相同的对齐方式。PT_LOAD类型的
		segment需要针对所在操作系统的页对齐
	*/
	Elf32_Word	p_align;	/* Alignment in memory and file. 对齐方式 */
} Elf32_Phdr;


/*
 * Dynamic structure.  The ".dynamic" section contains an array of them.
 * .dynamic section中会包含有 Elf32_Dyn 类型的数组，其中应该会包含有多种类型结构体，
 * 动态链接的过程中对数组遍历查找？
 */
typedef struct {
	Elf32_Sword	d_tag;		/* Entry type. 不同d_tag类型，union会有不同的值 */
	union {
		Elf32_Word	d_val;	/* Integer value. */
		Elf32_Addr	d_ptr;	/* Address value. */
	} d_un;
} Elf32_Dyn;


/*
 * Relocation entries.
 * 重定位就是连接符号引用和符号定义的一个过程，例如当我们调用函数的时候，关联的指令必须要能够将控制权传递到
 * 正确的目标地址才可以。这里就会涉及到可执行文件对某些section一些信息的修改，所以我们要保留描述信息来告诉
 * 可执行文件如何去实施具体的操作，这个应该就是Relocation entry存在的意义
 * 从结构体定义来看，跟重定位相关的主要就是offset，重定位的类型，符号的index，或者addend，所以重点要弄清楚
 * 它们之间的关系以及操作流程是什么样的
 * 
 * ELF文件装载(load？？)
 * 在ELF文件中，使用section和program两种结构描述文件的内容。通常来说，ELF可重定位文件采用section，可执行文件则采用
 * program，可重链接文件则是两种都有。装载文件其实就是通过section或者program中的type属性判断是否需要加载，然后通过
 * offset属性找到文件中的数据，将其读取(复制)到相应的内存位置即可。这个位置，就可以通过program中的vaddr属性确定；对于
 * section来说，则可以自己定义装载位置
 * 
 * ELF重定位
 * 动态链接的本质就是对ELF文件进行重定位和符号解析。重定位使得ELF文件可以在任何地方执行(普通程序在连接时会给定一个固定
 * 执行的地址)；符号解析，使得ELF文件可以引用动态数据(链接时不存在的数据)。符号解析是重定位流程的一个分支
 * 例如一段汇编代码：
 * 	jmp dxc
 * 	dxc：
 * 	...
 * 假设dxc这个标号的地址1000h，那么在编译链接之后，就变成了1000h；如果我们在运行时候移动了程序的位置，1000h就会指向错误的
 * 地址(因为我们已经不在那里了)。而当我们使用了外部符号的时候(例如动态链接库里面的函数)，在链接时根本就不知道这个符号在哪里，
 * 所以也就没办法生成这个1000h地址。重定位的目的就是在运行的时候修改1000h这个地址，使其指向正确的位置，此时我们需要三个数据:
 * 	1、进行重定位的地址，也就是jmp 1000h这条指令中操作数的地址，也就是1000h自己在内存中的地址(可以想象成C指针自己的存储地址&point)
 * 	2、需要指向的符号，就是上述例子中的dxc这个标号。通过对这个符号进行解析，就可以得到运行时该符号的正确地址(可以想象成C指针point指向的地址)
 * 	3、重定位的类型，比如R_386_32表示绝对地址的重定位；R_386_PC32表示相对地址的重定位。前者可以直接使用符号的地址，后者则需要
 * 	   用“符号地址 - 重定位地址”得出相对地址
 * 重定位表：由多个重定位表项组成的数组
 * 
 * 每个应用程序通过编译器编译出来之后，地址都是从零开始的虚拟地址，然后操作系统通过内存管理的机制，使得不同的应用程序即使拥有相同的虚拟地址，
 * 也不会产生冲突。而重定位就是针对程序的虚拟地址来说的，如上所说，1000h这个地址我们就可以不必写死，可以通过基地址+offset等等方式来计算，
 * 实现起来更加灵活
 */
/* 
	Relocations that don't need an addend field. 
	不需要加数域的重定位
*/
typedef struct {
	/* Location to be relocated. 需要进行重定位的地址，base address + r_offset 
		另外一种说法，offset指的是需要被重定位的引用在其节中的偏移。symbol也是存放在某个section当中的，所以我们要找到
		某个symbol，首先是要找到这个symbol所在的section，然后再根据这个offset确定其具体位置，上述公式就可以表示为：
		section begging address + offset
	*/
	Elf32_Addr	r_offset;	
	/*
		SYMBOL << 8 + TYPE & 0xff，其中 SYMBOL是重定位以后需要指向的符号；TYPE是重定位的类型
	*/
	Elf32_Word	r_info;		/* Relocation type and symbol index. */
} Elf32_Rel;


/* Relocations that need an addend field. */
typedef struct {
	Elf32_Addr	r_offset;	/* Location to be relocated. */
	Elf32_Word	r_info;		/* Relocation type and symbol index. */
	Elf32_Sword	r_addend;	/* Addend. */
} Elf32_Rela;

/* Macros for accessing the fields of r_info. */
#define ELF32_R_SYM(info)	((info) >> 8)
#define ELF32_R_TYPE(info)	((unsigned char)(info))

/* Macro for constructing r_info from field values. */
#define ELF32_R_INFO(sym, type)	(((sym) << 8) + (unsigned char)(type))

/*
 *	Note entry header
 */
typedef Elf_Note Elf32_Nhdr;

/*
 *	Move entry
 */
typedef struct {
	Elf32_Lword	m_value;	/* symbol value */
	Elf32_Word 	m_info;		/* size + index */
	Elf32_Word	m_poffset;	/* symbol offset */
	Elf32_Half	m_repeat;	/* repeat count */
	Elf32_Half	m_stride;	/* stride info */
} Elf32_Move;

/*
 *	The macros compose and decompose values for Move.r_info
 *
 *	sym = ELF32_M_SYM(M.m_info)
 *	size = ELF32_M_SIZE(M.m_info)
 *	M.m_info = ELF32_M_INFO(sym, size)
 */
#define	ELF32_M_SYM(info)	((info)>>8)
#define	ELF32_M_SIZE(info)	((unsigned char)(info))
#define	ELF32_M_INFO(sym, size)	(((sym)<<8)+(unsigned char)(size))

/*
 *	Hardware/Software capabilities entry
 */
typedef struct {
	Elf32_Word	c_tag;		/* how to interpret value */
	union {
		Elf32_Word	c_val;
		Elf32_Addr	c_ptr;
	} c_un;
} Elf32_Cap;


/*
 * Symbol table entries.
 * 对象文件的符号表包含定位和重新定位程序的符号定义和引用所需的信息。符号表索引是此数组的下标。
 * 索引0同时指定表中的第一个条目，并用作未定义的符号索引。印证上述所说的，table一般都是一个
 * 数组。这里，一个 Elf32_Sym 对应着一个symbol
 * 
 * 在每个符号表中，所有具有 STB_LOCAL 绑定的符号都在弱符号和全局符号之前。如上所述，
 * symbol table section的 sh_info成员保存第一个non_local符号表index。也就是sh_info不会
 * 指向符号表的起始位置，除非没有local symbol
 * 符号表所有项的大小相同，都为16个字节，所以.dynsym section 和 .symtab section 也被叫做符号表，并且每一个
 * 表项都会有一个索引值，而符号表的第一个表项的索引值必须是 STN_UNDEF(也就是0)的无效项。索引值 STN_UNDEF 在
 * 符号表的散列算法中可以被当做链尾，即表示它不引用有效的符号
 */
typedef struct {
	/*
		该成员保存相应字符串表中的index，进而获取symbol name；如果该值为非零，则表示给出符号名称的字符串表索引。否则，
		符号表条目没有名称。如.dynsym section中所有表项的符号名要从.dynstr section中获取，而.symtab是从.strtab中
		获取。而.dynstr和.strtab都是字符串表
	*/
	Elf32_Word	st_name;	/* String table index of name. */
	/*
		此成员给出关联符号的值。根据上下文，这可能是绝对值、地址等，但是一般表示的是虚拟地址
	*/
	Elf32_Addr	st_value;	/* Symbol value. */
	/*
		许多符号都有关联的大小。比如对于变量，它的值就是变量的数据类型（如int则它的值可能为4个字节）的大小。如果为0则表示
		该符号无需大小或大小未知。
	*/
	Elf32_Word	st_size;	/* Size of associated object. */
	/* 
		Type and binding information. 成员st_info的大小为一个字节，低4位表示符号类型，高4位表示符号的绑定特征 
		BIND << 4 + TYPE & 0x0f，BIND表示符号是内部符号还是外部符号，TYPE表示符号的类型，函数或者变量等
	*/
	unsigned char	st_info;	
	unsigned char	st_other;	/* Reserved (not used). 目前的值是0，未被使用，也没有任何意义 */
	/*
		symbol table entry中的每一个entry都是跟某一个section相关联，该成员保存了相关联的section header
		table index，也就是说symbol可以通过 st_shndx 找到相关联的section，其中一些index有着特殊的含义，
		参考 STB_LOCAL-elf_common.h 等定义，符号的绑定决定了链接的可见性和行为

		如果一个符号引用了一个section中的特定的位置，那么 st_shndx 成员就会保留一个section header table的
		index；当这个section由于重定位移动的时候，symbol value也会跟着改变，对符号的引用仍然会指向程序中的同
		一个位置？？

		st_shndx 表示符号所在的section对应的section header的索引号,它表示的应该是这个符号位于哪个section中
	*/
	Elf32_Half	st_shndx;	/* Section index of symbol. */
} Elf32_Sym;

/* Macros for accessing the fields of st_info. */
#define ELF32_ST_BIND(info)		((info) >> 4)	/* 获取symbol绑定的属性 */
#define ELF32_ST_TYPE(info)		((info) & 0xf)	/* 获取symbol的类型 */

/* Macro for constructing st_info from field values. 获取symbol info */
#define ELF32_ST_INFO(bind, type)	(((bind) << 4) + ((type) & 0xf))

/* Macro for accessing the fields of st_other. */
#define ELF32_ST_VISIBILITY(oth)	((oth) & 0x3)

/* Structures used by Sun & GNU symbol versioning. */
typedef struct
{
	Elf32_Half	vd_version;
	Elf32_Half	vd_flags;
	Elf32_Half	vd_ndx;
	Elf32_Half	vd_cnt;
	Elf32_Word	vd_hash;
	Elf32_Word	vd_aux;
	Elf32_Word	vd_next;
} Elf32_Verdef;

typedef struct
{
	Elf32_Word	vda_name;
	Elf32_Word	vda_next;
} Elf32_Verdaux;

typedef struct
{
	Elf32_Half	vn_version;
	Elf32_Half	vn_cnt;
	Elf32_Word	vn_file;
	Elf32_Word	vn_aux;
	Elf32_Word	vn_next;
} Elf32_Verneed;

typedef struct
{
	Elf32_Word	vna_hash;
	Elf32_Half	vna_flags;
	Elf32_Half	vna_other;
	Elf32_Word	vna_name;
	Elf32_Word	vna_next;
} Elf32_Vernaux;

typedef Elf32_Half Elf32_Versym;

typedef struct {
	Elf32_Half	si_boundto;	/* direct bindings - symbol bound to */
	Elf32_Half	si_flags;	/* per symbol flags */
} Elf32_Syminfo;

typedef struct {
	Elf32_Word	ch_type;
	Elf32_Word	ch_size;
	Elf32_Word	ch_addralign;
} Elf32_Chdr;

//////////////////////////////// Special Sections /////////////////////////
/* 
	各个部分保存程序和控制信息。以下列表中的部分由系统使用，并具有指示的类型和属性 
	|名称	|类型	|属性

	.bss		SHT_NOBITS		SHF_ALLOC + SHF_WRITE
	.comment 	SHT_PROGBITS 	none
	.data 		SHT_PROGBITS 	SHF_ALLOC + SHF_WRITE
	.data1 		SHT_PROGBITS 	SHF_ALLOC + SHF_WRITE
	.debug 		SHT_PROGBITS 	none
	.dynamic 	SHT_DYNAMIC 	see below
	.dynstr 	SHT_STRTAB 		SHF_ALLOC
	.dynsym 	SHT_DYNSYM 		SHF_ALLOC
	.fini 		SHT_PROGBITS 	SHF_ALLOC + SHF_EXECINSTR
	.got 		SHT_PROGBITS 	see below
	.hash 		SHT_HASH 		SHF_ALLOC
	.init 		SHT_PROGBITS	SHF_ALLOC + SHF_EXECINSTR
	.interp 	SHT_PROGBITS 	see below
	.line 		SHT_PROGBITS 	none
	.note 		SHT_NOTE 		none
	.plt 		SHT_PROGBITS 	see below
	.relname 	SHT_REL 		see below
	.relaname 	SHT_RELA 		see below
	.rodata 	SHT_PROGBITS 	SHF_ALLOC
	.rodata1 	SHT_PROGBITS 	SHF_ALLOC
	.shstrtab 	SHT_STRTAB 		none
	.strtab 	SHT_STRTAB 		see below
	.symtab 	SHT_SYMTAB 		see below
	.text 		SHT_PROGBITS 	SHF_ALLOC + SHF_EXECINSTR

	.bss: 此部分保存有助于程序内存映像的未初始化数据。根据定义，当程序开始运行时，系统用零初始化数据。
		  节不占用任何文件空间，如节类型SHT_NOBITS所示
	
	.comment: 本节保存版本控制信息

	.data & .data1: 这些部分保存有助于程序内存映像的初始化数据

	.debug: 此部分保存符号调试的信息。内容未指定

	.dynamic: 此部分保存动态链接信息。该部分的属性将包括SHF_ALLOC位。是否设置SHF_写入位取决于处理器

	.dynstr: 此部分包含动态链接所需的字符串，最常见的字符串表示与symbol table entry关联的名称

	.dynsym: 此部分包含动态链接符号表

	.fini: 本节包含可执行指令，这些指令有助于进程终止代码。也就是说，当程序正常退出时，系统会安排执行
		   本节中的代码
	
	.hash: section保存hash table

	.interp: 此部分保存程序解释器的路径名。如果文件有一个包含段的可加载段，则该段的属性将包括SHF_ALLOC位；
			 否则，该位将off(解释器好像就是dynamic linker)
	
	.line: 本节保存用于符号调试的行号信息，用于描述源程序和机器代码之间的对应关系。内容未指定

	.note: 参考note section

	.relname & .relaname: 这些部分包含重新定位信息，如下所述。如果文件有包含重定位的可加载段，则节的属性将
		   包括SHF_ALLOC位；否则，该位将关闭。按照惯例，名称由重新定位应用的部分提供。因此，.text的重定位节
		   通常具有名称.rel.text 或者.rela.text

	.rodata & .rodatal: 这些部分保存只读数据，这些数据通常有助于进程映像中的不可写段

	.shstrtab: 该section保留section name（section header string table？）

	.strtab: 此部分包含字符串，最常见的是表示与symbol table entry关联的名称的字符串。如果文件包含包含符号字符串表的
		   可加载段，则该节的属性将包括SHF_ALLOC位；否则，该位将off

	.symtab: 本节包含一个符号表，如本节中的“符号表”所述。如果文件包含包含符号表的可加载段，则该节的属性将包括SHF_ALLOC位；
		   否则，该位将off

	.text: 本节保存程序的“文本”或可执行指令

	带有点.前缀的节名是为系统保留的，尽管应用程序可以使用这些节，前提是它们的现有含义令人满意。应用程序可以使用不带前缀的
	名称，以避免与系统节冲突。对象文件格式允许定义不在上面列表中的部分。一个对象文件可以有多个同名的节
	为处理器体系结构保留的节名称是通过在节名称前面放置体系结构名称的缩写来形成的。该名称应取自用于e_machine的体系结构名称。
	例如。食品.psect是由FOO架构定义的psect部分。现有的扩展按其历史名称调用
*/


#endif /* !_SYS_ELF32_H_ */
