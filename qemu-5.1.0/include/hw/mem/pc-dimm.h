/*
 * PC DIMM device
 *
 * Copyright ProfitBricks GmbH 2012
 * Copyright (C) 2013-2014 Red Hat Inc
 *
 * Authors:
 *  Vasilis Liaskovitis <vasilis.liaskovitis@profitbricks.com>
 *  Igor Mammedov <imammedo@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_PC_DIMM_H
#define QEMU_PC_DIMM_H

#include "exec/memory.h"
#include "hw/qdev-core.h"

#define TYPE_PC_DIMM "pc-dimm"
#define PC_DIMM(obj) \
    OBJECT_CHECK(PCDIMMDevice, (obj), TYPE_PC_DIMM)
#define PC_DIMM_CLASS(oc) \
    OBJECT_CLASS_CHECK(PCDIMMDeviceClass, (oc), TYPE_PC_DIMM)
#define PC_DIMM_GET_CLASS(obj) \
    OBJECT_GET_CLASS(PCDIMMDeviceClass, (obj), TYPE_PC_DIMM)

#define PC_DIMM_ADDR_PROP "addr"
#define PC_DIMM_SLOT_PROP "slot"
#define PC_DIMM_NODE_PROP "node"
#define PC_DIMM_SIZE_PROP "size"
#define PC_DIMM_MEMDEV_PROP "memdev"

#define PC_DIMM_UNASSIGNED_SLOT -1

/**
 * PCDIMMDevice:
 * @addr: starting guest physical address, where @PCDIMMDevice is mapped.
 *         Default value: 0, means that address is auto-allocated.
 * @node: numa node to which @PCDIMMDevice is attached.
 * @slot: slot number into which @PCDIMMDevice is plugged in.
 *        Default value: -1, means that slot is auto-allocated.
 * @hostmem: host memory backend providing memory for @PCDIMMDevice
 * 
 * QEMU可以指定客户机初始运行时的内存大小以及客户机最大内存大小，以及内存芯片槽的数量（DIMM），之所以QEMU
 * 可以指定最大内存、槽等参数，是因为QEMU可以模拟DIMM的热插拔，客户机操作系统可以和在真实的系统上一样，检测
 * 新内存被插入或者拔出。也就是说，内存热插拔的粒度是DIMM槽（或者说DIMM集合），而不是最小的byte
 * 
 * PCDIMMDevice数据结构是使用QEMU中的面向对象编程模型QOM定义的，对应的对象和类的数据结构如下。通过在QEMU
 * 进程中创建一个新的PCDIMMDevice对象，就可以实现内存的热插拔
 * 值得注意的是，客户机启动时的初始化内存，可能不会被模拟成PCDIMMDevice设备，也就是说，这部分初始化内存不能进行热插拔
 * 
 */
typedef struct PCDIMMDevice {
    /* private */
    DeviceState parent_obj; /* 创建实例 */

    /* public */
    uint64_t addr;
    uint32_t node;  /* numa node 多个CPU如何访问内存的机制 */
    int32_t slot;   /* slot: 狭槽、卡槽，表示slot编号 */
    HostMemoryBackend *hostmem;
} PCDIMMDevice;

/**
 * PCDIMMDeviceClass:
 * @realize: called after common dimm is realized so that the dimm based
 * devices get the chance to do specified operations.
 * @get_vmstate_memory_region: returns #MemoryRegion which indicates the
 * memory of @dimm should be kept during live migration. Will not fail
 * after the device was realized.
 */
typedef struct PCDIMMDeviceClass {
    /* private */
    DeviceClass parent_class;   /* 实例所对应的class */

    /* public */
    void (*realize)(PCDIMMDevice *dimm, Error **errp);
    MemoryRegion *(*get_vmstate_memory_region)(PCDIMMDevice *dimm,
                                               Error **errp);
} PCDIMMDeviceClass;

void pc_dimm_pre_plug(PCDIMMDevice *dimm, MachineState *machine,
                      const uint64_t *legacy_align, Error **errp);
void pc_dimm_plug(PCDIMMDevice *dimm, MachineState *machine, Error **errp);
void pc_dimm_unplug(PCDIMMDevice *dimm, MachineState *machine);
#endif
