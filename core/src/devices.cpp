#include "tandem/devices.hpp"

#include "tandem/consistency.hpp"

#include <algorithm>
#include <cmath>

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

void BreachHolds::Hold(std::uint32_t hold, Target target, std::uint64_t nowUs) {
  for (auto &held : m_holds)
    if (held.hold == hold) {
      held.waiting.push_back(target);
      return;
    }
  m_holds.push_back({.hold = hold, .waiting = {target}, .sinceUs = nowUs});
}

std::optional<BreachHolds::Decision> BreachHolds::Answer(const proto::BreachResult &result) {
  const Target target{{result.entity, result.cls}, result.npc};
  for (auto held = m_holds.begin(); held != m_holds.end(); ++held) {
    const auto waiting = std::ranges::find(held->waiting, target);
    if (waiting == held->waiting.end())
      continue;
    held->waiting.erase(waiting);
    Decision decision{.hold = held->hold, .answered = true};
    if (result.verdict == proto::BreachVerdict::Granted) {
      // Ours for one of them: the breach stands, whatever the others say.
      decision.paid = true;
    } else {
      held->by = result.by;
      if (!held->waiting.empty())
        return std::nullopt; // its other networks may still be ours
      decision.by = held->by;
    }
    m_holds.erase(held);
    return decision;
  }
  return std::nullopt;
}

std::vector<BreachHolds::Decision> BreachHolds::Expire(std::uint64_t nowUs) {
  std::vector<Decision> out;
  std::erase_if(m_holds, [&](const Held &held) {
    if (nowUs < held.sinceUs || nowUs - held.sinceUs < kBreachHoldUs)
      return false;
    out.push_back({.hold = held.hold, .paid = true});
    return true;
  });
  return out;
}

std::vector<BreachHolds::Decision> BreachHolds::End() {
  std::vector<Decision> out;
  for (const auto &held : m_holds)
    out.push_back({.hold = held.hold, .paid = true});
  m_holds.clear();
  return out;
}

namespace {
float Distance(const proto::Vec3 &a, const proto::Vec3 &b) {
  return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) +
                   (a.z - b.z) * (a.z - b.z));
}
bool Named(const proto::Fracture &fracture) {
  return fracture.entity != 0 && fracture.component != 0;
}
} // namespace

const Fractures::Spot *Fractures::Near(const proto::Vec3 &at, float within, std::uint64_t nowUs,
                                       std::uint64_t window, Whose whose) const {
  for (const auto &spot : m_spots) {
    if ((whose == Whose::Ours && !spot.ours) || (whose == Whose::Theirs && spot.ours))
      continue;
    if (nowUs >= spot.atUs && nowUs - spot.atUs <= window && Distance(spot.at, at) <= within)
      return &spot;
  }
  return nullptr;
}

void Fractures::Note(const proto::Fracture &fracture, std::uint64_t nowUs, bool ours) {
  if (Named(fracture)) {
    if (m_parts.size() < kMaxFracturedParts)
      m_parts.emplace(Part{fracture.entity, fracture.component}, ours);
    return;
  }
  m_spots.push_back({fracture.at, nowUs, ours});
  while (m_spots.size() > kMaxFractureSpots ||
         (!m_spots.empty() && nowUs > m_spots.front().atUs &&
          nowUs - m_spots.front().atUs > std::max(kFractureWindowUs, kFractureEchoUs)))
    m_spots.pop_front();
}

bool Fractures::Ours(const proto::Fracture &fracture, std::uint64_t nowUs) {
  if (Named(fracture)) {
    // A part breaks once: the one our replay just broke, or one told already.
    if (const auto known = m_parts.find({fracture.entity, fracture.component});
        known != m_parts.end()) {
      ++(known->second ? m_repeats : m_echoes);
      return false;
    }
  } else if (Near(fracture.at, kFractureEcho, nowUs, kFractureEchoUs, Whose::Theirs)) {
    // A piece without a name of its own is known by its place.
    ++m_echoes;
    return false;
  } else if (Near(fracture.at, kFractureSpot, nowUs, kFractureWindowUs, Whose::Ours)) {
    ++m_repeats;
    return false;
  }
  if (!m_rate.Allow(nowUs)) {
    ++m_held;
    return false;
  }
  Note(fracture, nowUs, true);
  ++m_told;
  return true;
}

bool Fractures::Theirs(const proto::Fracture &fracture, std::uint64_t nowUs) {
  // Broken here already, by our own game or an earlier word: the same piece.
  if (Named(fracture)
          ? m_parts.contains({fracture.entity, fracture.component})
          : Near(fracture.at, kFractureSpot, nowUs, kFractureWindowUs, Whose::Anyone) != nullptr) {
    ++m_repeats;
    return false;
  }
  Note(fracture, nowUs, false);
  ++m_replayed;
  return true;
}

void Fractures::Clear() {
  m_spots.clear();
  m_parts.clear();
  m_rate = comms::Limiter{kFractureBurst, kFracturesPerSecond};
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
