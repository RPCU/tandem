#pragma once

// ENet plumbing shared by the session and the link shaper (core/src only).

#include <enet/enet.h>

#include <string>

namespace tandem::net {
// enet_initialize / enet_deinitialize, counted: every user holds one.
bool AcquireEnet();
void ReleaseEnet();

// The name's IPv4 address into `address.host` (ENet speaks IPv4 only).
bool ResolveIpv4(const std::string &name, ENetAddress &address);
} // namespace tandem::net
