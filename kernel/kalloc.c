// 物理内存页分配器，为用户进程、内核栈、页表页和管道缓冲区提供内存。
// 分配与释放的最小单位都是一个完整的 4096 字节页面。

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // 内核镜像结束后的第一个地址，由 kernel.ld 定义。

// 空闲页面自身被当作链表节点使用，页面开头几个字节保存下一节点指针。
// 这种侵入式链表不需要额外分配管理结构。
struct run {
  struct run *next;
};

struct {
  // 多个 CPU 可能同时分配或释放页面，因此用自旋锁保护空闲链表。
  struct spinlock lock;
  // freelist 指向空闲页单链表的表头。
  struct run *freelist;
} kmem;

// 以 KERNBASE 后的物理页号为下标，记录每个物理页当前被引用的次数。
// 只有引用计数降到 0 的页面才允许重新进入空闲链表。
#define PA2PGREF_ID(p) (((uint64)(p) - KERNBASE) / PGSIZE)
#define PGREF_MAX_ENTRIES ((PHYSTOP - KERNBASE) / PGSIZE)
#define PA2PGREF(p) (pageref[PA2PGREF_ID(p)])

static int pageref[PGREF_MAX_ENTRIES];
static struct spinlock pgreflock;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&pgreflock, "pgref");
  // 将内核镜像末尾到物理内存上限之间的完整页面加入空闲链表。
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  // 起始地址向上对齐到页边界，避免把包含内核数据的残缺页交给分配器。
  p = (char*)PGROUNDUP((uint64)pa_start);
  // char* 指针每加一表示前进一个字节，因此可直接按 PGSIZE 跨页。
  // 只有整页都位于区间内时才释放，末尾不足一页的部分会被跳过。
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    // kfree() 统一执行“引用减一”；初始化空闲页时先建立唯一引用，
    // 再让 kfree() 把计数从 1 降到 0，避免负引用计数破坏不变量。
    acquire(&pgreflock);
    PA2PGREF(p) = 1;
    release(&pgreflock);
    kfree(p);
  }
}

// 释放 pa 指向的一页物理内存。
// 通常该页面应由 kalloc 返回；唯一例外是 kinit 初始化时首次加入的空闲页。
void
kfree(void *pa)
{
  struct run *r;

  // 依次检查页对齐、不能落入内核镜像、不能超过可用物理内存上限。
  // 取模结果不为 0 表示 pa 不是页面起始地址。
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // 解除一次引用。仍有页表或内核对象引用该页时，绝不能把它交回分配器。
  acquire(&pgreflock);
  if(PA2PGREF(pa) < 1){
    release(&pgreflock);
    panic("kfree: ref");
  }
  PA2PGREF(pa)--;
  int refs = PA2PGREF(pa);
  release(&pgreflock);

  if(refs > 0)
    return;

  // 用固定垃圾值覆盖整页，便于尽早暴露释放后仍访问该页的悬空引用。
  memset(pa, 1, PGSIZE);

  // 释放后不再保留原页面内容，可将页首强制转换成空闲链表节点。
  r = (struct run*)pa;

  acquire(&kmem.lock);
  // 采用头插法把页面压入空闲链表，操作时间为 O(1)。
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// 分配一页 4096 字节的物理内存，并返回内核可直接访问的指针。
// 空闲链表为空时返回 0。
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  // 从链表头取出一个空闲页；freelist 为 0 时 r 也为 0。
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  // 页面已经从共享链表移除，后续填充无需继续占用自旋锁。
  if(r)
    memset((char*)r, 5, PGSIZE); // 用垃圾值填充，帮助发现未初始化内存的使用。

  if(r){
    // 新分配页由调用者持有唯一引用。计数必须在页面被分享前建立。
    acquire(&pgreflock);
    if(PA2PGREF(r) != 0){
      release(&pgreflock);
      panic("kalloc: ref");
    }
    PA2PGREF(r) = 1;
    release(&pgreflock);
  }
  return (void*)r;
}

// fork() 新增一个共享映射时，为对应物理页增加一次引用。
void
kama_krefpage(uint64 pa)
{
  if((pa % PGSIZE) != 0 || pa < KERNBASE || pa >= PHYSTOP)
    panic("krefpage");

  acquire(&pgreflock);
  if(PA2PGREF(pa) < 1){
    release(&pgreflock);
    panic("krefpage: ref");
  }
  PA2PGREF(pa)++;
  release(&pgreflock);
}

// 为 COW 写入准备私有页，并解除调用者对旧共享页的一次引用。
// 如果调用者已经是最后一个引用者，只需复用原页而不进行无意义复制。
void *
kama_kcopy_n_deref(void *pa)
{
  char *mem;

  if(((uint64)pa % PGSIZE) != 0 || (uint64)pa < KERNBASE ||
     (uint64)pa >= PHYSTOP)
    panic("kcopy: pa");

  acquire(&pgreflock);
  if(PA2PGREF(pa) < 1){
    release(&pgreflock);
    panic("kcopy: ref");
  }
  if(PA2PGREF(pa) == 1){
    release(&pgreflock);
    return pa;
  }
  release(&pgreflock);

  // 分配和整页复制都可能耗时，不能放在引用计数自旋锁的临界区内。
  mem = kalloc();
  if(mem == 0){
    // 等待分配期间其他共享者可能已经退出；此时原页可直接复用。
    acquire(&pgreflock);
    int sole_owner = PA2PGREF(pa) == 1;
    release(&pgreflock);
    return sole_owner ? pa : 0;
  }
  memmove(mem, pa, PGSIZE);

  // 复制期间共享者也可能退出，因此重新检查。若已成为唯一引用者，
  // 丢弃刚分配的页并直接恢复原页写权限；否则转移本次引用到新页。
  acquire(&pgreflock);
  if(PA2PGREF(pa) < 1){
    release(&pgreflock);
    panic("kcopy: lost ref");
  }
  if(PA2PGREF(pa) == 1){
    release(&pgreflock);
    kfree(mem);
    return pa;
  }
  PA2PGREF(pa)--;
  release(&pgreflock);

  return mem;
}
