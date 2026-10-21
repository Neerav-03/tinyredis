#include "net/event_loop.hpp"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdint>
#include <string>
#include <vector>

namespace tinyredis::net {
namespace {

// A non-blocking pipe: bytes written to `write` become readable on `read`.
struct Pipe {
  Fd read;
  Fd write;

  Pipe() {
    int fds[2];
    EXPECT_EQ(::pipe2(fds, O_NONBLOCK | O_CLOEXEC), 0);
    read.reset(fds[0]);
    write.reset(fds[1]);
  }

  void send_byte() { EXPECT_EQ(::write(write.get(), "x", 1), 1); }
};

TEST(EventLoop, NothingReadyMeansNoCallbacks) {
  EventLoop loop;
  Pipe pipe;
  int calls = 0;
  loop.add(pipe.read.get(), EventLoop::kReadable, [&](std::uint32_t) { ++calls; });

  EXPECT_EQ(loop.run_once(0), 0);
  EXPECT_EQ(calls, 0);
}

TEST(EventLoop, CallsHandlerWhenDataArrives) {
  EventLoop loop;
  Pipe pipe;
  std::uint32_t seen = 0;
  loop.add(pipe.read.get(), EventLoop::kReadable, [&](std::uint32_t events) { seen = events; });

  pipe.send_byte();
  EXPECT_EQ(loop.run_once(0), 1);
  EXPECT_TRUE(seen & EventLoop::kReadable);
}

TEST(EventLoop, LevelTriggeredReportsUnreadDataAgain) {
  EventLoop loop;
  Pipe pipe;
  int calls = 0;
  loop.add(pipe.read.get(), EventLoop::kReadable, [&](std::uint32_t) { ++calls; });

  pipe.send_byte();
  loop.run_once(0);
  loop.run_once(0);  // we never read the byte, so it's still ready
  EXPECT_EQ(calls, 2);
}

TEST(EventLoop, RemovedFdIsNoLongerReported) {
  EventLoop loop;
  Pipe pipe;
  int calls = 0;
  loop.add(pipe.read.get(), EventLoop::kReadable, [&](std::uint32_t) { ++calls; });
  loop.remove(pipe.read.get());

  pipe.send_byte();
  EXPECT_EQ(loop.run_once(0), 0);
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(loop.watched(), 0U);
}

TEST(EventLoop, ModifyChangesWatchedEvents) {
  EventLoop loop;
  Pipe pipe;
  int calls = 0;
  // A pipe's write end never becomes readable...
  loop.add(pipe.write.get(), EventLoop::kReadable, [&](std::uint32_t) { ++calls; });
  EXPECT_EQ(loop.run_once(0), 0);

  // ...but it's writable as soon as we ask for that.
  loop.modify(pipe.write.get(), EventLoop::kWritable);
  EXPECT_EQ(loop.run_once(0), 1);
  EXPECT_EQ(calls, 1);
}

TEST(EventLoop, HandlerCanRemoveItself) {
  EventLoop loop;
  Pipe pipe;
  std::vector<std::string> log;
  const int fd = pipe.read.get();
  // The captured string lives inside the handler. If remove() destroyed the
  // handler while it was running, using `label` afterwards would be a
  // use-after-free, which the sanitizer build catches.
  const std::string label = "a label long enough to need a heap allocation";
  loop.add(fd, EventLoop::kReadable, [&loop, &log, fd, label](std::uint32_t) {
    loop.remove(fd);
    log.push_back(label);
  });

  pipe.send_byte();
  EXPECT_EQ(loop.run_once(0), 1);
  EXPECT_EQ(log, std::vector<std::string>{label});
  EXPECT_EQ(loop.run_once(0), 0);
}

TEST(EventLoop, SkipsStaleEventWhenFdNumberIsReusedInSameBatch) {
  EventLoop loop;
  Pipe a;
  Pipe b;
  Pipe fresh;  // never written to, so never readable
  int original_calls = 0;
  int fresh_calls = 0;

  // Both pipes are readable, so a single epoll_wait returns both events.
  // Whichever handler runs first replaces the *other* pipe with `fresh`, under
  // the same fd number, just like closing a client and accepting a new one
  // that gets the recycled number. The other event in the batch is then stale:
  // it must not reach the new handler.
  bool replaced = false;
  auto replace = [&](Pipe& victim) {
    replaced = true;
    const int number = victim.read.get();
    loop.remove(number);
    ASSERT_EQ(::dup2(fresh.read.get(), number), number);  // `number` now points at `fresh`
    loop.add(number, EventLoop::kReadable, [&](std::uint32_t) { ++fresh_calls; });
  };
  loop.add(a.read.get(), EventLoop::kReadable, [&](std::uint32_t) {
    ++original_calls;
    if (!replaced) replace(b);
  });
  loop.add(b.read.get(), EventLoop::kReadable, [&](std::uint32_t) {
    ++original_calls;
    if (!replaced) replace(a);
  });

  a.send_byte();
  b.send_byte();
  EXPECT_EQ(loop.run_once(0), 1);
  EXPECT_EQ(original_calls, 1);
  EXPECT_EQ(fresh_calls, 0);
}

TEST(EventLoop, StopEndsRun) {
  EventLoop loop;
  Pipe pipe;
  loop.add(pipe.read.get(), EventLoop::kReadable, [&](std::uint32_t) { loop.stop(); });

  pipe.send_byte();
  loop.run();  // returns instead of looping forever
  SUCCEED();
}

}  // namespace
}  // namespace tinyredis::net
