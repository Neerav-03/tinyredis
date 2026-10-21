#include "net/socket.hpp"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <stdexcept>
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

TEST(ListenTcp, ListenerIsNonBlocking) {
  Fd sock = listen_tcp("127.0.0.1", 0);
  EXPECT_TRUE(::fcntl(sock.get(), F_GETFL) & O_NONBLOCK);
  // Nobody is connecting, so accept() must return right away instead of hanging.
  EXPECT_EQ(::accept(sock.get(), nullptr, nullptr), -1);
  EXPECT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
}

TEST(SetNodelay, EnablesTcpNodelay) {
  Fd listener = listen_tcp("127.0.0.1", 0);
  Fd client(::socket(AF_INET, SOCK_STREAM, 0));
  ASSERT_TRUE(client.valid());

  set_nodelay(client.get());
  int value = 0;
  socklen_t len = sizeof(value);
  ASSERT_EQ(::getsockopt(client.get(), IPPROTO_TCP, TCP_NODELAY, &value, &len), 0);
  EXPECT_NE(value, 0);
}

}  // namespace
}  // namespace tinyredis::net
