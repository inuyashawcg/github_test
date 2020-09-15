/*
 * Product specific probe and attach routines for:
 *      Adaptec 154x.
 */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999-2003 M. Warner Losh
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Derived from bt isa from end, written by:
 *
 * Copyright (c) 1998 Justin T. Gibbs
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: releng/12.0/sys/dev/aha/aha_isa.c 327102 2017-12-23 06:49:27Z imp $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <isa/isavar.h>

#include <dev/aha/ahareg.h>

#include <cam/scsi/scsi_all.h>

static struct isa_pnp_id aha_ids[] = {
	{ADP0100_PNP,		"Adaptec 1540/1542 ISA SCSI"},	/* ADP0100 */
	{AHA1540_PNP,		"Adaptec 1540/aha-1640/aha-1535"},/* ADP1540 */
	{AHA1542_PNP,		"Adaptec 1542/aha-1535"},	/* ADP1542 */
	{AHA1542_PNPCOMPAT,	"Adaptec 1542 compatible"},	/* PNP00A0 */
	{ICU0091_PNP,		"Adaptec AHA-1540/1542 SCSI"},	/* ICU0091 */
	{0}
};

/*
 * I/O ports listed in the order enumerated by the card for certain op codes.
 */
static bus_addr_t aha_board_ports[] =
{
	0x330,
	0x334,
	0x230,
	0x234,
	0x130,
	0x134
};

/*
 * Check if the device can be found at the port given
 */
static int
aha_isa_probe(device_t dev)
{
	/*
	 * find unit and check we have that many defined
	 */
	struct	aha_softc *aha = device_get_softc(dev);
	int	error;
	u_long	port_start;
	int	port_rid;
	int	drq;
	int	irq;
	config_data_t config_data;

	aha->dev = dev;
	/* Check isapnp ids */
	/* 检测设备是否存在，如果不存在的话，返回ENXIO 
		一般非PnP设备都是sensitive的，会被优先检测。对于每一个PnP设备，系统都会调用所有
		的ISA驱动中的例程去检测它，第一个响应的会拿到这个设备。但是有可能会多个驱动都产生了
		响应，这种情况下就要去比较他们的相应级别，高级别的拿到这个设备。
		响应级别可以通过ISA_PNP_PROBE()的返回值确定。ISA_PNP_PROBE()返回值大于0，表示
		出错，返回值小于等于0，表示正确。比如一个新的多功能接口程序和老的单一功能的接口程序
		都可以检测到这个设备，那么一般新接口会返回一个0，表示其有最高的优先级；而旧的接口一般
		返回的值会比较小，可能是-1，或者-2等等，所以新接口会拿到这个设备。
		每个驱动必须通过调用 ISA_PNP_PROBE()比较实际的PnP ID和驱动支持的PnP ID列表中的
		元素是否匹配的上，如果匹配不上，返回失败。即使驱动不支持任何一个PnP设备，也是需要调用
		这个函数的
	*/
	if (ISA_PNP_PROBE(device_get_parent(dev), dev, aha_ids) == ENXIO)
		return (ENXIO);

	port_rid = 0;
	/*
		在已经找到设备的情况下，从parent bus中分配一块数据。从 bus_alloc_resource_anywhere() 的解释中
		可以看到，好像是对于要分配的空间有某些限制的条件。这个可能跟parent bus中的一些操作或者配置有关系？
	 */
	aha->port = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT, &port_rid,
	    AHA_NREGS, RF_ACTIVE);

	if (aha->port == NULL)
		return (ENXIO);

	/* 获取alloc资源的起始地址*/
	port_start = rman_get_start(aha->port);

	/* 资源分配或者初始化一类的操作？ */
	aha_alloc(aha);		

	/* See if there is really a card present */
	/* 检测适配器的相关操作 */
	if (aha_probe(aha) || aha_fetch_adapter_info(aha)) {
		aha_free(aha);
		bus_release_resource(dev, SYS_RES_IOPORT, port_rid, aha->port);
		return (ENXIO);
	}

	/*
	 * Determine our IRQ, and DMA settings and
	 * export them to the configuration system.
	 */

	/*
		该函数主要是向适配器发送命令。I/O设备都是通过控制器或者适配器跟I/O总线相连，
		只不过两种方式的封装形式不太一样，控制器的话大部分情况是芯片组，适配器的话可能
		是板卡一类的设备。但是无论采用那种方式，主要还是为了在总线和设备之间传递数据
	*/
	error = aha_cmd(aha, AOP_INQUIRE_CONFIG, NULL, /*parmlen*/0,
	    (uint8_t*)&config_data, sizeof(config_data), DEFAULT_CMD_TIMEOUT);

	if (error != 0) {
		device_printf(dev, "Could not determine IRQ or DMA "
		    "settings for adapter at %#jx.  Failing probe\n",
		    (uintmax_t)port_start);
		aha_free(aha);
		bus_release_resource(dev, SYS_RES_IOPORT, port_rid,
		    aha->port);
		return (ENXIO);
	}

	/* 配置信息发送完毕后，释放其所占用的资源*/
	bus_release_resource(dev, SYS_RES_IOPORT, port_rid, aha->port);
	aha->port = NULL;

	switch (config_data.dma_chan) {
	case DMA_CHAN_5:
		drq = 5;
		break;
	case DMA_CHAN_6:
		drq = 6;
		break;
	case DMA_CHAN_7:
		drq = 7;
		break;
	default:
		device_printf(dev, "Invalid DMA setting for adapter at %#jx.",
		    (uintmax_t)port_start);
		return (ENXIO);
	}

	/* 
		SYS_RES_DRQ: ISA DMA channel number 
		SYS_RES_DRQ： interrupt number
		bus_set_resource() 主要是bus driver调用，一般设备驱动是不会调用这个的，
		主要还是根据设备的一些配置信息来设置用于该设备的一些资源
	*/
	error = bus_set_resource(dev, SYS_RES_DRQ, 0, drq, 1);
	if (error)
		return error;
	/*
		The ffs(),	ffsl() and ffsll() functions find the first (least signifi-
     	cant) bit set in value and	return the index of that bit.
	*/
	irq = ffs(config_data.irq) + 8;
	error = bus_set_resource(dev, SYS_RES_IRQ, 0, irq, 1);
	return (error);
}

