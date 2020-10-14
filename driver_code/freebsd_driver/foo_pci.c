#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/bus.h>

/*
    这里首先定义了软件的上下文(cdev的缘故？？)
*/
struct foo_pci_softc {
    device_t        device;     // 指向设备的指针
    struct cdev     *cdev;      // 调用make_dev函数返回的cdev
};

static d_open_t         foo_pci_open;
static d_close_t        foo_pci_close;
static d_read_t         foo_pci_read;
static d_write_t        foo_pci_write;

/*
    字符设备开关表，说明该设备是一个挂在pci总线上的字符设备
*/
static struct cdevsw foo_pci_cdevsw = {
    .d_version =    D_VERSION,
    .d_open =       foo_pci_open,
    .d_close =      foo_pci_close,
    .d_read =       foo_pci_read,
    .d_write =      foo_pci_write,
    .d_name =       "foo_pci"
};

static devclass_t foo_pci_devclass;

static int
foo_pci_open(struct cdev *dev, int flags, int devtype, struct thread *td)
{
    struct foo_pci_softc *sc;
    sc = dev -> si_driv1;
    device_printf(sc -> device, "opened successfully\n");
    return 0;
}

static int
foo_pci_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
    struct foo_pci_softc *sc;
    sc = dev -> si_drv1;
    device_printf(sc -> device, "close\n");
    return 0;
}

static int
foo_pci_read(struct cdev *dev, struct uio *uio, int ioflag)
{
    struct foo_pci_softc *sc;

    sc = dev -> si_drv1;
    device_printf(sc -> device, "read request = %dB\n", uio -> uio_resid);
    return 0;
}

static int
foo_pci_write(struct cdev *dev, struct uio *uio, int ioflag)
{
    struct foo_pci_softc *sc;

    sc = dev -> si_drv1;
    device_printf(sc -> device, "write request = %dB\n", uio -> uio_resid);
    return 0;
}

/*
    pci_ids数组主要用来列出该驱动程序代码所支持的设备
*/
static struct _pcsid {
    uint32_t   type;    // PCI ID
    const char  *desc;  // PCI设备的描述信息
} pci_ids[] = {
    {0x1234abcd, "RED PCI Widget"},
    {0x4321fedc, "BLU PCI Widget"},
    {0x00000000, NULL}
};

static int
foo_pci_probe(device_t dev)
{
    /*
        dev指定了一个PCI总线上可以找到并能识别的设备
        这里首先获取设备dev的PCI ID，可以看到 pci_get_devid 函数应该是系统提供的函数
    */
    uint32_t type = pci_get_devid(dev);
    struct _pcsid *ep = pci_ids;

    /*
        这一步就开始判断获取到的设备的PCI ID是否存在于pci_ids数组中，也就是判断这个是不是
        该驱动所支持的设备
    */
    while (ep -> type && ep -> type != type) {
        ep++;
    }
    
    if (ep -> desc) {
        /*
            如果是的话，设备该dev设备的详细描述信息(verbose description)并返回成功代码
        */
        device_set_desc(dev, ep -> desc);
        return BUS_PROBE_DEFAULT;
    }

    return ENXIO;
}

static int
foo_pci_attach(device_t dev)
{
    /*
        参数dev指定了本驱动程序所控制的设备，首先获取设备dev的软件上下文sc和单位号unit
    */
    struct foo_pci_softc *sc = device_get_softc(dev);
    int unit = device_get_uint(dev);

    sc -> device = dev;
    /* 
        创建设备节点并且将成员变量sc -> device和sc -> cdev -> si_drv1分别赋值
    */
    sc -> cdev = make_dev(&foo_pci_cdevsw, uint, UID_ROOT, GID_WHEEL, 0600, "foo_pci");
    sc -> cdev -> si_drv1 = sc;

    return 0;
}

static int
foo_pci_detach(device_t dev)
{
    /* 获取dev的软件上下文并销毁相应的设备结点 */
    struct foo_pci_softc *sc = device_get_softc(dev);

    destroy_dev(sc -> cdev);
    return 0;
}

static device_method_t foo_pci_methods[] = {
    /* 设备接口 */
    DEVMETHOD(device_probe,     foo_pci_probe),
    DEVMETHOD(device_attach,    foo_pci_attach),
    DEVMETHOD(device_detach,    foo_pci_detach),
    {0 ,0}
};

static driver_t foo_pci_driver = {
    "foo_pci",
    foo_pci_methods,
    sizeof(struct foo_pci_softc)
};

DRIVER_MODULE(foo_pci, pci, foo_pci_driver, foo_pci_devclass, 0, 0);