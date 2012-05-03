#!/usr/bin/python3

# Copyright © 2012 Maykel Moya <mmoya@mmoya.org>
#
# This file is part of chet2p
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#
#
# This simulate chet2p clients. For using it create a dummy device
# and add peers.txt ips to it:
#
# modprod dummy
# ip addr add 192.168.111.1/29 dev dummy0
# ip addr add 192.168.111.2/29 dev dummy0
# ip addr add 192.168.111.3/29 dev dummy0
# ip addr add 192.168.111.4/29 dev dummy0
#
# With code borrowed from http://scotdoyle.com/python-epoll-howto.html

import random
import select
import socket
import sys
import threading
import time

PONG_FAILS = 0.3
SEND_EXEC = 0.02

srvsockets = {}
udpsockets = {}
conns = {}

def randsend():
    while True:
        if conns:
            conn, addr = random.choice(list(conns.values()))
            if conn:
                if random.random() <= SEND_EXEC:
                    data = b'exec /usr/bin/xeyes\n'
                else:
                    data = b'Just any random data...\n'
                print('Sending <{0}> to TCP {1}:{2}'.format(data, *addr))
                conn.send(b'exec /usr/bin/xeyes\n')
        time.sleep(5)

def load_peers(filename, _id):
    peers = []
    with open(filename) as peersfile:
        for line in peersfile:
            if line[0] == '#':
                continue
            fields = line.strip().split()
            if fields[0] == _id:
                continue
            peers.append((fields[1], int(fields[2]), int(fields[3])))
    return tuple(peers)

def main():
    peers = load_peers(*sys.argv[1:])

    epoll = select.epoll()
    for peer in peers:
        tcp_peer = (peer[0], peer[2])
        sk = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sk.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        print("Binding to TCP {0}:{1}".format(*tcp_peer))
        sk.bind(tcp_peer)
        sk.listen(1)
        sk.setblocking(0)
        epoll.register(sk.fileno(), select.EPOLLIN)
        srvsockets[sk.fileno()] = sk

        udp_peer = (peer[0], peer[1])
        sk = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sk.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        print("Binding to UDP {0}:{1}".format(*udp_peer))
        sk.bind(udp_peer)
        sk.setblocking(0)
        epoll.register(sk.fileno(), select.EPOLLIN)
        udpsockets[sk.fileno()] = sk

    t = threading.Thread(target=randsend)
    t.start()

    while True:
        events = epoll.poll(-1)
        for fileno, event in events:
            if fileno in srvsockets.keys():
                srvsk = srvsockets[fileno]
                conn, addr = srvsk.accept()
                print("Accepted TCP connection from {0}:{1}".format(*addr))
                conn.setblocking(0)
                epoll.register(conn.fileno(), select.EPOLLIN)
                conns[conn.fileno()] = (conn, addr)
            elif fileno in udpsockets.keys():
                sk = udpsockets[fileno]
                _input, addr = sk.recvfrom(1024)
                print("From UDP {1}:{2}: {0}".format(_input, *addr))
                if _input.strip() == b'ping':
                    pong = b'pong\n'
                    if random.random() >= PONG_FAILS:
                        print("Sending {0} to UDP {1}:{2}".format(pong, *addr))
                        sk.sendto(pong, addr)
                    else:
                        print("Not sending {0} to UDP {1}:{2}".format(pong, *addr))
            elif event & select.EPOLLIN:
                conn, addr = conns[fileno]
                _input = conn.recv(1024)
                # Receive data of length zero ==> connection closed.
                if len(_input) > 0:
                    print("From TCP {1}:{2}: {0}".format(_input, *addr))
                else:
                    print("Closing TCP connection from {0}:{1}".format(*addr))
                    conn.close()
                    conns.pop(fileno)
                    epoll.unregister(fileno)
            elif event & select.EPOLLHUP:
                conn, addr = conns[fileno]
                conn.close()
                conns.pop(fileno)
                epoll.unregister(fileno)
            elif event & select.EPOLLOUT:
                pass

if __name__ == '__main__':
	main()
