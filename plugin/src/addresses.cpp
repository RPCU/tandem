#include "addresses.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <iphlpapi.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace tandem::plugin {
namespace {
std::string Text(const in_addr &address) {
  char text[INET_ADDRSTRLEN]{};
  return inet_ntop(AF_INET, &address, text, sizeof(text)) ? std::string(text) : std::string{};
}

bool Shareable(const in_addr &address) {
  const auto first = static_cast<std::uint8_t>(address.S_un.S_un_b.s_b1);
  const auto second = static_cast<std::uint8_t>(address.S_un.S_un_b.s_b2);
  return first != 0 && first != 127 && !(first == 169 && second == 254);
}

// The address the system would send from to reach the internet: a UDP
// "connect" sends nothing, it only picks the route.
std::string RouteAddress() {
  const SOCKET probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (probe == INVALID_SOCKET)
    return {};
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(53);
  inet_pton(AF_INET, "8.8.8.8", &to.sin_addr);
  std::string out;
  sockaddr_in local{};
  int size = sizeof(local);
  if (connect(probe, reinterpret_cast<const sockaddr *>(&to), sizeof(to)) == 0 &&
      getsockname(probe, reinterpret_cast<sockaddr *>(&local), &size) == 0 &&
      Shareable(local.sin_addr))
    out = Text(local.sin_addr);
  closesocket(probe);
  return out;
}
} // namespace

std::vector<std::string> LocalAddresses() {
  WSADATA wsa{};
  const bool started = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
  std::vector<std::string> out;
  if (auto route = RouteAddress(); !route.empty())
    out.push_back(std::move(route));

  ULONG size = 16 * 1024;
  std::vector<std::uint8_t> buffer(size);
  constexpr ULONG kFlags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                           GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME;
  auto *adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());
  ULONG result = GetAdaptersAddresses(AF_INET, kFlags, nullptr, adapters, &size);
  if (result == ERROR_BUFFER_OVERFLOW) {
    buffer.resize(size);
    adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());
    result = GetAdaptersAddresses(AF_INET, kFlags, nullptr, adapters, &size);
  }
  if (result == NO_ERROR) {
    for (auto *adapter = adapters; adapter; adapter = adapter->Next) {
      if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
        continue;
      for (auto *unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
        const auto *address = unicast->Address.lpSockaddr;
        if (!address || address->sa_family != AF_INET)
          continue;
        const auto &v4 = reinterpret_cast<const sockaddr_in *>(address)->sin_addr;
        auto text = Text(v4);
        if (Shareable(v4) && !text.empty() && std::ranges::find(out, text) == out.end())
          out.push_back(std::move(text));
      }
    }
  }
  if (started)
    WSACleanup();
  return out;
}
} // namespace tandem::plugin
