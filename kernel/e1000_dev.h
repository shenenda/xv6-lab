//
// E1000 硬件定义：寄存器与 DMA 描述符环格式。
// 取自 Intel 82540EP/EM 等型号的数据手册。
//

/* 寄存器 */
#define E1000_CTL      (0x00000/4)  /* 设备控制寄存器，可读写。 */
#define E1000_ICR      (0x000C0/4)  /* 中断原因寄存器，只读。 */
#define E1000_IMS      (0x000D0/4)  /* 中断屏蔽设置寄存器，可读写。 */
#define E1000_RCTL     (0x00100/4)  /* 接收控制寄存器，可读写。 */
#define E1000_TCTL     (0x00400/4)  /* 发送控制寄存器，可读写。 */
#define E1000_TIPG     (0x00410/4)  /* 发送包间隔寄存器，可读写。 */
#define E1000_RDBAL    (0x02800/4)  /* 接收描述符环基地址低位，可读写。 */
#define E1000_RDTR     (0x02820/4)  /* 接收中断延迟定时器。 */
#define E1000_RADV     (0x0282C/4)  /* 接收中断绝对延迟定时器。 */
#define E1000_RDH      (0x02810/4)  /* 接收描述符环头下标，可读写。 */
#define E1000_RDT      (0x02818/4)  /* 接收描述符环尾下标，可读写。 */
#define E1000_RDLEN    (0x02808/4)  /* 接收描述符环总长度，可读写。 */
#define E1000_RSRPD    (0x02C00/4)  /* 小接收包检测中断。 */
#define E1000_TDBAL    (0x03800/4)  /* 发送描述符环基地址低位，可读写。 */
#define E1000_TDLEN    (0x03808/4)  /* 发送描述符环总长度，可读写。 */
#define E1000_TDH      (0x03810/4)  /* 发送描述符环头下标，可读写。 */
#define E1000_TDT      (0x03818/4)  /* 发送描述符环尾下标，可读写。 */
#define E1000_MTA      (0x05200/4)  /* 多播过滤表，可读写数组。 */
#define E1000_RA       (0x05400/4)  /* 接收 MAC 地址表，可读写数组。 */

/* 设备控制位 */
#define E1000_CTL_SLU     0x00000040    /* 设置链路为启用状态。 */
#define E1000_CTL_FRCSPD  0x00000800    /* 强制指定链路速率。 */
#define E1000_CTL_FRCDPLX 0x00001000    /* 强制指定双工模式。 */
#define E1000_CTL_RST     0x00400000    /* 完整复位设备。 */

/* 发送控制位 */
#define E1000_TCTL_RST    0x00000001    /* 软件复位。 */
#define E1000_TCTL_EN     0x00000002    /* 开启发送。 */
#define E1000_TCTL_BCE    0x00000004    /* 开启忙状态检查。 */
#define E1000_TCTL_PSP    0x00000008    /* 自动填充过短以太网帧。 */
#define E1000_TCTL_CT     0x00000ff0    /* 冲突阈值。 */
#define E1000_TCTL_CT_SHIFT 4
#define E1000_TCTL_COLD   0x003ff000    /* 冲突距离。 */
#define E1000_TCTL_COLD_SHIFT 12
#define E1000_TCTL_SWXOFF 0x00400000    /* 发送软件 XOFF 流控帧。 */
#define E1000_TCTL_PBE    0x00800000    /* 开启突发包发送。 */
#define E1000_TCTL_RTLC   0x01000000    /* 发生迟到冲突时重新发送。 */
#define E1000_TCTL_NRTU   0x02000000    /* 下溢时不重新发送。 */
#define E1000_TCTL_MULR   0x10000000    /* 支持多个未完成请求。 */

