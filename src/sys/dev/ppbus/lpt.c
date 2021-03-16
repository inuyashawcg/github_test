/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1990 William F. Jolitz, TeleMuse
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This software is a component of "386BSD" developed by
 *	William F. Jolitz, TeleMuse.
 * 4. Neither the name of the developer nor the name "386BSD"
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS A COMPONENT OF 386BSD DEVELOPED BY WILLIAM F. JOLITZ
 * AND IS INTENDED FOR RESEARCH AND EDUCATIONAL PURPOSES ONLY. THIS
 * SOFTWARE SHOULD NOT BE CONSIDERED TO BE A COMMERCIAL PRODUCT.
 * THE DEVELOPER URGES THAT USERS WHO REQUIRE A COMMERCIAL PRODUCT
 * NOT MAKE USE OF THIS WORK.
 *
 * FOR USERS WHO WISH TO UNDERSTAND THE 386BSD SYSTEM DEVELOPED
 * BY WILLIAM F. JOLITZ, WE RECOMMEND THE USER STUDY WRITTEN
 * REFERENCES SUCH AS THE  "PORTING UNIX TO THE 386" SERIES
 * (BEGINNING JANUARY 1991 "DR. DOBBS JOURNAL", USA AND BEGINNING
 * JUNE 1991 "UNIX MAGAZIN", GERMANY) BY WILLIAM F. JOLITZ AND
 * LYNNE GREER JOLITZ, AS WELL AS OTHER BOOKS ON UNIX AND THE
 * ON-LINE 386BSD USER MANUAL BEFORE USE. A BOOK DISCUSSING THE INTERNALS
 * OF 386BSD ENTITLED "386BSD FROM THE INSIDE OUT" WILL BE AVAILABLE LATE 1992.
 *
 * THIS SOFTWARE IS PROVIDED BY THE DEVELOPER ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE DEVELOPER BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: unknown origin, 386BSD 0.1
 *	From Id: lpt.c,v 1.55.2.1 1996/11/12 09:08:38 phk Exp
 *	From Id: nlpt.c,v 1.14 1999/02/08 13:55:43 des Exp
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: releng/12.0/sys/dev/ppbus/lpt.c 325966 2017-11-18 14:26:50Z pfg $");

/*
 * Device Driver for AT parallel printer port
 * Written by William Jolitz 12/18/90
 */

/*
 * Updated for ppbus by Nicolas Souchu
 * [Mon Jul 28 1997]
 */

#include "opt_lpt.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/uio.h>
#include <sys/syslog.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/ppbus/lptio.h>
#include <dev/ppbus/ppbconf.h>
#include <dev/ppbus/ppb_1284.h>
#include <dev/ppbus/lpt.h>
#include "ppbus_if.h"
#include <dev/ppbus/ppbio.h>

#ifndef LPT_DEBUG
#define	lprintf(args)
#else
#define	lprintf(args)						\
		do {						\
			if (lptflag)				\
				printf args;			\
		} while (0)
static int volatile lptflag = 1;
#endif

#define	LPINITRDY	4	/* wait up to 4 seconds for a ready 等待4秒钟 */
#define	LPTOUTINITIAL	10	/* initial timeout to wait for ready 1/10 s 等待就绪的初始超时1/10秒 */
#define	LPTOUTMAX	1	/* maximal timeout 1 s */
#define	LPPRI		(PZERO+8)
#define	BUFSIZE		1024
#define	BUFSTATSIZE	32

struct lpt_data {
	device_t sc_dev;
	struct cdev *sc_cdev;
	struct cdev *sc_cdev_bypass;
	short	sc_state;
	/* default case: negative prime, negative ack, handshake strobe,
	   prime once - 默认情况：负启动，负确认，握手选通，启动一次 */
	u_char	sc_control;
	char	sc_flags;
#define	LP_POS_INIT	0x04	/* if we are a positive init signal 如果我们是一个正的初始信号 */
#define	LP_POS_ACK	0x08	/* if we are a positive going ack */
#define	LP_NO_PRIME	0x10	/* don't prime the printer at all 根本不要给打印机充油 */
#define	LP_PRIMEOPEN	0x20	/* prime on every open 每次打开都要充油 */
#define	LP_AUTOLF	0x40	/* tell printer to do an automatic lf 告诉打印机自动打印 */
#define	LP_BYPASS	0x80	/* bypass  printer ready checks 绕过打印机就绪检查 */
	void	*sc_inbuf;
	void	*sc_statbuf;
	short	sc_xfercnt ;
	char	sc_primed;
	char	*sc_cp ;
	u_short	sc_irq ;	/* IRQ status of port 端口的中断状态 */
#define	LP_HAS_IRQ	0x01	/* we have an irq available 我们拥有一个可用中断 */
#define	LP_USE_IRQ	0x02	/* we are using our irq 正在使用中断 */
#define	LP_ENABLE_IRQ	0x04	/* enable IRQ on open 开启的时候使能中断 */
#define	LP_ENABLE_EXT	0x10	/* we shall use advanced mode when possible 如果可能的话，我们将使用高级模式 */
	u_char	sc_backoff ;	/* time to call lptout() again */
	struct callout sc_timer;

