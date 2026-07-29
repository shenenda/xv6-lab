#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/fcntl.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "user/user.h"

#define fail(msg) do {printf("FAILURE: " msg "\n"); failed = 1; goto done;} while (0);
static int failed = 0;

static void testsymlink(void);
static void concur(void);
static void cleanup(void);

int
main(int argc, char *argv[])
{
  cleanup();
  testsymlink();
  concur();
  exit(failed);
}

// 清理上一次运行遗留的目录、普通文件和符号链接。
static void
cleanup(void)
{
  unlink("/testsymlink/a");
  unlink("/testsymlink/b");
  unlink("/testsymlink/c");
  unlink("/testsymlink/1");
  unlink("/testsymlink/2");
  unlink("/testsymlink/3");
  unlink("/testsymlink/4");
  unlink("/testsymlink/z");
  unlink("/testsymlink/y");
  unlink("/testsymlink");
}

// 使用 O_NOFOLLOW 打开符号链接本身，再通过 fstat 检查链接 inode。
static int
stat_slink(char *pn, struct stat *st)
{
  int fd = open(pn, O_RDONLY | O_NOFOLLOW);
  if(fd < 0)
    return -1;
  if(fstat(fd, st) != 0)
    return -1;
  return 0;
}

// 验证链接跟随、悬空链接、循环检测以及多级链接解析。
static void
testsymlink(void)
{
  int r, fd1 = -1, fd2 = -1;
  char buf[4] = {'a', 'b', 'c', 'd'};
  char c = 0, c2 = 0;
  struct stat st;
    
  printf("Start: test symlinks\n");

  mkdir("/testsymlink");

  fd1 = open("/testsymlink/a", O_CREATE | O_RDWR);
  if(fd1 < 0) fail("failed to open a");

  r = symlink("/testsymlink/a", "/testsymlink/b");
  if(r < 0)
    fail("symlink b -> a failed");

  if(write(fd1, buf, sizeof(buf)) != 4)
    fail("failed to write to a");

  if (stat_slink("/testsymlink/b", &st) != 0)
    fail("failed to stat b");
  if(st.type != T_SYMLINK)
    fail("b isn't a symlink");

  fd2 = open("/testsymlink/b", O_RDWR);
  if(fd2 < 0)
    fail("failed to open b");
  read(fd2, &c, 1);
  if (c != 'a')
    fail("failed to read bytes from b");

  unlink("/testsymlink/a");
  if(open("/testsymlink/b", O_RDWR) >= 0)
    fail("Should not be able to open b after deleting a");

  // 此时 b 指向已删除的 a；再让 a 指向 b，构造 a 与 b 的解析环。
  r = symlink("/testsymlink/b", "/testsymlink/a");
  if(r < 0)
    fail("symlink a -> b failed");

  r = open("/testsymlink/b", O_RDWR);
  if(r >= 0)
    fail("Should not be able to open b (cycle b->a->b->..)\n");
  
  r = symlink("/testsymlink/nonexistent", "/testsymlink/c");
  if(r != 0)
    fail("Symlinking to nonexistent file should succeed\n");

  // 构造 1 -> 2 -> 3 -> 4 的链接链，检查递归解析是否到达最终文件。
  r = symlink("/testsymlink/2", "/testsymlink/1");
  if(r) fail("Failed to link 1->2");
  r = symlink("/testsymlink/3", "/testsymlink/2");
  if(r) fail("Failed to link 2->3");
  r = symlink("/testsymlink/4", "/testsymlink/3");
  if(r) fail("Failed to link 3->4");

  close(fd1);
  close(fd2);

  fd1 = open("/testsymlink/4", O_CREATE | O_RDWR);
  if(fd1<0) fail("Failed to create 4\n");
  fd2 = open("/testsymlink/1", O_RDWR);
  if(fd2<0) fail("Failed to open 1\n");

  c = '#';
  r = write(fd2, &c, 1);
  if(r!=1) fail("Failed to write to 1\n");
  r = read(fd1, &c2, 1);
  if(r!=1) fail("Failed to read from 4\n");
  if(c!=c2)
    fail("Value read from 4 differed from value written to 1\n");

  printf("test symlinks: ok\n");
done:
  close(fd1);
  close(fd2);
}

// 多进程并发创建、查询和删除同一路径，检查目录与 inode 操作的同步。
static void
concur(void)
{
  int pid, i;
  int fd;
  struct stat st;
  int nchild = 2;

  printf("Start: test concurrent symlinks\n");
    
  fd = open("/testsymlink/z", O_CREATE | O_RDWR);
  if(fd < 0) {
    printf("FAILED: open failed");
    exit(1);
  }
  close(fd);

  for(int j = 0; j < nchild; j++) {
    pid = fork();
    if(pid < 0){
      printf("FAILED: fork failed\n");
      exit(1);
    }
    if(pid == 0) {
      int m = 0;
      unsigned int x = (pid ? 1 : 97);
      for(i = 0; i < 100; i++){
        x = x * 1103515245 + 12345;
        if((x % 3) == 0) {
          symlink("/testsymlink/z", "/testsymlink/y");
          if (stat_slink("/testsymlink/y", &st) == 0) {
            m++;
            if(st.type != T_SYMLINK) {
              printf("FAILED: not a symbolic link\n", st.type);
              exit(1);
            }
          }
        } else {
          unlink("/testsymlink/y");
        }
      }
      exit(0);
    }
  }

  int r;
  for(int j = 0; j < nchild; j++) {
    wait(&r);
    if(r != 0) {
      printf("test concurrent symlinks: failed\n");
      exit(1);
    }
  }
  printf("test concurrent symlinks: ok\n");
}
