#ifndef QDEV_CORE_H
#define QDEV_CORE_H

#include "qemu/queue.h"
#include "qemu/bitmap.h"
#include "qom/object.h"
#include "hw/hotplug.h"
#include "hw/resettable.h"

enum {
    DEV_NVECTORS_UNSPECIFIED = -1,
};

#define TYPE_DEVICE "device"
#define DEVICE(obj) OBJECT_CHECK(DeviceState, (obj), TYPE_DEVICE)
#define DEVICE_CLASS(klass) OBJECT_CLASS_CHECK(DeviceClass, (klass), TYPE_DEVICE)
#define DEVICE_GET_CLASS(obj) OBJECT_GET_CLASS(DeviceClass, (obj), TYPE_DEVICE)

/* device category: 设备类别 */
typedef enum DeviceCategory {
    DEVICE_CATEGORY_BRIDGE,
    DEVICE_CATEGORY_USB,
    DEVICE_CATEGORY_STORAGE,
    DEVICE_CATEGORY_NETWORK,
    DEVICE_CATEGORY_INPUT,
    DEVICE_CATEGORY_DISPLAY,
    DEVICE_CATEGORY_SOUND,
    DEVICE_CATEGORY_MISC,
    DEVICE_CATEGORY_CPU,
    DEVICE_CATEGORY_MAX
} DeviceCategory;

typedef void (*DeviceRealize)(DeviceState *dev, Error **errp);
typedef void (*DeviceUnrealize)(DeviceState *dev);
typedef void (*DeviceReset)(DeviceState *dev);
typedef void (*BusRealize)(BusState *bus, Error **errp);
typedef void (*BusUnrealize)(BusState *bus);

/**
 * DeviceClass:
 * @props: Properties accessing state fields.
 * @realize: Callback function invoked when the #DeviceState:realized
 * property is changed to %true. 回调
 * @unrealize: Callback function invoked when the #DeviceState:realized
 * property is changed to %false. 回调
 * @hotpluggable: indicates if #DeviceClass is hotpluggable, available
 * as readonly "hotpluggable" property of #DeviceState instance
 * 指示设备类是否可热插拔，可作为设备状态实例的只读“热插拔”属性使用
 *
 * # Realization #
 * Devices are constructed in two stages,
 * 1) object instantiation via object_initialize() and
 * 2) device realization via #DeviceState:realized property.
 * The former may not fail (and must not abort or exit, since it is called
 * during device introspection already), and the latter may return error
 * information to the caller and must be re-entrant.
 * Trivial field initializations should go into #TypeInfo.instance_init.
 * Operations depending on @props static properties should go into @realize.
 * After successful realization, setting static properties will fail.
 * 琐碎的字段初始化应该进入#TypeInfo.instance_init类型信息.  依赖于@props静态属性的
 * 操作应该进入@realize。成功实现后，设置静态属性将失败。
 *
 * As an interim step, the #DeviceState:realized property can also be
 * set with qdev_realize().
 * In the future, devices will propagate this state change to their children
 * and along busses they expose. 
 * 将来，设备会将这种状态更改传播到它们的子代以及它们暴露的总线上
 * 
 * The point in time will be deferred to machine creation, so that values
 * set in @realize will not be introspectable beforehand. Therefore devices
 * must not create children during @realize; they should initialize them via
 * object_initialize() in their own #TypeInfo.instance_init and forward the
 * realization events appropriately.
 *
 * Any type may override the @realize and/or @unrealize callbacks but needs
 * to call the parent type's implementation if keeping their functionality
 * is desired. Refer to QOM documentation for further discussion and examples.
 *
 * <note>
 *   <para>
 * Since TYPE_DEVICE doesn't implement @realize and @unrealize, types
 * derived directly from it need not call their parent's @realize and
 * @unrealize.
 * For other types consult the documentation and implementation of the
 * respective parent types.
 *   </para>
 * </note>
 *
 * # Hiding a device #
 * To hide a device, a DeviceListener function should_be_hidden() needs to
 * be registered.
 * It can be used to defer adding a device and therefore hide it from the
 * guest. The handler registering to this DeviceListener can save the QOpts
 * passed to it for re-using it later and must return that it wants the device
 * to be/remain hidden or not. When the handler function decides the device
 * shall not be hidden it will be added in qdev_device_add() and
 * realized as any other device. Otherwise qdev_device_add() will return early
 * without adding the device. The guest will not see a "hidden" device
 * until it was marked don't hide and qdev_device_add called again. 设备向用于隐藏
 * 并且可以取消隐藏
 *
 */
