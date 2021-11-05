/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017, Fedor Uporov
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
 * $FreeBSD: releng/12.0/sys/fs/ext2fs/ext2_extattr.c 327977 2018-01-14 20:46:39Z fsu $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/vnode.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/endian.h>
#include <sys/conf.h>
#include <sys/extattr.h>

#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/ext2fs.h>
#include <fs/ext2fs/inode.h>
#include <fs/ext2fs/ext2_dinode.h>
#include <fs/ext2fs/ext2_mount.h>
#include <fs/ext2fs/ext2_extattr.h>
#include <fs/ext2fs/ext2_extern.h>

/* ext2 扩展属性命名空间转换成 bsd 格式 */
static int
ext2_extattr_attrnamespace_to_bsd(int attrnamespace)
{

	switch (attrnamespace) {
	case EXT4_XATTR_INDEX_SYSTEM:
		return (EXTATTR_NAMESPACE_SYSTEM);

	case EXT4_XATTR_INDEX_USER:
		return (EXTATTR_NAMESPACE_USER);

	case EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT:
		return (POSIX1E_ACL_DEFAULT_EXTATTR_NAMESPACE);

	case EXT4_XATTR_INDEX_POSIX_ACL_ACCESS:
		return (POSIX1E_ACL_ACCESS_EXTATTR_NAMESPACE);
	}

	return (EXTATTR_NAMESPACE_EMPTY);
}

/* ext2 扩展属性命名转换成 bsd 格式 */
static const char *
ext2_extattr_name_to_bsd(int attrnamespace, const char *name, int* name_len)
{

	if (attrnamespace == EXT4_XATTR_INDEX_SYSTEM)
		return (name);
	else if (attrnamespace == EXT4_XATTR_INDEX_USER)
		return (name);
	else if (attrnamespace == EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT) {
		*name_len = strlen(POSIX1E_ACL_DEFAULT_EXTATTR_NAME);
		return (POSIX1E_ACL_DEFAULT_EXTATTR_NAME);
	} else if (attrnamespace == EXT4_XATTR_INDEX_POSIX_ACL_ACCESS) {
		*name_len = strlen(POSIX1E_ACL_ACCESS_EXTATTR_NAME);
		return (POSIX1E_ACL_ACCESS_EXTATTR_NAME);
	}

	/*
	 * XXX: Not all linux namespaces are mapped to bsd for now,
	 * return NULL, which will be converted to ENOTSUP on upper layer.
	 */
#ifdef EXT2FS_DEBUG
	printf("can not convert ext2fs name to bsd: namespace=%d\n", attrnamespace);
#endif

	return (NULL);
}

/* ext2 扩展属性命名空间转换成 linux 格式 */
static int
ext2_extattr_attrnamespace_to_linux(int attrnamespace, const char *name)
{

	if (attrnamespace == POSIX1E_ACL_DEFAULT_EXTATTR_NAMESPACE &&
	    !strcmp(name, POSIX1E_ACL_DEFAULT_EXTATTR_NAME))
		return (EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT);

	if (attrnamespace == POSIX1E_ACL_ACCESS_EXTATTR_NAMESPACE &&
	    !strcmp(name, POSIX1E_ACL_ACCESS_EXTATTR_NAME))
		return (EXT4_XATTR_INDEX_POSIX_ACL_ACCESS);

	switch (attrnamespace) {
	case EXTATTR_NAMESPACE_SYSTEM:
		return (EXT4_XATTR_INDEX_SYSTEM);

	case EXTATTR_NAMESPACE_USER:
		return (EXT4_XATTR_INDEX_USER);
	}

	/*
	 * In this case namespace conversion should be unique,
	 * so this point is unreachable.
	 */
	return (-1);
}

/* ext2 扩展属性命名转换成 linux 格式 */
static const char *
ext2_extattr_name_to_linux(int attrnamespace, const char *name)
{

	if (attrnamespace == POSIX1E_ACL_DEFAULT_EXTATTR_NAMESPACE ||
	    attrnamespace == POSIX1E_ACL_ACCESS_EXTATTR_NAMESPACE)
		return ("");
	else
		return (name);
}

/* 判断扩展属性名称是否有效 */
int
ext2_extattr_valid_attrname(int attrnamespace, const char *attrname)
{
	if (attrnamespace == EXTATTR_NAMESPACE_EMPTY)
		return (EINVAL);

	if (strlen(attrname) == 0)
		return (EINVAL);

	if (strlen(attrname) + 1 > EXT2_EXTATTR_NAMELEN_MAX)
		return (ENAMETOOLONG);

	return (0);
}

/* 
	遍历文件的所有扩展属性。从实际调用情况来看，end 指向的是 buf 的末尾位置。
	这个函数应该就是判断扩展属性是不是超过了一个块的空间。ext 应该是规定最多
	占用一个数据块
*/
static int
ext2_extattr_check(struct ext2fs_extattr_entry *entry, char *end)
{
	struct ext2fs_extattr_entry *next;

	while (!EXT2_IS_LAST_ENTRY(entry)) {
		next = EXT2_EXTATTR_NEXT(entry);
		if ((char *)next >= end)
			return (EIO);

		entry = next;
	}

	return (0);
}

/* 
	获取文件扩展属性对应的磁盘块。这里需要注意 bp 是从上级函数传递过来的，对应到 tptfs 中要注意
	buf 的释放过程是否被遗漏
*/
static int
ext2_extattr_block_check(struct inode *ip, struct buf *bp)
{
	struct ext2fs_extattr_header *header;
	int error;

	header = (struct ext2fs_extattr_header *)bp->b_data;

	error = ext2_extattr_check(EXT2_IFIRST(header),
	    bp->b_data + bp->b_bufsize);
	if (error)
		return (error);

	/* tptfs 中不再包含 checksum 计算，该步骤可以省略 */
	return (ext2_extattr_blk_csum_verify(ip, bp));
}

