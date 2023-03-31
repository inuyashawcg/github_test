/*
 * This file is produced automatically.
 * Do not modify anything in here by hand.
 *
 * Created from source file
 *   /usr/src/sys/kern/device_if.m
 * with
 *   makeobjops.awk
 *
 * See the source file for legal information
 */

/**
 * @defgroup DEVICE device - KObj methods for all device drivers
 * @brief A basic set of methods required for all device drivers.
 *
 * The device interface is used to match devices to drivers during
 * autoconfiguration and provides methods to allow drivers to handle
 * system-wide events such as suspend, resume or shutdown.
 * @{
 */

#ifndef _device_if_h_
#define _device_if_h_

/** @brief Unique descriptor for the DEVICE_PROBE() method */
extern struct kobjop_desc device_probe_desc;
/** @brief A function implementing the DEVICE_PROBE() method */
typedef int device_probe_t(device_t dev);
/**
 * @brief Probe to see if a device matches a driver.
 *
 * Users should not call this method directly. Normally, this
 * is called via device_probe_and_attach() to select a driver
 * calling the DEVICE_PROBE() of all candidate drivers and attach
 * the winning driver (if any) to the device.
 * 用户不应直接调用此方法。通常，这是通过 device_probe_and_attach() 调用的，
 * 以选择调用所有候选驱动程序的 device_probe() 的驱动程序，并将获胜的驱动程序
 * (如果有) 附加到设备
 *
 * This function is used to match devices to device drivers.
 * Typically, the driver will examine the device to see if
 * it is suitable for this driver. This might include checking
 * the values of various device instance variables or reading
 * hardware registers.
 * 此功能用于将设备与设备驱动程序匹配。通常，驱动程序会检查设备，看看它是否适合
 * 此驱动程序。这可能包括检查各种设备实例变量的值或读取硬件寄存器
 *  
 * In some cases, there may be more than one driver availables
 * which can be used for a device (for instance there might
 * be a generic driver which works for a set of many types of
 * device and a more specific driver which works for a subset
 * of devices). Because of this, a driver should not assume
 * that it will be the driver that attaches to the device even
 * if it returns a success status from DEVICE_PROBE(). In particular,
 * a driver must free any resources which it allocated during
 * the probe before returning. The return value of DEVICE_PROBE()
 * is used to elect which driver is used - the driver which returns
 * the largest non-error value wins the election and attaches to
 * the device. Common non-error values are described in the
 * DEVICE_PROBE(9) manual page.
 * 特别是，驱动程序必须在返回之前释放在探测期间分配的任何资源
 *
 * If a driver matches the hardware, it should set the device
 * description string using device_set_desc() or
 * device_set_desc_copy(). This string is used to generate an
 * informative message when DEVICE_ATTACH() is called.
 * 当一个驱动匹配到硬件时，它应该调用 device_set_desc() / device_set_desc_copy()
 * 函数设置设备描述字符串，该字符串会在 DEVICE_ATTACH() 调用时生成信息性消息
 * 
 * As a special case, if a driver returns zero, the driver election
 * is cut short and that driver will attach to the device
 * immediately. This should rarely be used.
 * 作为一种特殊情况，如果驱动返回零，驱动选择过程将被缩短，驱动程序将立即连接到设备。
 * 这应该很少使用
 *
 * For example, a probe method for a PCI device driver might look
 * like this:
 *
 * @code
 * int
 * foo_probe(device_t dev)
 * {
 *         if (pci_get_vendor(dev) == FOOVENDOR &&
 *             pci_get_device(dev) == FOODEVICE) {
 *                 device_set_desc(dev, "Foo device");
 *                 return (BUS_PROBE_DEFAULT);
 *         }
 *         return (ENXIO);
 * }
 * @endcode
 *
 * To include this method in a device driver, use a line like this
 * in the driver's method list:
 *
 * @code
 * 	KOBJMETHOD(device_probe, foo_probe)
 * @endcode
 *
 * @param dev		the device to probe
 *
 * @retval 0		if this is the only possible driver for this
 *			device
 * @retval negative	if the driver can match this device - the
 *			least negative value is used to select the
 *			driver 如果驱动程序可以匹配此设备，则为负值-最小负值用于选择驱动程序
 * @retval ENXIO	if the driver does not match the device
 * @retval positive	if some kind of error was detected during
 *			the probe, a regular unix error code should
 *			be returned to indicate the type of error
 * @see DEVICE_ATTACH(), pci_get_vendor(), pci_get_device()
 */

static __inline int DEVICE_PROBE(device_t dev)
{
	kobjop_t _m;
	KOBJOPLOOKUP(((kobj_t)dev)->ops,device_probe);
	return ((device_probe_t *) _m)(dev);
}

