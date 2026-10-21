#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "net/event_loop.hpp"
#include "net/socket.hpp"

namespace tinyredis::server {

// Accepts clients on a listening socket and serves all of them from one
// EventLoop, on one thread. Nothing ever blocks: reads and writes only happen
// when epoll says the socket is ready, and replies that don't fit in the
// socket's send buffer wait in a per-client output buffer.
class Server {
 public:
  // `listener` must be a non-blocking listening socket (see net::listen_tcp).
  // `loop` must outlive the server.
  Server(net::EventLoop& loop, net::Fd listener);
  ~Server();

  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  std::size_t client_count() const { return clients_.size(); }

 private:
  struct Client {
    net::Fd fd;
    std::string out;           // reply bytes not yet written to the socket
    std::size_t out_sent = 0;  // how much of `out` the kernel has already taken
    bool reading = true;       // watching kReadable (false while paused, see update_interest)
    bool writing = false;      // watching kWritable (only while `out` has unsent bytes)
  };

  void on_accept();
  void on_client_event(Client& client, std::uint32_t events);
  bool read_request(Client& client);  // false: close the client
  bool flush(Client& client);         // false: close the client
  void update_interest(Client& client);
  void close_client(Client& client);

  net::EventLoop& loop_;
  net::Fd listener_;
  std::unordered_map<int, std::unique_ptr<Client>> clients_;
};

}  // namespace tinyredis::server
