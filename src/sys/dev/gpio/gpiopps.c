/*-
 * Copyright (c) 2016 Ian Lepore <ian@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.	Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2.	Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: releng/12.0/sys/dev/gpio/gpiopps.c 300811 2016-05-26 23:56:12Z ian $");

#include <sys/param.h>
#include <sys/gpio.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/timepps.h>

#include <dev/gpio/gpiobusvar.h>

#include "opt_platform.h"

#ifdef FDT
#include <dev/ofw/ofw_bus.h>

/*
	pps: pulse per second
	一个PPS源就是一个每秒能提供高精度信号的设备，以便于app利用它来调整系统时钟时间(也就是为app提供精准时钟)
*/
static struct ofw_compat_data compat_data[] = {
	{"pps-gpio", 	1},
	{NULL,          0}
};
#endif /* FDT */

static devclass_t pps_devclass;

struct pps_softc {
	device_t         dev;
	gpio_pin_t	 gpin;
	void            *ihandler;	/* 处理函数 */
	struct resource *ires;
	int		 irid;
	struct cdev     *pps_cdev;	/* 字符设备 */
	struct pps_state pps_state;
	struct mtx       pps_mtx;
	bool		 falling_edge;
};

#define PPS_CDEV_NAME   "gpiopps"

static int
gpiopps_open(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	struct pps_softc *sc = dev->si_drv1;

	/* 
		We can't be unloaded while open, so mark ourselves BUSY. 
		只是改变了device的状态
	*/
	mtx_lock(&sc->pps_mtx);
	if (device_get_state(sc->dev) < DS_BUSY) {
		device_busy(sc->dev);
	}
	mtx_unlock(&sc->pps_mtx);

	return 0;
}

static	int
gpiopps_close(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	struct pps_softc *sc = dev->si_drv1;

	/*
	 * Un-busy on last close. We rely on the vfs counting stuff to only call
	 * this routine on last-close, so we don't need any open-count logic.
	 * 依赖VFS(虚拟文件系统)计数的机制，在最后一次关闭中调用这个函数
	 */
	mtx_lock(&sc->pps_mtx);
	device_unbusy(sc->dev);
	mtx_unlock(&sc->pps_mtx);

	return 0;
}

static int
gpiopps_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flags, struct thread *td)
{
	struct pps_softc *sc = dev->si_drv1;
	int err;

	/* 
		Let the kernel do the heavy lifting for ioctl. 
		让kernel来完成ioctl的繁重工作
	*/
	mtx_lock(&sc->pps_mtx);
	err = pps_ioctl(cmd, data, &sc->pps_state);
	mtx_unlock(&sc->pps_mtx);

	return err;
}

static struct cdevsw pps_cdevsw = {
	.d_version =    D_VERSION,
	.d_open =       gpiopps_open,
	.d_close =      gpiopps_close,
	.d_ioctl =      gpiopps_ioctl,
	.d_name =       PPS_CDEV_NAME,
};

/* 
	filter是有能力处理完整个中断的，但是一般计算复杂度比较高的工作时，为了提高整体效率，还是
	会去调用与其关联的ithread例程来执行它们
*/
static int
gpiopps_ifltr(void *arg)
{
	struct pps_softc *sc = arg;

	/*
	 * There is no locking here by design... The kernel cleverly captures
	 * the current time into an area of the pps_state structure which is
	 * written only by the pps_capture() routine and read only by the
	 * pps_event() routine.  We don't need lock-based management of access
	 * to the capture area because we have time-based access management:  we
	 * can't be reading and writing concurently because we can't be running
	 * both the threaded and filter handlers concurrently (because a new
	 * hardware interrupt can't happen until the threaded handler for the
	 * current interrupt exits, after which the system does the EOI that
	 * enables a new hardware interrupt).
	 * 我们不能同时读写，因为我们不能同时运行线程处理程序和过滤器处理程序（因为在当前中断的
	 * 	线程处理程序退出之前，新的硬件中断不会发生，之后系统将执行启用新硬件中断的EOI）
	 */
	pps_capture(&sc->pps_state);
	return (FILTER_SCHEDULE_THREAD);
}

static void
gpiopps_ithrd(void *arg)
{
	struct pps_softc *sc = arg;

	/*
	 * Go create a pps event from the data captured in the filter handler.
	 *
	 * Note that we DO need locking here, unlike the case with the filter
	 * handler.  The pps_event() routine updates the non-capture part of the
	 * pps_state structure, and the ioctl() code could be accessing that
	 * data right now in a non-interrupt context, so we need an interlock.
	 */
	mtx_lock(&sc->pps_mtx);
	pps_event(&sc->pps_state, PPS_CAPTUREASSERT);
	mtx_unlock(&sc->pps_mtx);
}

