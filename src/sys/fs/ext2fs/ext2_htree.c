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
 * $FreeBSD: releng/12.0/sys/fs/ext2fs/ext2_htree.c 337453 2018-08-08 12:07:45Z fsu $
 */

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/endian.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/dirent.h>
#include <sys/sysctl.h>

#include <ufs/ufs/dir.h>

#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/inode.h>
#include <fs/ext2fs/ext2_mount.h>
#include <fs/ext2fs/ext2fs.h>
#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/ext2_extern.h>
#include <fs/ext2fs/ext2_dinode.h>
#include <fs/ext2fs/ext2_dir.h>
#include <fs/ext2fs/htree.h>

static void	ext2_append_entry(char *block, uint32_t blksize,
		    struct ext2fs_direct_2 *last_entry,
		    struct ext2fs_direct_2 *new_entry, int csum_size);
static int	ext2_htree_append_block(struct vnode *vp, char *data,
		    struct componentname *cnp, uint32_t blksize);
static int	ext2_htree_check_next(struct inode *ip, uint32_t hash,
		    const char *name, struct ext2fs_htree_lookup_info *info);
static int	ext2_htree_cmp_sort_entry(const void *e1, const void *e2);
static int	ext2_htree_find_leaf(struct inode *ip, const char *name,
		    int namelen, uint32_t *hash, uint8_t *hash_version,
		    struct ext2fs_htree_lookup_info *info);
static uint32_t ext2_htree_get_block(struct ext2fs_htree_entry *ep);
static uint16_t	ext2_htree_get_count(struct ext2fs_htree_entry *ep);
static uint32_t ext2_htree_get_hash(struct ext2fs_htree_entry *ep);
static uint16_t	ext2_htree_get_limit(struct ext2fs_htree_entry *ep);
static void	ext2_htree_insert_entry_to_level(struct ext2fs_htree_lookup_level *level,
		    uint32_t hash, uint32_t blk);
static void	ext2_htree_insert_entry(struct ext2fs_htree_lookup_info *info,
		    uint32_t hash, uint32_t blk);
static uint32_t	ext2_htree_node_limit(struct inode *ip);
static void	ext2_htree_set_block(struct ext2fs_htree_entry *ep,
		    uint32_t blk);
static void	ext2_htree_set_count(struct ext2fs_htree_entry *ep,
		    uint16_t cnt);
static void	ext2_htree_set_hash(struct ext2fs_htree_entry *ep,
		    uint32_t hash);
static void	ext2_htree_set_limit(struct ext2fs_htree_entry *ep,
		    uint16_t limit);
static int	ext2_htree_split_dirblock(struct inode *ip,
		    char *block1, char *block2, uint32_t blksize,
		    uint32_t *hash_seed, uint8_t hash_version,
		    uint32_t *split_hash, struct  ext2fs_direct_2 *entry);
static void	ext2_htree_release(struct ext2fs_htree_lookup_info *info);
static uint32_t	ext2_htree_root_limit(struct inode *ip, int len);
static int	ext2_htree_writebuf(struct inode *ip,
		    struct ext2fs_htree_lookup_info *info);

/*
	该函数用来判断文件系统是否支持 hash 索引。从函数功能我们可以看到，这里是需要判断超级块的 feature 是否
	包含有 hash index 属性的。所以才文件系统初始化的时候，需要设置该属性到超级块当中
*/
int
ext2_htree_has_idx(struct inode *ip)
{
	if (EXT2_HAS_COMPAT_FEATURE(ip->i_e2fs, EXT2F_COMPAT_DIRHASHINDEX) &&
	    ip->i_flag & IN_E3INDEX)
		return (1);
	else
		return (0);
}

static int
ext2_htree_check_next(struct inode *ip, uint32_t hash, const char *name,
    struct ext2fs_htree_lookup_info *info)
{
	struct vnode *vp = ITOV(ip);
	struct ext2fs_htree_lookup_level *level;
	struct buf *bp;
	uint32_t next_hash;
	int idx = info->h_levels_num - 1;
	int levels = 0;

	do {
		level = &info->h_levels[idx];
		level->h_entry++;
		if (level->h_entry < level->h_entries +
		    ext2_htree_get_count(level->h_entries))
			break;
		if (idx == 0)
			return (0);
		idx--;
		levels++;
	} while (1);

	next_hash = ext2_htree_get_hash(level->h_entry);
	if ((hash & 1) == 0) {
		if (hash != (next_hash & ~1))
			return (0);
	}

	while (levels > 0) {
		levels--;
		if (ext2_blkatoff(vp, ext2_htree_get_block(level->h_entry) *
		    ip->i_e2fs->e2fs_bsize, NULL, &bp) != 0)
			return (0);
		level = &info->h_levels[idx + 1];
		brelse(level->h_bp);
		level->h_bp = bp;
		level->h_entry = level->h_entries =
		    ((struct ext2fs_htree_node *)bp->b_data)->h_entries;
	}

	return (1);
}

static uint32_t
ext2_htree_get_block(struct ext2fs_htree_entry *ep)
{
	return (ep->h_blk & 0x00FFFFFF);
}

static void
ext2_htree_set_block(struct ext2fs_htree_entry *ep, uint32_t blk)
{
	ep->h_blk = blk;
}

static uint16_t
ext2_htree_get_count(struct ext2fs_htree_entry *ep)
{
	return (((struct ext2fs_htree_count *)(ep))->h_entries_num);
}

static void
ext2_htree_set_count(struct ext2fs_htree_entry *ep, uint16_t cnt)
{
	((struct ext2fs_htree_count *)(ep))->h_entries_num = cnt;
}