typedef struct DeviceClass {
    /*< private >*/
    ObjectClass parent_class;   /* ObjectClass 放到起始位置，强制类型转换-派生 */
    /*< public >*/

    /* 一个数组对应一个设备类别 */
    DECLARE_BITMAP(categories, DEVICE_CATEGORY_MAX);
    const char *fw_name;    
    const char *desc;   /* 设备描述 */

    /*
     * The underscore at the end ensures a compile-time error if someone
     * assigns to dc->props instead of using device_class_set_props.
     * 管理 Property 的数组，说明一个设备可以包含多种属性？
     */
    Property *props_;

    /*
     * Can this device be instantiated with -device / device_add?
     * All devices should support instantiation with device_add, and
     * this flag should not exist.  But we're not there, yet.  Some
     * devices fail to instantiate with cryptic error messages.
     * Others instantiate, but don't work.  Exposing users to such
     * behavior would be cruel; clearing this flag will protect them.
     * It should never be cleared without a comment explaining why it
     * is cleared.
     * 所有设备都应支持使用device_add进行实例化，此标志不应存在。但我们还没到那儿。
     * 有些设备无法实例化，并显示神秘的错误消息。其他的实例化，但不起作用。将用户暴露
     * 在这种行为下是残忍的；清除此标志将保护他们。在没有解释清除原因的注释的情况下，
     * 绝对不能清除它。
     * TODO remove once we're there
     */
    bool user_creatable;    /* 用户可以创建的标志？ */
    bool hotpluggable;  /* 热插拔 */

    /* callbacks */
    /*
     * Reset method here is deprecated and replaced by methods in the
     * resettable class interface to implement a multi-phase reset.
     * 这里的Reset方法已弃用，并被resetable类接口中的方法替换，以实现多阶段重置(函数重载？？)
     * 
     * TODO: remove once every reset callback is unused
     */
    DeviceReset reset;
    DeviceRealize realize;
    DeviceUnrealize unrealize;

    /* device state */
    const VMStateDescription *vmsd;

    /* Private to qdev / bus.  */
    const char *bus_type;
} DeviceClass;

typedef struct NamedGPIOList NamedGPIOList;

struct NamedGPIOList {
    char *name;
    qemu_irq *in;   /* 中断 */

    /* 输入/输出引脚数量？？ */
    int num_in;
    int num_out;

    /* GPIO也是统一管理？ - struct DeviceState 中有相应的队列 */
    QLIST_ENTRY(NamedGPIOList) node;
};

typedef struct Clock Clock;
typedef struct NamedClockList NamedClockList;

struct NamedClockList {
    char *name;
    Clock *clock;
    bool output;
    bool alias;
    QLIST_ENTRY(NamedClockList) node;
};

/**
 * DeviceState:
 * @realized: Indicates whether the device has been fully constructed.
 * @reset: ResettableState for the device; handled by Resettable interface.
 *
 * This structure should not be accessed directly.  We declare it here
 * so that it can be embedded in individual device state structures.
 * 不应直接访问此结构。我们在这里声明它，以便它可以嵌入到单个设备状态结构中？
 */
struct DeviceState {
    /*< private >*/
    Object parent_obj;  /* object/state 应该都是用于表示实例 */
    /*< public >*/

    const char *id;
    char *canonical_path;   /* 规范路径 */
    bool realized;  /* 是否已完成初始化 */
    bool pending_deleted_event; /* 挂起的已删除事件 */
    QemuOpts *opts; /* 开启命令选项？？ */
    int hotplugged;
    bool allow_unplug_during_migration; /* 允许在迁移期间拔出? */
    BusState *parent_bus;   /* 父总线 */

    /* 三种不同类型的队列管理不同的设备 */
    QLIST_HEAD(, NamedGPIOList) gpios;
    QLIST_HEAD(, NamedClockList) clocks;
    QLIST_HEAD(, BusState) child_bus;
    int num_child_bus;
    int instance_id_alias;
    int alias_required_for_version;
    ResettableState reset;  /* 复位操作相关 */
};

struct DeviceListener {
    void (*realize)(DeviceListener *listener, DeviceState *dev);
    void (*unrealize)(DeviceListener *listener, DeviceState *dev);
    /*
     * This callback is called upon init of the DeviceState and allows to
     * inform qdev that a device should be hidden, depending on the device
     * opts, for example, to hide a standby device.
     * 此回调在DeviceState的init时调用，并允许通知qdev应该隐藏一个设备，这取决于设备
     * 的选择，例如，隐藏一个备用设备
     */
    int (*should_be_hidden)(DeviceListener *listener, QemuOpts *device_opts);
    QTAILQ_ENTRY(DeviceListener) link;  /* 也是通过链表来维护 */
};

