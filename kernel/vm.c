#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"

/*
 * 内核页表。
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld 将其设为内核代码的结束地址。

extern char trampoline[]; // trampoline.S 中定义的跳板页代码。

/*
 * 为内核创建直接映射页表，虚拟地址与物理地址相同。
 */
void
kvminit()
{
  kernel_pagetable = (pagetable_t) kalloc();
  // 根页表本身占一页物理内存；清零后所有 PTE 初始都处于无效状态。
  memset(kernel_pagetable, 0, PGSIZE);

  // 映射 UART 寄存器。kvmmap()是在内核页表中建立一段虚拟地址到物理地址的映射
  kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // 映射 VirtIO 磁盘的 MMIO 接口。 前两个参数分别是虚拟地址和物理地址，相同代表是直接映射
  kvmmap(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // 映射核间中断与定时器使用的 CLINT。
  kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // 映射管理外部设备中断的 PLIC。
  kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // 内核代码段只读、可执行。
  kvmmap(KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // 内核数据段以及可用物理内存可读、可写。
  kvmmap((uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // 将陷阱进入与返回使用的跳板页映射到内核最高虚拟地址。
  kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

// 将硬件页表寄存器切换为内核页表，并开启分页。
void
kvminithart()
{
  // MAKE_SATP 将根页表物理地址编码成 satp 寄存器要求的格式。
  w_satp(MAKE_SATP(kernel_pagetable));
  // 切换页表后刷新 TLB，防止继续使用旧的地址转换结果。
  sfence_vma();
}

// 返回页表 pagetable 中虚拟地址 va 对应的最低级 PTE 地址。
// 如果 alloc != 0，则在遍历过程中按需创建缺失的页表页。
//
// RISC-V Sv39 使用三级页表，每个页表页包含 512 个 64 位 PTE。
// 本实现使用的 64 位虚拟地址分为以下字段：
//   39..63 -- 必须为 0。
//   30..38 -- 9 位二级页表索引。
//   21..29 -- 9 位一级页表索引。
//   12..20 -- 9 位零级页表索引。
//    0..11 -- 12 位页内偏移。
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  // 从二级根页表逐级向下查找，最后返回零级页表项。
  for(int level = 2; level > 0; level--) {
    // PX 根据 level 提取 va 中对应的 9 位索引，再取出该索引位置的 PTE 地址。
    pte_t *pte = &pagetable[PX(level, va)];
    // 按位与用于检测 PTE_V 有效位是否被置位。
    if(*pte & PTE_V) {
      // 中间级 PTE 保存的是下一级页表的物理地址。
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      // 页表缺失且允许分配时，新建并清空下一层页表。
      // kalloc 返回一页物理内存，转换后将其作为包含 512 个 PTE 的页表使用。
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      // PA2PTE 将物理地址放入 PTE 的地址字段，再用按位或设置有效位。
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// 查询虚拟地址对应页面的物理起始地址，未映射时返回 0。
// 该函数只能用于查询用户页面。
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  // 只有有效且允许用户态访问的页表项才能用于地址转换。
  // alloc 传 0 表示只查询，缺少中间页表时不会进行分配。
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  // PTE_V 未置位说明该 PTE 当前无效。
  if((*pte & PTE_V) == 0)
    return 0;
  // PTE_U 未置位说明该页面只允许内核态访问。
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// 向内核页表中添加映射，仅在启动阶段使用。
// 本函数不会刷新 TLB，也不会开启分页。
void
kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kernel_pagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// 将内核虚拟地址转换为物理地址，目前只用于内核栈地址。
uint64
kvmpa(uint64 va)
{
  // 对页大小取模得到 va 在页面内部的字节偏移。
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  
  pte = walk(kernel_pagetable, va, 0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  // 页表项只保存物理页起始地址，返回时需要补回原来的页内偏移。
  return pa+off;
}

// 为从 va 开始的虚拟地址创建 PTE，使其映射到从 pa 开始的物理地址。
// va 和 size 可以不按页对齐。成功返回 0，页表页分配失败返回 -1。
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  // 首尾地址向下取整后，循环即可覆盖这段范围涉及的所有页面。
  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    // alloc 传 1，查找过程中缺少中间页表时允许 walk 自动创建。
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    // 已经有效的 PTE 不能被静默覆盖，否则原映射会丢失。
    if(*pte & PTE_V)
      panic("remap");
    // 将物理地址、调用者给出的权限位和有效位组合成一个叶子 PTE。
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    // 虚拟地址和物理地址同步前进一页，保持连续映射关系。
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// 从 va 开始移除 npages 个页面映射，va 必须按页对齐且映射必须存在。
// do_free 非零时，同时释放映射的物理页。
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  // 地址对页大小取模不为 0，说明起始地址没有按页边界对齐。
  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    // 这里只查找已有映射，所以 alloc 传 0，禁止创建新页表。
    // 惰性分配允许合法地址范围内存在尚未建立 PTE 的空洞。
    if((pte = walk(pagetable, a, 0)) == 0)
      continue;
    if((*pte & PTE_V) == 0)
      continue;
    // 只有叶子 PTE 才代表实际页面；仅含 PTE_V 的项指向下一级页表。
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      // PTE2PA 去掉权限标志并还原出页对齐的物理地址。
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    // 清零 PTE 即可解除映射；调用者负责在需要时刷新 TLB。
    *pte = 0;
  }
}

// 创建空的用户页表，内存不足时返回 0。
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  // pagetable_t 本质上是指向一页 PTE 数组的指针。
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  // 新页表必须清零，否则随机比特可能被误认为有效映射或权限位。
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// 将第一个进程的用户初始化代码加载到页表的虚拟地址 0。
// 初始化代码大小 sz 必须小于一页。
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  // 先建立一页可供用户态读、写、执行的映射，再复制初始化代码。
  memset(mem, 0, PGSIZE);
  // 多个权限宏通过按位或组合到同一个 PTE 中，PTE_U 表示用户态可访问。
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  // mem 是刚分配物理页的内核地址，src 中的 initcode 被复制到页首。
  memmove(mem, src, sz);
}

// 分配 PTE 和物理内存，将进程从 oldsz 扩展到 newsz。
// newsz 不要求按页对齐，成功返回新大小，失败返回 0。
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  // 本函数只负责扩容；目标更小时保持原大小不变。
  if(newsz < oldsz)
    return oldsz;

  // 从 oldsz 后的第一个完整页边界开始分配，避免重复映射已有页面。
  oldsz = PGROUNDUP(oldsz);
  // 逐页分配；任一步失败都回收本次已经扩展的部分。
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      // a 表示已经成功扩展到的位置，据此只回收本轮新增页面。
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      // 当前物理页尚未成功映射，需要单独释放，再回滚之前的页面。
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// 释放用户页面，将进程大小从 oldsz 缩减到 newsz。
// 两个大小都不要求按页对齐，newsz 也可以不小于 oldsz。
// oldsz 可以大于进程实际大小，函数最终返回新的进程大小。
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    // 只解除 newsz 之后完整页面的映射，保留其所在的最后一个不完整页。
    // 两个向上取整后的地址之差除以页大小，就得到需要释放的页数。
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// 递归释放各级页表页，调用前必须已经移除所有叶子映射。
void
freewalk(pagetable_t pagetable)
{
  // 一个页表页含有 2^9 = 512 个 PTE。
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    // 有效位为 1 且 R/W/X 均为 0 时，该项是中间级 PTE，不是叶子映射。
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // 有效但没有读、写、执行权限，说明该 PTE 指向下一级页表。
      uint64 child = PTE2PA(pte);
      // 深度优先释放子页表，保证父页表释放前不再引用下一级页表。
      freewalk((pagetable_t)child);
      // 子页表已经释放，清除父页表中的悬空项。
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      // 本函数只释放页表页，遇到叶子映射说明调用顺序错误。
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// 先释放用户物理内存页，再递归释放页表页。
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    // 向上取整后除以页大小，将字节数转换为需要释放的完整页数。
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// 根据父进程页表，将其内存复制到子进程页表。
// 页表结构和物理页内容都会被深拷贝。
// 成功返回 0，失败返回 -1，并释放本次已经分配的页面。
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  // 逐页复制内容，并沿用父进程原有的 PTE 权限建立子进程映射。
  for(i = 0; i < sz; i += PGSIZE){
    // 父进程可能只声明了这段地址，而从未触碰对应的惰性页。
    // 子进程继承同一个虚拟地址空洞即可，不应为它提前分配物理页。
    if((pte = walk(old, i, 0)) == 0)
      continue;
    if((*pte & PTE_V) == 0)
      continue;
    pa = PTE2PA(*pte);
    // 只提取权限与状态位，随后让子进程映射继承相同属性。
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    // 复制整个物理页，因此父子进程之后修改各自内存不会互相影响。
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      // 当前页映射失败时先释放它，再由 err 回收之前已映射的页面。
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  // i 之前的页面已经复制成功，失败时统一解除映射并释放物理页。
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// 清除 PTE_U，禁止用户态访问该页面。
// exec 使用它创建用户栈下方的保护页，内核映射本身仍然保留。
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  // 先找到对应叶子 PTE；这里不允许为了清权限而创建新的页表。
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  // ~PTE_U 得到“除用户位外全为 1”的掩码，&= 只清除 PTE_U，其余位保持不变。
  *pte &= ~PTE_U;
}

// 判断 va 是否属于当前进程通过 sbrk() 声明、但尚未建立映射的惰性堆页。
int
kama_uvmshouldallocate(uint64 va)
{
  pte_t *pte;
  struct proc *p = myproc();

  if(p == 0 || va >= MAXVA || va >= p->sz)
    return 0;

  // exec 在用户栈下方留有一页保护页。惰性堆位于当前用户栈页之上，
  // 因此只允许为栈页顶端及以上、且低于 p->sz 的地址补映射。
  // 这里必须使用 trapframe 中保存的用户 sp，内核态 r_sp() 得到的是内核栈。
  if(va < PGROUNDUP(p->trapframe->sp))
    return 0;

  pte = walk(p->pagetable, PGROUNDDOWN(va), 0);
  return pte == 0 || (*pte & PTE_V) == 0;
}

// 为当前进程的一页惰性地址分配清零物理页并建立用户映射。
// 失败时标记进程，统一由陷阱/系统调用返回路径终止并回收其地址空间。
void
kama_uvmlazyallocate(uint64 va)
{
  char *pa;
  struct proc *p = myproc();
  uint64 va0 = PGROUNDDOWN(va);

  pa = kalloc();
  if(pa == 0){
    printf("lazy alloc: out of memory\n");
    p->killed = 1;
    return;
  }

  memset(pa, 0, PGSIZE);
  if(mappages(p->pagetable, va0, PGSIZE, (uint64)pa,
              PTE_W | PTE_X | PTE_R | PTE_U) != 0){
    printf("lazy alloc: failed to map page\n");
    kfree(pa);
    p->killed = 1;
  }
}

// 从内核复制数据到用户空间。
// 将 src 开始的 len 个字节复制到给定页表中的虚拟地址 dstva。
// 成功返回 0，失败返回 -1。
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    // 向下取整得到 dstva 所在用户页的起始虚拟地址。
    va0 = PGROUNDDOWN(dstva);
    // walkaddr 同时完成页表查询和用户访问权限检查。
    pa0 = walkaddr(pagetable, va0);
    // 内核替用户访问内存时不会由硬件触发用户页错误，所以必须在软件
    // copy 路径中按页补做惰性分配。仅允许修改当前进程自己的页表，
    // 避免 exec 正在构造的新页表误触发对旧地址空间的分配。
    if(pa0 == 0 && pagetable == myproc()->pagetable &&
       kama_uvmshouldallocate(dstva)){
      kama_uvmlazyallocate(dstva);
      pa0 = walkaddr(pagetable, va0);
    }
    if(pa0 == 0)
      return -1;
    // 每轮只复制当前页剩余的空间，跨页后重新查询下一页的物理地址。
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    // pa0 是物理页起始地址，加上页内偏移后才是本次复制的真实目的地址。
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    // 同步扣减剩余长度、推进内核源指针，并把用户地址移到下一页页首。
    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// 从用户空间复制数据到内核。
// 从给定页表中的虚拟地址 srcva 复制 len 个字节到 dst。
// 成功返回 0，失败返回 -1；跨页时会重新查询下一页映射。
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    // 先定位 srcva 所在页，再将该用户页转换成内核可访问的物理地址。
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0 && pagetable == myproc()->pagetable &&
       kama_uvmshouldallocate(srcva)){
      kama_uvmlazyallocate(srcva);
      pa0 = walkaddr(pagetable, va0);
    }
    if(pa0 == 0)
      return -1;
    // 本轮最多读取当前页尾，不能一次 memmove 直接跨越两张物理页。
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    // 物理页地址加页内偏移，得到当前数据在内存中的实际起点。
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    // 目标指针和剩余长度按实际复制量推进，下一轮从下一用户页开始。
    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// 从用户空间向内核复制以空字符结尾的字符串。
// 从给定页表中的虚拟地址 srcva 开始复制到 dst，
// 遇到 '\0' 或达到 max 上限时停止。
// 成功复制到 '\0' 返回 0，否则返回 -1。
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  // got_null 记录是否已经遇到字符串结束符，用于区分成功和长度耗尽。
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0 && pagetable == myproc()->pagetable &&
       kama_uvmshouldallocate(srcva)){
      kama_uvmlazyallocate(srcva);
      pa0 = walkaddr(pagetable, va0);
    }
    // 外层循环按页转换地址，避免直接跨越用户页边界读取。
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    // p 指向用户字符串在当前物理页中的第一个待复制字节。
    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      // 内层循环只扫描当前页，并在遇到字符串结束符时立即停止。
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      // n 限制当前页内的读取，max 限制调用者允许的总复制长度。
      --n;
      --max;
      p++;
      dst++;
    }

    // 当前页没有找到 '\0' 时，下一轮从下一页页首继续搜索。
    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}
