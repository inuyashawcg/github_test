#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/ioccom.h>
#include <sys/queue.h>
#include "race.iotcl.h"

MALLOC_DEFINE(M_RACE, "race", "race object");

struct race_softc {
    LIST_ENTRY(race_softc) list;
    int unit;
};

static LIST_HEAD(, race_softc) race_list = 
    LIST_HEAD_INITIALIZER(&race_list);

static struct race_softc* race_new(void);
static struct race_softc* race_find(int unit);
static void     race_destroy(struct race_softc *sc);
static d_ioctl_t race_ioctl;

static struct cdevsw race_cdevsw = {
    .d_version = D_VERSION,
    .d_ioctl = race_ioctl,
    .d_name = RACE_NAME
};

static int
race_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td) 
{
    struct race_softc *sc;
    int error = 0;

    switch (cmd) {
        case RACE_IOC_ATTACH:
            sc = race_new();
            *(int*)data = sc -> unit;
            break;
        case RACE_IOC_DETACH:
            sc = race_find(*(int*)data);
            if (sc == NULL)
                return (ENOENT);
            race_destroy(sc);
            break;
        case RACE_IOC_QUERY:
            sc = race_find(*(int*)data);
            if (sc == NULL)
                return (ENOENT);
            break;
        case RACE_IOC_LIST:
            uprintf("   UNIT\n");
            LIST_FOREACH(sc, &race_list, list)
                uprintf("   %d\n", sc -> unit);
            break;
        default:
            error = ENOTTY;
            break;
    }
    return error;
}

static struct race_softc*
race_new(void)
{
    struct race_softc *sc;
    int unit, max = -1;

    LIST_FOREACH(sc, &race_list, list) {
        if (sc -> unit > max)
            max = sc -> unit;
    }
    unit = max + 1;
    sc = (struct race_softc*) malloc(sizeof(struct race_softc),
        M_RACE, M_WAITOK | M_ZERO);
    sc -> unit = unit;
    LIST_INSERT_HEAD(&race_list, sc, list);
    return sc;
}