// 供进程长时间持有的睡眠锁。
struct sleeplock {
  uint locked;       // 锁是否已被持有。
  struct spinlock lk; // 保护睡眠锁内部状态的短时自旋锁。
  
  // 以下字段用于调试和持锁检查：
  char *name;        // 锁的名称。
  int pid;           // 当前持锁进程的 pid。
};