	struct resource *sc_intr_resource;	/* interrupt resource */
	void	*sc_intr_cookie;		/* interrupt cookie */
};

#define	LPT_NAME	"lpt"		/* our official name 官方名称 */

static timeout_t lptout;
static int	lpt_port_test(device_t dev, u_char data, u_char mask);
static int	lpt_detect(device_t dev);

#define	DEVTOSOFTC(dev) \
	((struct lpt_data *)device_get_softc(dev))

static void lptintr(void *arg);

static devclass_t lpt_devclass;


/* bits for state */
#define	OPEN		(1<<0)	/* device is open 设备已经打开 */
#define	ASLP		(1<<1)	/* awaiting draining of printer 等待打印机排空 */
#define	EERROR		(1<<2)	/* error was received from printer 从打印机接收到错误 */
#define	OBUSY		(1<<3)	/* printer is busy doing output 打印机正在打印 */
#define	LPTOUT		(1<<4)	/* timeout while not selected 允许超时 */
#define	TOUT		(1<<5)	/* timeout while not selected */
#define	LPTINIT		(1<<6)	/* waiting to initialize for open 正在 lpt_open 初始化过程中 */
#define	INTERRUPTED	(1<<7)	/* write call was interrupted 写入呼叫被中断 */
#define	HAVEBUS		(1<<8)	/* the driver owns the bus */

/* status masks to interrogate printer status 询问打印机状态的状态掩码 */
#define	RDY_MASK	(LPS_SEL|LPS_OUT|LPS_NBSY|LPS_NERR)	/* ready ? */
#define	LP_READY	(LPS_SEL|LPS_NBSY|LPS_NERR)

/* Printer Ready condition  - from lpa.c */
/* Only used in polling code */
#define	LPS_INVERT	(LPS_NBSY | LPS_NACK |           LPS_SEL | LPS_NERR)
#define	LPS_MASK	(LPS_NBSY | LPS_NACK | LPS_OUT | LPS_SEL | LPS_NERR)
#define	NOT_READY(ppbus) ((ppb_rstr(ppbus)^LPS_INVERT)&LPS_MASK)

#define	MAX_SLEEP	(hz*5)	/* Timeout while waiting for device ready 等待20微秒 */
#define	MAX_SPIN	20	/* Max delay for device ready in usecs 等待的超时时间 */


static	d_open_t	lptopen;
static	d_close_t	lptclose;
static	d_write_t	lptwrite;
static	d_read_t	lptread;
static	d_ioctl_t	lptioctl;

static struct cdevsw lpt_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	lptopen,
	.d_close =	lptclose,
	.d_read =	lptread,
	.d_write =	lptwrite,
	.d_ioctl =	lptioctl,
	.d_name =	LPT_NAME,
};

static int
lpt_request_ppbus(device_t dev, int how)
{
	device_t ppbus = device_get_parent(dev);
	struct lpt_data *sc = DEVTOSOFTC(dev);
	int error;

	/*
	 * We might already have the bus for a write(2) after an interrupted
	 * write(2) call.
	 */
	ppb_assert_locked(ppbus);
	if (sc->sc_state & HAVEBUS)
		return (0);

	error = ppb_request_bus(ppbus, dev, how);
	if (error == 0)
		sc->sc_state |= HAVEBUS;
	return (error);
}

static int
lpt_release_ppbus(device_t dev)
{
	device_t ppbus = device_get_parent(dev);
	struct lpt_data *sc = DEVTOSOFTC(dev);
	int error = 0;

	ppb_assert_locked(ppbus);
	if (sc->sc_state & HAVEBUS) {
		error = ppb_release_bus(ppbus, dev);
		if (error == 0)
			sc->sc_state &= ~HAVEBUS;
	}
	return (error);
}

/*
 * Internal routine to lptprobe to do port tests of one byte value
 */
static int
lpt_port_test(device_t ppbus, u_char data, u_char mask)
{
	int	temp, timeout;

	data = data & mask;
	ppb_wdtr(ppbus, data);	// 将data写入并口的数据寄存器
	timeout = 10000;
	do {
		DELAY(10);
		temp = ppb_rdtr(ppbus) & mask;	// 从该寄存器读取数据
	}
	while (temp != data && --timeout);	// 将读取到的数据跟写入的数据进行比较
	lprintf(("out=%x\tin=%x\ttout=%d\n", data, temp, timeout));
	return (temp == data);
}

