// 物理内存布局

// QEMU 的 -machine virt 机器按如下方式布置物理地址，
// 具体定义来自 QEMU 的 hw/riscv/virt.c：
//
// 00001000 -- QEMU 提供的启动 ROM
// 02000000 -- CLINT
// 0C000000 -- PLIC
// 10000000 -- UART0
// 10001000 -- virtio 磁盘
// 80000000 -- 启动 ROM 在机器模式下跳转到这里
//             -kernel 参数把内核装载到这里
// 80000000 之后是尚未使用的 RAM。

// 内核按如下方式使用物理内存：
// 80000000 -- entry.S，随后是内核代码段和数据段
// end -- 内核页分配区域的起始位置
// PHYSTOP -- 内核所用 RAM 的结束位置

// QEMU 将 UART 寄存器映射到这段物理地址。
#define UART0 0x10000000L
#define UART0_IRQ 10

// virtio 的内存映射 I/O 接口
#define VIRTIO0 0x10001000
#define VIRTIO0_IRQ 1

#ifdef LAB_NET
// QEMU virt 平台把 E1000 的 PCI 中断路由到 IRQ 33。
#define E1000_IRQ 33
#endif

// 核心本地中断器 CLINT，其中包含定时器。
#define CLINT 0x2000000L
#define CLINT_MTIMECMP(hartid) (CLINT + 0x4000 + 8*(hartid))
#define CLINT_MTIME (CLINT + 0xBFF8) // 从系统启动开始累计的时钟周期数。

// QEMU 将平台级中断控制器 PLIC 映射到这里。
#define PLIC 0x0c000000L
#define PLIC_PRIORITY (PLIC + 0x0)
#define PLIC_PENDING (PLIC + 0x1000)
#define PLIC_MENABLE(hart) (PLIC + 0x2000 + (hart)*0x100)
#define PLIC_SENABLE(hart) (PLIC + 0x2080 + (hart)*0x100)
#define PLIC_MPRIORITY(hart) (PLIC + 0x200000 + (hart)*0x2000)
#define PLIC_SPRIORITY(hart) (PLIC + 0x201000 + (hart)*0x2000)
#define PLIC_MCLAIM(hart) (PLIC + 0x200004 + (hart)*0x2000)
#define PLIC_SCLAIM(hart) (PLIC + 0x201004 + (hart)*0x2000)

// 内核假定物理地址 0x80000000 到 PHYSTOP 之间都是 RAM，
// 这段内存同时用于内核页和用户页。
#define KERNBASE 0x80000000L
#define PHYSTOP (KERNBASE + 128*1024*1024)

// 在用户页表和内核页表中，都把 trampoline 页映射到最高虚拟地址。
// 两套页表使用相同的虚拟地址，切换页表时才能继续执行同一段跳板代码。
#define TRAMPOLINE (MAXVA - PGSIZE)

// 把各个内核栈映射在 trampoline 下方，
// 每个栈旁边都留出一个无效的保护页，用于捕获栈越界。
#define KSTACK(p) (TRAMPOLINE - ((p)+1)* 2*PGSIZE)

// 用户内存布局，从虚拟地址 0 开始依次为：
//   代码段
//   初始数据段和 bss 段
//   固定大小的用户栈
//   可扩展的堆
//   ...
//   TRAPFRAME（p->trapframe，供 trampoline 保存和恢复用户寄存器）
//   TRAMPOLINE（与内核页表映射的是同一个物理页）
#define TRAPFRAME (TRAMPOLINE - PGSIZE)
