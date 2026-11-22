#include "presence.hpp"

#include <algorithm>
#include <utility>

namespace tandem::plugin {
PresenceState &PresenceState::Get() {
  static PresenceState state;
  return state;
}

bool PresenceState::SubmitLocal(std::uint8_t health, std::uint8_t flags, std::uint64_t nowUs) {
  if (!m_gate.ShouldSend(health, flags, nowUs))
    return false;
  std::lock_guard _(m_lock);
  m_local = proto::Presence{.health = health, .flags = flags};
  return true;
}

std::optional<proto::Presence> PresenceState::TakeLocal() {
  std::lock_guard _(m_lock);
  return std::exchange(m_local, std::nullopt);
}

void PresenceState::SubmitCue(const proto::Cue &cue) {
  std::lock_guard _(m_lock);
  if (m_localCues.size() < 64)
    m_localCues.push_back(cue);
}

std::vector<proto::Cue> PresenceState::TakeLocalCues() {
  std::lock_guard _(m_lock);
  return std::exchange(m_localCues, {});
}

void PresenceState::SubmitHitTaken(const proto::HitTaken &hit) {
  std::lock_guard _(m_lock);
  if (m_localHitsTaken.size() < 32)
    m_localHitsTaken.push_back(hit);
}

std::vector<proto::HitTaken> PresenceState::TakeLocalHitsTaken() {
  std::lock_guard _(m_lock);
  return std::exchange(m_localHitsTaken, {});
}

void PresenceState::PushHitTaken(const proto::HitTaken &hit, std::uint64_t nowUs) {
  std::lock_guard _(m_lock);
  // Scripts drain these every frame; a pile-up means nobody is listening.
  if (m_hitsTaken.size() < 128)
    m_hitsTaken.emplace_back(hit, nowUs);
}

std::vector<proto::HitTaken> PresenceState::TakeHitsTaken(std::uint64_t nowUs) {
  std::lock_guard _(m_lock);
  std::vector<proto::HitTaken> out;
  for (const auto &[hit, at] : std::exchange(m_hitsTaken, {}))
    if (nowUs - at < 500'000)
      out.push_back(hit);
  return out;
}

bool PresenceState::SubmitStatusVisuals(std::vector<std::uint64_t> records) {
  auto set = presence::StatusSet(std::move(records));
  if (set == m_statusSent)
    return false;
  m_statusSent = set;
  std::lock_guard _(m_lock);
  m_statusLocal = std::move(set);
  return true;
}

std::optional<std::vector<std::uint64_t>> PresenceState::TakeStatusVisuals() {
  std::lock_guard _(m_lock);
  return std::exchange(m_statusLocal, std::nullopt);
}

void PresenceState::PushStatusVisuals(const proto::StatusVisuals &visuals) {
  std::lock_guard _(m_lock);
  m_statusRemote.insert_or_assign(visuals.player, visuals.records);
  ++m_statusChanges;
}

std::vector<std::uint64_t> PresenceState::StatusVisualsOf(proto::PlayerId player) {
  std::lock_guard _(m_lock);
  const auto it = m_statusRemote.find(player);
  return it == m_statusRemote.end() ? std::vector<std::uint64_t>{} : it->second;
}

std::uint32_t PresenceState::StatusChanges() {
  std::lock_guard _(m_lock);
  return m_statusChanges;
}

void PresenceState::PushRemote(const proto::Presence &presence) {
  std::lock_guard _(m_lock);
  m_remote.insert_or_assign(presence.player, presence);
}

std::optional<proto::Presence> PresenceState::Remote(proto::PlayerId player) {
  std::lock_guard _(m_lock);
  const auto it = m_remote.find(player);
  return it == m_remote.end() ? std::nullopt : std::optional{it->second};
}

void PresenceState::PushCue(const proto::Cue &cue, std::uint64_t nowUs) {
  std::lock_guard _(m_lock);
  // Scripts drain these every frame; a pile-up means nobody is listening.
  if (m_cues.size() < 256)
    m_cues.emplace_back(cue, nowUs);
}

std::vector<proto::Cue> PresenceState::TakeCues(std::uint64_t nowUs) {
  std::lock_guard _(m_lock);
  std::vector<proto::Cue> out;
  for (const auto &[cue, at] : std::exchange(m_cues, {}))
    if (nowUs - at < 500'000)
      out.push_back(cue);
  return out;
}

void PresenceState::PushReaction(const proto::NpcReaction &reaction) {
  std::lock_guard _(m_lock);
  if (m_reactions.size() < 512)
    m_reactions.push_back(reaction);
}

std::vector<proto::NpcReaction> PresenceState::TakeReactions() {
  std::lock_guard _(m_lock);
  return std::exchange(m_reactions, {});
}

void PresenceState::Flinch(proto::PlayerId player, float strength, const proto::Vec3 &direction,
                           const proto::Vec3 &forward, std::uint64_t nowUs) {
  std::lock_guard _(m_flinchLock);
  m_flinches[player].Hit(strength, direction, forward, nowUs);
}

presence::Lean PresenceState::LeanOf(proto::PlayerId player, std::uint64_t nowUs) {
  std::lock_guard _(m_flinchLock);
  const auto it = m_flinches.find(player);
  return it == m_flinches.end() ? presence::Lean{} : it->second.At(nowUs);
}

void PresenceState::Forget(proto::PlayerId player) {
  {
    std::lock_guard _(m_lock);
    m_remote.erase(player);
    if (m_statusRemote.erase(player))
      ++m_statusChanges;
  }
  // The observer goes on the game thread's next ask for it (a fresh one is
  // made for a namesake): erasing here would race its use.
  std::lock_guard _(m_flinchLock);
  m_flinches.erase(player);
}

void PresenceState::Reset() {
  {
    std::lock_guard _(m_lock);
    // Our own presence stays: it is still true in the next session.
    m_localCues.clear();
    m_remote.clear();
    m_cues.clear();
    m_reactions.clear();
    m_statusRemote.clear();
    ++m_statusChanges;
  }
  std::lock_guard _(m_flinchLock);
  m_flinches.clear();
}
} // namespace tandem::plugin