int
ext2_extattr_inode_list(struct inode *ip, int attrnamespace,
    struct uio *uio, size_t *size)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct ext2fs_extattr_dinode_header *header;
	struct ext2fs_extattr_entry *entry;
	const char *attr_name;
	int name_len;
	int error;

	fs = ip->i_e2fs;

	/* 读取 inode 所在的磁盘块数据 */
	if ((error = bread(ip->i_devvp,
	    fsbtodb(fs, ino_to_fsba(fs, ip->i_number)),
	    (int)fs->e2fs_bsize, NOCRED, &bp)) != 0) {
		brelse(bp);
		return (error);
	}

	/* 获取指定 inode 结构体数据 */
	struct ext2fs_dinode *dinode = (struct ext2fs_dinode *)
	    ((char *)bp->b_data +
	    EXT2_INODE_SIZE(fs) * ino_to_fsbo(fs, ip->i_number));

	/* Check attributes magic value */
	header = (struct ext2fs_extattr_dinode_header *)((char *)dinode +
	    E2FS_REV0_INODE_SIZE + dinode->e2di_extra_isize);

	if (header->h_magic != EXTATTR_MAGIC) {
		brelse(bp);
		return (0);
	}

	error = ext2_extattr_check(EXT2_IFIRST(header),
	    (char *)dinode + EXT2_INODE_SIZE(fs));
	if (error) {
		brelse(bp);
		return (error);
	}

	for (entry = EXT2_IFIRST(header); !EXT2_IS_LAST_ENTRY(entry);
	    entry = EXT2_EXTATTR_NEXT(entry)) {
		if (ext2_extattr_attrnamespace_to_bsd(entry->e_name_index) !=
		    attrnamespace)
			continue;

		name_len = entry->e_name_len;
		attr_name = ext2_extattr_name_to_bsd(entry->e_name_index,
		    entry->e_name, &name_len);
		if (!attr_name) {
			brelse(bp);
			return (ENOTSUP);
		}

		if (size != NULL)
			*size += name_len + 1;

		if (uio != NULL) {
			char *name = malloc(name_len + 1, M_TEMP, M_WAITOK);
			name[0] = name_len;
			memcpy(&name[1], attr_name, name_len);
			error = uiomove(name, name_len + 1, uio);
			free(name, M_TEMP);
			if (error)
				break;
		}
	}

	brelse(bp);

	return (error);
}

/*
	函数参数中只有一个 attrnamespace 参数，并没有给具体的属性名称。然后函数中包含了一个 for 循环。
	所以，该函数的作用应该就是将某一 namespace 下的所有属性都找出来
*/
int
ext2_extattr_block_list(struct inode *ip, int attrnamespace,
    struct uio *uio, size_t *size)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct ext2fs_extattr_header *header;
	struct ext2fs_extattr_entry *entry;
	const char *attr_name;
	int name_len;
	int error;

	fs = ip->i_e2fs;

	error = bread(ip->i_devvp, fsbtodb(fs, ip->i_facl),
	    fs->e2fs_bsize, NOCRED, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	/* 
		Check attributes magic value 
		magic 貌似是可以自定义的，不用非要按照 ext2 的指定的值。h_blocks 表示的应该是扩展属性
		所占用的磁盘块的数量，对应到 tptfs 中表示的就是占用的内存页的数量，从目前的设计来看，该成员
		的值始终是1
	*/
	header = EXT2_HDR(bp);
	if (header->h_magic != EXTATTR_MAGIC || header->h_blocks != 1) {
		brelse(bp);
		return (EINVAL);
	}

	/* 判断文件的扩展属性占用的空间是否超过了一个数据页的大小 */
	error = ext2_extattr_block_check(ip, bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	for (entry = EXT2_FIRST_ENTRY(bp); !EXT2_IS_LAST_ENTRY(entry);
	    entry = EXT2_EXTATTR_NEXT(entry)) {
		/* 首先判断 namespace 是否匹配 */
		if (ext2_extattr_attrnamespace_to_bsd(entry->e_name_index) !=
		    attrnamespace)
			continue;

		name_len = entry->e_name_len;
		/*
			通过 name_index 判断属性的类型，因为有的属性是默认的属性，它的名称是固定的，返回的 name 字符串也是一个
			全局字符串，而不是 entry->e_name 字段中存放的字符串
		*/
		attr_name = ext2_extattr_name_to_bsd(entry->e_name_index,
		    entry->e_name, &name_len);
		if (!attr_name) {
			brelse(bp);
			return (ENOTSUP);
		}

		/*
			下面的代码逻辑涉及到了 uiomove，所以这个函数很可能是用户通过 lsattr 等命令获取文件扩展属性时会调用
			到的底层内核函数，通过 uio 将数据发送到用户空间。size 其实是上层函数传递下来的一个指针，应该是用来
			表示获取到的文件扩展属性的大小是多少
		*/
		if (size != NULL)
			*size += name_len + 1;

		if (uio != NULL) {
			char *name = malloc(name_len + 1, M_TEMP, M_WAITOK);
			name[0] = name_len;
			memcpy(&name[1], attr_name, name_len);
			error = uiomove(name, name_len + 1, uio);
			free(name, M_TEMP);
			if (error)
				break;
		}
	}	// end for

	brelse(bp);

	return (error);
}

int
ext2_extattr_inode_get(struct inode *ip, int attrnamespace,
    const char *name, struct uio *uio, size_t *size)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct ext2fs_extattr_dinode_header *header;
	struct ext2fs_extattr_entry *entry;
	const char *attr_name;
	int name_len;
	int error;

	fs = ip->i_e2fs;

	if ((error = bread(ip->i_devvp,
	    fsbtodb(fs, ino_to_fsba(fs, ip->i_number)),
	    (int)fs->e2fs_bsize, NOCRED, &bp)) != 0) {
		brelse(bp);
		return (error);
	}

	struct ext2fs_dinode *dinode = (struct ext2fs_dinode *)
	    ((char *)bp->b_data +
	    EXT2_INODE_SIZE(fs) * ino_to_fsbo(fs, ip->i_number));

	/* Check attributes magic value */
	header = (struct ext2fs_extattr_dinode_header *)((char *)dinode +
	    E2FS_REV0_INODE_SIZE + dinode->e2di_extra_isize);

	if (header->h_magic != EXTATTR_MAGIC) {
		brelse(bp);
		return (ENOATTR);
	}

	error = ext2_extattr_check(EXT2_IFIRST(header),
	    (char *)dinode + EXT2_INODE_SIZE(fs));
	if (error) {
		brelse(bp);
		return (error);
	}

	for (entry = EXT2_IFIRST(header); !EXT2_IS_LAST_ENTRY(entry);
	    entry = EXT2_EXTATTR_NEXT(entry)) {
		if (ext2_extattr_attrnamespace_to_bsd(entry->e_name_index) !=
		    attrnamespace)
			continue;

		name_len = entry->e_name_len;
		attr_name = ext2_extattr_name_to_bsd(entry->e_name_index,
		    entry->e_name, &name_len);
		if (!attr_name) {
			brelse(bp);
			return (ENOTSUP);
		}

		if (strlen(name) == name_len &&
		    0 == strncmp(attr_name, name, name_len)) {
			if (size != NULL)
				*size += entry->e_value_size;

			if (uio != NULL)
				error = uiomove(((char *)EXT2_IFIRST(header)) +
				    entry->e_value_offs, entry->e_value_size, uio);

			brelse(bp);
			return (error);
		}
	 }

	brelse(bp);

	return (ENOATTR);
}

