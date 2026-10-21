# 02 · The event loop: one thread, thousands of clients

**Goal of this milestone:** serve many clients at the same time, from a single
thread, without any client ever making the others wait.

Code as it was at the end of this milestone (tag `m2`):
[`src/net/event_loop.cpp`](https://github.com/Neerav-03/tinyredis/blob/m2/src/net/event_loop.cpp),
[`src/server/server.cpp`](https://github.com/Neerav-03/tinyredis/blob/m2/src/server/server.cpp),
[`src/main.cpp`](https://github.com/Neerav-03/tinyredis/blob/m2/src/main.cpp)

## The problem, again

The Milestone 1 server called `recv()` on one client and **blocked**: the thread
slept inside the kernel until that client sent something. While it slept,
nobody else was served. A client that connected and then went quiet froze the
whole server.

There are two classic fixes.

### Option 1: one thread per client

Easy to write, since each thread runs the simple blocking loop from Milestone 1.
But:

- each thread has its own stack (8 MB of address space by default on Linux), so
  10,000 clients means 10,000 threads and a lot of scheduler work;
- all threads share the database, so **every** access needs a lock. Locks are
  slow under contention and a famous source of bugs (deadlocks, races).

### Option 2: I/O multiplexing (what Redis does)

Use **non-blocking** sockets, which never sleep: if there's nothing to read,
`recv()` returns immediately with the error `EAGAIN` ("try again later").

Then one thread asks the kernel a single question: *"of all my sockets, which
ones are ready right now?"* It serves exactly those, then asks again. That
question-and-serve cycle is the **event loop**.

## select → poll → epoll

Linux has three ways to ask "which sockets are ready?":

| API | How you ask | Cost per call | Limits |
|---|---|---|---|
| `select` (1983) | pass a bitmap of every fd | O(all fds) | max fd 1024 |
| `poll` (1986) | pass an array of every fd | O(all fds) | none |
| **`epoll`** (2002) | register fds **once**; the kernel keeps a ready list | **O(ready fds)** | none |

With `select`/`poll` you hand the kernel your *whole* list on every call, and it
checks each one. With 10,000 mostly idle clients, that's 10,000 checks to find
the 3 that sent something. `epoll` remembers your list (the **interest list**),
and as packets arrive it moves those sockets onto a **ready list**.
`epoll_wait()` just hands you the ready list. This is how one machine can hold
tens of thousands of connections (the "C10K problem").

(epoll is Linux-only. macOS and the BSDs have `kqueue`, which works the same
way. Redis wraps all of them behind one small interface called `ae`, which is
what our `EventLoop` class does too.)

The three calls:

```cpp
int ep = epoll_create1(EPOLL_CLOEXEC);          // create the epoll instance
epoll_ctl(ep, EPOLL_CTL_ADD, fd, &event);       // watch fd   (also _MOD, _DEL)
int n = epoll_wait(ep, ready, max, timeout);    // sleep until something is ready
```

## One iteration of our loop

```
                ┌───────────────────────────────────────────────┐
                │ epoll_wait(): sleep until some fds are ready  │
                └───────────────────────┬───────────────────────┘
          ┌─────────────────────────────┼──────────────────────────────┐
     listener ready               client ready                  signalfd ready
          │                             │                              │
  accept4() until EAGAIN,      recv() up to 16 KiB             SIGTERM/SIGINT
  register each new client     append reply to its out buffer   → loop.stop()
                               send() as much as the kernel takes
                               still unsent? watch for kWritable
```

Everything happens on one thread, so **the data never needs a lock**: only one
piece of code touches it at any moment. That's the real reason Redis is
single-threaded. It's not a limitation they put up with, it's a design choice.

## Level-triggered vs edge-triggered

epoll has two modes:

- **Level-triggered (LT, the default and what we use):** "this socket *has*
  unread data". Reported on every `epoll_wait` until you've read it all.
- **Edge-triggered (ET):** "new data *arrived*". Reported once; if you don't
  read everything right away, you won't be told again.

ET means fewer wake-ups, but you must read until `EAGAIN` on every event, or the
connection hangs forever. LT forgives mistakes, and it lets us be **fair**: we
read at most 16 KiB from a client per iteration (Redis does the same), and if
there's more, LT simply reports it again next time, after every other ready
client has had its turn. The unit test `LevelTriggeredReportsUnreadDataAgain`
shows this behaviour.

## The write path: buffers, kWritable and backpressure

Writing has its own catch. `send()` on a non-blocking socket copies your bytes
into the kernel's send buffer and returns. If that buffer is full (the client is
slow, or the network is), it takes only part of the data, or none (`EAGAIN`).

So each client has an **output buffer**. After handling a read we try to
`send()` straight away; usually the kernel takes everything. Only if bytes are
left over do we ask epoll to tell us when the socket is **writable** again.

> Why not watch for kWritable all the time? An idle socket almost always has
> room in its send buffer, so it is *always* writable. With level-triggered
> epoll the loop would wake up constantly and burn 100% CPU doing nothing.

**Backpressure:** imagine a client that sends requests nonstop but never reads
the replies. Its output buffer would grow until we run out of memory. So once
1 MiB of replies is waiting, we **stop reading** from that client (we stop
watching kReadable) until it catches up. Real Redis has the same safety valve
in a stricter form: `client-output-buffer-limit` disconnects such clients.

## Two subtle bugs we avoided

These are the kind of thing interviewers love, because they only show up under
load.

**1. A handler that deletes itself.** When a client disconnects, its own
handler calls `loop.remove(fd)`. If `remove` destroyed the handler, the code
still running would be using freed memory. Our `EventLoop::remove` uses
`unordered_map::extract()`, which unlinks the entry without moving or freeing
it, and parks it until the current batch of events is done. The unit test
`HandlerCanRemoveItself` checks this under AddressSanitizer.

**2. Stale events after fd reuse.** One `epoll_wait` can return a batch of
events. Suppose events for clients 7 and 9 are both in the batch. Handling
client 9 closes client **7**, and then a new client connects and the kernel
gives it the lowest free number, which is **7** again. Now the batch's leftover
event for "fd 7" would be delivered to the *new* client. A leftover error event
could even make us close it.

The fix: epoll lets us attach 64 bits to each registration. We store the fd
**and a generation number** that increases on every `add()`. When an event comes
back, we only deliver it if the generation still matches. The unit test
`SkipsStaleEventWhenFdNumberIsReusedInSameBatch` sets up exactly this race, and
fails if the generation check is removed. (We checked.)

## Shutting down cleanly: signalfd

`Ctrl+C` sends `SIGINT`; `kill` and Docker send `SIGTERM`. A classic signal
handler runs *in the middle of whatever the program was doing*, so it can only
safely do a handful of things. Linux's `signalfd` turns signals into a
readable file descriptor instead: we block the signals, put the signalfd into
the event loop, and a shutdown request becomes just another event. The loop
stops, destructors close every socket, and the process exits with status 0.
Later, when we have persistence, this is where we'll save data before exiting.

## File descriptor limits

Every client is a file descriptor, and the default *soft* limit is often 1024.
At startup we raise it to the *hard* limit (`setrlimit(RLIMIT_NOFILE)`), as
Redis does. In a shell, `ulimit -n` shows the soft limit and `ulimit -Hn` the
hard one.

## The catch with single-threaded

One thread means **one slow command stalls every client**. That's why Redis
warns against `KEYS *` on big databases (it scans everything while everyone
waits), and why each command we build later must be fast, ideally O(1) or
O(log n). Redis 6 added optional *I/O threads* that parallelise the socket
reads and writes. Commands still run one at a time on the main thread, so the
"no locks on the data" property stays.

## How this maps to real Redis

| tinyredis | Redis source |
|---|---|
| `net::EventLoop` | `ae.c`, `ae_epoll.c` (`aeCreateFileEvent`, `aeProcessEvents`) |
| `Server::on_accept` | `acceptTcpHandler` in `networking.c` |
| `Server::read_request` | `readQueryFromClient` |
| `Server::flush` | `writeToClient` (Redis writes just before the next `epoll_wait`, in `beforeSleep`) |
| backpressure | `client-output-buffer-limit` |

## Try it

```bash
cmake --build --preset debug
./build/debug/tinyredis-server --port 6380
```

Now open **two** `redis-cli -p 6380` sessions and type `PING` in both. Both
answer instantly; in Milestone 1 the second one hung. Press `Ctrl+C` in the
server window and it prints `received Interrupt, shutting down`.

Hammer it with 50 parallel clients using the official benchmark tool:

```bash
cmake --preset release && cmake --build --preset release
./build/release/tinyredis-server --port 6380 &
redis-benchmark -p 6380 -t ping -n 200000 -c 50 -q
```

(The "Could not fetch server CONFIG" warning is expected: we don't implement
`CONFIG`.) Run the same command against a real `redis-server` to compare. We'll
do that properly in Milestone 9.

## What's still wrong

We still answer one `PONG` per `recv()` call, not one per request. Pipelined
requests (several commands in one packet) or a request split across packets get
the wrong number of replies. **Milestone 3** adds a real RESP parser that buffers
partial input and handles every complete command in the buffer.

## Interview questions you can now answer

- Why can a single-threaded server handle thousands of clients?
- What's the difference between `select`, `poll` and `epoll`? Why is epoll O(ready)?
- Level-triggered vs edge-triggered: what's the trade-off?
- What does a non-blocking `send()` do when the socket buffer is full?
- Why shouldn't you watch for writability all the time?
- What is backpressure, and what happens without it?
- Why is Redis single-threaded, and what's the downside?
- How can a reused file descriptor number cause a bug in an event loop?
