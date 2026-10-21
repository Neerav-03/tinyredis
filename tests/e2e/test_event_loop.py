"""Milestone 2: one thread serves many clients at once via the epoll event loop."""

import signal
import socket
import struct

import pytest

RESP_PING = b"*1\r\n$4\r\nPING\r\n"
PONG = b"+PONG\r\n"


def test_second_client_is_served_while_first_is_connected(connect):
    # In Milestone 1 this second client waited until the first one hung up.
    first = connect()
    first.sendall(RESP_PING)
    assert first.recv(64) == PONG

    second = connect(timeout=1.0)
    second.sendall(RESP_PING)
    assert second.recv(64) == PONG


def test_idle_client_does_not_block_others(connect):
    connect()  # connects and then says nothing at all
    busy = connect(timeout=1.0)
    for _ in range(3):
        busy.sendall(RESP_PING)
        assert busy.recv(64) == PONG


def test_half_sent_request_does_not_block_others(connect):
    stalled = connect()
    stalled.sendall(b"*1\r\n$4\r\nPI")  # ...and never sends the rest
    other = connect(timeout=1.0)
    other.sendall(RESP_PING)
    assert other.recv(64) == PONG


def test_hundreds_of_concurrent_clients(connect):
    clients = [connect() for _ in range(500)]
    for client in clients:
        client.sendall(RESP_PING)
    for client in clients:
        assert client.recv(64) == PONG


def test_abrupt_disconnect_does_not_affect_others(connect):
    rude = connect()
    rude.sendall(RESP_PING)
    # SO_LINGER with a zero timeout makes close() send a TCP reset (RST)
    # instead of a polite FIN, as when a client process crashes.
    rude.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
    rude.close()

    other = connect(timeout=1.0)
    other.sendall(RESP_PING)
    assert other.recv(64) == PONG


@pytest.mark.parametrize("sig", [signal.SIGTERM, signal.SIGINT], ids=["SIGTERM", "SIGINT"])
def test_shuts_down_cleanly_on_signal(server, connect, sig):
    client = connect()
    client.sendall(RESP_PING)
    assert client.recv(64) == PONG

    server.proc.send_signal(sig)
    assert server.proc.wait(timeout=5) == 0
    output = server.proc.stdout.read()
    assert "shutting down" in output
    assert client.recv(64) == b""  # the server closed our connection on the way out
