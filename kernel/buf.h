struct buf {
  int valid;   // 是否已经从磁盘读入有效数据
  int disk;    // 磁盘驱动当前是否拥有该缓冲区
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // LRU 缓存双向链表中的前驱
  struct buf *next;
  uchar data[BSIZE];
};

