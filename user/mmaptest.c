#include "kernel/param.h"
#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/fs.h"
#include "user/user.h"

void mmap_test();
void fork_test();
char buf[BSIZE];

#define MAP_FAILED ((char *) -1)

int
main(int argc, char *argv[])
{
  mmap_test();
  fork_test();
  printf("mmaptest: all tests succeeded\n");
  exit(0);
}

char *testname = "???";

void
err(char *why)
{
  printf("mmaptest: %s failed: %s, pid=%d\n", testname, why, getpid());
  exit(1);
}

// 检查两页映射内容：文件内的 1.5 页应为 A，超出文件末尾的半页应补零。
void
_v1(char *p)
{
  int i;
  for (i = 0; i < PGSIZE*2; i++) {
    if (i < PGSIZE + (PGSIZE/2)) {
      if (p[i] != 'A') {
        printf("mismatch at %d, wanted 'A', got 0x%x\n", i, p[i]);
        err("v1 mismatch (1)");
      }
    } else {
      if (p[i] != 0) {
        printf("mismatch at %d, wanted zero, got 0x%x\n", i, p[i]);
        err("v1 mismatch (2)");
      }
    }
  }
}

// 创建待映射文件：实际写入 1.5 页 A，映射到两页后剩余半页应按零填充。
void
makefile(const char *f)
{
  int i;
  int n = PGSIZE/BSIZE;

  unlink(f);
  int fd = open(f, O_WRONLY | O_CREATE);
  if (fd == -1)
    err("open");
  memset(buf, 'A', BSIZE);
  // 按磁盘块写入一页半数据
  for (i = 0; i < n + n/2; i++) {
    if (write(fd, buf, BSIZE) != BSIZE)
      err("write 0 makefile");
  }
  if (close(fd) == -1)
    err("close");
}