/*
 * Probe simplified by replacing multiple loops with a hardcoded
 * test pattern - 1999/02/08 des@freebsd.org
 *
 * New lpt port probe Geoff Rehmet - Rhodes University - 14/2/94
 * Based partially on Rod Grimes' printer probe
 *
 * Logic:
 *	1) If no port address was given, use the bios detected ports
 *	   and autodetect what ports the printers are on.
 			 如果未提供端口地址，请使用bios检测到的端口和自动检测打印机所在的端口

 *	2) Otherwise, probe the data port at the address given,
 *	   using the method in Rod Grimes' port probe.
 *	   (Much code ripped off directly from Rod's probe.)
 *
 * Comments from Rod's probe:
 * Logic:
 *	1) You should be able to write to and read back the same value
 *	   to the data port.  Do an alternating zeros, alternating ones,
 *	   walking zero, and walking one test to check for stuck bits.
 *
 *	2) You should be able to write to and read back the same value
 *	   to the control port lower 5 bits, the upper 3 bits are reserved
 *	   per the IBM PC technical reference manauls and different boards
 *	   do different things with them.  Do an alternating zeros, alternating
 *	   ones, walking zero, and walking one test to check for stuck bits.
 *
 *	   Some printers drag the strobe line down when the are powered off
 * 	   so this bit has been masked out of the control port test.
 *
 *	   XXX Some printers may not like a fast pulse on init or strobe, I
 *	   don't know at this point, if that becomes a problem these bits
 *	   should be turned off in the mask byte for the control port test.
 *
 *	   We are finally left with a mask of 0x14, due to some printers
 *	   being adamant about holding other bits high ........
 *
 *	   Before probing the control port, we write a 0 to the data port -
 *	   If not, some printers chuck out garbage when the strobe line
 *	   gets toggled.
 *
 *	3) Set the data and control ports to a value of 0
 *
 *	This probe routine has been tested on Epson Lx-800, HP LJ3P,
 *	Epson FX-1170 and C.Itoh 8510RM
 *	printers.
 *	Quick exit on fail added.
 */
static int
lpt_detect(device_t dev)
{
	device_t ppbus = device_get_parent(dev);	// 父设备应该就是总线

	static u_char	testbyte[18] = {
		0x55,			/* alternating zeros */
		0xaa,			/* alternating ones */
		0xfe, 0xfd, 0xfb, 0xf7,
		0xef, 0xdf, 0xbf, 0x7f,	/* walking zero */
		0x01, 0x02, 0x04, 0x08,
		0x10, 0x20, 0x40, 0x80	/* walking one */
	};
	int		i, error, status;

	status = 1;				/* assume success */

	ppb_lock(ppbus);	// 将总线加锁
	
	// 将lpt指派为并口的宿主，也就是说让打印机拿到总线的使用权
	if ((error = lpt_request_ppbus(dev, PPB_DONTWAIT))) {
		ppb_unlock(ppbus);
		device_printf(dev, "cannot alloc ppbus (%d)!\n", error);
		return (0);
	}

	for (i = 0; i < 18 && status; i++)
		if (!lpt_port_test(ppbus, testbyte[i], 0xff)) {
			/*
				调用该函数测试对并口数据寄存器的写入和读出操作，用于测试此8位寄存器的数据
				存放于 testbyte 数组。这些值都是经过专门设计的，为的是能够触发所有的8个位
			*/
			status = 0;
			break;
		}

	/* write 0's to control and data ports 
			测试完成后，把并口的数据和控制寄存器清零
	*/
	ppb_wdtr(ppbus, 0);
	ppb_wctr(ppbus, 0);

	lpt_release_ppbus(dev); // 放弃对并口的所有权
	ppb_unlock(ppbus);	// 释放并口互斥锁

	return (status);
}

// identify 函数是为了向总线添加设备
static void
lpt_identify(driver_t *driver, device_t parent)
{

	device_t dev;

	/*
		首先判断是否之前添加过这个设备，如果没有的话在重新添加
	*/
	dev = device_find_child(parent, LPT_NAME, -1);
	if (!dev)
		BUS_ADD_CHILD(parent, 0, LPT_NAME, -1);
}

/*
 * lpt_probe()
 */
static int
lpt_probe(device_t dev)
{

	if (!lpt_detect(dev))
		return (ENXIO);

	device_set_desc(dev, "Printer");

	return (0);
}

