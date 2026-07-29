//
// 网络协议支持，包括 IP、UDP 和 ARP 等。
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "net.h"
#include "defs.h"

static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15); // QEMU 用户网络为来宾分配的 IP。
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint8 broadcast_mac[ETHADDR_LEN] = { 0xFF, 0XFF, 0XFF, 0XFF, 0XFF, 0XFF };

// 从缓冲区头部移除 len 字节并返回原头指针；可用数据不足时返回 0。
char *
mbufpull(struct mbuf *m, unsigned int len)
{
  char *tmp = m->head;
  if (m->len < len)
    return 0;
  m->len -= len;
  m->head += len;
  return tmp;
}

// 在缓冲区头部预留 len 字节并返回新头指针，常用于逐层添加协议头。
char *
mbufpush(struct mbuf *m, unsigned int len)
{
  m->head -= len;
  if (m->head < m->buf)
    panic("mbufpush");
  m->len += len;
  return m->head;
}

// 在缓冲区尾部追加 len 字节并返回追加区域的起始指针。
char *
mbufput(struct mbuf *m, unsigned int len)
{
  char *tmp = m->head + m->len;
  m->len += len;
  if (m->len > MBUF_SIZE)
    panic("mbufput");
  return tmp;
}

// 从缓冲区尾部移除 len 字节并返回新的尾指针；可用数据不足时返回 0。
char *
mbuftrim(struct mbuf *m, unsigned int len)
{
  if (len > m->len)
    return 0;
  m->len -= len;
  return m->head + m->len;
}

// 分配一个包缓冲区，并在数据头之前预留 headroom 字节。
struct mbuf *
mbufalloc(unsigned int headroom)
{
  struct mbuf *m;
 
  if (headroom > MBUF_SIZE)
    return 0;
  m = kalloc();
  if (m == 0)
    return 0;
  m->next = 0;
  // head 从预留空间之后开始，使发送路径可以向前 push 多层协议头。
  m->head = (char *)m->buf + headroom;
  m->len = 0;
  memset(m->buf, 0, sizeof(m->buf));
  return m;
}

// 释放包缓冲区。
void
mbuffree(struct mbuf *m)
{
  kfree(m);
}

// 把 mbuf 插入队列尾部。
void
mbufq_pushtail(struct mbufq *q, struct mbuf *m)
{
  m->next = 0;
  if (!q->head){
    q->head = q->tail = m;
    return;
  }
  q->tail->next = m;
  q->tail = m;
}

// 从队列头部取出一个 mbuf。
struct mbuf *
mbufq_pophead(struct mbufq *q)
{
  struct mbuf *head = q->head;
  if (!head)
    return 0;
  q->head = head->next;
  return head;
}

// 队列为空时返回非 0。
int
mbufq_empty(struct mbufq *q)
{
  return q->head == 0;
}

// 初始化 mbuf 队列。
void
mbufq_init(struct mbufq *q)
{
  q->head = 0;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * 使用 32 位累加器依次相加所有 16 位字，最后把高 16 位产生的进位
   * 折回低 16 位，得到互联网校验和。
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* 长度为奇数时，把最后一个字节补入累加和。 */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* 把高 16 位进位反复折回低 16 位。 */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* 此时 sum 的低 16 位已经完成回卷求和。 */

  answer = ~sum; /* 取反并截断为 16 位校验和。 */
  return answer;
}

// 组装并发送以太网帧。
static void
net_tx_eth(struct mbuf *m, uint16 ethtype)
{
  struct eth *ethhdr;

  ethhdr = mbufpushhdr(m, *ethhdr);
  memmove(ethhdr->shost, local_mac, ETHADDR_LEN);
  // 完整协议栈应通过 ARP 缓存取得目标 MAC；当前实现的 ARP 功能有限，
  // 因而把目标地址统一设为广播地址。
  memmove(ethhdr->dhost, broadcast_mac, ETHADDR_LEN);
  ethhdr->type = htons(ethtype);
  if (e1000_transmit(m)) {
    mbuffree(m);
  }
}

// 在现有负载前添加 IPv4 头并发送。
static void
net_tx_ip(struct mbuf *m, uint8 proto, uint32 dip)
{
  struct ip *iphdr;

  // 从 headroom 中向前扩展出 IP 头。
  iphdr = mbufpushhdr(m, *iphdr);
  memset(iphdr, 0, sizeof(*iphdr));
  // 高 4 位是 IPv4 版本，低 4 位是以 4 字节为单位的首部长度。
  iphdr->ip_vhl = (4 << 4) | (20 >> 2);
  iphdr->ip_p = proto;
  iphdr->ip_src = htonl(local_ip);
  iphdr->ip_dst = htonl(dip);
  iphdr->ip_len = htons(m->len);
  iphdr->ip_ttl = 100;
  iphdr->ip_sum = in_cksum((unsigned char *)iphdr, sizeof(*iphdr));

  // IP 头完成后继续交给以太网层封装。
  net_tx_eth(m, ETHTYPE_IP);
}

// 在现有负载前添加 UDP 头并发送。
void
net_tx_udp(struct mbuf *m, uint32 dip,
           uint16 sport, uint16 dport)
{
  struct udp *udphdr;

  // 从 headroom 中向前扩展出 UDP 头，并转换为网络字节序。
  udphdr = mbufpushhdr(m, *udphdr);
  udphdr->sport = htons(sport);
  udphdr->dport = htons(dport);
  udphdr->ulen = htons(m->len);
  udphdr->sum = 0; // UDP 校验和为 0 表示发送端未提供校验和。

  // UDP 头完成后继续交给 IP 层封装。
  net_tx_ip(m, IPPROTO_UDP, dip);
}

