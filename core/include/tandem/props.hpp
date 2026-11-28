#pragma once

// Physics props (docs/architecture/props.md): a placed prop with a physics
// body that something moved is the area owner's, as its NPCs and its street
// are. Who speaks for a moving prop, the session's record of where moved
// props came to rest, how a moving one is packed and drawn.

#include "tandem/protocol.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace tandem::props {
// A game speaks for a moving prop until this long after its last word: no
// other game's word about it is passed on meanwhile (first word wins), and
// a game that holds its instance to another's word lets it go to its own
// physics this long after that word stopped (its speaker vanished).
inline constexpr std::uint64_t kHoldUs = 2'000'000;
// Our instance of a prop another game runs, pushed by our V: put back where
// it stood when that game said nothing of it for this long.
inline constexpr std::uint64_t kPushAnswerUs = 2'000'000;
// A prop that moved less than kRestMove in kRestUs is at rest.
inline constexpr float kRestMove = 0.01f;
inline constexpr std::uint64_t kRestUs = 500'000;
// Shared within this of a V that sees the owner (regions::kShare).
inline constexpr float kShareRange = 100.f;
// The session's record of rests: the most a late joiner is sent.
inline constexpr std::size_t kMaxRecord = 1024;
// A moving prop is drawn this far in the past (two of its owner's rounds:
// smooth on a lost packet), extrapolated at most kExtrapolateUs past its
// last state, then held.
inline constexpr std::uint64_t kDrawDelayUs = 200'000;
inline constexpr std::uint64_t kExtrapolateUs = 250'000;
// Its owner's states: ten a second while any moves.
inline constexpr std::uint64_t kStatesEveryUs = 100'000;

// A prop's name in every game: its placed entity and its body's index.
using Key = std::pair<std::uint64_t, std::uint8_t>;

// A prop as its owner's game has it now.
struct Prop {
  std::uint64_t entity{};
  std::uint8_t part{};
  proto::Vec3 position;
  proto::Quat orientation;
  proto::Vec3 velocity;
};
proto::PropEntry Pack(const Prop &prop);
Prop Unpack(const proto::PropEntry &entry);

// Who speaks for each moving prop (the host's arbitration; a guest keeps
// the same of the others' words, to stay silent about a prop another game
// runs and to hold its own instance to that game's word).
class Speakers {
public:
  // True: `player` may speak for the prop now (none does, or it does
  // already), and it is noted as its speaker. `ownsArea`: whether that
  // game runs the area round its V (a game that does not, asks: PropPush).
  bool Claim(const Key &key, proto::PlayerId player, std::uint64_t nowUs, bool ownsArea);
  // Its speaker, while it speaks (kHoldUs after its last word).
  std::optional<proto::PlayerId> Of(const Key &key, std::uint64_t nowUs) const;
  // Another game's word heard (a guest): that game speaks for it.
  void Heard(const Key &key, proto::PlayerId player, std::uint64_t nowUs);
  // A player left: it speaks for nothing any more.
  void Forget(proto::PlayerId player);
  void Expire(std::uint64_t nowUs);
  void Clear() { m_speakers.clear(); }
  std::size_t Size() const { return m_speakers.size(); }

private:
  struct Speaker {
    proto::PlayerId player{};
    std::uint64_t lastUs{};
  };
  std::map<Key, Speaker> m_speakers;
};

// Where moved props came to rest in the session: the host keeps every rest
// it accepted, a guest every rest the host sent it (its own back too), in
// the same order, so the two match and the world digest covers them. Full:
// a prop new to the record is neither kept nor passed on (Refused counts
// them); one it knows is still updated.
class Record {
public:
  // True: news (a prop new to the record, or another rest of one it knows).
  bool Note(const proto::PropRest &rest);
  const proto::PropRest *Find(const Key &key) const;
  bool Knows(const Key &key) const { return m_rests.contains(key); }
  bool Full() const { return m_rests.size() >= kMaxRecord; }
  bool Empty() const { return m_rests.empty(); }
  std::size_t Size() const { return m_rests.size(); }
  std::uint64_t Refused() const { return m_refused; }
  std::uint32_t Digest() const;
  // Every rest, in the order the props were first recorded.
  std::vector<proto::PropRest> InOrder() const;
  // Those within `radius` of `at` (the scripts' check of what streamed in).
  std::vector<proto::PropRest> Around(const proto::Vec3 &at, float radius, std::size_t most) const;
  void Clear();

private:
  std::map<Key, proto::PropRest> m_rests;
  std::vector<Key> m_order;
  std::uint64_t m_refused{};
};

// One prop another game runs, as its states arrived: drawn kDrawDelayUs in
// the past, linear between two states (the orientation normalized between
// them), carried on along its last velocity at most kExtrapolateUs, then
// held.
class Track {
public:
  // A state older than the newest is dropped (the State channel reorders).
  bool Add(std::uint64_t timeUs, const Prop &prop);
  std::optional<Prop> At(std::uint64_t timeUs) const;
  bool Empty() const { return m_samples.empty(); }
  std::uint64_t LastUs() const { return m_samples.empty() ? 0 : m_samples.back().first; }

private:
  std::deque<std::pair<std::uint64_t, Prop>> m_samples;
};

// The owner's moving props for one PropStates: the nearest to a V first,
// at most kMaxPropsPerState (the rest wait for the next round: a prop far
// from every V is drawn a round later, never starved since a round's pick
// rotates the ones left out to the front).
class Budget {
public:
  std::vector<proto::PropEntry> Pick(std::span<const Prop> moving,
                                     std::span<const proto::Vec3> viewers);

private:
  std::map<Key, std::uint32_t> m_waited;
};

// Whether a prop at `at` is within kShareRange of one of `viewers`.
bool Near(const proto::Vec3 &at, std::span<const proto::Vec3> viewers);
} // namespace tandem::props