#define TYPE_BUS "bus"
#define BUS(obj) OBJECT_CHECK(BusState, (obj), TYPE_BUS)
#define BUS_CLASS(klass) OBJECT_CLASS_CHECK(BusClass, (klass), TYPE_BUS)
#define BUS_GET_CLASS(obj) OBJECT_GET_CLASS(BusClass, (obj), TYPE_BUS)

/*
    BusClass和 BusState分别代表bus的基类和对象
*/
struct BusClass {
    ObjectClass parent_class;

    /* FIXME first arg should be BusState */
    void (*print_dev)(Monitor *mon, DeviceState *dev, int indent);  /* 用于monitor监控调试使用 */
    char *(*get_dev_path)(DeviceState *dev);    /* 用户获取总线上设备的路径 */
    /*
     * This callback is used to create Open Firmware device path in accordance
     * with OF spec http://forthworks.com/standards/of1275.pdf. Individual bus
     * bindings can be found at http://playground.sun.com/1275/bindings/.
     */
    char *(*get_fw_dev_path)(DeviceState *dev);     /* 用于支持of规范 */
    void (*reset)(BusState *bus);   /* 用于重置bus */
    BusRealize realize;
    BusUnrealize unrealize;

    /* maximum devices allowed on the bus, 0: no limit. 总线上最大支持的设备数 */
    int max_dev;
    /* number of automatically allocated bus ids (e.g. ide.0) 用于给子设备命名 */
    int automatic_ids;
};

/* BusChild用于描述总线上的一个设备实例 */
typedef struct BusChild {
    DeviceState *child; /* 设备实例 */
    int index;  /* 表示是第几个设备 */
    QTAILQ_ENTRY(BusChild) sibling; /* 用于管理挂载到总线的children节点 */
} BusChild;

#define QDEV_HOTPLUG_HANDLER_PROPERTY "hotplug-handler"

/**
 * BusState: BusState为总线的实例，obj为基类
 * @hotplug_handler: link to a hotplug handler associated with bus.
 * @reset: ResettableState for the bus; handled by Resettable interface.
 */
struct BusState {
    Object obj;
    DeviceState *parent;    /* 表示总线的父设备，一般为总线桥设备 */
    char *name; /* 总线名称 */
    HotplugHandler *hotplug_handler;    /* 热插拔处理接口 */
    int max_index;  /* 当前设备最大索引 */
    bool realized;  /* 是否初始化完成 */
    int num_children;   /* 子设备数量 */
    QTAILQ_HEAD(, BusChild) children;   /* 用于存放总线上的子设备 */
    QLIST_ENTRY(BusState) sibling;  /* 用于挂载到父节点上 */
    ResettableState reset;  /* device实例相关结构体大概率会有这个 */
};

/**
 * Property:
 * @set_default: true if the default value should be set from @defval,
 *    in which case @info->set_default_value must not be NULL
 *    (if false then no default value is set by the property system
 *     and the field retains whatever value it was given by instance_init).
 * @defval: default value for the property. This is used only if @set_default
 *     is true.
 */
struct Property {
    const char   *name; /* 属性名称 */
    const PropertyInfo *info;   /* 属性信息 */
    ptrdiff_t    offset;
    uint8_t      bitnr;
    /*
        如果要把 defval 设置为默认值，那么 set_default 要设置为true，并且 set_default_value
        函数不能为NULL
    */
    bool         set_default;
    union {
        int64_t i;
        uint64_t u;
    } defval;

    /* 
        Property为数组中的entry？？ 
        DeviceClass中包含有一个 *props_ 成员，表示的应该是Property数组
    */
    int          arrayoffset;   
    const PropertyInfo *arrayinfo;
    int          arrayfieldsize;
    const char   *link_type;
};

struct PropertyInfo {
    const char *name;   /* 为了跟 Property->name 对应？？*/
    const char *description;
    const QEnumLookup *enum_table;
    int (*print)(DeviceState *dev, Property *prop, char *dest, size_t len);

    /* 该函数关联 Property 属性初始值的设置 */
    void (*set_default_value)(ObjectProperty *op, const Property *prop);
    void (*create)(ObjectClass *oc, Property *prop);

