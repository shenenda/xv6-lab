#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void wakeup1(struct proc *chan);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// 系统启动时初始化进程表。
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");

      // 为每个进程预先分配一页内核栈，并映射到高虚拟地址。
      // 每个内核栈旁边留有一个无效保护页，用于尽早发现栈越界。
      char *pa = kalloc();
      if(pa == 0)
        panic("kalloc");
      uint64 va = KSTACK((int) (p - proc));
      kvmmap(va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
      p->kstack = va;
  }
  kvminithart();
}

// 调用时必须关闭中断，防止进程迁移到其他 CPU 后仍使用旧的 hart 编号。
int
cpuid()
{
  int id = r_tp();
  return id;
}

// 返回当前 CPU 的 cpu 结构；调用者必须已经关闭中断。
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// 返回当前进程的 proc 结构；CPU 空闲时返回 0。
struct proc*
myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid() {
  int pid;
  
  // nextpid 是全局递增计数，必须加锁保证不同 CPU 不会分配出相同 pid。
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// 在进程表中查找 UNUSED 项。找到后初始化内核运行所需状态，
// 并在仍持有 p->lock 的情况下返回；没有空闲项或内存分配失败时返回 0。
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();

  // 为保存用户寄存器的 trapframe 分配一页内存。
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    release(&p->lock);
    return 0;
  }

  // 创建只含 trampoline 与 trapframe 映射的空用户页表。
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // 构造新进程的初始内核上下文：第一次被调度时从 forkret 开始，
  // 再经 usertrapret 返回用户态。
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// 释放 proc 结构及其关联资源，包括用户页面；调用者必须持有 p->lock。
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// 为指定进程创建用户页表；暂不映射普通用户内存，只建立陷阱跳板所需映射。
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // 先分配一个没有任何映射的根页表。
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // 把系统调用返回所需的 trampoline 代码映射到用户虚拟地址最高处。
  // 该页只在用户态与内核态切换过程中由监管者模式执行，因此不设置 PTE_U。
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // 在 TRAMPOLINE 正下方映射 trapframe，供 trampoline.S 保存和恢复寄存器。
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// 释放进程页表以及其中普通用户映射指向的物理内存。
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// 下面是调用 exec("/init") 的极小用户程序机器码，可用 od -t xC 查看。
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// 创建系统中的第一个用户进程。
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // 分配一个用户页，把 initcode 的指令和数据复制进去。
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // 为系统第一次从内核“返回”用户态准备入口地址和栈。
  p->trapframe->epc = 0;      // 用户程序计数器。
  p->trapframe->sp = PGSIZE;  // 用户栈指针。

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// 将用户内存扩大或缩小 n 字节；成功返回 0，失败返回 -1。
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// 复制父进程创建新进程，并设置子进程上下文，使其表现得像刚从 fork() 返回。
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // 分配并初始化一个新的进程表项。
  if((np = allocproc()) == 0){
    return -1;
  }

  // 复制父进程的用户地址空间到子进程。
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  np->parent = p;

  // 结构体整体赋值会复制父进程保存的全部用户寄存器。
  *(np->trapframe) = *(p->trapframe);

  // 单独把子进程的 a0 清零，使 fork 在子进程中返回 0。
  np->trapframe->a0 = 0;

  // 复制已打开文件描述符，并增加底层 file 对象的引用计数。
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  np->state = RUNNABLE;

  release(&np->lock);

  return pid;
}

// 把进程 p 遗留的子进程托管给 init；调用者必须持有 p->lock。
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    // 这里有意在未持有 pp->lock 时查看 pp->parent；如果先给 pp 加锁，
    // 而 pp 或其子进程也正在 exit() 中等待 p->lock，就可能形成死锁。
    if(pp->parent == p){
      // 从检查到取得锁之间 pp->parent 不会被别人修改，因为只有父进程能改它，
      // 而当前执行者正是它的父进程。
      acquire(&pp->lock);
      pp->parent = initproc;
      // 理论上这里应唤醒 init，但那需要再取得 initproc->lock；当前已持有
      // init 某个子进程 pp 的锁，会违反锁顺序并可能死锁。因此 exit() 会在
      // 获取其他进程锁之前无条件唤醒 init。
      release(&pp->lock);
    }
  }
}

// 退出当前进程且不再返回。进程会保持 ZOMBIE 状态，直到父进程调用 wait() 回收。
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // 关闭所有打开的文件，并清空文件描述符表。
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  // 当前进程退出时可能要把子进程托管给 init。取得其他 proc 锁后不能再安全
  // 获取 initproc->lock，因此在此处先无条件唤醒 init。即使这次唤醒多余或被错过，
  // 也不会影响正确性。
  acquire(&initproc->lock);
  wakeup1(initproc);
  release(&initproc->lock);

  // 先保存 p->parent，确保随后解锁的仍是自己加锁的同一个父进程。等待父进程锁时，
  // 当前进程可能被重新托管给 init；这最多造成一次发给已退出或非当前父进程的无害
  // 伪唤醒，因为 proc 表项不会被重新解释成其他类型的对象。
  acquire(&p->lock);
  struct proc *original_parent = p->parent;
  release(&p->lock);
  
  // 唤醒正在 wait() 的父进程需要持有父进程锁；按“先父后子”的锁顺序，必须先锁父进程。
  acquire(&original_parent->lock);

  acquire(&p->lock);

  // 把当前进程的所有子进程托管给 init。
  reparent(p);

  // 父进程可能正睡眠在 wait() 中，状态变为僵尸前将其唤醒。
  wakeup1(original_parent);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&original_parent->lock);

  // 切换回调度器，此后不会再返回该进程。
  sched();
  panic("zombie exit");
}

