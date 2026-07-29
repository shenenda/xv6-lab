// 内核上下文切换时需要保存的寄存器。
struct context {
  uint64 ra;
  uint64 sp;

  // 按 RISC-V 调用约定由被调用者保存的寄存器。
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

// 每个 CPU 各自维护的运行状态。
struct cpu {
  struct proc *proc;          // 当前在该 CPU 上运行的进程；空闲时为 0。
  struct context context;     // 切换到此上下文即可进入 scheduler()。
  int noff;                   // push_off() 的嵌套层数。
  int intena;                 // 最外层 push_off() 前中断是否处于开启状态。
};

extern struct cpu cpus[NCPU];

// trapframe 保存 trampoline.S 中陷阱处理代码需要的每进程数据。
// 它在用户页表中独占 trampoline 下方的一页，在内核页表中不需要特殊映射，
// sscratch 寄存器指向该结构。
// trampoline.S 的 uservec 先把用户寄存器保存到 trapframe，随后从其中装入
// kernel_sp、kernel_hartid 和 kernel_satp，并跳转到 usertrap。
// 返回时，usertrapret() 与 trampoline.S 中的 userret 设置 kernel_* 字段，
// 从 trapframe 恢复用户寄存器、切换到用户页表，最后进入用户态。
// trapframe 还必须保存 s0-s11 等被调用者保存寄存器，因为 usertrapret()
// 返回用户态时不会沿完整的内核调用栈逐层返回。
struct trapframe {
  /*   0 */ uint64 kernel_satp;   // 内核页表的 satp 值。
  /*   8 */ uint64 kernel_sp;     // 该进程内核栈的栈顶地址。
  /*  16 */ uint64 kernel_trap;   // usertrap() 的入口地址。
  /*  24 */ uint64 epc;           // 保存的用户程序计数器。
  /*  32 */ uint64 kernel_hartid; // 保存的内核 tp，即 hart 编号。
  /*  40 */ uint64 ra;
  /*  48 */ uint64 sp;
  /*  56 */ uint64 gp;
  /*  64 */ uint64 tp;
  /*  72 */ uint64 t0;
  /*  80 */ uint64 t1;
  /*  88 */ uint64 t2;
  /*  96 */ uint64 s0;
  /* 104 */ uint64 s1;
  /* 112 */ uint64 a0;
  /* 120 */ uint64 a1;
  /* 128 */ uint64 a2;
  /* 136 */ uint64 a3;
  /* 144 */ uint64 a4;
  /* 152 */ uint64 a5;
  /* 160 */ uint64 a6;
  /* 168 */ uint64 a7;
  /* 176 */ uint64 s2;
  /* 184 */ uint64 s3;
  /* 192 */ uint64 s4;
  /* 200 */ uint64 s5;
  /* 208 */ uint64 s6;
  /* 216 */ uint64 s7;
  /* 224 */ uint64 s8;
  /* 232 */ uint64 s9;
  /* 240 */ uint64 s10;
  /* 248 */ uint64 s11;
  /* 256 */ uint64 t3;
  /* 264 */ uint64 t4;
  /* 272 */ uint64 t5;
  /* 280 */ uint64 t6;
};

enum procstate { UNUSED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// 每个进程各自维护的状态。
struct proc {
  struct spinlock lock;

  // 访问以下字段时必须持有 p->lock：
  enum procstate state;        // 进程状态。
  struct proc *parent;         // 父进程。
  void *chan;                  // 非 0 时表示正在 chan 上睡眠。
  int killed;                  // 非 0 时表示进程已收到终止请求。
  int xstate;                  // 由父进程 wait 取得的退出状态。
  int pid;                     // 进程 ID。

  // 以下字段由进程自己专用，访问时通常不必持有 p->lock。
  uint64 kstack;               // 内核栈的虚拟地址。
  uint64 sz;                   // 进程内存大小，单位为字节。
  pagetable_t pagetable;       // 用户页表。
  struct trapframe *trapframe; // trampoline.S 使用的数据页。
  struct context context;      // 切换到此上下文即可运行进程。
  struct file *ofile[NOFILE];  // 已打开文件表。
  struct inode *cwd;           // 当前工作目录。
  char name[16];               // 进程名，仅用于调试。
  uint64 kama_syscall_trace;   // 存储进程的系统调用跟踪掩码，用于记录哪些系统调用需要被跟踪
};
