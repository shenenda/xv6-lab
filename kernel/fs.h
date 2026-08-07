// 磁盘文件系统格式。
// 内核与用户程序共同使用这个头文件。


#define ROOTINO  1   // 根目录的 inode 编号
#define BSIZE 1024  // 磁盘块大小

// 磁盘布局：
// [ 引导块 | 超级块 | 日志 | inode 块 |
//                                          空闲位图 | 数据块 ]
//
// mkfs 负责计算超级块并创建初始文件系统；
// 超级块用下列字段描述磁盘布局：
struct superblock {
  uint magic;        // 必须等于 FSMAGIC
  uint size;         // 文件系统镜像总块数
  uint nblocks;      // 数据块数量
  uint ninodes;      // inode 数量
  uint nlog;         // 日志块数量
  uint logstart;     // 第一个日志块的块号
  uint inodestart;   // 第一个 inode 块的块号
  uint bmapstart;    // 第一个空闲位图块的块号
};

#define FSMAGIC 0x10203040

#define NDIRECT 11
#define NINDIRECT (BSIZE / sizeof(uint))
// 一个 inode 可寻址 11 个直接块、256 个一级间接块和 256*256 个二级间接块。
#define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT * NINDIRECT)

// 磁盘上的 inode 结构
struct dinode {
  short type;           // 文件类型
  short major;          // 主设备号（仅 T_DEVICE 使用）
  short minor;          // 次设备号（仅 T_DEVICE 使用）
  short nlink;          // 文件系统中指向该 inode 的硬链接数
  uint size;            // 文件大小（字节）
  uint addrs[NDIRECT+2];   // 0~10：直接索引，11：一级间接索引，12：二级间接索引
};

// 每个磁盘块可容纳的 inode 数。
#define IPB           (BSIZE / sizeof(struct dinode))

// 包含 inode i 的磁盘块
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// 每个位图块包含的位数
#define BPB           (BSIZE*8)

// 记录磁盘块 b 状态的空闲位图块
#define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)

// 目录本质上是由一系列 dirent 结构组成的特殊文件。
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

