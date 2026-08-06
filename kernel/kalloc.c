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
  // 每个 CPU 拥有一条空闲页链表；不同 CPU 的常规分配/释放不再争用同一把锁。
  struct spinlock lock;
  // freelist 指向空闲页单链表的表头。
  struct run *freelist;
} kmem[NCPU];

// initlock 只保存名字指针，因此名字必须具有静态存储期，不能使用 kinit 的栈数组。
static char kmem_lockname[NCPU][8];

// 本地链表耗尽时一次搬运少量页面，避免逐页跨 CPU 加锁拖慢大规模分配。
#define KMEM_STEAL_BATCH 64

void
kinit()
{
  for(int i = 0; i < NCPU; i++){
    snprintf(kmem_lockname[i], sizeof(kmem_lockname[i]), "kmem_%d", i);
    initlock(&kmem[i].lock, kmem_lockname[i]);
  }
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
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
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

  // 用固定垃圾值覆盖整页，便于尽早暴露释放后仍访问该页的悬空引用。
  memset(pa, 1, PGSIZE);

  // 释放后不再保留原页面内容，可将页首强制转换成空闲链表节点。
  r = (struct run*)pa;

  // 关中断期间 CPU 编号保持稳定，页面归还到当前 CPU 的空闲链表。
  push_off();
  int id = cpuid();
  acquire(&kmem[id].lock);
  // 采用头插法把页面压入空闲链表，操作时间为 O(1)。
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
  pop_off();
}

// 分配一页 4096 字节的物理内存，并返回内核可直接访问的指针。
// 空闲链表为空时返回 0。
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int id = cpuid();

  acquire(&kmem[id].lock);
  // 优先从当前 CPU 的链表头取页，常见路径只涉及一把本地锁。
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  release(&kmem[id].lock);

  // 本地链表为空时，从其他 CPU 窃取一批页面。
  // 每次最多搬运 KMEM_STEAL_BATCH 页，避免 countfree 一类大规模分配逐页跨 CPU 加锁。
  // 搬运时不持有本地锁再申请远端锁，避免两个 CPU 形成 AB/BA 相互等待。
  if(r == 0){
    for(int antid = 0; antid < NCPU; antid++){
      if(antid == id)
        continue;
      acquire(&kmem[antid].lock);
      r = kmem[antid].freelist;
      struct run *tail = r;
      int n = 1;
      while(tail && tail->next && n < KMEM_STEAL_BATCH){
        tail = tail->next;
        n++;
      }
      if(tail){
        kmem[antid].freelist = tail->next;
        tail->next = 0;
      }
      release(&kmem[antid].lock);

      if(r){
        // 批次中的第一页直接返回，其余页面转入当前 CPU 的链表。
        // 远端锁已释放，因此这里仍然只持有一把 kmem 锁。
        struct run *rest = r->next;
        r->next = 0;
        if(rest){
          acquire(&kmem[id].lock);
          tail->next = kmem[id].freelist;
          kmem[id].freelist = rest;
          release(&kmem[id].lock);
        }
        break;
      }
    }
  }

  pop_off();

  // 页面已经从共享链表移除，后续填充无需继续占用自旋锁。
  if(r)
    memset((char*)r, 5, PGSIZE); // 用垃圾值填充，帮助发现未初始化内存的使用。
  return (void*)r;
}
