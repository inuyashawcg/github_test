/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2017 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 */

/* Portions Copyright 2010 Robert Milkowski */

#ifndef	_SYS_DMU_OBJSET_H
#define	_SYS_DMU_OBJSET_H

#include <sys/spa.h>
#include <sys/arc.h>
#include <sys/txg.h>
#include <sys/zfs_context.h>
#include <sys/dnode.h>
#include <sys/zio.h>
#include <sys/zil.h>
#include <sys/sa.h>
#include <sys/zfs_ioctl.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern krwlock_t os_lock;

struct dsl_pool;
struct dsl_dataset;
struct dmu_tx;

#define	OBJSET_PHYS_SIZE 2048
#define	OBJSET_OLD_PHYS_SIZE 1024

#define	OBJSET_BUF_HAS_USERUSED(buf) \
	(arc_buf_size(buf) > OBJSET_OLD_PHYS_SIZE)

#define	OBJSET_FLAG_USERACCOUNTING_COMPLETE	(1ULL<<0)

/*
	The DMU organizes objects into groups called object sets. 
	Object sets are used in ZFS to group related objects, 
	such as objects in a filesystem,snapshot, clone, or volume.
	Object sets are represented by a 1K byte objset_phys_t structure.

	整个数据结构的大小是 2048，旧版本是 1024，os_pad 的大小就是 2048 - 结构体中
	其他所有成员后剩余空间的大小。
*/
typedef struct objset_phys {
	dnode_phys_t os_meta_dnode;
	zil_header_t os_zil_header;	// zfs intent log
	/*
		The DMU supports several types of object sets, where each object
		set type has it's own well defined format/layout for its objects.
	*/
	uint64_t os_type;
	uint64_t os_flags;
	char os_pad[OBJSET_PHYS_SIZE - sizeof (dnode_phys_t)*3 -
	    sizeof (zil_header_t) - sizeof (uint64_t)*2];
	dnode_phys_t os_userused_dnode;
	dnode_phys_t os_groupused_dnode;
} objset_phys_t;

#define	OBJSET_PROP_UNINITIALIZED	((uint64_t)-1)
/*
	我们可以全局搜索一下 objset 的定义，只有这里会存在，别的地方都是引用自此处。参考 FreeBSD
	设计与实现英文版中相关内容，DSL 层级是 metaobject set，也就是说 dsl 处理的应该元数据对象。
	os_dsl_dataset 成员指向的应该就是该 objset 关联的元数据？
*/
struct objset {
	/* Immutable: */
	struct dsl_dataset *os_dsl_dataset;
	spa_t *os_spa;
	arc_buf_t *os_phys_buf;
	objset_phys_t *os_phys;
	/*
	 * The following "special" dnodes have no parent, are exempt
	 * from dnode_move(), and are not recorded in os_dnodes, but they
	 * root their descendents in this objset using handles anyway, so
	 * that all access to dnodes from dbufs consistently uses handles.
	 * 
	 * 以下“特殊”数据节点没有父节点，不受 dnode_move() 的约束，也不记录在os_dnodes 节点中，
	 * 但它们仍然使用句柄在该对象集中生成其子节点，因此所有从 dbufs 访问数据节点的权限都一致
	 * 使用句柄
	 */
	dnode_handle_t os_meta_dnode;
	dnode_handle_t os_userused_dnode;
	dnode_handle_t os_groupused_dnode;
	zilog_t *os_zil;

	list_node_t os_evicting_node;	// 链表 entry(其实就是 prev 和 next 指针)

	/* can change, under dsl_dir's locks: */
	uint64_t os_dnodesize; /* default dnode size for new objects */
	enum zio_checksum os_checksum;
	enum zio_compress os_compress;
	uint8_t os_copies;
	enum zio_checksum os_dedup_checksum;
	boolean_t os_dedup_verify;
	zfs_logbias_op_t os_logbias;
	zfs_cache_type_t os_primary_cache;
	zfs_cache_type_t os_secondary_cache;
	zfs_sync_type_t os_sync;
	zfs_redundant_metadata_type_t os_redundant_metadata;	// metadata 类型信息？
	int os_recordsize;
	/*
	 * The next four values are used as a cache of whatever's on disk, and
	 * are initialized the first time these properties are queried. Before
	 * being initialized with their real values, their values are
	 * OBJSET_PROP_UNINITIALIZED.
	 * 
	 * 接下来的四个值用作磁盘上任何内容的缓存，并在第一次查询这些属性时初始化。在用它们的实际值
	 * 初始化之前，它们的值是未初始化的 OBJSET_PROP_UNINITIALIZED
	 */
	uint64_t os_version;
	uint64_t os_normalization;
	uint64_t os_utf8only;
	uint64_t os_casesensitivity;

