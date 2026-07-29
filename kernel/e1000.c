#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// 保存 E1000 寄存器所在的 MMIO 基地址。
static volatile uint32 *regs;

struct spinlock e1000_lock;

// 由 pci_init() 调用；xregs 是 E1000 寄存器映射后的内存地址。
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // 先屏蔽中断并复位设备，复位后再次屏蔽，避免初始化未完成就收到中断。
  regs[E1000_IMS] = 0; // 关闭全部设备中断。
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // 复位可能恢复默认值，因此再次关闭中断。
  __sync_synchronize();

  // 初始化发送描述符环，参见 E1000 手册 14.5。
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    // 初始标记为 DD，表示这些发送描述符均可由驱动使用。
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // 初始化接收描述符环，参见 E1000 手册 14.4。
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    // 描述符保存供网卡 DMA 写入的物理地址；xv6 内核使用直接映射。
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // 设置 QEMU 虚拟网卡的 MAC 地址过滤项 52:54:00:12:34:56。
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // 清空多播过滤表，默认不接收额外多播地址。
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // 配置发送控制位。
  regs[E1000_TCTL] = E1000_TCTL_EN |  // 开启发送器。
    E1000_TCTL_PSP |                  // 自动填充过短帧。
    (0x10 << E1000_TCTL_CT_SHIFT) |   // 设置冲突阈值。
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // 发送包间隔参数。

  // 配置接收控制位。
  regs[E1000_RCTL] = E1000_RCTL_EN | // 开启接收器。
    E1000_RCTL_BAM |                 // 允许广播帧。
    E1000_RCTL_SZ_2048 |             // 使用 2048 字节接收缓冲区。
    E1000_RCTL_SECRC;                // 由硬件移除以太网 CRC。
  
  // 配置为每写回一个接收描述符就触发中断，不使用延迟定时器。
  regs[E1000_RDTR] = 0; // 每收到一个包立即中断，不延迟。
  regs[E1000_RADV] = 0; // 每个包都产生中断，不设延迟定时器。
  regs[E1000_IMS] = (1 << 7); // RXDW：接收描述符写回中断。
}

int
e1000_transmit(struct mbuf *m)
{
  //
  // 在此补充驱动逻辑。
  //
  // mbuf 中保存完整以太网帧：选择 TDT 指向的空闲发送描述符，填写地址、长度和
  // EOP/RS 命令后推进 TDT；同时保存 mbuf 指针，待硬件写回 DD 后释放。
  //
  
  return 0;
}

static void
e1000_recv(void)
{
  //
  // 在此补充驱动逻辑。
  //
  // 从 RDT 的下一个描述符开始检查 DD；为每个已到达数据包设置长度并调用 net_rx()，
  // 随后补充新的 mbuf、清状态并推进 RDT，把描述符归还网卡。
  //
}

void
e1000_intr(void)
{
  // 写 ICR 确认本次中断；不确认会阻止 E1000 继续产生后续中断。
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