static int
lpt_attach(device_t dev)
{
	device_t ppbus = device_get_parent(dev);
	struct lpt_data *sc = DEVTOSOFTC(dev);
	int rid = 0, unit = device_get_unit(dev);
	int error;

	sc->sc_primed = 0;	/* not primed yet 表示打印机目前需要装填 */
	ppb_init_callout(ppbus, &sc->sc_timer, 0);	// 初始化callout

	ppb_lock(ppbus);
	if ((error = lpt_request_ppbus(dev, PPB_DONTWAIT))) {	// request就是获取并口总线的使用权
		ppb_unlock(ppbus);
		device_printf(dev, "cannot alloc ppbus (%d)!\n", error);
		return (0);
	}

	/*
		将16针(也称为 nINIT 位)的电信号从高电平改成低电平，这将导致打印机启动内部重置；
		多数信号是高电平有效，而 nINIT 中的n表示该信号为低电平有效
	*/ 
	ppb_wctr(ppbus, LPC_NINIT);
	lpt_release_ppbus(dev);
	ppb_unlock(ppbus);

	/* declare our interrupt handler 中断资源分配 */
	sc->sc_intr_resource = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE);
	if (sc->sc_intr_resource) {
		/*
			将 lptintr 注册为中断处理函数。如果成功，函数将会把 sc->sc_irq_status 赋值为 LP_HAS_IRQ、
			LP_USE_IRQ 和 LP_ENABLE_IRQ 三个标志之一，表示此打印机是中断驱动的
		*/
		error = bus_setup_intr(dev, sc->sc_intr_resource,
		    INTR_TYPE_TTY | INTR_MPSAFE, NULL, lptintr, sc,
		    &sc->sc_intr_cookie);
		if (error) {
			bus_release_resource(dev, SYS_RES_IRQ, rid,
			    sc->sc_intr_resource);
			device_printf(dev,
			    "Unable to register interrupt handler\n");
			return (error);
		}
		sc->sc_irq = LP_HAS_IRQ | LP_USE_IRQ | LP_ENABLE_IRQ;
		device_printf(dev, "Interrupt-driven port\n");
	} else {
		sc->sc_irq = 0;
		device_printf(dev, "Polled port\n");
	}
	lprintf(("irq %x\n", sc->sc_irq));

	/*
		为两个缓冲区分配内存，第一个用于维护打印机的数据；第二个用于维护打印机的状态
	*/
	sc->sc_inbuf = malloc(BUFSIZE, M_DEVBUF, M_WAITOK);
	sc->sc_statbuf = malloc(BUFSTATSIZE, M_DEVBUF, M_WAITOK);
	sc->sc_dev = dev;

	/* 
		创建两个设备节点 lpt%d 和 lpt%d.ctl，其中 %d 表示单位号。需要注意的是 lpt%d.ctl 
		包含了 LP_BYPASS
		lpt%d 其实表示的是该打印机，而 lpt%d.ctl 仅仅是在需要改变打印机操作模式的时候才会用到
	*/
	sc->sc_cdev = make_dev(&lpt_cdevsw, unit,
	    UID_ROOT, GID_WHEEL, 0600, LPT_NAME "%d", unit);
	sc->sc_cdev->si_drv1 = sc;
	sc->sc_cdev->si_drv2 = 0;
	sc->sc_cdev_bypass = make_dev(&lpt_cdevsw, unit,
	    UID_ROOT, GID_WHEEL, 0600, LPT_NAME "%d.ctl", unit);
	sc->sc_cdev_bypass->si_drv1 = sc;
	sc->sc_cdev_bypass->si_drv2 = (void *)LP_BYPASS;
	return (0);
}

static int
lpt_detach(device_t dev)
{
	struct lpt_data *sc = DEVTOSOFTC(dev);
	device_t ppbus = device_get_parent(dev);

	// 在 attach 函数中 make_dev 出的两个设备，detach 函数中要调用 destroy_dev 函数给注销掉
	destroy_dev(sc->sc_cdev);
	destroy_dev(sc->sc_cdev_bypass);

	// 每次获取或释放总线使用权的时候，要加锁
	ppb_lock(ppbus);
	lpt_release_ppbus(dev);
	ppb_unlock(ppbus);
	callout_drain(&sc->sc_timer);
	if (sc->sc_intr_resource != NULL) {
		bus_teardown_intr(dev, sc->sc_intr_resource,
		    sc->sc_intr_cookie);	// 释放中断
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_intr_resource);	// 释放资源
	}

	// malloc 申请的空间释放掉
	free(sc->sc_inbuf, M_DEVBUF);
	free(sc->sc_statbuf, M_DEVBUF);

	return (0);
}

/* 该函数是 lpt 的 callout 函数，用于处理错过的或者未处理的中断 */
static void
lptout(void *arg)
{
	struct lpt_data *sc = arg;
	device_t dev = sc->sc_dev;
	device_t ppbus;

	ppbus = device_get_parent(dev);
	ppb_assert_locked(ppbus);
	lprintf(("T %x ", ppb_rstr(ppbus)));
	/*
		首先检查 lpt%d 是否打开，如果是的话，lptout 函数会推迟执行
	*/
	if (sc->sc_state & OPEN) {
		sc->sc_backoff++;
		if (sc->sc_backoff > hz/LPTOUTMAX)
			sc->sc_backoff = hz/LPTOUTMAX;
		callout_reset(&sc->sc_timer, sc->sc_backoff, lptout, sc);
	} else
		sc->sc_state &= ~TOUT;

	if (sc->sc_state & EERROR)
		sc->sc_state &= ~EERROR;

	/*
	 * Avoid possible hangs due to missed interrupts
	 * 如果错过了中断，函数就会调用 lptintr 以重启向打印机的数据传输；如果没有这个处理分支，
	 * 那lpt将会挂起并一直等待它所发出后丢失的中断
	 */
	if (sc->sc_xfercnt) {
		lptintr(sc);
	} else {
		sc->sc_state &= ~OBUSY;
		wakeup(dev);
	}
}

