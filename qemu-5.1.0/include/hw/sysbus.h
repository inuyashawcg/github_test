#ifndef HW_SYSBUS_H
#define HW_SYSBUS_H

/* Devices attached directly to the main system bus.  */

#include "hw/qdev-core.h"
#include "exec/memory.h"

#define QDEV_MAX_MMIO 32
#define QDEV_MAX_PIO 32

#define TYPE_SYSTEM_BUS "System"
#define SYSTEM_BUS(obj) OBJECT_CHECK(BusState, (obj), TYPE_SYSTEM_BUS)

typedef struct SysBusDevice SysBusDevice;

#define TYPE_SYS_BUS_DEVICE "sys-bus-device"
#define SYS_BUS_DEVICE(obj) \
     OBJECT_CHECK(SysBusDevice, (obj), TYPE_SYS_BUS_DEVICE)
#define SYS_BUS_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(SysBusDeviceClass, (klass), TYPE_SYS_BUS_DEVICE)
#define SYS_BUS_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(SysBusDeviceClass, (obj), TYPE_SYS_BUS_DEVICE)

/**
 * SysBusDeviceClass:
 *
 * SysBusDeviceClass is not overriding #DeviceClass.realize, so derived
 * classes overriding it are not required to invoke its implementation.
 * SysBusDeviceClass未重写#设备类实现，因此不需要重写它的派生类来调用它的实现
 */

#define SYSBUS_DEVICE_GPIO_IRQ "sysbus-irq"

typedef struct SysBusDeviceClass {
    /*< private >*/
    /* SysBusDeviceClass首先属于一个设备，所以父类为DeviceClass */
    DeviceClass parent_class;   

    /*
     * Let the sysbus device format its own non-PIO, non-MMIO unit address.
     *
     * Sometimes a class of SysBusDevices has neither MMIO nor PIO resources,
     * yet instances of it would like to distinguish themselves, in
     * OpenFirmware device paths, from other instances of the same class on the
     * sysbus. For that end we expose this callback.
     * 有时候，一类sysbus设备既没有MMIO也没有PIO资源，但是它的实例希望在OpenFirmware设备
     * 路径中与sysbus上同一类的其他实例区分开来。为此，我们公开这个回调
     * 
     * The implementation is not supposed to change *@dev, or incur other
     * observable change. 该实现不应该改变*@dev，或者引起其他可观察到的变化
     *
     * The function returns a dynamically allocated string. On error, NULL
     * should be returned; the unit address portion of the OFW node will be
     * omitted then. (This is not considered a fatal error.)
     * 函数返回一个动态分配的字符串。出现错误时，应返回NULL；然后将省略OFW节点的单元地址部分。
     * （这不是致命错误。）
     * explicit: 明确的
     */
    char *(*explicit_ofw_unit_address)(const SysBusDevice *dev);    /* 用于返回ofw单元地址 */
    void (*connect_irq_notifier)(SysBusDevice *dev, qemu_irq irq);
} SysBusDeviceClass;

/* sysbus = parent obj + resource */
struct SysBusDevice {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    int num_mmio;   /* mmio区域的个数 */
    struct {
        hwaddr addr;
        MemoryRegion *memory;
    } mmio[QDEV_MAX_MMIO];  /* 管理IO内存 */

    int num_pio;
    uint32_t pio[QDEV_MAX_PIO];
};

typedef void FindSysbusDeviceFunc(SysBusDevice *sbdev, void *opaque);

void sysbus_init_mmio(SysBusDevice *dev, MemoryRegion *memory);
MemoryRegion *sysbus_mmio_get_region(SysBusDevice *dev, int n);
void sysbus_init_irq(SysBusDevice *dev, qemu_irq *p);
void sysbus_pass_irq(SysBusDevice *dev, SysBusDevice *target);
void sysbus_init_ioports(SysBusDevice *dev, uint32_t ioport, uint32_t size);

/* irq: 中断相关 */
bool sysbus_has_irq(SysBusDevice *dev, int n);
bool sysbus_has_mmio(SysBusDevice *dev, unsigned int n);
void sysbus_connect_irq(SysBusDevice *dev, int n, qemu_irq irq);
bool sysbus_is_irq_connected(SysBusDevice *dev, int n);
qemu_irq sysbus_get_connected_irq(SysBusDevice *dev, int n);

/* 内存映射 */
void sysbus_mmio_map(SysBusDevice *dev, int n, hwaddr addr);
void sysbus_mmio_map_overlap(SysBusDevice *dev, int n, hwaddr addr,
                             int priority);
void sysbus_mmio_unmap(SysBusDevice *dev, int n);
void sysbus_add_io(SysBusDevice *dev, hwaddr addr,
                   MemoryRegion *mem);
MemoryRegion *sysbus_address_space(SysBusDevice *dev);

/* 初始化相关 */
bool sysbus_realize(SysBusDevice *dev, Error **errp);
bool sysbus_realize_and_unref(SysBusDevice *dev, Error **errp);

/* Call func for every dynamically created sysbus device in the system 
    动态创建的sysbus？ 
*/
void foreach_dynamic_sysbus_device(FindSysbusDeviceFunc *func, void *opaque);

/* Legacy helper function for creating devices.  
    用于创建设备的旧助手函数
*/
DeviceState *sysbus_create_varargs(const char *name,
                                 hwaddr addr, ...);

static inline DeviceState *sysbus_create_simple(const char *name,
                                              hwaddr addr,
                                              qemu_irq irq)
{
    return sysbus_create_varargs(name, addr, irq, NULL);
}

#endif /* HW_SYSBUS_H */
