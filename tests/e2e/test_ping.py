"""Milestone 1: the server answers PING over raw TCP and works with redis-cli."""

import shutil
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


@pytest.mark.skipif(shutil.which("redis-cli") is None, reason="redis-cli not installed")
def test_redis_cli_ping(server):
    result = subprocess.run(
        ["redis-cli", "-h", server.host, "-p", str(server.port), "PING"],
        capture_output=True,
        text=True,
        timeout=5,
    )
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == "PONG"
