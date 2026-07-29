#ifdef LAB_FS
#define NPROC        10  // 最大进程数
#else
#define NPROC        64  // 最大进程数（增大该值可加速 bigfile 测试）
#endif
#define NCPU          8  // 最大 CPU 数
#define NOFILE       16  // 每个进程最多打开的文件数
#define NFILE       100  // 整个系统最多打开的文件数
#define NINODE       50  // 内存中最多同时存在的活跃 inode 数
#define NDEV         10  // 最大主设备号
#define ROOTDEV       1  // 文件系统根磁盘的设备号
#define MAXARG       32  // exec 最多接收的参数个数
#define MAXOPBLOCKS  10  // 单次文件系统操作最多写入的块数
#define LOGSIZE      (MAXOPBLOCKS*3)  // 磁盘日志最多容纳的数据块数
#define NBUF         (MAXOPBLOCKS*3)  // 磁盘块缓存中的缓冲区数量
#define FSSIZE       200000  // 文件系统包含的块数
#define MAXPATH      128   // 文件路径的最大长度