/*
 * lptopen -- reset the printer, then wait until it's selected and not busy.
 * 重新设置打印机，然后等待它被选中并且不忙
 * 
 *	If LP_BYPASS flag is selected, then we do not try to select the
 *	printer -- this is just used for passing ioctls.
 		如果选择了 LP_BYPASS 标志，则不尝试选择打印机——这只是用于传递ioctl
 */

static	int
lptopen(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	int trys, err;
	struct lpt_data *sc = dev->si_drv1;	// si_drv1 成员表示的好像是上下文信息
	device_t lptdev;
	device_t ppbus;

	if (!sc)
		return (ENXIO);

	lptdev = sc->sc_dev;
	ppbus = device_get_parent(lptdev);

	ppb_lock(ppbus);

	// 检查状态，如果不是0的话，就表明目前有另外一个进程正在使用打印机。注意上面对总线加锁
	if (sc->sc_state) {
		lprintf(("%s: still open %x\n", device_get_nameunit(lptdev),
		    sc->sc_state));
		ppb_unlock(ppbus);
		return(EBUSY);
	} else
		sc->sc_state |= LPTINIT;

	sc->sc_flags = (uintptr_t)dev->si_drv2;

	/* Check for open with BYPASS flag set. 
		检查 sc_flags 的值是否包含有 LP_BYPASS，如果有的话直接把状态设置为 OPEN，然后退出。
		这也意味着该设备结点是 lpt%d.ctl。lpt%d.ctl 仅在要改变打印机操作模式的时候才会用到，
		因而只有少量的工作要做
	*/
	if (sc->sc_flags & LP_BYPASS) {
		sc->sc_state = OPEN;
		ppb_unlock(ppbus);
		return(0);
	}

	/* request the ppbus only if we don't have it already 打印机获取总线的使用权 */
	if ((err = lpt_request_ppbus(lptdev, PPB_WAIT|PPB_INTR)) != 0) {
		/* give it a chance to try later */
		sc->sc_state = 0;
		ppb_unlock(ppbus);
		return (err);
	}

	lprintf(("%s flags 0x%x\n", device_get_nameunit(lptdev),
	    sc->sc_flags));

	/* set IRQ status according to ENABLE_IRQ flag 中断设置
	 */
	if (sc->sc_irq & LP_ENABLE_IRQ)
		sc->sc_irq |= LP_USE_IRQ;
	else
		sc->sc_irq &= ~LP_USE_IRQ;

	/* init printer 重启打印机 */
	if ((sc->sc_flags & LP_NO_PRIME) == 0) {
		if ((sc->sc_flags & LP_PRIMEOPEN) || sc->sc_primed == 0) {
			ppb_wctr(ppbus, 0);	// 装填打印机
			sc->sc_primed++;
			DELAY(500);
		}
	}
	/*
		重启打印机，其中当并口第17针(即nSELIN位)的电信号从高电平变成低电平时，打印机被
		选中，从而开始准备接收数据
	*/
	ppb_wctr(ppbus, LPC_SEL|LPC_NINIT);

	/* wait till ready (printer running diagnostics) 
		等待直到可用，这个间隔是打印机用来运行诊断程序
	*/
	trys = 0;
	do {
		/* ran out of waiting for the printer 打印机时间用完了，放弃吗？*/
		if (trys++ >= LPINITRDY*4) {
			lprintf(("status %x\n", ppb_rstr(ppbus)));

			lpt_release_ppbus(lptdev);
			sc->sc_state = 0;
			ppb_unlock(ppbus);
			return (EBUSY);
		}

		/* wait 1/4 second, give up if we get a signal 
			等待1/4，如果我们收到了一个信号则放弃
		*/
		if (ppb_sleep(ppbus, lptdev, LPPRI | PCATCH, "lptinit",
		    hz / 4) != EWOULDBLOCK) {
			lpt_release_ppbus(lptdev);
			sc->sc_state = 0;
			ppb_unlock(ppbus);
			return (EBUSY);
		}

		/* is printer online and ready for output */
	} while ((ppb_rstr(ppbus) &
			(LPS_SEL|LPS_OUT|LPS_NBSY|LPS_NERR)) !=
					(LPS_SEL|LPS_NBSY|LPS_NERR));

	sc->sc_control = LPC_SEL|LPC_NINIT;	// 选择并且重置打印机
	if (sc->sc_flags & LP_AUTOLF)
		sc->sc_control |= LPC_AUTOL;	// 启用自动换行

	/* enable interrupt if interrupt-driven */
	if (sc->sc_irq & LP_USE_IRQ)
		sc->sc_control |= LPC_ENA;	// 当打印机通过中断驱动的时候，使能中断

	ppb_wctr(ppbus, sc->sc_control);	// 然后将控制信息写入到控制寄存器当中

	sc->sc_state &= ~LPTINIT;
	sc->sc_state |= OPEN;
	sc->sc_xfercnt = 0;

	/* only use timeout if using interrupt */
	lprintf(("irq %x\n", sc->sc_irq));
	if (sc->sc_irq & LP_USE_IRQ) {
		sc->sc_state |= TOUT;
		sc->sc_backoff = hz / LPTOUTINITIAL;
		callout_reset(&sc->sc_timer, sc->sc_backoff, lptout, sc);
	}

	/* release the ppbus */
	lpt_release_ppbus(lptdev);
	ppb_unlock(ppbus);

	lprintf(("opened.\n"));
	return(0);
}