/* 接收控制位 */
#define E1000_RCTL_RST            0x00000001    /* 软件复位。 */
#define E1000_RCTL_EN             0x00000002    /* 开启该功能。 */
#define E1000_RCTL_SBP            0x00000004    /* 保留校验失败的数据包。 */
#define E1000_RCTL_UPE            0x00000008    /* 开启单播混杂模式。 */
#define E1000_RCTL_MPE            0x00000010    /* 开启多播混杂模式。 */
#define E1000_RCTL_LPE            0x00000020    /* 允许长数据包。 */
#define E1000_RCTL_LBM_NO         0x00000000    /* 不启用环回。 */
#define E1000_RCTL_LBM_MAC        0x00000040    /* MAC 层环回模式。 */
#define E1000_RCTL_LBM_SLP        0x00000080    /* 串行链路环回模式。 */
#define E1000_RCTL_LBM_TCVR       0x000000C0    /* 收发器环回模式。 */
#define E1000_RCTL_DTYP_MASK      0x00000C00    /* 描述符类型掩码。 */
#define E1000_RCTL_DTYP_PS        0x00000400    /* 分包接收描述符。 */
#define E1000_RCTL_RDMTS_HALF     0x00000000    /* 接收描述符最小阈值。 */
#define E1000_RCTL_RDMTS_QUAT     0x00000100    /* 接收描述符最小阈值。 */
#define E1000_RCTL_RDMTS_EIGTH    0x00000200    /* 接收描述符最小阈值。 */
#define E1000_RCTL_MO_SHIFT       12            /* 多播哈希偏移位移。 */
#define E1000_RCTL_MO_0           0x00000000    /* 选择多播哈希位 11:0。 */
#define E1000_RCTL_MO_1           0x00001000    /* 选择多播哈希位 12:1。 */
#define E1000_RCTL_MO_2           0x00002000    /* 选择多播哈希位 13:2。 */
#define E1000_RCTL_MO_3           0x00003000    /* 选择多播哈希位 15:4。 */
#define E1000_RCTL_MDR            0x00004000    /* 使用多播描述符环 0。 */
#define E1000_RCTL_BAM            0x00008000    /* 允许接收广播帧。 */
/* E1000_RCTL_BSEX 为 0 时可选择以下缓冲区大小。 */
#define E1000_RCTL_SZ_2048        0x00000000    /* 接收缓冲区大小为 2048 字节。 */
#define E1000_RCTL_SZ_1024        0x00010000    /* 接收缓冲区大小为 1024 字节。 */
#define E1000_RCTL_SZ_512         0x00020000    /* 接收缓冲区大小为 512 字节。 */
#define E1000_RCTL_SZ_256         0x00030000    /* 接收缓冲区大小为 256 字节。 */
/* E1000_RCTL_BSEX 为 1 时可选择以下扩展缓冲区大小。 */
#define E1000_RCTL_SZ_16384       0x00010000    /* 接收缓冲区大小为 16384 字节。 */
#define E1000_RCTL_SZ_8192        0x00020000    /* 接收缓冲区大小为 8192 字节。 */
#define E1000_RCTL_SZ_4096        0x00030000    /* 接收缓冲区大小为 4096 字节。 */
#define E1000_RCTL_VFE            0x00040000    /* 开启 VLAN 过滤。 */
#define E1000_RCTL_CFIEN          0x00080000    /* 开启规范格式检查。 */
#define E1000_RCTL_CFI            0x00100000    /* 规范格式指示位。 */
#define E1000_RCTL_DPF            0x00400000    /* 丢弃 PAUSE 流控帧。 */
#define E1000_RCTL_PMCF           0x00800000    /* 向上层传递 MAC 控制帧。 */
#define E1000_RCTL_BSEX           0x02000000    /* 缓冲区大小扩展位。 */
#define E1000_RCTL_SECRC          0x04000000    /* 接收时移除以太网 CRC。 */
#define E1000_RCTL_FLXBUF_MASK    0x78000000    /* 灵活缓冲区大小掩码。 */
#define E1000_RCTL_FLXBUF_SHIFT   27            /* 灵活缓冲区大小位移。 */

#define DATA_MAX 1518

/* 发送描述符命令位定义，参见 E1000 手册 3.3.3.1。 */
#define E1000_TXD_CMD_EOP    0x01 /* 数据包结束。 */
#define E1000_TXD_CMD_RS     0x08 /* 请求写回发送状态。 */

/* 发送描述符状态位定义，参见 E1000 手册 3.3.3.2。 */
#define E1000_TXD_STAT_DD    0x00000001 /* 描述符处理完成。 */

// [E1000 3.3.3]
struct tx_desc
{
  uint64 addr;
  uint16 length;
  uint8 cso;
  uint8 cmd;
  uint8 status;
  uint8 css;
  uint16 special;
};

/* 接收描述符状态位定义，参见 E1000 手册 3.2.3.1。 */
#define E1000_RXD_STAT_DD       0x01    /* 描述符处理完成。 */
#define E1000_RXD_STAT_EOP      0x02    /* 数据包结束。 */

// [E1000 3.2.3]
struct rx_desc
{
  uint64 addr;       /* 描述符对应数据缓冲区的物理地址。 */
  uint16 length;     /* DMA 写入数据缓冲区的字节数。 */
  uint16 csum;       /* 数据包校验和。 */
  uint8 status;      /* 描述符状态。 */
  uint8 errors;      /* 描述符错误位。 */
  uint16 special;
};

