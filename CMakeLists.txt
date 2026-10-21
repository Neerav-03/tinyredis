cmake_minimum_required(VERSION 3.22)
project(tinyredis VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TINYREDIS_WERROR "Treat compiler warnings as errors" OFF)
option(TINYREDIS_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(TINYREDIS_BUILD_TESTS "Build the unit tests" ON)

# Warnings are attached to our own targets only, so third-party headers stay quiet.
add_library(tinyredis_warnings INTERFACE)
target_compile_options(tinyredis_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow
  $<$<BOOL:${TINYREDIS_WERROR}>:-Werror>)

if(TINYREDIS_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# Everything except main() lives in a library so the tests can link against it.
add_library(tinyredis_core
  src/net/socket.cpp
)
target_include_directories(tinyredis_core PUBLIC src)
target_link_libraries(tinyredis_core PRIVATE tinyredis_warnings)

add_executable(tinyredis-server src/main.cpp)
target_link_libraries(tinyredis-server PRIVATE tinyredis_core tinyredis_warnings)

if(TINYREDIS_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)
  include(GoogleTest)

  add_executable(unit_tests
    tests/unit/socket_test.cpp
  )
  target_link_libraries(unit_tests PRIVATE tinyredis_core tinyredis_warnings GTest::gtest_main)
  gtest_discover_tests(unit_tests)
endif()