/*
 * lptclose -- close the device, free the local line buffer.
 *
 * Check for interrupted write call added.
 */

static	int
lptclose(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	struct lpt_data *sc = dev->si_drv1;
	device_t lptdev = sc->sc_dev;
	device_t ppbus = device_get_parent(lptdev);
	int err;

	ppb_lock(ppbus);
	if (sc->sc_flags & LP_BYPASS)
		goto end_close;

	if ((err = lpt_request_ppbus(lptdev, PPB_WAIT|PPB_INTR)) != 0) {
		ppb_unlock(ppbus);
		return (err);
	}

	/* if the last write was interrupted, don't complete it */
	if ((!(sc->sc_state  & INTERRUPTED)) && (sc->sc_irq & LP_USE_IRQ))
		while ((ppb_rstr(ppbus) &
			(LPS_SEL|LPS_OUT|LPS_NBSY|LPS_NERR)) !=
			(LPS_SEL|LPS_NBSY|LPS_NERR) || sc->sc_xfercnt)
			/* wait 1 second, give up if we get a signal */
			if (ppb_sleep(ppbus, lptdev, LPPRI | PCATCH, "lpclose",
			    hz) != EWOULDBLOCK)
				break;

	sc->sc_state &= ~OPEN;
	callout_stop(&sc->sc_timer);
	ppb_wctr(ppbus, LPC_NINIT);

	/*
	 * unregistration of interrupt forced by release
	 */
	lpt_release_ppbus(lptdev);

end_close:
	sc->sc_state = 0;
	sc->sc_xfercnt = 0;
	ppb_unlock(ppbus);
	lprintf(("closed.\n"));
	return(0);
}

/*
 * lpt_pushbytes()
 *	Workhorse for actually spinning and writing bytes to printer
 *	Derived from lpa.c
 *	Originally by ?
 *
 *	This code is only used when we are polling the port
 */
static int
lpt_pushbytes(struct lpt_data *sc)
{
	device_t dev = sc->sc_dev;
	device_t ppbus = device_get_parent(dev);
	int spin, err, tic;
	char ch;

	ppb_assert_locked(ppbus);
	lprintf(("p"));
	/* loop for every character .. */
	while (sc->sc_xfercnt > 0) {
		/* printer data */
		ch = *(sc->sc_cp);
		sc->sc_cp++;
		sc->sc_xfercnt--;

		/*
		 * Wait for printer ready.
		 * Loop 20 usecs testing BUSY bit, then sleep
		 * for exponentially increasing timeout. (vak)
		 */
		for (spin = 0; NOT_READY(ppbus) && spin < MAX_SPIN; ++spin)
			DELAY(1);	/* XXX delay is NOT this accurate! */
		if (spin >= MAX_SPIN) {
			tic = 0;
			while (NOT_READY(ppbus)) {
				/*
				 * Now sleep, every cycle a
				 * little longer ..
				 */
				tic = tic + tic + 1;
				/*
				 * But no more than 10 seconds. (vak)
				 */
				if (tic > MAX_SLEEP)
					tic = MAX_SLEEP;
				err = ppb_sleep(ppbus, dev, LPPRI,
					LPT_NAME "poll", tic);
				if (err != EWOULDBLOCK) {
					return (err);
				}
			}
		}

		/* output data */
		ppb_wdtr(ppbus, ch);
		/* strobe */
		ppb_wctr(ppbus, sc->sc_control|LPC_STB);
		ppb_wctr(ppbus, sc->sc_control);

	}
	return(0);
}

/*
 * lptread --retrieve printer status in IEEE1284 NIBBLE mode
 * lptread 函数用于查询打印机的状态，用户可以通过对设备节点 lpt%d 执行 cat
 * 命令来获取打印机的状态信息
 */

static int
lptread(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct lpt_data *sc = dev->si_drv1;
	device_t lptdev = sc->sc_dev;
	device_t ppbus = device_get_parent(lptdev);
	int error = 0, len;

	/*
		如果 sc_flags 包含有 LP_BYPASS 标志，则表示该设备节点是 lpt%d.ctl，是不允许
		执行相应的操作的(cat获取状态信息，它只是用于ioctl)
	*/
	if (sc->sc_flags & LP_BYPASS) {
		/* we can't do reads in bypass mode */
		return (EPERM);
	}

	ppb_lock(ppbus);
	// 通过调用该函数将并口接口设置为半字节模式
	if ((error = ppb_1284_negociate(ppbus, PPB_NIBBLE, 0))) {
		ppb_unlock(ppbus);
		return (error);
	}

	/* read data in an other buffer, read/write may be simultaneous */
	len = 0;
	while (uio->uio_resid) {
		// 该函数用于实现从打印机向内核空间传递数据，len表示传递的数据的字节数
		if ((error = ppb_1284_read(ppbus, PPB_NIBBLE,
				sc->sc_statbuf, min(BUFSTATSIZE,
				uio->uio_resid), &len))) {
			goto error;
		}

		if (!len)
			goto error;		/* no more data */

		ppb_unlock(ppbus);
		// 内核空间向用户空间传递数据
		error = uiomove(sc->sc_statbuf, len, uio);
		ppb_lock(ppbus);
		if (error)
			goto error;
	}

error:
	ppb_1284_terminate(ppbus);
	ppb_unlock(ppbus);
	return (error);
}

