"""Milestone 1: the server answers PING over raw TCP and works with redis-cli."""

import shutil
import socket
import subprocess

import pytest

# This is exactly what `redis-cli PING` puts on the wire: an array of one bulk string.
RESP_PING = b"*1\r\n$4\r\nPING\r\n"
PONG = b"+PONG\r\n"


def test_ping_in_resp_format(connect):
    client = connect()
    client.sendall(RESP_PING)
    assert client.recv(64) == PONG


def test_inline_ping(connect):
    # Redis also accepts plain text commands, which is what you type in telnet/nc.
    client = connect()
    client.sendall(b"PING\r\n")
    assert client.recv(64) == PONG


def test_many_requests_on_one_connection(connect):
    client = connect()
    for _ in range(5):
        client.sendall(RESP_PING)
        assert client.recv(64) == PONG


def test_new_client_after_previous_one_disconnects(connect):
    first = connect()
    first.sendall(RESP_PING)
    assert first.recv(64) == PONG
    first.close()

    second = connect()
    second.sendall(RESP_PING)
    assert second.recv(64) == PONG


def test_second_client_waits_while_first_is_connected(connect):
    """Documents the Milestone 1 limitation: one client at a time.

    Milestone 2's event loop flips this test, and the second client gets an answer at once.
    """
    first = connect()
    first.sendall(RESP_PING)
    assert first.recv(64) == PONG

    second = connect(timeout=0.5)
    second.sendall(RESP_PING)  # the kernel accepts the bytes, but nobody reads them yet
    with pytest.raises(socket.timeout):
        second.recv(64)

    first.close()  # the server finishes with `first` and moves on to `second`
    second.settimeout(2.0)
    assert second.recv(64) == PONG


@pytest.mark.skipif(shutil.which("redis-cli") is None, reason="redis-cli not installed")
def test_redis_cli_ping(server):
    host, port = server
    result = subprocess.run(
        ["redis-cli", "-h", host, "-p", str(port), "PING"],
        capture_output=True,
        text=True,
        timeout=5,
    )
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == "PONG"
