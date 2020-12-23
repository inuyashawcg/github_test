/*
 * Physical memory management API
 *
 * Copyright 2011 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Avi Kivity <avi@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef MEMORY_H
#define MEMORY_H

#ifndef CONFIG_USER_ONLY

#include "exec/cpu-common.h"
#include "exec/hwaddr.h"
#include "exec/memattrs.h"
#include "exec/memop.h"
#include "exec/ramlist.h"
#include "qemu/bswap.h"
#include "qemu/queue.h"
#include "qemu/int128.h"
#include "qemu/notify.h"
#include "qom/object.h"
#include "qemu/rcu.h"

#define RAM_ADDR_INVALID (~(ram_addr_t)0)

#define MAX_PHYS_ADDR_SPACE_BITS 62
#define MAX_PHYS_ADDR            (((hwaddr)1 << MAX_PHYS_ADDR_SPACE_BITS) - 1)

#define TYPE_MEMORY_REGION "qemu:memory-region"
#define MEMORY_REGION(obj) \
        OBJECT_CHECK(MemoryRegion, (obj), TYPE_MEMORY_REGION)

#define TYPE_IOMMU_MEMORY_REGION "qemu:iommu-memory-region"
#define IOMMU_MEMORY_REGION(obj) \
        OBJECT_CHECK(IOMMUMemoryRegion, (obj), TYPE_IOMMU_MEMORY_REGION)
#define IOMMU_MEMORY_REGION_CLASS(klass) \
        OBJECT_CLASS_CHECK(IOMMUMemoryRegionClass, (klass), \
                         TYPE_IOMMU_MEMORY_REGION)
#define IOMMU_MEMORY_REGION_GET_CLASS(obj) \
        OBJECT_GET_CLASS(IOMMUMemoryRegionClass, (obj), \
                         TYPE_IOMMU_MEMORY_REGION)

extern bool global_dirty_log;

typedef struct MemoryRegionOps MemoryRegionOps;

/*
    需要保留的region，应该就是常驻内存(有些内存中的资源可能要一直被使用，不能从内存页中换出，
    所以需要在内存中一直保留)
*/
struct ReservedRegion {
    hwaddr low;
    hwaddr high;
    unsigned type;
};

typedef struct IOMMUTLBEntry IOMMUTLBEntry;

/* See address_space_translate: bit 0 is read, bit 1 is write.  
    IOMMU 读写权限标识
*/
typedef enum {
    IOMMU_NONE = 0,
    IOMMU_RO   = 1,
    IOMMU_WO   = 2,
    IOMMU_RW   = 3,
} IOMMUAccessFlags;

#define IOMMU_ACCESS_FLAG(r, w) (((r) ? IOMMU_RO : 0) | ((w) ? IOMMU_WO : 0))

/*
    TLB：快表，实现虚拟地址和物理地址的快速转换
*/
struct IOMMUTLBEntry {
    AddressSpace    *target_as;
    hwaddr           iova;
    hwaddr           translated_addr;   /* 已经转换的地址 */
    hwaddr           addr_mask;  /* 0xfff = 4k translation */
    IOMMUAccessFlags perm;  
};

/*
 * Bitmap for different IOMMUNotifier capabilities. Each notifier can
 * register with one or multiple IOMMU Notifier capability bit(s).
 * 不同IOMMUNotifier功能的位图。每个通知程序可以注册一个或多个IOMMU通知程序功能位
 */
typedef enum {
    IOMMU_NOTIFIER_NONE = 0,

    /* Notify cache invalidations 通知缓存失效 */
    IOMMU_NOTIFIER_UNMAP = 0x1,

    /* Notify entry changes (newly created entries) 通知entry更改（新创建的entry） */
    IOMMU_NOTIFIER_MAP = 0x2,
} IOMMUNotifierFlag;

#define IOMMU_NOTIFIER_ALL (IOMMU_NOTIFIER_MAP | IOMMU_NOTIFIER_UNMAP)

struct IOMMUNotifier;
typedef void (*IOMMUNotify)(struct IOMMUNotifier *notifier,
                            IOMMUTLBEntry *data);
struct IOMMUNotifier {
    IOMMUNotify notify; /* 通知函数 */
    IOMMUNotifierFlag notifier_flags;   /* 通知标识 */
    /* Notify for address space range start <= addr <= end */
    hwaddr start;
    hwaddr end;
    int iommu_idx;
    QLIST_ENTRY(IOMMUNotifier) node;    /* notifrer也是通过队列来管理的 */
};
typedef struct IOMMUNotifier IOMMUNotifier;

/* 下面这些宏都是对RAM属性的设置 */
/* RAM is pre-allocated and passed into qemu_ram_alloc_from_ptr */
#define RAM_PREALLOC   (1 << 0)

/* RAM is mmap-ed with MAP_SHARED */
#define RAM_SHARED     (1 << 1)

/* Only a portion of RAM (used_length) is actually used, and migrated.
 * This used_length size can change across reboots.
 * 只有一部分RAM（已用长度）被实际使用和迁移。此已用长度大小可以在重新启动时更改
 */
#define RAM_RESIZEABLE (1 << 2)

/* UFFDIO_ZEROPAGE is available on this RAMBlock to atomically
 * zero the page and wake waiting processes.
 * (Set during postcopy)
 * UFFDIO_ZEROPAGE 在这个 RAMBlock 上可用，可以原子性地将页面和唤醒等待过程归零(在后期复制时设置)
 */ 
#define RAM_UF_ZEROPAGE (1 << 3)

/* RAM can be migrated RAM可以迁移 */
#define RAM_MIGRATABLE (1 << 4)

/* RAM is a persistent kind memory RAM是一种持久性的记忆 */
#define RAM_PMEM (1 << 5)

static inline void iommu_notifier_init(IOMMUNotifier *n, IOMMUNotify fn,
                                       IOMMUNotifierFlag flags,
                                       hwaddr start, hwaddr end,
                                       int iommu_idx)
{
    n->notify = fn;
    n->notifier_flags = flags;
    n->start = start;
    n->end = end;
    n->iommu_idx = iommu_idx;
}

/*
 * Memory region callbacks
 * 对内存区域的操作，好像也就是读写；其中包含了两个结构体，valid和impl，可能就是表示我能够获取到的region的
 * 大小是多少，然后我真正能够进行操作(impl)的region又是多少
 */
struct MemoryRegionOps {
    /* Read from the memory region. @addr is relative to @mr; @size is
     * in bytes. 从memory region 读数据 */
    uint64_t (*read)(void *opaque,
                     hwaddr addr,
                     unsigned size);
    /* Write to the memory region. @addr is relative to @mr; @size is
     * in bytes. 向memory region 写数据 */
    void (*write)(void *opaque,
                  hwaddr addr,
                  uint64_t data,
                  unsigned size);

    MemTxResult (*read_with_attrs)(void *opaque,
                                   hwaddr addr,
                                   uint64_t *data,
                                   unsigned size,
                                   MemTxAttrs attrs);
    MemTxResult (*write_with_attrs)(void *opaque,
                                    hwaddr addr,
                                    uint64_t data,
                                    unsigned size,
                                    MemTxAttrs attrs);

    enum device_endian endianness;  /* 大端&小端 */
    /* Guest-visible constraints: 用户可见的参数限制 */
    struct {
        /* If nonzero, specify bounds on access sizes beyond which a machine
         * check is thrown. 如果不为零，请指定访问大小的界限，超出该界限将引发计算机检查
         */
        unsigned min_access_size;
        unsigned max_access_size;
        /* If true, unaligned accesses are supported.  Otherwise unaligned
         * accesses throw machine checks. 如果为true，则支持未对齐的访问。否则，
         * 未对齐的访问将引发计算机检查
         */
         bool unaligned;
        /*
         * If present, and returns #false, the transaction is not accepted
         * by the device (and results in machine dependent behaviour such
         * as a machine check exception).
         * 如果存在并返回#false，则设备不接受事件（并导致机器相关行为，如机器检查异常）
         */
        bool (*accepts)(void *opaque, hwaddr addr,
                        unsigned size, bool is_write,
                        MemTxAttrs attrs);
    } valid;
    /* Internal implementation constraints: 内部实现的限制 */
    struct {
        /* If nonzero, specifies the minimum size implemented.  Smaller sizes
         * will be rounded upwards and a partial result will be returned.
         * 如果非零，则指定实现的最小size。较小的尺寸将向上取整，并返回部分结果
         */
        unsigned min_access_size;

        /* If nonzero, specifies the maximum size implemented.  Larger sizes
         * will be done as a series of accesses with smaller sizes.
         * 如果非零，则指定实现的最大size。较大的尺寸将作为一系列较小尺寸的通道来完成
         */
        unsigned max_access_size;

        /* If true, unaligned accesses are supported.  Otherwise all accesses
         * are converted to (possibly multiple) naturally aligned accesses.
         * 如果为true，则支持未对齐的访问。否则，所有访问将转换为（可能有多个）自然对齐的访问
         */
        bool unaligned;
    } impl;
};

typedef struct MemoryRegionClass {
    /* private */
    ObjectClass parent_class;   /* ObjectClass 的派生类 */
} MemoryRegionClass;

/* IOMMU属性 */
enum IOMMUMemoryRegionAttr {
    IOMMU_ATTR_SPAPR_TCE_FD
};

/**
 * IOMMUMemoryRegionClass:
 *
 * All IOMMU implementations need to subclass TYPE_IOMMU_MEMORY_REGION
 * and provide an implementation of at least the @translate method here
 * to handle requests to the memory region. Other methods are optional.
 * 所有 IOMMU 的实现都需要子类 TYPE_IOMMU_MEMORY_REGION，并且至少要提供 @translate 方法的实现来
 * 处理对于memory region的请求，其他方法则是可选择的
 *
 * The IOMMU implementation must use the IOMMU notifier infrastructure
 * to report whenever mappings are changed, by calling
 * memory_region_notify_iommu() (or, if necessary, by calling
 * memory_region_notify_one() for each registered notifier).
 * 无论映射什么时候发生改变，IOMMU都必须要使用 IOMMU notifier 去通知，通过调用上述函数
 *
 * Conceptually an IOMMU provides a mapping from input address
 * to an output TLB entry. If the IOMMU is aware of memory transaction
 * attributes and the output TLB entry depends on the transaction
 * attributes, we represent this using IOMMU indexes. Each index
 * selects a particular translation table that the IOMMU has:
 *   @attrs_to_index returns the IOMMU index for a set of transaction attributes
 *   @translate takes an input address and an IOMMU index
 * and the mapping returned can only depend on the input address and the
 * IOMMU index.
 * 从概念上来说，一个IOMMU会提供一个从输入地址到输出TLB entry(translation lookaside buffer: 虚拟地址
 * 到物理地址的快速转换)的映射。如果IOMMU知道内存事件属性，并且输出TLB entry依赖于事件属性，那么我们使用IOMMU
 * index来表示它。每个index都会选择IOMMU拥有的一个专用转换表：
 *  @attrs_to_index：返回一个事件属性集合的IOMMU index
 *  @translate：接受输入地址和IOMMU index，返回的映射只能依赖于输入地址和IOMMU索引
 *
 * Most IOMMUs don't care about the transaction attributes and support
 * only a single IOMMU index. A more complex IOMMU might have one index
 * for secure transactions and one for non-secure transactions.
 * 大多数IOMMU并不关心时间属性并且仅仅支持一个简单的IOMMU index。更复杂的IOMMU可能会有一个index用于安全事件，
 * 一个index用于非安全事件
 */
