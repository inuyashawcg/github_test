/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010, 2012 Zheng Liu <lz@freebsd.org>
 * Copyright (c) 2012, Vyacheslav Matyushin
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
 * $FreeBSD: releng/12.0/sys/fs/ext2fs/htree.h 327977 2018-01-14 20:46:39Z fsu $
 */

#ifndef _FS_EXT2FS_HTREE_H_
#define	_FS_EXT2FS_HTREE_H_

/* EXT3 HTree directory indexing */

#define	EXT2_HTREE_LEGACY		0
#define	EXT2_HTREE_HALF_MD4		1
#define	EXT2_HTREE_TEA			2
#define	EXT2_HTREE_LEGACY_UNSIGNED	3
#define	EXT2_HTREE_HALF_MD4_UNSIGNED	4
#define	EXT2_HTREE_TEA_UNSIGNED		5

#define	EXT2_HTREE_EOF 0x7FFFFFFF

struct ext2fs_fake_direct {
	uint32_t e2d_ino;		/* inode number of entry 每一个条目的 inode number */
	uint16_t e2d_reclen;		/* length of this record 该结构体的长度 */
	uint8_t	e2d_namlen;		/* length of string in d_name 目录项名字长度 */
	uint8_t	e2d_type;		/* file type 文件类型 */
};

struct ext2fs_htree_count {
	uint16_t h_entries_max;		// htree 最大拥有的 entry 的数量
	uint16_t h_entries_num;		// htree 当前所拥有的 entry 数量？
};

struct ext2fs_htree_entry {
	uint32_t h_hash;	// entry 的 hash 值
	uint32_t h_blk;		// entry 的 block number？
};

/*
 * This goes at the end of each htree block.
 */
struct ext2fs_htree_tail {
	uint32_t ht_reserved;	/* 保留项 */
	uint32_t ht_checksum;	/* crc32c(uuid+inum+dirblock) */
};

/* htree 的根节点的一些信息，用于检查 htree 的深度和完整性的相关信息 */
struct ext2fs_htree_root_info {
	uint32_t h_reserved1;
	uint8_t	h_hash_version;	/* 这个貌似是用来指定 hash 计算方式的 */
	uint8_t	h_info_len;
	uint8_t	h_ind_levels;
	uint8_t	h_reserved2;
};

/* 
	- htree 的根节点；
	- 每个目录项貌似都带有一个 ext2fs_fake_direct
	- 需要确认 htree 中的数据是否需要磁盘块来进行存储，因为在文档中提到了 4k 数据块容纳的 8 字节索引的数量
*/
struct ext2fs_htree_root {
	struct ext2fs_fake_direct h_dot;
	char	h_dot_name[4];
	struct ext2fs_fake_direct h_dotdot;
	char	h_dotdot_name[4];
	struct ext2fs_htree_root_info h_info;
	struct ext2fs_htree_entry h_entries[0];
};

/* htree 节点 */
struct ext2fs_htree_node {
	struct ext2fs_fake_direct h_fake_dirent;
	struct ext2fs_htree_entry h_entries[0];
};

/* htree 查找等级 */
struct ext2fs_htree_lookup_level {
	struct buf *h_bp;	/* 存放的应该是查找必须的一些基本数据 */
	struct ext2fs_htree_entry *h_entries;
	struct ext2fs_htree_entry *h_entry;
};

/* htree 查找信息 */
struct ext2fs_htree_lookup_info {
	struct ext2fs_htree_lookup_level h_levels[2];
	uint32_t h_levels_num;
};

/* 排序？ */
struct ext2fs_htree_sort_entry {
	uint16_t h_offset;
	uint16_t h_size;
	uint32_t h_hash;
};

#endif	/* !_FS_EXT2FS_HTREE_H_ */
