#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

/* 用户线程可能处于以下状态： */
#define FREE        0x0
#define RUNNING     0x1
#define RUNNABLE    0x2

#define STACK_SIZE  8192
#define MAX_THREAD  4

// A user thread only needs the registers that must survive thread_switch().
struct context {
  uint64 ra;
  uint64 sp;

  // callee-saved registers
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

struct thread {
  char       stack[STACK_SIZE]; /* 该线程独占的用户栈。 */
  int        state;             /* 状态取 FREE、RUNNING 或 RUNNABLE。 */
  struct context context;       /* 线程切换时保存的最小寄存器上下文。 */
};
struct thread all_thread[MAX_THREAD];
struct thread *current_thread;
extern void thread_switch(struct context *old, struct context *new);
              
void 
thread_init(void)
{
  // main() 作为 0 号线程第一次调用 thread_schedule()。它也需要自己的栈空间，
  // 以便首次 thread_switch() 保存上下文。0 号线程保持 RUNNING，而调度器只选择
  // RUNNABLE 线程，因此完成第一次切换后不会再调度回 main。
  current_thread = &all_thread[0];
  current_thread->state = RUNNING;
}

void 
thread_schedule(void)
{
  struct thread *t, *next_thread;

  /* 从当前线程的下一个表项开始循环查找可运行线程。 */
  next_thread = 0;
  t = current_thread + 1;
  for(int i = 0; i < MAX_THREAD; i++){
    if(t >= all_thread + MAX_THREAD)
      t = all_thread;
    if(t->state == RUNNABLE) {
      next_thread = t;
      break;
    }
    t = t + 1;
  }

  if (next_thread == 0) {
    printf("thread_schedule: no runnable threads\n");
    exit(-1);
  }

  if (current_thread != next_thread) {         /* 只有目标不同才需要切换上下文。 */
    next_thread->state = RUNNING;
    t = current_thread;
    current_thread = next_thread;
    thread_switch(&t->context, &next_thread->context);
  } else
    next_thread = 0;
}

void 
thread_create(void (*func)())
{
  struct thread *t;

  // 指针在连续的线程表中前进，寻找可复用的 FREE 表项。
  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == FREE) break;
  }
  if (t == all_thread + MAX_THREAD) {
    printf("thread_create: no free thread slots\n");
    exit(-1);
  }

  // A reused slot must not retain registers from its previous incarnation.
  memset(&t->context, 0, sizeof(t->context));
  t->state = RUNNABLE;
  // thread_switch() ends in ret, so ra is the first instruction to run.
  t->context.ra = (uint64)func;
  // The stack grows down. Keep the initial stack pointer 16-byte aligned.
  t->context.sp = ((uint64)(t->stack + STACK_SIZE)) & ~((uint64)0xf);
}

void 
thread_yield(void)
{
  // 协作式调度：当前线程先标记为可运行，再主动选择下一个线程。
  current_thread->state = RUNNABLE;
  thread_schedule();
}

volatile int a_started, b_started, c_started;
volatile int a_n, b_n, c_n;

void 
thread_a(void)
{
  int i;
  printf("thread_a started\n");
  a_started = 1;
  while(b_started == 0 || c_started == 0)
    thread_yield();
  
  for (i = 0; i < 100; i++) {
    printf("thread_a %d\n", i);
    a_n += 1;
    thread_yield();
  }
  printf("thread_a: exit after %d\n", a_n);

  current_thread->state = FREE;
  thread_schedule();
}

void 
thread_b(void)
{
  int i;
  printf("thread_b started\n");
  b_started = 1;
  while(a_started == 0 || c_started == 0)
    thread_yield();
  
  for (i = 0; i < 100; i++) {
    printf("thread_b %d\n", i);
    b_n += 1;
    thread_yield();
  }
  printf("thread_b: exit after %d\n", b_n);

  current_thread->state = FREE;
  thread_schedule();
}

void 
thread_c(void)
{
  int i;
  printf("thread_c started\n");
  c_started = 1;
  while(a_started == 0 || b_started == 0)
    thread_yield();
  
  for (i = 0; i < 100; i++) {
    printf("thread_c %d\n", i);
    c_n += 1;
    thread_yield();
  }
  printf("thread_c: exit after %d\n", c_n);

  current_thread->state = FREE;
  thread_schedule();
}

int 
main(int argc, char *argv[]) 
{
  a_started = b_started = c_started = 0;
  a_n = b_n = c_n = 0;
  thread_init();
  thread_create(thread_a);
  thread_create(thread_b);
  thread_create(thread_c);
  thread_schedule();
  exit(0);
}