    /* 三个回调函数，用于获取/设置/释放属性信息 */
    ObjectPropertyAccessor *get;
    ObjectPropertyAccessor *set;
    ObjectPropertyRelease *release;
};

/**
 * GlobalProperty:
 * @used: Set to true if property was used when initializing a device.
 * @optional: If set to true, GlobalProperty will be skipped without errors
 *            if the property doesn't exist.
 *
 * An error is fatal for non-hotplugged devices, when the global is applied.
 */
typedef struct GlobalProperty {
    const char *driver;
    const char *property;
    const char *value;
    bool used;  /* 表示已经应用到了某个设备的属性设置 */
    bool optional;
} GlobalProperty;

static inline void
compat_props_add(GPtrArray *arr,
                 GlobalProperty props[], size_t nelem)
{
    int i;
    for (i = 0; i < nelem; i++) {
        g_ptr_array_add(arr, (void *)&props[i]);
    }
}

/*** Board API.  This should go away once we have a machine config file.  ***/
/* 当我们拥有机器配置文件的时候，这个就会走开；意思是我们也可以通过配置文件来提供API？ */
/**
 * qdev_new: Create a device on the heap
 * @name: device type to create (we assert() that this type exists)
 * 参数-name 用来表示设备类型？？并且如果已经拥有这个类型的话，要报警？？
 *
 * This only allocates the memory and initializes the device state
 * structure, ready for the caller to set properties if they wish.
 * The device still needs to be realized.
 * The returned object has a reference count of 1.
 */
DeviceState *qdev_new(const char *name);
/**
 * qdev_try_new: Try to create a device on the heap
 * @name: device type to create
 *
 * This is like qdev_new(), except it returns %NULL when type @name
 * does not exist, rather than asserting.
 */
DeviceState *qdev_try_new(const char *name);
/**
 * qdev_realize: Realize @dev.
 * @dev: device to realize
 * @bus: bus to plug it into (may be NULL)
 * @errp: pointer to error object
 *
 * "Realize" the device, i.e. perform the second phase of device
 * initialization.
 * @dev must not be plugged into a bus already. @dev不能已经插入总线
 * If @bus, plug @dev into @bus.  This takes a reference to @dev.
 * If @dev has no QOM parent, make one up, taking another reference.
 * On success, return true.
 * On failure, store an error through @errp and return false.
 *
 * If you created @dev using qdev_new(), you probably want to use
 * qdev_realize_and_unref() instead.
 */
bool qdev_realize(DeviceState *dev, BusState *bus, Error **errp);
/**
 * qdev_realize_and_unref: Realize @dev and drop a reference
 * @dev: device to realize
 * @bus: bus to plug it into (may be NULL)
 * @errp: pointer to error object
 *
 * Realize @dev and drop a reference.
 * This is like qdev_realize(), except the caller must hold a
 * (private) reference, which is dropped on return regardless of
 * success or failure.  Intended use::
 *
 *     dev = qdev_new();
 *     [...]
 *     qdev_realize_and_unref(dev, bus, errp);
 *
 * Now @dev can go away without further ado.
 *
 * If you are embedding the device into some other QOM device and
 * initialized it via some variant on object_initialize_child() then
 * do not use this function, because that family of functions arrange
 * for the only reference to the child device to be held by the parent
 * via the child<> property, and so the reference-count-drop done here
 * would be incorrect. For that use case you want qdev_realize().
 */
bool qdev_realize_and_unref(DeviceState *dev, BusState *bus, Error **errp);
/**
 * qdev_unrealize: Unrealize a device
 * @dev: device to unrealize
 *
 * This function will "unrealize" a device, which is the first phase
 * of correctly destroying a device that has been realized. It will:
 *
 *  - unrealize any child buses by calling qbus_unrealize()
 *    (this will recursively unrealize any devices on those buses)
 *  - call the the unrealize method of @dev
 *
 * The device can then be freed by causing its reference count to go
 * to zero.
 *
 * Warning: most devices in QEMU do not expect to be unrealized.  Only
 * devices which are hot-unpluggable should be unrealized (as part of
 * the unplugging process); all other devices are expected to last for
 * the life of the simulation and should not be unrealized and freed.
 */
void qdev_unrealize(DeviceState *dev);
void qdev_set_legacy_instance_id(DeviceState *dev, int alias_id,
                                 int required_for_version);
