#include "param.h"
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"

//
// 本文件提供 copyin_new() 和 copyinstr_new()，
// 用来替代 vm.c 中的 copyin() 与 copyinstr()。
//

static struct stats {
  int ncopyin;
  int ncopyinstr;
} stats;

int
statscopyin(char *buf, int sz) {
  int n;
  n = snprintf(buf, sz, "copyin: %d\n", stats.ncopyin);
  n += snprintf(buf+n, sz, "copyinstr: %d\n", stats.ncopyinstr);
  return n;
}

// 从用户空间复制到内核空间：从给定页表的虚拟地址 srcva 开始，
// 向 dst 复制 len 个字节。成功返回 0，出错返回 -1。
int
copyin_new(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  struct proc *p = myproc();

  // 同时检查起点、终点以及无符号加法回绕，防止越过进程地址空间。
  if (srcva >= p->sz || srcva+len >= p->sz || srcva+len < srcva)
    return -1;
  memmove((void *) dst, (void *)srcva, len);
  stats.ncopyin++;   // TODO：这里的统计计数尚未加锁保护。
  return 0;
}

// 从用户空间向内核空间复制以空字符结尾的字符串。
// 从给定页表中的虚拟地址 srcva 开始复制到 dst，遇到 '\0' 或达到 max 时停止。
// 成功找到结尾并复制完成时返回 0，否则返回 -1。
int
copyinstr_new(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  struct proc *p = myproc();
  char *s = (char *) srcva;
  
  stats.ncopyinstr++;   // TODO：这里的统计计数尚未加锁保护。
  // 逐字节检查结尾，同时保证读取地址不超过当前进程大小。
  for(int i = 0; i < max && srcva + i < p->sz; i++){
    dst[i] = s[i];
    if(s[i] == '\0')
      return 0;
  }
  return -1;
}
