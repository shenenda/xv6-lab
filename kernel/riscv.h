// 读取当前 hart（硬件线程/核心）的编号。
// asm volatile 用于直接访问控制状态寄存器；"=r" 表示由汇编把结果写入通用寄存器操作数。
static inline uint64
r_mhartid()
{
  uint64 x;
  asm volatile("csrr %0, mhartid" : "=r" (x) );
  return x;
}

// 机器模式状态寄存器 mstatus。

#define MSTATUS_MPP_MASK (3L << 11) // 陷阱发生前所处的特权级。
#define MSTATUS_MPP_M (3L << 11)
#define MSTATUS_MPP_S (1L << 11)
#define MSTATUS_MPP_U (0L << 11)
#define MSTATUS_MIE (1L << 3)    // 机器模式全局中断使能位。

static inline uint64
r_mstatus()
{
  uint64 x;
  asm volatile("csrr %0, mstatus" : "=r" (x) );
  return x;
}

static inline void 
w_mstatus(uint64 x)
{
  asm volatile("csrw mstatus, %0" : : "r" (x));
}

// 机器模式异常程序计数器 mepc，保存异常返回后继续执行的指令地址。
static inline void 
w_mepc(uint64 x)
{
  asm volatile("csrw mepc, %0" : : "r" (x));
}

// PMP 权限位：读、写、执行
#define PMP_R (1L << 0)
#define PMP_W (1L << 1)
#define PMP_X (1L << 2)

// NAPOT：Naturally Aligned Power-Of-Two 匹配模式
#define PMP_MATCH_NAPOT (3L << 3)

// 写入第 0 组 PMP 配置寄存器
static inline void
w_pmpcfg0(uint64 x)
{
  asm volatile("csrw pmpcfg0, %0" : : "r" (x));
}

// 写入第 0 个 PMP 地址寄存器
static inline void
w_pmpaddr0(uint64 x)
{
  asm volatile("csrw pmpaddr0, %0" : : "r" (x));
}

// 监管者模式状态寄存器 sstatus。

#define SSTATUS_SPP (1L << 8)  // 陷阱前的特权级：1 表示监管者模式，0 表示用户模式。
#define SSTATUS_SPIE (1L << 5) // 进入陷阱前监管者中断是否开启。
#define SSTATUS_UPIE (1L << 4) // 进入陷阱前用户中断是否开启。
#define SSTATUS_SIE (1L << 1)  // 监管者模式中断使能。
#define SSTATUS_UIE (1L << 0)  // 用户模式全局中断使能位。

static inline uint64
r_sstatus()
{
  uint64 x;
  asm volatile("csrr %0, sstatus" : "=r" (x) );
  return x;
}

static inline void 
w_sstatus(uint64 x)
{
  asm volatile("csrw sstatus, %0" : : "r" (x));
}

// 监管者模式中断挂起寄存器 sip。
static inline uint64
r_sip()
{
  uint64 x;
  asm volatile("csrr %0, sip" : "=r" (x) );
  return x;
}

static inline void 
w_sip(uint64 x)
{
  asm volatile("csrw sip, %0" : : "r" (x));
}

// 监管者模式中断使能。
#define SIE_SEIE (1L << 9) // 外部中断。
#define SIE_STIE (1L << 5) // 定时器中断。
#define SIE_SSIE (1L << 1) // 软件中断。
static inline uint64
r_sie()
{
  uint64 x;
  asm volatile("csrr %0, sie" : "=r" (x) );
  return x;
}

static inline void 
w_sie(uint64 x)
{
  asm volatile("csrw sie, %0" : : "r" (x));
}

// 机器模式中断使能寄存器 mie。
#define MIE_MEIE (1L << 11) // 外部中断。
#define MIE_MTIE (1L << 7)  // 定时器中断。
#define MIE_MSIE (1L << 3)  // 软件中断。
static inline uint64
r_mie()
{
  uint64 x;
  asm volatile("csrr %0, mie" : "=r" (x) );
  return x;
}

static inline void 
w_mie(uint64 x)
{
  asm volatile("csrw mie, %0" : : "r" (x));
}

// 监管者模式异常程序计数器 sepc，保存陷阱返回用户态后继续执行的指令地址。
static inline void 
w_sepc(uint64 x)
{
  asm volatile("csrw sepc, %0" : : "r" (x));
}

static inline uint64
r_sepc()
{
  uint64 x;
  asm volatile("csrr %0, sepc" : "=r" (x) );
  return x;
}

// 机器模式异常委托寄存器 medeleg。
static inline uint64
r_medeleg()
{
  uint64 x;
  asm volatile("csrr %0, medeleg" : "=r" (x) );
  return x;
}

static inline void 
w_medeleg(uint64 x)
{
  asm volatile("csrw medeleg, %0" : : "r" (x));
}

// 机器模式中断委托寄存器 mideleg。
static inline uint64
r_mideleg()
{
  uint64 x;
  asm volatile("csrr %0, mideleg" : "=r" (x) );
  return x;
}

static inline void 
w_mideleg(uint64 x)
{
  asm volatile("csrw mideleg, %0" : : "r" (x));
}