HotplugHandler *qdev_get_bus_hotplug_handler(DeviceState *dev);
HotplugHandler *qdev_get_machine_hotplug_handler(DeviceState *dev);
bool qdev_hotplug_allowed(DeviceState *dev, Error **errp);
/**
 * qdev_get_hotplug_handler: Get handler responsible for device wiring
 *
 * Find HOTPLUG_HANDLER for @dev that provides [pre|un]plug callbacks for it.
 *
 * Note: in case @dev has a parent bus, it will be returned as handler unless
 * machine handler overrides it.
 *
 * Returns: pointer to object that implements TYPE_HOTPLUG_HANDLER interface
 *          or NULL if there aren't any.
 */
HotplugHandler *qdev_get_hotplug_handler(DeviceState *dev);
void qdev_unplug(DeviceState *dev, Error **errp);
void qdev_simple_device_unplug_cb(HotplugHandler *hotplug_dev,
                                  DeviceState *dev, Error **errp);
void qdev_machine_creation_done(void);
bool qdev_machine_modified(void);

/**
 * qdev_get_gpio_in: Get one of a device's anonymous input GPIO lines
 * 获取设备的匿名输入GPIO line之一
 * 
 * @dev: Device whose GPIO we want
 * @n: Number of the anonymous GPIO line (which must be in range)
 * n 应该就相当于是gpio引脚的index
 * 
 * Returns the qemu_irq corresponding to an anonymous input GPIO line
 * (which the device has set up with qdev_init_gpio_in()). The index
 * @n of the GPIO line must be valid (i.e. be at least 0 and less than
 * the total number of anonymous input GPIOs the device has); this
 * function will assert() if passed an invalid index.
 *
 * This function is intended to be used by board code or SoC "container"
 * device models to wire up the GPIO lines; usually the return value
 * will be passed to qdev_connect_gpio_out() or a similar function to
 * connect another device's output GPIO line to this input.
 * 此函数用于板代码或SoC“容器”设备模型连接GPIO线；通常返回值将传递给
 * qdev_connect_GPIO_out（）或类似函数，以将另一个设备的输出GPIO线连接到此输入
 *
 * For named input GPIO lines, use qdev_get_gpio_in_named().
 */
qemu_irq qdev_get_gpio_in(DeviceState *dev, int n);
/**
 * qdev_get_gpio_in_named: Get one of a device's named input GPIO lines
 * @dev: Device whose GPIO we want
 * @name: Name of the input GPIO array
 * @n: Number of the GPIO line in that array (which must be in range)
 *
 * Returns the qemu_irq corresponding to a named input GPIO line
 * (which the device has set up with qdev_init_gpio_in_named()).
 * The @name string must correspond to an input GPIO array which exists on
 * the device, and the index @n of the GPIO line must be valid (i.e.
 * be at least 0 and less than the total number of input GPIOs in that
 * array); this function will assert() if passed an invalid name or index.
 *
 * For anonymous input GPIO lines, use qdev_get_gpio_in().
 */
qemu_irq qdev_get_gpio_in_named(DeviceState *dev, const char *name, int n);

/**
 * qdev_connect_gpio_out: Connect one of a device's anonymous output GPIO lines
 * @dev: Device whose GPIO to connect
 * @n: Number of the anonymous output GPIO line (which must be in range)
 * @pin: qemu_irq to connect the output line to
 *
 * This function connects an anonymous output GPIO line on a device
 * up to an arbitrary qemu_irq, so that when the device asserts that
 * output GPIO line, the qemu_irq's callback is invoked.
 * 此函数将设备上的匿名输出GPIO线连接到任意qemu_irq，以便当设备中断该输出GPIO线时，
 * 将调用qemu_irq的回调
 * 
 * The index @n of the GPIO line must be valid (i.e. be at least 0 and
 * less than the total number of anonymous output GPIOs the device has
 * created with qdev_init_gpio_out()); otherwise this function will assert().
 *
 * Outbound GPIO lines can be connected to any qemu_irq, but the common
 * case is connecting them to another device's inbound GPIO line, using
 * the qemu_irq returned by qdev_get_gpio_in() or qdev_get_gpio_in_named().
 *
 * It is not valid to try to connect one outbound GPIO to multiple
 * qemu_irqs at once, or to connect multiple outbound GPIOs to the
 * same qemu_irq. (Warning: there is no assertion or other guard to
 * catch this error: the model will just not do the right thing.)
 * Instead, for fan-out you can use the TYPE_IRQ_SPLIT device: connect
 * a device's outbound GPIO to the splitter's input, and connect each
 * of the splitter's outputs to a different device.  For fan-in you
 * can use the TYPE_OR_IRQ device, which is a model of a logical OR
 * gate with multiple inputs and one output.
 *
 * For named output GPIO lines, use qdev_connect_gpio_out_named().
 */
