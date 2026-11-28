#pragma once

// What players do outside combat and quests (docs/architecture/activities.md),
// the parts that need no game: what a received Activity may say, and the
// host's side of a time skip, which is never one player's alone.
//
// A skip is asked (proto::TimeSkip), answered by every other player
// (proto::TimeSkipVote) and concluded by the host (proto::TimeSkipResult).
// The arbiter keeps the one open request: who still has to answer, when it
// lapses, and what a player leaving means (a voter that goes no longer has
// a say; the asker going withdraws it). Nothing moves the clock but an
// Applied result.

#include "tandem/protocol.hpp"

#include <cstdint>
#include <optional>
#include <set>

namespace tandem::activity {
// How long the others have to answer a time skip.
inline constexpr std::uint64_t kSkipAnswerUs = 20000000;
// A skip moves the clock forward, by a day at most (the wheel's own range).
inline constexpr std::int32_t kMaxSkipSeconds = 24 * 3600;

// What a received state may say: an unknown kind is none, none has no
// fields, unknown flags are dropped, a broken anchor is no place to hold a
// body at, only a bed is lain in... (see activity.cpp).
proto::Activity Sanitized(proto::Activity state);

// The game that sent it stands still: its poses stop where they are.
inline bool Frozen(const proto::Activity &state) {
  return state.kind != proto::ActivityKind::None && (state.flags & proto::kActivityFrozen) != 0;
}
// The others hold the puppet at the anchor.
inline bool Anchored(const proto::Activity &state) {
  return state.kind != proto::ActivityKind::None && (state.flags & proto::kActivityAnchored) != 0;
}

// A remote view (ActivityKind::Remote): where its player's view is, when
// it says (a view at the origin is none known).
inline std::optional<proto::Vec3> ViewOf(const proto::Activity &state) {
  if (state.kind != proto::ActivityKind::Remote ||
      (state.anchor.x == 0.f && state.anchor.y == 0.f && state.anchor.z == 0.f))
    return std::nullopt;
  return state.anchor;
}
// The shared vehicle a remote view drives (0: none).
inline std::uint32_t RemoteCar(const proto::Activity &state) {
  return state.kind == proto::ActivityKind::Remote ? state.traffic : 0u;
}

// What stands in the way of a skip, whoever asks (the session's record).
struct Blocker {
  std::uint8_t why{}; // proto::kSkip*; kSkipNoReason: nothing
  proto::PlayerId by{};
};

class SkipArbiter {
public:
  // A request, with the players who must answer (everyone but the asker).
  // Returns the result when it ends at once: refused (another one is open,
  // a blocker, a skip backwards or of more than a day, a guest's "quest"
  // skip), or applied (nobody to ask; the host's story, which only says
  // so). Otherwise it is open and the others are to be asked.
  std::optional<proto::TimeSkipResult> Ask(const proto::TimeSkip &skip, bool fromHost,
                                           std::set<proto::PlayerId> voters, Blocker blocker,
                                           std::uint64_t nowUs);
  // Two beds at once: a player who goes to sleep while another's skip is
  // open, and is still to answer it, wants the hours to pass too. Its own
  // request is refused (one at a time, kSkipBusy, as Ask says) and counts as
  // its yes to the open one (the caller votes it). Never a wheel's (it
  // waits for its own hours), never the asker's own.
  bool Joins(const proto::TimeSkip &skip) const;
  // An answer. The asker's own `false` withdraws its request. Returns the
  // result when it concludes; votes about another request are ignored.
  std::optional<proto::TimeSkipResult> Vote(const proto::TimeSkipVote &vote);
  // A player left or vanished.
  std::optional<proto::TimeSkipResult> Left(proto::PlayerId player);
  // Something now stands in the way (a V went down, a Sandevistan...).
  std::optional<proto::TimeSkipResult> Block(Blocker blocker);
  // Time passes: the request lapses unanswered.
  std::optional<proto::TimeSkipResult> Tick(std::uint64_t nowUs);

  const std::optional<proto::TimeSkip> &Open() const { return m_open; }
  const std::set<proto::PlayerId> &Waiting() const { return m_waiting; }
  // How long the others have to answer (tests make it short).
  void SetAnswerTime(std::uint64_t us) { m_answerUs = us; }
  void Reset() {
    m_open.reset();
    m_waiting.clear();
    m_lapsesAtUs = 0;
  }

private:
  proto::TimeSkipResult Conclude(proto::SkipOutcome outcome, proto::PlayerId by, std::uint8_t why);

  std::optional<proto::TimeSkip> m_open;
  std::set<proto::PlayerId> m_waiting;
  std::uint64_t m_lapsesAtUs{};
  std::uint64_t m_answerUs{kSkipAnswerUs};
};
} // namespace tandem::activity