typedef struct IOMMUMemoryRegionClass {
    /* private */
    MemoryRegionClass parent_class; /* MemoryRegionClass 是它的父类 */

    /*
     * Return a TLB entry that contains a given address.
     *
     * The IOMMUAccessFlags indicated via @flag are optional and may
     * be specified as IOMMU_NONE to indicate that the caller needs
     * the full translation information for both reads and writes. If
     * the access flags are specified then the IOMMU implementation
     * may use this as an optimization, to stop doing a page table
     * walk as soon as it knows that the requested permissions are not
     * allowed. If IOMMU_NONE is passed then the IOMMU must do the
     * full page table walk and report the permissions in the returned
     * IOMMUTLBEntry. (Note that this implies that an IOMMU may not
     * return different mappings for reads and writes.)
     * 通过 @flag 所指定的 IOMMUAccessFlags 是可选的，可能会被指定为 IOMMU_NONE
     * 来表明调用者对于读写操作需要完整的转换信息。如果指定了访问标志，那么IOMMU实现
     * 可以将此用作优化，以便在知道请求的权限不被允许时立即停止执行页表遍历。如果传递
     * 了IOMMU_NONE，则IOMMU必须执行整页表遍历，并报告在返回的 IOMMUTLBEntry 中的权限。
     * 请注意，这意味着IOMMU可能不会为读和写返回不同的映射。
     *
     * The returned information remains valid while the caller is
     * holding the big QEMU lock or is inside an RCU critical section;
     * if the caller wishes to cache the mapping beyond that it must
     * register an IOMMU notifier so it can invalidate its cached
     * information when the IOMMU mapping changes.
     * 当调用者持有大的QEMU锁或在RCU临界区内时，返回的信息仍然有效；如果调用方希望
     * 缓存映射，那么它必须注册一个IOMMU通知程序，以便在IOMMU映射更改时使其缓存的
     * 信息失效
     *
     * @iommu: the IOMMUMemoryRegion
     * @hwaddr: address to be translated within the memory region
     * @flag: requested access permissions
     * @iommu_idx: IOMMU index for the translation
     */
    IOMMUTLBEntry (*translate)(IOMMUMemoryRegion *iommu, hwaddr addr,
                               IOMMUAccessFlags flag, int iommu_idx);
    
    /* Returns minimum supported page size in bytes.
     * If this method is not provided then the minimum is assumed to
     * be TARGET_PAGE_SIZE.
     * 返回支持的最小的page的大小(bytes)
     * 
     * @iommu: the IOMMUMemoryRegion
     */
    uint64_t (*get_min_page_size)(IOMMUMemoryRegion *iommu);

    /* Called when IOMMU Notifier flag changes (ie when the set of
     * events which IOMMU users are requesting notification for changes).
     * Optional method -- need not be provided if the IOMMU does not
     * need to know exactly which events must be notified.
     * 当IOMMU通知程序标志更改时调用（例如当IOMMU用户请求更改通知的事件集时）
     * 方法是可选择的，如果 IOMMU 并不是特别确切的知道那些事件需要被通知，那么就不需要提供
     *
     * @iommu: the IOMMUMemoryRegion
     * @old_flags: events which previously needed to be notified
     * @new_flags: events which now need to be notified
     *
     * Returns 0 on success, or a negative errno; in particular
     * returns -EINVAL if the new flag bitmap is not supported by the
     * IOMMU memory region. In case of failure, the error object
     * must be created
     * 成功时返回0，或返回负错误号；如果IOMMU内存区域不支持新标志位图，则返回-EINVAL；
     * 如果失败，必须创建错误对象
     */
    int (*notify_flag_changed)(IOMMUMemoryRegion *iommu,
                               IOMMUNotifierFlag old_flags,
                               IOMMUNotifierFlag new_flags,
                               Error **errp);

    /* Called to handle memory_region_iommu_replay().
     *
     * The default implementation of memory_region_iommu_replay() is to
     * call the IOMMU translate method for every page in the address space
     * with flag == IOMMU_NONE and then call the notifier if translate
     * returns a valid mapping. If this method is implemented then it
     * overrides the default behaviour, and must provide the full semantics
     * of memory_region_iommu_replay(), by calling @notifier for every
     * translation present in the IOMMU.
     * memory_region_iommu_replay() 的默认实现是去调用地址空间中所有 flag == IOMMU_NONE 的
     * 每一个page的 IOMMU 转换方法。如果返回一个可用的映射，那么就调用 notifier。如果这个方法已经
     * 被实现了，那么它将覆盖默认的方法实现，并且对于每一个在 IOMMU 中存在的转换，一定要通过调用 @notifier
     * 来提供 memory_region_iommu_replay() 的完整语义
     *
     * Optional method -- an IOMMU only needs to provide this method
     * if the default is inefficient or produces undesirable side effects.
     * IOMMU只需要在默认值无效或产生不需要的副作用时提供此方法
     *
     * Note: this is not related to record-and-replay functionality.
     * 这与记录-重播功能无关？？
     */
    void (*replay)(IOMMUMemoryRegion *iommu, IOMMUNotifier *notifier);

    /* Get IOMMU misc attributes. This is an optional method that
     * can be used to allow users of the IOMMU to get implementation-specific
     * information. The IOMMU implements this method to handle calls
     * by IOMMU users to memory_region_iommu_get_attr() by filling in
     * the arbitrary data pointer for any IOMMUMemoryRegionAttr values that
     * the IOMMU supports. If the method is unimplemented then
     * memory_region_iommu_get_attr() will always return -EINVAL.
     * 获取IOMMU其他属性。这是一种可选方法，可用于允许IOMMU的用户获取特定于实现的信息。
     * IOMMU实现此方法来处理IOMMU用户对memory_region_IOMMU_get_attr（）的调用，
     * 方法是为IOMMU支持的任何IomumMemoryRegionAttr值填充任意数据指针。如果该方法未实现，
     * 则memory_region_iommu_get_attr（）将始终返回-EINVAL
     *
     * @iommu: the IOMMUMemoryRegion
     * @attr: attribute being queried
     * @data: memory to fill in with the attribute data
     *
     * Returns 0 on success, or a negative errno; in particular
     * returns -EINVAL for unrecognized or unimplemented attribute types.
     */
    int (*get_attr)(IOMMUMemoryRegion *iommu, enum IOMMUMemoryRegionAttr attr,
                    void *data);

    /* Return the IOMMU index to use for a given set of transaction attributes.
     *
     * Optional method: if an IOMMU only supports a single IOMMU index then
     * the default implementation of memory_region_iommu_attrs_to_index()
     * will return 0.
     * 可选方法：如果一个IOMMU只支持一个IOMMU索引，则memory_region_IOMMU_attrs_to_index（）
     * 的默认实现将返回0
     *
     * The indexes supported by an IOMMU must be contiguous, starting at 0.
     * IOMMU 支持的 index 必须要是连续的，起始为0
     *
     * @iommu: the IOMMUMemoryRegion
     * @attrs: memory transaction attributes
     */
    int (*attrs_to_index)(IOMMUMemoryRegion *iommu, MemTxAttrs attrs);

    /* Return the number of IOMMU indexes this IOMMU supports.
     *
     * Optional method: if this method is not provided, then
     * memory_region_iommu_num_indexes() will return 1, indicating that
     * only a single IOMMU index is supported.
     * 如果这个方法没有被提供，那么 memory_region_iommu_num_indexes 函数将会返回1，
     * 表明仅仅一个简单的 IOMMU index 是被支持的
     * 
     * @iommu: the IOMMUMemoryRegion
     */
    int (*num_indexes)(IOMMUMemoryRegion *iommu);
} IOMMUMemoryRegionClass;

/* Coalesced: 联合的，合并的 */
typedef struct CoalescedMemoryRange CoalescedMemoryRange;
typedef struct MemoryRegionIoeventfd MemoryRegionIoeventfd;

/** MemoryRegion:
 *
 * A struct representing a memory region.
 * QEMU通过MemoryRegion来管理虚拟机内存，通过内存属性，GUEST物理地址等特点对内存分类，
 * 就形成了多个MemoryRegion，这些MemoryRegion 通过树状组织起来，挂接到根MemoryRegion
 * 下。每个MemoryRegion树代表了一类作用的内存，如系统内存空间(system_memory)或IO内存
 * 空间(system_io),这两个是qemu中的两个全局MemoryRegion
 * 
 * MemoryRegion 表示在 Guest memory layout 中的一段内存，可将 MemoryRegion 划分为以下三种类型：
 * 根级 MemoryRegion: 直接通过 memory_region_init 初始化，没有自己的内存，用于管理 subregion。如 system_memory
 * 
 * 实体 MemoryRegion: 通过 memory_region_init_ram 初始化，有自己的内存 (从 QEMU 进程地址空间中分配)，大小为 size；
 *      如 ram_memory(pc.ram) 、 pci_memory(pci) 等。 这种MemoryRegion中真正的分配物理内存，最主要的就是pc.ram和pci；
 *      分配的物理内存的作用分别是内存、PCI地址空间以及fireware空间。QEMU是用户空间代码，分配的物理内存返回的是hva，hva保存
 *      至RAMBlock的host域。通过实体MemoryRegion对应的RAMBlock可以管理HVA
 * 
 * 别名 MemoryRegion: 通过 memory_region_init_alias 初始化，没有自己的内存，表示实体 MemoryRegion(如 pc.ram) 的一部分，
 *      通过 alias 成员指向实体 MemoryRegion，alias_offset 代表了该别名MemoryRegion所代表内存起始GPA相对于实体 MemoryRegion 
 *      所代表内存起始GPA的偏移量。如 ram_below_4g 、ram_above_4g 等
 * 
 *      GVA - guest virtual addr
 *      GPA - guest physical addr 
 *      HVA - host virtual addr
 *      HPA - host physical addr
 * 
 *      GVA -> GPA 由客户机负责(运行在QEMU上的系统)
 *      GPA -> HVA 由QEMU负责
 *      HVA -> HPA 由宿主机操作系统负责
 */
struct MemoryRegion {
    Object parent_obj;

    /* private: */

    /* The following fields should fit in a cache line */
    bool romd_mode;
    bool ram;
    bool subpage;
    bool readonly; /* For RAM regions，标记是否为ROM类型的MR */
    bool nonvolatile;   /* 非易失的标识 */
    bool rom_device;    /* 判断是否为ROM device，是不是只读的内存 */
    bool flush_coalesced_mmio;  /* CoalescedMemoryRange 相关 */
    bool global_locking;    /* 全局锁 */
    uint8_t dirty_log_mask; /* 表示哪种dirty map被使用，共有三种 */
    bool is_iommu;
    RAMBlock *ram_block;    /* 关联的RAMBlock */
    Object *owner;  /* 表示哪个Object实体拥有这个memory region？？ */
    /*
        - callback，与MemoryRegion相关的操作
        - 是否为MMIO类型的MR
    */
    const MemoryRegionOps *ops; 
    void *opaque;
    /*
        - 指向包含此MR的容器MR
        - 指向父级 MemoryRegion
    */
    MemoryRegion *container;    
    Int128 size;    /* 虚拟机内存的物理地址大小 */
    /* 
        - 在父级 MemoryRegion 中的偏移量 
        - 虚拟机内存的绝对物理地址
        - 在AddressSpace中的地址
    */
    hwaddr addr;    
    void (*destructor)(MemoryRegion *mr);   /* 析构函数 */
    uint64_t align;
    bool terminates;
    bool ram_device;
    bool enabled;   /* 如果为true，表示已经通知kvm使用这段内存 */
    bool warning_printed; /* For reservations - 保留项？？ */
    uint8_t vga_logging_count;
    MemoryRegion *alias;    /* alias: 别名，指向实体MemoryRegion */

    /*
         - 起始地址 (GPA) 在实体 MemoryRegion 中的偏移量
         - 别名MR在所属MR内的offset，alias_offset表示本MR在alias指向的MR的偏移
    */
    hwaddr alias_offset;    
    int32_t priority;
    QTAILQ_HEAD(, MemoryRegion) subregions; /* 容器MR的子MR组成的链表header */
    QTAILQ_ENTRY(MemoryRegion) subregions_link; /* 用于将子MR组织成链表的成员 */
    QTAILQ_HEAD(, CoalescedMemoryRange) coalesced;  /* 联合的memory range */
    const char *name;   /* MemoryRegion的名字,调试时使用 */
    /* 
        - IOevent文件描述符的管理
        - MR包含的ioeventfd个数 ??
    */
    unsigned ioeventfd_nb; 
    MemoryRegionIoeventfd *ioeventfds;  /* MR包含的ioeventfd数组，用于和内核通信 */
};

/*
    推测可能是 IOMMU region 对某些操作的响应，我们可以注册一些响应的通知函数？？
*/
struct IOMMUMemoryRegion {
    MemoryRegion parent_obj;

    QLIST_HEAD(, IOMMUNotifier) iommu_notify;
    IOMMUNotifierFlag iommu_notify_flags;
};

#define IOMMU_NOTIFIER_FOREACH(n, mr) \
    QLIST_FOREACH((n), &(mr)->iommu_notify, node)

/**
 * MemoryListener: callbacks structure for updates to the physical memory map
 * 当物理内存映射更新的时候，调用回调函数
 *
 * Allows a component to adjust to changes in the guest-visible memory map.
 * Use with memory_listener_register() and memory_listener_unregister().
 * 
 * 当qemu模拟的内存地址空间发生变化时，需要有机制通知到其它模块，对于kvm的模拟，内存地址空间变化后
 * 要通知内核，从而保证内核与用户态内存信息一致。对于tcg的模拟，虽然不通知到内核，但在内存地址信息
 * 变化时也有其它事情需要做，MemoryListener结构体就是为此而设计，每个地址空间都维护了这样一个结构
 * 体的链表，当内存信息变化时，会触发相关的回调。同时还有一个全局的链表维护所有注册的Listener结构体，
 * 这些结构体在链表内通过优先级排序
 */
struct MemoryListener {
    /**
     * @begin:
     *
     * Called at the beginning of an address space update transaction.
     * 在地址空间更新转换开始的时候调用
     * 
     * Followed by calls to #MemoryListener.region_add(),
     * #MemoryListener.region_del(), #MemoryListener.region_nop(),
     * #MemoryListener.log_start() and #MemoryListener.log_stop() in
     * increasing address order.
     *
     * @listener: The #MemoryListener.
     */
    void (*begin)(MemoryListener *listener);

