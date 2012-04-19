#!/usr/bin/python3

# http://scotdoyle.com/python-epoll-howto.html

import select
import socket

peers = (
    ('192.168.111.1', 10666),
    ('192.168.111.2', 10666),
    ('192.168.111.3', 10666),
)

srvsockets = {}
conns = {}

def main():
    epoll = select.epoll()
    for peer in peers:
        sk = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sk.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        print("Binding to {0}:{1}".format(*peer))
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
                print("Accepted connection from {0}".format(addr))
                conn.setblocking(0)
                epoll.register(conn.fileno(), select.EPOLLIN)
                conns[conn.fileno()] = conn
            elif event & select.EPOLLIN:
                _input = conns[fileno].recv(1024)
                # Receive data of length zero ==> connection closed.
                if len(_input) > 0:
                    print("Received {0}".format(_input))
                else:
                    conns[fileno].close()
                    conns.pop(fileno)
                    epoll.unregister(fileno)
            elif event & select.EPOLLHUP:
                conns[fileno].close()
                conns.pop(fileno)
                epoll.unregister(fileno)
            elif event & select.EPOLLOUT:
                pass

if __name__ == '__main__':
	main()
