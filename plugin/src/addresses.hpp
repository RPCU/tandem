#pragma once

// This machine's IPv4 addresses, for the co-op menu's invite: the one on the
// default route first (what a LAN friend reaches), then every other adapter
// that is up (Tailscale, NetBird, a second LAN). Loopback and link-local
// addresses are left out. Plain Winsock and IP Helper calls: no engine.

#include <string>
#include <vector>

namespace tandem::plugin {
std::vector<std::string> LocalAddresses();
}
