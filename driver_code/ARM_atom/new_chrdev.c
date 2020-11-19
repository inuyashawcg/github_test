int major;  /* 主设备号 */
// int minor;  /* 次设备号 */
// dev_t devid;    /* 设备号，主设备号和次设备号统称为设备号 */

// struct cdev testcdev;

// /* 设备操作函数 */
// static struct file_operations test_fops = {
//     .owner = THIS_MODULE,
// };

// testcdev.owner = THIS_MODULE;
// cdev_init(&testcdev, &test_fops);   /* 初始化cdev结构体变量 */
// cdev_add(&testcdev, devid, 1);      /* 添加字符设备 */
// cdev_del(&testcdev);

// if (major) {
//     devid = MKDEV(major, 0);    /* 定义了主设备号 */
//     register_chrdev_region(devid, 1, "test");   /* 大部分驱动程序次设备号都选择0 */
// } else {    /* 未定义主设备号 */
//     alloc_chidev_region(&devid, 0, 1, "test");  /* 申请设备号 */
//     major = MAJOR(devid);   /* 获取分配的主设备号 */
//     minor = MINOR(devid);   /* 获取分配的次设备号 */
// }

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linue/device.h>

#include <asm/mach/map.h>
#include <sam/uaccess.h>
#include <asm/io.h>

#define NEWCHRLED_CNT   1   /* 主设备号 */
#define NEWCHRLED_NAME  "newchrled"     /* 名字 */
#define LEDOFF          0
#define LEDON           1

/* 
    寄存器物理地址 
*/
#define CCM_CCGR1_BASE          (0x020c406c)
#define SW_MUX_GPIO1_IO03_BASE  (0x020e0068)
#define SW_PAD_GPIO1_IO03_BASE  (0x020e02f4)
#define GPIO1_DR_BASE           (0x0209c000)
#define GPIO1_GDIR_BASE         (0x0209c004)

/* 
    映射后的寄存器虚拟地址 
*/
static void __iomem *IMX6U_CCM_CCGR1;
static void __iomem *SW_MUX_GPIO1_IO03;
static void __iomem *SW_PAD_GPIO1_IO03;
static void __iomem *GPIO1_DR;
static void __iomem *GPIO1_GDIR;

/*
    newchrled设备结构体
*/
struct newchrled_dev {
    dev_t devid;            /* 设备号 */
    struct cdev cdev;       /* cdev */
    struct class *class;    /* 类 */
    struct device *device;  /* 设备 */
    int major;              /* 主设备号 */
    int minor;              /* 次设备号 */
};

struct newchrled_dev newchrled; /* led设备 */

/*
 * @description : LED 打开/关闭
 * @param - sta : LEDON(0) 打开 LED，LEDOFF(1) 关闭 LED
 * @return : 无
 */
void led_switch(u8 sta)
{
    u32 val = 0;
    if (sta == LEDON) {
        val = readl(GPIO1_DR);
        val &= ~(1 << 3);
        writel(val, GPIO1_DR);
    } else if (sta == LENOFF) {
        val = readl(GPIO1_DR);
        val |= (1 << 3);
        writel(val, GPIO1_DR);
    }
}

/*
 * @description : 打开设备
 * @param – inode : 传递给驱动的 inode
 * @param - filp : 设备文件，file 结构体有个叫做 private_data 的成员变量
 * 一般在 open 的时候将 private_data 指向设备结构体。
 * @return : 0 成功;其他 失败
*/
static int led_open(struct inode *inode, struct file *filp)
{
    filp -> private_data = &newchrled;  /* 设置私有数据 */
    return 0;
}

/*
 * @description : 从设备读取数据
 * @param - filp : 要打开的设备文件(文件描述符)
 * @param - buf : 返回给用户空间的数据缓冲区
 * @param - cnt : 要读取的数据长度
 * @param - offt : 相对于文件首地址的偏移
 * @return : 读取的字节数，如果为负值，表示读取失败
*/
static ssize_t led_read(struct file *filp, char __user *buf,
                        size_t cnt, loff_t *offt)
{
    return 0;
}

/*
 * @description : 向设备写数据
 * @param - filp : 设备文件，表示打开的文件描述符
 * @param - buf : 要写给设备写入的数据
 * @param - cnt : 要写入的数据长度
 * @param - offt : 相对于文件首地址的偏移
 * @return : 写入的字节数，如果为负值，表示写入失败
*/
static ssize_t led_write(struct file *filp, const char __user *buf,
                        size_t cnt, loff_t *offt)
{
    int retvalue;
    unsigned char databuf[1];
    unsigned char ledstat;

    retvalue = copy_from_user(databuf, buf, cnt);
    if (retvalue < 0) {
        printk("kernel write failed!\r\n");
        return -EFAULT;
    }

    ledstat = databuf[0];

    if (ledstat == LEDON) {
        led_switch(LEDON);
    } else if (ledstat == LEDOFF) {
        led_switch(LEDOFF);
    }
    return 0;
}

