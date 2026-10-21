#include "server/server.hpp"

#include <sys/socket.h>

#include <cerrno>
#include <cstdio>

namespace tinyredis::server {

namespace {

using net::EventLoop;

// Real Redis also reads at most 16 KiB per readiness event, so one client
// sending a flood of data can't hog the loop while others wait.
constexpr std::size_t kReadChunk = 16 * 1024;

// Backpressure threshold: stop reading requests from a client once this many
// reply bytes are waiting for it to read (see update_interest).
constexpr std::size_t kMaxPendingOutput = 1024 * 1024;

bool would_block(int err) { return err == EAGAIN || err == EWOULDBLOCK; }

}  // namespace

Server::Server(EventLoop& loop, net::Fd listener) : loop_(loop), listener_(std::move(listener)) {
  loop_.add(listener_.get(), EventLoop::kReadable, [this](std::uint32_t) { on_accept(); });
}

Server::~Server() {
  for (const auto& [fd, client] : clients_) loop_.remove(fd);
  loop_.remove(listener_.get());
}

void Server::on_accept() {
  // Several clients may be queued. Take all of them now instead of one per
  // loop iteration; accept4 returns EAGAIN once the queue is empty.
  for (;;) {
    net::Fd fd(::accept4(listener_.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC));
    if (!fd.valid()) {
      if (errno == EINTR || errno == ECONNABORTED) continue;
      if (!would_block(errno)) std::perror("accept");
      return;
    }
    net::set_nodelay(fd.get());

    auto client = std::make_unique<Client>();
    client->fd = std::move(fd);
    Client& ref = *client;
    const int key = ref.fd.get();
    clients_.emplace(key, std::move(client));
    // Capturing a reference is safe: close_client() removes this handler before
    // destroying the Client, and the loop ignores stale events for it.
    loop_.add(key, EventLoop::kReadable,
              [this, &ref](std::uint32_t events) { on_client_event(ref, events); });
  }
}

void Server::on_client_event(Client& client, std::uint32_t events) {
  if (events & EPOLLERR) {
    close_client(client);
    return;
  }
  // EPOLLHUP: the connection is gone. Reading drains anything it sent first,
  // then returns 0, and we close.
  if ((events & (EventLoop::kReadable | EPOLLHUP)) && client.reading && !read_request(client)) {
    close_client(client);
    return;
  }
  // Try to send replies right away. Usually the kernel takes all of it and we
  // never need to wait for kWritable at all.
  if (!flush(client)) {
    close_client(client);
    return;
  }
  update_interest(client);
}

bool Server::read_request(Client& client) {
  char buf[kReadChunk];
  const ssize_t n = ::recv(client.fd.get(), buf, sizeof(buf), 0);
  if (n > 0) {
    // There's no protocol parser yet (Milestone 3), so, as in Milestone 1,
    // every read gets one PONG.
    client.out += "+PONG\r\n";
    return true;
  }
  if (n == 0) return false;  // the client closed the connection
  return would_block(errno) || errno == EINTR;
}

bool Server::flush(Client& client) {
  while (client.out_sent < client.out.size()) {
    // MSG_NOSIGNAL: a vanished client gives us EPIPE instead of SIGPIPE.
    const ssize_t n = ::send(client.fd.get(), client.out.data() + client.out_sent,
                             client.out.size() - client.out_sent, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR) continue;
      return would_block(errno);  // send buffer full: wait for kWritable
    }
    client.out_sent += static_cast<std::size_t>(n);
  }
  client.out.clear();
  client.out_sent = 0;
  return true;
}

void Server::update_interest(Client& client) {
  const std::size_t pending = client.out.size() - client.out_sent;
  // Only ask for kWritable while there is something to write. Asking all the
  // time would wake us constantly: an idle socket is almost always writable.
  const bool want_write = pending > 0;
  // Backpressure: a client that keeps sending requests but never reads the
  // replies would make us buffer replies without limit. Stop reading from it
  // until it catches up.
  const bool want_read = pending < kMaxPendingOutput;

  if (want_read == client.reading && want_write == client.writing) return;
  client.reading = want_read;
  client.writing = want_write;
  loop_.modify(client.fd.get(),
               (want_read ? EventLoop::kReadable : 0U) | (want_write ? EventLoop::kWritable : 0U));
}

void Server::close_client(Client& client) {
  const int fd = client.fd.get();
  loop_.remove(fd);    // first: epoll must forget the fd before it's closed
  clients_.erase(fd);  // destroys the Client, whose Fd closes the socket
}

}  // namespace tinyredis::server
