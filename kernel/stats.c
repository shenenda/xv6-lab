#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "riscv.h"
#include "defs.h"

#define BUFSZ 4096
static struct {
  struct spinlock lock;
  char buf[BUFSZ];
  int sz;
  int off;
} stats;

int statscopyin(char*, int);
int statslock(char*, int);
  
int
statswrite(int user_src, uint64 src, int n)
{
  return -1;
}

int
statsread(int user_dst, uint64 dst, int n)
{
  int m;

  acquire(&stats.lock);

  // 缓冲区为空时才生成一次统计快照，后续读取通过 off 分段取走。
  if(stats.sz == 0) {
#ifdef LAB_PGTBL
    stats.sz = statscopyin(stats.buf, BUFSZ);
#endif
#ifdef LAB_LOCK
    stats.sz = statslock(stats.buf, BUFSZ);
#endif
  }
  m = stats.sz - stats.off;

  if (m > 0) {
    if(m > n)
      m  = n;
    // 设备读既可能面向用户地址，也可能面向内核地址，由 either_copyout 统一处理。
    if(either_copyout(user_dst, dst, stats.buf+stats.off, m) != -1) {
      stats.off += m;
    }
  } else {
    // 当前快照读完后复位，下次读取会重新收集最新统计信息。
    m = -1;
    stats.sz = 0;
    stats.off = 0;
  }
  release(&stats.lock);
  return m;
}

void
statsinit(void)
{
  initlock(&stats.lock, "stats");

  // 将统计接口注册成一个设备文件，沿用普通文件的 read/write 分发路径。
  devsw[STATS].read = statsread;
  devsw[STATS].write = statswrite;
}

