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

/*
	在 ext2fs_htree_node 结构中包含有 ext2fs_fake_direct，其中的字段会进行一些特殊处理，参考:
		https://www.kernel.org/doc/html/latest/filesystems/ext4/directory.html
*/
struct ext2fs_fake_direct {
	/*
		Zero, to make it look like this entry is not in use.
	*/
	uint32_t e2d_ino;		/* inode number of entry 每一个条目的 inode number */
	/*
		The size of the block, in order to hide all of the htree_node data.
	*/
	uint16_t e2d_reclen;		/* length of this record 该结构体的长度 */
	/*
		Zero. There is no name for this “unused” directory entry.
	*/
	uint8_t	e2d_namlen;		/* length of string in d_name 目录项名字长度 */
	uint8_t	e2d_type;		/* file type 文件类型 */
};

struct ext2fs_htree_count {
	/* Maximum number of entries that can follow this header, plus 1 for the header itself. */
	uint16_t h_entries_max;
	/* Actual number of entries that follow this header, plus 1 for the header itself. */
	uint16_t h_entries_num;
};

/*
	ext2_htree_get_limit() 函数代码逻辑可以看到，它会将传入的 ext2fs_htree_entry 结构进行强制类型转换成
	ext2fs_htree_count 结构。说明 entry hash 其实可以拆分成 count 中的两个字段
*/
struct ext2fs_htree_entry {
	uint32_t h_hash;	// entry 的 hash 值
	/*
		The block number (within the directory file) that goes with hash=0
		哈希值为0的块编号（在目录文件中），从代码中的应用情况来看，有点像是逻辑块号
	*/
	uint32_t h_blk;
};

/*
 * This goes at the end of each htree block.
		如果文件系统支持 checksum 属性的话，这个还是有用的
 */
struct ext2fs_htree_tail {
	uint32_t ht_reserved;	/* 保留项 */
	uint32_t ht_checksum;	/* crc32c(uuid+inum+dirblock) */
};

/* htree 的根节点的一些信息，用于检查 htree 的深度和完整性的相关信息 */
struct ext2fs_htree_root_info {
	uint32_t h_reserved1;	/* 32位数，直接置0 */
	uint8_t	h_hash_version;	/* hash bype， 这个貌似是用来指定 hash 计算方式的 */
	uint8_t	h_info_len;	/* Length of the tree information 0x8 */
	/*
		这个就有点类似于inode查找数据块时的三级间接索引，表示树的深度。如果设置了 EXT2F_INCOMPAT_LARGEDIR，
		则深度是不能大于3的，否则不能大于2
	*/
	uint8_t	h_ind_levels;	/* indirect levels */
	uint8_t	h_reserved2;
};

/* 
	- htree 的根节点；从文档介绍来看，它应该也是 HTree 的第一级索引块
	- 每个目录项貌似都带有一个 ext2fs_fake_direct
	- 需要确认 htree 中的数据是否需要磁盘块来进行存储，因为在文档中提到了 4k 数据块容纳的 8 字节索引的数量

	从结构体定义可以观察到，ext2fs_htree_root 前两个成员刚好就是一个 ext2 目录项结构体(连4字节对齐都考虑
	到了)。对照 ext2_htree_root_limit 函数的代码逻辑可以推测，ext2fs_htree_root 其实就是代表的第一个
	目录项数据块中的内容。起始位置是两个目录项，后面会跟一个 ext2fs_htree_root_info 结构，再后面就是各种
	entry

	参考:
		https://www.kernel.org/doc/html/latest/filesystems/ext4/directory.html#dirhash
	root  中的 .. 目录项的长度被设置为了 block_size - 12，也就是说把 . 目录项的长度给删减掉了。tptfs 中这个name
	的长度要进行修改。因为都是固定长度256，所以name要设置为128，可能还要添加一个 reserved 的字段来模拟完整的目录
	结构 
*/
struct ext2fs_htree_root {
	struct ext2fs_fake_direct h_dot;
	char	h_dot_name[4];
	struct ext2fs_fake_direct h_dotdot;
	char	h_dotdot_name[4];
	struct ext2fs_htree_root_info h_info;
	/*
		As many 8-byte struct entry as fits in the rest of the data block.
		尽可能多的8字节结构条目适合数据块的其余部分
	*/
	struct ext2fs_htree_entry h_entries[0];
};

/*
	Interior nodes of an htree are recorded as struct htree_node, which is also
	the full length of a data block.
	htree 的内部节点记录为struct htree_node，也是数据块的全长
*/
struct ext2fs_htree_node {
	struct ext2fs_fake_direct h_fake_dirent;
	struct ext2fs_htree_entry h_entries[0];
};

/* htree 查找等级 */
struct ext2fs_htree_lookup_level {
	struct buf *h_bp;	/* 存放的应该是查找必须的一些基本数据 */
	/*
		h_entries 指向的应该是 entry 数组的起始位置，h_entry 指向的应该是我们所要操作的
		那个 entry
	*/
	struct ext2fs_htree_entry *h_entries;
	struct ext2fs_htree_entry *h_entry;
};

/* htree 查找信息 */
struct ext2fs_htree_lookup_info {
	struct ext2fs_htree_lookup_level h_levels[2];
	uint32_t h_levels_num;
};

/* 排序 */
struct ext2fs_htree_sort_entry {
	uint16_t h_offset;
	uint16_t h_size;
	uint32_t h_hash;
};

#endif	/* !_FS_EXT2FS_HTREE_H_ */
