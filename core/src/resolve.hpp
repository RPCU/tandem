#pragma once

#include <enet/enet.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <string>

namespace tandem::net {
// The name's IPv4 address (ENet speaks IPv4 only). ENet's own lookup missed
// it for a host whose name also resolves to IPv6 addresses (2026-11-17).
inline bool ResolveIpv4(const std::string &name, ENetAddress &address) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo *found = nullptr;
  if (getaddrinfo(name.c_str(), nullptr, &hints, &found) != 0 || !found)
    return false;
  address.host = reinterpret_cast<const sockaddr_in *>(found->ai_addr)->sin_addr.s_addr;
  freeaddrinfo(found);
  return true;
}
} // namespace tandem::net