// 构造并发送 ARP 数据包。
static int
net_tx_arp(uint16 op, uint8 dmac[ETHADDR_LEN], uint32 dip)
{
  struct mbuf *m;
  struct arp *arphdr;

  m = mbufalloc(MBUF_DEFAULT_HEADROOM);
  if (!m)
    return -1;

  // 填写 ARP 头中硬件类型、协议类型、地址长度和操作码。
  arphdr = mbufputhdr(m, *arphdr);
  arphdr->hrd = htons(ARP_HRD_ETHER);
  arphdr->pro = htons(ETHTYPE_IP);
  arphdr->hln = ETHADDR_LEN;
  arphdr->pln = sizeof(uint32);
  arphdr->op = htons(op);

  // 填写发送方与目标方的 MAC/IP 地址。
  memmove(arphdr->sha, local_mac, ETHADDR_LEN);
  arphdr->sip = htonl(local_ip);
  memmove(arphdr->tha, dmac, ETHADDR_LEN);
  arphdr->tip = htonl(dip);

  // ARP 头已完成，交给以太网层发送。
  net_tx_eth(m, ETHTYPE_ARP);
  return 0;
}

// 接收并处理 ARP 数据包。
static void
net_rx_arp(struct mbuf *m)
{
  struct arp *arphdr;
  uint8 smac[ETHADDR_LEN];
  uint32 sip, tip;

  arphdr = mbufpullhdr(m, *arphdr);
  if (!arphdr)
    goto done;

  // 只接受以太网上承载 IPv4、且地址长度符合预期的 ARP 包。
  if (ntohs(arphdr->hrd) != ARP_HRD_ETHER ||
      ntohs(arphdr->pro) != ETHTYPE_IP ||
      arphdr->hln != ETHADDR_LEN ||
      arphdr->pln != sizeof(uint32)) {
    goto done;
  }

  // 当前只支持发给本机 IP 的 ARP 请求。
  tip = ntohl(arphdr->tip); // 目标 IP 地址。
  if (ntohs(arphdr->op) != ARP_OP_REQUEST || tip != local_ip)
    goto done;

  // 取出请求方地址，向其发送 ARP 回复。
  memmove(smac, arphdr->sha, ETHADDR_LEN); // 发送方 MAC 地址。
  sip = ntohl(arphdr->sip); // 发送方 IP，即 QEMU slirp 地址。
  net_tx_arp(ARP_OP_REPLY, smac, sip);

done:
  mbuffree(m);
}

// 接收并校验 UDP 数据包。
static void
net_rx_udp(struct mbuf *m, uint16 len, struct ip *iphdr)
{
  struct udp *udphdr;
  uint32 sip;
  uint16 sport, dport;


  udphdr = mbufpullhdr(m, *udphdr);
  if (!udphdr)
    goto fail;

  // TODO：尚未校验 UDP 校验和。

  // UDP 头声明的长度必须与 IP 层传入长度一致，且不能超过 mbuf 剩余数据。
  if (ntohs(udphdr->ulen) != len)
    goto fail;
  len -= sizeof(*udphdr);
  if (len > m->len)
    goto fail;
  // 以太网最小帧可能含填充字节，把 mbuf 尾部裁剪到真实 UDP 负载长度。
  mbuftrim(m, m->len - len);

  // 解析源地址和端口，再按本地/远端端口顺序投递给 socket 层。
  sip = ntohl(iphdr->ip_src);
  sport = ntohs(udphdr->sport);
  dport = ntohs(udphdr->dport);
  sockrecvudp(m, sip, dport, sport);
  return;

fail:
  mbuffree(m);
}

// 接收并校验 IPv4 数据包。
static void
net_rx_ip(struct mbuf *m)
{
  struct ip *iphdr;
  uint16 len;

  iphdr = mbufpullhdr(m, *iphdr);
  if (!iphdr)
	  goto fail;

  // 当前只接受无选项的 IPv4 固定 20 字节首部。
  if (iphdr->ip_vhl != ((4 << 4) | (20 >> 2)))
    goto fail;
  // 重新计算 IP 首部校验和；正确报文的结果应为 0。
  if (in_cksum((unsigned char *)iphdr, sizeof(*iphdr)))
    goto fail;
  // 当前协议栈不支持 IP 分片。
  if (htons(iphdr->ip_off) != 0)
    goto fail;
  // 丢弃目标 IP 不是本机的报文。
  if (htonl(iphdr->ip_dst) != local_ip)
    goto fail;
  // 当前 IP 层只向上分发 UDP。
  if (iphdr->ip_p != IPPROTO_UDP)
    goto fail;

  len = ntohs(iphdr->ip_len) - sizeof(*iphdr);
  net_rx_udp(m, len, iphdr);
  return;

fail:
  mbuffree(m);
}

// 由 E1000 中断处理程序调用，把收到的以太网帧递交给协议栈。
void net_rx(struct mbuf *m)
{
  struct eth *ethhdr;
  uint16 type;

  ethhdr = mbufpullhdr(m, *ethhdr);
  if (!ethhdr) {
    mbuffree(m);
    return;
  }

  // EtherType 使用网络字节序，转换后选择 IP 或 ARP 处理路径。
  type = ntohs(ethhdr->type);
  if (type == ETHTYPE_IP)
    net_rx_ip(m);
  else if (type == ETHTYPE_ARP)
    net_rx_arp(m);
  else
    mbuffree(m);
}
