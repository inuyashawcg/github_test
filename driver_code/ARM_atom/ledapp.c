#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"

#define LEDOFF  0
#define LEDON   1

/*
 * @description : main 主程序
 * @param - argc : argv 数组元素个数
 * @param - argv : 具体参数
 * @return : 0 成功;其他 失败
*/
int main(int argc, char *argv[])
{
    int fd, retvalue;
    char *filename;
    unsigned char databuf[1];

    if (argc != 3) {
        printf("Error Usage!\r\n");
        return -1;
    }

    filename = argv[1];

    /* 打开LED驱动 */
    fd = open(filename, O_RDWR);
    if(fd < 0) {
        printf("file %s open failed!\r\n", argv[1]);
        return -1;
    }

    retvalue = close(fd);
    if (retvalue < 0) {
        printf("file %s close failed!\r\n", argv[1]);
        return -1;
    }
    return 0;
}

/*
    cmdline:
        ./ledApp /dev/led 1
        ./ledApp /dev/led 0
*/