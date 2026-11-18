#pragma once

// ENet plumbing shared by the session and the link shaper (core/src only).

#include "resolve.hpp" // ResolveIpv4

#include <enet/enet.h>

namespace tandem::net {
// enet_initialize / enet_deinitialize, counted: every user holds one.
bool AcquireEnet();
void ReleaseEnet();
} // namespace tandem::net
