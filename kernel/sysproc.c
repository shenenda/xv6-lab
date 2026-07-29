#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

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
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
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

// kernel/sysproc.c
// trace 系统调用在内核中的具体处理函数：从用户态取出传入的 mask 参数，把它保存到当前进程的进程控制块中。
uint64
sys_trace(void) //sys_trace() 不从形参列表拿参数，而是通过 argint() 从当前进程保存的寄存器现场中提取参数
{
  int mask;

  //获取用户程序传入的数据
  //argint(0, &mask)  获取当前系统调用的第 0 个整数参数，并写入 mask。
  if(argint(0, &mask) < 0) {
    return -1;
  }

  //设置调用进程的kama_syscall_trace掩码mask
  //myproc()返回当前正在运行进程的：struct proc *，也就是当前进程控制块的指针。
  myproc()->kama_syscall_trace = mask;
  return 0;
}

/*
xv6 的系统调用处理函数统一使用类似形式：
uint64 sys_xxx(void);
因为系统调用的返回值最终要写入用户进程的 a0 寄存器，而 RISC-V 64 位架构的寄存器宽度是 64 位。
虽然它返回的是 uint64，这里仍然可以返回：
return -1;
-1 会按补码形式写入 64 位返回寄存器。用户态把它按照有符号整数解释时，仍然看到 -1。
*/

// 统计空闲内存和正在使用的进程数量，然后把结果从内核空间复制到用户程序传入的 struct sysinfo 中
uint64
sys_sysinfo(void) {
  // info 是内核栈上的局部结构体，
  // 暂时保存统计得到的系统信息。
  struct sysinfo info;

  // 统计当前空闲内存字节数，
  // 并把结果写入 info.freemem。
  kama_freebytes(&info.freemem);

  // 统计当前正在使用的进程数量，
  // 并把结果写入 info.nproc。
  kama_procnum(&info.nproc);

  // 保存用户程序传入的(用户空间中的) struct sysinfo 对象的虚拟地址。
  uint64 dstaddr;

  // 取得 sysinfo() 的第 0 个参数。
  // 用户态调用 sysinfo(&info) 时，
  // &info 会通过 a0 寄存器传入内核。
  if(argaddr(0, &dstaddr) < 0)
    return -1;

  // 根据当前进程的页表，将内核中的 info 结构体复制到用户地址空间。
  //
  // myproc()->pagetable：当前进程的用户页表
  // dstaddr：用户传入的目标虚拟地址，要通过用户进程的页表找到对应的物理内存
  // (char *)&info：内核中数据的起始地址 [&info的类型是结构体struct sysinfo *]
  // sizeof(info)：要复制的字节数
  if(copyout(myproc()->pagetable,
             dstaddr,
             (char *)&info,
             sizeof(info)) < 0)
    return -1;

  // 成功返回 0
  return 0;
}