// file 根据 type 解释 pipe、ip、off 和 major 等字段，相当于一个手工实现的变体结构。
struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
  int ref; // 引用计数
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE
  struct inode *ip;  // FD_INODE 和 FD_DEVICE 共用
  uint off;          // FD_INODE
  short major;       // FD_DEVICE
};

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

// inode 的内存副本
struct inode {
  uint dev;           // 设备号
  uint inum;          // inode 编号
  int ref;            // 引用计数
  struct sleeplock lock; // 保护此字段以下的所有成员
  int valid;          // 是否已经从磁盘读入 inode

  short type;         // 磁盘 inode 的内存副本
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+1];
};

// 把主设备号映射到相应的设备读写函数。
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
