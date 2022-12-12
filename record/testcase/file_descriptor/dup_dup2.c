// https://perrynzhou.github.io/2020/04/16/Linux%E7%B3%BB%E7%BB%9F%E8%B0%83%E7%94%A8dup%E5%92%8Cdup2%E5%8C%BA%E5%88%AB/

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define FILE_NAME "out"

static void redirect_stdout_without_dup() {
  fprintf(stdout, "pid = %d\n", getpid());
  const char *str = "my dup\n";

  //关闭 stdout
  close(1);

  //当前 process 中描述符表中最小可用的下标是 1，因为刚刚关闭
  int fd = open(FILE_NAME, O_RDWR | O_CREAT | O_TRUNC, 0666);
  if (fd > 0) {
    // stdout 在每个进程描述表中的下标为 1
    // 此时，数据是写到了刚刚打开的 fd 中，新打开的 fd 返回的是 1
    fprintf(stdout, "open fd = %d\n", fd);
    // write 操作也是写到 fd = 1 中，当前进程中文件描述符为 1 的并不是标准输出
    write(fd, str, strlen(str));
    close(1);
  }
  /*
    shell 终端:
      pid = 282777

    out 文件:
      open fd = 1
      my dup

    假如我们直接 open()，不使用 dup()，我们获取到的是进程描述符表中最小可用的索引值，也就是 1。
    然后我们执行 fprintf(stdout, ...) 函数，正常来讲，输出的结果应该是到 shell 终端，但是这次
    却把内容写入到了 out 文件当中，说明什么？ 说明标准输出只会去取进程文件描述符表中索引值为 1 的
    元素，而不管该元素最终指向哪里。可以是 shell 终端设备，也可以是某个文件；

    进一步猜测，输出重定位应该就是把目标文件对应的 file 写到文件描述符表索引 1 的位置。然后只要是
    用到标准输出的，都会去索引 1 处拿 file，然后向其中写入数据即可，而不必去管这个 file 关联到哪里
  */
}

static void redirect_stdout_with_dup() {
  fprintf(stdout, "pid = %d\n", getpid());
  const char *str = "my dup";

  // 默认打开 fd，在当前进程描述表中 fd 并不是 {0，1，2}
  int fd = open(FILE_NAME, O_RDWR | O_CREAT | O_TRUNC, 0666);
  if (fd > 0) {
    // 关闭标准的输出的文件描述符
    close(1);
    // 拷贝 fd 到当前进程描述符中最小的下标位置，当前最小的下标应该是刚刚关闭的 1
    dup(fd);
    // fprintf 的内容写入到了 fd 中，并没有写入到标准输出中
    fprintf(stdout, "open fd = %d\n", fd);
    write(fd, str, strlen(str));
    //关闭当前文件描述符
    close(fd);
  }

  /*
    dup
      - 函数原型是 int dup(old_fd),把 old_fd 下标中的内容拷贝到当前进程文件描述符表中最小的可用位置
        下标空间中，open 系统调用会默认返回当前进程描述符表中最小的下标作为文件描述符
      - dup系统调用不是原子的

    shell 终端输出:
      pid = 285288

    out 文件输出:
      open fd = 3
      my dup

    可以看到，dup() 是将表中索引 3 位置的 file 拷贝到了索引 1 处，这个 file 指向的是 out 文件。所以
    标准输出最终会写入到文件 out 当中
  */
}

static void redirect_stdout_with_dup2() {
  fprintf(stdout, "pid = %d\n", getpid());
  const char *str = "i'm dup2\n";

  // 打开一个新的文件描述符
  int fd = open(FILE_NAME, O_RDWR | O_CREAT | O_TRUNC, 0666);
  if (fd > 0) {
    // 如果 1 号文件描述符是打开状态，就关闭 1 号文件描述符
    // 把当前进程中文件描述符表中下标为 fd 的指针拷贝下标为 1 的空间
    // 如果 fd == 1 就直接返回 fd
    dup2(fd, 1); // equals: close(1) and dup(fd)
    // fd 和 1 号文件描述符指向相同的文件结构体指针
    fprintf(stdout, "%d already redirect to stdout\n", fd);
    write(fd, str, strlen(str));
    // 刷盘操作
    if (fd != 1) {
      close(fd);
    }
  }
  /*
    dup2
      - 函数原型是 int dup2(new_fd, old_fd),这个操作是原子的
      - 如果 old_fd 已经存在就 close(old_fd),然后调用 dup(new_fd), 把 new_fd 中内容拷贝到当前进程
        文件描述符表中最小的下标空间中

    shell 终端输出:
      pid = 287638

    out 文件:
      3 already redirect to stdout
      i'm dup2

    open() 获取到的 fd = 3，也就是 out 文件对应描述符表索引 3 的位置。然后我们是要把索引 3 的元素拷贝到
    索引 1，也就是标准输出的位置。dup2() 会检测标准输出是否已经存在，这次我们没有 close(1)，所以它是存在的。
    后续应该会是执行 close(1)，然后 dup(3)，最终还是会把索引 3 处的元素拷贝到索引 1 处。该过程的基本实现
    与文件重定位的底层原理应该是一样的

    拓展一下，当我们重定位标准输出之后，怎么给它再重定位回去? 
  */
}


// 标准输出反向重定位测试
static void reverse_redirect(void) {
  fprintf(stdout, "pid = %d\n", getpid());
  const char *str = "reverse redirect test\n";

  int fd = open(FILE_NAME, O_RDWR | O_CREAT | O_TRUNC, 0666);
  if (fd > 0) {
    fprintf(stdout, "open fd = %d\n", fd);
    // 在 out 文件中，应该会存在 Reverse redirect1 一条记录 
    write(fd, str, strlen(str));
  }

  // 重定位恢复首先是要备份原有标准输出表项，然后在重定位之后，再将备份回写，进而恢复到原有状态
  int s_fd = dup(1), n_fd = dup2(fd, 1);
  fprintf(stdout, "before redirect");

  // 恢复重定向
  dup2(s_fd, n_fd);
  fprintf(stdout, "after redirect\n");

  /*
    shell 终端输出:
      pid = 298656
      open fd = 3
      after redirect
    
    out 文件:
      reverse redirect test
      before redirect

    结果符合预期。这种反向重定位的操作，只能用 dup() / dup2() 来实现？
    别的方式应该也是可以的，只要符合操作逻辑即可
  */
}

int main(void) {
  // redirect_stdout_without_dup();
  // redirect_stdout_with_dup();
  // redirect_stdout_with_dup2();
  reverse_redirect();
  // for (;;) {
  //   sleep(1);
  // }
  return 0;
}