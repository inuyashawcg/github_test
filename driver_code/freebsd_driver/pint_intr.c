#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/bus.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/ppbus/ppconf.h>
#include "ppbus_if.h"
#include <dev/ppbus/ppbio.h>

#define PINT_NAME       "pint"
#define BUFFER_SIZE     256

struct pint_data {
    int                 sc_irq_rid;
    struct resource     *sc_irq_resource;
    void                *sc_irq_cookie;
    device_t            sc_device;
    struct cdev         *sc_cdev;
    short               sc_state;

#define PINT_OPEN       0x01;

    char                *sc_buffer;
    int                 sc_length;
};

static d_open_t     pint_open;
static d_close_t    pint_close;
static d_read_t     pint_read;
static d_write_t    pint_write;

static struct cdevsw pint_cdevsw = {
    .d_version = D_VERSION,
    .d_open =   pint_open,
    .d_close =  pint_close,
    .d_read =   pint_read,
    .d_write =  pint_write,
    .d_name =   PINT_NAME
};

static devclass_t pint_devclass;

static int
pint_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
    struct pint_data *sc = dev -> si_drv1;
    device_t pint_device = sc -> sc_device;
    device_t ppbus = device_get_parent(pint_device);
    int error;

    /* 首先获取并口互斥锁 */
    ppb_lock(ppbus);

    if (sc -> sc_state) {
        /*
            如果该值不为0，返回错误码EBUSY，表明有其他进程正在打开此设备
        */
        ppbus_unlock(ppbus);
        return EBUSY;
    } else {
        sc -> sc_state |= PINT_OPEN;
    }

    /*
        调用函数ppb_request_bus从而将pint_device设置为并口的宿主
        这里，pint_device正是pint_attach函数所返回的指向dev的指针，拥有并口的设备能够输入或者输出数据
    */
    error = ppb_request_bus(ppbus, pint_device, PPB_WAIT | PPB_INTR);
    if (error) {
        sc -> sc_state = 0;
        ppb_unlock(ppbus);
        return error;
    }

    /* 清空并口的控制寄存器 */
    ppbus_wctr(ppbus, 0);

    /* 使能中断 */   
    ppbus_wctr(ppbus, IRQENABLE);

    ppb_unlock(ppbus);
    return 0;
}

static int
pint_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
    struct pint_data *sc = dev -> si_drv1;
    device_t pint_device = sc -> sc_device;
    device_t ppbus = device_get_parent(pint_device);

    ppbus_lock(ppbus);

    /* 逻辑上的应该是先关闭中断，然后再清空控制寄存器。实际开发的时候是这么做的 */
    ppb_wctr(ppbus, 0);

    /* 放弃对并口的所有权 */
    ppb_release_bus(ppbus, pint_device);

    /* 将state设置为0，这样其他的进程就可以打开该设备了 */
    sc -> sc_state = 0;

    ppb_unlock(ppbus);
    return 0;
}

/* 类比之前的代码，这里也是实现了从用户空间获得一个字符串并将其存储到缓冲区 */
static int
pint_write(struct cdev *dev, struct uio *uio, int ioflag)
{
    struct pint_data *sc = dev -> si_drv1;
    device_t pint_device = sc -> sc_device;
    int amount, error = 0;

    amount = MIN(uio -> uio_resid, (BUFFER_SIZE - 1 - uio->uio_offset > 0) ? BUFFER_SIZE - 1 - uio->uio_resid : 0);

    if (0 == amount)
        return error;
    
    error = uiomove(sc->sc_buffer, amount, uio);
    if (error) {
        device_printf(pint_device, "write failed\n");
        return error;
    }

    sc->buffer[amount] = '\0';
    sc->sc_length = amount;

    return error;
}

/*
    此函数在入口处休眠以等待输入，并最终将缓冲区中存储的字符串返回到用户空间
*/
static int
pint_read(struct cdev *dev, struct uio *uio, int ioflag)
{
    struct pint_data *sc = dev -> si_drv1;
    device_t pint_device = sc -> sc_device;
    int amount, error = 0;

    ppb_lock(ppbus);
    error = ppb_sleep(ppbus, pint_device, PPBPRT | PCATCH, PINT_NAME, 0);
    ppb_unlock(ppbus);

    if (error)
        return error;
    
    amount = MIN(uio->uio_resid,(sc->sc_length - uio->uio_offset > 0) ? sc->sc_length - uio->uio_offset : 0);
    error = uiomove(sc->sc_buffer + uio->uio_offset, amount, uio);
    if (error)
        device_printf(pint_device, "read failed\n");
    return error;
}