/** @brief Unique descriptor for the DEVICE_IDENTIFY() method */
extern struct kobjop_desc device_identify_desc;
/** @brief A function implementing the DEVICE_IDENTIFY() method */
typedef void device_identify_t(driver_t *driver, device_t parent);
/**
 * @brief Allow a device driver to detect devices not otherwise enumerated.
 * 允许设备驱动程序检测未以其他方式枚举的设备
 *
 * The DEVICE_IDENTIFY() method is used by some drivers (e.g. the ISA
 * bus driver) to help populate the bus device with a useful set of
 * child devices, normally by calling the BUS_ADD_CHILD() method of
 * the parent device. For instance, the ISA bus driver uses several
 * special drivers, including the isahint driver and the pnp driver to
 * create child devices based on configuration hints and PnP bus
 * probes respectively.
 * 某些驱动程序 (例如ISA总线驱动程序) 使用DEVICE_IDENTIFY() 方法，通常通过调用父设备的
 * BUS_ADD_CHILD() 方法来帮助用一组有用的子设备填充总线设备。例如，ISA 总线驱动程序使用
 * 几个特殊的驱动程序，包括 isahint 驱动程序和 pnp 驱动程序，分别基于配置提示和 pnp 总线
 * 探测创建子设备
 *
 * Many bus drivers which support true plug-and-play do not need to
 * use this method at all since child devices can be discovered
 * automatically without help from child drivers.
 * 许多支持真正即插即用的总线驱动程序根本不需要使用这种方法，因为可以在没有子驱动程序帮助的情况下
 * 自动发现子设备
 *
 * To include this method in a device driver, use a line like this
 * in the driver's method list:
 *
 * @code
 * 	KOBJMETHOD(device_identify, foo_identify)
 * @endcode
 *
 * @param driver	the driver whose identify method is being called
 * @param parent	the parent device to use when adding new children
 * 
 * 注意，该函数的第二个参数传入的是父设备，而不是当前设备。通过这个函数可以在父设备中
 * 查找或者添加一个子设备。说明该子设备当前不一定是存在的。而且这个 driver 在很多具体
 * 实现中没有起到什么作用，仅仅就是一个参数传递
 */

static __inline void DEVICE_IDENTIFY(driver_t *driver, device_t parent)
{
	kobjop_t _m;
	KOBJOPLOOKUP(driver->ops,device_identify);
	((device_identify_t *) _m)(driver, parent);
}

/** @brief Unique descriptor for the DEVICE_ATTACH() method */
extern struct kobjop_desc device_attach_desc;
/** @brief A function implementing the DEVICE_ATTACH() method */
typedef int device_attach_t(device_t dev);
/**
 * @brief Attach a device to a device driver
 *
 * Normally only called via device_probe_and_attach(), this is called
 * when a driver has succeeded in probing against a device.
 * This method should initialise the hardware and allocate other
 * system resources (e.g. devfs entries) as required.
 * 通常仅通过 device_probe_and_attach() 调用，当驱动程序成功探测到设备时调用。
 * 该方法应初始化硬件并根据需要分配其他系统资源 (例如 devfs 条目)
 *
 * To include this method in a device driver, use a line like this
 * in the driver's method list:
 *
 * @code
 * 	KOBJMETHOD(device_attach, foo_attach)
 * @endcode
 *
 * @param dev		the device to probe
 *
 * @retval 0		success
 * @retval non-zero	if some kind of error was detected during
 *			the attach, a regular unix error code should
 *			be returned to indicate the type of error
 * @see DEVICE_PROBE()
 */

static __inline int DEVICE_ATTACH(device_t dev)
{
	kobjop_t _m;
	KOBJOPLOOKUP(((kobj_t)dev)->ops,device_attach);
	return ((device_attach_t *) _m)(dev);
}

/** @brief Unique descriptor for the DEVICE_DETACH() method */
extern struct kobjop_desc device_detach_desc;
/** @brief A function implementing the DEVICE_DETACH() method */
typedef int device_detach_t(device_t dev);
/**
 * @brief Detach a driver from a device.
 *
 * This can be called if the user is replacing the
 * driver software or if a device is about to be physically removed
 * from the system (e.g. for removable hardware such as USB or PCCARD).
 * 如果用户正在更换驱动程序软件，或者设备即将从系统中物理移除 (例如，对于USB或 PCCARD
 * 等可移动硬件)，则可以调用此命令
 *
 * To include this method in a device driver, use a line like this
 * in the driver's method list:
 *
 * @code
 * 	KOBJMETHOD(device_detach, foo_detach)
 * @endcode
 *
 * @param dev		the device to detach
 *
 * @retval 0		success
 * @retval non-zero	the detach could not be performed, e.g. if the
 *			driver does not support detaching.
 *
 * @see DEVICE_ATTACH()
 */

static __inline int DEVICE_DETACH(device_t dev)
{
	kobjop_t _m;
	KOBJOPLOOKUP(((kobj_t)dev)->ops,device_detach);
	return ((device_detach_t *) _m)(dev);
}

/** @brief Unique descriptor for the DEVICE_SHUTDOWN() method */
extern struct kobjop_desc device_shutdown_desc;
/** @brief A function implementing the DEVICE_SHUTDOWN() method */
typedef int device_shutdown_t(device_t dev);
/**
 * @brief Called during system shutdown.
 *
 * This method allows drivers to detect when the system is being shut down.
 * Some drivers need to use this to place their hardware in a consistent
 * state before rebooting the computer.
 * 此方法允许驱动程序检测系统何时关闭。某些驱动程序需要使用此功能在重新启动计算机之前将其
 * 硬件置于一致状态
 *
 * To include this method in a device driver, use a line like this
 * in the driver's method list:
 *
 * @code
 * 	KOBJMETHOD(device_shutdown, foo_shutdown)
 * @endcode
 */

