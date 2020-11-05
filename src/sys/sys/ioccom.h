/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)ioccom.h	8.2 (Berkeley) 3/28/94
 * $FreeBSD: releng/12.0/sys/sys/ioccom.h 331077 2018-03-16 22:23:04Z brooks $
 */

#ifndef	_SYS_IOCCOM_H_
#define	_SYS_IOCCOM_H_

/*
 * Ioctl's have the command encoded in the lower word, and the size of
 * any in or out parameters in the upper word.  The high 3 bits of the
 * upper word are used to encode the in/out status of the parameter.
 */
#define	IOCPARM_SHIFT	13		/* number of bits for ioctl size */
#define	IOCPARM_MASK	((1 << IOCPARM_SHIFT) - 1) /* parameter length mask */
#define	IOCPARM_LEN(x)	(((x) >> 16) & IOCPARM_MASK)
#define	IOCBASECMD(x)	((x) & ~(IOCPARM_MASK << 16))
#define	IOCGROUP(x)	(((x) >> 8) & 0xff)

#define	IOCPARM_MAX	(1 << IOCPARM_SHIFT) /* max size of ioctl */
#define	IOC_VOID	0x20000000	/* no parameters */
#define	IOC_OUT		0x40000000	/* copy out parameters */
#define	IOC_IN		0x80000000	/* copy in parameters */
#define	IOC_INOUT	(IOC_IN|IOC_OUT)
#define	IOC_DIRMASK	(IOC_VOID|IOC_OUT|IOC_IN)

#define	_IOC(inout,group,num,len)	((unsigned long) \
	((inout) | (((len) & IOCPARM_MASK) << 16) | ((group) << 8) | (num)))

/*
    paramters:
        g: 表示组号(group)，应该是一个8位的魔数，我们可以选择任意值，只在驱动程序中保持一致即可
        n: 表示序数，用于将本驱动程序中的各个ioctl命令分开
        t: 表示输入输出操作所传输的数据类型。很显然，_IO宏没有参数t，因为它所定义的命令不会引发
           数据传输
    
    魔数，其实也称为神奇数字，我们大多数人是在学习计算机过程中接触到这个词的。它被用来为重要的数据定义标签，
    用独特的数字唯－地标识该数据，这种独特的数字是只有少数人才能掌握其奥秘的“神秘力量”。故，直接出现的一个
    数字，只要其意义不明确，感觉很诡异，就称之为魔数。魔数应用的地方太多了，如elf 文件头：
    ELF Header :
        Magic : 7f 45 4c 46 01 01 01 00 00 00 00 00 00 00 00 00
    这个Magic 后面的一长串就是魔数， elf 解析器（通常是程序加载器）用它来校验文件的类型是否是elf
*/

/*  
    创建一个不传输数据的输入输出操作ioctl命令，也就是这类命令的d_ioctl函数中参数data是
    没有用的，例如弹出可移动媒体
*/
#define	_IO(g,n)	_IOC(IOC_VOID,	(g), (n), 0)
#define	_IOWINT(g,n)	_IOC(IOC_VOID,	(g), (n), sizeof(int))

/*
    创建一个读操作的ioctl命令，该操作可以从设备向用户空间传输数据，例如：获取出错信息
    与_IO宏的主要区别就是 IOC_OUT - IOC_VOID，表示命令的类型
*/
#define	_IOR(g,n,t)	_IOC(IOC_OUT,	(g), (n), sizeof(t))

/*
    创建一个写操作的ioctl命令，该操作可以将用户空间向设备传送数据。例如：设置设备参数
*/
#define	_IOW(g,n,t)	_IOC(IOC_IN,	(g), (n), sizeof(t))
/* this should be _IORW, but stdio got there first */
#define	_IOWR(g,n,t)	_IOC(IOC_INOUT,	(g), (n), sizeof(t))
/* Replace length/type in an ioctl command. */
#define	_IOC_NEWLEN(ioc, len) \
    (((~(IOCPARM_MASK << 16)) & (ioc)) | (((len) & IOCPARM_MASK) << 16))
#define	_IOC_NEWTYPE(ioc, type)	_IOC_NEWLEN((ioc), sizeof(type))

#ifdef _KERNEL

#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
#define	IOCPARM_IVAL(x)	((int)(intptr_t)(void *)*(caddr_t *)(void *)(x))
#endif

#else

#include <sys/cdefs.h>

__BEGIN_DECLS
int	ioctl(int, unsigned long, ...);
__END_DECLS

#endif

#endif /* !_SYS_IOCCOM_H_ */
