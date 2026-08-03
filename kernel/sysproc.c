#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // exit 不会返回到这里。
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;
  struct proc *p = myproc();

  if(argint(0, &n) < 0)
    return -1;

  addr = p->sz;
  if(n > 0){
    // 惰性分配只扩大进程的合法虚拟地址范围，不立即取得物理页。
    // 同时阻止地址空间越过 Sv39 中 xv6 允许使用的最高虚拟地址。
    if(p->sz >= MAXVA || (uint64)n > MAXVA - p->sz)
      return -1;
    p->sz += n;
  } else if(n < 0){
    // 收缩地址空间必须立即撤销现有映射；未被触碰的空洞由
    // 支持惰性页的 uvmunmap() 安全跳过。
    uint64 decrease = (uint64)(-(long)n);
    if(decrease > p->sz)
      return -1;
    p->sz = uvmdealloc(p->pagetable, p->sz, p->sz - decrease);
  }

  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  // ticks 会在时钟中断中并发更新，读取和等待都必须由 tickslock 保护。
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    // sleep 会原子地释放 tickslock，唤醒后重新持有它，避免错过时钟唤醒。
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// 返回系统启动以来已经发生的时钟中断次数。
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