    /**
     * @commit:
     *
     * Called at the end of an address space update transaction,
     * 在地址空间更新转换结束的时候调用
     * 
     * after the last call to #MemoryListener.region_add(),
     * #MemoryListener.region_del() or #MemoryListener.region_nop(),
     * #MemoryListener.log_start() and #MemoryListener.log_stop().
     *
     * @listener: The #MemoryListener.
     */
    void (*commit)(MemoryListener *listener);

    /**
     * @region_add:
     *
     * Called during an address space update transaction,
     * for a section of the address space that is new in this address space
     * space since the last transaction.
     * 对于自上一个事务以来在此地址空间中新出现的地址空间部分
     * 
     * @listener: The #MemoryListener.
     * @section: The new #MemoryRegionSection.
     */
    void (*region_add)(MemoryListener *listener, MemoryRegionSection *section);

    /**
     * @region_del:
     *
     * Called during an address space update transaction,
     * for a section of the address space that has disappeared in the address
     * space since the last transaction.
     * 对于自上次事务后在地址空间中消失的地址空间部分
     *
     * @listener: The #MemoryListener.
     * @section: The old #MemoryRegionSection.
     */
    void (*region_del)(MemoryListener *listener, MemoryRegionSection *section);

    /**
     * @region_nop:
     *
     * Called during an address space update transaction,
     * for a section of the address space that is in the same place in the address
     * space as in the last transaction.
     * 地址空间中与上一个事务中位于同一位置的地址空间部分
     *
     * @listener: The #MemoryListener.
     * @section: The #MemoryRegionSection.
     */
    void (*region_nop)(MemoryListener *listener, MemoryRegionSection *section);

    /**
     * @log_start:
     *
     * Called during an address space update transaction, after
     * one of #MemoryListener.region_add(),#MemoryListener.region_del() or
     * #MemoryListener.region_nop(), if dirty memory logging clients have
     * become active since the last transaction.
     *
     * @listener: The #MemoryListener.
     * @section: The #MemoryRegionSection.
     * @old: A bitmap of dirty memory logging clients that were active in
     * the previous transaction. 在上一个事务中处于活动状态的脏内存日志记录客户端的位图
     * @new: A bitmap of dirty memory logging clients that are active in
     * the current transaction.
     */
    void (*log_start)(MemoryListener *listener, MemoryRegionSection *section,
                      int old, int new);

    /**
     * @log_stop:
     *
     * Called during an address space update transaction, after
     * one of #MemoryListener.region_add(), #MemoryListener.region_del() or
     * #MemoryListener.region_nop() and possibly after
     * #MemoryListener.log_start(), if dirty memory logging clients have
     * become inactive since the last transaction.
     *
     * @listener: The #MemoryListener.
     * @section: The #MemoryRegionSection.
     * @old: A bitmap of dirty memory logging clients that were active in
     * the previous transaction.
     * @new: A bitmap of dirty memory logging clients that are active in
     * the current transaction.
     */
    void (*log_stop)(MemoryListener *listener, MemoryRegionSection *section,
                     int old, int new);

    /**
     * @log_sync:
     *
     * Called by memory_region_snapshot_and_clear_dirty() and
     * memory_global_dirty_log_sync(), before accessing QEMU's "official"
     * copy of the dirty memory bitmap for a #MemoryRegionSection.
     *
     * @listener: The #MemoryListener.
     * @section: The #MemoryRegionSection.
     */
    void (*log_sync)(MemoryListener *listener, MemoryRegionSection *section);

    /**
     * @log_clear:
     *
     * Called before reading the dirty memory bitmap for a
     * #MemoryRegionSection.
     *
     * @listener: The #MemoryListener.
     * @section: The #MemoryRegionSection.
     */
    void (*log_clear)(MemoryListener *listener, MemoryRegionSection *section);

    /**
     * @log_global_start:
     *
     * Called by memory_global_dirty_log_start(), which
     * enables the %DIRTY_LOG_MIGRATION client on all memory regions in
     * the address space.  #MemoryListener.log_global_start() is also
     * called when a #MemoryListener is added, if global dirty logging is
     * active at that time.
     * 使能地址空间中所有内存区域里的 %DIRTY_LOG_MIGRATION client
     *
     * @listener: The #MemoryListener.
     */
    void (*log_global_start)(MemoryListener *listener);

    /**
     * @log_global_stop:
     *
     * Called by memory_global_dirty_log_stop(), which
     * disables the %DIRTY_LOG_MIGRATION client on all memory regions in
     * the address space.
     *
     * @listener: The #MemoryListener.
     */
    void (*log_global_stop)(MemoryListener *listener);

    /**
     * @log_global_after_sync:
     *
     * Called after reading the dirty memory bitmap
     * for any #MemoryRegionSection.
     *
     * @listener: The #MemoryListener.
     */
    void (*log_global_after_sync)(MemoryListener *listener);

    /**
     * @eventfd_add:
     *
     * Called during an address space update transaction,
     * for a section of the address space that has had a new ioeventfd
     * registration since the last transaction.
     * 在地址空间更新事务期间调用，用于自上一个事务以来已进行新ioeventfd注册的地址空间部分
     *
     * @listener: The #MemoryListener.
     * @section: The new #MemoryRegionSection.
     * @match_data: The @match_data parameter for the new ioeventfd.
     * @data: The @data parameter for the new ioeventfd.
     * @e: The #EventNotifier parameter for the new ioeventfd.
     */
    void (*eventfd_add)(MemoryListener *listener, MemoryRegionSection *section,
                        bool match_data, uint64_t data, EventNotifier *e);

    /**
     * @eventfd_del:
     *
     * Called during an address space update transaction,
     * for a section of the address space that has dropped an ioeventfd
     * registration since the last transaction.
     *
     * @listener: The #MemoryListener.
     * @section: The new #MemoryRegionSection.
     * @match_data: The @match_data parameter for the dropped ioeventfd.
     * @data: The @data parameter for the dropped ioeventfd.
     * @e: The #EventNotifier parameter for the dropped ioeventfd.
     */
    void (*eventfd_del)(MemoryListener *listener, MemoryRegionSection *section,
                        bool match_data, uint64_t data, EventNotifier *e);

    /**
     * @coalesced_io_add:
     *
     * Called during an address space update transaction,
     * for a section of the address space that has had a new coalesced
     * MMIO range registration since the last transaction.
     * 在地址空间更新事务期间调用，用于自上一个事务以来具有新的合并MMIO范围注册的地址空间部分
     *
     * @listener: The #MemoryListener.
     * @section: The new #MemoryRegionSection.
     * @addr: The starting address for the coalesced MMIO range.
     * @len: The length of the coalesced MMIO range.
     */
    void (*coalesced_io_add)(MemoryListener *listener, MemoryRegionSection *section,
                               hwaddr addr, hwaddr len);

    /**
     * @coalesced_io_del:
     *
     * Called during an address space update transaction,
     * for a section of the address space that has dropped a coalesced
     * MMIO range since the last transaction.
     *
     * @listener: The #MemoryListener.
     * @section: The new #MemoryRegionSection.
     * @addr: The starting address for the coalesced MMIO range.
     * @len: The length of the coalesced MMIO range.
     */
    void (*coalesced_io_del)(MemoryListener *listener, MemoryRegionSection *section,
                               hwaddr addr, hwaddr len);
    /**
     * @priority:
     *
     * Govern the order in which memory listeners are invoked. Lower priorities
     * are invoked earlier for "add" or "start" callbacks, and later for "delete"
     * or "stop" callbacks.
     * 控制调用内存侦听器的顺序。较低的优先级在“添加”或“开始”回调时被调用，稍后对于“删除”或“停止”回调调用
     */
    unsigned priority;

    /* private: */
    AddressSpace *address_space;
    QTAILQ_ENTRY(MemoryListener) link;
    QTAILQ_ENTRY(MemoryListener) link_as;
};

/**
 * AddressSpace: describes a mapping of addresses to #MemoryRegion objects
 * 描述对 #MemoryRegion 类型 object 的一个地址映射
 * 
 * AddressSpace是cpu可以看到的地址空间,一般就是cpu地址总线宽度的可寻址范围
 * 
 * 所有的CPU架构都有内存地址空间，有的CPU架构还会有一个IO地址空间，它们在QEMU中被表示为 AddressSpace 数据结构；
 * 而每个地址空间都会包含有一个 MemoryRegion 的树状结构，之所以称之为树状结构，是因为每个 MemoryRegion 的内部
 * 可以包含有 MemoryRegion，这样就形成了树状结构。所以它的成员变量 root 也就是树状结构的根节点，可以通过它获取到
 * 整个树的内存信息
 */
struct AddressSpace {
    /* private: */
    struct rcu_head rcu;    /* RCU: 数据同步 */
    char *name;
    /*
        关联的根MR，地址空间拥有了它，就拥有了整棵MR树的内存信息，结构体初始化时这一字段作为输入
    */
    MemoryRegion *root; 

    /* Accessed via RCU. 通过RCU进行数据访问 
        Root MR对应的扁平化内存视图
        AddressSpace的一张平面视图，它是AddressSpace所有正在使用的MemoryRegion的集合，这是从CPU的视角来看到的
    */
    struct FlatView *current_map;

    int ioeventfd_nb;   /* nb: number？表示IO event数量？ */
    struct MemoryRegionIoeventfd *ioeventfds;
    QTAILQ_HEAD(, MemoryListener) listeners;    /* 用于当地址空间发生变化时通知qemu其它模块或者内核 */
    QTAILQ_ENTRY(AddressSpace) address_spaces_link; /* 通过队列管理 */
};

typedef struct AddressSpaceDispatch AddressSpaceDispatch;
typedef struct FlatRange FlatRange;

/* Flattened global view of current active memory hierarchy.  Kept in sorted
 * order.
 * 当前活动内存层次结构的展平全局视图。保持有序
 * 这个可能就是把整个memory区域类比成了一个区块图形(帮助开发者理解？？)，它对应的应该就是 FlatRange;
 * 
 * 扁平化视图同样有两个元素，一是FlatView，cpu可访问地址空间的扁平化表示；一是FlatRange，逻辑内存区域
 * 的扁平化描述，表示一段段内存区域。同样地，FlatView由许多FlatRange组成。扁平视图，用于展示和导出内存
 * 视图，也方便传递给KVM
 * 
 * 将树状的MemoryRegion展成平坦型的FlatView，用于内存映射；意思可能就是我们对于MemoryRegion的处理是以树状的
 * 结构来进行组织的，而内存按照我们平常的理解，是一个长条状结构，FlatView的用意应该就是把树状结构跟条状结构对应
 * 起来，把树状结构上的每一个结点映射到内存上的一块区域
 * 
 * MemoryRegion是联系客户机内存和包含这一部分内存的RAMBlock。每个MemoryRegion都包含一个在RAMBlock中ram_addr_t
 * 类型的offset，每个RAMBlock也有一个MemoryRegion的指针
 * 
 * MemoryRegion不仅可以表示RAM，也可以表示I/O映射内存，在访问时可以调用read/write回调函数。这也是硬件从客户机CPU
 * 注册的访问被分派到相应的模拟设备的方法
 */
struct FlatView {
    struct rcu_head rcu;    /* rcu队列，每个结点都会对应一个function */
    unsigned ref;
    /* 
        FlatRange数组，一个flat view对应多个FlatRange？
    */
    FlatRange *ranges;  
    unsigned nr;
    unsigned nr_allocated;
    struct AddressSpaceDispatch *dispatch;  /* diapatch: 调度 */
    /*
        跟FlatRange中的memory region的关系是什么？
    */
    MemoryRegion *root; 
};

static inline FlatView *address_space_to_flatview(AddressSpace *as)
{
    /* 原子操作 */
    return atomic_rcu_read(&as->current_map);
}


/**
 * MemoryRegionSection: describes a fragment of a #MemoryRegion
 * 该结构体描述的应该是某个memory region中的一段(section),类比ELF文件中section定义
 *
 * @mr: the region, or %NULL if empty
 * @fv: the flat view of the address space the region is mapped in
 * @offset_within_region: the beginning of the section, relative to @mr's start
 * @size: the size of the section; will not exceed @mr's boundaries
 * @offset_within_address_space: the address of the first byte of the section
 *     relative to the region's address space
 * @readonly: writes to this section are ignored
 * @nonvolatile: this section is non-volatile
 */
struct MemoryRegionSection {
    Int128 size;
    MemoryRegion *mr;
    FlatView *fv;   /* memory region映射的地址空间的flat view */
    hwaddr offset_within_region;    /* 相对于mr起始位置的offset */
    hwaddr offset_within_address_space;
    bool readonly;
    bool nonvolatile;   /* 非易失性的 */
};

/* 类似与C++中类赋值操作 */
static inline bool MemoryRegionSection_eq(MemoryRegionSection *a,
                                          MemoryRegionSection *b)
{
    return a->mr == b->mr &&
           a->fv == b->fv &&
           a->offset_within_region == b->offset_within_region &&
           a->offset_within_address_space == b->offset_within_address_space &&
           int128_eq(a->size, b->size) &&
           a->readonly == b->readonly &&
           a->nonvolatile == b->nonvolatile;
}

