//
// 文件系统相关系统调用。用户参数不可信，因此这里主要负责参数检查，
// 再调用 file.c 和 fs.c 中的底层实现。
//

#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// 把第 n 个机器字宽参数解释为文件描述符，同时返回描述符编号及对应的 file 结构。
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// 为给定 file 分配进程文件描述符；成功后接管调用者持有的 file 引用。
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // 指向用户空间 struct stat 的指针。

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// 创建路径 new，使它与 old 指向同一个 inode。
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// 判断目录 dp 除 "." 和 ".." 外是否为空。
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // 不允许删除当前目录项 "." 或父目录项 ".."。
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // 创建 "." 和 ".." 目录项。
    dp->nlink++;  // ".." 会增加父目录的链接计数。
    iupdate(dp);
    // 不因 "." 增加 ip->nlink，避免形成自引用计数。
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

// 打开或创建路径，并把获得的 inode 包装成进程可见的文件描述符。
uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  // filealloc 与 fdalloc 分别占用全局文件表和进程描述符表；任一步失败
  // 都必须按已取得的资源逐项回滚，同时释放 inode 引用并结束事务。
  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  // 只有普通文件响应 O_TRUNC；目录和设备不能在 open 中被截断。
  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // 指向用户空间两个整数数组的指针。
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

// 通过虚拟地址查找当前所属的 VMA。
struct kama_vma*
findvma(struct proc *p, uint64 va)
{
  for(int i = 0; i < NVMA; i++){
    struct kama_vma *v = &p->vmas[i];
    if(v->valid && va >= v->vastart && va - v->vastart < v->sz)
      return v;
  }
  return 0;
}

// 建立文件映射。物理页暂不分配，第一次访问时由 vmaalloc() 装入。
uint64
sys_mmap(void)
{
  uint64 addr, sz, offset;
  int prot, flags, fd;
  struct file *f;

  if(argaddr(0, &addr) < 0 || argaddr(1, &sz) < 0 ||
     argint(2, &prot) < 0 || argint(3, &flags) < 0 ||
     argfd(4, &fd, &f) < 0 || argaddr(5, &offset) < 0 || sz == 0)
    return -1;

  // 本实验由内核选择地址，addr 只作为兼容 mmap 接口的提示参数。
  (void)addr;
  (void)fd;

  if((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0 ||
     (flags != MAP_SHARED && flags != MAP_PRIVATE) ||
     (offset % PGSIZE) != 0 || f->type != FD_INODE)
    return -1;

  // 读映射要求文件可读；共享可写映射最终要回写，要求文件可写。
  if((prot & PROT_READ) && !f->readable)
    return -1;
  if((prot & PROT_WRITE) && flags == MAP_SHARED && !f->writable)
    return -1;

  if(sz > MMAPEND || sz + PGSIZE - 1 < sz)
    return -1;
  sz = PGROUNDUP(sz);

  struct proc *p = myproc();
  struct kama_vma *freev = 0;
  uint64 vaend = MMAPEND;

  // 保存第一个空槽位，同时找到现有映射中的最低地址。
  for(int i = 0; i < NVMA; i++){
    struct kama_vma *v = &p->vmas[i];
    if(!v->valid){
      if(freev == 0)
        freev = v;
    } else if(v->vastart < vaend){
      vaend = PGROUNDDOWN(v->vastart);
    }
  }

  if(freev == 0 || vaend < sz)
    return -1;

  uint64 vastart = vaend - sz;
  // 不允许向下增长的 mmap 区与 text/data/heap/stack 区重叠。
  if(vastart < PGROUNDUP(p->sz))
    return -1;

  freev->vastart = vastart;
  freev->sz = sz;
  freev->f = filedup(f);
  freev->prot = prot;
  freev->flags = flags;
  freev->offset = offset;
  freev->valid = 1;
  return vastart;
}

// 为发生页故障的 VMA 地址分配物理页、读取文件内容并建立页表映射。
int
vmaalloc(uint64 va)
{
  struct proc *p = myproc();
  struct kama_vma *v = findvma(p, va);
  if(v == 0)
    return 0;

  uint64 pageva = PGROUNDDOWN(va);
  // 已映射页面再次故障通常意味着权限错误，不能覆盖原映射。
  if(walkaddr(p->pagetable, pageva) != 0)
    return 0;

  int perm = PTE_U;
  if(v->prot & PROT_READ)
    perm |= PTE_R;
  if(v->prot & PROT_WRITE)
    // Sv39 不允许“可写但不可读”的叶子 PTE，因此写权限同时带上读权限。
    perm |= PTE_W | PTE_R;
  if(v->prot & PROT_EXEC)
    perm |= PTE_X;
  if((perm & (PTE_R | PTE_W | PTE_X)) == 0)
    return 0;

  char *pa = kalloc();
  if(pa == 0)
    return 0;
  memset(pa, 0, PGSIZE);

  int nread;
  begin_op();
  ilock(v->f->ip);
  nread = readi(v->f->ip, 0, (uint64)pa,
                v->offset + (pageva - v->vastart), PGSIZE);
  iunlock(v->f->ip);
  end_op();
  if(nread < 0){
    kfree(pa);
    return 0;
  }

  if(mappages(p->pagetable, pageva, PGSIZE, (uint64)pa, perm) < 0){
    kfree(pa);
    return 0;
  }
  return 1;
}

// 取消一段位于 VMA 首部、尾部或整个 VMA 的映射。
uint64
sys_munmap(void)
{
  uint64 addr, sz;
  if(argaddr(0, &addr) < 0 || argaddr(1, &sz) < 0 ||
     sz == 0 || (addr % PGSIZE) != 0 || sz + PGSIZE - 1 < sz)
    return -1;

  sz = PGROUNDUP(sz);
  if(addr + sz < addr)
    return -1;

  struct proc *p = myproc();
  struct kama_vma *v = findvma(p, addr);
  if(v == 0)
    return -1;

  uint64 vend = v->vastart + v->sz;
  uint64 unmapend = addr + sz;
  if(unmapend > vend)
    return -1;
  // 实验只要求从首部或尾部收缩，不允许在 VMA 中间打洞。
  if(addr > v->vastart && unmapend < vend)
    return -1;

  vmaunmap(p->pagetable, addr, sz, v);

  if(addr == v->vastart && unmapend == vend){
    struct file *f = v->f;
    memset(v, 0, sizeof(*v));
    fileclose(f);
  } else if(addr == v->vastart){
    v->vastart = unmapend;
    v->offset += sz;
    v->sz -= sz;
  } else {
    v->sz = addr - v->vastart;
  }
  return 0;
}

// 释放进程的全部 VMA。调用者不得持有自旋锁，因为回写和 fileclose 可能睡眠。
void
vmafree(struct proc *p, pagetable_t pagetable)
{
  for(int i = 0; i < NVMA; i++){
    struct kama_vma *v = &p->vmas[i];
    if(!v->valid)
      continue;
    vmaunmap(pagetable, v->vastart, v->sz, v);
    struct file *f = v->f;
    memset(v, 0, sizeof(*v));
    fileclose(f);
  }
}
