#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>

#define BUFFER_SIZE 256

/*
    这里新增加了两个malloc_type的定义
*/

MALLOC_DEFINE(M_ECHO, "echo buffer", "buffer for echo driver");

static d_open_t 	echo_open;
static d_close_t	echo_close;
static d_read_t 	echo_read;
static d_write_t 	echo_write;

static struct cdevsw echo_cdevsw = {
	.d_version = 	D_VERSION,
	.d_open = 	echo_open,
	.d_close = 	echo_close,
	.d_read = 	echo_read,
	.d_write = 	echo_write,
	.d_name = 	"echo"
};

typedef struct echo {
	char buffer[BUFFER_SIZE];
	int length;
} echo_t;

static echo_t *echo_message;

static struct cdev *echo_dev;   

static int
echo_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	uprintf("Open echo device.\n");
	return (0);
}

static int
echo_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	uprintf("Close echo device.\n");
	return (0);
}

static int
echo_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	int error = 0;
	error = copyin(uio -> uio_iov -> iov_base, echo_message -> buffer,
		MIN(uio -> uio_iov -> iov_len, BUFFER_SIZE - 1));
	if (error != 0) {
		uprintf("Write failed.\n");
		return error;
	}
	*(echo_message -> buffer + MIN(uio -> uio_iov -> iov_len, BUFFER_SIZE - 1)) = 0;
	echo_message -> length = MIN(uio -> uio_iov -> iov_len, BUFFER_SIZE - 1);
	return (error);
}

static int
echo_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	int error = 0;
	int amount;
	amount = MIN(uio -> uio_resid, (echo_message -> length - uio -> uio_offset > 0) ? echo_message -> length - uio -> uio_offset : 0);
	error = uiomove(echo_message -> buffer + uio -> uio_offset, amount, uio);

	if (error != 0)
		uprintf("Read failed.\n");
	return (error);
}

static int
echo_modevent(module_t mod_unused, int event, void *arg_unused)
{
	int error = 0;

	switch (event) {
	case MOD_LOAD:
        /*
             这里替换成contigmalloc函数, 0和0xffffffff指定了该内存空间的物理地址所在范围，
             对齐到PAGE_SIZE边界并且不会超过1M的地址界限

             contigmalloc函数分配内存的时候总是分配PAGE_SIZE的整数倍大小，所以尽管sizeof计算
             出来的大小只有260字节，但是分配的空间却有4K，也就是通常系统的一个页的大小
        */
		echo_message = contigmalloc(sizeof(echo_t), M_ECHO, M_ZERO | M_WAITOK,
            0, 0xffffffff, PAGE_SIZE, 1024 * 1024);
		echo_dev = make_dev(&echo_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600, "echo");
		uprintf("Echo driver loaded.\n");
		break;
	case MOD_UNLOAD:
		destroy_dev(echo_dev);
        // 同上
		free(echo_message, M_ECHO);
		uprintf("Echo driver unloaded.\n");
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

DEV_MODULE(echo, echo_modevent, NULL);
