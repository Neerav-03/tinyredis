# 00 · What is Redis, and what are we building?

## Redis in one paragraph

Redis is a **key–value database that keeps all its data in memory (RAM)**. You
connect to it over TCP and send commands such as `SET user:42 "Neerav"` and
`GET user:42`. RAM is roughly 100,000× faster to access than a disk seek, so a
single Redis server can answer hundreds of thousands of requests per second with
sub-millisecond latency.

## Why backend teams use it

| Use case | How Redis helps | Commands we'll build |
|---|---|---|
| **Cache** in front of a slow database | Store the result of an expensive query with a time-to-live | `SET key value EX 60`, `GET` |
| **Sessions** / login tokens | Fast lookups that expire on their own | `SET`, `EXPIRE`, `TTL` |
| **Rate limiting** ("100 requests/min") | Atomic counters with expiry | `INCR`, `EXPIRE` |
| **Leaderboards** | Sorted sets keep members ordered by score | `ZADD`, `ZRANGE` |
| **Queues** / background jobs | Lists work as fast queues | `LPUSH`, `RPOP` |
| **Real-time messaging** | Publish/subscribe channels | `PUBLISH`, `SUBSCRIBE` |

So "Redis" shows up in a lot of backend system-design interview answers. After
this project you'll know what actually happens inside it, not just how to call it.

## The big ideas (each one is a milestone)

1. **It's a network server.** Clients talk to it over TCP using a simple text
   protocol called **RESP**. *(Milestones 1 and 3)*
2. **It's single-threaded, and still very fast.** One thread serves thousands of
   clients with an **event loop** (`epoll` on Linux). There are no locks, because
   only one thread ever touches the data. *(Milestone 2)*
3. **Keys can expire.** Redis deletes expired keys *lazily* (when you touch one)
   and *actively* (a background sweep that samples random keys). *(Milestone 4)*
4. **Values have types.** Strings, lists, hashes, sets and sorted sets each use a
   purpose-built data structure. Sorted sets use a **skip list**. *(Milestone 5)*
5. **Memory isn't durable.** If the process dies, RAM is gone. Redis persists in
   two ways: an **append-only file** (a log of every write) and **snapshots**
   taken with `fork()` and copy-on-write. *(Milestone 6)*
6. **Memory is limited.** With `maxmemory` set, Redis **evicts** keys using an
   approximate LRU/LFU algorithm. *(Milestone 7)*

## What "Redis-compatible" means for us

We speak the same wire protocol as real Redis. So:

- the official `redis-cli` can connect to **our** server;
- any Redis client library (Python's `redis`, Java's Jedis, Node's `ioredis`)
  works against it;
- we can benchmark it against real Redis with the official `redis-benchmark` tool.

We won't build everything Redis has (clustering, Lua scripting, modules). We'll
build the core well, test it thoroughly and measure it honestly.

## Try real Redis first

Once real Redis is installed in your WSL Ubuntu, it's worth playing with it for
five minutes:

```bash
redis-server --port 7000 --daemonize yes   # start real Redis in the background
redis-cli -p 7000
127.0.0.1:7000> SET greeting "hello"
OK
127.0.0.1:7000> GET greeting
"hello"
127.0.0.1:7000> SET temp 1 EX 10     # expires in 10 seconds
127.0.0.1:7000> TTL temp
(integer) 8
127.0.0.1:7000> INCR visits
(integer) 1
127.0.0.1:7000> exit
redis-cli -p 7000 shutdown nosave           # stop it
```

Everything you just did, we'll build.
