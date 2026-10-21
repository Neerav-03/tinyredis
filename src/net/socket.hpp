#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace tinyredis::net {

// Owns a file descriptor and closes it when it goes out of scope (RAII).
// Move-only: two owners of one descriptor would close it twice.
class Fd {
 public:
  Fd() = default;
  explicit Fd(int fd) noexcept : fd_(fd) {}
  ~Fd() { reset(); }

  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;

  Fd(Fd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
  Fd& operator=(Fd&& other) noexcept {
    if (this != &other) reset(std::exchange(other.fd_, -1));
    return *this;
  }

  int get() const noexcept { return fd_; }
  bool valid() const noexcept { return fd_ >= 0; }

  // Gives up ownership without closing.
  int release() noexcept { return std::exchange(fd_, -1); }

  // Closes the current descriptor (if any) and takes ownership of `fd`.
  void reset(int fd = -1) noexcept;

 private:
  int fd_ = -1;
};

// Creates a TCP socket bound to `host:port` (IPv4) and starts listening.
// Port 0 lets the kernel pick a free port; read it back with local_port().
// Throws std::invalid_argument for a bad address, std::system_error otherwise.
Fd listen_tcp(const std::string& host, std::uint16_t port, int backlog = 128);

// Returns the local port a socket is bound to.
std::uint16_t local_port(const Fd& sock);

// Sends all of `data`, retrying short writes and EINTR.
// Returns false if the peer has gone away.
bool write_all(int fd, std::string_view data);

}  // namespace tinyredis::net