static void
pint_intr(void *arg)
{
    struct pint_data *sc = arg;
    device_t pint_device = sc -> sc_device;

#ifdef INVARIANTS
    device_t ppbus = device_get_parent(pint_device);
    ppb_assert_locked(ppbus);
#endif

    wakeup(pint_device);
}

/*
    该函数只有在并口无法识别出设备的时候才会被调用
*/
static void
pint_identify(driver_t *driver, device_t parent)
{
    device_t dev;

    /*
        该函数首先确定并口是否(曾经)识别过一个名为PINT_NAME的子设备，如果没有，那么pint_identify函数
        就会将PINT_NAME添加到并口已识别设备列表中(BUS_ADD_CHILD)
    */
    dev = device_find_child(parent, PINT_NAME, -1);
    if(!dev)
        BUS_ADD_CHILD(parent, 0, PINT_NAME, -1);
}

static int
pint_probe(device_t dev)
{
    /* 
        probe函数总是显示正确,所以该代码从属于任何它能检测到的设备，这个做法看起来不对其实是正确的，因为
        经过了probe例程识别并经过BUD_ADD_CHILD添加的设备只会通过同一个名字出来侦测。就目前的情形来说，
        能识别的设备和驱动程序的名字就是PINT_NAME
    */
    device_set_desc(dev, "Interrupt Handler Example");

    return BUS_PROBE_SPECIFIC;
}

static int
pint_attach(device_t dev)
{
    struct pint_data *sc = device_get_softc(dev);
    int error, unit = device_get_unit(dev);

    /* 声明我们自己的中断处理程序 */
    sc->sc_irq_rid = 0;
    
    /* 通过bus_alloc_reosource_any函数首先分配一个IRQ */
    sc->sc_irq_resource = bus_alloc_reosource_any(dev, SYS_RES_IRQ, &sc->sc_irq_rid, RF_ACTIVE | RF_SHAREABLE);

    if (!sc->sc_irq_resource) {
        device_printf(dev, "unable to allocate interrupt resource\n");
        return ENXIO;   // 此错误码表示设备未配置
    }

    /* 注册我们的中断处理程序，并且将pint_intr函数设置为中断处理函数(所以，此中断处理程序就是一个ithread例程) */
    error = bus_setup_intr(dev, sc->sc_irq_resource, INTR_TYPE_TTY | INTR_MPSAFE, NULL, pint_intr, sc, &sc->sc_irq_cookie);
    if (error) {
        bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irq_rid, sc->sc_irq_resource);
        device_printf(dev, "unable to register interrupt handler\n");
        return error;
    }

    /* 分配BUFFER_SIZE大小缓冲区*/
    sc->sc_buffer = malloc(BUFFER_SIZE, M_DEVBUF, M_WAITOK);

    sc->device = dev;

    /* 创建字符设备结点并将指向其软件上下文(sc)的指针保存在sc->sc_cdev->si_drv1中 */
    sc->sc_cdev = make_dev(&pint_cdevsw, unit, UID_ROOT, GID_WHEEL, 0600, PINT_NAME "%d", unit);
    sc->sc_cdev->si_drv1 = sc;
    return 0;
}

static int
pint_detach(device_t dev)
{
    struct pint_data *sc = device_get_softc(dev);
    destroy_dev(sc->sc_cdev);

    bus_teardown_intr(dev, sc->sc_irq_resource, sc->sc_irq_cookie);
    bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irq_rid, sc->sc_irq_resource);
    free(sc->sc_buffer, M_DEVBUF);

    return 0;
}

static device_method_t pint_methods[] = {
    /**/
    DEVMETHOD(device_identify, pint_identify),
    DEVMETHOD(device_probe, pint_probe),
    DEVMETHOD(device_attach, pint_attach),
    DEVMETHOD(device_detach, pint_detach),
    {0 , 0}
};

static driver_t pint_driver = {
    PINT_NAME,
    pint_methods,
    sizeof(struct pint_data)
};

DRIVER_MODULE(pint, ppbus, pint_driver, pint_devclass, 0, 0);
MODULE_DEPEND(pint, ppbus, 1, 1, 1);