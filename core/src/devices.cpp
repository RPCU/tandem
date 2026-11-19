#include "tandem/devices.hpp"

#include "tandem/consistency.hpp"

namespace tandem::devices {
std::optional<proto::DeviceState> Ledger::Report(proto::DeviceState state) {
  state.how = proto::kDeviceLive;
  const auto key = KeyOf(state);
  const auto it = m_devices.find(key);
  if (it == m_devices.end()) {
    if (m_devices.size() >= kMaxDevices)
      return std::nullopt;
    state.revision = 1;
    m_devices.emplace(key, state);
    // The session learns what the device is (someone asked): a record to
    // set. With a player's name on it, that player just made it so.
    if (state.by == proto::kNoPlayer)
      state.how = proto::kDeviceQuiet;
    return state;
  }
  if (WhatOf(it->second) == WhatOf(state))
    return std::nullopt;
  state.revision = it->second.revision + 1;
  it->second = state;
  return state;
}

Ledger::Verdict Ledger::Judge(const proto::DeviceUse &use) const {
  const auto it = m_devices.find(KeyOf(use));
  if (it == m_devices.end())
    return m_devices.size() >= kMaxDevices ? Verdict::Full : Verdict::Grant;
  const auto &record = it->second;
  if (WhatOf(record) == WhatOf(use))
    return Verdict::Same;
  // The last change was the asker's own: what it has not seen yet is its
  // own doing (two quick actions in a row), not someone else's.
  if (use.seen >= record.revision || (record.by != proto::kNoPlayer && record.by == use.player))
    return Verdict::Grant;
  return Verdict::Stale;
}

bool Ledger::Accept(const proto::DeviceState &state) {
  const auto key = KeyOf(state);
  auto kept = state;
  kept.how = proto::kDeviceLive;
  const auto it = m_devices.find(key);
  if (it == m_devices.end()) {
    if (m_devices.size() >= kMaxDevices)
      return false;
    m_devices.emplace(key, kept);
    return true;
  }
  if (state.revision < it->second.revision || it->second == kept)
    return false;
  it->second = kept;
  return true;
}

const proto::DeviceState *Ledger::Find(const Key &key) const {
  const auto it = m_devices.find(key);
  return it == m_devices.end() ? nullptr : &it->second;
}

std::uint32_t Ledger::Digest() const {
  consistency::Hasher hash;
  for (const auto &[key, state] : m_devices)
    hash.Add(key.entity)
        .Add(key.cls)
        .Add(static_cast<std::uint64_t>(static_cast<std::uint8_t>(state.status)))
        .Add(std::uint64_t{state.flags})
        .Add(static_cast<std::uint64_t>(static_cast<std::uint32_t>(state.value)))
        .Add(std::uint64_t{state.revision});
  return hash.Value();
}

bool DigestCheck::Compare(const proto::DeviceDigest &host, const proto::DeviceDigest &ours,
                          std::uint64_t nowUs) {
  const bool differ = !(host == ours);
  if (differ)
    ++m_mismatches;
  // Twice in a row: the first may have raced states still on their way.
  const bool twice = differ && m_last;
  m_last = differ;
  if (!twice || (m_askedUs != 0 && nowUs - m_askedUs < m_cooldownUs))
    return false;
  m_askedUs = nowUs;
  m_last = false;
  ++m_requests;
  return true;
}
} // namespace tandem::devices
