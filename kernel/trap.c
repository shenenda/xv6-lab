#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// kernelvec.S 的内核陷阱入口最终会调用 kerneltrap()。
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// 为当前 hart 设置内核态异常和中断入口。
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// 处理来自用户态的中断、异常或系统调用，由 trampoline.S 调用。
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // 已进入内核页表和内核栈，后续陷阱应交给 kerneltrap()，因此立即改写 stvec。
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // 保存触发陷阱时的用户程序计数器，返回用户态时还要恢复。
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // scause=8 表示用户态执行 ecall，即系统调用。

    if(p->killed)
      exit(-1);

    // sepc 指向 ecall 本身；系统调用完成后应继续执行下一条 4 字节指令。
    p->trapframe->epc += 4;

    // 中断会改动 sstatus 等陷阱寄存器，完成上述读取和修改后才能重新开中断。
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // 已识别并处理设备中断。
  } else if((r_scause() == 13 || r_scause() == 15) &&
            kama_uvmshouldallocate(r_stval())){
    // scause=13/15 分别表示用户态加载/存储页错误。若故障地址属于
    // sbrk() 已声明但尚未映射的堆区，则在第一次访问时补上物理页。
    // 不推进 epc，返回用户态后让 CPU 重新执行刚才失败的指令。
    kama_uvmlazyallocate(r_stval());
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // 定时器中断到来时主动让出 CPU，实现抢占式调度。
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// 准备返回用户态。
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // 即将把陷阱入口从 kerneltrap() 切换回 uservec；在真正回到用户态前先关中断，
  // 避免内核执行期间误用面向用户态的入口。
  intr_off();

  // stvec 指向 trampoline 页内的 uservec，接收下一次用户态陷阱。
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // 填好 uservec 下次进入内核时需要的页表、内核栈、C 入口和 hart 编号。
  p->trapframe->kernel_satp = r_satp();         // 内核页表的 satp 值。
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // 当前进程的内核栈顶。
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // cpuid() 使用的 hart 编号。

  // 设置 trampoline.S 执行 sret 返回用户态所依赖的状态寄存器。
  
  // 把 sret 的目标特权级设置为用户模式。
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // 清零 SPP，使 sret 返回用户模式。
  x |= SSTATUS_SPIE; // sret 后通过 SPIE 恢复用户态中断使能。
  w_sstatus(x);

  // sepc 设置为保存的用户 pc，sret 会从该地址继续执行。
  w_sepc(p->trapframe->epc);

  // 把用户页表编码成 satp，作为 userret 的第二个参数。
  uint64 satp = MAKE_SATP(p->pagetable);

  // 通过函数指针跳到高地址处的 userret；它切换用户页表、恢复寄存器，
  // 最后执行 sret 进入用户模式。
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// 内核代码中的中断和异常经 kernelvec 到达这里，并继续使用当前内核栈。
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // 定时器中断到来时主动让出 CPU，实现抢占式调度。
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // yield() 期间可能又发生陷阱并覆盖 CSR，因此返回 kernelvec.S 前恢复原 sepc 和 sstatus。
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// 判断并处理外部中断或软件中断。定时器中断返回 2，其他设备中断返回 1，
// 无法识别时返回 0。
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // scause 的中断位为 1 且编号为 9，表示经 PLIC 到达的监管者外部中断。

    // 从 PLIC claim 寄存器取得发起中断的设备 IRQ。
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // PLIC 同一时刻只允许每个设备挂起一个中断；处理完成后写 complete，
    // 才允许该设备再次发起中断。
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // 机器模式定时器中断由 kernelvec.S 的 timervec 转发成监管者软件中断。

    if(cpuid() == 0){
      clockintr();
    }
    
    // 清除 sip 中的 SSIP 位，确认并结束这次软件中断。
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

