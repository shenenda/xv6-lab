#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"

// 申请 1 GiB 虚拟地址范围，但只稀疏触碰少量页面，用来验证惰性物理页分配。
#define REGION_SZ (1024 * 1024 * 1024)

// 大幅扩展地址空间后每隔 64 页写入一次，检查稀疏页错误都能按需分配。
void
sparse_memory(char *s)
{
  char *i, *prev_end, *new_end;
  
  prev_end = sbrk(REGION_SZ);
  if (prev_end == (char*)0xffffffffffffffffL) {
    printf("sbrk() failed\n");
    exit(1);
  }
  new_end = prev_end + REGION_SZ;

  // 把每个被触碰地址自身写入该地址，随后可同时验证映射与数据内容。
  for (i = prev_end + PGSIZE; i < new_end; i += 64 * PGSIZE)
    *(char **)i = i;

  for (i = prev_end + PGSIZE; i < new_end; i += 64 * PGSIZE) {
    if (*(char **)i != i) {
      printf("failed to read value from memory\n");
      exit(1);
    }
  }

  exit(0);
}

// 子进程收缩地址空间后再访问原页面应被内核终止，验证惰性页也能正确解除映射。
void
sparse_memory_unmap(char *s)
{
  int pid;
  char *i, *prev_end, *new_end;

  prev_end = sbrk(REGION_SZ);
  if (prev_end == (char*)0xffffffffffffffffL) {
    printf("sbrk() failed\n");
    exit(1);
  }
  new_end = prev_end + REGION_SZ;

  for (i = prev_end + PGSIZE; i < new_end; i += PGSIZE * PGSIZE)
    *(char **)i = i;

  for (i = prev_end + PGSIZE; i < new_end; i += PGSIZE * PGSIZE) {
    pid = fork();
    if (pid < 0) {
      printf("error forking\n");
      exit(1);
    } else if (pid == 0) {
      // 一次归还整段虚拟地址空间，随后访问 i 应触发非法页错误。
      sbrk(-1L * REGION_SZ);
      *(char **)i = i;
      exit(0);
    } else {
      int status;
      wait(&status);
      if (status == 0) {
        printf("memory not unmapped\n");
        exit(1);
      }
    }
  }

  exit(0);
}

// 持续分配并触碰大块内存，检查物理内存耗尽时内核不会崩溃或泄漏。
void
oom(char *s)
{
  void *m1, *m2;
  int pid;

  if((pid = fork()) == 0){
    m1 = 0;
    while((m2 = malloc(4096*4096)) != 0){
      // 写入每块内存以迫使惰性分配真正取得物理页，并串成链防止被忽略。
      *(char**)m2 = m1;
      m1 = m2;
    }
    exit(0);
  } else {
    int xstatus;
    wait(&xstatus);
    exit(xstatus == 0);
  }
}

// 每项测试都在独立子进程中运行；子进程退出状态表示成功时，run 返回 1。
int
run(void f(char *), char *s) {
  int pid;
  int xstatus;
  
  printf("running test %s\n", s);
  if((pid = fork()) < 0) {
    printf("runtest: fork error\n");
    exit(1);
  }
  if(pid == 0) {
    f(s);
    exit(0);
  } else {
    wait(&xstatus);
    if(xstatus != 0) 
      printf("test %s: FAILED\n", s);
    else
      printf("test %s: OK\n", s);
    return xstatus == 0;
  }
}

int
main(int argc, char *argv[])
{
  char *n = 0;
  if(argc > 1) {
    n = argv[1];
  }
  
  // 函数指针 f 与测试名配对，使命令行既可选择单项也可顺序执行全部测试。
  struct test {
    void (*f)(char *);
    char *s;
  } tests[] = {
    { sparse_memory, "lazy alloc"},
    { sparse_memory_unmap, "lazy unmap"},
    { oom, "out of memory"},
    { 0, 0},
  };
    
  printf("lazytests starting\n");

  int fail = 0;
  for (struct test *t = tests; t->s != 0; t++) {
    if((n == 0) || strcmp(t->s, n) == 0) {
      if(!run(t->f, t->s))
        fail = 1;
    }
  }
  if(!fail)
    printf("ALL TESTS PASSED\n");
  else
    printf("SOME TESTS FAILED\n");
  exit(1);   // exit 不会返回到这里。
}