void qdev_connect_gpio_out(DeviceState *dev, int n, qemu_irq pin);
/**
 * qdev_connect_gpio_out: Connect one of a device's anonymous output GPIO lines
 * @dev: Device whose GPIO to connect
 * @name: Name of the output GPIO array
 * @n: Number of the anonymous output GPIO line (which must be in range)
 * @pin: qemu_irq to connect the output line to
 *
 * This function connects an anonymous output GPIO line on a device
 * up to an arbitrary qemu_irq, so that when the device asserts that
 * output GPIO line, the qemu_irq's callback is invoked.
 * The @name string must correspond to an output GPIO array which exists on
 * the device, and the index @n of the GPIO line must be valid (i.e.
 * be at least 0 and less than the total number of input GPIOs in that
 * array); this function will assert() if passed an invalid name or index.
 *
 * Outbound GPIO lines can be connected to any qemu_irq, but the common
 * case is connecting them to another device's inbound GPIO line, using
 * the qemu_irq returned by qdev_get_gpio_in() or qdev_get_gpio_in_named().
 *
 * It is not valid to try to connect one outbound GPIO to multiple
 * qemu_irqs at once, or to connect multiple outbound GPIOs to the
 * same qemu_irq; see qdev_connect_gpio_out() for details.
 *
 * For named output GPIO lines, use qdev_connect_gpio_out_named().
 */
void qdev_connect_gpio_out_named(DeviceState *dev, const char *name, int n,
                                 qemu_irq pin);
/**
 * qdev_get_gpio_out_connector: Get the qemu_irq connected to an output GPIO
 * @dev: Device whose output GPIO we are interested in
 * @name: Name of the output GPIO array
 * @n: Number of the output GPIO line within that array
 *
 * Returns whatever qemu_irq is currently connected to the specified
 * output GPIO line of @dev. This will be NULL if the output GPIO line
 * has never been wired up to the anything.  Note that the qemu_irq
 * returned does not belong to @dev -- it will be the input GPIO or
 * IRQ of whichever device the board code has connected up to @dev's
 * output GPIO.
 *
 * You probably don't need to use this function -- it is used only
 * by the platform-bus subsystem.
 */
qemu_irq qdev_get_gpio_out_connector(DeviceState *dev, const char *name, int n);
/**
 * qdev_intercept_gpio_out: Intercept an existing GPIO connection
 * @dev: Device to intercept the outbound GPIO line from
 * @icpt: New qemu_irq to connect instead
 * @name: Name of the output GPIO array
 * @n: Number of the GPIO line in the array
 *
 * This function is provided only for use by the qtest testing framework
 * and is not suitable for use in non-testing parts of QEMU.
 *
 * This function breaks an existing connection of an outbound GPIO
 * line from @dev, and replaces it with the new qemu_irq @icpt, as if
 * ``qdev_connect_gpio_out_named(dev, icpt, name, n)`` had been called.
 * The previously connected qemu_irq is returned, so it can be restored
 * by a second call to qdev_intercept_gpio_out() if desired.
 */
qemu_irq qdev_intercept_gpio_out(DeviceState *dev, qemu_irq icpt,
                                 const char *name, int n);

BusState *qdev_get_child_bus(DeviceState *dev, const char *name);

/*** Device API.  ***/

/**
 * qdev_init_gpio_in: create an array of anonymous input GPIO lines
 * @dev: Device to create input GPIOs for
 * @handler: Function to call when GPIO line value is set
 * @n: Number of GPIO lines to create
 *
 * Devices should use functions in the qdev_init_gpio_in* family in
 * their instance_init or realize methods to create any input GPIO
 * lines they need. There is no functional difference between
 * anonymous and named GPIO lines. Stylistically, named GPIOs are
 * preferable (easier to understand at callsites) unless a device
 * has exactly one uniform kind of GPIO input whose purpose is obvious.
 * Note that input GPIO lines can serve as 'sinks' for IRQ lines.
 *
 * See qdev_get_gpio_in() for how code that uses such a device can get
 * hold of an input GPIO line to manipulate it.
 */