/*
 * @description : 关闭/释放设备
 * @param – filp : 要关闭的设备文件(文件描述符)
 * @return : 0 成功;其他 失败
*/
static int led_release(struct inode *inode, struct file *filp)
{
    return 0;
}

/* 设备操作函数 */
static struct file_opetations led_fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .read = led_read,
    .write = led_write,
    .release = led_release,
};

/*
 * @description : 驱动出口函数
 * @param : 无
 * @return : 无
 */
static int __init led_init(void)
{
    int retvalue = 0;
    u32 val = 0;

    /* 初始化LED */
    /* 1、寄存器地址映射 */
    IMX6U_CCM_CCGR1 = iormap(CCM_CCGR1_BASE, 4);
    SW_MUX_GPIO1_IO03 = ioremap(SW_MUX_GPIO1_IO03_BASE, 4);
    SW_PAD_GPIO1_IO03 = ioremap(SW_PAD_GPIO1_IO03_BASE, 4);
    GPIO1_DR = ioremap(GPIO1_DR_BASE, 4);
    GPIO1_GDIR = ioremap(GPIO1_GDIR_BASE, 4);

    /* 2、使能GPIO1时钟 */
    val = readl(IMX6U_CCM_CCGR1);
    val &= ~(3 << 26);  /* 清除以前的设置 */
    val |= (3 << 26);   /* 设置新值 */
    writel(val, IMX6U_CCM_CCGR1);

    /*
        3、设置GPIO1_IO03的复用功能，将其设置为GPIO1_IO03，最后设置IO属性
    */
   writel(5, SW_MUX_GPIO1_IO03);

   /*
        寄存器 SW_MUX_GPIO1_IO03 设置IO属性
    */
   writel(0x10b0, SW_PAD_GPIO1_IO03);

   /*
        4、设置GPIO1_为输出功能
   */
    val = readl(GPIO1_GDIR);
    val &= ~(1 << 3);   /* 清除以前的设置 */
    val |= (1 << 3);    /* 设置为输出 */
    writel(val, GPIO1_GDIR);

    /*
        5、默认关闭LED
    */
    val = readl(GPIO1_DR);
    val |= (1 << 3);
    writel(val, GPIO1_DR);

    /*
        6、注册字符设备驱动
        创建设备号
    */
    if (newchrled.major) {  /* 定义了设备号 */
        newchrled.devid = MDDEV(newchrled.major, 0);
        register_chrdev_region(newchrled.devid, NEWCHRLED_CNT, NEWCHRLED_NAME);
    } else {    /* 没有定义设备号 */
        alloc_chrdev_region(&newchrled.devid, 0, NEWCHRLED_CNT, NEWCHRLED_NAMW); /* 申请设备号 */
        newchrled.major = MAJOR(newchrled.devid);   /* 获取主设备号 */
        newchrled.minor = MINOR(newchrled.devid);   /* 获取次设备号 */
    }
    printk("newcheled major = %d, minor = %d\r\n", newchrled.major, newchrled.minor);

    /* 初始化cdev */
    newchrled.cdev.owner = THIS_MODULE;
    cdev_init(&newchrled.cdev, &newchrled_fops);

    /* 添加一个cdev */
    cdev_add(&newchrled.cdev, newchrled.devid, NEWCHRLED_CNT);

    /* 创建类 */
    newchrled.class = class_create(THIS_MODULE, NEWCHRLED_NAME);
    if (IS_ERR(newchrled.class)) {
        return PTR_ERR(newchrled.class);
    }

    /* 创建设备 */
    newchrled.device = device_create(newchrled.class, NULL, newchrled.devid, NULL, NEWCHRLED_NAME);
    if (IS_ERR(newchrled.device)) {
        return PTR_ERR(newchrled.device);
    }

    return 0;
}

static void __exit led_exit(void)
{
    /* 取消映射 */
    iounmap(IMX6U_CCM_CCGR1);
    iounmap(SW_MUX_GPIO1_IO03);
    iounmap(SW_PAD_GPIO1_IO03);
    iounmap(GPIO1_DR);
    iounmap(GPIO1_GDIR);

    /* 注销字符设备 */
    cdev_del(&newchrled.cdev);  /* 删除cdev */
    unregister_chrdev_region(newchrled.devid, NEWCHRLED_CNT);

    device_destory(newchrled.class, newchrled.devid);
    class_destory(newchrled.class);
}

module_init(led_init);
moudle_exit(led_exit);