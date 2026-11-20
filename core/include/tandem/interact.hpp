#pragma once

// Quest interactions (docs/architecture/story.md, "Quest interactions"): a
// guest's V uses an object of the host's world (grouped, a placed entity),
// and the host's game does it on its own copy so its quest moves, or says
// the host must do it. The parts that need no game: the verdicts each side
// keeps (so the second ask is instant and a host-only prompt is greyed
// before anyone presses it), first come first served per object, a guest
// that vanishes mid-request, and the reach check with lag compensation.
//
// No list of quests or objects anywhere: the host's game judges each
// choice from its own data (Interact.reds, engine/casts.cpp). The override
// lists of tandem.ini (quest_interact_hostonly, quest_interact_allow) are a
// safety valve and stay empty.

#include "tandem/lagcomp.hpp"
#include "tandem/protocol.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>

namespace tandem::interact {
// How far a V reaches an object (metres): the game's interaction ranges are
// a couple of metres from the object's own shape; the margin is its size
// and a step.
inline constexpr float kReachM = 4.f;
// How far the guest's word on where its V stood may be from the host's own
// track of that V (its states, rewound to the moment): a step and a half.
inline constexpr float kClaimToleranceM = 2.f;
// A use that was answered keeps its object this long (a second player's ask
// in the same moment loses: the object changed under it).
inline constexpr std::uint64_t kHoldUs = 2'000'000;
// A question the host's game never answered is let go after this.
inline constexpr std::uint64_t kPendingUs = 5'000'000;
// Guest: a question with no answer after this is its own again (its game
// treats the object as solo would; the log says so).
inline constexpr std::uint64_t kAnswerWaitUs = 3'000'000;
inline constexpr std::size_t kMaxKnown = 2048;
inline constexpr std::size_t kMaxPending = 64;

// One object and one kind of choice on it.
struct Key {
  std::uint64_t target{};
  std::uint64_t choice{};
  std::uint64_t detail{};
  friend auto operator<=>(const Key &, const Key &) = default;
};
inline Key KeyOf(const proto::QuestInteract &m) { return {m.target, m.choice, m.detail}; }

// A verdict that says what the choice is (kept for the session), not what
// happened this once (taken, too far, missing, busy: asked again next time).
bool IsClassification(std::uint8_t verdict);
const char *VerdictName(std::uint8_t verdict);

// What is known of each (object, choice) for the session. The host answers
// a known host-only or local choice at once, without its game; a guest greys
// a known host-only prompt before it is pressed.
class Known {
public:
  std::optional<std::uint8_t> Get(const Key &key) const;
  // Kept when it is a classification; the oldest goes past kMaxKnown.
  void Note(const Key &key, std::uint8_t verdict);
  void Clear();
  std::size_t Size() const { return m_verdicts.size(); }

private:
  std::map<Key, std::uint8_t> m_verdicts;
  std::deque<Key> m_order;
};

// Host: first come, first served, per object (as the devices' Ledger:
// tandem/devices.hpp). A question is with the host's game until its
// answer; an answered use holds the object for kHoldUs. A player's own
// claim never blocks itself.
class Referee {
public:
  enum class Claim { Granted, Taken, Full };
  Claim Ask(std::uint64_t target, proto::PlayerId asker, std::uint32_t seq, std::uint64_t nowUs);
  // The host's game answered: false when that question is no longer here
  // (its asker vanished, it expired): the answer is not sent.
  bool Answered(std::uint64_t target, proto::PlayerId asker, std::uint32_t seq,
                std::uint8_t verdict, std::uint64_t nowUs);
  // A vanished guest: its questions go, and so do its holds.
  std::size_t Forget(proto::PlayerId player);
  // Questions the game never answered (kPendingUs), holds that are over.
  std::size_t Expire(std::uint64_t nowUs);
  bool Pending(proto::PlayerId asker, std::uint32_t seq) const;
  std::size_t PendingCount() const;
  void Clear() { m_objects.clear(); }

private:
  struct Use {
    proto::PlayerId player{};
    std::uint32_t seq{};
    bool answered{};
    std::uint64_t sinceUs{}; // asked, or answered (then: held until + kHoldUs)
  };
  std::map<std::uint64_t, Use> m_objects;
};

// Host: could the guest's V reach the object when it chose? Its word on
// where it stood is believed within kClaimToleranceM of the host's track of
// that V at the moment it asked (lag compensation: the time it says, on the
// host clock, or half a round trip back when it says none; never more than
// lagcomp::kMaxRewindUs back); else the track's position is used.
struct Reach {
  float claimOff{}; // the claim against the track (0: no track)
  float distance{}; // from the believed position to the object
  bool ok{};
};
Reach CheckReach(const lagcomp::History *track, const proto::Vec3 &claimed,
                 const proto::Vec3 &object, std::uint64_t askedUs, std::uint64_t arrivalUs,
                 std::uint32_t rttMs, float limit = kReachM);
} // namespace tandem::interact