/**
 * memory_region_init: Initialize a memory region
 *
 * The region typically acts as a container for other memory regions.  Use
 * memory_region_add_subregion() to add subregions.
 * 一个region可以作为其他region的拥有者，通过调用 memory_region_add_subregion 函数来添加子区域
 * 根级memory region通过这个函数初始化，没有自己的内存，只是用于管理，比如 system memory
 *
 * @mr: the #MemoryRegion to be initialized
 * @owner: the object that tracks the region's reference count 跟踪region引用计数变化的object
 * @name: used for debugging; not visible to the user or ABI
 * @size: size of the region; any subregions beyond this size will be clipped
 *          任何超出此大小的分区域都将被剪裁
 */
void memory_region_init(MemoryRegion *mr,
                        struct Object *owner,
                        const char *name,
                        uint64_t size);

/**
 * memory_region_ref: Add 1 to a memory region's reference count
 *
 * Whenever memory regions are accessed outside the BQL, they need to be
 * preserved against hot-unplug.  MemoryRegions actually do not have their
 * own reference count; they piggyback on a QOM object, their "owner".
 * This function adds a reference to the owner.
 * 无论何时在BQL外部访问内存区域，都需要保存它们以防热插拔。 MemoryRegions 实际上没有
 * 自己的引用计数；它们依赖于QOM对象，即它们的“所有者”。此函数用于添加对所有者的引用
 *
 * All MemoryRegions must have an owner if they can disappear, even if the
 * device they belong to operates exclusively under the BQL.  This is because
 * the region could be returned at any time by memory_region_find, and this
 * is usually under guest control.
 * 所有的内存区域都必须有一个所有者，如果它们可以消失，即使它们所属的设备在BQL下专门运行。
 * 这是因为该区域可以在任何时候由memory_region_find返回，并且通常由guest控制。
 *
 * @mr: the #MemoryRegion
 */
void memory_region_ref(MemoryRegion *mr);

/**
 * memory_region_unref: Remove 1 to a memory region's reference count
 * BQL：锁操作相关
 * Whenever memory regions are accessed outside the BQL, they need to be
 * preserved against hot-unplug.  MemoryRegions actually do not have their
 * own reference count; they piggyback on a QOM object, their "owner".
 * This function removes a reference to the owner and possibly destroys it.
 * 此函数删除对所有者的引用，并可能销毁它
 *
 * @mr: the #MemoryRegion
 */
void memory_region_unref(MemoryRegion *mr);

/**
 * memory_region_init_io: Initialize an I/O memory region.
 *
 * Accesses into the region will cause the callbacks in @ops to be called.
 * if @size is nonzero, subregions will be clipped to @size.
 * 对区域的访问将导致调用 @ops 中的回调。如果 @size 非零，subregion 将被剪裁为 @size。
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @ops: a structure containing read and write callbacks to be used when
 *       I/O is performed on the region.
 * @opaque: passed to the read and write callbacks of the @ops structure.
 *          传递给@ops结构的读写回调
 * 
 * @name: used for debugging; not visible to the user or ABI
 * @size: size of the region.
 */
void memory_region_init_io(MemoryRegion *mr,
                           struct Object *owner,
                           const MemoryRegionOps *ops,
                           void *opaque,
                           const char *name,
                           uint64_t size);

/**
 * memory_region_init_ram_nomigrate:  Initialize RAM memory region.  Accesses
 *                                    into the region will modify memory
 *                                    directly.
 * 初始化RAM内存区域。对该区域的访问将直接修改内存
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: Region name, becomes part of RAMBlock name used in migration stream
 *        must be unique within any device
 *        区域名称，成为RAMBlock名称的一部分，在迁移流中使用的名称在任何设备中都必须是唯一的
 * 
 * @size: size of the region.
 * @errp: pointer to Error*, to store an error if it happens.
 *
 * Note that this function does not do anything to cause the data in the
 * RAM memory region to be migrated; that is the responsibility of the caller.
 * 请注意，此函数不会导致RAM内存区域中的数据被迁移；这是调用者的责任
 */
void memory_region_init_ram_nomigrate(MemoryRegion *mr,
                                      struct Object *owner,
                                      const char *name,
                                      uint64_t size,
                                      Error **errp);

/**
 * memory_region_init_ram_shared_nomigrate:  Initialize RAM memory region.
 *                                           Accesses into the region will
 *                                           modify memory directly.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: Region name, becomes part of RAMBlock name used in migration stream
 *        must be unique within any device
 * @size: size of the region.
 * @share: allow remapping RAM to different addresses 允许映射到不同的地址
 * @errp: pointer to Error*, to store an error if it happens.
 *
 * Note that this function is similar to memory_region_init_ram_nomigrate.
 * The only difference is part of the RAM region can be remapped.
 * 跟上面的区域的区别就是这里的部分memory可以remap
 */
void memory_region_init_ram_shared_nomigrate(MemoryRegion *mr,
                                             struct Object *owner,
                                             const char *name,
                                             uint64_t size,
                                             bool share,
                                             Error **errp);

/**
 * memory_region_init_resizeable_ram:  Initialize memory region with resizeable
 *                                     RAM.  Accesses into the region will
 *                                     modify memory directly.  Only an initial
 *                                     portion of this RAM is actually used.
 *                                     The used size can change across reboots.
 *
 * 使用可调整内存大小初始化内存区域。对该区域的访问将直接修改内存。实际上只使用了这个RAM的初始部分；
 * 使用的大小可以在重新启动时更改
 * 
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: Region name, becomes part of RAMBlock name used in migration stream
 *        must be unique within any device
 * @size: used size of the region.
 * @max_size: max size of the region.
 * @resized: callback to notify owner about used size change.
 * @errp: pointer to Error*, to store an error if it happens.
 *
 * Note that this function does not do anything to cause the data in the
 * RAM memory region to be migrated; that is the responsibility of the caller.
 */
void memory_region_init_resizeable_ram(MemoryRegion *mr,
                                       struct Object *owner,
                                       const char *name,
                                       uint64_t size,
                                       uint64_t max_size,
                                       void (*resized)(const char*,
                                                       uint64_t length,
                                                       void *host),
                                       Error **errp);
#ifdef CONFIG_POSIX

/**
 * memory_region_init_ram_from_file:  Initialize RAM memory region with a
 *                                    mmap-ed backend.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: Region name, becomes part of RAMBlock name used in migration stream
 *        must be unique within any device
 * @size: size of the region.
 * @align: alignment of the region base address; if 0, the default alignment
 *         (getpagesize()) will be used.
 * @ram_flags: Memory region features:
 *             - RAM_SHARED: memory must be mmaped with the MAP_SHARED flag
 *             - RAM_PMEM: the memory is persistent memory
 *             Other bits are ignored now.
 * @path: the path in which to allocate the RAM.
 * @errp: pointer to Error*, to store an error if it happens.
 *
 * Note that this function does not do anything to cause the data in the
 * RAM memory region to be migrated; that is the responsibility of the caller.
 */
void memory_region_init_ram_from_file(MemoryRegion *mr,
                                      struct Object *owner,
                                      const char *name,
                                      uint64_t size,
                                      uint64_t align,
                                      uint32_t ram_flags,
                                      const char *path,
                                      Error **errp);

/**
 * memory_region_init_ram_from_fd:  Initialize RAM memory region with a
 *                                  mmap-ed backend.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: the name of the region.
 * @size: size of the region.
 * @share: %true if memory must be mmaped with the MAP_SHARED flag
 * @fd: the fd to mmap.
 * @errp: pointer to Error*, to store an error if it happens.
 *
 * Note that this function does not do anything to cause the data in the
 * RAM memory region to be migrated; that is the responsibility of the caller.
 */
void memory_region_init_ram_from_fd(MemoryRegion *mr,
                                    struct Object *owner,
                                    const char *name,
                                    uint64_t size,
                                    bool share,
                                    int fd,
                                    Error **errp);
#endif

/**
 * memory_region_init_ram_ptr:  Initialize RAM memory region from a
 *                              user-provided pointer.  Accesses into the
 *                              region will modify memory directly.
 * 从用户提供的指针初始化RAM内存区域
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: Region name, becomes part of RAMBlock name used in migration stream
 *        must be unique within any device
 * @size: size of the region.
 * @ptr: memory to be mapped; must contain at least @size bytes.
 *      表示需要被映射的区域，必须至少包含 @size byte
 *
 * Note that this function does not do anything to cause the data in the
 * RAM memory region to be migrated; that is the responsibility of the caller.
 */
void memory_region_init_ram_ptr(MemoryRegion *mr,
                                struct Object *owner,
                                const char *name,
                                uint64_t size,
                                void *ptr);

/**
 * memory_region_init_ram_device_ptr:  Initialize RAM device memory region from
 *                                     a user-provided pointer.
 *
 * A RAM device represents a mapping to a physical device, such as to a PCI
 * MMIO BAR of an vfio-pci assigned device.  The memory region may be mapped
 * into the VM address space and access to the region will modify memory
 * directly.  However, the memory region should not be included in a memory
 * dump (device may not be enabled/mapped at the time of the dump), and
 * operations incompatible with manipulating MMIO should be avoided.  Replaces
 * skip_dump flag.
 * RAM设备表示到物理设备的映射，例如vfio-PCI分配设备的PCI MMIO条。内存区域可以映射到虚拟机
 * 地址空间，对该区域的访问将直接修改内存。但是，内存区域不应该包括在内存转储中（设备在转储时
 * 可能没有被启用/映射），并且应该避免与操作MMIO不兼容的操作。替换 skip_dump 标志
 * 
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: the name of the region.
 * @size: size of the region.
 * @ptr: memory to be mapped; must contain at least @size bytes.
 *
 * Note that this function does not do anything to cause the data in the
 * RAM memory region to be migrated; that is the responsibility of the caller.
 * (For RAM device memory regions, migrating the contents rarely makes sense.)
 * 对于RAM设备内存区域，迁移内容很少有意义
 */
void memory_region_init_ram_device_ptr(MemoryRegion *mr,
                                       struct Object *owner,
                                       const char *name,
                                       uint64_t size,
                                       void *ptr);

/**
 * memory_region_init_alias: Initialize a memory region that aliases all or a
 *                           part of another memory region.
 * 初始化一个与另一个内存区域的全部或部分别名相同的内存区域
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: used for debugging; not visible to the user or ABI
 * @orig: the region to be referenced; @mr will be equivalent to
 *        @orig between @offset and @offset + @size - 1.
 * @offset: start of the section in @orig to be referenced.
 * @size: size of the region.
 */
void memory_region_init_alias(MemoryRegion *mr,
                              struct Object *owner,
                              const char *name,
                              MemoryRegion *orig,
                              hwaddr offset,
                              uint64_t size);

/**
 * memory_region_init_rom_nomigrate: Initialize a ROM memory region.
 *
 * This has the same effect as calling memory_region_init_ram_nomigrate()
 * and then marking the resulting region read-only with
 * memory_region_set_readonly().
 *
 * Note that this function does not do anything to cause the data in the
 * RAM side of the memory region to be migrated; that is the responsibility
 * of the caller.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: Region name, becomes part of RAMBlock name used in migration stream
 *        must be unique within any device
 * @size: size of the region.
 * @errp: pointer to Error*, to store an error if it happens.
 */
void memory_region_init_rom_nomigrate(MemoryRegion *mr,
                                      struct Object *owner,
                                      const char *name,
                                      uint64_t size,
                                      Error **errp);

/**
 * memory_region_init_rom_device_nomigrate:  Initialize a ROM memory region.
 *                                 Writes are handled via callbacks.
 *
 * Note that this function does not do anything to cause the data in the
 * RAM side of the memory region to be migrated; that is the responsibility
 * of the caller.
 * 注意，这个函数不会导致内存区域的RAM端的数据被迁移；这是调用者的责任
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @ops: callbacks for write access handling (must not be NULL).
 * @opaque: passed to the read and write callbacks of the @ops structure.
 * @name: Region name, becomes part of RAMBlock name used in migration stream
 *        must be unique within any device
 * @size: size of the region.
 * @errp: pointer to Error*, to store an error if it happens.
 */
void memory_region_init_rom_device_nomigrate(MemoryRegion *mr,
                                             struct Object *owner,
                                             const MemoryRegionOps *ops,
                                             void *opaque,
                                             const char *name,
                                             uint64_t size,
                                             Error **errp);

