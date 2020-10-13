#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>

#define BUFFER_SIZE 256

MALLOC_DEFINE(M_ECHO, "echo buffer", "buffer for echo driver");

// 增加了两个ioctl命令的定义, _IO表示IO control， _IOW表示IO Write，其实也是IO control的一种

#define ECHO_CLEAR_BUFFER _IO('E', 1)
#define ECHO_SET_BUFFER_SIZE _IOW('E', 2, int)

static d_open_t 	echo_open;
static d_close_t	echo_close;
static d_read_t 	echo_read;
static d_write_t 	echo_write;
static d_ioctl_t    echo_ioctl;     // 增添了echo_ioctl函数

static struct cdevsw echo_cdevsw = {
	.d_version = 	D_VERSION,
	.d_open = 	echo_open,
	.d_close = 	echo_close,
	.d_read = 	echo_read,
	.d_write = 	echo_write,
    .d_ioctl =  echo_ioctl,     // 要在其注册表中声明该驱动设备实现了ioctl功能
	.d_name = 	"echo"
};

typedef struct echo {
    int buffer_size;    // 为了适应ioctl函数的加入，新添加一个参数
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
    int amount;     // 添加局部变量
    
    /*
        uiomove函数在每次拷贝完一个字符时会将 uio->uio_resid 成员变量的值减1，而将 uio->offset 成员变量的值加1，
        所以多次调用uiomove函数可以轻松实现大块数据拷贝
        uio->uio_resid 表示剩下还需要传输的数据字节数
        uio->offset 表示数据(此处是字符串)中的偏移量

        echo_message是在内核空间，amount就表示还需要拷贝的字节数。要么是用户发送来的数据总量，即uio->uio_resid，要么是
        缓冲区所能容纳的数据的总量，即buffer size和offset所求的差值，接着从用户空间(uio)传送(uiomove) 该数据量(amount)
        的数据到内核空间(echo_message -> buffer)
    */
    amount = MIN(uio -> uio_resid,
        (echo_message -> buffer_size - 1 - uio -> uio_offset > 0) ? echo_message -> buffer_size - 1 - uio -> uio_offset : 0);
    
    if (amount == 0) 
        return error;

    error = uiomove(echo_message -> buffer, amount, uio);
    if (error != 0){
        uprintf("Write failed.\n");
        return error;
    }

    echo_message -> buffer[amount] = '\0';
    echo_message -> length = amount;

	return (error);
}

static int
echo_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	int error = 0;
	int amount;
	amount = MIN(uio -> uio_resid, (echo_message -> length - uio -> uio_offset > 0) ? echo_message -> length - uio -> uio_offset : 0);

    /*
        这里跟write函数的逻辑刚好是相反的，是从内核空间echo_message传输响应数据量的数据到用户空间(uio)
    */
	error = uiomove(echo_message -> buffer + uio -> uio_offset, amount, uio);

	if (error != 0)
		uprintf("Read failed.\n");
	return (error);
}

// 新增函数，重新调整buffer的大小
static int
echo_set_buffer_size(int size)
{
    int error = 0;

    if (echo_message -> buffer_size == size) 
        return error；

    if (size >= 128 && size <= 512) {
        // 如果原来的缓冲区的长度比新的还要长，那么realloc会截断原来的缓冲区
        echo_message -> buffer = realloc(echo_message -> buffer, size, M_ECHO, M_WAITOK);
        echo_message -> buffer_size = size;

        // 下面的代码也只有在缓冲区被截断之后才会执行，它先修正所存储数据的长度，然后再将其修改为以空结尾
        if (echo_message -> length >= size) {
            echo_message -> length = size - 1;
            echo_message -> buffer[size - 1] = '\0';
        } 
    } else {
        error = EINVAL;
    }

    return error;    
}

// 新增函数，ioctl
static int
echo_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag, struct thread* td)
{
    int error = 0;
    switch (cmd) {
        case ECHO_CLEAR_BUFFER:
            memset(echo_message -> buffer, '\0', echo_message -> buffer_size);
            echo_message -> length = 0;
            uprintf("Buffer cleared.\n");
            break;
        case ECHO_SET_BUFFER_SIZE:
            error = echo_set_buffer_size(*(int*)data);      // data就表示预期调整的缓冲区的大小，从用户空间获取
            if (error == 0)
                uprintf("Buffer resized.\n");
            break;
        default:
            error = ENOTTY;
            break;
    }

    return error;
}


static int
echo_modevent(module_t mod_unused, int event, void *arg_unused)
{
	int error = 0;

	switch (event) {
	case MOD_LOAD:
        /*
            这里用M_ECHO替换了原来的参数，表示用新的malloc类型
            与之前不同的是，这里同时为echo_message和buffer分配了空间，因为在以前的例子中，内存缓冲区(buffer)的大小是不变的，
            所以也就没有必要为其单独分配存储空间
        */
		echo_message = malloc(sizeof(echo_t), M_ECHO, M_WAITOK);

        echo_message -> buffer_size = 256;
        echo_message -> buffer = malloc(echo_message -> buffer_size, M_ECHO, M_WAITOK);

		echo_dev = make_dev(&echo_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600, "echo");
		uprintf("Echo driver loaded.\n");
		break;
	case MOD_UNLOAD:
		destroy_dev(echo_dev);
        // 同上
        free(echo_message -> buffer, M_ECHO);
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