/*
	这个函数跟上面 block_list 函数的区别就在于多了一个 name，猜测是通过 namespace 和 name
	定位某一个扩展属性 entry
*/
int
ext2_extattr_block_get(struct inode *ip, int attrnamespace,
    const char *name, struct uio *uio, size_t *size)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct ext2fs_extattr_header *header;
	struct ext2fs_extattr_entry *entry;
	const char *attr_name;
	int name_len;
	int error;

	fs = ip->i_e2fs;

	error = bread(ip->i_devvp, fsbtodb(fs, ip->i_facl),
	    fs->e2fs_bsize, NOCRED, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	/* Check attributes magic value */
	header = EXT2_HDR(bp);
	if (header->h_magic != EXTATTR_MAGIC || header->h_blocks != 1) {
		brelse(bp);
		return (EINVAL);
	}

	error = ext2_extattr_block_check(ip, bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	for (entry = EXT2_FIRST_ENTRY(bp); !EXT2_IS_LAST_ENTRY(entry);
	    entry = EXT2_EXTATTR_NEXT(entry)) {
		if (ext2_extattr_attrnamespace_to_bsd(entry->e_name_index) !=
		    attrnamespace)
			continue;

		name_len = entry->e_name_len;
		attr_name = ext2_extattr_name_to_bsd(entry->e_name_index,
		    entry->e_name, &name_len);
		if (!attr_name) {
			brelse(bp);
			return (ENOTSUP);
		}

		/* 跟 block_list 函数最重要的区别就在这个地方，多了一个 name 比较 */
		if (strlen(name) == name_len &&
		    0 == strncmp(attr_name, name, name_len)) {
			if (size != NULL)
				*size += entry->e_value_size;

			if (uio != NULL)
				error = uiomove(bp->b_data + entry->e_value_offs,
				    entry->e_value_size, uio);

			brelse(bp);
			return (error);
		}
	 }

	brelse(bp);

	/* 如果没有找到对应属性的话，就返回 ENOATTR */
	return (ENOATTR);
}

/*
	扩展属性其实就类似于map，一个属性名对应一个属性值。从上级函数调用的情况来看，off 表示的是 buffer 的起始位置，
	end 表示的是 buffer 的末尾位置。entry 表示的是需要被处理的属性 entry
*/
static uint16_t
ext2_extattr_delete_value(char *off,
    struct ext2fs_extattr_entry *first_entry,
    struct ext2fs_extattr_entry *entry, char *end)
{
	uint16_t min_offs;
	struct ext2fs_extattr_entry *next;

	/* min_offs 表示的是整个数据页的大小 */
	min_offs = end - off;
	next = first_entry;

	/*
		while 循环结束后，min_offs 表示的就是 offset 最小的 attr value 所在的位置
	*/
	while (!EXT2_IS_LAST_ENTRY(next)) {
		if (min_offs > next->e_value_offs && next->e_value_offs > 0)
			min_offs = next->e_value_offs;

		next = EXT2_EXTATTR_NEXT(next);
	}

	/*
		如果该属性对应的 value 是 0，则直接返回最小 value offset
	*/
	if (entry->e_value_size == 0)
		return (min_offs);

	/*
		从这里可以看出，EXT2_EXTATTR_SIZE 宏是对 attr value 进行字节对齐处理。应该就是之前分析的那样，
		四字节对齐。下面的操作其实就是数据的整体偏移。假设最小 value 偏移是 5，我们所要找的 entry 对应的
		value 偏移是 7，长度是 1，那将该属性删除之后的结果就是最小偏移变成了 6，原来 7-8 之间的数据被覆盖
		掉了。
		下面的操作就是把原来 5-7 的数据搬移到了 6-8 的位置。然后原来比 entry value offset 小的偏移量需要
		进行更新，因为数据搬移了。而比它大的哪些value的偏移则是不需要进行变动的。然后更新一下 min_offs
	*/
	memmove(off + min_offs + EXT2_EXTATTR_SIZE(entry->e_value_size),
	    off + min_offs, entry->e_value_offs - min_offs);

	/* Adjust all value offsets */
	next = first_entry;
	while (!EXT2_IS_LAST_ENTRY(next))
	{
		if (next->e_value_offs > 0 &&
		    next->e_value_offs < entry->e_value_offs)
			next->e_value_offs +=
			    EXT2_EXTATTR_SIZE(entry->e_value_size);

		next = EXT2_EXTATTR_NEXT(next);
	}

	min_offs += EXT2_EXTATTR_SIZE(entry->e_value_size);

	return (min_offs);
}

/*
	off：buffer 的起始位置
	first_entry：表示的就是读取到的属性块中的第一个 entry
	entry：表示的是我们找到的那个需要被删除掉的 entry
	end：表示 buffer 末尾的位置
*/
static void
ext2_extattr_delete_entry(char *off,
    struct ext2fs_extattr_entry *first_entry,
    struct ext2fs_extattr_entry *entry, char *end)
{
	char *pad;
	struct ext2fs_extattr_entry *next;

	/* Clean entry value */
	ext2_extattr_delete_value(off, first_entry, entry, end);

	/* Clean the entry 
		上面把 entry 对应的 value 删除了，然后开始删除 entry 本身。经过 while 循环的处理之后，entry 应该是
		已经到了最后的位置。
	*/
	next = first_entry;
	while (!EXT2_IS_LAST_ENTRY(next))
		next = EXT2_EXTATTR_NEXT(next);

	/* 
		pad 的作用感觉还是对数据进行对齐，所用的宏跟 value 操作是一样的，所以应该还是4字节对齐。但是在 tptfs 中
		所有的 entry 都已经被设计成了256个字节，所以这个步骤可以直接省去
	*/
	pad = (char*)next + sizeof(uint32_t);

	memmove(entry, (char *)entry + EXT2_EXTATTR_LEN(entry->e_name_len),
	    pad - ((char *)entry + EXT2_EXTATTR_LEN(entry->e_name_len)));
}

int
ext2_extattr_inode_delete(struct inode *ip, int attrnamespace, const char *name)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct ext2fs_extattr_dinode_header *header;
	struct ext2fs_extattr_entry *entry;
	const char *attr_name;
	int name_len;
	int error;

	fs = ip->i_e2fs;

	if ((error = bread(ip->i_devvp,
	    fsbtodb(fs, ino_to_fsba(fs, ip->i_number)),
	    (int)fs->e2fs_bsize, NOCRED, &bp)) != 0) {
		brelse(bp);
		return (error);
	}

	struct ext2fs_dinode *dinode = (struct ext2fs_dinode *)
	    ((char *)bp->b_data +
	    EXT2_INODE_SIZE(fs) * ino_to_fsbo(fs, ip->i_number));

	/* Check attributes magic value */
	header = (struct ext2fs_extattr_dinode_header *)((char *)dinode +
	    E2FS_REV0_INODE_SIZE + dinode->e2di_extra_isize);

	if (header->h_magic != EXTATTR_MAGIC) {
		brelse(bp);
		return (ENOATTR);
	}

	error = ext2_extattr_check(EXT2_IFIRST(header),
	    (char *)dinode + EXT2_INODE_SIZE(fs));
	if (error) {
		brelse(bp);
		return (error);
	}

	/* If I am last entry, just make magic zero */
	entry = EXT2_IFIRST(header);
	if ((EXT2_IS_LAST_ENTRY(EXT2_EXTATTR_NEXT(entry))) &&
	    (ext2_extattr_attrnamespace_to_bsd(entry->e_name_index) ==
	    attrnamespace)) {

		name_len = entry->e_name_len;
		attr_name = ext2_extattr_name_to_bsd(entry->e_name_index,
		    entry->e_name, &name_len);
		if (!attr_name) {
			brelse(bp);
			return (ENOTSUP);
		}

		if (strlen(name) == name_len &&
		    0 == strncmp(attr_name, name, name_len)) {
			memset(header, 0, sizeof(struct ext2fs_extattr_dinode_header));

			return (bwrite(bp));
		}
	}

	for (entry = EXT2_IFIRST(header); !EXT2_IS_LAST_ENTRY(entry);
	    entry = EXT2_EXTATTR_NEXT(entry)) {
		if (ext2_extattr_attrnamespace_to_bsd(entry->e_name_index) !=
		    attrnamespace)
			continue;

		name_len = entry->e_name_len;
		attr_name = ext2_extattr_name_to_bsd(entry->e_name_index,
		    entry->e_name, &name_len);
		if (!attr_name) {
			brelse(bp);
			return (ENOTSUP);
		}

		if (strlen(name) == name_len &&
		    0 == strncmp(attr_name, name, name_len)) {
			ext2_extattr_delete_entry((char *)EXT2_IFIRST(header),
			    EXT2_IFIRST(header), entry,
			    (char *)dinode + EXT2_INODE_SIZE(fs));

			return (bwrite(bp));
		}
	}

	brelse(bp);

	return (ENOATTR);
}

