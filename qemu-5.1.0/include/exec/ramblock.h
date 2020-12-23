/*
 * Declarations for cpu physical memory functions
 *
 * Copyright 2011 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Avi Kivity <avi@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 *
 */

/*
 * This header is for use by exec.c and memory.c ONLY.  Do not include it.
 * The functions declared here will be removed soon.
 */

#ifndef QEMU_EXEC_RAMBLOCK_H
#define QEMU_EXEC_RAMBLOCK_H

#ifndef CONFIG_USER_ONLY
#include "cpu-common.h"

/*
    RAMBlock: 表示一段拥有hva的地址空间，一个RAMBlock对应一个非纯容器的 MemoryRegion, 
    这样 AddressSpace, MemoryRegion, RAMBlock 就建立起了gpa->hva的关系

    HostMemoryBackend 对象中的内存被实际通过 qemu_ram_alloc() 函数（代码定义在exec.c中）映射到
    RAMBlock 数据结构中。每个 RAMBlock 都有一个指针指向被映射内存的起始位置，同时包含一个 ram_addr_t
    的位移变量。ram_addr_t 位于全局的命名空间中，因此 RAMBlock 能够通过 offset 来查找

    所有的RAMBlock保存在全局的RAMBlock的链表中，名为RAMList，它有专门的数据结构定义。RAMList数据结构定义
    在include/exec/ram_addr.h中，而全局的ram_list变量则定义在exec.c中。因此这个链表保存了客户机的内存对应
    的所有的物理机的实际内存信息
*/
struct RAMBlock {
    struct rcu_head rcu;    /* 表示该数据结构受到rcu机制的保护 */
    struct MemoryRegion *mr;
    uint8_t *host;  /* RAMBlock的内存起始位置 */
    uint8_t *colo_cache; /* For colo, VM's ram cache */
    ram_addr_t offset;  /* 在所有的RAMBlock中offset */
    ram_addr_t used_length; /* 已使用的长度 */
    ram_addr_t max_length;  /* 最大分配内存 */
    void (*resized)(const char*, uint64_t length, void *host);
    uint32_t flags;
    /* Protected by iothread lock.  RAMBlock的ID*/
    char idstr[256];
    /* RCU-enabled, writes protected by the ramlist lock */
    QLIST_ENTRY(RAMBlock) next;
    QLIST_HEAD(, RAMBlockNotifier) ramblock_notifiers;
    int fd; /* 被映射文件的描述符 */
    size_t page_size;
    /* dirty bitmap used during migration */
    unsigned long *bmap;
    /* bitmap of already received pages in postcopy */
    unsigned long *receivedmap;

    /*
     * bitmap to track already cleared dirty bitmap.  When the bit is
     * set, it means the corresponding memory chunk needs a log-clear.
     * Set this up to non-NULL to enable the capability to postpone
     * and split clearing of dirty bitmap on the remote node (e.g.,
     * KVM).  The bitmap will be set only when doing global sync.
     *
     * NOTE: this bitmap is different comparing to the other bitmaps
     * in that one bit can represent multiple guest pages (which is
     * decided by the `clear_bmap_shift' variable below).  On
     * destination side, this should always be NULL, and the variable
     * `clear_bmap_shift' is meaningless.
     */
    unsigned long *clear_bmap;
    uint8_t clear_bmap_shift;
};
#endif
#endif