/*
 * lptwrite --copy a line from user space to a local buffer, then call
 * putc to get the chars moved to the output queue.
 * 将用户空间的数据存储到 sc->sc_inbuf 当中，随后发送到打印机中进行打印
 *
 * Flagging of interrupted write added.	添加中断写入标志
 */

static	int
lptwrite(struct cdev *dev, struct uio *uio, int ioflag)
{
	register unsigned n;
	int err;
	struct lpt_data *sc = dev->si_drv1;
	device_t lptdev = sc->sc_dev;
	device_t ppbus = device_get_parent(lptdev);

	// 只要是包含 bypass 标志，就表示是设备控制结点
	if (sc->sc_flags & LP_BYPASS) {
		/* we can't do writes in bypass mode */
		return (EPERM);
	}

	/* request the ppbus only if we don't have it already 
		只有在我们还没有ppbus的时候才请求它
	*/
	ppb_lock(ppbus);
	if ((err = lpt_request_ppbus(lptdev, PPB_WAIT|PPB_INTR)) != 0) {
		ppb_unlock(ppbus);
		return (err);
	}

	sc->sc_state &= ~INTERRUPTED;
	/*
		while 循环是为了计算从用户空间拷贝到内核空间的数据的总数量
	*/
	while ((n = min(BUFSIZE, uio->uio_resid)) != 0) {
		sc->sc_cp = sc->sc_inbuf;
		ppb_unlock(ppbus);
		err = uiomove(sc->sc_cp, n, uio);	// 从用户空间到内核空间
		ppb_lock(ppbus);
		if (err)
			break;
		sc->sc_xfercnt = n;	// sc_xfercnt 变量则会在每次发送1个字节给打印机后递减1

		// 下面是描述从内核空间向打印机传送数据的方法，一共是三种
		if (sc->sc_irq & LP_ENABLE_EXT) {
			/* try any extended mode 
				第一种，当开启扩展模式时，可以通过 ppb_write 函数直接向打印机写数据
			*/
			err = ppb_write(ppbus, sc->sc_cp,
					sc->sc_xfercnt, 0);
			switch (err) {
			case 0:
				/* if not all data was sent, we could rely
				 * on polling for the last bytes */
				sc->sc_xfercnt = 0;
				break;
			case EINTR:
				sc->sc_state |= INTERRUPTED;
				ppb_unlock(ppbus);
				return (err);
			case EINVAL:
				/* advanced mode not avail */
				log(LOG_NOTICE,
				    "%s: advanced mode not avail, polling\n",
				    device_get_nameunit(sc->sc_dev));
				break;
			default:
				ppb_unlock(ppbus);
				return (err);
			}
		} else while ((sc->sc_xfercnt > 0)&&(sc->sc_irq & LP_USE_IRQ)) {
			lprintf(("i"));
			/* if the printer is ready for a char, */
			/* give it one 第二种，通过中断函数 lptintr 来向打印出传递数据 */
			if ((sc->sc_state & OBUSY) == 0){
				lprintf(("\nC %d. ", sc->sc_xfercnt));
				lptintr(sc);
			}
			lprintf(("W "));
			if (sc->sc_state & OBUSY)
				if ((err = ppb_sleep(ppbus, lptdev,
					 LPPRI|PCATCH, LPT_NAME "write", 0))) {
					sc->sc_state |= INTERRUPTED;
					ppb_unlock(ppbus);
					return(err);
				}
		}

		/* check to see if we must do a polled write
			如果上述两种方式都不可行的话，那就调用 lpt_pushbytes 发起轮询传输 
		*/
		if (!(sc->sc_irq & LP_USE_IRQ) && (sc->sc_xfercnt)) {
			lprintf(("p"));

			err = lpt_pushbytes(sc);

			if (err) {
				ppb_unlock(ppbus);
				return (err);
			}
		}
	}

	/* we have not been interrupted, release the ppbus */
	lpt_release_ppbus(lptdev);
	ppb_unlock(ppbus);

	return (err);
}

/*
 * lptintr -- handle printer interrupts which occur when the printer is
 * ready to accept another char.
 * 该函数从 sc->inbuf 中每次读取一个字节向打印机传递后退出。当打印机可以接受下一个字节时，
 * 它会再发送一个中断。从代码逻辑可以看出，对buf的读取是通过 sc->sc_cp 来实现的
 *
 * do checking for interrupted write call.
 */