/**
 * memory_region_init_iommu: Initialize a memory region of a custom type
 * that translates addresses
 *
 * An IOMMU region translates addresses and forwards accesses to a target
 * memory region. IOMMU区域转换地址并将访问转发到目标内存区域
 *
 * The IOMMU implementation must define a subclass of TYPE_IOMMU_MEMORY_REGION.
 * @_iommu_mr should be a pointer to enough memory for an instance of
 * that subclass, @instance_size is the size of that subclass, and
 * @mrtypename is its name. This function will initialize @_iommu_mr as an
 * instance of the subclass, and its methods will then be called to handle
 * accesses to the memory region. See the documentation of
 * #IOMMUMemoryRegionClass for further details.
 * IOMMU的实现必须要定义一个 TYPE_IOMMU_MEMORY_REGION subclass，@_iommu_mr 应该是一个
 * 指向有足够区域存放 subclass 实例的指针 @instance_size 表示实例的大小，@mrtypename 表示
 * 它的名字。这个函数将初始化 @_iommu_mr 作为 subclass 的实例，它的方法将会在处理对内存区域访问
 * 的时候被调用
 *
 * @_iommu_mr: the #IOMMUMemoryRegion to be initialized
 * @instance_size: the IOMMUMemoryRegion subclass instance size
 * @mrtypename: the type name of the #IOMMUMemoryRegion
 * @owner: the object that tracks the region's reference count
 * @name: used for debugging; not visible to the user or ABI
 * @size: size of the region.
 */
void memory_region_init_iommu(void *_iommu_mr,
                              size_t instance_size,
                              const char *mrtypename,
                              Object *owner,
                              const char *name,
                              uint64_t size);

/**
 * memory_region_init_ram - Initialize RAM memory region.  Accesses into the
 *                          region will modify memory directly.
 *
 * @mr: the #MemoryRegion to be initialized
 * @owner: the object that tracks the region's reference count (must be
 *         TYPE_DEVICE or a subclass of TYPE_DEVICE, or NULL)
 * @name: name of the memory region
 * @size: size of the region in bytes
 * @errp: pointer to Error*, to store an error if it happens.
 *
 * This function allocates RAM for a board model or device, and
 * arranges for it to be migrated (by calling vmstate_register_ram()
 * if @owner is a DeviceState, or vmstate_register_ram_global() if
 * @owner is NULL). 此函数为板型号或设备分配RAM，并安排迁移
 *
 * TODO: Currently we restrict @owner to being either NULL (for
 * global RAM regions with no owner) or devices, so that we can
 * give the RAM block a unique name for migration purposes.
 * We should lift this restriction and allow arbitrary Objects.
 * If you pass a non-NULL non-device @owner then we will assert.
 * 目前，我们将@owner限制为NULL（对于没有所有者的全局RAM区域）或设备，这样我们就可以
 * 为RAM块指定一个唯一的名称，以便于迁移。我们应该取消这一限制，允许任意的对象。如果您
 * 传递一个非空的non-device@owner，那么我们将断言
 */
void memory_region_init_ram(MemoryRegion *mr,
                            struct Object *owner,
                            const char *name,
                            uint64_t size,
                            Error **errp);

/**
 * memory_region_init_rom: Initialize a ROM memory region.
 *
 * This has the same effect as calling memory_region_init_ram()
 * and then marking the resulting region read-only with
 * memory_region_set_readonly(). This includes arranging for the
 * contents to be migrated.
 *
 * TODO: Currently we restrict @owner to being either NULL (for
 * global RAM regions with no owner) or devices, so that we can
 * give the RAM block a unique name for migration purposes.
 * We should lift this restriction and allow arbitrary Objects.
 * If you pass a non-NULL non-device @owner then we will assert.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: Region name, becomes part of RAMBlock name used in migration stream
 *        must be unique within any device
 * @size: size of the region.
 * @errp: pointer to Error*, to store an error if it happens.
 */
void memory_region_init_rom(MemoryRegion *mr,
                            struct Object *owner,
                            const char *name,
                            uint64_t size,
                            Error **errp);

/**
 * memory_region_init_rom_device:  Initialize a ROM memory region.
 *                                 Writes are handled via callbacks.
 *
 * This function initializes a memory region backed by RAM for reads
 * and callbacks for writes, and arranges for the RAM backing to
 * be migrated (by calling vmstate_register_ram()
 * if @owner is a DeviceState, or vmstate_register_ram_global() if
 * @owner is NULL).
 *
 * TODO: Currently we restrict @owner to being either NULL (for
 * global RAM regions with no owner) or devices, so that we can
 * give the RAM block a unique name for migration purposes.
 * We should lift this restriction and allow arbitrary Objects.
 * If you pass a non-NULL non-device @owner then we will assert.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @ops: callbacks for write access handling (must not be NULL).
 * @opaque: passed to the read and write callbacks of the @ops structure.
 * @name: Region name, becomes part of RAMBlock name used in migration stream
 *        must be unique within any device
 * @size: size of the region.
 * @errp: pointer to Error*, to store an error if it happens.
 */
void memory_region_init_rom_device(MemoryRegion *mr,
                                   struct Object *owner,
                                   const MemoryRegionOps *ops,
                                   void *opaque,
                                   const char *name,
                                   uint64_t size,
                                   Error **errp);


/**
 * memory_region_owner: get a memory region's owner.
 *
 * @mr: the memory region being queried.
 */
struct Object *memory_region_owner(MemoryRegion *mr);

/**
 * memory_region_size: get a memory region's size.
 *
 * @mr: the memory region being queried.
 */
uint64_t memory_region_size(MemoryRegion *mr);

/**
 * memory_region_is_ram: check whether a memory region is random access
 *
 * Returns %true if a memory region is random access.
 *
 * @mr: the memory region being queried
 */
static inline bool memory_region_is_ram(MemoryRegion *mr)
{
    return mr->ram;
}

/**
 * memory_region_is_ram_device: check whether a memory region is a ram device
 *
 * Returns %true if a memory region is a device backed ram region
 *
 * @mr: the memory region being queried
 */
bool memory_region_is_ram_device(MemoryRegion *mr);

/**
 * memory_region_is_romd: check whether a memory region is in ROMD mode
 *
 * Returns %true if a memory region is a ROM device and currently set to allow
 * direct reads.
 *
 * @mr: the memory region being queried
 */
static inline bool memory_region_is_romd(MemoryRegion *mr)
{
    return mr->rom_device && mr->romd_mode;
}

/**
 * memory_region_get_iommu: check whether a memory region is an iommu
 *
 * Returns pointer to IOMMUMemoryRegion if a memory region is an iommu,
 * otherwise NULL.
 *
 * @mr: the memory region being queried
 */
static inline IOMMUMemoryRegion *memory_region_get_iommu(MemoryRegion *mr)
{
    if (mr->alias) {
        return memory_region_get_iommu(mr->alias);
    }
    if (mr->is_iommu) {
        return (IOMMUMemoryRegion *) mr;
    }
    return NULL;
}

/**
 * memory_region_get_iommu_class_nocheck: returns iommu memory region class
 *   if an iommu or NULL if not
 *
 * Returns pointer to IOMMUMemoryRegionClass if a memory region is an iommu,
 * otherwise NULL. This is fast path avoiding QOM checking, use with caution.
 *
 * @iommu_mr: the memory region being queried
 */
static inline IOMMUMemoryRegionClass *memory_region_get_iommu_class_nocheck(
        IOMMUMemoryRegion *iommu_mr)
{
    return (IOMMUMemoryRegionClass *) (((Object *)iommu_mr)->class);
}

#define memory_region_is_iommu(mr) (memory_region_get_iommu(mr) != NULL)

/**
 * memory_region_iommu_get_min_page_size: get minimum supported page size
 * for an iommu
 *
 * Returns minimum supported page size for an iommu.
 *
 * @iommu_mr: the memory region being queried
 */
uint64_t memory_region_iommu_get_min_page_size(IOMMUMemoryRegion *iommu_mr);

/**
 * memory_region_notify_iommu: notify a change in an IOMMU translation entry.
 *
 * The notification type will be decided by entry.perm bits:
 *
 * - For UNMAP (cache invalidation) notifies: set entry.perm to IOMMU_NONE.
 * - For MAP (newly added entry) notifies: set entry.perm to the
 *   permission of the page (which is definitely !IOMMU_NONE).
 *
 * Note: for any IOMMU implementation, an in-place mapping change
 * should be notified with an UNMAP followed by a MAP.
 *
 * @iommu_mr: the memory region that was changed
 * @iommu_idx: the IOMMU index for the translation table which has changed
 * @entry: the new entry in the IOMMU translation table.  The entry
 *         replaces all old entries for the same virtual I/O address range.
 *         Deleted entries have .@perm == 0.
 */
void memory_region_notify_iommu(IOMMUMemoryRegion *iommu_mr,
                                int iommu_idx,
                                IOMMUTLBEntry entry);

/**
 * memory_region_notify_one: notify a change in an IOMMU translation
 *                           entry to a single notifier
 *
 * This works just like memory_region_notify_iommu(), but it only
 * notifies a specific notifier, not all of them.
 * 作用跟上述方法类似，只不过这里是要处理一些特殊的情形
 *
 * @notifier: the notifier to be notified
 * @entry: the new entry in the IOMMU translation table.  The entry
 *         replaces all old entries for the same virtual I/O address range.
 *         Deleted entries have .@perm == 0.
 * 该条目将替换相同虚拟I/O地址范围的所有旧条目。删除的条目有 .@perm == 0
 */
void memory_region_notify_one(IOMMUNotifier *notifier,
                              IOMMUTLBEntry *entry);

/**
 * memory_region_register_iommu_notifier: register a notifier for changes to
 * IOMMU translation entries.
 *
 * Returns 0 on success, or a negative errno otherwise. In particular,
 * -EINVAL indicates that at least one of the attributes of the notifier
 * is not supported (flag/range) by the IOMMU memory region. In case of error
 * the error object must be created.
 *
 * @mr: the memory region to observe
 * @n: the IOMMUNotifier to be added; the notify callback receives a
 *     pointer to an #IOMMUTLBEntry as the opaque value; the pointer
 *     ceases to be valid on exit from the notifier.
 * @errp: pointer to Error*, to store an error if it happens.
 */
int memory_region_register_iommu_notifier(MemoryRegion *mr,
                                          IOMMUNotifier *n, Error **errp);

/**
 * memory_region_iommu_replay: replay existing IOMMU translations to
 * a notifier with the minimum page granularity returned by
 * mr->iommu_ops->get_page_size().
 *
 * Note: this is not related to record-and-replay functionality.
 *
 * @iommu_mr: the memory region to observe
 * @n: the notifier to which to replay iommu mappings
 */
void memory_region_iommu_replay(IOMMUMemoryRegion *iommu_mr, IOMMUNotifier *n);

/**
 * memory_region_unregister_iommu_notifier: unregister a notifier for
 * changes to IOMMU translation entries.
 *
 * @mr: the memory region which was observed and for which notity_stopped()
 *      needs to be called
 * @n: the notifier to be removed.
 */
void memory_region_unregister_iommu_notifier(MemoryRegion *mr,
                                             IOMMUNotifier *n);

/**
 * memory_region_iommu_get_attr: return an IOMMU attr if get_attr() is
 * defined on the IOMMU.
 *
 * Returns 0 on success, or a negative errno otherwise. In particular,
 * -EINVAL indicates that the IOMMU does not support the requested
 * attribute.
 *
 * @iommu_mr: the memory region
 * @attr: the requested attribute
 * @data: a pointer to the requested attribute data
 */
int memory_region_iommu_get_attr(IOMMUMemoryRegion *iommu_mr,
                                 enum IOMMUMemoryRegionAttr attr,
                                 void *data);

/**
 * memory_region_iommu_attrs_to_index: return the IOMMU index to
 * use for translations with the given memory transaction attributes.
 *
 * @iommu_mr: the memory region
 * @attrs: the memory transaction attributes
 */
int memory_region_iommu_attrs_to_index(IOMMUMemoryRegion *iommu_mr,
                                       MemTxAttrs attrs);

/**
 * memory_region_iommu_num_indexes: return the total number of IOMMU
 * indexes that this IOMMU supports.
 *
 * @iommu_mr: the memory region
 */
int memory_region_iommu_num_indexes(IOMMUMemoryRegion *iommu_mr);

/**
 * memory_region_name: get a memory region's name
 *
 * Returns the string that was used to initialize the memory region.
 *
 * @mr: the memory region being queried
 */
const char *memory_region_name(const MemoryRegion *mr);

/**
 * memory_region_is_logging: return whether a memory region is logging writes
 *
 * Returns %true if the memory region is logging writes for the given client
 *
 * @mr: the memory region being queried
 * @client: the client being queried
 */
bool memory_region_is_logging(MemoryRegion *mr, uint8_t client);

/**
 * memory_region_get_dirty_log_mask: return the clients for which a
 * memory region is logging writes.
 *
 * Returns a bitmap of clients, in which the DIRTY_MEMORY_* constants
 * are the bit indices.
 *
 * @mr: the memory region being queried
 */
uint8_t memory_region_get_dirty_log_mask(MemoryRegion *mr);

/**
 * memory_region_is_rom: check whether a memory region is ROM
 *
 * Returns %true if a memory region is read-only memory.
 *
 * @mr: the memory region being queried
 */
