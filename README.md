# tinyredis

A Redis-compatible in-memory key–value server, written from scratch in modern C++ (C++20)
for Linux.

It speaks the real Redis wire protocol (RESP), so the official `redis-cli`, `redis-benchmark`
and any Redis client library can talk to it. It's a learning project: each milestone comes with
a write-up in [`docs/learn/`](docs/learn/) explaining the systems concept behind it.

## Status

| # | Milestone | Status |
|---|---|---|
| 0 | Project setup: CMake, unit and end-to-end tests, sanitizers, CI | ✅ |
| 1 | TCP server that answers `PING` | ✅ |
| 2 | Single-threaded `epoll` event loop for many concurrent clients | ⏳ |
| 3 | RESP protocol parser, pipelining, `GET` / `SET` / `DEL` / `EXISTS` | |
| 4 | Key expiry: `EXPIRE` / `TTL` / `SET … EX`, lazy and active expiration | |
| 5 | Lists, hashes, sets, sorted sets (skip list) | |
| 6 | Persistence: append-only file, log rewrite, `fork()` snapshots | |
| 7 | `maxmemory` with approximate LRU / LFU eviction | |
| 8 | Pub/Sub and `MULTI` / `EXEC` transactions | |
| 9 | Benchmarks against real Redis, profiling | |
| ★ | Stretch: primary–replica replication | |

## Build and run

Requires Linux (or WSL 2), a C++20 compiler, CMake ≥ 3.22, Ninja and GoogleTest.
On Ubuntu:

```bash
sudo apt-get install -y build-essential cmake ninja-build libgtest-dev python3-pytest redis-tools
```

```bash
cmake --preset debug             # configure (also: release, asan)
cmake --build --preset debug     # build
ctest --preset debug             # unit tests
python3 -m pytest tests/e2e -v   # end-to-end tests against the real binary

./build/debug/tinyredis-server --port 6380
redis-cli -p 6380 PING           # → PONG
```

## Testing

- **Unit tests** (GoogleTest) for the internals.
- **End-to-end tests** (pytest) that start the real server and talk to it over TCP, using
  raw bytes and `redis-cli`.
- **Sanitizers**: the `asan` preset builds with AddressSanitizer and UBSan to catch memory
  errors and undefined behavior.
- **CI** on GitHub Actions: GCC and Clang builds with warnings as errors, a sanitizer build,
  and a `clang-format` check.

## Learning notes

0. [What is Redis, and what are we building?](docs/learn/00-what-is-redis.md)
1. [Sockets and TCP: our first server](docs/learn/01-sockets-and-tcp.md)

## License

[MIT](LICENSE)
