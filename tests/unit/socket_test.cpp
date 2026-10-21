#include "net/socket.hpp"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <stdexcept>
#include <string>
#include <system_error>

namespace tinyredis::net {
namespace {

bool is_open(int fd) { return ::fcntl(fd, F_GETFD) != -1 || errno != EBADF; }

TEST(Fd, ClosesDescriptorWhenDestroyed) {
  int fds[2];
  ASSERT_EQ(::pipe(fds), 0);
  ::close(fds[1]);
  {
    Fd owner(fds[0]);
    EXPECT_TRUE(is_open(fds[0]));
  }
  EXPECT_FALSE(is_open(fds[0]));
}

TEST(Fd, MoveTransfersOwnership) {
  int fds[2];
  ASSERT_EQ(::pipe(fds), 0);
  ::close(fds[1]);

  Fd a(fds[0]);
  Fd b(std::move(a));
  EXPECT_FALSE(a.valid());  // NOLINT(bugprone-use-after-move): checking the moved-from state
  EXPECT_EQ(b.get(), fds[0]);

  Fd c;
  c = std::move(b);
  EXPECT_EQ(c.get(), fds[0]);
  EXPECT_TRUE(is_open(fds[0]));
}

TEST(Fd, ReleaseGivesUpOwnershipWithoutClosing) {
  int fds[2];
  ASSERT_EQ(::pipe(fds), 0);
  ::close(fds[1]);

  int raw = -1;
  {
    Fd owner(fds[0]);
    raw = owner.release();
  }
  EXPECT_TRUE(is_open(raw));
  ::close(raw);
}

TEST(ListenTcp, PortZeroPicksAFreePort) {
  Fd sock = listen_tcp("127.0.0.1", 0);
  EXPECT_TRUE(sock.valid());
  EXPECT_NE(local_port(sock), 0);
}

TEST(ListenTcp, RejectsInvalidAddress) {
  EXPECT_THROW(listen_tcp("not-an-ip", 0), std::invalid_argument);
}

TEST(ListenTcp, PortAlreadyInUseThrows) {
  Fd first = listen_tcp("127.0.0.1", 0);
  EXPECT_THROW(listen_tcp("127.0.0.1", local_port(first)), std::system_error);
}

TEST(WriteAll, SendsEveryByte) {
  int pair[2];
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
  Fd writer(pair[0]);
  Fd reader(pair[1]);

  const std::string msg = "+PONG\r\n";
  ASSERT_TRUE(write_all(writer.get(), msg));

  char buf[16] = {};
  ASSERT_EQ(::recv(reader.get(), buf, sizeof(buf), 0), static_cast<ssize_t>(msg.size()));
  EXPECT_EQ(std::string(buf, msg.size()), msg);
}

TEST(WriteAll, ReturnsFalseWhenPeerIsGone) {
  int pair[2];
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
  Fd writer(pair[0]);
  ::close(pair[1]);

  EXPECT_FALSE(write_all(writer.get(), "hello"));
}

}  // namespace
}  // namespace tinyredis::net
