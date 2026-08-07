// 文件系统实现分为五层：
//   + 磁盘块：分配和释放原始磁盘块。
//   + 日志：保证多步骤更新在崩溃后仍具有原子性。
//   + 文件：分配 inode，并读写文件内容与元数据。
//   + 目录：内容是一组目录项的特殊 inode。
//   + 路径名：把 /usr/rtm/xv6/fs.c 之类的路径解析为 inode。
//
// 本文件实现底层文件系统操作；更高层的系统调用位于 sysfile.c。

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
// 原则上每个磁盘设备都应有一个超级块；当前 xv6 只使用一个设备。
struct superblock sb; 

// 从固定的 1 号磁盘块读取超级块。
static void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// 初始化文件系统并检查磁盘格式标识。
void
fsinit(int dev) {
  readsb(dev, &sb);
  if(sb.magic != FSMAGIC)
    panic("invalid file system");
  initlog(dev, &sb);
}

// 通过日志把一个磁盘块清零。
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}

// 磁盘块分配层。

// 在空闲位图中分配一个磁盘块，并在返回前将其清零。
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      // bi/8 定位字节，bi%8 定位字节中的位；0 表示对应磁盘块空闲。
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // 找到空闲块
        bp->data[bi/8] |= m;  // 在位图中把该块标记为已占用
        log_write(bp);
        brelse(bp);
        bzero(dev, b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  panic("balloc: out of blocks");
}

// 清除空闲位图中的对应位，释放一个磁盘块。
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;
  log_write(bp);
  brelse(bp);
}

// inode 层。
//
// 一个 inode 描述一个没有名字的文件。磁盘 inode 保存文件类型、大小、
// 硬链接数，以及保存文件内容的磁盘块地址。所有 inode 从 sb.inodestart
// 开始顺序排列，inode 编号决定它在磁盘上的位置。
//
// 内核还会缓存正在使用的 inode，使多个进程能在同一个内存对象上同步。
// 内存 inode 额外包含不会写入磁盘的管理字段，例如 ip->ref 和 ip->valid。
// 使用 inode 时需要区分以下四种状态：
//
// * 已分配：磁盘上的 type 非零。ialloc() 分配 inode；当引用数和硬链接数
//   都降为零时，iput() 才真正释放它。
//
// * 被引用：ip->ref 为零表示缓存槽空闲，否则它统计指向该槽的内存引用
//   （例如打开文件和当前目录）。iget() 查找或建立缓存项并增加 ref，
//   iput() 则减少 ref。
//
// * 数据有效：只有 ip->valid 为 1 时，缓存项中的类型、大小等磁盘字段
//   才可信。ilock() 必要时从磁盘读入 inode 并设置 valid；缓存项失去最后
//   一个引用后，iput() 会清除 valid。
//
// * 已加锁：检查或修改 inode 元数据及文件内容前，必须先持有 ip->lock。
//
// 典型调用顺序如下：
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... 检查并修改 ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() 与 iget() 分离，使系统调用可以长期持有 inode 引用（如打开文件），
// 但只在短暂访问数据时加锁（如 read()）。路径查找过程中，这种分离还能
// 避免死锁和竞争；iget() 增加 ref，保证缓存对象及其指针持续有效。
//
// 许多内部函数要求调用者提前锁住相关 inode，以便组合出多步骤原子操作。
//
// icache.lock 是自旋锁，保护缓存项的分配以及 ref、dev、inum。由于 ref
// 决定槽是否空闲，而 dev 与 inum 决定槽代表哪个 inode，访问这些字段时
// 必须持有 icache.lock。
//
// 每个 ip->lock 是睡眠锁，保护除 ref、dev、inum 外的所有字段。读写
// ip->valid、ip->size、ip->type 等内容时必须持有这把锁。

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} icache;

void
iinit()
{
  int i = 0;
  
  initlock(&icache.lock, "icache");
  for(i = 0; i < NINODE; i++) {
    initsleeplock(&icache.inode[i].lock, "inode");
  }
}

