#define T_DIR     1   // 目录
#define T_FILE    2   // 普通文件
#define T_DEVICE  3   // 设备
#define T_SYMLINK 4   // 符号链接

struct stat {
  int dev;     // 文件系统所在的磁盘设备
  uint ino;    // inode 编号
  short type;  // 文件类型
  short nlink; // 指向文件的硬链接数
  uint64 size; // 文件大小（字节）
};
