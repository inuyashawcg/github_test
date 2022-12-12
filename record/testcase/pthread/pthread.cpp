#include <pthread.h>
#include <iostream>

int status;

void *thread1(void *junk) {
  status = 2333;
  std::cout << "status addr is " << &status << std::endl;
  pthread_exit(static_cast<void*>(&status));
}

int main() {
  void *status_m;
  std::cout << "status_m addr is " << &status_m << std::endl;

  pthread_t t_a;
  pthread_create(&t_a, nullptr, thread1, nullptr);
  pthread_join(t_a, &status_m);
  std::cout << "status_m value is " << status_m << std::endl;

  int *ret = static_cast<int*>(status_m);
  std::cout << "the value is " << *ret << std::endl;

  return 0;
}