//
// 写时复制 fork() 实验测试。
//

#include "kernel/types.h"
#include "kernel/memlayout.h"
#include "user/user.h"

// 先分配超过物理内存一半的空间再 fork。普通整页复制会因内存不足失败，
// 只有共享只读页面并延迟复制的 COW 实现才能通过。
void
simpletest()
{
  uint64 phys_size = PHYSTOP - KERNBASE;
  // 选择约三分之二物理内存，确保父子各复制一份时总量会超过可用内存。
  int sz = (phys_size / 3) * 2;

  printf("simple: ");
  
  char *p = sbrk(sz);
  if(p == (char*)0xffffffffffffffffL){
    printf("sbrk(%d) failed\n", sz);
    exit(-1);
  }

  // 每页写一次，确保父进程的页面都已真实分配。
  // 父进程覆盖整段；其写入结果不应被两个后代进程的写操作污染。
  for(char *q = p; q < p + sz; q += 4096){
    *(int*)q = getpid();
  }

  int pid = fork();
  if(pid < 0){
    printf("fork() failed\n");
    exit(-1);
  }

  if(pid == 0)
    exit(0);

  wait(0);

  if(sbrk(-sz) == (char*)0xffffffffffffffffL){
    printf("sbrk(-%d) failed\n", sz);
    exit(-1);
  }

  printf("ok\n");
}

// 三个进程分别写入共享的 COW 内存，迫使部分页面发生私有复制。
// 总分配量超过物理内存一半，因此也能检查复制页是否在进程退出后及时释放。
void
threetest()
{
  uint64 phys_size = PHYSTOP - KERNBASE;
  int sz = phys_size / 4;
  int pid1, pid2;

  printf("three: ");
  
  char *p = sbrk(sz);
  if(p == (char*)0xffffffffffffffffL){
    printf("sbrk(%d) failed\n", sz);
    exit(-1);
  }

  pid1 = fork();
  if(pid1 < 0){
    printf("fork failed\n");
    exit(-1);
  }
  if(pid1 == 0){
    pid2 = fork();
    if(pid2 < 0){
      printf("fork failed");
      exit(-1);
    }
    if(pid2 == 0){
      // 孙进程写前五分之四区域，每次写都应触发独立的 COW 页面复制。
      for(char *q = p; q < p + (sz/5)*4; q += 4096){
        *(int*)q = getpid();
      }
      // 孙进程写前五分之四区域，每次写都应触发独立的 COW 页面复制。
      for(char *q = p; q < p + (sz/5)*4; q += 4096){
        if(*(int*)q != getpid()){
          printf("wrong content\n");
          exit(-1);
        }
      }
      exit(-1);
    }
    // 子进程只覆盖前一半，用不同值检查各进程物理页是否真正隔离。
    for(char *q = p; q < p + (sz/2); q += 4096){
      *(int*)q = 9999;
    }
    exit(0);
  }

  // 每页写一次，确保父进程的页面都已真实分配。
  // 父进程覆盖整段；其写入结果不应被两个后代进程的写操作污染。
  for(char *q = p; q < p + sz; q += 4096){
    *(int*)q = getpid();
  }

  wait(0);

  sleep(1);

  // 每页写一次，确保父进程的页面都已真实分配。
  for(char *q = p; q < p + sz; q += 4096){
    if(*(int*)q != getpid()){
      printf("wrong content\n");
      exit(-1);
    }
  }

  if(sbrk(-sz) == (char*)0xffffffffffffffffL){
    printf("sbrk(-%d) failed\n", sz);
    exit(-1);
  }

  printf("ok\n");
}

char junk1[4096];
int fds[2];
char junk2[4096];
char buf[4096];
char junk3[4096];

// 测试 copyout() 向 COW 页面写数据时，能否主动完成与写页错误相同的复制流程。
void
filetest()
{
  printf("file: ");
  
  buf[0] = 99;

  for(int i = 0; i < 4; i++){
    if(pipe(fds) != 0){
      printf("pipe() failed\n");
      exit(-1);
    }
    int pid = fork();
    if(pid < 0){
      printf("fork failed\n");
      exit(-1);
    }
    if(pid == 0){
      sleep(1);
      // read 最终通过 copyout 写入子进程的 buf；buf 若仍共享，就必须先拆分 COW 页。
      if(read(fds[0], buf, sizeof(i)) != sizeof(i)){
        printf("error: read failed\n");
        exit(1);
      }
      sleep(1);
      int j = *(int*)buf;
      if(j != i){
        printf("error: read the wrong value\n");
        exit(1);
      }
      exit(0);
    }
    if(write(fds[1], &i, sizeof(i)) != sizeof(i)){
      printf("error: write failed\n");
      exit(-1);
    }
  }

  int xstatus = 0;
  for(int i = 0; i < 4; i++) {
    wait(&xstatus);
    if(xstatus != 0) {
      exit(1);
    }
  }

  // 子进程通过 copyout 修改自己的 buf 后，父进程原值必须保持不变。
  if(buf[0] != 99){
    printf("error: child overwrote parent\n");
    exit(1);
  }

  printf("ok\n");
}

int
main(int argc, char *argv[])
{
  simpletest();

  // 再运行一次，确认第一次 simpletest() 占用的物理页已经全部释放。
  simpletest();

  threetest();
  threetest();
  threetest();

  filetest();

  printf("ALL COW TESTS PASSED\n");

  exit(0);
}
