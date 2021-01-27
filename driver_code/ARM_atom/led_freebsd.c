#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>

#define BUFFER_SIZE 256

/* qemu */
#define TARGET_REGION_ADDR 0x100000
#define TARGET_REGION_SIZE 0x1000
enum {
    FINISHER_FAIL = 0x3333,
    FINISHER_PASS = 0x5555,
    FINISHER_RESET = 0x7777
};

static d_open_t     dev_open;
static d_close_t    dev_close;
static d_write_t    dev_write;
 
static struct cdevsw dev_cdevsw = {
    .d_version = D_VERSION,
    .d_open = dev_open,
    .d_close = dev_close,
    .d_write = dev_write,
    .d_name = "dev_test"
};

typedef struct dev_buffer {
	char buffer[BUFFER_SIZE];
	int length;
} cmd_buffer_t;

static cmd_buffer_t *cmd_buffer;
static struct cdev *dev_test;

extern struct bus_space memmap_bus;

static int
dev_open(struct cdev *dev, int oflags, int devtype, struct thread *td) {
    uprintf("Open dev_test.\n");
    return 0;
}

static int
dev_close(struct cdev *dev, int fflag, int devtype, struct thread *td) {
    uprintf("Close dev_test.\n");
    return 0;
}

/* cmd: reset */
static int
dev_write(struct cdev *dev, struct uio *uio, int ioflag) {
    int error = 0;
    error = copyin(uio->uio_iov->iov_base, cmd_buffer->buffer,
        MIN(uio->uio_iov->iov_len, BUFFER_SIZE - 1));
    if (error != 0) {
        uprintf("Write failed.\n");
        return error;
    }
    *(cmd_buffer->buffer + MIN(uio->uio_iov->iov_len, BUFFER_SIZE - 1)) = 0;
    cmd_buffer->length = MIN(uio->uio_iov->iov_len, BUFFER_SIZE - 1);

    error = strcmp(cmd_buffer->buffer, "reset");
    if (error == 0) {
        vm_offset_t vaddr;
        struct bus_space* tag = &memmap_bus;
        vaddr = (vm_offset_t)pmap_mapdev(TARGET_REGION_ADDR, TARGET_REGION_SIZE);
        tag->bs_w_2(NULL, 0, FINISHER_RESET);
    } else {
        uprintf(" Invalid command.\n");
    }

   return error;
}


static int
dev_modevent(module_t mod_unused, int event, void *arg_unused) {
    int error = 0;

    switch (event) {
        case MOD_LOAD:
            cmd_buffer = malloc(sizeof(cmd_buffer_t), M_TEMP, M_WAITOK);
            dev_test = make_dev(&dev_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600, "dev_test");
            uprintf("dev_test driver loaded.\n");
            break;
        case MOD_UNLOAD:
            destroy_dev(dev_test);
            free(cmd_buffer, M_TEMP);
            uprintf("dev_test driver unloaded.\n");
            break;
        default:
            error = EOPNOTSUPP;
            break;
    }
    return error;
}

DEV_MODULE(dev_test, dev_modevent, NULL);
