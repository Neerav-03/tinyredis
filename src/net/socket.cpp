#include "net/socket.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <stdexcept>
#include <system_error>

namespace tinyredis::net {

namespace {

[[noreturn]] void throw_errno(const char* what) {
  throw std::system_error(errno, std::generic_category(), what);
}

}  // namespace

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

  // SOCK_CLOEXEC: don't leak this descriptor into child processes (we fork() for
  // snapshots in a later milestone).
  Fd sock(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
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

bool write_all(int fd, std::string_view data) {
  while (!data.empty()) {
    // MSG_NOSIGNAL: a closed peer gives us EPIPE instead of killing the process
    // with SIGPIPE.
    ssize_t n = ::send(fd, data.data(), data.size(), MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    data.remove_prefix(static_cast<std::size_t>(n));
  }
  return true;
}

}  // namespace tinyredis::net
