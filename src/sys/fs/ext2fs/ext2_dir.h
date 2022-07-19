/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Aditya Sarawgi
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
 * $FreeBSD: releng/12.0/sys/fs/ext2fs/ext2_dir.h 337454 2018-08-08 12:08:46Z fsu $
 */

#ifndef _FS_EXT2FS_EXT2_DIR_H_
#define	_FS_EXT2FS_EXT2_DIR_H_

/*
 * Structure of a directory entry
 */
#define	EXT2FS_MAXNAMLEN	255

struct ext2fs_direct {
	uint32_t e2d_ino;		/* inode number of entry 索引节点号 */
	uint16_t e2d_reclen;		/* length of this record 目录项长度 */
	uint16_t e2d_namlen;		/* length of string in e2d_name 文件名长度 */
	char e2d_name[EXT2FS_MAXNAMLEN];/* name with length<=EXT2FS_MAXNAMLEN 文件名 */
};

enum slotstatus {
	NONE,
	COMPACT,
	FOUND
};

/*
	从代码实现来看，该结构体只在 lookup 和 htree 模块中出现，说明只是用于可用 slot 的查找
*/
struct ext2fs_searchslot {
	enum slotstatus slotstatus;
	doff_t	slotoffset;		/* offset of area with free space 拥有空闲空间的区域的偏移 */
	int	slotsize;		/* size of area at slotoffset slotoffset处的区域的大小 */
	int	slotfreespace;		/* amount of space free in slot slot中可用的空闲区域的数量 */
	int	slotneeded;		/* sizeof the entry we are seeking 我们需要找的 entry 的大小 */
};

/*
 * The new version of the directory entry.  Since EXT2 structures are
 * stored in intel byte order, and the name_len field could never be
 * bigger than 255 chars, it's safe to reclaim the extra byte for the
 * file_type field.
 * 目录项的新版本。由于EXT2结构是以intel字节顺序存储的，并且 name_len 字段不能
 * 大于255个字符，因此可以安全地回收 file_type 字段的额外字节
 */
struct ext2fs_direct_2 {
	uint32_t e2d_ino;		/* inode number of entry 目录项对应的 inode number */
	uint16_t e2d_reclen;		/* length of this record 可以理解为指向像一个有效目录项的指针，本质是偏移量 */
	uint8_t	e2d_namlen;		/* length of string in e2d_name 目录名字的长度 */
	uint8_t	e2d_type;		/* file type 文件类型 */
	char	e2d_name[EXT2FS_MAXNAMLEN];	/* name with
						 * length<=EXT2FS_MAXNAMLEN 文件名 */
};

/* 该结构体的大小一共是12个字节 */
struct ext2fs_direct_tail {
	uint32_t e2dt_reserved_zero1;	/* pretend to be unused */
	uint16_t e2dt_rec_len;		/* 12 */
	uint8_t	e2dt_reserved_zero2;	/* zero name length */
	uint8_t	e2dt_reserved_ft;	/* 0xDE, fake file type */
	uint32_t e2dt_checksum;		/* crc32c(uuid+inum+dirblock) */
};

#define EXT2_FT_DIR_CSUM	0xDE

/*
	data 指向的是它所读取的磁盘块数据在内存中的起始位置，加上 blocksize 后指向数据块
	末尾位置，减去 ext2fs_direct_tail 得到 ext2fs_direct_tail 结构体的起始位置(如果有的话)
*/
#define EXT2_DIRENT_TAIL(data, blocksize) \
	((struct ext2fs_direct_tail *)(((char *)(data)) + \
	(blocksize) - sizeof(struct ext2fs_direct_tail)))

/*
 * Maximal count of links to a file 指向文件的最大链接数
 */
#define	EXT4_LINK_MAX	65000

/*
 * Ext2 directory file types.  Only the low 3 bits are used.  The
 * other bits are reserved for now.
 * Ext2 所有的文件类型，仅仅低位的3个bit被使用，其他位现在被保留
 */
#define	EXT2_FT_UNKNOWN		0
#define	EXT2_FT_REG_FILE	1
#define	EXT2_FT_DIR		2
#define	EXT2_FT_CHRDEV		3
#define	EXT2_FT_BLKDEV 		4
#define	EXT2_FT_FIFO		5
#define	EXT2_FT_SOCK		6
#define	EXT2_FT_SYMLINK		7
#define	EXT2_FT_MAX		8

/*
 * EXT2_DIR_PAD defines the directory entries boundaries - EXT2_DIR_PAD 定义目录项边界
 *
 * NOTE: It must be a multiple of 4	它必须是4的倍数
 */
#define	EXT2_DIR_PAD		 	4
#define	EXT2_DIR_ROUND			(EXT2_DIR_PAD - 1)
#define	EXT2_DIR_REC_LEN(name_len)	(((name_len) + 8 + EXT2_DIR_ROUND) & \
					 ~EXT2_DIR_ROUND)
#endif	/* !_FS_EXT2FS_EXT2_DIR_H_ */