/* 获取 entry 的 hash 值 */
static uint32_t
ext2_htree_get_hash(struct ext2fs_htree_entry *ep)
{
	return (ep->h_hash);
}

static uint16_t
ext2_htree_get_limit(struct ext2fs_htree_entry *ep)
{
	return (((struct ext2fs_htree_count *)(ep))->h_entries_max);
}

static void
ext2_htree_set_hash(struct ext2fs_htree_entry *ep, uint32_t hash)
{
	ep->h_hash = hash;
}

static void
ext2_htree_set_limit(struct ext2fs_htree_entry *ep, uint16_t limit)
{
	((struct ext2fs_htree_count *)(ep))->h_entries_max = limit;
}

static void
ext2_htree_release(struct ext2fs_htree_lookup_info *info)
{
	u_int i;

	for (i = 0; i < info->h_levels_num; i++) {
		struct buf *bp = info->h_levels[i].h_bp;

		if (bp != NULL)
			brelse(bp);
	}
}

/*
	在上级函数调用过程中，len 传进来的是 ext2fs_htree_root_info 的 info_len，再加上减去的
	. 和 .. 两个目录项的长度，可以大致推测出来目录项第一个数据块中的内容：
		./
		../
		struct ext2fs_htree_root_info
*/
static uint32_t
ext2_htree_root_limit(struct inode *ip, int len)
{
	struct m_ext2fs *fs;
	uint32_t space;

	fs = ip->i_e2fs;
	space = ip->i_e2fs->e2fs_bsize - EXT2_DIR_REC_LEN(1) -
	    EXT2_DIR_REC_LEN(2) - len;

	/* 
		如果有 tail 的话，还要把 tail 的所占用的空间给删掉。这个其实也是判断 csum 属性获知文件系统
		是否支持
	*/
	if (EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		space -= sizeof(struct ext2fs_htree_tail);

	return (space / sizeof(struct ext2fs_htree_entry));
}

/*
	node 节点是不包括 . 和 .. 两个目录项的，数据块的格式也是按照类似线性排布的方式进行。node 节点里边貌似是
	不会存放目录项的，第一个目录项只是为了保存整个数据块的大小等属性信息
*/
static uint32_t
ext2_htree_node_limit(struct inode *ip)
{
	struct m_ext2fs *fs;
	uint32_t space;

	fs = ip->i_e2fs;
	/* block size - 空目录项的大小？ */
	space = fs->e2fs_bsize - EXT2_DIR_REC_LEN(0);

	if (EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		space -= sizeof(struct ext2fs_htree_tail);

	return (space / sizeof(struct ext2fs_htree_entry));
}

/*
	ip 表示的是父级目录项对应的 inode。所以，如果我们在根目录查找的话，应该就是对应文件系统的 root node
*/
static int
ext2_htree_find_leaf(struct inode *ip, const char *name, int namelen,
    uint32_t *hash, uint8_t *hash_ver,
    struct ext2fs_htree_lookup_info *info)
{
	struct vnode *vp;
	struct ext2fs *fs;
	struct m_ext2fs *m_fs;
	struct buf *bp = NULL;
	struct ext2fs_htree_root *rootp;
	struct ext2fs_htree_entry *entp, *start, *end, *middle, *found;	/* root 节点管理的 entry */
	struct ext2fs_htree_lookup_level *level_info;	/* 有点类似于文件系统 inode 间接寻址，一般是两级 */
	uint32_t hash_major = 0, hash_minor = 0;
	uint32_t levels, cnt;
	uint8_t hash_version;

	if (name == NULL || info == NULL)
		return (-1);

	vp = ITOV(ip);
	fs = ip->i_e2fs->e2fs;
	m_fs = ip->i_e2fs;

	/*
		通过逻辑块号读取对应磁盘上的数据块。需要注意的是，逻辑块号这里直接指定的是0，说明读取的是第一个逻辑块。
		根据文档中的说明可以看到，root 节点的信息是保存在第一个逻辑块当中的

		奇海 newtpt 工具的执行流程可能需要改一下，目前采用的方式是直接将目录项信息写入，没有这里 htree 的信息，是否
		需要补充上呢？ 其他数据块是否也需要相应的做一些处理？
		观察 ext2fs_htree_root 结构体设计可以看出，前面的字段就是按照目录项在磁盘块中排布进行的。从这里其实也可以反向
		推一下磁盘存储目录项的数据块整体结构
	*/
	if (ext2_blkatoff(vp, 0, NULL, &bp) != 0)
		return (-1);

	/* ext2fs_htree_lookup_info 应该就是对查找信息的一个封装 */
	info->h_levels_num = 1;	/* 表示 h_levels 数组中已有元素的数量？ */
	info->h_levels[0].h_bp = bp;
	rootp = (struct ext2fs_htree_root *)bp->b_data;	/* 获取根节点信息 */

	/* 判断根节点 hash 属性，为后续计算hash做准备 */
	if (rootp->h_info.h_hash_version != EXT2_HTREE_LEGACY &&
	    rootp->h_info.h_hash_version != EXT2_HTREE_HALF_MD4 &&
	    rootp->h_info.h_hash_version != EXT2_HTREE_TEA)
		goto error;

	hash_version = rootp->h_info.h_hash_version;
	if (hash_version <= EXT2_HTREE_TEA)
		hash_version += m_fs->e2fs_uhash;
	*hash_ver = hash_version;	/* 这里处理需要返回给上级的 hash version */

	/*
		在数据块中的目录项的 hash 值是顺序排布的。通过 hash_major 和 hash_minor 就可以判断当前
		所要查找的 hash 是否在所包含的范围之内。要注意的是这里计算的是要查找的那个目录项的 hash 值
	*/
	ext2_htree_hash(name, namelen, fs->e3fs_hash_seed,
	    hash_version, &hash_major, &hash_minor);
	/* 将 hash 进行赋值 */
	*hash = hash_major;

	/* 
		目前来看，目录项不会包含特别多的数据，所以两级查找完全是够了的。在设计 ext2 hash tree 的时候作者考虑到了这一点，
		对查找等级做了一些限制。如果奇海操作系统中的目录项规模比较大，就可以在这里把查找等级放开
	*/
	if ((levels = rootp->h_info.h_ind_levels) > 1)
		goto error;

	/* entry pointer 应该是指向 ext2fs_htree_root 中的 ext2fs_htree_entry 类型数组的第一个元素 */
	entp = (struct ext2fs_htree_entry *)(((char *)&rootp->h_info) +
	    rootp->h_info.h_info_len);

	/*
		ext2_htree_get_limit: 判断 entry 数量的最大值
		ext2_htree_root_limit: 计算除去 . / .. / info 之后数据块剩余空间的大小。这里就从侧面验证了目录项
			的第一个块中的数据跟跟之前认为的不一样(可能包含有一些info，而不仅仅是目录项数据)。其实 FreeBSD 中 ext2
			引入了许多 ext4 的设计，可以参考一下 ext4 hash tree 的设计和 disk layout
	*/
	if (ext2_htree_get_limit(entp) !=
	    ext2_htree_root_limit(ip, rootp->h_info.h_info_len))
		goto error;
	
	/*
		上面的代码逻辑更多的还是针对root节点来的，但是从函数命名来看，它的功能其实是查找页节点。所以还是按照正常
		的步骤从根节点处一步步往下找，所以下面的while循环才是整个函数的重点内容
	*/
	while (1) {
		cnt = ext2_htree_get_count(entp);	/* 判断第一个 entry 所拥有的子 entry 的数量 */
		if (cnt == 0 || cnt > ext2_htree_get_limit(entp))
			goto error;

		/* entp 是 struct ext2fs_htree_entry* 类型，+1直接跳转到下一项(第一个entry表示的可能是它本身) */
		start = entp + 1;
		end = entp + cnt - 1;
		/* 
			- 看着像是二分查找，要注意的是，这里的 start 和 end 都是 entry 指针，不是索引值。
			- 每一个 entry 计算出cnt对应的是从 start 到 count 这一组 entry。通常理解就是，每一个节点对应多个节点，
				以此类推，就形成了一个树状结构。每个节点对应的子节点的数量应该是不一样的
		*/
		while (start <= end) {
			middle = start + (end - start) / 2;
			if (ext2_htree_get_hash(middle) > hash_major)
				end = middle - 1;
			else
				start = middle + 1;
		}
		found = start - 1;

		level_info = &(info->h_levels[info->h_levels_num - 1]);
		level_info->h_bp = bp;
		/* 
			entp 指向数据块中的 entry 数据的起始位置，一直没有被改变。所以 level_info->h_entries 表示的是
			数据块中 entry 组的起始位置
		*/
		level_info->h_entries = entp;
		level_info->h_entry = found;	/* level_info->h_entry 表示的是被找到的那个entry */
		/* 当 level 为0的时候，应该就是直接索引 */
		if (levels == 0)	
			return (0);
		levels--;
		/* 
			从这里可以看出，ext2_htree_get_block 得到的其实还是文件逻辑块，而不是磁盘块。对磁盘的
			操作本质上是应该驱动做的，所以在文件系统这一层面，我们应该操作的是逻辑块，通过 bmap 函数
			再去查找对应的磁盘块
		*/
		if (ext2_blkatoff(vp,
		    ext2_htree_get_block(found) * m_fs->e2fs_bsize,
		    NULL, &bp) != 0)
			goto error;
		entp = ((struct ext2fs_htree_node *)bp->b_data)->h_entries;
		info->h_levels_num++;
		info->h_levels[info->h_levels_num - 1].h_bp = bp;
	}	// end while

	/*
		经过上面的循环可知，htree 查找数据全部都放到了 info 当中，上层的函数就可以通过收集到的信息进行查找
	*/
error:
	ext2_htree_release(info);
	return (-1);
}

/*
 * Try to lookup a directory entry in HTree index
 * 在 htree 中尝试查找一个目录 entry
 * 
 * 	entryoffp 表示的是应该是目录项在数据块中的偏移，所以它作用的对象应该是存放目录项成员的数据块，而不是
 * 存放 node 的数据块。
 * 
 * 	offp 传入的是 i_offset，表示所要查找的目录项成员的偏移量(相对于文件的偏移还是数据块的偏移？，感觉是
 * 相对于文件的偏移，上面的参数已经表示相对于数据块的偏移了)
 * 
 * 	prevoff 表示的是上一次查找的目录项的偏移量？
 * 
 * 	上述这些参数在上级函数中都是没有初始化的参数，应该就是要借助这个函数进行填充后，再在上级函数中处理
 */
int
ext2_htree_lookup(struct inode *ip, const char *name, int namelen,
    struct buf **bpp, int *entryoffp, doff_t *offp,
    doff_t *prevoffp, doff_t *endusefulp,
    struct ext2fs_searchslot *ss)
{
	struct vnode *vp;
	/* 这是一个结构体实例，而不是指针，要注意 */
	struct ext2fs_htree_lookup_info info;
	struct ext2fs_htree_entry *leaf_node;
	struct m_ext2fs *m_fs;
	struct buf *bp;
	uint32_t blk;	/* block number */
	uint32_t dirhash;	/* 由 name 计算得到的 hash */
	uint32_t bsize;	/* block size */
	uint8_t hash_version;	
	int search_next;
	int found = 0;

	m_fs = ip->i_e2fs;
	bsize = m_fs->e2fs_bsize;
	vp = ITOV(ip);

	/* TODO: print error msg because we don't lookup '.' and '..' */

	memset(&info, 0, sizeof(info));
	/*
		通过 find_leaf 函数，我们把要查找的叶结点的信息保存到了 dirhash / hash_version / info 之中。
		参数 ip 表示的应该是父目录项对应的 inode 的指针
	*/
	if (ext2_htree_find_leaf(ip, name, namelen, &dirhash,
	    &hash_version, &info))
		return (-1);

	/*
		通过 find_leaf 函数将查找信息保存到 info 当中，用于后续对叶节点的查找。
		这个函数的除了 find leaf 函数之外的主要代码逻辑就是这个 while 循环了，所以重点关注这个循环做了哪些操作
	*/
	do {
		leaf_node = info.h_levels[info.h_levels_num - 1].h_entry;	/* 表示被找到的那个entry */
		blk = ext2_htree_get_block(leaf_node);
		/* 
			上述获取到的应该是文件的逻辑块，然后借助 bmap 函数将对应的磁盘块中的数据读取出来，存放到 buf 当中
		*/
		if (ext2_blkatoff(vp, blk * bsize, NULL, &bp) != 0) {
			ext2_htree_release(&info);
			return (-1);
		}

		*offp = blk * bsize;	// offset pointer，应该是距离目录文件起始位置的偏移
		*entryoffp = 0;
		*prevoffp = blk * bsize;
		*endusefulp = blk * bsize;

		if (ss->slotstatus == NONE) {
			ss->slotoffset = -1;
			ss->slotfreespace = 0;
		}
		/*
			这个函数的意思应该是在目录项数据快中去查找我们所要找的 entry，应该不是去查找目录项的数据块。
			因为目录项数据块已经在上面的代码操作中读取到了 buf，这里也传入了 buf->b_data,其实也印证了
			猜测
		*/
		if (ext2_search_dirblock(ip, bp->b_data, &found,
		    name, namelen, entryoffp, offp, prevoffp,
		    endusefulp, ss) != 0) {
			brelse(bp);
			ext2_htree_release(&info);
			return (-1);
		}

		/* 
			found 是我们查找到 entry 的标志。如果找到的话，就直接返回 buf 并且释放掉 info；
			如果没有找到的话，执行下面的代码分支，即 ext2_htree_check_next。如果遍历完成之后
			还没有找到，那应该就是退出循环，返回 ENOENT；
			但是在 tptfs 的调试中发现，它在 find_leaf 那一步就直接挂掉了，根本就没有执行下面的
			代码逻辑。所以要查找 find_leaf 为什么会出错
		*/
		if (found) {
			*bpp = bp;
			ext2_htree_release(&info);
			return (0);
		}

		brelse(bp);
		search_next = ext2_htree_check_next(ip, dirhash, name, &info);
	} while (search_next);

	ext2_htree_release(&info);
	return (ENOENT);
}

static int
ext2_htree_append_block(struct vnode *vp, char *data,
    struct componentname *cnp, uint32_t blksize)
{
	struct iovec aiov;
	struct uio auio;
	struct inode *dp = VTOI(vp);
	uint64_t cursize, newsize;
	int error;

	cursize = roundup(dp->i_size, blksize);
	newsize = cursize + blksize;

	auio.uio_offset = cursize;
	auio.uio_resid = blksize;
	aiov.iov_len = blksize;
	aiov.iov_base = data;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_rw = UIO_WRITE;
	auio.uio_segflg = UIO_SYSSPACE;
	/*
		这里会去调用 ext2_write 函数，其中又会调用 ext2_balloc 函数，开始有了数据块分配操作。也就
		意味着 hash tree 开始增长
	*/
	error = VOP_WRITE(vp, &auio, IO_SYNC, cnp->cn_cred);
	if (!error)
		dp->i_size = newsize;

	return (error);
}

static int
ext2_htree_writebuf(struct inode* ip, struct ext2fs_htree_lookup_info *info)
{
	int i, error;

	for (i = 0; i < info->h_levels_num; i++) {
		struct buf *bp = info->h_levels[i].h_bp;
		ext2_dx_csum_set(ip, (struct ext2fs_direct_2 *)bp->b_data);
		error = bwrite(bp);
		if (error)
			return (error);
	}

	return (0);
}

/* 
	向 lookup_level 中插入一个entry。lookup_level 应该是完全在内存中存在的对象。从一些函数的
	操作来看，从磁盘中读取的数据都是先放到 buf 当中，处理之后再对level结构进行填充
*/
static void
ext2_htree_insert_entry_to_level(struct ext2fs_htree_lookup_level *level,
    uint32_t hash, uint32_t blk)
{
	struct ext2fs_htree_entry *target;
	int entries_num;

	target = level->h_entry + 1;
	entries_num = ext2_htree_get_count(level->h_entries);

	memmove(target + 1, target, (char *)(level->h_entries + entries_num) -
	    (char *)target);
	ext2_htree_set_block(target, blk);
	ext2_htree_set_hash(target, hash);
	ext2_htree_set_count(level->h_entries, entries_num + 1);
}

/*
 * Insert an index entry to the index node.
 */
static void
ext2_htree_insert_entry(struct ext2fs_htree_lookup_info *info,
    uint32_t hash, uint32_t blk)
{
	struct ext2fs_htree_lookup_level *level;

	level = &info->h_levels[info->h_levels_num - 1];
	ext2_htree_insert_entry_to_level(level, hash, blk);
}

/*
 * Compare two entry sort descriptors by name hash value.
 * This is used together with qsort.
 * 按名称哈希值比较两个条目排序描述符。这与qsort(C/C++快速排序标准库函数)一起使用
 */
static int
ext2_htree_cmp_sort_entry(const void *e1, const void *e2)
{
	const struct ext2fs_htree_sort_entry *entry1, *entry2;

	entry1 = (const struct ext2fs_htree_sort_entry *)e1;
	entry2 = (const struct ext2fs_htree_sort_entry *)e2;

	if (entry1->h_hash < entry2->h_hash)
		return (-1);
	if (entry1->h_hash > entry2->h_hash)
		return (1);
	return (0);
}

/*
 * Append an entry to the end of the directory block. 在目录快的末尾添加一个entry
 */
static void
ext2_append_entry(char *block, uint32_t blksize,
    struct ext2fs_direct_2 *last_entry,
    struct ext2fs_direct_2 *new_entry, int csum_size)
{
	uint16_t entry_len;

	/* 宏的实现可能要改一下，因为tptfs目录项的长度已经是固定的了 */
	entry_len = EXT2_DIR_REC_LEN(last_entry->e2d_namlen);
	last_entry->e2d_reclen = entry_len;
	last_entry = (struct ext2fs_direct_2 *)((char *)last_entry + entry_len);
	new_entry->e2d_reclen = block + blksize - (char *)last_entry - csum_size;
	memcpy(last_entry, new_entry, EXT2_DIR_REC_LEN(new_entry->e2d_namlen));
}

/*
 * Move half of entries from the old directory block to the new one.
 * 从上层函数调用的情况来看，block1 存放的是原有目录项数据块中的数据备份，block2 为空，
 * entry 表示的是需要被创建的目录项。这个函数所要计算的就是 split_hash 参数，它的作用
 * 应该就是作为两个数据块中目录项分布策略的比较对象
 */
static int
ext2_htree_split_dirblock(struct inode *ip, char *block1, char *block2,
    uint32_t blksize, uint32_t *hash_seed, uint8_t hash_version,
    uint32_t *split_hash, struct ext2fs_direct_2 *entry)
{
	struct m_ext2fs *fs;
	int entry_cnt = 0;
	int size = 0, csum_size = 0;
	int i, k;
	uint32_t offset;
	uint16_t entry_len = 0;
	uint32_t entry_hash;
	struct ext2fs_direct_2 *ep, *last;
	char *dest;
	struct ext2fs_htree_sort_entry *sort_info;

	fs = ip->i_e2fs;
	ep = (struct ext2fs_direct_2 *)block1;
	dest = block2;
	/*
		这里的 sort_info 指向了数据块的末尾，下面的 while 循环中它的操作都是自减，
		感觉这里的处理方式是倒序；
		sort_entry 中一共包含有三个参数： offset、size 和 hash，offset 表示的应该是该目录项在原数据块中的偏移量，
		size 表示的是目录项的大小，hash 表示的是该目录项的 hash 值。sort_info 的数据是保存在 buf2 当中
 	*/
	sort_info = (struct ext2fs_htree_sort_entry *)
	    ((char *)block2 + blksize);

	if (EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		csum_size = sizeof(struct ext2fs_direct_tail);

	/*
	 * Calculate name hash value for the entry which is to be added.
	 * 利用新创建的目录项的名字计算该项的 hash 值，并保存到 entry_hash 当中，在数据块拆分中会使用到
	 */
	ext2_htree_hash(entry->e2d_name, entry->e2d_namlen, hash_seed,
	    hash_version, &entry_hash, NULL);

	/*
	 * Fill in directory entry sort descriptors.
	 * ep 指向 block1，这里把所有的目录项成员的 hash 都计算了出来，放到 sort_info 项当中
	 */
	while ((char *)ep < block1 + blksize - csum_size) {
		if (ep->e2d_ino && ep->e2d_namlen) {
			entry_cnt++;
			sort_info--;
			sort_info->h_size = ep->e2d_reclen;
			sort_info->h_offset = (char *)ep - block1;
			ext2_htree_hash(ep->e2d_name, ep->e2d_namlen,
			    hash_seed, hash_version,
			    &sort_info->h_hash, NULL);
		}
		ep = (struct ext2fs_direct_2 *)
		    ((char *)ep + ep->e2d_reclen);
	}

	/*
	 * Sort directory entry descriptors by name hash value.
	 * 上面把所有的目录项成员hash值都计算出来了，下面开始对这些 hash 值进行排序
	 */
	qsort(sort_info, entry_cnt, sizeof(struct ext2fs_htree_sort_entry),
	    ext2_htree_cmp_sort_entry);

	/*
	 * Count the number of entries to move to directory block 2.
	 * 计算需要转移到 block2 的目录项的个数，注意 sort_info 目前已经是顺序排列了。
	 * 从代码逻辑可以看出，需要把一半的目录项转移到另外一个数据块中
	 */
	for (i = entry_cnt - 1; i >= 0; i--) {
		if (sort_info[i].h_size + size > blksize / 2)
			break;
		size += sort_info[i].h_size;
	}

	*split_hash = sort_info[i + 1].h_hash;

	/*
	 * Set collision bit.
	 * 如果处于中间的两个目录项的 hash 值刚好相等，那就将 split_hash 自加1
	 */
	if (*split_hash == sort_info[i].h_hash)
		*split_hash += 1;

	/*
	 * Move half of directory entries from block 1 to block 2.
	 */
	for (k = i + 1; k < entry_cnt; k++) {
		ep = (struct ext2fs_direct_2 *)((char *)block1 +
		    sort_info[k].h_offset);
		entry_len = EXT2_DIR_REC_LEN(ep->e2d_namlen);
		memcpy(dest, ep, entry_len);
		((struct ext2fs_direct_2 *)dest)->e2d_reclen = entry_len;
		/* Mark directory entry as unused. */
		ep->e2d_ino = 0;
		dest += entry_len;
	}
	dest -= entry_len;

	/* Shrink directory entries in block 1. 
		收缩块1中的目录项
	*/
	last = (struct ext2fs_direct_2 *)block1;
	entry_len = 0;
	for (offset = 0; offset < blksize - csum_size; ) {
		ep = (struct ext2fs_direct_2 *)(block1 + offset);
		offset += ep->e2d_reclen;
		if (ep->e2d_ino) {
			last = (struct ext2fs_direct_2 *)
			    ((char *)last + entry_len);
			entry_len = EXT2_DIR_REC_LEN(ep->e2d_namlen);
			memcpy((void *)last, (void *)ep, entry_len);
			last->e2d_reclen = entry_len;
		}
	}

	if (entry_hash >= *split_hash) {
		/* Add entry to block 2. */
		ext2_append_entry(block2, blksize,
		    (struct ext2fs_direct_2 *)dest, entry, csum_size);

		/* Adjust length field of last entry of block 1. */
		last->e2d_reclen = block1 + blksize - (char *)last - csum_size;
	} else {
		/* Add entry to block 1. */
		ext2_append_entry(block1, blksize, last, entry, csum_size);

		/* Adjust length field of last entry of block 2. */
		((struct ext2fs_direct_2 *)dest)->e2d_reclen =
		    block2 + blksize - dest - csum_size;
	}

	if (csum_size) {
		ext2_init_dirent_tail(EXT2_DIRENT_TAIL(block1, blksize));
		ext2_init_dirent_tail(EXT2_DIRENT_TAIL(block2, blksize));
	}
	/* 处理完后的数据都是暂存在两个 buffer 当中 */
	return (0);
}

/*
 * Create an HTree index for a directory
 */
int
ext2_htree_create_index(struct vnode *vp, struct componentname *cnp,
    struct ext2fs_direct_2 *new_entry)
{
	struct buf *bp = NULL;
	struct inode *dp;
	struct ext2fs *fs;
	struct m_ext2fs *m_fs;
	struct ext2fs_direct_2 *ep, *dotdot;
	struct ext2fs_htree_root *root;
	/*
		info 只是一个局部变量，它可能不会保存到磁盘当中，有可能只是每次在 htree 开启的时候动态创建，
		动作执行完成之后就被销毁 
	*/
	struct ext2fs_htree_lookup_info info;
	uint32_t blksize, dirlen, split_hash;
	uint8_t hash_version;
	char *buf1 = NULL;
	char *buf2 = NULL;
	int error = 0;

	dp = VTOI(vp);
	fs = dp->i_e2fs->e2fs;
	m_fs = dp->i_e2fs;
	blksize = m_fs->e2fs_bsize;

	buf1 = malloc(blksize, M_TEMP, M_WAITOK | M_ZERO);
	buf2 = malloc(blksize, M_TEMP, M_WAITOK | M_ZERO);

	/*
		root node 并不是对应系统的根节点，而是对应于某个目录项。所以这里还是读取的目录项
		对应的第一个逻辑块数据
	*/
	if ((error = ext2_blkatoff(vp, 0, NULL, &bp)) != 0)
		goto out;

	/* 
		第一个逻辑块就是对应 root 节点。从这里可以看出，每个目录项的第一个数据块应该都是 htree_root，
		并且此时数据块中已经填充了对应的数据。所以肯定是在此之前的某个函数完成了写入操作
	*/
	root = (struct ext2fs_htree_root *)bp->b_data;
	dotdot = (struct ext2fs_direct_2 *)((char *)&(root->h_dotdot));
	/*
		除了 . 和 .. 两个目录项外，剩余空间的起始位置指针和数据大小。这个对应 htree root node 的设计，目录项的第一个
		逻辑块中只有 root node 数据；
		从这里可以看出，dotdot->e2d_reclen 不应该被设置为数据块剩余长度，就是它本身的长度。所以 tptfs 目前对于目录项信息
		的设置需要修改；
		此时 ep 在这里指向的 root info 的起始地址，dirlen 表示的数据块除去 . 和 .. 两个目录项成员所占空间后剩余空间的大小
	*/
	ep = (struct ext2fs_direct_2 *)((char *)dotdot + dotdot->e2d_reclen);
	dirlen = (char *)root + blksize - (char *)ep;

	/* 
		将数据块中的剩余数据拷贝到 buf1 当中。这里就存在另外一个问题，.. 目录项的长度是仅包括它自己，还是把下面的 root info
		信息也包含在内。ep 指向了 buf1
	*/
	memcpy(buf1, ep, dirlen);
	ep = (struct ext2fs_direct_2 *)buf1;

	/*
		从代码逻辑可以看出，原来这个数据块中的数据貌似就是线性存储的方式，然后这里将除了 . 和 .. 两个目录项
		的其他所有数据都拷贝到了缓存，利用对缓存处理的结果再对 root node 中的元素进行赋值；
		tptfs 感觉是可以利用这个函数初始化 root node，要把数据块分割成三个目录项：. / .. / 空目录。然后
		空目录项的长度 = 4096 - 256 * 2
	*/
	while ((char *)ep < buf1 + dirlen)
		ep = (struct ext2fs_direct_2 *)
		    ((char *)ep + ep->e2d_reclen);
	ep->e2d_reclen = buf1 + blksize - (char *)ep;

	/*
		要注意这一步操作，在 ext2_htree_create_index 执行的过程中将 inode flag 设置为 hash index。
		而不能直接手动进行操作。
	*/
	dp->i_flag |= IN_E3INDEX;

	/*
	 * Initialize index root.
	 * 
	 * .. 的目录项长度还是要设置为除去 . 之后剩余空间的长度
	 */
	dotdot->e2d_reclen = blksize - EXT2_DIR_REC_LEN(1);
	memset(&root->h_info, 0, sizeof(root->h_info));
	root->h_info.h_hash_version = fs->e3fs_def_hash_version;
	root->h_info.h_info_len = sizeof(root->h_info);
	/*
		设置第一个 ext2fs_htree_entry，表示的应该就是 root node 的相关信息
	*/
	ext2_htree_set_block(root->h_entries, 1);
	ext2_htree_set_count(root->h_entries, 1);
	ext2_htree_set_limit(root->h_entries,
	    ext2_htree_root_limit(dp, sizeof(root->h_info)));

	/*
		设置 htree lookup info 信息。info 可看到是一个局部变量，并且在磁盘结构体中并没有该字段。
		所以，注意下面的代码逻辑是否包含对该信息的保存操作，如果没有的话，那它的生命周期就是函数执行
		的这段时间
	*/
	memset(&info, 0, sizeof(info));
	info.h_levels_num = 1;	/* 表示当前的搜索等级数组中只有一个元素 */
	info.h_levels[0].h_entries = root->h_entries;
	info.h_levels[0].h_entry = root->h_entries;
	hash_version = root->h_info.h_hash_version;
	/*
		当 hash_version < EXT2_HTREE_TEA 的时候，就要判断 hash_version 是否要转换成 unsigned 类型
	*/
	if (hash_version <= EXT2_HTREE_TEA)
		hash_version += m_fs->e2fs_uhash;
	
	/* new_entry 表示的是最新的需要创建的目录项成员 */
	ext2_htree_split_dirblock(dp, buf1, buf2, blksize, fs->e3fs_hash_seed,
	    hash_version, &split_hash, new_entry);
	ext2_htree_insert_entry(&info, split_hash, 2);

	/*
	 * Write directory block 0.
	 */
	ext2_dx_csum_set(dp, (struct ext2fs_direct_2 *)bp->b_data);
	if (DOINGASYNC(vp)) {
		bdwrite(bp);
		error = 0;
	} else {
		error = bwrite(bp);
	}
	dp->i_flag |= IN_CHANGE | IN_UPDATE;
	if (error)
		goto out;

	/*
	 * Write directory block 1.
	 */
	ext2_dirent_csum_set(dp, (struct ext2fs_direct_2 *)buf1);
	error = ext2_htree_append_block(vp, buf1, cnp, blksize);
	if (error)
		goto out1;

	/*
	 * Write directory block 2.
	 */
	ext2_dirent_csum_set(dp, (struct ext2fs_direct_2 *)buf2);
	error = ext2_htree_append_block(vp, buf2, cnp, blksize);

	free(buf1, M_TEMP);
	free(buf2, M_TEMP);
	return (error);
out:
	if (bp != NULL)
		brelse(bp);
out1:
	free(buf1, M_TEMP);
	free(buf2, M_TEMP);
	return (error);
}

/*
 * Add an entry to the directory using htree index.
 */
int
ext2_htree_add_entry(struct vnode *dvp, struct ext2fs_direct_2 *entry,
    struct componentname *cnp)
{
	struct ext2fs_htree_entry *entries, *leaf_node;
	struct ext2fs_htree_lookup_info info;
	struct buf *bp = NULL;
	struct ext2fs *fs;
	struct m_ext2fs *m_fs;
	struct inode *ip;
	uint16_t ent_num;
	uint32_t dirhash, split_hash;
	uint32_t blksize, blknum;
	uint64_t cursize, dirsize;
	uint8_t hash_version;
	char *newdirblock = NULL;
	char *newidxblock = NULL;
	struct ext2fs_htree_node *dst_node;
	struct ext2fs_htree_entry *dst_entries;
	struct ext2fs_htree_entry *root_entires;
	struct buf *dst_bp = NULL;
	int error, write_bp = 0, write_dst_bp = 0, write_info = 0;

	ip = VTOI(dvp);
	m_fs = ip->i_e2fs;
	fs = m_fs->e2fs;
	blksize = m_fs->e2fs_bsize;

	/*
		目录项中可用的 slot 不为 0 的时候，直接调用 ext2_add_entry，这个函数是添加一个目录项成员，
		而不是添加一个 hash 索引。当 i_count ！= 0 的时候会执行下面的函数。根据调试结果来看，这个
		函数把目录项成员添加到了 node 所在的数据块当中，正常的话应该是添加到目录项成员所在的数据块。
		所以这个处理逻辑还是存在问题。
		在挂载目录中添加第一个目录项成员的时候，感觉 i_count 应该是0，这样才会分配新的 block。导致
		icount 不为 0 的原因有可能是 inode 初始化的时候对 i_size 的设置不正确。
	*/
	if (ip->i_count != 0)
		return ext2_add_entry(dvp, entry);

	/* Target directory block is full, split it */
	memset(&info, 0, sizeof(info));
	error = ext2_htree_find_leaf(ip, entry->e2d_name, entry->e2d_namlen,
	    &dirhash, &hash_version, &info);
	if (error)
		return (error);

	entries = info.h_levels[info.h_levels_num - 1].h_entries;
	ent_num = ext2_htree_get_count(entries);
	if (ent_num == ext2_htree_get_limit(entries)) {
		/* Split the index node. */
		root_entires = info.h_levels[0].h_entries;
		newidxblock = malloc(blksize, M_TEMP, M_WAITOK | M_ZERO);
		dst_node = (struct ext2fs_htree_node *)newidxblock;
		memset(&dst_node->h_fake_dirent, 0,
		    sizeof(dst_node->h_fake_dirent));
		dst_node->h_fake_dirent.e2d_reclen = blksize;

		cursize = roundup(ip->i_size, blksize);
		dirsize = cursize + blksize;
		blknum = dirsize / blksize - 1;
		ext2_dx_csum_set(ip, (struct ext2fs_direct_2 *)newidxblock);
		error = ext2_htree_append_block(dvp, newidxblock,
		    cnp, blksize);
		if (error)
			goto finish;
		error = ext2_blkatoff(dvp, cursize, NULL, &dst_bp);
		if (error)
			goto finish;
		dst_node = (struct ext2fs_htree_node *)dst_bp->b_data;
		dst_entries = dst_node->h_entries;

		if (info.h_levels_num == 2) {
			uint16_t src_ent_num, dst_ent_num;

			if (ext2_htree_get_count(root_entires) ==
			    ext2_htree_get_limit(root_entires)) {
				/* Directory index is full */
				error = EIO;
				goto finish;
			}

			src_ent_num = ent_num / 2;
			dst_ent_num = ent_num - src_ent_num;
			split_hash = ext2_htree_get_hash(entries + src_ent_num);

			/* Move half of index entries to the new index node */
			memcpy(dst_entries, entries + src_ent_num,
			    dst_ent_num * sizeof(struct ext2fs_htree_entry));
			ext2_htree_set_count(entries, src_ent_num);
			ext2_htree_set_count(dst_entries, dst_ent_num);
			ext2_htree_set_limit(dst_entries,
			    ext2_htree_node_limit(ip));

			if (info.h_levels[1].h_entry >= entries + src_ent_num) {
				struct buf *tmp = info.h_levels[1].h_bp;

				info.h_levels[1].h_bp = dst_bp;
				dst_bp = tmp;

				info.h_levels[1].h_entry =
				    info.h_levels[1].h_entry -
				    (entries + src_ent_num) +
				    dst_entries;
				info.h_levels[1].h_entries = dst_entries;
			}
			ext2_htree_insert_entry_to_level(&info.h_levels[0],
			    split_hash, blknum);

			/* Write new index node to disk */
			ext2_dx_csum_set(ip,
			    (struct ext2fs_direct_2 *)dst_bp->b_data);
			error = bwrite(dst_bp);
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
			if (error)
				goto finish;
			write_dst_bp = 1;
		} else {
			/* Create second level for htree index */
			struct ext2fs_htree_root *idx_root;

			memcpy(dst_entries, entries,
			    ent_num * sizeof(struct ext2fs_htree_entry));
			ext2_htree_set_limit(dst_entries,
			    ext2_htree_node_limit(ip));

			idx_root = (struct ext2fs_htree_root *)
			    info.h_levels[0].h_bp->b_data;
			idx_root->h_info.h_ind_levels = 1;

			ext2_htree_set_count(entries, 1);
			ext2_htree_set_block(entries, blknum);

			info.h_levels_num = 2;
			info.h_levels[1].h_entries = dst_entries;
			info.h_levels[1].h_entry = info.h_levels[0].h_entry -
			    info.h_levels[0].h_entries + dst_entries;
			info.h_levels[1].h_bp = dst_bp;
			dst_bp = NULL;
		}
	}

	leaf_node = info.h_levels[info.h_levels_num - 1].h_entry;
	blknum = ext2_htree_get_block(leaf_node);
	error = ext2_blkatoff(dvp, blknum * blksize, NULL, &bp);
	if (error)
		goto finish;

	/* Split target directory block */
	newdirblock = malloc(blksize, M_TEMP, M_WAITOK | M_ZERO);
	ext2_htree_split_dirblock(ip, (char *)bp->b_data, newdirblock, blksize,
	    fs->e3fs_hash_seed, hash_version, &split_hash, entry);
	cursize = roundup(ip->i_size, blksize);
	dirsize = cursize + blksize;
	blknum = dirsize / blksize - 1;

	/* Add index entry for the new directory block */
	ext2_htree_insert_entry(&info, split_hash, blknum);

	/* Write the new directory block to the end of the directory */
	ext2_dirent_csum_set(ip, (struct ext2fs_direct_2 *)newdirblock);
	error = ext2_htree_append_block(dvp, newdirblock, cnp, blksize);
	if (error)
		goto finish;

	/* Write the target directory block */
	ext2_dirent_csum_set(ip, (struct ext2fs_direct_2 *)bp->b_data);
	error = bwrite(bp);
	ip->i_flag |= IN_CHANGE | IN_UPDATE;
	if (error)
		goto finish;
	write_bp = 1;

	/* Write the index block */
	error = ext2_htree_writebuf(ip, &info);
	if (!error)
		write_info = 1;

finish:
	if (dst_bp != NULL && !write_dst_bp)
		brelse(dst_bp);
	if (bp != NULL && !write_bp)
		brelse(bp);
	if (newdirblock != NULL)
		free(newdirblock, M_TEMP);
	if (newidxblock != NULL)
		free(newidxblock, M_TEMP);
	if (!write_info)
		ext2_htree_release(&info);
	return (error);
}
