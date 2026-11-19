#pragma once

// Devices and world objects (docs/architecture/devices.md): the session's
// record of every device anyone touched or asked about. The host's is the
// authority (its game reports what its devices are, guests' requests are
// judged against it); a guest's is its copy of the host's word.
//
// Platform-independent: the endpoint uses it on both sides, the plugin and
// the mock player on top of it.

#include "tandem/protocol.hpp"

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace tandem::devices {
// A device's name: its entity's static id and its persistent state's class.
struct Key {
  std::uint64_t entity{};
  std::uint64_t cls{};
  friend auto operator<=>(const Key &, const Key &) = default;
};
inline Key KeyOf(const proto::DeviceState &state) { return {state.entity, state.cls}; }
inline Key KeyOf(const proto::DeviceUse &use) { return {use.entity, use.cls}; }

// What a device is (status, state bits, value), whoever said it and how.
struct What {
  std::int8_t status{};
  std::uint16_t flags{};
  std::int32_t value{proto::kDeviceNoValue};
  friend bool operator==(const What &, const What &) = default;
};
inline What WhatOf(const proto::DeviceState &state) {
  return {state.status, state.flags, state.value};
}
inline What WhatOf(const proto::DeviceUse &use) { return {use.status, use.flags, use.value}; }

// The record is bounded: a guest cannot fill the host's memory by asking
// about devices that do not exist.
inline constexpr std::size_t kMaxDevices = 4096;

class Ledger {
public:
  // Host: our game says what a device is now (`state.by`: who made it so).
  // The record to send to everyone when it is new or changed (its revision
  // counted here; quiet when it is new and nobody's doing: the device was
  // like that); nullopt for a repeat, or when the record is full.
  std::optional<proto::DeviceState> Report(proto::DeviceState state);

  // Host: a guest's request to make a device something.
  enum class Verdict {
    Grant, // ours to apply: nobody else changed it since what the guest saw
    Stale, // someone else did: void, the guest gets the record back
    Same,  // it is that already
    Full,  // unknown device and no room for it
  };
  Verdict Judge(const proto::DeviceUse &use) const;

  // Guest: the host's word. False for what we have already (a repeat, a
  // resync of what never changed) or what is older than what we have.
  bool Accept(const proto::DeviceState &state);

  const proto::DeviceState *Find(const Key &key) const;
  const std::map<Key, proto::DeviceState> &All() const { return m_devices; }
  std::size_t Size() const { return m_devices.size(); }
  void Clear() { m_devices.clear(); }
  // The same on the host and on a guest that has every record.
  std::uint32_t Digest() const;

private:
  std::map<Key, proto::DeviceState> m_devices;
};

// Guest: when to ask for the record again. As for the other domains
// (consistency::Checker): it must differ twice in a row, and is asked for at
// most once per cooldown.
class DigestCheck {
public:
  explicit DigestCheck(std::uint64_t cooldownUs = 5'000'000) : m_cooldownUs(cooldownUs) {}
  bool Compare(const proto::DeviceDigest &host, const proto::DeviceDigest &ours,
               std::uint64_t nowUs);
  std::uint32_t Mismatches() const { return m_mismatches; }
  std::uint32_t Requests() const { return m_requests; }

private:
  std::uint64_t m_cooldownUs;
  bool m_last{};
  std::uint64_t m_askedUs{};
  std::uint32_t m_mismatches{}, m_requests{};
};
} // namespace tandem::devices
