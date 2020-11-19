#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/module.h>

#define MINORBITS   20
#define MINORMASK   ((1U << MINORBITS) - 1)
#define MAJOR(dev)  ((unsigned int) (dev) >> MINORBITS)
#define MINOR(dev)  ((unsigned int) (dev & MINORMASK))
#define MKDEV(ma, mi)   (((ma) << MINORBITS) | (mi))

#define CHRDEVBASE_MAJOR    200
#define CHRDEVBASE_NAME     "chrdevbase"

static char readbuf[100];
static char writebuf[100];
static char kerneldata[] = {"kernel data!"};

static struct file_operations test_fops;

static int chrtest_open(struct inode *inode, struct file *filp)
{
    return 0;
}

static ssize_t chrtest_read(struct file *filp, char __user *buf,
                            size_t cnt, loff_t *offt)
{
    int retvalue = 0;

    memcpy(readbuf, kerneldata, sizeof(kerneldata));
    retvalue = copy_to_user(buf, readbuf, cnt);
    if (retvalue == 0) {
        printk("kernel senddata ok!\r\n");
    } else {
        printk("kernel sendtata failed!\r\n");
    }
    return 0;
}

static ssize_t chrtest_write(struct file *filp, const char __user *buf,
                            size_t cnt, loff_t *offt)
{
    int retvalue = 0;
    retvalue = copy_from_user(writebuf, buf, cnt);
    if (retvalue == 0) {
        printk("kernel recevdata: %s\r\n", writebuf);
    } else {
        printk("kernel recevdata failed!\r\n");
    }
    return 0;
}

static int chrtest_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static struct file_operations test_fops = {
    .owner = THIS_MODULE,
    .open = chrtest_open,
    .read = chrtest_read,
    .write = chrtest_write,
    .release = chrtest_release,
};

static int __init chrdevbase_init(void)
{
    int retvalue = 0;
    retvalue = register_chrdev(CHRDEVBASE_MAJOR, CHRDEVBASE_NAME, &test_fops);
    if (retvalue < 0) {
        printk("chrdevbase driver register failed!\r\n");
    }

    printk("chrdevbase_init()\r\n");
    return 0;
}

static void __exit chrdevbase_exit(void)
{
    unregister_chrdev(CHRDEVBASE_MAJOR, CHRDEVBASE_NAME);
    printk("chrdevbase_exit()\r\n");
}

module_init(chrdevbase_init);
module_exit(chrdevbase_exit);
