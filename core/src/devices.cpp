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

Breaches::Verdict Breaches::Judge(const Key &network, proto::PlayerId player,
                                  const Ledger &record) const {
  if (m_networks.contains(network))
    return Verdict::Breached; // once, whoever asks again (its breacher too)
  // The record has it breached by someone else, or by the host's world:
  // the game would not have let anyone breach it again. The breacher's
  // own copy may have told the host first (its DeviceUse): still its own.
  if (const auto *state = record.Find(network);
      state && (state->flags & proto::kDeviceBreached) && state->by != player)
    return Verdict::Breached;
  if (m_networks.size() >= kMaxDevices)
    return Verdict::Full;
  return Verdict::Granted;
}

Breaches::Verdict Breaches::JudgeNpc(proto::NpcId npc) const {
  if (m_npcs.contains(npc))
    return Verdict::Breached;
  return m_npcs.size() >= kMaxDevices ? Verdict::Full : Verdict::Granted;
}

proto::PlayerId Breaches::By(const Key &network, const Ledger &record) const {
  if (const auto it = m_networks.find(network); it != m_networks.end())
    return it->second;
  if (const auto *state = record.Find(network); state && (state->flags & proto::kDeviceBreached))
    return state->by;
  return proto::kNoPlayer;
}

proto::PlayerId Breaches::ByNpc(proto::NpcId npc) const {
  const auto it = m_npcs.find(npc);
  return it == m_npcs.end() ? proto::kNoPlayer : it->second;
}

proto::DeviceState BreachedState(const Key &network, proto::PlayerId player,
                                 const proto::DeviceState *record) {
  proto::DeviceState state{.entity = network.entity,
                           .cls = network.cls,
                           .status = proto::kDeviceOn,
                           .value = proto::kDeviceNoValue};
  if (record)
    state = *record;
  state.flags |= proto::kDeviceBreached | proto::kDeviceExposed;
  state.by = player;
  state.how = proto::kDeviceLive;
  return state;
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