/*
	当多个文件共享一个数据块属性、此时其中一个文件需要修改属性的时候，我们就需要 clone 出
	一个副本，否则所有关联文件的属性都会被改变
*/
static int
ext2_extattr_block_clone(struct inode *ip, struct buf **bpp)
{
	struct m_ext2fs *fs;
	struct buf *sbp;
	struct buf *cbp;
	struct ext2fs_extattr_header *header;
	uint64_t facl;

	fs = ip->i_e2fs;
	sbp = *bpp;

	header = EXT2_HDR(sbp);
	if (header->h_magic != EXTATTR_MAGIC || header->h_refcount == 1)
		return (EINVAL);

	/* 给当前文件重新分配一个扩展属性数据块 */
	facl = ext2_alloc_meta(ip);
	if (!facl)
		return (ENOSPC);

	cbp = getblk(ip->i_devvp, fsbtodb(fs, facl), fs->e2fs_bsize, 0, 0, 0);
	if (!cbp) {
		ext2_blkfree(ip, facl, fs->e2fs_bsize);
		return (EIO);
	}

	memcpy(cbp->b_data, sbp->b_data, fs->e2fs_bsize);
	header->h_refcount--;
	bwrite(sbp);

	ip->i_facl = facl;
	ext2_update(ip->i_vnode, 1);

	header = EXT2_HDR(cbp);
	header->h_refcount = 1;

	*bpp = cbp;

	return (0);
}

