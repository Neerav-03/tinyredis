"""Starts a real tinyredis-server process for each test and talks to it over TCP."""

import os
import re
import socket
import subprocess
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BIN = REPO_ROOT / "build" / "debug" / "tinyredis-server"


@pytest.fixture
def server():
    """Yields (host, port) of a fresh server listening on a free port."""
    binary = Path(os.environ.get("TINYREDIS_BIN", DEFAULT_BIN))
    if not binary.exists():
        pytest.fail(f"server binary not found at {binary}; build first or set TINYREDIS_BIN")

    proc = subprocess.Popen(
        [str(binary), "--port", "0"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    try:
        line = proc.stdout.readline()
        match = re.search(r"listening on ([\d.]+):(\d+)", line)
        if not match:
            pytest.fail(f"unexpected startup output: {line!r}")
        yield match.group(1), int(match.group(2))
    finally:
        proc.terminate()
        proc.wait(timeout=5)


@pytest.fixture
def connect(server):
    """Returns a function that opens a new client socket to the server."""
    opened = []

    def _connect(timeout=2.0):
        sock = socket.create_connection(server, timeout=timeout)
        opened.append(sock)
        return sock

    yield _connect
    for sock in opened:
        sock.close()
