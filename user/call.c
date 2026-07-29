#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// 这组刻意保持简单且不内联的调用链用于观察 RISC-V 栈帧和返回地址。
int g(int x) {
  return x+3;
}

int f(int x) {
  return g(x);
}

// main -> f -> g 形成连续栈帧，便于在实验中练习 backtrace。
void main(void) {
  printf("%d %d\n", f(8)+1, 13);
  exit(0);
}
