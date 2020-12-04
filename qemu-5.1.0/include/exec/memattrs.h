/*
 * Memory transaction attributes
 *
 * Copyright (c) 2015 Linaro Limited.
 *
 * Authors:
 *  Peter Maydell <peter.maydell@linaro.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef MEMATTRS_H
#define MEMATTRS_H

/* Every memory transaction has associated with it a set of
 * attributes. Some of these are generic (such as the ID of
 * the bus master); some are specific to a particular kind of
 * bus (such as the ARM Secure/NonSecure bit). We define them
 * all as non-overlapping bitfields in a single struct to avoid
 * confusion if different parts of QEMU used the same bit for
 * different semantics.
 * 每一个内存都会关联一些属性，其中一些是比较通用搞得，比如bus master id；一些是特殊的总线指定的
 * (ARM Secure/NonSecure bit)。所以我们它们全部定义为互不重叠的简单的结构体，防止QEMU不同的部
 * 分在不同的语义中使用相同的bit而出现混乱
 */
typedef struct MemTxAttrs {
    /* Bus masters which don't specify any attributes will get this
     * (via the MEMTXATTRS_UNSPECIFIED constant), so that we can
     * distinguish "all attributes deliberately clear" from
     * "didn't specify" if necessary.
     * 没有指定任何属性的总线主机将得到这个（通过MEMTXATTRS_UNSPECIFIED常量），这样我们就可以在
     * 必要时区分“所有有意清除的属性”和“没有指定的属性”
     */
    unsigned int unspecified:1;

    /* ARM/AMBA: TrustZone Secure access - 信任区安全访问
     * x86: System Management Mode access - 系统管理模式访问
     */
    unsigned int secure:1;

    /* Memory access is usermode (unprivileged) 用户模式下的内存访问(非特权模式) */
    unsigned int user:1;

    /* Requester ID (for MSI for example) 请求者ID(应该就是表示谁要访问内存区域) */
    unsigned int requester_id:16;

    /* Invert endianness for this page 反转此页的结尾？？ */
    unsigned int byte_swap:1;
    
    /*
     * The following are target-specific page-table bits.  These are not
     * related to actual memory transactions at all.  However, this structure
     * is part of the tlb_fill interface, cached in the cputlb structure,
     * and has unused bits.  These fields will be read by target-specific
     * helpers using env->iotlb[mmu_idx][tlb_index()].attrs.target_tlb_bitN.
     * 
     * 以下是特定于目标的页表位。这些与实际的内存事务完全无关。但是，这个结构是tlb_fill接口的一部分，
     * 缓存在cputlb结构中，并且有未使用的位，这些bits会被特定目标助手通过上述方式来读取
     */
    unsigned int target_tlb_bit0 : 1;
    unsigned int target_tlb_bit1 : 1;
    unsigned int target_tlb_bit2 : 1;
} MemTxAttrs;

/* Bus masters which don't specify any attributes will get this,
 * which has all attribute bits clear except the topmost one
 * (so that we can distinguish "all attributes deliberately clear"
 * from "didn't specify" if necessary).
 * 用于未指定任何属性的bus master，其实就是设置一下 unspecified 成员
 */
#define MEMTXATTRS_UNSPECIFIED ((MemTxAttrs) { .unspecified = 1 })

/* New-style MMIO accessors can indicate that the transaction failed.
 * A zero (MEMTX_OK) response means success; anything else is a failure
 * of some kind. The memory subsystem will bitwise-OR together results
 * if it is synthesizing an operation from multiple smaller accesses.
 * 
 * 新型MMIO访问器可以指示事务失败。零（MEMTX_OK）响应表示成功；其他任何响应都是某种失败；
 * 如果内存子系统从多个较小的访问合成一个操作，则它将按位或一起产生结果
 */
#define MEMTX_OK 0
#define MEMTX_ERROR             (1U << 0) /* device returned an error */
#define MEMTX_DECODE_ERROR      (1U << 1) /* nothing at that address */
typedef uint32_t MemTxResult;

#endif
