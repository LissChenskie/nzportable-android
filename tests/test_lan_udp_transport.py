#!/usr/bin/env python3
"""Loopback UDP transport smoke-test; not a Vril engine/gameplay integration test."""
import socket
import time
import unittest

class LanUdpSocketSmoke(unittest.TestCase):
    def test_four_clients_echo_without_global_internet(self):
        # Four endpoints and one UDP host, all bound to local loopback.
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as host:
            host.bind(("127.0.0.1", 0))
            host.settimeout(1.0)
            dest = host.getsockname()
            clients = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(4)]
            try:
                for i, client in enumerate(clients):
                    client.bind(("127.0.0.1", 0))
                    client.settimeout(1.0)
                    client.sendto(bytes([i]) + b"nzp-udp", dest)
                senders = set()
                for _ in clients:
                    data, addr = host.recvfrom(1024)
                    self.assertEqual(data[1:], b"nzp-udp")
                    senders.add(addr)
                    host.sendto(b"ack" + data[:1], addr)
                self.assertEqual(len(senders), 4)
                for i, client in enumerate(clients):
                    response, _ = client.recvfrom(32)
                    self.assertEqual(response, b"ack" + bytes([i]))
            finally:
                for client in clients:
                    client.close()

    def test_no_packets_return_nonblocking(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as host:
            host.bind(("127.0.0.1", 0))
            host.setblocking(False)
            start = time.monotonic()
            with self.assertRaises(BlockingIOError):
                host.recvfrom(1024)
            self.assertLess(time.monotonic()-start, 0.5)

    def test_restarted_client_can_rejoin_socket(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as host:
            host.bind(("127.0.0.1",0));host.settimeout(1.0)
            for attempt in range(2):
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
                    client.settimeout(1.0)
                    client.sendto(b"join",host.getsockname())
                    msg,addr=host.recvfrom(64)
                    self.assertEqual(msg,b"join")
                    host.sendto(b"ok",addr)
                    self.assertEqual(client.recv(64),b"ok")

if __name__ == "__main__":
    unittest.main()