static __inline int DEVICE_SHUTDOWN(device_t dev)
{
	kobjop_t _m;
	KOBJOPLOOKUP(((kobj_t)dev)->ops,device_shutdown);
	return ((device_shutdown_t *) _m)(dev);
}

/** @brief Unique descriptor for the DEVICE_SUSPEND() method */
extern struct kobjop_desc device_suspend_desc;
/** @brief A function implementing the DEVICE_SUSPEND() method */
typedef int device_suspend_t(device_t dev);
/**
 * @brief This is called by the power-management subsystem when a
 * suspend has been requested by the user or by some automatic
 * mechanism.
 * 当用户或某个自动机制请求挂起时，电源管理子系统会调用此命令
 *
 * This gives drivers a chance to veto the suspend or save their
 * configuration before power is removed.
 * 这使驾驶员有机会在断电前否决暂停或保存其配置
 *
 * To include this method in a device driver, use a line like this in
 * the driver's method list:
 *
 * @code
 * 	KOBJMETHOD(device_suspend, foo_suspend)
 * @endcode
 *
 * @param dev		the device being suspended
 *
 * @retval 0		success
 * @retval non-zero	an error occurred while attempting to prepare the
 *                      device for suspension
 *
 * @see DEVICE_RESUME()
 */

static __inline int DEVICE_SUSPEND(device_t dev)
{
	kobjop_t _m;
	KOBJOPLOOKUP(((kobj_t)dev)->ops,device_suspend);
	return ((device_suspend_t *) _m)(dev);
}

/** @brief Unique descriptor for the DEVICE_RESUME() method */
extern struct kobjop_desc device_resume_desc;
/** @brief A function implementing the DEVICE_RESUME() method */
typedef int device_resume_t(device_t dev);
/**
 * @brief This is called when the system resumes after a suspend.
 * 当系统在挂起后恢复时调用
 *
 * To include this method in a device driver, use a line like this
 * in the driver's method list:
 *
 * @code
 * 	KOBJMETHOD(device_resume, foo_resume)
 * @endcode
 *
 * @param dev		the device being resumed
 *
 * @retval 0		success
 * @retval non-zero	an error occurred while attempting to restore the
 *                      device from suspension
 *
 * @see DEVICE_SUSPEND()
 */

static __inline int DEVICE_RESUME(device_t dev)
{
	kobjop_t _m;
	KOBJOPLOOKUP(((kobj_t)dev)->ops,device_resume);
	return ((device_resume_t *) _m)(dev);
}

/** @brief Unique descriptor for the DEVICE_QUIESCE() method */
extern struct kobjop_desc device_quiesce_desc;
/** @brief A function implementing the DEVICE_QUIESCE() method */
typedef int device_quiesce_t(device_t dev);
/**
 * @brief This is called when the driver is asked to quiesce itself.
 * 当要求驱动程序自行停止时，调用此命令
 *
 * The driver should arrange for the orderly shutdown of this device.
 * All further access to the device should be curtailed.  Soon there
 * will be a request to detach, but there won't necessarily be one.
 * 驾驶员应安排有序关闭该设备，应限制进一步使用该设备。很快就会有一个 detach 的请求，
 * 但不一定会有
 *
 * To include this method in a device driver, use a line like this
 * in the driver's method list:
 *
 * @code
 * 	KOBJMETHOD(device_quiesce, foo_quiesce)
 * @endcode
 *
 * @param dev		the device being quiesced
 *
 * @retval 0		success
 * @retval non-zero	an error occurred while attempting to quiesce the
 *                      device
 *
 * @see DEVICE_DETACH()
 */

static __inline int DEVICE_QUIESCE(device_t dev)
{
	kobjop_t _m;
	KOBJOPLOOKUP(((kobj_t)dev)->ops,device_quiesce);
	return ((device_quiesce_t *) _m)(dev);
}

/** @brief Unique descriptor for the DEVICE_REGISTER() method */
extern struct kobjop_desc device_register_desc;
/** @brief A function implementing the DEVICE_REGISTER() method */
typedef void * device_register_t(device_t dev);
/**
 * @brief This is called when the driver is asked to register handlers.
 * 当要求驱动程序注册处理程序时，会调用此函数
 *
 * To include this method in a device driver, use a line like this
 * in the driver's method list:
 *
 * @code
 * 	KOBJMETHOD(device_register, foo_register)
 * @endcode
 *
 * @param dev		the device for which handlers are being registered
 *
 * @retval NULL     method not implemented
 * @retval non-NULL	a pointer to implementation specific static driver state
 *
 */

static __inline void * DEVICE_REGISTER(device_t dev)
{
	kobjop_t _m;
	KOBJOPLOOKUP(((kobj_t)dev)->ops,device_register);
	return ((device_register_t *) _m)(dev);
}

#endif /* _device_if_h_ */