static int
gpiopps_detach(device_t dev)
{
	struct pps_softc *sc = device_get_softc(dev);

	if (sc->pps_cdev != NULL)
		destroy_dev(sc->pps_cdev);
	if (sc->ihandler != NULL)
		bus_teardown_intr(dev, sc->ires, sc->ihandler);
	if (sc->ires != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, sc->irid, sc->ires);
	if (sc->gpin != NULL)
		gpiobus_release_pin(GPIO_GET_BUS(sc->gpin->dev), sc->gpin->pin);
	return (0);
}

#ifdef FDT
static int
gpiopps_fdt_attach(device_t dev)
{
	struct pps_softc *sc;
	struct make_dev_args devargs;
	phandle_t node;
	uint32_t edge, pincaps;
	int err;

	sc = device_get_softc(dev);
	sc->dev = dev;

	mtx_init(&sc->pps_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	/* Initialize the pps_state struct. */
	sc->pps_state.ppscap = PPS_CAPTUREASSERT | PPS_CAPTURECLEAR;
	sc->pps_state.driver_abi = PPS_ABI_VERSION;
	sc->pps_state.driver_mtx = &sc->pps_mtx;
	pps_init_abi(&sc->pps_state);

	/* 
		Check which edge we're configured to capture (default is rising). 
		设备树相关，判断dev中是否含有以 assert-falling-edge 命名的属性(配置上升沿和下降沿触发)
	*/
	if (ofw_bus_has_prop(dev, "assert-falling-edge"))
		edge = GPIO_INTR_EDGE_FALLING;
	else
		edge = GPIO_INTR_EDGE_RISING;

	/*
	 * Look up the configured gpio pin and ensure it can be configured for
	 * the interrupt mode we need.
	 * 其实就是查找是否含某个gpio pin
	 */
	node = ofw_bus_get_node(dev);
	if ((err = gpio_pin_get_by_ofw_idx(dev, node, 0, &sc->gpin)) != 0) {
		device_printf(dev, "Cannot obtain gpio pin\n");
		return (err);
	}
	device_printf(dev, "PPS input on %s pin %u\n",
	    device_get_nameunit(sc->gpin->dev), sc->gpin->pin);

	/* 获取gpio pin的capabilities */
	if ((err = gpio_pin_getcaps(sc->gpin, &pincaps)) != 0) {
		device_printf(dev, "Cannot query capabilities of gpio pin\n");
		gpiopps_detach(dev);
		return (err);
	}
	if ((pincaps & edge) == 0) {
		device_printf(dev, "Pin cannot be configured for the requested signal edge\n");
		gpiopps_detach(dev);
		return (ENOTSUP);
	}

	/*
	 * Transform our 'gpios' property into an interrupt resource and set up
	 * the interrupt.
	 * 在注册中断处理函数之前，首先要做的就是成功分配IRQ，也就是interrupt resource
	 */
	if ((sc->ires = gpio_alloc_intr_resource(dev, &sc->irid, RF_ACTIVE,
	    sc->gpin, edge)) == NULL) {
		device_printf(dev, "Cannot allocate an IRQ for the GPIO\n");
		gpiopps_detach(dev);
		return (err);
	}

	/* 
		资源分配完成之后，注册我们的中断处理函数，一般这一步是在attach阶段来执行的 
	*/
	err = bus_setup_intr(dev, sc->ires, INTR_TYPE_CLK | INTR_MPSAFE, 
	    gpiopps_ifltr, gpiopps_ithrd, sc, &sc->ihandler);
	if (err != 0) {
		device_printf(dev, "Unable to setup pps irq handler\n");
		gpiopps_detach(dev);
		return (err);
	}

	/* Create the RFC 2783 pps-api cdev. */
	make_dev_args_init(&devargs);
	devargs.mda_devsw = &pps_cdevsw;
	devargs.mda_uid = UID_ROOT;
	devargs.mda_gid = GID_WHEEL;
	devargs.mda_mode = 0660;
	devargs.mda_si_drv1 = sc;

	/* 
		通过devargs中的信息创建字符设备结点
	*/
	err = make_dev_s(&devargs, &sc->pps_cdev, PPS_CDEV_NAME "%d", 
	    device_get_unit(dev));
	if (err != 0) {
		device_printf(dev, "Unable to create pps cdev\n");
		gpiopps_detach(dev);
		return (err);
	}

	return (0);
}

/*
	很多驱动程序中的probe函数的实现都是只有 device_set_desc 这么一个函数，那么这个函数也就
	从属于任何总线能检测到的设备，前提是设备的名称是唯一的？？
*/
static int
gpiopps_fdt_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data != 0) {
		device_set_desc(dev, "GPIO PPS");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static device_method_t pps_fdt_methods[] = {
	DEVMETHOD(device_probe,		gpiopps_fdt_probe),
	DEVMETHOD(device_attach,	gpiopps_fdt_attach),
	DEVMETHOD(device_detach,	gpiopps_detach),

	DEVMETHOD_END
};

static driver_t pps_fdt_driver = {
	"gpiopps",
	pps_fdt_methods,
	sizeof(struct pps_softc),
};

DRIVER_MODULE(gpiopps, simplebus, pps_fdt_driver, pps_devclass, 0, 0);

#endif /* FDT */
