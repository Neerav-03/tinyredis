// Milestone 1: a blocking TCP server that answers every request with +PONG.
//
// It serves one client at a time: while one client is connected, everyone else
// waits. docs/learn/01-sockets-and-tcp.md explains why, and Milestone 2 replaces
// this loop with an event loop.

#include <sys/socket.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>

#include "net/socket.hpp"

namespace net = tinyredis::net;

namespace {

struct Options {
  std::string bind = "127.0.0.1";
  std::uint16_t port = 6379;
};

[[noreturn]] void usage(const char* prog, int exit_code) {
  std::fprintf(exit_code == 0 ? stdout : stderr,
               "Usage: %s [--bind ADDR] [--port PORT]\n"
               "  --bind ADDR  IPv4 address to listen on (default 127.0.0.1)\n"
               "  --port PORT  TCP port, 0 picks a free one (default 6379)\n",
               prog);
  std::exit(exit_code);
}

std::uint16_t parse_port(const char* prog, const std::string& text) {
  try {
    std::size_t used = 0;
    unsigned long port = std::stoul(text, &used);
    if (used == text.size() && port <= 65535) return static_cast<std::uint16_t>(port);
  } catch (const std::exception&) {
  }
  std::fprintf(stderr, "invalid port: %s\n", text.c_str());
  usage(prog, 2);
}

Options parse_args(int argc, char** argv) {
  Options opts;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];
    auto next_value = [&]() -> std::string {
      if (i + 1 >= argc) usage(argv[0], 2);
      return argv[++i];
    };

    if (arg == "--bind") {
      opts.bind = next_value();
    } else if (arg == "--port") {
      opts.port = parse_port(argv[0], next_value());
    } else if (arg == "--help" || arg == "-h") {
      usage(argv[0], 0);
    } else {
      std::fprintf(stderr, "unknown option: %s\n", argv[i]);
      usage(argv[0], 2);
    }
  }
  return opts;
}

// Reads until the client hangs up. We don't parse requests yet (Milestone 3),
// so each successful read gets exactly one PONG back.
void serve_client(const net::Fd& client) {
  char buf[4096];
  for (;;) {
    ssize_t n = ::recv(client.get(), buf, sizeof(buf), 0);
    if (n == 0) return;  // client closed the connection
    if (n < 0) {
      if (errno == EINTR) continue;
      std::perror("recv");
      return;
    }
    if (!net::write_all(client.get(), "+PONG\r\n")) return;
  }
}

}  // namespace

int main(int argc, char** argv) {
  // Writing to a disconnected client must not kill the whole server.
  std::signal(SIGPIPE, SIG_IGN);
  const Options opts = parse_args(argc, argv);

  try {
    net::Fd listener = net::listen_tcp(opts.bind, opts.port);
    // The e2e tests start us with --port 0 and read the real port from this line.
    std::printf("tinyredis listening on %s:%u\n", opts.bind.c_str(),
                static_cast<unsigned>(net::local_port(listener)));
    std::fflush(stdout);

    for (;;) {
      net::Fd client(::accept4(listener.get(), nullptr, nullptr, SOCK_CLOEXEC));
      if (!client.valid()) {
        if (errno != EINTR) std::perror("accept");
        continue;
      }
      serve_client(client);
    }  // `client` is closed here, then we accept the next one
  } catch (const std::exception& e) {
    std::fprintf(stderr, "fatal: %s\n", e.what());
    return 1;
  }
}