static void
lptintr(void *arg)
{
	struct lpt_data *sc = arg;
	device_t lptdev = sc->sc_dev;
	device_t ppbus = device_get_parent(lptdev);
	int sts = 0;
	int i;

	/*
	 * Is printer online and ready for output?
	 *
	 * Avoid falling back to lptout() too quickly.  First spin-loop
	 * to see if the printer will become ready ``really soon now''.
	 * for循环用于确认打印机确实在线并且可以用于输出
	 */
	for (i = 0; i < 100 &&
	     ((sts=ppb_rstr(ppbus)) & RDY_MASK) != LP_READY; i++) ;

	if ((sts & RDY_MASK) == LP_READY) {
		sc->sc_state = (sc->sc_state | OBUSY) & ~EERROR;	// 清除 EERROR 标志
		sc->sc_backoff = hz / LPTOUTINITIAL;	// 重置 sc_backoff

		if (sc->sc_xfercnt) {
			/* send char */
			/*lprintf(("%x ", *sc->sc_cp)); */
			ppb_wdtr(ppbus, *sc->sc_cp++) ;	// 将 buf中的1个字节写入到并口的数据寄存器
			ppb_wctr(ppbus, sc->sc_control|LPC_STB);	// 将数据发送到发送到打印机
			/* DELAY(X) */
			ppb_wctr(ppbus, sc->sc_control);

			/* any more data for printer 
				如果还需要更多的数据发送，函数将退出。因为根据协议，必须要等到新的中断触发的时候才能
				发送下一个字节
			*/
			if (--(sc->sc_xfercnt) > 0)
				return;
		}

		/*
		 * No more data waiting for printer.
		 * Wakeup is not done if write call was not interrupted.
		 * 这里表示已经没有其他的数据需要再发送了，那就把 state 清除 OBUSY 标志并唤醒 lptdev
		 */
		sc->sc_state &= ~OBUSY;

		if (!(sc->sc_state & INTERRUPTED))
			wakeup(lptdev);
		lprintf(("w "));
		return;
	} else	{	/* check for error */
		if (((sts & (LPS_NERR | LPS_OUT) ) != LPS_NERR) &&
				(sc->sc_state & OPEN))
			sc->sc_state |= EERROR;
		/* lptout() will jump in and try to restart. */
	}
	lprintf(("sts %x ", sts));
}

static	int
lptioctl(struct cdev *dev, u_long cmd, caddr_t data, int flags, struct thread *td)
{
	int	error = 0;
	struct lpt_data *sc = dev->si_drv1;
	device_t ppbus;
	u_char	old_sc_irq;	/* old printer IRQ status */

	switch (cmd) {
	case LPT_IRQ :
		ppbus = device_get_parent(sc->sc_dev);
		ppb_lock(ppbus);
		if (sc->sc_irq & LP_HAS_IRQ) {
			/*
			 * NOTE:
			 * If the IRQ status is changed,
			 * this will only be visible on the
			 * next open.
			 *
			 * If interrupt status changes,
			 * this gets syslog'd.
			 */
			old_sc_irq = sc->sc_irq;
			switch (*(int*)data) {
			case 0:
				sc->sc_irq &= (~LP_ENABLE_IRQ);
				break;
			case 1:
				sc->sc_irq &= (~LP_ENABLE_EXT);
				sc->sc_irq |= LP_ENABLE_IRQ;
				break;
			case 2:
				/* classic irq based transfer and advanced
				 * modes are in conflict
				 */
				sc->sc_irq &= (~LP_ENABLE_IRQ);
				sc->sc_irq |= LP_ENABLE_EXT;
				break;
			case 3:
				sc->sc_irq &= (~LP_ENABLE_EXT);
				break;
			default:
				break;
			}

			if (old_sc_irq != sc->sc_irq )
				log(LOG_NOTICE, "%s: switched to %s %s mode\n",
					device_get_nameunit(sc->sc_dev),
					(sc->sc_irq & LP_ENABLE_IRQ)?
					"interrupt-driven":"polled",
					(sc->sc_irq & LP_ENABLE_EXT)?
					"extended":"standard");
		} else /* polled port */
			error = EOPNOTSUPP;
		ppb_unlock(ppbus);
		break;
	default:
		error = ENODEV;
	}

	return(error);
}

static device_method_t lpt_methods[] = {
	/* device interface */
	DEVMETHOD(device_identify,	lpt_identify),
	DEVMETHOD(device_probe,		lpt_probe),
	DEVMETHOD(device_attach,	lpt_attach),
	DEVMETHOD(device_detach,	lpt_detach),

	{ 0, 0 }
};

static driver_t lpt_driver = {
	LPT_NAME,
	lpt_methods,
	sizeof(struct lpt_data),
};

DRIVER_MODULE(lpt, ppbus, lpt_driver, lpt_devclass, 0, 0);
MODULE_DEPEND(lpt, ppbus, 1, 1, 1);