// 监管者模式陷阱向量基地址 stvec；最低两位用于选择向量模式。
static inline void 
w_stvec(uint64 x)
{
  asm volatile("csrw stvec, %0" : : "r" (x));
}

static inline uint64
r_stvec()
{
  uint64 x;
  asm volatile("csrr %0, stvec" : "=r" (x) );
  return x;
}

// 机器模式中断向量地址 mtvec。
static inline void 
w_mtvec(uint64 x)
{
  asm volatile("csrw mtvec, %0" : : "r" (x));
}

// 使用 RISC-V 的 Sv39 三级页表方案。
#define SATP_SV39 (8L << 60)

// satp 的低 44 位保存根页表的物理页号，因此要丢弃按页对齐地址的低 12 位。
#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64)pagetable) >> 12))

// 监管者地址转换与保护寄存器 satp，其中保存页表模式和根页表物理页号。
static inline void 
w_satp(uint64 x)
{
  asm volatile("csrw satp, %0" : : "r" (x));
}

static inline uint64
r_satp()
{
  uint64 x;
  asm volatile("csrr %0, satp" : "=r" (x) );
  return x;
}

// 监管者模式暂存寄存器 sscratch，供 trampoline.S 的早期陷阱入口使用。
static inline void 
w_sscratch(uint64 x)
{
  asm volatile("csrw sscratch, %0" : : "r" (x));
}

static inline void 
w_mscratch(uint64 x)
{
  asm volatile("csrw mscratch, %0" : : "r" (x));
}

// 监管者模式陷阱原因寄存器 scause。
static inline uint64
r_scause()
{
  uint64 x;
  asm volatile("csrr %0, scause" : "=r" (x) );
  return x;
}

// 监管者模式陷阱附加值寄存器 stval，常用于记录出错虚拟地址。
static inline uint64
r_stval()
{
  uint64 x;
  asm volatile("csrr %0, stval" : "=r" (x) );
  return x;
}

// 机器模式计数器使能寄存器 mcounteren。
static inline void 
w_mcounteren(uint64 x)
{
  asm volatile("csrw mcounteren, %0" : : "r" (x));
}

static inline uint64
r_mcounteren()
{
  uint64 x;
  asm volatile("csrr %0, mcounteren" : "=r" (x) );
  return x;
}

// 读取机器模式时间计数器。
static inline uint64
r_time()
{
  uint64 x;
  asm volatile("csrr %0, time" : "=r" (x) );
  return x;
}

// 开启监管者模式设备中断。
static inline void
intr_on()
{
  w_sstatus(r_sstatus() | SSTATUS_SIE);
}

// 关闭监管者模式设备中断。
static inline void
intr_off()
{
  w_sstatus(r_sstatus() & ~SSTATUS_SIE);
}

// 判断监管者模式设备中断是否开启。
static inline int
intr_get()
{
  uint64 x = r_sstatus();
  return (x & SSTATUS_SIE) != 0;
}

static inline uint64
r_sp()
{
  uint64 x;
  asm volatile("mv %0, sp" : "=r" (x) );
  return x;
}

// 读写线程指针 tp；xv6 用它保存当前 hart 编号，也就是 cpus[] 的下标。
static inline uint64
r_tp()
{
  uint64 x;
  asm volatile("mv %0, tp" : "=r" (x) );
  return x;
}

static inline void 
w_tp(uint64 x)
{
  asm volatile("mv tp, %0" : : "r" (x));
}

static inline uint64
r_ra()
{
  uint64 x;
  asm volatile("mv %0, ra" : "=r" (x) );
  return x;
}

// 刷新 TLB，使页表修改对后续地址转换立即可见。
static inline void
sfence_vma()
{
  // 两个操作数都为 zero，表示刷新当前地址空间的全部 TLB 表项。
  asm volatile("sfence.vma zero, zero");
}


#define PGSIZE 4096 // 每页的字节数。
#define PGSHIFT 12  // 页内偏移占用的位数。

#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))

#define PTE_V (1L << 0) // 页表项有效。
#define PTE_R (1L << 1)
#define PTE_W (1L << 2)
#define PTE_X (1L << 3)
#define PTE_U (1L << 4) // 为 1 时允许用户态访问。
#define PTE_G (1L << 5)
#define PTE_A (1L << 6) // 硬件访问位。
#define PTE_D (1L << 7) // 硬件脏页位；页发生写入后置位。

// 把物理地址转换到页表项中物理页号所在的位段。
#define PA2PTE(pa) ((((uint64)pa) >> 12) << 10)

#define PTE2PA(pte) (((pte) >> 10) << 12)

#define PTE_FLAGS(pte) ((pte) & 0x3FF)

// 从虚拟地址中提取三级页表所需的三个 9 位索引。
#define PXMASK          0x1FF // 9 位索引掩码。
#define PXSHIFT(level)  (PGSHIFT+(9*(level)))
#define PX(level, va) ((((uint64) (va)) >> PXSHIFT(level)) & PXMASK)

// MAXVA 是允许使用的虚拟地址上界（不包含该值）。
// 它比 Sv39 理论上限少用一个最高位，从而避免对最高位为 1 的虚拟地址做符号扩展。
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))

typedef uint64 pte_t;
typedef uint64 *pagetable_t; // 一个页表页包含 512 个页表项。
