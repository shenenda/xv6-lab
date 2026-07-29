// 自旋互斥锁实现。

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
}

// 获取锁；锁忙时在原地循环自旋，直到原子交换成功。
void
acquire(struct spinlock *lk)
{
  push_off(); // 关闭当前 CPU 中断，避免中断处理程序再次获取同一把锁而死锁。
  if(holding(lk))
    panic("acquire");

  // 在 RISC-V 上，__sync_lock_test_and_set 会生成带获取语义的原子交换：
  //   a5 = 1
  //   s1 = &lk->locked
  //   amoswap.w.aq a5, a5, (s1)
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0)
    ;

  // 编译器与处理器内存屏障禁止把临界区内的读写重排到加锁之前。
  // 在 RISC-V 上会生成 fence 指令。
  __sync_synchronize();

  // 记录持锁 CPU，供 holding() 检查和调试使用。
  lk->cpu = mycpu();
}

// 释放锁。
void
release(struct spinlock *lk)
{
  if(!holding(lk))
    panic("release");

  lk->cpu = 0;

  // 释放前的内存屏障保证临界区写入先对其他 CPU 可见，也禁止临界区读取
  // 被重排到解锁之后；在 RISC-V 上会生成 fence 指令。
  __sync_synchronize();

  // 原子释放等价于把 lk->locked 置 0。普通 C 赋值不保证只用一条原子存储，
  // 因此使用 __sync_lock_release；在 RISC-V 上会生成原子交换：
  //   s1 = &lk->locked
  //   amoswap.w zero, zero, (s1)
  __sync_lock_release(&lk->locked);

  pop_off();
}

// 检查当前 CPU 是否持有该锁；调用时必须关闭中断以固定当前 CPU。
int
holding(struct spinlock *lk)
{
  int r;
  r = (lk->locked && lk->cpu == mycpu());
  return r;
}

// push_off/pop_off 与 intr_off/intr_on 类似，但按嵌套层数成对恢复：两次 push_off
// 需要两次 pop_off；若最外层调用前中断本就关闭，最终仍保持关闭。

void
push_off(void)
{
  int old = intr_get();

  intr_off();
  if(mycpu()->noff == 0)
    mycpu()->intena = old;
  mycpu()->noff += 1;
}

void
pop_off(void)
{
  struct cpu *c = mycpu();
  if(intr_get())
    panic("pop_off - interruptible");
  if(c->noff < 1)
    panic("pop_off");
  c->noff -= 1;
  if(c->noff == 0 && c->intena)
    intr_on();
}
