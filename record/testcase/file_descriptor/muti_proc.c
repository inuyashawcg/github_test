// https://fuerain.ink/archives/linux-multi-process-w-file.html#:~:text=Linux%E5%A4%9A%E8%BF%9B%E7%A8%8B%E5%86%99%E5%90%8C%E4%B8%80%E6%96%87%E4%BB%B6%201%20write%20%28%29%20%E5%87%BD%E6%95%B0%E6%98%AF%E5%8E%9F%E5%AD%90%E6%93%8D%E4%BD%9C%EF%BC%8C%E5%AF%B9%E6%9F%90%E4%B8%80%E6%96%87%E4%BB%B6%E7%9A%84%E5%86%99%E5%85%A5%E5%8F%AA%E6%9C%89%E5%9C%A8%E4%B8%8A%E4%B8%80%E4%B8%AAwrite%20%28%29%E5%87%BD%E6%95%B0%E6%89%A7%E8%A1%8C%E7%BB%93%E6%9D%9F%E5%90%8E%E6%89%8D%E8%83%BD%E7%BB%A7%E7%BB%AD%E4%BD%BF%E7%94%A8write%20%28%29,%E5%86%99%E5%85%A5%E6%96%87%E4%BB%B6%EF%BC%8C%E4%BD%BF%E5%BE%97%E4%B8%8D%E5%90%8C%E8%BF%9B%E7%A8%8B%E5%86%99%E5%85%A5%E7%9A%84%E6%95%B0%E6%8D%AE%E4%BE%9D%E6%AC%A1%E5%87%BA%E7%8E%B0%202%20write%20%28%29%20%E5%87%BD%E6%95%B0%20%E4%B8%8D%E6%98%AF%20%E5%8E%9F%E5%AD%90%E6%93%8D%E4%BD%9C%EF%BC%8C%E5%86%99%E5%85%A5%E7%9A%84%E6%95%B0%E6%8D%AE%E5%8F%AF%E8%83%BD%E4%BC%9A%E4%BA%A4%E5%8F%89%E5%87%BA%E7%8E%B0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

int main(int argc, char* argv[]) {
  /*
    fork 之前打开，父进程和子进程会共享同一个文件描述符表，所以子进程执行完成之后，父进程才会
    接着之前的结果再次执行。此时文件指针已经发生了改变，从而实现 append 效果

    shell 终端输出结果:
      child process: fd = 3
      main process: fd = 3
    
    out 文件:
      aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
  */
  // int fd = open("out", O_RDWR | O_CREAT, 0755);
  int rc = fork();

  /*
    在 fork() 之后打开文件，父进程与子进程在 fork() 之后打开同一文件会生成不同的文件表，
    文件的偏移量一致 (应该都是从 0 开始)，所以它们写入的时候会产生内容覆盖

    shell 终端输出:
      child process: fd = 3
      main process: fd = 3
    
    out 文件:
      bbbbbbbbbbbbbbbbbbbbbbbbbbbbbba
  */
  int fd = open("out", O_RDWR | O_CREAT, 0755);

  if (rc < 0) {
      fprintf(stderr, "fork failed\n");
      exit(1);
  }
  else if (rc == 0) {
      char buf[31];
      memset(buf, 'a', sizeof(buf));

      write(fd, buf, sizeof(buf));
      // close(fd);
      printf("child process: fd = %d\n", fd);
  } else {
      int wc = wait(NULL);

      char buf[30];
      memset(buf, 'b', sizeof(buf));
      write(fd, buf, sizeof(buf));

      printf("main process: fd = %d\n", fd);
  }

  return 0;
}