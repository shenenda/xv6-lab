#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define SZ 4096
char buf[SZ];

int
main(void)
{
  int i, n;
  
  // statistics 每次最多返回 SZ 字节；读满时继续请求下一段。
  while (1) {
    n = statistics(buf, SZ);
    // xv6 的 write 接口允许一次写多个字节，这里逐字节输出以保持原实验实现。
    for (i = 0; i < n; i++) {
      write(1, buf+i, 1);
    }
    if (n != SZ)
      break;
  }

  exit(0);
}
