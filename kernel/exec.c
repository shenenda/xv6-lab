#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

static int loadseg(pde_t *pgdir, uint64 addr, struct inode *ip, uint offset, uint sz);

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint64 argc, sz = 0, sp, ustack[MAXARG+1], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc();

  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);

  // 检查 ELF 文件头，拒绝短读或魔数不匹配的可执行文件。
  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pagetable = proc_pagetable(p)) == 0)
    goto bad;

  // 逐个读取程序头，只把 ELF 中标记为可装载的段映射并读入内存。
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    // 加法结果回绕说明虚拟地址范围溢出，不能继续创建映射。
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    uint64 sz1;
    if((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    //防止程序内存大小超过PLIC
    if(sz1 >= PLIC)
      goto bad;

    sz = sz1;
    // 检查 ELF 段虚拟地址页对齐
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    // loadseg() 把文件内容真正读进物理内存
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  p = myproc();
  uint64 oldsz = p->sz;

  // 在下一个页边界处分配两页；第一页清除用户权限作为栈保护页，
  // 第二页才作为真正的用户栈。
  sz = PGROUNDUP(sz);
  uint64 sz1;
  if((sz1 = uvmalloc(pagetable, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  sz = sz1;
  uvmclear(pagetable, sz-2*PGSIZE);
  sp = sz;
  stackbase = sp - PGSIZE;

  // 从高地址向低地址压入参数字符串，并在 ustack 中暂存各字符串的用户地址。
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16; // RISC-V 调用约定要求栈指针按 16 字节对齐。
    if(sp < stackbase)
      goto bad;
    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp;
  }
  ustack[argc] = 0;

  // 再把 argv[] 指针数组整体复制到用户栈，末尾的 0 作为空指针哨兵。
  sp -= (argc+1) * sizeof(uint64);
  sp -= sp % 16;
  if(sp < stackbase)
    goto bad;
  if(copyout(pagetable, sp, (char *)ustack, (argc+1)*sizeof(uint64)) < 0)
    goto bad;

  // 用户态 main(argc, argv) 的第二个参数通过 a1 传递。
  // argc 由 exec 的系统调用返回值送入 a0，因此这里不直接写 a0。
  p->trapframe->a1 = sp;

  // 只保存路径最后一个分量作为进程名，便于调试和进程信息输出。
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(p->name, last, sizeof(p->name));

  //清除内核页表中对程序内存的旧映射，然后重新建立映射
  uvmunmap(p->kama_kernelpgtbl, 0, PGROUNDUP(oldsz)/PGSIZE, 0);
  kama_kvmcopymappings(pagetable, p->kama_kernelpgtbl, 0, sz);

  // 到这里新地址空间已经完整构造成功，再原子式替换进程的旧用户映像。
  oldpagetable = p->pagetable; //保存旧页表
  p->pagetable = pagetable; //安装新页表
  p->sz = sz; //更新地址空间大小
  p->trapframe->epc = elf.entry;  // 设置新程序入口。用户程序首次返回时使用的初始程序计数器。
  p->trapframe->sp = sp; // 用户程序的初始栈指针。
  proc_freepagetable(oldpagetable, oldsz); //释放旧页表
  // 新程序的页表已经完整构造并安装成功；
  // 当前还处于内核态，可以安全地打印它。
  if(p->pid == 1) kama_vmprint(p->pagetable);

  return argc; // 系统调用返回值最终进入 a0，也就是 main(argc, argv) 的第一个参数。

 bad:
  if(pagetable)
    proc_freepagetable(pagetable, sz);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}

// 把程序的一段内容装入 pagetable 中从虚拟地址 va 开始的区域。
// va 必须按页对齐，而且 [va, va+sz) 对应的页面必须已经建立映射。
// 成功返回 0，失败返回 -1。
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
  uint i, n;
  uint64 pa;

  if((va % PGSIZE) != 0)
    panic("loadseg: va must be page aligned");

  // 每次最多读取一页；最后一次读取不足一页时使用剩余字节数。
  for(i = 0; i < sz; i += PGSIZE){
    pa = walkaddr(pagetable, va + i);
    if(pa == 0)
      panic("loadseg: address should exist");
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, 0, (uint64)pa, offset+i, n) != n)
      return -1;
  }

  return 0;
}
