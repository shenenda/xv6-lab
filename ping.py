import socket
import sys
import time

# 创建 IPv4 UDP 套接字，持续向 QEMU 端口转发目标发送探测报文。
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
addr = ('localhost', int(sys.argv[1]))
buf = "this is a ping!".encode('utf-8')

# 每秒发送一次，便于观察 xv6 接收描述符、中断和协议栈处理。
while True:
	print("pinging...", file=sys.stderr)
	sock.sendto(buf, ("127.0.0.1", int(sys.argv[1])))
	time.sleep(1)
