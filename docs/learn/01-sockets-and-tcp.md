# 01 · Sockets and TCP: our first server

**Goal of this milestone:** a process that listens on a TCP port and replies
`+PONG` to anything it receives, enough for `redis-cli PING` to work.

Code: [`src/net/socket.cpp`](../../src/net/socket.cpp), [`src/main.cpp`](../../src/main.cpp)

## Everything is a file descriptor

On Linux, an open file, a pipe and a network connection are all represented by a
small integer called a **file descriptor (fd)**. You `read`/`write` (or
`recv`/`send`) on it and `close` it when you're done.

If you forget to `close`, you leak descriptors. A process can only have a limited
number open (often 1024), so a server that leaks one per client eventually stops
accepting connections. We prevent that with **RAII**: `net::Fd` closes its
descriptor in its destructor, so it's closed on every path, including
exceptions. It's *move-only*, because two owners would close it twice.

## The server life cycle

```
socket()   create a TCP socket                        → listening fd
bind()     attach it to an address, e.g. 127.0.0.1:6379
listen()   tell the kernel to queue incoming connections
loop:
  accept()   take the next queued connection          → a NEW fd for this client
  recv()     read the bytes the client sent
  send()     write our reply
  close()    hang up when the client leaves
```

Two details people often miss:

- **`accept` returns a new descriptor.** The listening socket keeps listening;
  each client gets its own connection socket.
- **The kernel queues connections for you.** The `backlog` argument to `listen`
  is the size of that queue. A client can connect (the TCP handshake completes)
  *before* we ever call `accept`.

## TCP is a byte stream, not messages

This one causes real bugs. TCP delivers a **stream of bytes**, not separate
messages. If a client sends `PING` twice very quickly, one `recv` call may return
both (`PING\r\nPING\r\n`), or only half of one (`PI`).

Our Milestone 1 server ignores this and sends one `PONG` per `recv` call. That's
**wrong**, but good enough to get started. Milestone 3 fixes it with a real
protocol parser that buffers partial input and handles several commands arriving
at once (*pipelining*).

## Small things that matter in production

| Code | Why |
|---|---|
| `SO_REUSEADDR` | Without it, restarting the server fails with "Address already in use" for about 60 s while old connections sit in `TIME_WAIT`. |
| `SIGPIPE` ignored and `MSG_NOSIGNAL` | Writing to a client that already disconnected sends `SIGPIPE`, which **kills the process** by default. One rude client shouldn't take down the server. |
| `SOCK_CLOEXEC` | Child processes don't inherit our sockets. This matters in Milestone 6, when we `fork()` to take snapshots. |
| Retry on `EINTR` | A signal can interrupt a system call partway through; that isn't an error, just try again. |
| `write_all` loop | `send` may write fewer bytes than you asked for (a *short write*), so we loop until everything is sent. |
| Bind `127.0.0.1` by default | Only this machine can connect. Exposing an unauthenticated database to the network is a classic security incident; real Redis has a "protected mode" for this reason. |

## The problem we'll fix next

Our loop is `accept → serve that client until it leaves → accept the next one`.
While one client is connected, **every other client waits**, even an idle one.
The e2e test `test_second_client_waits_while_first_is_connected` shows this on
purpose.

Possible fixes:

1. **A thread per client.** Simple, but 10,000 clients means 10,000 threads,
   each with its own stack (about 8 MB of address space), plus locks around all
   shared data.
2. **An event loop**: one thread asks the kernel "which of my 10,000 sockets
   have data right now?" and serves only those. **This is what Redis does**, and
   it's Milestone 2.

## Try it

```bash
cmake --preset debug && cmake --build --preset debug
./build/debug/tinyredis-server --port 6380
```

In a second terminal:

```bash
redis-cli -p 6380 PING                  # → PONG
printf 'PING\r\n' | nc -q1 localhost 6380   # → +PONG (the raw wire format)
```

Then open two `redis-cli -p 6380` sessions at once and type `PING` in the second
one. It hangs until you exit the first. That's the limitation above.

## Interview questions you can now answer

- What's the difference between the listening socket and the socket `accept` returns?
- Why might one `recv` return half a message, or two messages?
- What is `SIGPIPE`, and why do servers ignore it?
- Why does restarting a server sometimes fail with "Address already in use"?
- What's wrong with one thread per client at large scale?
