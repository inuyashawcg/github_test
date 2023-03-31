#include <iostream>

// riscv64-unknown-elf-g++ -O1 rv64.cpp -S

int main() {
  // uint32_t imme1 = 0x7faffbc1;
  /*
    操作一个 32 位立即数，对照汇编代码，一共是分成两条指令:
      li	a0,2142240768
      addi	a0,a0,-1087
      ret
    ==>>
      li： 伪指令，load immediate
      a0 = a0 - 1087 = 2142240768 - 1087 = 0x7faffbc1
      返回
  */

  uint64_t imme1 = 0x7faffbc100002222;
  return imme1;
}