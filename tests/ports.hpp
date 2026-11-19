#pragma once

// UDP ports for loopback tests, one sequence for the whole test binary.

#include <atomic>
#include <cstdint>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace tandem::test {
// Free right now: nobody has the UDP port bound (another test binary, a
// relay, a game).
inline bool PortFree(std::uint16_t port) {
  const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0)
    return true;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  const bool free = ::bind(fd, reinterpret_cast<const sockaddr *>(&address), sizeof address) == 0;
  ::close(fd);
  return free;
}

// 41000 to 61000 (tests/relay_loopback.sh keeps above), from a place
// apart per process: other test runs may be going on, from this checkout
// (several at once on a busy machine) or another. Consecutive process ids
// start far apart (a whole run takes a few hundred ports), and a port
// someone holds is skipped.
inline std::uint16_t NextPort() {
  constexpr std::uint32_t kFirst = 41000, kCount = 20000;
  static std::atomic<std::uint32_t> next{(static_cast<std::uint32_t>(getpid()) * 7919u) % kCount};
  for (int tries = 0; tries < 64; ++tries) {
    const auto port = static_cast<std::uint16_t>(kFirst + next++ % kCount);
    if (PortFree(port))
      return port;
  }
  return static_cast<std::uint16_t>(kFirst + next++ % kCount);
}
} // namespace tandem::test