/*
	从 extattr 数据块中删除掉一个 entry，通过 name 和 namespace 定位
*/
int
ext2_extattr_block_delete(struct inode *ip, int attrnamespace, const char *name)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct ext2fs_extattr_header *header;
	struct ext2fs_extattr_entry *entry;
	const char *attr_name;
	int name_len;
	int error;

	fs = ip->i_e2fs;

	error = bread(ip->i_devvp, fsbtodb(fs, ip->i_facl),
	    fs->e2fs_bsize, NOCRED, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	/* Check attributes magic value */
	header = EXT2_HDR(bp);
	if (header->h_magic != EXTATTR_MAGIC || header->h_blocks != 1) {
		brelse(bp);
		return (EINVAL);
	}

	error = ext2_extattr_block_check(ip, bp);
	if (error) {
		brelse(bp);
		return (error);
	}
	/*
		貌似是只要操作扩展属性所在的数据块，就要执行上述这些代码逻辑。因为这个函数的功能是要删除掉数据块中的属性 entry，
		但是可能会出现的情况就是有多个文件共享这一个数据块属性。那么我们就要像创建影子对象那样克隆出一个对象给没有修改
		扩展属性的文件来使用。应该是要重新分配一个数据块
	*/
	if (header->h_refcount > 1) {
		error = ext2_extattr_block_clone(ip, &bp);
		/*
			此时 bp 已经是指向克隆出的新的扩展属性数据块，所以后续的修改都是基于新的数据块来实现的
		*/
		if (error) {
			brelse(bp);
			return (error);
		}
	}

	/* If I am last entry, clean me and free the block */
	entry = EXT2_FIRST_ENTRY(bp);
	if (EXT2_IS_LAST_ENTRY(EXT2_EXTATTR_NEXT(entry)) &&
	    (ext2_extattr_attrnamespace_to_bsd(entry->e_name_index) ==
	    attrnamespace)) {

		name_len = entry->e_name_len;
		attr_name = ext2_extattr_name_to_bsd(entry->e_name_index,
		    entry->e_name, &name_len);
		if (!attr_name) {
			brelse(bp);
			return (ENOTSUP);
		}

		/* strncmp 返回 0 表示两个字符串一致 */
		if (strlen(name) == name_len &&
		    0 == strncmp(attr_name, name, name_len)) {
			ip->i_blocks -= btodb(fs->e2fs_bsize);
			ext2_blkfree(ip, ip->i_facl, fs->e2fs_bsize);
			ip->i_facl = 0;
			error = ext2_update(ip->i_vnode, 1);

			brelse(bp);
			return (error);
		}
	}	// end if

	for (entry = EXT2_FIRST_ENTRY(bp); !EXT2_IS_LAST_ENTRY(entry);
	    entry = EXT2_EXTATTR_NEXT(entry)) {
		if (ext2_extattr_attrnamespace_to_bsd(entry->e_name_index) !=
		    attrnamespace)
			continue;

		name_len = entry->e_name_len;
		attr_name = ext2_extattr_name_to_bsd(entry->e_name_index,
		    entry->e_name, &name_len);
		if (!attr_name) {
			brelse(bp);
			return (ENOTSUP);
		}

		if (strlen(name) == name_len &&
		    0 == strncmp(attr_name, name, name_len)) {
			ext2_extattr_delete_entry(bp->b_data,
			    EXT2_FIRST_ENTRY(bp), entry,
			    bp->b_data + bp->b_bufsize);

			return (bwrite(bp));
		}
	}

	brelse(bp);

	return (ENOATTR);
}

static struct ext2fs_extattr_entry *
allocate_entry(const char *name, int attrnamespace, uint16_t offs,
    uint32_t size, uint32_t hash)
{
	const char *attr_name;
	int name_len;
	struct ext2fs_extattr_entry *entry;

	attr_name = ext2_extattr_name_to_linux(attrnamespace, name);
	name_len = strlen(attr_name);

	entry = malloc(sizeof(struct ext2fs_extattr_entry) + name_len,
	    M_TEMP, M_WAITOK);

	entry->e_name_len = name_len;
	entry->e_name_index = ext2_extattr_attrnamespace_to_linux(attrnamespace, name);
	entry->e_value_offs = offs;
	entry->e_value_block = 0;
	entry->e_value_size = size;
	entry->e_hash = hash;
	memcpy(entry->e_name, name, name_len);

	return (entry);
}

