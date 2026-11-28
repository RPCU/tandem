#pragma once

// The host's open-world activities on a guest's map (docs/architecture/story.md,
// "Open-world activities"): the NCPD scanner activities (hustles, reported
// crimes, gang watches, outposts, hidden stashes), each by its point-of-
// interest mappin's journal path hash, done when that mappin's phase is
// CompletedPhase. Rules only: the scripts read the pins, the plugin sets a
// pin's phase the engine's way.

#include "tandem/protocol.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <vector>

namespace tandem::openworld {
// gamedataMappinPhase (final.reds (2.50):1333).
inline constexpr std::uint8_t kCompleted = 0;
inline constexpr std::uint8_t kDefault = 1;
inline constexpr std::uint8_t kDiscovered = 2;
inline constexpr std::uint8_t kUndiscovered = 3;
inline constexpr std::uint8_t kPhases = 4;

// One of our own activity pins as our game shows it now.
struct Seen {
  std::uint32_t marker{}; // its journal path hash (never 0)
  std::uint8_t phase{};   // gamedataMappinPhase
  bool tracked{};         // the player tracks it (IMappin.IsPlayerTracked)
};
// One pin to set: `marker` to `phase`; `track`: tracked again after (our
// setter untracked it when it showed the host's done, §13.167).
struct Step {
  std::uint32_t marker{};
  std::uint8_t phase{};
  bool track{};
  friend bool operator==(const Step &, const Step &) = default;
};

// Host: what its world has done, as OpenWorldDone says it (sorted, each
// once, no 0, at most kMaxOpenWorldDone).
std::vector<std::uint32_t> DoneOf(std::span<const Seen> ours);

// The phase the host's world gives our pin: done there, done; done here but
// open there, discovered (it is there to play, with the host's game running
// it); else our own.
std::uint8_t Wanted(bool doneThere, std::uint8_t own);

// A guest's rule. Grouped with the host, each of our activity pins shows the
// host's state; the ones we changed remember their own phase, and are the
// only ones we ever put back: when the host's state matches ours again, when
// we go apart, when we leave. Our game putting a pin back (it streamed in
// again from its own record) while grouped is set again. A pin the player
// tracked when we set it done (the engine's setter untracks a completed pin)
// is tracked again when it is put back. Nothing here is our save's: the save
// keeps what our own game did (the session never saves).
class Mirror {
public:
  // The host's set heard. Answers what it adds to the set heard before it
  // (none for the first set of a session: that is the host's past, not done
  // with us), for the ledger.
  std::vector<std::uint32_t> Host(const proto::OpenWorldDone &message);
  const std::vector<std::uint32_t> &HostSet() const { return m_host; }
  // Each check: what to set now (the plugin sets the pins it finds).
  std::vector<Step> Update(bool grouped, std::span<const Seen> ours);
  // The session is over or the host vanished: every pin we changed back to
  // its own phase, the host's set forgotten.
  std::vector<Step> Lift();
  // Ours: marker -> its own phase from before we changed it.
  const std::map<std::uint32_t, std::uint8_t> &Ours() const { return m_ours; }
  // Ours set again after our game put them back.
  std::uint32_t Reapplied() const { return m_reapplied; }
  // Ours the player tracked when we set them done: tracked again at the
  // put-back.
  const std::set<std::uint32_t> &Untracked() const { return m_untracked; }

private:
  std::vector<Step> Release();
  Step Back(std::uint32_t marker, std::uint8_t own);

  std::vector<std::uint32_t> m_host;
  bool m_heard{};
  std::map<std::uint32_t, std::uint8_t> m_ours;
  std::set<std::uint32_t> m_untracked;
  std::uint32_t m_reapplied{};
};
} // namespace tandem::openworld