void qdev_init_gpio_in(DeviceState *dev, qemu_irq_handler handler, int n);
/**
 * qdev_init_gpio_out: create an array of anonymous output GPIO lines
 * @dev: Device to create output GPIOs for
 * @pins: Pointer to qemu_irq or qemu_irq array for the GPIO lines
 * @n: Number of GPIO lines to create
 *
 * Devices should use functions in the qdev_init_gpio_out* family
 * in their instance_init or realize methods to create any output
 * GPIO lines they need. There is no functional difference between
 * anonymous and named GPIO lines. Stylistically, named GPIOs are
 * preferable (easier to understand at callsites) unless a device
 * has exactly one uniform kind of GPIO output whose purpose is obvious.
 *
 * The @pins argument should be a pointer to either a "qemu_irq"
 * (if @n == 1) or a "qemu_irq []" array (if @n > 1) in the device's
 * state structure. The device implementation can then raise and
 * lower the GPIO line by calling qemu_set_irq(). (If anything is
 * connected to the other end of the GPIO this will cause the handler
 * function for that input GPIO to be called.)
 *
 * See qdev_connect_gpio_out() for how code that uses such a device
 * can connect to one of its output GPIO lines.
 */
void qdev_init_gpio_out(DeviceState *dev, qemu_irq *pins, int n);
/**
 * qdev_init_gpio_out: create an array of named output GPIO lines
 * @dev: Device to create output GPIOs for
 * @pins: Pointer to qemu_irq or qemu_irq array for the GPIO lines
 * @name: Name to give this array of GPIO lines
 * @n: Number of GPIO lines to create
 *
 * Like qdev_init_gpio_out(), but creates an array of GPIO output lines
 * with a name. Code using the device can then connect these GPIO lines
 * using qdev_connect_gpio_out_named().
 */
void qdev_init_gpio_out_named(DeviceState *dev, qemu_irq *pins,
                              const char *name, int n);
/**
 * qdev_init_gpio_in_named_with_opaque: create an array of input GPIO lines
 *   for the specified device
 *
 * @dev: Device to create input GPIOs for
 * @handler: Function to call when GPIO line value is set
 * @opaque: Opaque data pointer to pass to @handler
 * @name: Name of the GPIO input (must be unique for this device)
 * @n: Number of GPIO lines in this input set
 */
void qdev_init_gpio_in_named_with_opaque(DeviceState *dev,
                                         qemu_irq_handler handler,
                                         void *opaque,
                                         const char *name, int n);

/**
 * qdev_init_gpio_in_named: create an array of input GPIO lines
 *   for the specified device
 *
 * Like qdev_init_gpio_in_named_with_opaque(), but the opaque pointer
 * passed to the handler is @dev (which is the most commonly desired behaviour).
 */
static inline void qdev_init_gpio_in_named(DeviceState *dev,
                                           qemu_irq_handler handler,
                                           const char *name, int n)
{
    qdev_init_gpio_in_named_with_opaque(dev, handler, dev, name, n);
}

/**
 * qdev_pass_gpios: create GPIO lines on container which pass through to device
 * 在通过设备的容器上创建GPIO行
 * 
 * @dev: Device which has GPIO lines
 * @container: Container device which needs to expose them
 * @name: Name of GPIO array to pass through (NULL for the anonymous GPIO array)
 *
 * In QEMU, complicated devices like SoCs are often modelled with a
 * "container" QOM device which itself contains other QOM devices and
 * which wires them up appropriately. This function allows the container
 * to create GPIO arrays on itself which simply pass through to a GPIO
 * array of one of its internal devices.
 *
 * If @dev has both input and output GPIOs named @name then both will
 * be passed through. It is not possible to pass a subset of the array
 * with this function.
 *
 * To users of the container device, the GPIO array created on @container
 * behaves exactly like any other.
 */
void qdev_pass_gpios(DeviceState *dev, DeviceState *container,
                     const char *name);

BusState *qdev_get_parent_bus(DeviceState *dev);

/*** BUS API. ***/

DeviceState *qdev_find_recursive(BusState *bus, const char *id);

/* Returns 0 to walk children, > 0 to skip walk, < 0 to terminate(终止) walk. */
typedef int (qbus_walkerfn)(BusState *bus, void *opaque);
typedef int (qdev_walkerfn)(DeviceState *dev, void *opaque);

void qbus_create_inplace(void *bus, size_t size, const char *typename,
                         DeviceState *parent, const char *name);
BusState *qbus_create(const char *typename, DeviceState *parent, const char *name);
bool qbus_realize(BusState *bus, Error **errp);
void qbus_unrealize(BusState *bus);

/* Returns > 0 if either devfn or busfn skip walk somewhere in cursion,
 *         < 0 if either devfn or busfn terminate walk somewhere in cursion,
 *           0 otherwise. */