/*
 * Attach all the sub-devices we can find
 */
static int
aha_isa_attach(device_t dev)
{
	struct	aha_softc *aha = device_get_softc(dev);
	int		 error = ENOMEM;

	aha->dev = dev;
	aha->portrid = 0;

	// 从parent bus中分配资源
	aha->port = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT,
	    &aha->portrid, AHA_NREGS, RF_ACTIVE);
	if (!aha->port) {
		device_printf(dev, "Unable to allocate I/O ports\n");
		goto fail;
	}

	// 获取interrupt lines所需要的资源
	aha->irqrid = 0;
	aha->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &aha->irqrid,
	    RF_ACTIVE);
	if (!aha->irq) {
		device_printf(dev, "Unable to allocate excluse use of irq\n");
		goto fail;
	}

	// 获取ISA DMA Lines所需要的资源
	aha->drqrid = 0;
	aha->drq = bus_alloc_resource_any(dev, SYS_RES_DRQ, &aha->drqrid,
	    RF_ACTIVE);
	if (!aha->drq) {
		device_printf(dev, "Unable to allocate drq\n");
		goto fail;
	}

#if 0				/* is the drq ever unset? */
	if (dev->id_drq != -1)
		isa_dmacascade(dev->id_drq);
#endif
	// DMA 级联控制，外部板卡相关
	isa_dmacascade(rman_get_start(aha->drq));

	// DMA tag主要跟DMA Memory分配是相关的
	/* Allocate our parent dmatag */
	if (bus_dma_tag_create(	/* parent	*/ bus_get_dma_tag(dev),
				/* alignemnt	*/ 1,
				/* boundary	*/ 0,
				/* lowaddr	*/ BUS_SPACE_MAXADDR_24BIT,
				/* highaddr	*/ BUS_SPACE_MAXADDR,
				/* filter	*/ NULL,
				/* filterarg	*/ NULL,
				/* maxsize	*/ BUS_SPACE_MAXSIZE_24BIT,
				/* nsegments	*/ ~0,
				/* maxsegsz	*/ BUS_SPACE_MAXSIZE_24BIT,
				/* flags	*/ 0,
				/* lockfunc	*/ NULL,
				/* lockarg	*/ NULL,
				&aha->parent_dmat) != 0) {
		device_printf(dev, "dma tag create failed.\n");
		goto fail;
        }                              

	// 开启板卡，准备正常运行
	if (aha_init(aha)) {
		device_printf(dev, "init failed\n");
		goto fail;
        }
	/*
	 * The 1542A and B look the same.  So we guess based on
	 * the firmware revision.  It appears that only rev 0 is on
	 * the A cards.
	 */
	if (aha->boardid <= BOARD_1542 && aha->fw_major == 0) {
		device_printf(dev, "154xA may not work\n");
		aha->ccb_sg_opcode = INITIATOR_SG_CCB;
		aha->ccb_ccb_opcode = INITIATOR_CCB;
	}
	
	error = aha_attach(aha);
	if (error) {
		device_printf(dev, "attach failed\n");
		goto fail;
	}

	// 创建，绑定，解绑中断处理程序
	error = bus_setup_intr(dev, aha->irq, INTR_TYPE_CAM|INTR_ENTROPY|
	    INTR_MPSAFE, NULL, aha_intr, aha, &aha->ih);
	if (error) {
		device_printf(dev, "Unable to register interrupt handler\n");
		aha_detach(aha);
                goto fail;
	}

	return (0);