	/*
	 * Pointer is constant; the blkptr it points to is protected by
	 * os_dsl_dataset->ds_bp_rwlock
	 */
	blkptr_t *os_rootbp;

	/* no lock needed: */
	struct dmu_tx *os_synctx; /* XXX sketchy */
	zil_header_t os_zil_header;
	multilist_t *os_synced_dnodes;
	uint64_t os_flags;
	uint64_t os_freed_dnodes;
	boolean_t os_rescan_dnodes;

	/* Protected by os_obj_lock */
	kmutex_t os_obj_lock;
	uint64_t os_obj_next;

	/* Protected by os_lock */
	kmutex_t os_lock;
	multilist_t *os_dirty_dnodes[TXG_SIZE];
	list_t os_dnodes;
	list_t os_downgraded_dbufs;

	/* Protects changes to DMU_{USER,GROUP}USED_OBJECT */
	kmutex_t os_userused_lock;

	/* stuff we store for the user */
	kmutex_t os_user_ptr_lock;
	void *os_user_ptr;
	sa_os_t *os_sa;
};

#define	DMU_META_OBJSET		0
#define	DMU_META_DNODE_OBJECT	0
#define	DMU_OBJECT_IS_SPECIAL(obj) ((int64_t)(obj) <= 0)
#define	DMU_META_DNODE(os)	((os)->os_meta_dnode.dnh_dnode)
#define	DMU_USERUSED_DNODE(os)	((os)->os_userused_dnode.dnh_dnode)
#define	DMU_GROUPUSED_DNODE(os)	((os)->os_groupused_dnode.dnh_dnode)

#define	DMU_OS_IS_L2CACHEABLE(os)				\
	((os)->os_secondary_cache == ZFS_CACHE_ALL ||		\
	(os)->os_secondary_cache == ZFS_CACHE_METADATA)

#define	DMU_OS_IS_L2COMPRESSIBLE(os)	(zfs_mdcomp_disable == B_FALSE)

/* called from zpl */
int dmu_objset_hold(const char *name, void *tag, objset_t **osp);
int dmu_objset_own(const char *name, dmu_objset_type_t type,
    boolean_t readonly, void *tag, objset_t **osp);
int dmu_objset_own_obj(struct dsl_pool *dp, uint64_t obj,
    dmu_objset_type_t type, boolean_t readonly, void *tag, objset_t **osp);
void dmu_objset_refresh_ownership(struct dsl_dataset *ds,
    struct dsl_dataset **newds, void *tag);
void dmu_objset_rele(objset_t *os, void *tag);
void dmu_objset_disown(objset_t *os, void *tag);
int dmu_objset_from_ds(struct dsl_dataset *ds, objset_t **osp);

void dmu_objset_stats(objset_t *os, nvlist_t *nv);
void dmu_objset_fast_stat(objset_t *os, dmu_objset_stats_t *stat);
void dmu_objset_space(objset_t *os, uint64_t *refdbytesp, uint64_t *availbytesp,
    uint64_t *usedobjsp, uint64_t *availobjsp);
uint64_t dmu_objset_fsid_guid(objset_t *os);
int dmu_objset_find_dp(struct dsl_pool *dp, uint64_t ddobj,
    int func(struct dsl_pool *, struct dsl_dataset *, void *),
    void *arg, int flags);
int dmu_objset_prefetch(const char *name, void *arg);
void dmu_objset_evict_dbufs(objset_t *os);
timestruc_t dmu_objset_snap_cmtime(objset_t *os);

/* called from dsl */
void dmu_objset_sync(objset_t *os, zio_t *zio, dmu_tx_t *tx);
boolean_t dmu_objset_is_dirty(objset_t *os, uint64_t txg);
objset_t *dmu_objset_create_impl(spa_t *spa, struct dsl_dataset *ds,
    blkptr_t *bp, dmu_objset_type_t type, dmu_tx_t *tx);
int dmu_objset_open_impl(spa_t *spa, struct dsl_dataset *ds, blkptr_t *bp,
    objset_t **osp);
void dmu_objset_evict(objset_t *os);
void dmu_objset_do_userquota_updates(objset_t *os, dmu_tx_t *tx);
void dmu_objset_userquota_get_ids(dnode_t *dn, boolean_t before, dmu_tx_t *tx);
boolean_t dmu_objset_userused_enabled(objset_t *os);
int dmu_objset_userspace_upgrade(objset_t *os);
boolean_t dmu_objset_userspace_present(objset_t *os);
int dmu_fsname(const char *snapname, char *buf);

void dmu_objset_evict_done(objset_t *os);
void dmu_objset_willuse_space(objset_t *os, int64_t space, dmu_tx_t *tx);

void dmu_objset_init(void);
void dmu_objset_fini(void);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DMU_OBJSET_H */
