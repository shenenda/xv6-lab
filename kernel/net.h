//
// 数据包缓冲区管理。
//

#define MBUF_SIZE              2048
#define MBUF_DEFAULT_HEADROOM  128

struct mbuf {
  struct mbuf  *next; // 链表中的下一个 mbuf。
  char         *head; // 当前有效数据的起始位置。
  unsigned int len;   // 当前有效数据长度。
  char         buf[MBUF_SIZE]; // 实际存放数据的底层数组。
};

char *mbufpull(struct mbuf *m, unsigned int len);
char *mbufpush(struct mbuf *m, unsigned int len);
char *mbufput(struct mbuf *m, unsigned int len);
char *mbuftrim(struct mbuf *m, unsigned int len);

// 上述函数通过移动 head 或改变 len 操作缓冲区：
//            <- 头部扩展          <- 尾部裁剪
//             -> 头部移除          -> 尾部追加
// [--预留头空间--][----有效数据----][--尾部空闲--]
// |------------------MBUF_SIZE------------------|
//
// 这些宏会自动完成协议头结构体的类型转换和大小计算；大多数场景应优先使用它们，
// 而不是直接调用上面的裸操作。
#define mbufpullhdr(mbuf, hdr) (typeof(hdr)*)mbufpull(mbuf, sizeof(hdr))
#define mbufpushhdr(mbuf, hdr) (typeof(hdr)*)mbufpush(mbuf, sizeof(hdr))
#define mbufputhdr(mbuf, hdr) (typeof(hdr)*)mbufput(mbuf, sizeof(hdr))
#define mbuftrimhdr(mbuf, hdr) (typeof(hdr)*)mbuftrim(mbuf, sizeof(hdr))

struct mbuf *mbufalloc(unsigned int headroom);
void mbuffree(struct mbuf *m);

struct mbufq {
  struct mbuf *head;  // 队列首元素。
  struct mbuf *tail;  // 队列尾元素。
};

void mbufq_pushtail(struct mbufq *q, struct mbuf *m);
struct mbuf *mbufq_pophead(struct mbufq *q);
int mbufq_empty(struct mbufq *q);
void mbufq_init(struct mbufq *q);


//
// 字节序转换支持。
//

static inline uint16 bswaps(uint16 val)
{
  return (((val & 0x00ffU) << 8) |
          ((val & 0xff00U) >> 8));
}

static inline uint32 bswapl(uint32 val)
{
  return (((val & 0x000000ffUL) << 24) |
          ((val & 0x0000ff00UL) << 8) |
          ((val & 0x00ff0000UL) >> 8) |
          ((val & 0xff000000UL) >> 24));
}

// 使用这些宏在网络字节序和本机字节序之间转换。RISC-V 使用小端序，
// 而网络协议字段使用大端序。
#define ntohs bswaps
#define ntohl bswapl
#define htons bswaps
#define htonl bswapl


//
// 常用网络协议头。
//

#define ETHADDR_LEN 6

// 以太网帧头，位于数据包最前端。
struct eth {
  uint8  dhost[ETHADDR_LEN];
  uint8  shost[ETHADDR_LEN];
  uint16 type;
} __attribute__((packed));

#define ETHTYPE_IP  0x0800 // 网际协议 IPv4。
#define ETHTYPE_ARP 0x0806 // 地址解析协议 ARP。

// IPv4 数据包头，紧跟在以太网头之后。
struct ip {
  uint8  ip_vhl; // 高 4 位是版本，低 4 位是以 4 字节为单位的头长。
  uint8  ip_tos; // 服务类型。
  uint16 ip_len; // IP 数据包总长度。
  uint16 ip_id;  // 分片标识。
  uint16 ip_off; // 分片标志与偏移字段。
  uint8  ip_ttl; // 生存时间 TTL。
  uint8  ip_p;   // 上层协议编号。
  uint16 ip_sum; // 首部校验和。
  uint32 ip_src, ip_dst;
};

#define IPPROTO_ICMP 1  // 网际控制报文协议 ICMP。
#define IPPROTO_TCP  6  // 传输控制协议 TCP。
#define IPPROTO_UDP  17 // 用户数据报协议 UDP。

#define MAKE_IP_ADDR(a, b, c, d)           \
  (((uint32)a << 24) | ((uint32)b << 16) | \
   ((uint32)c << 8) | (uint32)d)

// UDP 数据包头，紧跟在 IP 头之后。
struct udp {
  uint16 sport; // 源端口。
  uint16 dport; // 目标端口。
  uint16 ulen;  // UDP 头与负载总长度，不含 IP 头。
  uint16 sum;   // 首部校验和。
};

// ARP 数据包头，紧跟在以太网头之后。
struct arp {
  uint16 hrd; // 硬件地址类型。
  uint16 pro; // 协议地址类型。
  uint8  hln; // 硬件地址长度。
  uint8  pln; // 协议地址长度。
  uint16 op;  // ARP 操作码。

  char   sha[ETHADDR_LEN]; // 发送方硬件地址。
  uint32 sip;              // 发送方 IP 地址。
  char   tha[ETHADDR_LEN]; // 目标方硬件地址。
  uint32 tip;              // 目标方 IP 地址。
} __attribute__((packed));

#define ARP_HRD_ETHER 1 // 以太网硬件类型。

enum {
  ARP_OP_REQUEST = 1, // 根据协议地址请求硬件地址。
  ARP_OP_REPLY = 2,   // 回复协议地址对应的硬件地址。
};

// DNS 报文头，紧跟在 UDP 头之后。
struct dns {
  uint16 id;  // 查询事务 ID。

  uint8 rd: 1;  // 请求递归查询。
  uint8 tc: 1;  // 响应是否被截断。
  uint8 aa: 1;  // 权威回答标志。
  uint8 opcode: 4; 
  uint8 qr: 1;  // 查询或响应标志。
  uint8 rcode: 4; // 响应码。
  uint8 cd: 1;  // 禁用 DNSSEC 检查。
  uint8 ad: 1;  // 数据已经过认证。
  uint8 z:  1;  
  uint8 ra: 1;  // 服务器支持递归查询。
  
  uint16 qdcount; // 问题区条目数。
  uint16 ancount; // 回答区资源记录数。
  uint16 nscount; // 权威区 NS 资源记录数。
  uint16 arcount; // 附加区资源记录数。
} __attribute__((packed));

struct dns_question {
  uint16 qtype;
  uint16 qclass;
} __attribute__((packed));
  
#define ARECORD (0x0001)
#define QCLASS  (0x0001)

struct dns_data {
  uint16 type;
  uint16 class;
  uint32 ttl;
  uint16 len;
} __attribute__((packed));