static void
free_entry(struct ext2fs_extattr_entry *entry)
{

	free(entry, M_TEMP);
}

/*
	从函数调用情况来看，new_size 传入的参数是 uio_resid，也就是说这个函数应该是将用户程序发送过来的设置
	属性的数据添加到数据块中对应的位置，然后计算这些数据总量是否已经超过整个数据块的大小
*/
static int
ext2_extattr_get_size(struct ext2fs_extattr_entry *first_entry,
    struct ext2fs_extattr_entry *exist_entry, int header_size,
    int name_len, int new_size)
{
	struct ext2fs_extattr_entry *entry;
	int size;

	size = header_size;
	size += sizeof(uint32_t);

	if (NULL == exist_entry) {
		size += EXT2_EXTATTR_LEN(name_len);
		size += EXT2_EXTATTR_SIZE(new_size);
	}

	/*
		for 循环把所有的扩展属性 entry 和 value 的长度全部加到 size 当中
	*/
	if (first_entry)
		for (entry = first_entry; !EXT2_IS_LAST_ENTRY(entry);
		    entry = EXT2_EXTATTR_NEXT(entry)) {
			if (entry != exist_entry)
				size += EXT2_EXTATTR_LEN(entry->e_name_len) +
				    EXT2_EXTATTR_SIZE(entry->e_value_size);
			else
				size += EXT2_EXTATTR_LEN(entry->e_name_len) +
				    EXT2_EXTATTR_SIZE(new_size);
		}

	return (size);
}

/*
	该函数应该就是对于已经存在的文件扩展属性值进行进一步设置
*/
static void
ext2_extattr_set_exist_entry(char *off,
    struct ext2fs_extattr_entry *first_entry,
    struct ext2fs_extattr_entry *entry,
    char *end, struct uio *uio)
{
	uint16_t min_offs;

	min_offs = ext2_extattr_delete_value(off, first_entry, entry, end);

	entry->e_value_size = uio->uio_resid;
	if (entry->e_value_size)
		entry->e_value_offs = min_offs -
		    EXT2_EXTATTR_SIZE(uio->uio_resid);
	else
		entry->e_value_offs = 0;

	uiomove(off + entry->e_value_offs, entry->e_value_size, uio);
}

/*
	设置一个新添加的属性 entry
*/
static struct ext2fs_extattr_entry *
ext2_extattr_set_new_entry(char *off, struct ext2fs_extattr_entry *first_entry,
    const char *name, int attrnamespace, char *end, struct uio *uio)
{
	int name_len;
	char *pad;
	uint16_t min_offs;
	struct ext2fs_extattr_entry *entry;
	struct ext2fs_extattr_entry *new_entry;

	/* Find pad's */
	min_offs = end - off;
	entry = first_entry;
	/* while 循环用于获取最小的 value offset */
	while (!EXT2_IS_LAST_ENTRY(entry)) {
		if (min_offs > entry->e_value_offs && entry->e_value_offs > 0)
			min_offs = entry->e_value_offs;

		entry = EXT2_EXTATTR_NEXT(entry);
	}

	pad = (char*)entry + sizeof(uint32_t);

	/* Find entry insert position 
		通过比较 name 来确定 entry 插入的位置。ext 文件系统的名称信息是通过 name 字符串比较来确定的，
		而 value 则是根据插入的时间前后进行排序的
	*/
	name_len = strlen(name);
	entry = first_entry;
	while (!EXT2_IS_LAST_ENTRY(entry)) {
		if (!(attrnamespace - entry->e_name_index) &&
		    !(name_len - entry->e_name_len))
			if (memcmp(name, entry->e_name, name_len) <= 0)
				break;

		entry = EXT2_EXTATTR_NEXT(entry);
	}

	/* Create new entry and insert it */
	new_entry = allocate_entry(name, attrnamespace, 0, uio->uio_resid, 0);
	memmove((char *)entry + EXT2_EXTATTR_LEN(new_entry->e_name_len), entry,
	    pad - (char*)entry);

	memcpy(entry, new_entry, EXT2_EXTATTR_LEN(new_entry->e_name_len));
	free_entry(new_entry);

	new_entry = entry;
	if (new_entry->e_value_size > 0)
		new_entry->e_value_offs = min_offs -
		    EXT2_EXTATTR_SIZE(new_entry->e_value_size);

	uiomove(off + new_entry->e_value_offs, new_entry->e_value_size, uio);

	return (new_entry);
}

