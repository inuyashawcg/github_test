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
 * $FreeBSD: releng/12.0/sys/fs/ext2fs/ext2_acl.h 326474 2017-12-02 17:22:55Z pfg $
 */

#ifndef _FS_EXT2FS_EXT2_ACL_H_
#define	_FS_EXT2FS_EXT2_ACL_H_

#define	EXT4_ACL_VERSION	0x0001

/* 
	- ACL 结构体的定义与 Linux 中是一致的，ext2文件系统的acl结构体，遵循posix标准，和POSIX标准的一样
	- 首先我想说一下什么是acl，之前的linux系统对于文件的权限管理都是9位的管理方式，user，group，other各三位，
	分别是rwx，读写执行三个权限，但是后来发现这种方式有缺陷，所以呢，访问控制列表ACL就应运而生
	- 比如user1，user2和user3都在group组里边，但是user1的文件只想让user2查看，而不想让user3查看，为此，user1
	需要建立一个只有user1和user2的组，这不但带来了不必要的开销，也是有安全隐患的。而现在用acl机制，可以给予用户，
	目录以单独的权限，使用命令  setfacl -m user:user3:--- file 就可以使 user3 对 file 没有任何权限

	我们使用getfacl file, 可以看到如下输出 :
	user::rwx
	user:user2:rwx
	group::r-w
	other::r--
	mask::rwx
	其中每一行都对应着一条具体的访问规则，就对应着一个acl的结构体
*/
struct ext2_acl_entry {
	/*
		tag 表示acl结构体的标志，在posix标准中规定一共有6种：ACL_USER_OBJ, ACL_USER, ACL_GROUP_OBJ, 
		ACL_GROUP, ACL_MASK, ACL_OTHER。分别代表文件主，文件其他用户，文件组主，文件其他组，文件掩码，
		其他人。ae_perm 代表权限，就是rwx。ae_id 除了 ACL_USER 和 ACL_GROUP 都是空 - Linux
		FreeBSD 中可能会有增加的类型
	*/
	int16_t		ae_tag;
	int16_t		ae_perm;
	int32_t		ae_id;
};


/*ext2 文件系统的简短的结构体，和posix标准的区别是没有了 ea_id 字段*/
struct ext2_acl_entry_short {
	int16_t		ae_tag;
	int16_t		ae_perm;
};

/*ext2的头部，仅仅有一个版本号*/
struct ext2_acl_header {
	int32_t		a_version;
};

void ext2_sync_acl_from_inode(struct inode *ip, struct acl *acl);

int	ext2_getacl(struct vop_getacl_args *);
int	ext2_setacl(struct vop_setacl_args *);
int	ext2_aclcheck(struct vop_aclcheck_args *);

#endif /* !_FS_EXT2FS_EXT2_ACL_H_ */
