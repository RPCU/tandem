#include "tandem/openworld.hpp"

#include <algorithm>
#include <iterator>

namespace tandem::openworld {
std::vector<std::uint32_t> DoneOf(std::span<const Seen> ours) {
  std::vector<std::uint32_t> done;
  for (const auto &seen : ours)
    if (seen.marker != 0 && seen.phase == kCompleted)
      done.push_back(seen.marker);
  std::ranges::sort(done);
  done.erase(std::unique(done.begin(), done.end()), done.end());
  if (done.size() > proto::kMaxOpenWorldDone)
    done.resize(proto::kMaxOpenWorldDone);
  return done;
}

std::uint8_t Wanted(bool doneThere, std::uint8_t own) {
  if (doneThere)
    return kCompleted;
  return own == kCompleted ? kDiscovered : own;
}

std::vector<std::uint32_t> Mirror::Host(const proto::OpenWorldDone &message) {
  auto set = message.markers;
  std::erase(set, std::uint32_t{0});
  std::ranges::sort(set);
  set.erase(std::unique(set.begin(), set.end()), set.end());
  std::vector<std::uint32_t> added;
  if (m_heard)
    std::ranges::set_difference(set, m_host, std::back_inserter(added));
  m_host = std::move(set);
  m_heard = true;
  return added;
}

std::vector<Step> Mirror::Update(bool grouped, std::span<const Seen> ours) {
  if (!grouped)
    return Release();
  std::vector<Step> steps;
  // Nothing heard of the host's world yet: our pins stay ours (an empty set
  // would open every hustle our own game finished).
  if (!m_heard)
    return steps;
  for (const auto &seen : ours) {
    if (seen.marker == 0 || seen.phase >= kPhases)
      continue;
    const auto it = m_ours.find(seen.marker);
    const auto own = it != m_ours.end() ? it->second : seen.phase;
    const auto want = Wanted(std::ranges::binary_search(m_host, seen.marker), own);
    if (want == own) {
      // The host's state is ours again: put back, and ours no longer.
      if (it != m_ours.end()) {
        const auto back = Back(seen.marker, own);
        if (seen.phase != own || back.track)
          steps.push_back(back);
        m_ours.erase(it);
      }
      continue;
    }
    if (it == m_ours.end())
      m_ours.emplace(seen.marker, seen.phase);
    else if (seen.phase != want)
      ++m_reapplied; // our game put it back (streamed in again)
    if (seen.phase != want) {
      // The engine's setter untracks a pin set done: the player's track is
      // given back with the pin.
      if (want == kCompleted && seen.tracked)
        m_untracked.insert(seen.marker);
      steps.push_back({seen.marker, want});
    }
  }
  return steps;
}

std::vector<Step> Mirror::Lift() {
  auto steps = Release();
  m_host.clear();
  m_heard = false;
  return steps;
}

std::vector<Step> Mirror::Release() {
  std::vector<Step> steps;
  steps.reserve(m_ours.size());
  for (const auto &[marker, own] : m_ours)
    steps.push_back(Back(marker, own));
  m_ours.clear();
  m_untracked.clear();
  return steps;
}

Step Mirror::Back(std::uint32_t marker, std::uint8_t own) {
  // A pin is only ever untracked by being set done, and its own phase is
  // then not done (else it would not have been set): it can be tracked.
  const bool track = m_untracked.erase(marker) > 0 && own != kCompleted;
  return {marker, own, track};
}
} // namespace tandem::openworld
