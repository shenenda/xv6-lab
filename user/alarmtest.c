//
// alarm 实验测试程序。可以临时修改本文件辅助调试，但最终内核实现必须通过原始测试。
//

#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "user/user.h"

void test0();
void test1();
void test2();
void periodic();
void slow_handler();

int
main(int argc, char *argv[])
{
  test0();
  test1();
  test2();
  exit(0);
}

volatile static int count;

// 周期性处理函数每次只更新计数，随后用 sigreturn 恢复被中断的用户上下文。
void
periodic()
{
  count = count + 1;
  printf("alarm!\n");
  sigreturn();
}

// 测试内核能否至少调用一次 alarm 处理函数。
void
test0()
{
  int i;
  printf("test0 start\n");
  count = 0;
  // 每累计两个时钟滴答调用一次 periodic。
  sigalarm(2, periodic);
  for(i = 0; i < 1000*500000; i++){
    if((i % 1000000) == 0)
      write(2, ".", 1);
    if(count > 0)
      break;
  }
  sigalarm(0, 0);
  if(count > 0){
    printf("test0 passed\n");
  } else {
    printf("\ntest0 failed: the kernel never called the alarm handler\n");
  }
}

void __attribute__ ((noinline)) foo(int i, int *j) {
  if((i % 2500000) == 0) {
    write(2, ".", 1);
  }
  *j += 1;
}

//
// 测试内核能否多次调用处理函数，并验证处理函数返回后会回到定时器中断发生点，
// 且所有寄存器都恢复为中断前的值。
//
void
test1()
{
  int i;
  int j;

  printf("test1 start\n");
  count = 0;
  j = 0;
  // 每累计两个时钟滴答调用一次 periodic。
  sigalarm(2, periodic);
  for(i = 0; i < 500000000; i++){
    if(count >= 10)
      break;
    foo(i, &j);
  }
  if(count < 10){
    printf("\ntest1 failed: too few calls to the handler\n");
  } else if(i != j){
    // 循环执行了 i 次，foo() 每次把 j 加一，因此两者应相等。若不相等，可能是
    // 处理函数返回到了错误位置，或寄存器恢复不完整，破坏了 i、j 或 j 的地址。
    printf("\ntest1 failed: foo() executed fewer times than it was called\n");
  } else {
    printf("test1 passed\n");
  }
}

//
// 测试内核不会在一个 alarm 处理函数尚未返回时重入调用它。
void
test2()
{
  int i;
  int pid;
  int status;

  printf("test2 start\n");
  if ((pid = fork()) < 0) {
    printf("test2: fork failed\n");
  }
  if (pid == 0) {
    count = 0;
    sigalarm(2, slow_handler);
    for(i = 0; i < 1000*500000; i++){
      if((i % 1000000) == 0)
        write(2, ".", 1);
      if(count > 0)
        break;
    }
    if (count == 0) {
      printf("\ntest2 failed: alarm not called\n");
      exit(1);
    }
    exit(0);
  }
  wait(&status);
  if (status == 0) {
    printf("test2 passed\n");
  }
}

// 慢处理函数在内部持续运行，用来检查 alarm 是否会发生重入。
void
slow_handler()
{
  count++;
  printf("alarm!\n");
  if (count > 1) {
    printf("test2 failed: alarm handler called more than once\n");
    exit(1);
  }
  for (int i = 0; i < 1000*500000; i++) {
    asm volatile("nop"); // 防止编译器删除这个故意耗时的空循环。
  }
  sigalarm(0, 0);
  sigreturn();
}
