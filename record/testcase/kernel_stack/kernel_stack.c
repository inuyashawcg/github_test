/*
  https://www.jianshu.com/p/8f98b6e69063

  command:
    gcc -no-pie -g kernel_stack.c -o test
    gdb ./test
      - r
      - print $rsp
      - print orig_stack_pointer
  
  output:
    9       void blow_stack() {
    (gdb) print $rsp
    $1 = (void *) 0x7fffff7ff000     我们一开始保存的栈底指针
    (gdb) print $orig_stack_pointer
    $2 = void
    (gdb) print orig_stack_pointer
    $3 = (void *) 0x7fffffffd830    栈溢出时的指针
    (gdb) print 0x7fffffffd830 - 0x7fffff7ff000
    $4 = 8382512  大约为 8M
*/

void *orig_stack_pointer;

void blow_stack() {
    blow_stack();
}

int main() {
    __asm__("movq %rsp, orig_stack_pointer");

    blow_stack();
    return 0;
}