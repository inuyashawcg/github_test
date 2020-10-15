#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/lock.h>
#include <sys/module.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#define LED_IO_ADDR     0x404c
#define LED_NUM         2

struct led_softc {
    int                 sc_io_rid;
    struct resource     *sc_io_resource;
    struct cdev         *sc_cdev0;
    struct cdev         *sc_cdev1;
    u_int32_t           sc_open_mask;
    u_int32_t           sc_read_mask;
    struct mtx          sc_mutex;
};

static devclass_t led_devclass;

static d_open_t     led_open;
static d_close_t    led_close;
static d_read_t     led_read;
static d_write_t    led_write;

static struct cdevsw led_cdevsw = {
    .d_version =    D_VERSION,
    .d_open =       led_open,
    .d_close =      led_close,
    .d_read =       led_read,
    .d_write =      led_write,
    .d_name =       "led"
};