#include "net/socket.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <stdexcept>

#include "util/system_error.hpp"

namespace tinyredis::net {

void Fd::reset(int fd) noexcept {
  if (fd_ >= 0) ::close(fd_);
  fd_ = fd;
}

Fd listen_tcp(const std::string& host, std::uint16_t port, int backlog) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    throw std::invalid_argument("not an IPv4 address: " + host);
  }

  // SOCK_NONBLOCK: accept() returns EAGAIN instead of blocking when nobody is
  // waiting, which the event loop relies on.
  // SOCK_CLOEXEC: don't leak this descriptor into child processes (we fork() for
  // snapshots in a later milestone).
  Fd sock(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
  if (!sock.valid()) throw_errno("socket");

  // Lets us restart the server right away instead of waiting out TIME_WAIT.
  int one = 1;
  if (::setsockopt(sock.get(), SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
    throw_errno("setsockopt(SO_REUSEADDR)");
  }

  if (::bind(sock.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
    throw_errno("bind");
  }
  if (::listen(sock.get(), backlog) < 0) throw_errno("listen");
  return sock;
}

std::uint16_t local_port(const Fd& sock) {
  sockaddr_in addr{};
  socklen_t len = sizeof(addr);
  if (::getsockname(sock.get(), reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
    throw_errno("getsockname");
  }
  return ntohs(addr.sin_port);
}

void set_nodelay(int fd) {
  // Best effort: if it fails we're only slower, not wrong.
  int one = 1;
  ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

}  // namespace tinyredis::net
