#include "tandem/progress.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace tandem::progress {
std::uint8_t MeanLevel(std::span<const std::uint8_t> levels) {
  if (levels.empty())
    return 0;
  const auto sum = std::accumulate(levels.begin(), levels.end(), std::uint32_t{0});
  const auto count = static_cast<std::uint32_t>(levels.size());
  return static_cast<std::uint8_t>((2 * sum + count) / (2 * count));
}

std::uint8_t SessionLevelOf(std::span<const std::uint8_t> levels, std::uint8_t gap) {
  const auto mean = MeanLevel(levels);
  if (levels.empty() || gap == 0)
    return mean;
  const auto lowest = *std::ranges::min_element(levels);
  return static_cast<std::uint8_t>(std::min<unsigned>(mean, unsigned{lowest} + gap));
}

float NpcShift(const NpcScale &npc) {
  if (npc.sessionLevel <= 0.f || npc.hostLevel <= 0.f || npc.fixed)
    return 0.f;
  const bool curve = npc.curveAtHost >= 0.f && npc.curveAtSession >= 0.f;
  const float wanted =
      curve ? npc.curveAtSession - npc.curveAtHost : npc.sessionLevel - npc.hostLevel;
  // Its level without us. One the game put outside the range stays there.
  const float own = npc.level - npc.applied;
  const float top = std::max(npc.maxLevel, 1.f);
  if (own < 1.f || own > top)
    return 0.f;
  const float shift = std::clamp(own + wanted, 1.f, top) - own;
  return std::fabs(shift) < 0.01f ? 0.f : shift;
}

float ProxyShift(float level, float applied, float hostNpcLevel) {
  if (hostNpcLevel <= 0.f)
    return 0.f; // not told yet: as our game made it
  const float shift = hostNpcLevel - (level - applied);
  return std::fabs(shift) < 0.01f ? 0.f : shift;
}

std::string LeftNames(std::uint32_t left) {
  constexpr const char *names[] = {"story", "difficulty", "bodies", "levels",
                                   "holds", "world",      "down",   "session state"};
  std::string out;
  for (std::size_t bit = 0; bit < std::size(names); ++bit)
    if (left & (1u << bit)) {
      if (!out.empty())
        out += ", ";
      out += names[bit];
    }
  if (left >> std::size(names))
    out += out.empty() ? "other" : ", other";
  return out;
}

bool Leave::Locked() const {
  return m_step == LeaveStep::Session || m_step == LeaveStep::Cleaning ||
         m_step == LeaveStep::Dirty;
}

LeaveAction Leave::Tick(const LeaveInput &input) {
  // The load is on its way: nothing else is ours to decide (Reset follows).
  if (m_step == LeaveStep::Loading)
    return LeaveAction::None;
  if (input.session) {
    // In a session (again): whatever was under way is dropped. A save
    // already asked for is the game's own, of a clean world.
    m_step = LeaveStep::Session;
    m_save = false;
    return LeaveAction::None;
  }
  switch (m_step) {
  case LeaveStep::Solo:
    // The pause menu's Exit after a session that wrote nothing (`stay`, or
    // a save that failed): the save it still owes, now.
    if (input.save && m_owed) {
      m_step = LeaveStep::Waiting;
      m_since = input.now;
      m_save = true;
      m_chosen = false;
    }
    return LeaveAction::None;
  case LeaveStep::Dirty:
  case LeaveStep::Loading:
    return LeaveAction::None;
  case LeaveStep::Session:
    m_step = LeaveStep::Cleaning;
    m_since = input.now;
    m_chosen = input.chosen;
    m_save = input.save;
    m_owed = true;
    return LeaveAction::None;
  case LeaveStep::Cleaning:
    m_chosen = m_chosen || input.chosen;
    m_save = m_save || input.save;
    if (input.now - m_since < kLeaveSettle)
      return LeaveAction::None;
    if (input.left != 0) {
      if (input.now - m_since < kLeaveCleanBy)
        return LeaveAction::None;
      m_step = LeaveStep::Dirty;
      return LeaveAction::Dirty;
    }
    if (m_mode == LeaveMode::Stay && !m_save) {
      m_step = LeaveStep::Solo;
    } else {
      m_step = LeaveStep::Waiting;
      m_since = input.now;
    }
    return LeaveAction::Clean;
  case LeaveStep::Waiting:
    if (input.save && !m_save) {
      // Asked while the game kept us waiting: the shorter wait, from now.
      m_save = true;
      m_since = input.now;
    }
    // Something of the session came back (a late event): not a save.
    if (input.left != 0) {
      m_step = LeaveStep::Cleaning;
      m_since = input.now;
      return LeaveAction::None;
    }
    if (input.blocked) {
      if (input.now - m_since < (m_save ? kLeaveSaveSoonBy : kLeaveSaveBy))
        return LeaveAction::None;
      m_step = LeaveStep::Solo;
      return LeaveAction::GaveUp;
    }
    m_step = LeaveStep::Saving;
    m_since = input.now;
    return LeaveAction::Save;
  case LeaveStep::Saving:
    if (input.saved > 0) {
      m_owed = false;
      // Never a load before an exit or a rejoin: the save is all they need.
      if (m_mode == LeaveMode::Reload && m_chosen && !m_save) {
        m_step = LeaveStep::Loading;
        return LeaveAction::Load;
      }
      m_step = LeaveStep::Solo;
      return LeaveAction::Saved;
    }
    if (input.saved < 0 || input.now - m_since >= kLeaveSavedBy) {
      m_step = LeaveStep::Solo;
      return LeaveAction::SaveFailed;
    }
    return LeaveAction::None;
  }
  return LeaveAction::None;
}

const char *NameOf(LeaveMode mode) {
  constexpr const char *names[] = {"stay", "save", "reload"};
  return names[static_cast<std::size_t>(mode)];
}

const char *NameOf(LeaveStep step) {
  constexpr const char *names[] = {"solo",   "session", "cleaning", "waiting",
                                   "saving", "loading", "dirty"};
  return names[static_cast<std::size_t>(step)];
}

const char *NameOf(LeaveAction action) {
  constexpr const char *names[] = {"none", "clean",       "save",    "saved",
                                   "load", "save failed", "gave up", "dirty"};
  return names[static_cast<std::size_t>(action)];
}
} // namespace tandem::progress