void
mmap_test(void)
{
  int fd;
  int i;
  const char * const f = "mmap.dur";
  printf("mmap_test starting\n");
  testname = "mmap_test";

  // 创建内容已知的文件并映射到内存，检查映射字节与原始文件一致。
  makefile(f);
  if ((fd = open(f, O_RDONLY)) == -1)
    err("open");

  printf("test mmap f\n");
  // mmap(0, length, prot, flags, fd, offset) 请求内核把 fd 对应文件从 offset
  // 开始的 length 字节映射进当前地址空间。首参数为 0，表示虚拟地址由内核选择；
  // PROT_READ 只允许读取；MAP_PRIVATE 表示修改不会写回文件，也不会被其他映射
  // 同一文件的进程看到（本例本身只读，因此不能实际修改）。
  char *p = mmap(0, PGSIZE*2, PROT_READ, MAP_PRIVATE, fd, 0);
  if (p == MAP_FAILED)
    err("mmap (1)");
  _v1(p);
  if (munmap(p, PGSIZE*2) == -1)
    err("munmap (1)");

  printf("test mmap f: OK\n");
    
  printf("test mmap private\n");
  // 私有可写映射采用写时私有语义，因此允许映射一个只读打开的文件。
  p = mmap(0, PGSIZE*2, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  if (p == MAP_FAILED)
    err("mmap (2)");
  if (close(fd) == -1)
    err("close");
  _v1(p);
  for (i = 0; i < PGSIZE*2; i++)
    p[i] = 'Z';
  if (munmap(p, PGSIZE*2) == -1)
    err("munmap (2)");

  printf("test mmap private: OK\n");
    
  printf("test mmap read-only\n");
    
  // 共享可写映射可能回写文件，所以不能基于只读文件描述符创建。
  if ((fd = open(f, O_RDONLY)) == -1)
    err("open");
  p = mmap(0, PGSIZE*3, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (p != MAP_FAILED)
    err("mmap call should have failed");
  if (close(fd) == -1)
    err("close");

  printf("test mmap read-only: OK\n");
    
  printf("test mmap read/write\n");
  
  // 文件以读写方式打开后，应允许建立共享可读写映射。
  if ((fd = open(f, O_RDWR)) == -1)
    err("open");
  p = mmap(0, PGSIZE*3, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (p == MAP_FAILED)
    err("mmap (3)");
  if (close(fd) == -1)
    err("close");

  // close(fd) 后映射仍须有效，因为内核应为映射单独持有文件引用。
  _v1(p);

  // 修改共享映射，产生需要在解除映射时回写的脏页。
  for (i = 0; i < PGSIZE*2; i++)
    p[i] = 'Z';

  // 只解除三页映射中的前两页，验证部分 munmap。
  if (munmap(p, PGSIZE*2) == -1)
    err("munmap (3)");
  
  printf("test mmap read/write: OK\n");
  
  printf("test mmap dirty\n");
  
  // 重新读取文件，检查共享映射中的修改是否已经写回。
  if ((fd = open(f, O_RDWR)) == -1)
    err("open");
  for (i = 0; i < PGSIZE + (PGSIZE/2); i++){
    char b;
    if (read(fd, &b, 1) != 1)
      err("read (1)");
    if (b != 'Z')
      err("file does not contain modifications");
  }
  if (close(fd) == -1)
    err("close");

  printf("test mmap dirty: OK\n");

  printf("test not-mapped unmap\n");
  
  // 解除先前保留下来的最后一页映射。
  if (munmap(p+PGSIZE*2, PGSIZE) == -1)
    err("munmap (4)");

  printf("test not-mapped unmap: OK\n");
    
  printf("test mmap two files\n");
  
  // 同时映射两个文件，验证不同映射区域及其文件引用互不干扰。
  int fd1;
  if((fd1 = open("mmap1", O_RDWR|O_CREATE)) < 0)
    err("open mmap1");
  if(write(fd1, "12345", 5) != 5)
    err("write mmap1");
  char *p1 = mmap(0, PGSIZE, PROT_READ, MAP_PRIVATE, fd1, 0);
  if(p1 == MAP_FAILED)
    err("mmap mmap1");
  close(fd1);
  unlink("mmap1");

  int fd2;
  if((fd2 = open("mmap2", O_RDWR|O_CREATE)) < 0)
    err("open mmap2");
  if(write(fd2, "67890", 5) != 5)
    err("write mmap2");
  char *p2 = mmap(0, PGSIZE, PROT_READ, MAP_PRIVATE, fd2, 0);
  if(p2 == MAP_FAILED)
    err("mmap mmap2");
  close(fd2);
  unlink("mmap2");

  if(memcmp(p1, "12345", 5) != 0)
    err("mmap1 mismatch");
  if(memcmp(p2, "67890", 5) != 0)
    err("mmap2 mismatch");

  munmap(p1, PGSIZE);
  if(memcmp(p2, "67890", 5) != 0)
    err("mmap2 mismatch (2)");
  munmap(p2, PGSIZE);
  
  printf("test mmap two files: OK\n");
  
  printf("mmap_test: ALL OK\n");
}

// 映射文件后再 fork，检查子进程是否继承映射，且父子进程各自解除映射
// 不会破坏另一进程仍在使用的映射。
void
fork_test(void)
{
  int fd;
  int pid;
  const char * const f = "mmap.dur";
  
  printf("fork_test starting\n");
  testname = "fork_test";
  
  // 把同一文件映射两次，检查两份 VMA 能独立维护。
  makefile(f);
  if ((fd = open(f, O_RDONLY)) == -1)
    err("open");
  unlink(f);
  char *p1 = mmap(0, PGSIZE*2, PROT_READ, MAP_SHARED, fd, 0);
  if (p1 == MAP_FAILED)
    err("mmap (4)");
  char *p2 = mmap(0, PGSIZE*2, PROT_READ, MAP_SHARED, fd, 0);
  if (p2 == MAP_FAILED)
    err("mmap (5)");

  // 只访问第二页，用来验证按页延迟装入而非一次性读入整个区域。
  if(*(p1+PGSIZE) != 'A')
    err("fork mismatch (1)");

  if((pid = fork()) < 0)
    err("fork");
  if (pid == 0) {
    _v1(p1);
    munmap(p1, PGSIZE); // 子进程只解除第一份映射的第一页
    exit(0); // 用退出状态通知父进程：子进程继承的映射工作正常
  }

  int status = -1;
  wait(&status);

  if(status != 0){
    printf("fork_test failed\n");
    exit(1);
  }

  // 子进程退出并解除部分映射后，父进程的两份映射仍应完整有效。
  _v1(p1);
  _v1(p2);

  printf("fork_test OK\n");
}

