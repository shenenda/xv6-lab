#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// Fetch the uint64 at addr from the current process.
int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  if(addr >= p->sz || addr+sizeof(uint64) > p->sz)
    return -1;
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
int
fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();
  int err = copyinstr(p->pagetable, buf, addr, max);
  if(err < 0)
    return err;
  return strlen(buf);
}

static uint64
argraw(int n)
{
  struct proc *p = myproc();
  switch (n) {
  case 0:
    return p->trapframe->a0;
  case 1:
    return p->trapframe->a1;
  case 2:
    return p->trapframe->a2;
  case 3:
    return p->trapframe->a3;
  case 4:
    return p->trapframe->a4;
  case 5:
    return p->trapframe->a5;
  }
  panic("argraw");
  return -1;
}

// Fetch the nth 32-bit system call argument.
int
argint(int n, int *ip)
{
  *ip = argraw(n);
  return 0;
}

// Retrieve an argument as a pointer.
// Doesn't check for legality, since
// copyin/copyout will do that.
int
argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);
  return 0;
}

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  if(argaddr(n, &addr) < 0)
    return -1;
  return fetchstr(addr, buf, max);
}

extern uint64 sys_chdir(void);
extern uint64 sys_close(void);
extern uint64 sys_dup(void);
extern uint64 sys_exec(void);
extern uint64 sys_exit(void);
extern uint64 sys_fork(void);
extern uint64 sys_fstat(void);
extern uint64 sys_getpid(void);
extern uint64 sys_kill(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_mknod(void);
extern uint64 sys_open(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_sleep(void);
extern uint64 sys_unlink(void);
extern uint64 sys_wait(void);
extern uint64 sys_write(void);
extern uint64 sys_uptime(void);
extern uint64 sys_trace(void); //全局声明trace系统调用处理函数
extern uint64 sys_sysinfo(void); //全局声明sysinfo系统调用处理函数

static uint64 (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
[SYS_trace]   sys_trace,
[SYS_sysinfo] sys_sysinfo,
};

// kernel/syscall.c
// 定义系统调用名称的字符串数组
const char* kama_syscall_names[] = {
    [SYS_fork]    "fork",
    [SYS_exit]    "exit",
    [SYS_wait]    "wait",
    [SYS_pipe]    "pipe",
    [SYS_read]    "read",
    [SYS_kill]    "kill",
    [SYS_exec]    "exec",
    [SYS_fstat]   "fstat",
    [SYS_chdir]   "chdir",
    [SYS_dup]     "dup",
    [SYS_getpid]  "getpid",
    [SYS_sbrk]    "sbrk",
    [SYS_sleep]   "sleep",
    [SYS_uptime]  "uptime",
    [SYS_open]    "open",
    [SYS_write]   "write",
    [SYS_mknod]   "mknod",
    [SYS_unlink]  "unlink",
    [SYS_link]    "link",
    [SYS_mkdir]   "mkdir",
    [SYS_close]   "close",
    [SYS_trace]   "trace",
    [SYS_sysinfo] "sysinfo",
};

// kernel/syscall.c
//根据a7中的系统调用编号找到并执行对应的内核函数，将返回值写回a0；
//如果当前进程的跟踪掩码包含该系统调用，则打印PID、系统调用名称和返回值。
void
syscall(void)
{
  int num;

  // 获取当前正在运行的进程控制块。
  // p（结构体指针）指向当前进程对应的 struct proc。
  struct proc *p = myproc();

  // 用户程序执行系统调用前，会把系统调用编号放入 a7 寄存器。
  // 进入内核时，用户态寄存器被保存到 trapframe，
  // 因此这里从 trapframe->a7 中取出系统调用编号。
  num = p->trapframe->a7;

  // 判断系统调用编号是否合法：
  // 1. num > 0：xv6 的有效系统调用编号从 1 开始；
  // 2. num < NELEM(syscalls)：防止数组下标越界；
  // 3. syscalls[num] != 0：该编号确实注册了处理函数。
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {

    // syscalls 是系统调用处理函数指针数组。
    //
    // 例如 num == SYS_read 时：
    // syscalls[num]() 实际调用 sys_read()。
    //
    // 系统调用执行后的返回值保存到 trapframe->a0。
    // 内核返回用户态后，用户程序会从 a0 中得到返回值。
    p->trapframe->a0 = syscalls[num]();

    // 判断当前进程是否要求跟踪编号为 num 的系统调用。
    //
    // kama_syscall_trace 是位掩码：
    // 将它右移 num 位后，再与 1 做按位与，
    // 就能检查第 num 位是不是 1。
    if((p->kama_syscall_trace >> num) & 1) {

      // 只有对应位为 1 时，才打印系统调用跟踪信息。
      //
      // p->pid：当前进程 PID；
      // kama_syscall_names[num]：当前系统调用名称；
      // p->trapframe->a0：该系统调用的返回值。
      printf("%d: syscall %s -> %d\n",
              p->pid,
              kama_syscall_names[num],
              p->trapframe->a0);
    }

  } else {
    // 系统调用编号无效，或者没有注册对应的处理函数。
    printf("%d %s: unknown sys call %d\n",
           p->pid,
           p->name,
           num);

    // 向用户态返回 -1，表示系统调用失败。
    p->trapframe->a0 = -1;
  }
}