static inline bool memory_region_is_rom(MemoryRegion *mr)
{
    return mr->ram && mr->readonly;
}

/**
 * memory_region_is_nonvolatile: check whether a memory region is non-volatile
 *
 * Returns %true is a memory region is non-volatile memory.
 *
 * @mr: the memory region being queried
 */
static inline bool memory_region_is_nonvolatile(MemoryRegion *mr)
{
    return mr->nonvolatile;
}

/**
 * memory_region_get_fd: Get a file descriptor backing a RAM memory region.
 *
 * Returns a file descriptor backing a file-based RAM memory region,
 * or -1 if the region is not a file-based RAM memory region.
 *
 * @mr: the RAM or alias memory region being queried.
 */
int memory_region_get_fd(MemoryRegion *mr);

/**
 * memory_region_from_host: Convert a pointer into a RAM memory region
 * and an offset within it.
 *
 * Given a host pointer inside a RAM memory region (created with
 * memory_region_init_ram() or memory_region_init_ram_ptr()), return
 * the MemoryRegion and the offset within it.
 *
 * Use with care; by the time this function returns, the returned pointer is
 * not protected by RCU anymore.  If the caller is not within an RCU critical
 * section and does not hold the iothread lock, it must have other means of
 * protecting the pointer, such as a reference to the region that includes
 * the incoming ram_addr_t.
 *
 * @ptr: the host pointer to be converted
 * @offset: the offset within memory region
 */
MemoryRegion *memory_region_from_host(void *ptr, ram_addr_t *offset);

/**
 * memory_region_get_ram_ptr: Get a pointer into a RAM memory region.
 *
 * Returns a host pointer to a RAM memory region (created with
 * memory_region_init_ram() or memory_region_init_ram_ptr()).
 *
 * Use with care; by the time this function returns, the returned pointer is
 * not protected by RCU anymore.  If the caller is not within an RCU critical
 * section and does not hold the iothread lock, it must have other means of
 * protecting the pointer, such as a reference to the region that includes
 * the incoming ram_addr_t.
 *
 * @mr: the memory region being queried.
 */
void *memory_region_get_ram_ptr(MemoryRegion *mr);

/* memory_region_ram_resize: Resize a RAM region.
 *
 * Only legal before guest might have detected the memory size: e.g. on
 * incoming migration, or right after reset.
 *
 * @mr: a memory region created with @memory_region_init_resizeable_ram.
 * @newsize: the new size the region
 * @errp: pointer to Error*, to store an error if it happens.
 */
void memory_region_ram_resize(MemoryRegion *mr, ram_addr_t newsize,
                              Error **errp);

/**
 * memory_region_msync: Synchronize selected address range of
 * a memory mapped region
 *
 * @mr: the memory region to be msync
 * @addr: the initial address of the range to be sync
 * @size: the size of the range to be sync
 */
void memory_region_msync(MemoryRegion *mr, hwaddr addr, hwaddr size);

/**
 * memory_region_writeback: Trigger cache writeback for
 * selected address range 触发选定地址范围的缓存写回
 *
 * @mr: the memory region to be updated
 * @addr: the initial address of the range to be written back
 * @size: the size of the range to be written back
 */
void memory_region_writeback(MemoryRegion *mr, hwaddr addr, hwaddr size);

/**
 * memory_region_set_log: Turn dirty logging on or off for a region.
 *
 * Turns dirty logging on or off for a specified client (display, migration).
 * Only meaningful for RAM regions.
 *
 * @mr: the memory region being updated.
 * @log: whether dirty logging is to be enabled or disabled.
 * @client: the user of the logging information; %DIRTY_MEMORY_VGA only.
 */
void memory_region_set_log(MemoryRegion *mr, bool log, unsigned client);

/**
 * memory_region_set_dirty: Mark a range of bytes as dirty in a memory region.
 *
 * Marks a range of bytes as dirty, after it has been dirtied outside
 * guest code.
 *
 * @mr: the memory region being dirtied.
 * @addr: the address (relative to the start of the region) being dirtied.
 * @size: size of the range being dirtied.
 */
void memory_region_set_dirty(MemoryRegion *mr, hwaddr addr,
                             hwaddr size);

/**
 * memory_region_clear_dirty_bitmap - clear dirty bitmap for memory range
 *
 * This function is called when the caller wants to clear the remote
 * dirty bitmap of a memory range within the memory region.  This can
 * be used by e.g. KVM to manually clear dirty log when
 * KVM_CAP_MANUAL_DIRTY_LOG_PROTECT is declared support by the host
 * kernel.
 *
 * @mr:     the memory region to clear the dirty log upon
 * @start:  start address offset within the memory region
 * @len:    length of the memory region to clear dirty bitmap
 */
void memory_region_clear_dirty_bitmap(MemoryRegion *mr, hwaddr start,
                                      hwaddr len);

/**
 * memory_region_snapshot_and_clear_dirty: Get a snapshot of the dirty
 *                                         bitmap and clear it.
 *
 * Creates a snapshot of the dirty bitmap, clears the dirty bitmap and
 * returns the snapshot.  The snapshot can then be used to query dirty
 * status, using memory_region_snapshot_get_dirty.  Snapshotting allows
 * querying the same page multiple times, which is especially useful for
 * display updates where the scanlines often are not page aligned.
 *
 * The dirty bitmap region which gets copyed into the snapshot (and
 * cleared afterwards) can be larger than requested.  The boundaries
 * are rounded up/down so complete bitmap longs (covering 64 pages on
 * 64bit hosts) can be copied over into the bitmap snapshot.  Which
 * isn't a problem for display updates as the extra pages are outside
 * the visible area, and in case the visible area changes a full
 * display redraw is due anyway.  Should other use cases for this
 * function emerge we might have to revisit this implementation
 * detail.
 *
 * Use g_free to release DirtyBitmapSnapshot.
 *
 * @mr: the memory region being queried.
 * @addr: the address (relative to the start of the region) being queried.
 * @size: the size of the range being queried.
 * @client: the user of the logging information; typically %DIRTY_MEMORY_VGA.
 */
DirtyBitmapSnapshot *memory_region_snapshot_and_clear_dirty(MemoryRegion *mr,
                                                            hwaddr addr,
                                                            hwaddr size,
                                                            unsigned client);

/**
 * memory_region_snapshot_get_dirty: Check whether a range of bytes is dirty
 *                                   in the specified dirty bitmap snapshot.
 *
 * @mr: the memory region being queried.
 * @snap: the dirty bitmap snapshot
 * @addr: the address (relative to the start of the region) being queried.
 * @size: the size of the range being queried.
 */
bool memory_region_snapshot_get_dirty(MemoryRegion *mr,
                                      DirtyBitmapSnapshot *snap,
                                      hwaddr addr, hwaddr size);

/**
 * memory_region_reset_dirty: Mark a range of pages as clean, for a specified
 *                            client.
 *
 * Marks a range of pages as no longer dirty.
 *
 * @mr: the region being updated.
 * @addr: the start of the subrange being cleaned.
 * @size: the size of the subrange being cleaned.
 * @client: the user of the logging information; %DIRTY_MEMORY_MIGRATION or
 *          %DIRTY_MEMORY_VGA.
 */
void memory_region_reset_dirty(MemoryRegion *mr, hwaddr addr,
                               hwaddr size, unsigned client);

/**
 * memory_region_flush_rom_device: Mark a range of pages dirty and invalidate
 *                                 TBs (for self-modifying code).
 *
 * The MemoryRegionOps->write() callback of a ROM device must use this function
 * to mark byte ranges that have been modified internally, such as by directly
 * accessing the memory returned by memory_region_get_ram_ptr().
 *
 * This function marks the range dirty and invalidates TBs so that TCG can
 * detect self-modifying code.
 *
 * @mr: the region being flushed.
 * @addr: the start, relative to the start of the region, of the range being
 *        flushed.
 * @size: the size, in bytes, of the range being flushed.
 */
void memory_region_flush_rom_device(MemoryRegion *mr, hwaddr addr, hwaddr size);

/**
 * memory_region_set_readonly: Turn a memory region read-only (or read-write)
 *
 * Allows a memory region to be marked as read-only (turning it into a ROM).
 * only useful on RAM regions.
 *
 * @mr: the region being updated.
 * @readonly: whether rhe region is to be ROM or RAM.
 */
void memory_region_set_readonly(MemoryRegion *mr, bool readonly);

/**
 * memory_region_set_nonvolatile: Turn a memory region non-volatile
 *
 * Allows a memory region to be marked as non-volatile.
 * only useful on RAM regions.
 *
 * @mr: the region being updated.
 * @nonvolatile: whether rhe region is to be non-volatile.
 */
void memory_region_set_nonvolatile(MemoryRegion *mr, bool nonvolatile);

/**
 * memory_region_rom_device_set_romd: enable/disable ROMD mode
 *
 * Allows a ROM device (initialized with memory_region_init_rom_device() to
 * set to ROMD mode (default) or MMIO mode.  When it is in ROMD mode, the
 * device is mapped to guest memory and satisfies read access directly.
 * When in MMIO mode, reads are forwarded to the #MemoryRegion.read function.
 * Writes are always handled by the #MemoryRegion.write function.
 *
 * @mr: the memory region to be updated
 * @romd_mode: %true to put the region into ROMD mode
 */
void memory_region_rom_device_set_romd(MemoryRegion *mr, bool romd_mode);

/**
 * memory_region_set_coalescing: Enable memory coalescing for the region.
 *
 * Enabled writes to a region to be queued for later processing. MMIO ->write
 * callbacks may be delayed until a non-coalesced MMIO is issued.
 * Only useful for IO regions.  Roughly similar to write-combining hardware.
 *
 * @mr: the memory region to be write coalesced
 */
void memory_region_set_coalescing(MemoryRegion *mr);

/**
 * memory_region_add_coalescing: Enable memory coalescing for a sub-range of
 *                               a region.
 *
 * Like memory_region_set_coalescing(), but works on a sub-range of a region.
 * Multiple calls can be issued coalesced disjoint ranges.
 *
 * @mr: the memory region to be updated.
 * @offset: the start of the range within the region to be coalesced.
 * @size: the size of the subrange to be coalesced.
 */
void memory_region_add_coalescing(MemoryRegion *mr,
                                  hwaddr offset,
                                  uint64_t size);

/**
 * memory_region_clear_coalescing: Disable MMIO coalescing for the region.
 *
 * Disables any coalescing caused by memory_region_set_coalescing() or
 * memory_region_add_coalescing().  Roughly equivalent to uncacheble memory
 * hardware.
 *
 * @mr: the memory region to be updated.
 */
void memory_region_clear_coalescing(MemoryRegion *mr);

/**
 * memory_region_set_flush_coalesced: Enforce memory coalescing flush before
 *                                    accesses.
 *
 * Ensure that pending coalesced MMIO request are flushed before the memory
 * region is accessed. This property is automatically enabled for all regions
 * passed to memory_region_set_coalescing() and memory_region_add_coalescing().
 *
 * @mr: the memory region to be updated.
 */
void memory_region_set_flush_coalesced(MemoryRegion *mr);

/**
 * memory_region_clear_flush_coalesced: Disable memory coalescing flush before
 *                                      accesses.
 *
 * Clear the automatic coalesced MMIO flushing enabled via
 * memory_region_set_flush_coalesced. Note that this service has no effect on
 * memory regions that have MMIO coalescing enabled for themselves. For them,
 * automatic flushing will stop once coalescing is disabled.
 *
 * @mr: the memory region to be updated.
 */
void memory_region_clear_flush_coalesced(MemoryRegion *mr);

/**
 * memory_region_clear_global_locking: Declares that access processing does
 *                                     not depend on the QEMU global lock.
 *
 * By clearing this property, accesses to the memory region will be processed
 * outside of QEMU's global lock (unless the lock is held on when issuing the
 * access request). In this case, the device model implementing the access
 * handlers is responsible for synchronization of concurrency.
 *
 * @mr: the memory region to be updated.
 */
void memory_region_clear_global_locking(MemoryRegion *mr);

/**
 * memory_region_add_eventfd: Request an eventfd to be triggered when a word
 *                            is written to a location.
 *
 * Marks a word in an IO region (initialized with memory_region_init_io())
 * as a trigger for an eventfd event.  The I/O callback will not be called.
 * The caller must be prepared to handle failure (that is, take the required
 * action if the callback _is_ called).
 *
 * @mr: the memory region being updated.
 * @addr: the address within @mr that is to be monitored
 * @size: the size of the access to trigger the eventfd
 * @match_data: whether to match against @data, instead of just @addr
 * @data: the data to match against the guest write
 * @e: event notifier to be triggered when @addr, @size, and @data all match.
 **/
void memory_region_add_eventfd(MemoryRegion *mr,
                               hwaddr addr,
                               unsigned size,
                               bool match_data,
                               uint64_t data,
                               EventNotifier *e);

