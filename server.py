import socket
import sys

# 创建并绑定本机 IPv4 UDP 套接字，作为 xv6 网络测试的回显服务器。
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
addr = ('localhost', int(sys.argv[1]))
print('listening on %s port %s' % addr, file=sys.stderr)
sock.bind(addr)

# 收到数据后把固定响应发回原地址，验证 xv6 的双向 UDP 路径。
while True:
    buf, raddr = sock.recvfrom(4096)
    print(buf.decode("utf-8"), file=sys.stderr)
    if buf:
        sent = sock.sendto(b'this is the host!', raddr)
