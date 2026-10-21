#pragma once

#include <sys/epoll.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "net/socket.hpp"

namespace tinyredis::net {

// A thin wrapper around Linux epoll. Register file descriptors with a callback;
// run() sleeps until some of them are ready and then calls their callbacks.
//
// Level-triggered: as long as a descriptor has unread data (or buffer space, for
// kWritable), every run_once() reports it again.
//
// Single-threaded: call every method from the thread that runs the loop.
class EventLoop {
 public:
  static constexpr std::uint32_t kReadable = EPOLLIN;
  static constexpr std::uint32_t kWritable = EPOLLOUT;

  // Receives the ready events: kReadable / kWritable, plus EPOLLHUP or EPOLLERR.
  using Handler = std::function<void(std::uint32_t events)>;

  EventLoop();

  // Starts watching `fd` for `events`. The loop doesn't own the descriptor:
  // call remove() before closing it.
  void add(int fd, std::uint32_t events, Handler handler);

  // Changes which events `fd` is watched for.
  void modify(int fd, std::uint32_t events);

  // Stops watching `fd`. Safe from inside any handler, including fd's own.
  void remove(int fd);

  // Waits up to `timeout_ms` (-1 = forever, 0 = just poll) and calls the handler
  // of every ready descriptor once. Returns how many handlers ran.
  int run_once(int timeout_ms);

  // Calls run_once() until stop() is called (typically from a handler).
  void run();
  void stop() { running_ = false; }

  std::size_t watched() const { return handlers_.size(); }

 private:
  struct Entry {
    std::uint32_t generation;
    Handler handler;
  };
  using HandlerMap = std::unordered_map<int, Entry>;

  Fd epoll_;
  HandlerMap handlers_;
  // Handlers removed during a dispatch stay alive until it ends, because one of
  // them may be the handler that is running right now.
  std::vector<HandlerMap::node_type> removed_;
  std::vector<epoll_event> ready_;
  std::uint32_t next_generation_ = 0;
  bool running_ = false;
};

}  // namespace tinyredis::net
