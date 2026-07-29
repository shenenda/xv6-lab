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

void
kinit()
{
  initlock(&kmem.lock, "kmem");
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
  return (void*)r;
}


// kernel/kalloc.c
// 获取空闲内存 以页为单位
void
kama_freebytes(uint64 *dst) //*dst是调用者的info.freemem
{
  *dst = 0;

  acquire(&kmem.lock);

  //定义一个指向 struct run 结构体的指针 
  //kmem.freelist指向空闲页链表的第一个节点
  struct run *p = kmem.freelist; //表示让 p 从第一个空闲页开始遍历

  while(p){
    *dst += PGSIZE;
    p = p->next;
  }

  release(&kmem.lock);
}