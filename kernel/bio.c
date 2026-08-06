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

#define NBUCKET 17
#define HASH(id) ((id) % NBUCKET)

struct hashbuf {
  struct buf head;
  struct spinlock lock;
};

struct {
  // 只串行化缓存未命中后的复查和淘汰；命中路径不获取这把全局锁。
  struct spinlock lock;
  struct buf buf[NBUF];
  struct hashbuf buckets[NBUCKET];
} bcache;

// initlock 不复制字符串，使用静态二维数组保证锁名在 binit 返回后仍然有效。
static char bucket_lockname[NBUCKET][16];

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");
  for(int i = 0; i < NBUCKET; i++){
    snprintf(bucket_lockname[i], sizeof(bucket_lockname[i]), "bcache_%d", i);
    initlock(&bcache.buckets[i].lock, bucket_lockname[i]);
    bcache.buckets[i].head.prev = &bcache.buckets[i].head;
    bcache.buckets[i].head.next = &bcache.buckets[i].head;
  }

  // 初始化时所有缓冲区都放入 0 号桶，之后复用时再移动到目标哈希桶。
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.buckets[0].head.next;
    b->prev = &bcache.buckets[0].head;
    initsleeplock(&b->lock, "buffer");
    bcache.buckets[0].head.next->prev = b;
    bcache.buckets[0].head.next = b;
  }
}

// 在缓冲区缓存中查找设备 dev 上的指定块；若未命中则复用一个空闲缓冲区。
// 无论命中还是复用，返回前都会持有该缓冲区自己的睡眠锁。
// 不变量：任意 (dev, blockno) 在整个缓存中最多只有一个副本。
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b, *tmp;
  int bid = HASH(blockno);

  acquire(&bcache.buckets[bid].lock);

  // 命中路径只锁对应哈希桶，不同桶中的磁盘块可以并行查找。
  for(b = bcache.buckets[bid].head.next;
      b != &bcache.buckets[bid].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      acquire(&tickslock);
      b->timestamp = ticks;
      release(&tickslock);
      release(&bcache.buckets[bid].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.buckets[bid].lock);

  // 全局锁只串行化未命中者，保证同一 (dev, blockno) 不会被重复创建。
  // 命中者只拿一个桶锁，因此未命中路径不能长期同时持有所有桶锁。
  acquire(&bcache.lock);

  // 等待全局锁期间，另一个 CPU 可能已经缓存了目标块，所以必须再次检查。
  acquire(&bcache.buckets[bid].lock);
  for(b = bcache.buckets[bid].head.next;
      b != &bcache.buckets[bid].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      acquire(&tickslock);
      b->timestamp = ticks;
      release(&tickslock);
      release(&bcache.buckets[bid].lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.buckets[bid].lock);

  // 每次只锁一个桶并记录 LRU 候选；真正复用前重新加锁验证，
  // 防止扫描期间命中者改变候选缓冲区的引用计数或时间戳。
  for(;;){
    b = 0;
    int oldbid = -1;
    uint oldest = 0;
    for(int i = 0; i < NBUCKET; i++){
      acquire(&bcache.buckets[i].lock);
      for(tmp = bcache.buckets[i].head.next;
          tmp != &bcache.buckets[i].head; tmp = tmp->next){
        if(tmp->refcnt == 0 && (b == 0 || tmp->timestamp < oldest)){
          b = tmp;
          oldbid = i;
          oldest = tmp->timestamp;
        }
      }
      release(&bcache.buckets[i].lock);
    }

    if(b == 0)
      panic("bget: no buffers");

    acquire(&bcache.buckets[oldbid].lock);
    if(b->refcnt != 0 || b->timestamp != oldest){
      release(&bcache.buckets[oldbid].lock);
      continue;
    }

    // 只有未命中者会同时获取两个桶锁，而未命中者又被全局锁串行化，
    // 所以旧桶到目标桶的移动不会与另一个双锁路径形成环路等待。
    if(oldbid != bid)
      acquire(&bcache.buckets[bid].lock);

    if(oldbid != bid){
      b->next->prev = b->prev;
      b->prev->next = b->next;
      b->next = bcache.buckets[bid].head.next;
      b->prev = &bcache.buckets[bid].head;
      bcache.buckets[bid].head.next->prev = b;
      bcache.buckets[bid].head.next = b;
    }

    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
    acquire(&tickslock);
    b->timestamp = ticks;
    release(&tickslock);

    if(oldbid != bid)
      release(&bcache.buckets[bid].lock);
    release(&bcache.buckets[oldbid].lock);
    release(&bcache.lock);
    acquiresleep(&b->lock);
    return b;
  }
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
// 时间戳替代原链表位置表达最近使用次序，因此无需移动节点。
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  int bid = HASH(b->blockno);
  releasesleep(&b->lock);

  acquire(&bcache.buckets[bid].lock);
  b->refcnt--;
  acquire(&tickslock);
  b->timestamp = ticks;
  release(&tickslock);
  release(&bcache.buckets[bid].lock);
}

// 日志系统用额外引用固定缓冲区，防止提交完成前被缓存回收。
void
bpin(struct buf *b) {
  int bid = HASH(b->blockno);
  acquire(&bcache.buckets[bid].lock);
  b->refcnt++;
  release(&bcache.buckets[bid].lock);
}

// 撤销 bpin 增加的引用，使缓冲区重新具备被回收的条件。
void
bunpin(struct buf *b) {
  int bid = HASH(b->blockno);
  acquire(&bcache.buckets[bid].lock);
  b->refcnt--;
  release(&bcache.buckets[bid].lock);
}

