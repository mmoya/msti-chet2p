#!/usr/bin/python3

# http://scotdoyle.com/python-epoll-howto.html

import select
import socket
import sys

srvsockets = {}
conns = {}

def load_peers(filename, _id):
    peers = []
    with open(filename) as peersfile:
        for line in peersfile:
            if line[0] == '#':
                continue
            fields = line.strip().split()
            if fields[0] == _id:
                continue
            peers.append((fields[1], int(fields[3])))
    return tuple(peers)

def main():
    peers = load_peers(*sys.argv[1:])

    epoll = select.epoll()
    for peer in peers:
        sk = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sk.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        print("Binding to TCP {0}:{1}".format(*peer))
        sk.bind(peer)
        sk.listen(1)
        sk.setblocking(0)
        epoll.register(sk.fileno(), select.EPOLLIN)
        srvsockets[sk.fileno()] = sk

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