int
ext2_extattr_inode_set(struct inode *ip, int attrnamespace,
    const char *name, struct uio *uio)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct ext2fs_extattr_dinode_header *header;
	struct ext2fs_extattr_entry *entry;
	const char *attr_name;
	int name_len;
	size_t size = 0, max_size;
	int error;

	fs = ip->i_e2fs;

	if ((error = bread(ip->i_devvp,
	    fsbtodb(fs, ino_to_fsba(fs, ip->i_number)),
	    (int)fs->e2fs_bsize, NOCRED, &bp)) != 0) {
		brelse(bp);
		return (error);
	}

	struct ext2fs_dinode *dinode = (struct ext2fs_dinode *)
	    ((char *)bp->b_data +
	    EXT2_INODE_SIZE(fs) * ino_to_fsbo(fs, ip->i_number));

	/* Check attributes magic value */
	header = (struct ext2fs_extattr_dinode_header *)((char *)dinode +
	    E2FS_REV0_INODE_SIZE + dinode->e2di_extra_isize);

	if (header->h_magic != EXTATTR_MAGIC) {
		brelse(bp);
		return (ENOSPC);
	}

	error = ext2_extattr_check(EXT2_IFIRST(header), (char *)dinode +
	    EXT2_INODE_SIZE(fs));
	if (error) {
		brelse(bp);
		return (error);
	}

	/* Find if entry exist */
	for (entry = EXT2_IFIRST(header); !EXT2_IS_LAST_ENTRY(entry);
	    entry = EXT2_EXTATTR_NEXT(entry)) {
		if (ext2_extattr_attrnamespace_to_bsd(entry->e_name_index) !=
		    attrnamespace)
			continue;

		name_len = entry->e_name_len;
		attr_name = ext2_extattr_name_to_bsd(entry->e_name_index,
		    entry->e_name, &name_len);
		if (!attr_name) {
			brelse(bp);
			return (ENOTSUP);
		}

		if (strlen(name) == name_len &&
		    0 == strncmp(attr_name, name, name_len))
			break;
	}

	max_size = EXT2_INODE_SIZE(fs) - E2FS_REV0_INODE_SIZE -
	    dinode->e2di_extra_isize;

	if (!EXT2_IS_LAST_ENTRY(entry)) {
		size = ext2_extattr_get_size(EXT2_IFIRST(header), entry,
		    sizeof(struct ext2fs_extattr_dinode_header),
		    entry->e_name_len, uio->uio_resid);
		if (size > max_size) {
			brelse(bp);
			return (ENOSPC);
		}

		ext2_extattr_set_exist_entry((char *)EXT2_IFIRST(header),
		    EXT2_IFIRST(header), entry, (char *)header + max_size, uio);
	} else {
		/* Ensure that the same entry does not exist in the block */
		if (ip->i_facl) {
			error = ext2_extattr_block_get(ip, attrnamespace, name,
			    NULL, &size);
			if (error != ENOATTR || size > 0) {
				brelse(bp);
				if (size > 0)
					error = ENOSPC;

				return (error);
			}
		}

		size = ext2_extattr_get_size(EXT2_IFIRST(header), NULL,
		    sizeof(struct ext2fs_extattr_dinode_header),
		    entry->e_name_len, uio->uio_resid);
		if (size > max_size) {
			brelse(bp);
			return (ENOSPC);
		}

		ext2_extattr_set_new_entry((char *)EXT2_IFIRST(header),
		    EXT2_IFIRST(header), name, attrnamespace,
		    (char *)header + max_size, uio);
	}

	return (bwrite(bp));
}

static void
ext2_extattr_hash_entry(struct ext2fs_extattr_header *header,
    struct ext2fs_extattr_entry *entry)
{
	uint32_t hash = 0;
	char *name = entry->e_name;
	int n;

	for (n=0; n < entry->e_name_len; n++) {
		hash = (hash << EXT2_EXTATTR_NAME_HASH_SHIFT) ^
		    (hash >> (8*sizeof(hash) - EXT2_EXTATTR_NAME_HASH_SHIFT)) ^
		    (*name++);
	}

	if (entry->e_value_block == 0 && entry->e_value_size != 0) {
		uint32_t *value = (uint32_t *)((char *)header + entry->e_value_offs);
		for (n = (entry->e_value_size +
		    EXT2_EXTATTR_ROUND) >> EXT2_EXTATTR_PAD_BITS; n; n--) {
			hash = (hash << EXT2_EXTATTR_VALUE_HASH_SHIFT) ^
			    (hash >> (8*sizeof(hash) - EXT2_EXTATTR_VALUE_HASH_SHIFT)) ^
			    (*value++);
		}
	}

	entry->e_hash = hash;
}

static void
ext2_extattr_rehash(struct ext2fs_extattr_header *header,
    struct ext2fs_extattr_entry *entry)
{
	struct ext2fs_extattr_entry *here;
	uint32_t hash = 0;

	ext2_extattr_hash_entry(header, entry);

	here = EXT2_ENTRY(header+1);
	while (!EXT2_IS_LAST_ENTRY(here)) {
		if (!here->e_hash) {
			/* Block is not shared if an entry's hash value == 0 */
			hash = 0;
			break;
		}

		hash = (hash << EXT2_EXTATTR_BLOCK_HASH_SHIFT) ^
		    (hash >> (8*sizeof(hash) - EXT2_EXTATTR_BLOCK_HASH_SHIFT)) ^
		    here->e_hash;

		here = EXT2_EXTATTR_NEXT(here);
	}

	header->h_hash = hash;
}

