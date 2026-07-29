//
// 简化的 PCI Express 初始化，仅支持 QEMU virt 机器及其 E1000 网卡。
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

void
pci_init()
{
  // 把 E1000 寄存器 BAR 映射到该物理地址；vm.c 已为这段范围建立内核映射。
  uint64 e1000_regs = 0x40000000L;

  // QEMU -machine virt 把 PCIe 配置空间放在这里；vm.c 已映射该范围。
  uint32  *ecam = (uint32 *) 0x30000000L;
  
  // 扫描总线 0 上所有可能的设备号。
  for(int dev = 0; dev < 32; dev++){
    int bus = 0;
    int func = 0;
    int offset = 0;
    uint32 off = (bus << 16) | (dev << 11) | (func << 8) | (offset);
    volatile uint32 *base = ecam + off;
    uint32 id = base[0];
    
    // 设备 ID 100e、厂商 ID 8086 对应 E1000。
    if(id == 0x100e8086){
      // 配置命令/状态寄存器：位 0 开启 I/O 访问，位 1 开启内存访问，
      // 位 2 允许总线主控，使网卡能够发起 DMA。
      base[1] = 7;
      __sync_synchronize();

      for(int i = 0; i < 6; i++){
        uint32 old = base[4+i];

        // 向 BAR 写全 1 后再读回，硬件返回地址掩码，可据此计算 BAR 所需空间大小。
        base[4+i] = 0xffffffff;
        __sync_synchronize();

        base[4+i] = old;
      }

      // 把 BAR0 写为 0x40000000，使 E1000 寄存器出现在约定物理地址。
      base[4+0] = e1000_regs;

      e1000_init((uint32*)e1000_regs);
    }
  }
}
