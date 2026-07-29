// 自旋互斥锁。
struct spinlock {
  uint locked;       // 锁是否已被持有。

  // 以下字段仅用于调试和持锁检查：
  char *name;        // 锁的名称。
  struct cpu *cpu;   // 当前持有该锁的 CPU。
#ifdef LAB_LOCK
  int nts;
  int n;
#endif
};

