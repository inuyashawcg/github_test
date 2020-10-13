/*
    int ioctl(int fd, unsigned long request, ...)
        fd: 必须是打开文件描述符
*/   
#include <sys/types.h>
#include <sys/ioctl.h>

#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define ECHO_CLEAR_BUFFER _IO('E', 1)
#define ECHO_SET_BUFFER_SIZE _IOW('E', 2, int)

static enum {
    UNSET,
    CLEAR,
    SETSIZE 
} action = UNSET;

/**
 * 用法: echo_ioctl -c | -s size
*/

static void
usage()
{
    /**
     * 此程序的两个参数是二选一的(either-or),也就是说
     * "echo_ioctl -c" / "echo_ioctl -s size"都是有效的用法，
     * 但是"echo_ioctl -c -s size"则是无效的用法
    */

    fprintf(stderr, "usage: echo_ioctl -c | -s size\n");
    exit(1);
}

/*
    该程序用于清空 /dev/echo 所带缓冲去或者重新调整其大小
*/

int main()
{
    int ch, fd, i, size;
    char *p;
    
    /*
        分析命令行参数列表以确定应采取的正确操作
        -c: 清空缓冲区
        -s size: 将缓冲区的大小调整为size
    */

    while ((ch = getopt(argc, argv, "cs:")) != -1) {
        switch (ch) {
            case 'c':
            {
                if (action != UNSET)
                    usage();
                action = CLEAR;
                break;
            }
            case 's':
            {
                if (action != UNSET)
                    usage();
                action = SETSIZE;
                size = (int)strtol(optarg, &p, 10);

                if (*p)
                    errx(1, "illegal size -- %s", optarg);
                break;
            }
            default:
                usage();
        }
    }

    /*
        执行所选操作
    */
    if (action == CLEAR) {
        fd = open("dev/echo", O_RDWR);
        if (fd < 0)
            err(1, "open(/dev/echo)");

        i = ioctl(fd, ECHO_CLEAR_BUFFER, NULL);
        if (i < 0)
            err(1, "ioctl(/dev/echo)");
        close(fd);

    } else if (action == SETSIZE) {
        fd = open("/dev/echo", O_RDWR);
        if (fd < 0)
            err(1, "open(/dev/echo)");
        
        i = ioctl(fd, ECHO_SET_BUFFER_SIZE, &size);
        if (i < 0)
            err(1, "ioctl(/dev/echo)");
        close (fd);
    } else {
        usage();
    }

    return 0;
}