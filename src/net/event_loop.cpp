#include "net/event_loop.hpp"

#include <cerrno>
#include <stdexcept>

#include "util/system_error.hpp"

namespace tinyredis::net {

namespace {

constexpr std::size_t kInitialBatch = 256;

// epoll hands back 64 bits of our choosing with every event. We store the fd
// *and* a generation number, so an event for a descriptor that was closed and
// whose number was reused (all within one batch) can be recognised as stale.
std::uint64_t pack(int fd, std::uint32_t generation) {
  return (std::uint64_t{generation} << 32) | static_cast<std::uint32_t>(fd);
}

}  // namespace

EventLoop::EventLoop() : epoll_(::epoll_create1(EPOLL_CLOEXEC)), ready_(kInitialBatch) {
  if (!epoll_.valid()) throw_errno("epoll_create1");
}

void EventLoop::add(int fd, std::uint32_t events, Handler handler) {
  const std::uint32_t generation = next_generation_++;
  epoll_event ev{};
  ev.events = events;
  ev.data.u64 = pack(fd, generation);
  if (::epoll_ctl(epoll_.get(), EPOLL_CTL_ADD, fd, &ev) < 0) throw_errno("epoll_ctl(ADD)");
  handlers_.insert_or_assign(fd, Entry{generation, std::move(handler)});
}

void EventLoop::modify(int fd, std::uint32_t events) {
  auto it = handlers_.find(fd);
  if (it == handlers_.end()) throw std::logic_error("EventLoop::modify: fd is not watched");
  epoll_event ev{};
  ev.events = events;
  ev.data.u64 = pack(fd, it->second.generation);
  if (::epoll_ctl(epoll_.get(), EPOLL_CTL_MOD, fd, &ev) < 0) throw_errno("epoll_ctl(MOD)");
}

void EventLoop::remove(int fd) {
  auto it = handlers_.find(fd);
  if (it == handlers_.end()) return;
  // Can only fail if fd was already closed, in which case the kernel already
  // dropped it from the epoll set.
  ::epoll_ctl(epoll_.get(), EPOLL_CTL_DEL, fd, nullptr);
  // extract() unlinks the node without moving or destroying the handler, so a
  // handler that removes itself keeps running safely.
  removed_.push_back(handlers_.extract(it));
}

int EventLoop::run_once(int timeout_ms) {
  const int n =
      ::epoll_wait(epoll_.get(), ready_.data(), static_cast<int>(ready_.size()), timeout_ms);
  if (n < 0) {
    if (errno == EINTR) return 0;
    throw_errno("epoll_wait");
  }

  int dispatched = 0;
  for (int i = 0; i < n; ++i) {
    const std::uint64_t key = ready_[static_cast<std::size_t>(i)].data.u64;
    const int fd = static_cast<int>(key & 0xffffffffU);
    const auto generation = static_cast<std::uint32_t>(key >> 32);

    // An earlier handler in this batch may have removed this fd, and maybe
    // registered a different descriptor under the same number. Either way the
    // event isn't meant for whatever is registered now.
    auto it = handlers_.find(fd);
    if (it == handlers_.end() || it->second.generation != generation) continue;

    // Handlers may add or remove entries. That can rehash the map, but elements
    // of an unordered_map never move, so the handler we're calling stays put.
    it->second.handler(ready_[static_cast<std::size_t>(i)].events);
    ++dispatched;
  }
  removed_.clear();

  // A full batch suggests more were waiting; take bigger bites next time.
  if (static_cast<std::size_t>(n) == ready_.size()) ready_.resize(ready_.size() * 2);
  return dispatched;
}

void EventLoop::run() {
  running_ = true;
  while (running_) run_once(-1);
}

}  // namespace tinyredis::net
