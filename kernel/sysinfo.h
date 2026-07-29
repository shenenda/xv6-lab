struct sysinfo {
  uint64 freemem;   // 当前空闲物理内存字节数。
  uint64 nproc;     // 当前非 UNUSED 状态的进程数量。
};