// 等待一个子进程退出并返回其 pid；没有子进程时返回 -1。
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  // 整个扫描与睡眠过程都以 p->lock 串联，避免丢失子进程 exit() 发出的唤醒。
  acquire(&p->lock);

  for(;;){
    // 扫描进程表，寻找已经退出的子进程。
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      // 这里在未持有 np->lock 时检查 np->parent；若先锁 np，而 np 恰好是祖先进程，
      // 当前已持有的 p->lock 可能与其锁顺序形成死锁。
      if(np->parent == p){
        // 检查与加锁之间 np->parent 不会变化，因为只有父进程能修改它，当前正是父进程。
        acquire(&np->lock);
        havekids = 1;
        if(np->state == ZOMBIE){
          // 找到一个可回收的僵尸子进程。
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&p->lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&p->lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // 没有任何子进程或当前进程已被终止时，不再继续等待。
    if(!havekids || p->killed){
      release(&p->lock);
      return -1;
    }
    
    // 以当前进程地址为睡眠通道，等待子进程退出时唤醒。
    sleep(p, &p->lock);  //DOC: wait-sleep
  }
}

// 每个 CPU 都运行自己的进程调度器。初始化完成后进入 scheduler()，且永不返回：
//  - 选择一个可运行进程；
//  - 通过 swtch 切换到该进程；
//  - 进程最终再次通过 swtch 把控制权交回调度器。
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // 保持设备中断开启，使等待 I/O 的进程能够被中断处理程序唤醒，避免系统僵死。
    intr_on();
    
    int nproc = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state != UNUSED) {
        nproc++;
      }
      if(p->state == RUNNABLE) {
        // 切换到选中的进程。该进程负责释放自己的锁，并在切回调度器之前重新取得它。
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // 进程本轮运行结束；切回前它应当已经更新了 p->state。
        c->proc = 0;
      }
      release(&p->lock);
    }
    if(nproc <= 2) {   // 只剩 init 和 sh 时进入低功耗等待。
      intr_on();
      // 没有可运行进程时用 wfi 等待下一次中断，减少空转。
      asm volatile("wfi");
    }
  }
}

// 切换到调度器。调用前必须只持有 p->lock，并已修改进程状态。
// intena 属于当前内核执行流而不是 CPU 本身，因此切换前后要保存和恢复。
// 理论上它和 noff 更适合作为进程字段，但少数无当前进程却持锁的路径无法这样处理。
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// 主动放弃 CPU，等待下一轮调度。
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// fork 子进程第一次被调度时，swtch 会从 forkret 开始执行。
void
forkret(void)
{
  static int first = 1;

  // 此时仍持有调度器交接过来的 p->lock。
  release(&myproc()->lock);

  if (first) {
    // 文件系统初始化可能调用 sleep，因此必须在普通进程上下文中执行，不能直接在 main() 中运行。
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// 原子地释放锁并睡眠在 chan 上；被唤醒后重新取得原锁。
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // 修改 p->state 并调用 sched 前必须持有 p->lock。取得它以后，wakeup 也必须
  // 等待同一把锁，因此不会在释放 lk 与进入睡眠之间丢失唤醒。
  if(lk != &p->lock){  //DOC: sleeplock0
    acquire(&p->lock);  //DOC: sleeplock1
    release(lk);
  }

  // 记录睡眠通道并把进程状态改为 SLEEPING。
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // 被唤醒后清除睡眠通道。
  p->chan = 0;

  // 按接口约定重新取得调用者传入的原锁。
  if(lk != &p->lock){
    release(&p->lock);
    acquire(lk);
  }
}

// 唤醒所有睡眠在 chan 上的进程；调用时不能持有任何 p->lock。
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
    }
    release(&p->lock);
  }
}

// 如果 p 正睡眠在 wait() 中就将其唤醒，供 exit() 使用；调用者必须持有 p->lock。
static void
wakeup1(struct proc *p)
{
  if(!holding(&p->lock))
    panic("wakeup1");
  if(p->chan == p && p->state == SLEEPING) {
    p->state = RUNNABLE;
  }
}

// 标记给定 pid 的进程为已终止。目标进程要到尝试返回用户态时才真正退出，详见 trap.c 的 usertrap()。
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // 若目标正在睡眠，先改为可运行，使它有机会观察 killed 标志。
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// 根据 user_dst 把数据复制到用户地址或内核地址；成功返回 0，失败返回 -1。
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// 根据 user_src 从用户地址或内核地址复制数据；成功返回 0，失败返回 -1。
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// 用户在控制台按下 Ctrl-P 时打印进程列表，供调试使用。
// 此处故意不加锁，避免在系统已经卡住时因等待锁而彻底无法输出诊断信息。
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
