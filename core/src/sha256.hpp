#pragma once

// SHA-256 (FIPS 180-4), for the relay's proof of work (relay.cpp): one
// message at a time, no allocation.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace tandem::net {
using Sha256Digest = std::array<std::uint8_t, 32>;

Sha256Digest Sha256(std::span<const std::uint8_t> message);

// The digest's leading zero bits.
int LeadingZeroBits(const Sha256Digest &digest);
} // namespace tandem::net