/**
 * memory_region_del_eventfd: Cancel an eventfd.
 *
 * Cancels an eventfd trigger requested by a previous
 * memory_region_add_eventfd() call.
 *
 * @mr: the memory region being updated.
 * @addr: the address within @mr that is to be monitored
 * @size: the size of the access to trigger the eventfd
 * @match_data: whether to match against @data, instead of just @addr
 * @data: the data to match against the guest write
 * @e: event notifier to be triggered when @addr, @size, and @data all match.
 */
void memory_region_del_eventfd(MemoryRegion *mr,
                               hwaddr addr,
                               unsigned size,
                               bool match_data,
                               uint64_t data,
                               EventNotifier *e);

/**
 * memory_region_add_subregion: Add a subregion to a container.
 *
 * Adds a subregion at @offset.  The subregion may not overlap with other
 * subregions (except for those explicitly marked as overlapping).  A region
 * may only be added once as a subregion (unless removed with
 * memory_region_del_subregion()); use memory_region_init_alias() if you
 * want a region to be a subregion in multiple locations.
 *
 * @mr: the region to contain the new subregion; must be a container
 *      initialized with memory_region_init().
 * @offset: the offset relative to @mr where @subregion is added.
 * @subregion: the subregion to be added.
 */
void memory_region_add_subregion(MemoryRegion *mr,
                                 hwaddr offset,
                                 MemoryRegion *subregion);
/**
 * memory_region_add_subregion_overlap: Add a subregion to a container
 *                                      with overlap.
 *
 * Adds a subregion at @offset.  The subregion may overlap with other
 * subregions.  Conflicts are resolved by having a higher @priority hide a
 * lower @priority. Subregions without priority are taken as @priority 0.
 * A region may only be added once as a subregion (unless removed with
 * memory_region_del_subregion()); use memory_region_init_alias() if you
 * want a region to be a subregion in multiple locations.
 *
 * @mr: the region to contain the new subregion; must be a container
 *      initialized with memory_region_init().
 * @offset: the offset relative to @mr where @subregion is added.
 * @subregion: the subregion to be added.
 * @priority: used for resolving overlaps; highest priority wins.
 */
void memory_region_add_subregion_overlap(MemoryRegion *mr,
                                         hwaddr offset,
                                         MemoryRegion *subregion,
                                         int priority);

/**
 * memory_region_get_ram_addr: Get the ram address associated with a memory
 *                             region
 *
 * @mr: the region to be queried
 */
ram_addr_t memory_region_get_ram_addr(MemoryRegion *mr);

uint64_t memory_region_get_alignment(const MemoryRegion *mr);
/**
 * memory_region_del_subregion: Remove a subregion.
 *
 * Removes a subregion from its container.
 *
 * @mr: the container to be updated.
 * @subregion: the region being removed; must be a current subregion of @mr.
 */
void memory_region_del_subregion(MemoryRegion *mr,
                                 MemoryRegion *subregion);

/*
 * memory_region_set_enabled: dynamically enable or disable a region
 *
 * Enables or disables a memory region.  A disabled memory region
 * ignores all accesses to itself and its subregions.  It does not
 * obscure sibling subregions with lower priority - it simply behaves as
 * if it was removed from the hierarchy.
 *
 * Regions default to being enabled.
 *
 * @mr: the region to be updated
 * @enabled: whether to enable or disable the region
 */
void memory_region_set_enabled(MemoryRegion *mr, bool enabled);

/*
 * memory_region_set_address: dynamically update the address of a region
 *
 * Dynamically updates the address of a region, relative to its container.
 * May be used on regions are currently part of a memory hierarchy.
 *
 * @mr: the region to be updated
 * @addr: new address, relative to container region
 */
void memory_region_set_address(MemoryRegion *mr, hwaddr addr);

/*
 * memory_region_set_size: dynamically update the size of a region.
 *
 * Dynamically updates the size of a region.
 *
 * @mr: the region to be updated
 * @size: used size of the region.
 */
void memory_region_set_size(MemoryRegion *mr, uint64_t size);

/*
 * memory_region_set_alias_offset: dynamically update a memory alias's offset
 *
 * Dynamically updates the offset into the target region that an alias points
 * to, as if the fourth argument to memory_region_init_alias() has changed.
 *
 * @mr: the #MemoryRegion to be updated; should be an alias.
 * @offset: the new offset into the target memory region
 */
void memory_region_set_alias_offset(MemoryRegion *mr,
                                    hwaddr offset);

/**
 * memory_region_present: checks if an address relative to a @container
 * translates into #MemoryRegion within @container
 *
 * Answer whether a #MemoryRegion within @container covers the address
 * @addr.
 *
 * @container: a #MemoryRegion within which @addr is a relative address
 * @addr: the area within @container to be searched
 */
bool memory_region_present(MemoryRegion *container, hwaddr addr);

/**
 * memory_region_is_mapped: returns true if #MemoryRegion is mapped
 * into any address space.
 *
 * @mr: a #MemoryRegion which should be checked if it's mapped
 */
bool memory_region_is_mapped(MemoryRegion *mr);

/**
 * memory_region_find: translate an address/size relative to a
 * MemoryRegion into a #MemoryRegionSection.
 *
 * Locates the first #MemoryRegion within @mr that overlaps the range
 * given by @addr and @size.
 *
 * Returns a #MemoryRegionSection that describes a contiguous overlap.
 * It will have the following characteristics:
 * - @size = 0 iff no overlap was found
 * - @mr is non-%NULL iff an overlap was found
 *
 * Remember that in the return value the @offset_within_region is
 * relative to the returned region (in the .@mr field), not to the
 * @mr argument.
 *
 * Similarly, the .@offset_within_address_space is relative to the
 * address space that contains both regions, the passed and the
 * returned one.  However, in the special case where the @mr argument
 * has no container (and thus is the root of the address space), the
 * following will hold:
 * - @offset_within_address_space >= @addr
 * - @offset_within_address_space + .@size <= @addr + @size
 *
 * @mr: a MemoryRegion within which @addr is a relative address
 * @addr: start of the area within @as to be searched
 * @size: size of the area to be searched
 */
MemoryRegionSection memory_region_find(MemoryRegion *mr,
                                       hwaddr addr, uint64_t size);

/**
 * memory_global_dirty_log_sync: synchronize the dirty log for all memory
 *
 * Synchronizes the dirty page log for all address spaces.
 */
void memory_global_dirty_log_sync(void);

/**
 * memory_global_dirty_log_sync: synchronize the dirty log for all memory
 *
 * Synchronizes the vCPUs with a thread that is reading the dirty bitmap.
 * This function must be called after the dirty log bitmap is cleared, and
 * before dirty guest memory pages are read.  If you are using
 * #DirtyBitmapSnapshot, memory_region_snapshot_and_clear_dirty() takes
 * care of doing this.
 */
void memory_global_after_dirty_log_sync(void);

/**
 * memory_region_transaction_begin: Start a transaction.
 *
 * During a transaction, changes will be accumulated and made visible
 * only when the transaction ends (is committed).
 */
void memory_region_transaction_begin(void);

/**
 * memory_region_transaction_commit: Commit a transaction and make changes
 *                                   visible to the guest.
 */
void memory_region_transaction_commit(void);

/**
 * memory_listener_register: register callbacks to be called when memory
 *                           sections are mapped or unmapped into an address
 *                           space
 *
 * @listener: an object containing the callbacks to be called
 * @filter: if non-%NULL, only regions in this address space will be observed
 */
void memory_listener_register(MemoryListener *listener, AddressSpace *filter);

/**
 * memory_listener_unregister: undo the effect of memory_listener_register()
 *
 * @listener: an object containing the callbacks to be removed
 */
void memory_listener_unregister(MemoryListener *listener);

/**
 * memory_global_dirty_log_start: begin dirty logging for all regions
 */
void memory_global_dirty_log_start(void);

/**
 * memory_global_dirty_log_stop: end dirty logging for all regions
 */
void memory_global_dirty_log_stop(void);

void mtree_info(bool flatview, bool dispatch_tree, bool owner, bool disabled);

/**
 * memory_region_dispatch_read: perform a read directly to the specified
 * MemoryRegion.
 *
 * @mr: #MemoryRegion to access
 * @addr: address within that region
 * @pval: pointer to uint64_t which the data is written to
 * @op: size, sign, and endianness of the memory operation
 * @attrs: memory transaction attributes to use for the access
 */
MemTxResult memory_region_dispatch_read(MemoryRegion *mr,
                                        hwaddr addr,
                                        uint64_t *pval,
                                        MemOp op,
                                        MemTxAttrs attrs);
/**
 * memory_region_dispatch_write: perform a write directly to the specified
 * MemoryRegion.
 *
 * @mr: #MemoryRegion to access
 * @addr: address within that region
 * @data: data to write
 * @op: size, sign, and endianness of the memory operation
 * @attrs: memory transaction attributes to use for the access
 */
MemTxResult memory_region_dispatch_write(MemoryRegion *mr,
                                         hwaddr addr,
                                         uint64_t data,
                                         MemOp op,
                                         MemTxAttrs attrs);

/**
 * address_space_init: initializes an address space
 *
 * @as: an uninitialized #AddressSpace
 * @root: a #MemoryRegion that routes addresses for the address space
 * @name: an address space name.  The name is only used for debugging
 *        output.
 */
void address_space_init(AddressSpace *as, MemoryRegion *root, const char *name);

/**
 * address_space_destroy: destroy an address space
 *
 * Releases all resources associated with an address space.  After an address space
 * is destroyed, its root memory region (given by address_space_init()) may be destroyed
 * as well.
 *
 * @as: address space to be destroyed
 */
void address_space_destroy(AddressSpace *as);

/**
 * address_space_remove_listeners: unregister all listeners of an address space
 *
 * Removes all callbacks previously registered with memory_listener_register()
 * for @as.
 *
 * @as: an initialized #AddressSpace
 */
void address_space_remove_listeners(AddressSpace *as);

/**
 * address_space_rw: read from or write to an address space.
 *
 * Return a MemTxResult indicating whether the operation succeeded
 * or failed (eg unassigned memory, device rejected the transaction,
 * IOMMU fault).
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @attrs: memory transaction attributes
 * @buf: buffer with the data transferred
 * @len: the number of bytes to read or write
 * @is_write: indicates the transfer direction
 */
MemTxResult address_space_rw(AddressSpace *as, hwaddr addr,
                             MemTxAttrs attrs, void *buf,
                             hwaddr len, bool is_write);

/**
 * address_space_write: write to address space.
 *
 * Return a MemTxResult indicating whether the operation succeeded
 * or failed (eg unassigned memory, device rejected the transaction,
 * IOMMU fault).
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @attrs: memory transaction attributes
 * @buf: buffer with the data transferred
 * @len: the number of bytes to write
 */
MemTxResult address_space_write(AddressSpace *as, hwaddr addr,
                                MemTxAttrs attrs,
                                const void *buf, hwaddr len);

/**
 * address_space_write_rom: write to address space, including ROM.
 *
 * This function writes to the specified address space, but will
 * write data to both ROM and RAM. This is used for non-guest
 * writes like writes from the gdb debug stub or initial loading
 * of ROM contents.
 *
 * Note that portions of the write which attempt to write data to
 * a device will be silently ignored -- only real RAM and ROM will
 * be written to.
 *
 * Return a MemTxResult indicating whether the operation succeeded
 * or failed (eg unassigned memory, device rejected the transaction,
 * IOMMU fault).
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @attrs: memory transaction attributes
 * @buf: buffer with the data transferred
 * @len: the number of bytes to write
 */
MemTxResult address_space_write_rom(AddressSpace *as, hwaddr addr,
                                    MemTxAttrs attrs,
                                    const void *buf, hwaddr len);

/* address_space_ld*: load from an address space
 * address_space_st*: store to an address space
 *
 * These functions perform a load or store of the byte, word,
 * longword or quad to the specified address within the AddressSpace.
 * The _le suffixed functions treat the data as little endian;
 * _be indicates big endian; no suffix indicates "same endianness
 * as guest CPU".
 *
 * The "guest CPU endianness" accessors are deprecated for use outside
 * target-* code; devices should be CPU-agnostic and use either the LE
 * or the BE accessors.
 *
 * @as #AddressSpace to be accessed
 * @addr: address within that address space
 * @val: data value, for stores
 * @attrs: memory transaction attributes
 * @result: location to write the success/failure of the transaction;
 *   if NULL, this information is discarded
 */

#define SUFFIX
#define ARG1         as
#define ARG1_DECL    AddressSpace *as
#include "exec/memory_ldst.inc.h"

#define SUFFIX
#define ARG1         as
#define ARG1_DECL    AddressSpace *as
#include "exec/memory_ldst_phys.inc.h"

struct MemoryRegionCache {
    void *ptr;
    hwaddr xlat;
    hwaddr len;
    FlatView *fv;
    MemoryRegionSection mrs;
    bool is_write;
};

#define MEMORY_REGION_CACHE_INVALID ((MemoryRegionCache) { .mrs.mr = NULL })


