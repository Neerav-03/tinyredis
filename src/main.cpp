// tinyredis-server: a single-threaded server built on an epoll event loop.
// See docs/learn/02-event-loop.md for how it works.

#include <sys/resource.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <string_view>

#include "net/event_loop.hpp"
#include "net/socket.hpp"
#include "server/server.hpp"
#include "util/system_error.hpp"

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

// Every client costs one file descriptor, and the default soft limit is often
// just 1024. Like Redis, raise our soft limit as far as the hard limit allows.
void raise_open_files_limit() {
  rlimit limit{};
  if (::getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_cur < limit.rlim_max) {
    limit.rlim_cur = limit.rlim_max;
    ::setrlimit(RLIMIT_NOFILE, &limit);
  }
}

// Turns SIGINT/SIGTERM into a readable file descriptor, so a shutdown request
// is just another event in the loop instead of an interrupt that can land in
// the middle of anything.
net::Fd open_shutdown_signalfd() {
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);
  // Blocked signals aren't delivered the normal way; they wait for us to read
  // them from the signalfd.
  if (::sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) tinyredis::throw_errno("sigprocmask");
  net::Fd fd(::signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC));
  if (!fd.valid()) tinyredis::throw_errno("signalfd");
  return fd;
}

}  // namespace

int main(int argc, char** argv) {
  // Writing to a disconnected client must not kill the whole server.
  std::signal(SIGPIPE, SIG_IGN);
  const Options opts = parse_args(argc, argv);

  try {
    raise_open_files_limit();
    net::Fd shutdown_signals = open_shutdown_signalfd();

    net::EventLoop loop;
    net::Fd listener = net::listen_tcp(opts.bind, opts.port);
    const unsigned port = net::local_port(listener);
    tinyredis::server::Server server(loop, std::move(listener));

    loop.add(shutdown_signals.get(), net::EventLoop::kReadable, [&](std::uint32_t) {
      signalfd_siginfo info{};
      if (::read(shutdown_signals.get(), &info, sizeof(info)) == sizeof(info)) {
        std::printf("received %s, shutting down\n", strsignal(static_cast<int>(info.ssi_signo)));
      }
      loop.stop();
    });

    // The e2e tests start us with --port 0 and read the real port from this line.
    std::printf("tinyredis listening on %s:%u\n", opts.bind.c_str(), port);
    std::fflush(stdout);

    loop.run();
    loop.remove(shutdown_signals.get());
    std::printf("bye\n");
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "fatal: %s\n", e.what());
    return 1;
  }
}
