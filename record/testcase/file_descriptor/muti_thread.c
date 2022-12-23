#include <fcntl.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#define FILE_NAME "out"

//定义线程要执行的函数，arg 为接收线程传递过来的数据
void* write_thread(void* arg) {
  const char* buffer2 = "hi, wcg";
  int fd2 = open(FILE_NAME, O_RDWR | O_CREAT, 0755);

  if (fd2 > 0)
    printf(">>>>> Sub thread >>>>>> fd is %d\n", fd2);

  off_t file_pos = lseek(fd2, 0, SEEK_END);
  printf(">>>>> Sub thread  >>>>>> file position is %ld\n", file_pos);

  int ret = write(fd2, buffer2, 7);
  printf(">>>>> Sub thread  >>>>>> write value is %d\n", ret);
  close(fd2);
  return "Sub thread over!";
}

int main() {
  pthread_t thread;
  void* thread_result;
  const char* buffer1 = "hello, world";

  int fd1 = open(FILE_NAME, O_RDWR | O_CREAT, 0755);
  if (fd1 > 0) {
    printf(">>>>> Main thread >>>>>> fd is %d\n", fd1);
  }

  off_t file_pos = lseek(fd1, 0, SEEK_END);
  printf(">>>>> Main thread >>>>>> file position is %ld\n", file_pos);
  int ret = write(fd1, buffer1, 12);
  printf(">>>>> Main thread  >>>>>> write value is %d\n", ret);
  close(fd1);

  //创建 thread 线程，执行 write_thread() 函数
  int res = pthread_create(&thread, NULL, write_thread, NULL);
  if (res != 0) {
    printf("pthread_create() failed!\n");
    return 0;
  }

  //阻塞主线程，直至 thread 线程执行结束，用 thread_result 指向接收到的返回值，阻塞状态才消除。
  res = pthread_join(thread, &thread_result);
  printf("%s\n", (char*)thread_result);
  printf("Main thread over!\n");
  return 0;
}

/*
  如果只是 open()，没有执行 close()，打印结果如下:
    >>>>> Main thread >>>>>> fd is 3
    >>>>> Main thread >>>>>> file position is 0
    >>>>> Main thread  >>>>>> write value is 12
    >>>>> Sub thread >>>>>> fd is 4
    >>>>> Sub thread  >>>>>> file position is 12
    >>>>> Sub thread  >>>>>> write value is 7
    Sub thread over!
    Main thread over!
  
  添加 close() 之后，打印结果:
    >>>>> Main thread >>>>>> fd is 3
    >>>>> Main thread >>>>>> file position is 0
    >>>>> Main thread  >>>>>> write value is 12
    >>>>> Sub thread >>>>>> fd is 3
    >>>>> Sub thread  >>>>>> file position is 12
    >>>>> Sub thread  >>>>>> write value is 7
    Sub thread over!
    Main thread over!

  也就是说，如果我们在一个线程中执行完文件读写后，将其关闭，另外一个线程再次打开这个文件，分配的仍然是
  最小可用的文件描述符索引，也就是 3。也从侧面说明了线程是共享进程的文件描述符表
*/