int
ext2_extattr_block_set(struct inode *ip, int attrnamespace,
    const char *name, struct uio *uio)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct ext2fs_extattr_header *header;
	struct ext2fs_extattr_entry *entry;
	const char *attr_name;
	int name_len;
	size_t size;
	int error;

	fs = ip->i_e2fs;

	if (ip->i_facl) {
		error = bread(ip->i_devvp, fsbtodb(fs, ip->i_facl),
		    fs->e2fs_bsize, NOCRED, &bp);
		if (error) {
			brelse(bp);
			return (error);
		}

		/* Check attributes magic value */
		header = EXT2_HDR(bp);
		if (header->h_magic != EXTATTR_MAGIC || header->h_blocks != 1) {
			brelse(bp);
			return (EINVAL);
		}

		error = ext2_extattr_block_check(ip, bp);
		if (error) {
			brelse(bp);
			return (error);
		}

		if (header->h_refcount > 1) {
			error = ext2_extattr_block_clone(ip, &bp);
			if (error) {
				brelse(bp);
				return (error);
			}

			header = EXT2_HDR(bp);
		}

		/* Find if entry exist */
		for (entry = EXT2_FIRST_ENTRY(bp); !EXT2_IS_LAST_ENTRY(entry);
		    entry = EXT2_EXTATTR_NEXT(entry)) {
			if (ext2_extattr_attrnamespace_to_bsd(entry->e_name_index) !=
			    attrnamespace)
				continue;

			name_len = entry->e_name_len;
			attr_name = ext2_extattr_name_to_bsd(entry->e_name_index,
			    entry->e_name, &name_len);
			if (!attr_name) {
				brelse(bp);
				return (ENOTSUP);
			}

			if (strlen(name) == name_len &&
			    0 == strncmp(attr_name, name, name_len))
				break;
		}

		if (!EXT2_IS_LAST_ENTRY(entry)) {
			size = ext2_extattr_get_size(EXT2_FIRST_ENTRY(bp), entry,
			    sizeof(struct ext2fs_extattr_header),
			    entry->e_name_len, uio->uio_resid);
			if (size > bp->b_bufsize) {
				brelse(bp);
				return (ENOSPC);
			}

			ext2_extattr_set_exist_entry(bp->b_data, EXT2_FIRST_ENTRY(bp),
			    entry, bp->b_data + bp->b_bufsize, uio);
		} else {
			size = ext2_extattr_get_size(EXT2_FIRST_ENTRY(bp), NULL,
			    sizeof(struct ext2fs_extattr_header),
			    strlen(name), uio->uio_resid);
			if (size > bp->b_bufsize) {
				brelse(bp);
				return (ENOSPC);
			}

			entry = ext2_extattr_set_new_entry(bp->b_data, EXT2_FIRST_ENTRY(bp),
			    name, attrnamespace, bp->b_data + bp->b_bufsize, uio);

			/* Clean the same entry in the inode */
			error = ext2_extattr_inode_delete(ip, attrnamespace, name);
			if (error && error != ENOATTR) {
				brelse(bp);
				return (error);
			}
		}

		ext2_extattr_rehash(header, entry);
		ext2_extattr_blk_csum_set(ip, bp);

		return (bwrite(bp));
	}

	size = ext2_extattr_get_size(NULL, NULL,
	    sizeof(struct ext2fs_extattr_header),
	    strlen(ext2_extattr_name_to_linux(attrnamespace, name)), uio->uio_resid);
	if (size > fs->e2fs_bsize)
		return (ENOSPC);

	/* Allocate block, fill EA header and insert entry */
	ip->i_facl = ext2_alloc_meta(ip);
	if (0 == ip->i_facl)
		return (ENOSPC);

	ip->i_blocks += btodb(fs->e2fs_bsize);
	ext2_update(ip->i_vnode, 1);

	bp = getblk(ip->i_devvp, fsbtodb(fs, ip->i_facl), fs->e2fs_bsize, 0, 0, 0);
	if (!bp) {
		ext2_blkfree(ip, ip->i_facl, fs->e2fs_bsize);
		ip->i_blocks -= btodb(fs->e2fs_bsize);
		ip->i_facl = 0;
		ext2_update(ip->i_vnode, 1);
		return (EIO);
	}

	header = EXT2_HDR(bp);
	header->h_magic = EXTATTR_MAGIC;
	header->h_refcount = 1;
	header->h_blocks = 1;
	header->h_hash = 0;
	memset(header->h_reserved, 0, sizeof(header->h_reserved));
	memcpy(bp->b_data, header, sizeof(struct ext2fs_extattr_header));
	memset(EXT2_FIRST_ENTRY(bp), 0, sizeof(uint32_t));

	entry = ext2_extattr_set_new_entry(bp->b_data, EXT2_FIRST_ENTRY(bp),
	    name, attrnamespace, bp->b_data + bp->b_bufsize, uio);

	/* Clean the same entry in the inode */
	error = ext2_extattr_inode_delete(ip, attrnamespace, name);
	if (error && error != ENOATTR) {
		brelse(bp);
		return (error);
	}

	ext2_extattr_rehash(header, entry);
	ext2_extattr_blk_csum_set(ip, bp);

	return (bwrite(bp));
}

/*
	释放掉扩展属性所占用的数据块
*/
int ext2_extattr_free(struct inode *ip)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct ext2fs_extattr_header *header;
	int error;

	fs = ip->i_e2fs;

	if (!ip->i_facl)
		return (0);

	error = bread(ip->i_devvp, fsbtodb(fs, ip->i_facl),
	    fs->e2fs_bsize, NOCRED, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	/* Check attributes magic value */
	header = EXT2_HDR(bp);
	if (header->h_magic != EXTATTR_MAGIC || header->h_blocks != 1) {
		brelse(bp);
		return (EINVAL);
	}

	error = ext2_extattr_check(EXT2_FIRST_ENTRY(bp),
	    bp->b_data + bp->b_bufsize);
	if (error) {
		brelse(bp);
		return (error);
	}

	if (header->h_refcount > 1) {
		header->h_refcount--;
		bwrite(bp);
	} else {
		ext2_blkfree(ip, ip->i_facl, ip->i_e2fs->e2fs_bsize);
		brelse(bp);
	}

	ip->i_blocks -= btodb(ip->i_e2fs->e2fs_bsize);
	ip->i_facl = 0;
	ext2_update(ip->i_vnode, 1);

	return (0);
}
