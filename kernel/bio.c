// 缓冲区缓存。
//
// 缓冲区缓存由 buf 结构组成的链表保存磁盘块内容的缓存副本。
// 把磁盘块留在内存中既能减少磁盘读取，也为多个进程访问同一磁盘块
// 提供统一的同步点。
//
// 接口约定：
// * 调用 bread 获取指定磁盘块对应的、已经加锁的缓冲区。
// * 修改缓冲区数据后，调用 bwrite 把内容写回磁盘。
// * 使用完毕后调用 brelse；释放后不能继续访问该缓冲区。
// * 同一时刻只有一个进程能持有某个缓冲区，因此不要无故长期占用它。


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // 所有缓冲区通过 prev/next 组成双向链表，并按最近使用时间排序。
  // head 是不保存数据的哨兵节点：head.next 最新，head.prev 最旧。
  // bcache.lock 保护块到缓冲区的映射、引用计数以及这条链表。
  struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // 先让哨兵节点自环，再把每个缓冲区插到链表头部。
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// 在缓冲区缓存中查找设备 dev 上的指定块；若未命中则复用一个空闲缓冲区。
// 无论命中还是复用，返回前都会持有该缓冲区自己的睡眠锁。
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // 命中时必须在全局锁保护下增加引用计数，避免该缓冲区同时被回收复用。
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      // 获取睡眠锁可能阻塞，所以先释放短期持有的全局自旋锁。
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 未命中时，从链表尾部开始寻找引用计数为 0 的最久未使用缓冲区。
  // 先写入新的块标识并清除 valid，随后真正读取磁盘内容。
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// 返回已经加睡眠锁、且包含指定磁盘块有效内容的缓冲区。
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  // valid 为 0 表示缓冲区刚被复用，必须先由磁盘驱动填充数据。
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// 把 b 的内容写回磁盘；调用者必须持有 b 的睡眠锁。
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// 释放一个已经加锁的缓冲区。
// 最后一个引用消失时，把它移到最近使用链表头部，供后续 LRU 选择使用。
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // 引用计数归零后，才允许调整链表位置并让该缓冲区成为可复用对象。
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}

// 日志系统用额外引用固定缓冲区，防止提交完成前被缓存回收。
void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

// 撤销 bpin 增加的引用，使缓冲区重新具备被回收的条件。
void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}