int qbus_walk_children(BusState *bus,
                       qdev_walkerfn *pre_devfn, qbus_walkerfn *pre_busfn,
                       qdev_walkerfn *post_devfn, qbus_walkerfn *post_busfn,
                       void *opaque);
int qdev_walk_children(DeviceState *dev,
                       qdev_walkerfn *pre_devfn, qbus_walkerfn *pre_busfn,
                       qdev_walkerfn *post_devfn, qbus_walkerfn *post_busfn,
                       void *opaque);

/**
 * @qdev_reset_all:
 * Reset @dev. See @qbus_reset_all() for more details.
 *
 * Note: This function is deprecated and will be removed when it becomes unused.
 * Please use device_cold_reset() now.
 */
void qdev_reset_all(DeviceState *dev);
void qdev_reset_all_fn(void *opaque);

/**
 * @qbus_reset_all:
 * @bus: Bus to be reset.
 *
 * Reset @bus and perform a bus-level ("hard") reset of all devices connected
 * to it, including recursive processing of all buses below @bus itself.  A
 * hard reset means that qbus_reset_all will reset all state of the device.
 * For PCI devices, for example, this will include the base address registers
 * or configuration space.
 *
 * Note: This function is deprecated and will be removed when it becomes unused.
 * Please use bus_cold_reset() now.
 */
void qbus_reset_all(BusState *bus);
void qbus_reset_all_fn(void *opaque);

/**
 * device_cold_reset:
 * Reset device @dev and perform a recursive processing using the resettable
 * interface. It triggers a RESET_TYPE_COLD.
 */
void device_cold_reset(DeviceState *dev);

/**
 * bus_cold_reset:
 *
 * Reset bus @bus and perform a recursive processing using the resettable
 * interface. It triggers a RESET_TYPE_COLD.
 */
void bus_cold_reset(BusState *bus);

/**
 * device_is_in_reset:
 * Return true if the device @dev is currently being reset.
 */
bool device_is_in_reset(DeviceState *dev);

/**
 * bus_is_in_reset:
 * Return true if the bus @bus is currently being reset.
 */
bool bus_is_in_reset(BusState *bus);

/* This should go away once we get rid of the NULL bus hack */
BusState *sysbus_get_default(void);

char *qdev_get_fw_dev_path(DeviceState *dev);
char *qdev_get_own_fw_dev_path_from_handler(BusState *bus, DeviceState *dev);

/**
 * @qdev_machine_init
 *
 * Initialize platform devices before machine init.  This is a hack until full
 * support for composition is added.
 */
void qdev_machine_init(void);

/**
 * device_legacy_reset:
 *
 * Reset a single device (by calling the reset method).
 * Note: This function is deprecated and will be removed when it becomes unused.
 * Please use device_cold_reset() now.
 */
void device_legacy_reset(DeviceState *dev);

void device_class_set_props(DeviceClass *dc, Property *props);

/**
 * device_class_set_parent_reset:
 * TODO: remove the function when DeviceClass's reset method
 * is not used anymore.
 */
void device_class_set_parent_reset(DeviceClass *dc,
                                   DeviceReset dev_reset,
                                   DeviceReset *parent_reset);
void device_class_set_parent_realize(DeviceClass *dc,
                                     DeviceRealize dev_realize,
                                     DeviceRealize *parent_realize);
void device_class_set_parent_unrealize(DeviceClass *dc,
                                       DeviceUnrealize dev_unrealize,
                                       DeviceUnrealize *parent_unrealize);

const VMStateDescription *qdev_get_vmsd(DeviceState *dev);

const char *qdev_fw_name(DeviceState *dev);

Object *qdev_get_machine(void);

/* FIXME: make this a link<> */
void qdev_set_parent_bus(DeviceState *dev, BusState *bus);

extern bool qdev_hotplug;
extern bool qdev_hot_removed;

char *qdev_get_dev_path(DeviceState *dev);

void qbus_set_hotplug_handler(BusState *bus, Object *handler);
void qbus_set_bus_hotplug_handler(BusState *bus);

static inline bool qbus_is_hotpluggable(BusState *bus)
{
   return bus->hotplug_handler;
}

void device_listener_register(DeviceListener *listener);
void device_listener_unregister(DeviceListener *listener);

/**
 * @qdev_should_hide_device:
 * @opts: QemuOpts as passed on cmdline.
 *
 * Check if a device should be added.
 * When a device is added via qdev_device_add() this will be called,
 * and return if the device should be added now or not.
 */
bool qdev_should_hide_device(QemuOpts *opts);

#endif