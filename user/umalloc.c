#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

// 内存分配器源自 Kernighan 与 Ritchie《The C Programming Language》
// 第二版第 8.7 节。

typedef long Align;

// 每个内存块前放置一个 Header；联合体中的 Align 强制块按 long 对齐。
union header {
  struct {
    union header *ptr;
    uint size;
  } s;
  Align x;
};

typedef union header Header;

static Header base;
static Header *freep;

void
free(void *ap)
{
  Header *bp, *p;

  // 用户指针前一个 Header 保存块大小；空闲块按地址组成环形链表。
  bp = (Header*)ap - 1;
  for(p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
    // p >= p->ptr 表示走到地址空间首尾相接处；新块落在最高端或最低端都应插在这里。
    if(p >= p->s.ptr && (bp > p || bp < p->s.ptr))
      break;
  // 分别与后继、前驱做地址相邻检查，尽可能合并连续空闲块。
  if(bp + bp->s.size == p->s.ptr){
    bp->s.size += p->s.ptr->s.size;
    bp->s.ptr = p->s.ptr->s.ptr;
  } else
    bp->s.ptr = p->s.ptr;
  if(p + p->s.size == bp){
    p->s.size += bp->s.size;
    p->s.ptr = bp->s.ptr;
  } else
    p->s.ptr = bp;
  freep = p;
}

static Header*
morecore(uint nu)
{
  char *p;
  Header *hp;

  // 批量向 sbrk 申请至少 4096 个 Header 单元，降低系统调用频率。
  if(nu < 4096)
    nu = 4096;
  p = sbrk(nu * sizeof(Header));
  if(p == (char*)-1)
    return 0;
  hp = (Header*)p;
  hp->s.size = nu;
  free((void*)(hp + 1));
  return freep;
}

void*
malloc(uint nbytes)
{
  Header *p, *prevp;
  uint nunits;

  // 向上取整为 Header 单元数，并额外预留一个单元存放块头。
  nunits = (nbytes + sizeof(Header) - 1)/sizeof(Header) + 1;
  if((prevp = freep) == 0){
    base.s.ptr = freep = prevp = &base;
    base.s.size = 0;
  }
  for(p = prevp->s.ptr; ; prevp = p, p = p->s.ptr){
    if(p->s.size >= nunits){
      if(p->s.size == nunits)
        prevp->s.ptr = p->s.ptr;
      else {
        // 从空闲块尾部切下所需大小，原块头继续代表剩余的前半部分。
        p->s.size -= nunits;
        p += p->s.size;
        p->s.size = nunits;
      }
      freep = prevp;
      return (void*)(p + 1);
    }
    // 环绕一周仍未找到足够大的块时，再扩张进程堆。
    if(p == freep)
      if((p = morecore(nunits)) == 0)
        return 0;
  }
}