static struct inode* iget(uint dev, uint inum);

// 在设备 dev 上分配一个 inode，通过写入非零 type 把它标记为已分配。
// 返回的 inode 已被引用但尚未加睡眠锁。
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // type 为 0 表示空闲 inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);   // 通过日志把分配结果写入磁盘
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}

// 把修改后的内存 inode 同步到磁盘。inode 缓存采用写穿策略，因此每次
// 修改需要持久化的 ip->xxx 字段后都必须调用本函数；调用者须持有 ip->lock。
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  brelse(bp);
}

// 查找设备 dev 上编号为 inum 的 inode 并返回其内存缓存项。
// 本函数只建立引用，不加 inode 睡眠锁，也不从磁盘读取 inode 内容。
static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&icache.lock);

  // 优先复用已经代表同一磁盘 inode 的缓存项。
  empty = 0;
  for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // 记住第一个可复用的空闲槽
      empty = ip;
  }

  // 未命中时复用一个引用计数为 0 的缓存槽。
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&icache.lock);

  return ip;
}

// 增加 ip 的引用计数，并返回 ip，以支持 ip = idup(ip1) 这种写法。
struct inode*
idup(struct inode *ip)
{
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

// 锁住指定 inode；若缓存内容尚未生效，则在持锁期间从磁盘加载。
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if(ip->valid == 0){
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if(ip->type == 0)
      panic("ilock: no type");
  }
}

// 释放指定 inode 的睡眠锁。
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");

  releasesleep(&ip->lock);
}

// 释放一个内存 inode 引用。最后一个引用消失后，缓存槽可以被复用；
// 如果此时硬链接数也为零，还要释放磁盘 inode 及其数据块。
// iput() 可能修改磁盘，因此所有调用都必须位于文件系统事务中。
void
iput(struct inode *ip)
{
  acquire(&icache.lock);

  if(ip->ref == 1 && ip->valid && ip->nlink == 0){
    // 既没有硬链接也没有其他引用：截断文件并释放磁盘 inode。

    // ref == 1 表示其他进程不可能仍持有这个 inode，因而这里获取睡眠锁
    // 不会阻塞，也不会与其他持锁者形成死锁。
    acquiresleep(&ip->lock);

    release(&icache.lock);

    itrunc(ip);
    ip->type = 0;
    iupdate(ip);
    ip->valid = 0;

    releasesleep(&ip->lock);

    acquire(&icache.lock);
  }

  ip->ref--;
  release(&icache.lock);
}

// 常用组合操作：先解锁，再释放引用。
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

// inode 数据块映射。
//
// 文件内容保存在磁盘块中。前 NDIRECT 个块号直接记录在 ip->addrs[]；
// 后续 NINDIRECT 个块号存放在 ip->addrs[NDIRECT] 指向的一级间接块中；
// 再后面的 NINDIRECT*NINDIRECT 个块由 ip->addrs[NDIRECT+1] 二级索引。