fail: ;
	aha_free(aha);
	bus_free_resource(dev, SYS_RES_IOPORT, aha->port);
	bus_free_resource(dev, SYS_RES_IRQ, aha->irq);
	bus_free_resource(dev, SYS_RES_DRQ, aha->drq);
	return (error);
}

static int
aha_isa_detach(device_t dev)
{
	struct aha_softc *aha = (struct aha_softc *)device_get_softc(dev);
	int error;

	error = bus_teardown_intr(dev, aha->irq, aha->ih);
	if (error)
		device_printf(dev, "failed to unregister interrupt handler\n");

	error = aha_detach(aha);
	if (error) {
		device_printf(dev, "detach failed\n");
		return (error);
	}
	aha_free(aha);
	bus_free_resource(dev, SYS_RES_IOPORT, aha->port);
	bus_free_resource(dev, SYS_RES_IRQ, aha->irq);
	bus_free_resource(dev, SYS_RES_DRQ, aha->drq);

	return (0);
}

static void
aha_isa_identify(driver_t *driver, device_t parent)
{
	int i;
	bus_addr_t ioport;
	struct aha_softc aha;
	int rid;
	device_t child;

	/* Attempt to find an adapter */
	for (i = 0; i < nitems(aha_board_ports); i++) {
		bzero(&aha, sizeof(aha));
		ioport = aha_board_ports[i];
		/*
		 * XXX Check to see if we have a hard-wired aha device at
		 * XXX this port, if so, skip.  This should also cover the
		 * XXX case where we are run multiple times due to, eg,
		 * XXX kldload/kldunload.
		 */
		rid = 0;
		aha.port = bus_alloc_resource(parent, SYS_RES_IOPORT, &rid,
		    ioport, ioport, AHA_NREGS, RF_ACTIVE);
		if (aha.port == NULL)
			continue;
		aha_alloc(&aha);
		/* See if there is really a card present */
		if (aha_probe(&aha) || aha_fetch_adapter_info(&aha))
			goto not_this_one;
		child = BUS_ADD_CHILD(parent, ISA_ORDER_SPECULATIVE, "aha", -1);
		bus_set_resource(child, SYS_RES_IOPORT, 0, ioport, AHA_NREGS);
		/*
		 * Could query the board and set IRQ/DRQ, but probe does
		 * that.
		 */
	not_this_one:
		bus_release_resource(parent, SYS_RES_IOPORT, rid, aha.port);
		aha_free(&aha);
	}
}

static device_method_t aha_isa_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aha_isa_probe),
	DEVMETHOD(device_attach,	aha_isa_attach),
	DEVMETHOD(device_detach,	aha_isa_detach),
	DEVMETHOD(device_identify,	aha_isa_identify),

	{ 0, 0 }
};

static driver_t aha_isa_driver = {
	"aha",
	aha_isa_methods,
	sizeof(struct aha_softc),
};

static devclass_t aha_devclass;

DRIVER_MODULE(aha, isa, aha_isa_driver, aha_devclass, 0, 0);
MODULE_DEPEND(aha, isa, 1, 1, 1);
ISA_PNP_INFO(aha_ids);
