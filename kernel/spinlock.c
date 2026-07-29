// 自旋互斥锁实现。

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

#ifdef LAB_LOCK
#define NLOCK 500

// 记录已经初始化的锁，供 statistics 系统调用汇总竞争次数。
// lock_locks 专门保护这张登记表，避免多个 CPU 并发增删槽位。
static struct spinlock *locks[NLOCK];
struct spinlock lock_locks;

void
freelock(struct spinlock *lk)
{
  acquire(&lock_locks);
  int i;
  for (i = 0; i < NLOCK; i++) {
    if(locks[i] == lk) {
      locks[i] = 0;
      break;
    }
  }
  release(&lock_locks);
}

static void
findslot(struct spinlock *lk) {
  acquire(&lock_locks);
  int i;
  for (i = 0; i < NLOCK; i++) {
    if(locks[i] == 0) {
      locks[i] = lk;
      release(&lock_locks);
      return;
    }
  }
  panic("findslot");
}
#endif

void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
#ifdef LAB_LOCK
  lk->nts = 0;
  lk->n = 0;
  findslot(lk);
#endif  
}

// 获取锁；锁忙时在原地循环自旋，直到原子交换成功。
void
acquire(struct spinlock *lk)
{
  push_off(); // 关闭当前 CPU 中断，避免中断处理程序再次获取同一把锁而死锁。
  if(holding(lk))
    panic("acquire");

#ifdef LAB_LOCK
    // n 统计 acquire 调用次数；原子累加避免统计本身引入新的数据竞争。
    __sync_fetch_and_add(&(lk->n), 1);
#endif      

  // 在 RISC-V 上，__sync_lock_test_and_set 会生成带获取语义的原子交换：
  //   a5 = 1
  //   s1 = &lk->locked
  //   amoswap.w.aq a5, a5, (s1)
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0) {
#ifdef LAB_LOCK
    // 每次原子交换失败都说明发生了一次自旋竞争。
    __sync_fetch_and_add(&(lk->nts), 1);
#else
   ;
#endif
  }

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

#ifdef LAB_LOCK
int
snprint_lock(char *buf, int sz, struct spinlock *lk)
{
  int n = 0;
  if(lk->n > 0) {
    n = snprintf(buf, sz, "lock: %s: #fetch-and-add %d #acquire() %d\n",
                 lk->name, lk->nts, lk->n);
  }
  return n;
}

int
statslock(char *buf, int sz) {
  int n;
  int tot = 0;

  acquire(&lock_locks);
  n = snprintf(buf, sz, "--- lock kmem/bcache stats\n");
  for(int i = 0; i < NLOCK; i++) {
    if(locks[i] == 0)
      break;
    if(strncmp(locks[i]->name, "bcache", strlen("bcache")) == 0 ||
       strncmp(locks[i]->name, "kmem", strlen("kmem")) == 0) {
      tot += locks[i]->nts;
      n += snprint_lock(buf +n, sz-n, locks[i]);
    }
  }
  
  n += snprintf(buf+n, sz-n, "--- top 5 contended locks:\n");
  int last = 100000000;
  // 用五轮线性扫描依次找出竞争次数递减的前五把锁；实现直接但复杂度较高。
  for(int t = 0; t < 5; t++) {
    int top = 0;
    for(int i = 0; i < NLOCK; i++) {
      if(locks[i] == 0)
        break;
      if(locks[i]->nts > locks[top]->nts && locks[i]->nts < last) {
        top = i;
      }
    }
    n += snprint_lock(buf+n, sz-n, locks[top]);
    last = locks[top]->nts;
  }
  n += snprintf(buf+n, sz-n, "tot= %d\n", tot);
  release(&lock_locks);  
  return n;
}
#endif