/* address_space_ld*_cached: load from a cached #MemoryRegion
 * address_space_st*_cached: store into a cached #MemoryRegion
 *
 * These functions perform a load or store of the byte, word,
 * longword or quad to the specified address.  The address is
 * a physical address in the AddressSpace, but it must lie within
 * a #MemoryRegion that was mapped with address_space_cache_init.
 *
 * The _le suffixed functions treat the data as little endian;
 * _be indicates big endian; no suffix indicates "same endianness
 * as guest CPU".
 *
 * The "guest CPU endianness" accessors are deprecated for use outside
 * target-* code; devices should be CPU-agnostic and use either the LE
 * or the BE accessors.
 *
 * @cache: previously initialized #MemoryRegionCache to be accessed
 * @addr: address within the address space
 * @val: data value, for stores
 * @attrs: memory transaction attributes
 * @result: location to write the success/failure of the transaction;
 *   if NULL, this information is discarded
 */

#define SUFFIX       _cached_slow
#define ARG1         cache
#define ARG1_DECL    MemoryRegionCache *cache
#include "exec/memory_ldst.inc.h"

/* Inline fast path for direct RAM access.  */
static inline uint8_t address_space_ldub_cached(MemoryRegionCache *cache,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result)
{
    assert(addr < cache->len);
    if (likely(cache->ptr)) {
        return ldub_p(cache->ptr + addr);
    } else {
        return address_space_ldub_cached_slow(cache, addr, attrs, result);
    }
}

static inline void address_space_stb_cached(MemoryRegionCache *cache,
    hwaddr addr, uint32_t val, MemTxAttrs attrs, MemTxResult *result)
{
    assert(addr < cache->len);
    if (likely(cache->ptr)) {
        stb_p(cache->ptr + addr, val);
    } else {
        address_space_stb_cached_slow(cache, addr, val, attrs, result);
    }
}

#define ENDIANNESS   _le
#include "exec/memory_ldst_cached.inc.h"

#define ENDIANNESS   _be
#include "exec/memory_ldst_cached.inc.h"

#define SUFFIX       _cached
#define ARG1         cache
#define ARG1_DECL    MemoryRegionCache *cache
#include "exec/memory_ldst_phys.inc.h"

/* address_space_cache_init: prepare for repeated access to a physical
 * memory region
 *
 * @cache: #MemoryRegionCache to be filled
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @len: length of buffer
 * @is_write: indicates the transfer direction
 *
 * Will only work with RAM, and may map a subset of the requested range by
 * returning a value that is less than @len.  On failure, return a negative
 * errno value.
 *
 * Because it only works with RAM, this function can be used for
 * read-modify-write operations.  In this case, is_write should be %true.
 *
 * Note that addresses passed to the address_space_*_cached functions
 * are relative to @addr.
 */
int64_t address_space_cache_init(MemoryRegionCache *cache,
                                 AddressSpace *as,
                                 hwaddr addr,
                                 hwaddr len,
                                 bool is_write);

/**
 * address_space_cache_invalidate: complete a write to a #MemoryRegionCache
 *
 * @cache: The #MemoryRegionCache to operate on.
 * @addr: The first physical address that was written, relative to the
 * address that was passed to @address_space_cache_init.
 * @access_len: The number of bytes that were written starting at @addr.
 */
void address_space_cache_invalidate(MemoryRegionCache *cache,
                                    hwaddr addr,
                                    hwaddr access_len);

/**
 * address_space_cache_destroy: free a #MemoryRegionCache
 *
 * @cache: The #MemoryRegionCache whose memory should be released.
 */
void address_space_cache_destroy(MemoryRegionCache *cache);

/* address_space_get_iotlb_entry: translate an address into an IOTLB
 * entry. Should be called from an RCU critical section.
 */
IOMMUTLBEntry address_space_get_iotlb_entry(AddressSpace *as, hwaddr addr,
                                            bool is_write, MemTxAttrs attrs);

/* address_space_translate: translate an address range into an address space
 * into a MemoryRegion and an address range into that section.  Should be
 * called from an RCU critical section, to avoid that the last reference
 * to the returned region disappears after address_space_translate returns.
 *
 * @fv: #FlatView to be accessed
 * @addr: address within that address space
 * @xlat: pointer to address within the returned memory region section's
 * #MemoryRegion.
 * @len: pointer to length
 * @is_write: indicates the transfer direction
 * @attrs: memory attributes
 */
MemoryRegion *flatview_translate(FlatView *fv,
                                 hwaddr addr, hwaddr *xlat,
                                 hwaddr *len, bool is_write,
                                 MemTxAttrs attrs);

static inline MemoryRegion *address_space_translate(AddressSpace *as,
                                                    hwaddr addr, hwaddr *xlat,
                                                    hwaddr *len, bool is_write,
                                                    MemTxAttrs attrs)
{
    return flatview_translate(address_space_to_flatview(as),
                              addr, xlat, len, is_write, attrs);
}

/* address_space_access_valid: check for validity of accessing an address
 * space range
 *
 * Check whether memory is assigned to the given address space range, and
 * access is permitted by any IOMMU regions that are active for the address
 * space.
 *
 * For now, addr and len should be aligned to a page size.  This limitation
 * will be lifted in the future.
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @len: length of the area to be checked
 * @is_write: indicates the transfer direction
 * @attrs: memory attributes
 */
bool address_space_access_valid(AddressSpace *as, hwaddr addr, hwaddr len,
                                bool is_write, MemTxAttrs attrs);

/* address_space_map: map a physical memory region into a host virtual address
 *
 * May map a subset of the requested range, given by and returned in @plen.
 * May return %NULL and set *@plen to zero(0), if resources needed to perform
 * the mapping are exhausted.
 * Use only for reads OR writes - not for read-modify-write operations.
 * Use cpu_register_map_client() to know when retrying the map operation is
 * likely to succeed.
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @plen: pointer to length of buffer; updated on return
 * @is_write: indicates the transfer direction
 * @attrs: memory attributes
 */
void *address_space_map(AddressSpace *as, hwaddr addr,
                        hwaddr *plen, bool is_write, MemTxAttrs attrs);

/* address_space_unmap: Unmaps a memory region previously mapped by address_space_map()
 *
 * Will also mark the memory as dirty if @is_write == %true.  @access_len gives
 * the amount of memory that was actually read or written by the caller.
 *
 * @as: #AddressSpace used
 * @buffer: host pointer as returned by address_space_map()
 * @len: buffer length as returned by address_space_map()
 * @access_len: amount of data actually transferred
 * @is_write: indicates the transfer direction
 */
void address_space_unmap(AddressSpace *as, void *buffer, hwaddr len,
                         bool is_write, hwaddr access_len);


/* Internal functions, part of the implementation of address_space_read.  */
MemTxResult address_space_read_full(AddressSpace *as, hwaddr addr,
                                    MemTxAttrs attrs, void *buf, hwaddr len);
MemTxResult flatview_read_continue(FlatView *fv, hwaddr addr,
                                   MemTxAttrs attrs, void *buf,
                                   hwaddr len, hwaddr addr1, hwaddr l,
                                   MemoryRegion *mr);
void *qemu_map_ram_ptr(RAMBlock *ram_block, ram_addr_t addr);

/* Internal functions, part of the implementation of address_space_read_cached
 * and address_space_write_cached.  */
MemTxResult address_space_read_cached_slow(MemoryRegionCache *cache,
                                           hwaddr addr, void *buf, hwaddr len);
MemTxResult address_space_write_cached_slow(MemoryRegionCache *cache,
                                            hwaddr addr, const void *buf,
                                            hwaddr len);

static inline bool memory_access_is_direct(MemoryRegion *mr, bool is_write)
{
    if (is_write) {
        return memory_region_is_ram(mr) && !mr->readonly &&
               !mr->rom_device && !memory_region_is_ram_device(mr);
    } else {
        return (memory_region_is_ram(mr) && !memory_region_is_ram_device(mr)) ||
               memory_region_is_romd(mr);
    }
}

/**
 * address_space_read: read from an address space.
 *
 * Return a MemTxResult indicating whether the operation succeeded
 * or failed (eg unassigned memory, device rejected the transaction,
 * IOMMU fault).  Called within RCU critical section.
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @attrs: memory transaction attributes
 * @buf: buffer with the data transferred
 * @len: length of the data transferred
 */
static inline __attribute__((__always_inline__))
MemTxResult address_space_read(AddressSpace *as, hwaddr addr,
                               MemTxAttrs attrs, void *buf,
                               hwaddr len)
{
    MemTxResult result = MEMTX_OK;
    hwaddr l, addr1;
    void *ptr;
    MemoryRegion *mr;
    FlatView *fv;

    if (__builtin_constant_p(len)) {
        if (len) {
            RCU_READ_LOCK_GUARD();
            fv = address_space_to_flatview(as);
            l = len;
            mr = flatview_translate(fv, addr, &addr1, &l, false, attrs);
            if (len == l && memory_access_is_direct(mr, false)) {
                ptr = qemu_map_ram_ptr(mr->ram_block, addr1);
                memcpy(buf, ptr, len);
            } else {
                result = flatview_read_continue(fv, addr, attrs, buf, len,
                                                addr1, l, mr);
            }
        }
    } else {
        result = address_space_read_full(as, addr, attrs, buf, len);
    }
    return result;
}

/**
 * address_space_read_cached: read from a cached RAM region
 *
 * @cache: Cached region to be addressed
 * @addr: address relative to the base of the RAM region
 * @buf: buffer with the data transferred
 * @len: length of the data transferred
 */
static inline MemTxResult
address_space_read_cached(MemoryRegionCache *cache, hwaddr addr,
                          void *buf, hwaddr len)
{
    assert(addr < cache->len && len <= cache->len - addr);
    if (likely(cache->ptr)) {
        memcpy(buf, cache->ptr + addr, len);
        return MEMTX_OK;
    } else {
        return address_space_read_cached_slow(cache, addr, buf, len);
    }
}

/**
 * address_space_write_cached: write to a cached RAM region
 *
 * @cache: Cached region to be addressed
 * @addr: address relative to the base of the RAM region
 * @buf: buffer with the data transferred
 * @len: length of the data transferred
 */
static inline MemTxResult
address_space_write_cached(MemoryRegionCache *cache, hwaddr addr,
                           const void *buf, hwaddr len)
{
    assert(addr < cache->len && len <= cache->len - addr);
    if (likely(cache->ptr)) {
        memcpy(cache->ptr + addr, buf, len);
        return MEMTX_OK;
    } else {
        return address_space_write_cached_slow(cache, addr, buf, len);
    }
}

#ifdef NEED_CPU_H
/* enum device_endian to MemOp.  */
static inline MemOp devend_memop(enum device_endian end)
{
    QEMU_BUILD_BUG_ON(DEVICE_HOST_ENDIAN != DEVICE_LITTLE_ENDIAN &&
                      DEVICE_HOST_ENDIAN != DEVICE_BIG_ENDIAN);

#if defined(HOST_WORDS_BIGENDIAN) != defined(TARGET_WORDS_BIGENDIAN)
    /* Swap if non-host endianness or native (target) endianness */
    return (end == DEVICE_HOST_ENDIAN) ? 0 : MO_BSWAP;
#else
    const int non_host_endianness =
        DEVICE_LITTLE_ENDIAN ^ DEVICE_BIG_ENDIAN ^ DEVICE_HOST_ENDIAN;

    /* In this case, native (target) endianness needs no swap.  */
    return (end == non_host_endianness) ? MO_BSWAP : 0;
#endif
}
#endif

/*
 * Inhibit technologies that require discarding of pages in RAM blocks, e.g.,
 * to manage the actual amount of memory consumed by the VM (then, the memory
 * provided by RAM blocks might be bigger than the desired memory consumption).
 * This *must* be set if:
 * - Discarding parts of a RAM blocks does not result in the change being
 *   reflected in the VM and the pages getting freed.
 * - All memory in RAM blocks is pinned or duplicated, invaldiating any previous
 *   discards blindly.
 * - Discarding parts of a RAM blocks will result in integrity issues (e.g.,
 *   encrypted VMs).
 * Technologies that only temporarily pin the current working set of a
 * driver are fine, because we don't expect such pages to be discarded
 * (esp. based on guest action like balloon inflation).
 *
 * This is *not* to be used to protect from concurrent discards (esp.,
 * postcopy).
 *
 * Returns 0 if successful. Returns -EBUSY if a technology that relies on
 * discards to work reliably is active.
 */
int ram_block_discard_disable(bool state);

/*
 * Inhibit technologies that disable discarding of pages in RAM blocks.
 *
 * Returns 0 if successful. Returns -EBUSY if discards are already set to
 * broken.
 */
int ram_block_discard_require(bool state);

/*
 * Test if discarding of memory in ram blocks is disabled.
 */
bool ram_block_discard_is_disabled(void);

/*
 * Test if discarding of memory in ram blocks is required to work reliably.
 */
bool ram_block_discard_is_required(void);

#endif

#endif