// 返回 inode ip 中第 bn 个逻辑块对应的磁盘块号；映射不存在时自动分配。
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  // 去掉直接块范围后，bn 就成为一级间接块中的数组下标。
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // 先取得一级间接块；尚未分配时连同目标数据块一起按需分配。
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      a[bn] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

  // 去掉一级间接块范围后，bn 是二级间接区域内的逻辑块号。
  bn -= NINDIRECT;

  if(bn < NINDIRECT * NINDIRECT){
    // addrs[NDIRECT+1] 指向二级间接索引的根块。
    if((addr = ip->addrs[NDIRECT+1]) == 0)
      ip->addrs[NDIRECT+1] = addr = balloc(ip->dev);

    // 根块的第 bn/NINDIRECT 项指向一个一级索引块。
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn / NINDIRECT]) == 0){
      a[bn / NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);

    // 一级索引块的第 bn%NINDIRECT 项指向最终的数据块。
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn % NINDIRECT]) == 0){
      a[bn % NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}

// 截断 inode 并释放全部数据块；调用者必须持有 ip->lock。
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp, *bp2;
  uint *a, *a2;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  // 一级间接块中的每个非零表项都指向一个需要释放的数据块；
  // 最后再释放间接块自身。
  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  // 二级间接根块中的每个非零表项指向一个一级索引块；先释放其
  // 数据块，再释放一级索引块，最后释放二级根块本身。
  if(ip->addrs[NDIRECT+1]){
    bp = bread(ip->dev, ip->addrs[NDIRECT+1]);
    a = (uint*)bp->data;
    for(i = 0; i < NINDIRECT; i++){
      if(a[i]){
        bp2 = bread(ip->dev, a[i]);
        a2 = (uint*)bp2->data;
        for(j = 0; j < NINDIRECT; j++){
          if(a2[j])
            bfree(ip->dev, a2[j]);
        }
        brelse(bp2);
        bfree(ip->dev, a[i]);
      }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT+1]);
    ip->addrs[NDIRECT+1] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}

// 从 inode 复制 stat 信息；调用者必须持有 ip->lock。
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

// 从 inode 读取数据；调用者必须持有 ip->lock。
// user_dst 为 1 时 dst 是用户虚拟地址，否则是内核地址。
int
readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return 0;
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    // 单次只复制当前磁盘块内的剩余部分，循环再跨到下一块。
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1) {
      brelse(bp);
      tot = -1;
      break;
    }
    brelse(bp);
  }
  return tot;
}

// 向 inode 写入数据；调用者必须持有 ip->lock。
// user_src 为 1 时 src 是用户虚拟地址，否则是内核地址。
// 返回成功写入的字节数；返回值小于请求的 n 表示写入途中发生错误。
int
writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    return -1;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1) {
      brelse(bp);
      break;
    }
    log_write(bp);
    brelse(bp);
  }

  if(off > ip->size)
    ip->size = off;

  // 即使文件大小未变化也要写回 inode，因为上面的循环可能通过 bmap()
  // 新增了 ip->addrs[] 中的数据块映射。
  iupdate(ip);

  return tot;
}

// 目录操作。

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// 在目录中查找名称匹配的目录项；若找到且 poff 非空，则写回目录项偏移。
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if(de.inum == 0)
      continue;
    if(namecmp(name, de.name) == 0){
      // 当前目录项与路径分量匹配
      if(poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }

  return 0;
}

// 向目录 dp 写入一个新的 (name, inum) 目录项。
int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // 同名目录项已存在时拒绝重复插入。
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  // 复用 inum 为 0 的空目录项；没有空槽时 writei 会扩展目录文件。
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");

  return 0;
}

// 路径解析。

// 把 path 的下一个路径分量复制到 name，并返回下一个分量的起始位置。
// 返回的路径不会带前导斜杠，因此调用者可用 *path=='\0' 判断当前名称
// 是否为最后一个分量；若没有可提取的名称则返回 0。
//
// 示例：
//   skipelem("a/bb/c", name) = "bb/c"，同时 name = "a"
//   skipelem("///a//bb", name) = "bb"，同时 name = "a"
//   skipelem("a", name) = ""，同时 name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  // 路径分量超过 DIRSIZ 时按磁盘目录项宽度截断；较短时补字符串终止符。
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

// 沿路径逐级查找并返回 inode。nameiparent 非零时返回父目录 inode，
// 并把最后一个路径分量复制到至少能容纳 DIRSIZ 字节的 name。
// 本函数会调用 iput()，因此必须在文件系统事务中使用。
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  if(*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(myproc()->cwd);

  while((path = skipelem(path, name)) != 0){
    ilock(ip);
    if(ip->type != T_DIR){
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // 调用者需要父目录，故在最后一个路径分量前提前停止。
      iunlock(ip);
      return ip;
    }
    if((next = dirlookup(ip, name, 0)) == 0){
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}
