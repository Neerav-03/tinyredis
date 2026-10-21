#pragma once

#include <cerrno>
#include <system_error>

namespace tinyredis {

// Throws std::system_error for the current errno, e.g. throw_errno("bind").
[[noreturn]] inline void throw_errno(const char* what) {
  throw std::system_error(errno, std::generic_category(), what);
}

}  // namespace tinyredis
