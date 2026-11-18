#pragma once

// UDP ports for loopback tests, one sequence for the whole test binary.

#include <atomic>
#include <cstdint>

#include <unistd.h>

namespace tandem::test {
inline std::uint16_t NextPort() {
  // Apart per process: test runs in other checkouts may be going on.
  static std::atomic<std::uint16_t> port{static_cast<std::uint16_t>(41000 + (getpid() % 400) * 50)};
  return port++;
}
} // namespace tandem::test
