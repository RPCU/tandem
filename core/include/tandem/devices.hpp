#pragma once

// Devices and world objects (docs/architecture/devices.md): the session's
// record of every device anyone touched or asked about. The host's is the
// authority (its game reports what its devices are, guests' requests are
// judged against it); a guest's is its copy of the host's word.
//
// Platform-independent: the endpoint uses it on both sides, the plugin and
// the mock player on top of it.

#include "tandem/comms.hpp"
#include "tandem/protocol.hpp"

#include <compare>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace tandem::devices {
// A device's name: its entity's static id and its persistent state's class.
struct Key {
  std::uint64_t entity{};
  std::uint64_t cls{};
  friend auto operator<=>(const Key &, const Key &) = default;
};
inline Key KeyOf(const proto::DeviceState &state) { return {state.entity, state.cls}; }
inline Key KeyOf(const proto::DeviceUse &use) { return {use.entity, use.cls}; }

// What a device is (status, state bits, value), whoever said it and how.
struct What {
  std::int8_t status{};
  std::uint16_t flags{};
  std::int32_t value{proto::kDeviceNoValue};
  friend bool operator==(const What &, const What &) = default;
};
inline What WhatOf(const proto::DeviceState &state) {
  return {state.status, state.flags, state.value};
}
inline What WhatOf(const proto::DeviceUse &use) { return {use.status, use.flags, use.value}; }

// The record is bounded: a guest cannot fill the host's memory by asking
// about devices that do not exist.
inline constexpr std::size_t kMaxDevices = 4096;

class Ledger {
public:
  // Host: our game says what a device is now (`state.by`: who made it so).
  // The record to send to everyone when it is new or changed (its revision
  // counted here; quiet when it is new and nobody's doing: the device was
  // like that); nullopt for a repeat, or when the record is full.
  std::optional<proto::DeviceState> Report(proto::DeviceState state);

  // Host: a guest's request to make a device something.
  enum class Verdict {
    Grant, // ours to apply: nobody else changed it since what the guest saw
    Stale, // someone else did: void, the guest gets the record back
    Same,  // it is that already
    Full,  // unknown device and no room for it
  };
  Verdict Judge(const proto::DeviceUse &use) const;

  // Guest: the host's word. False for what we have already (a repeat, a
  // resync of what never changed) or what is older than what we have.
  bool Accept(const proto::DeviceState &state);

  const proto::DeviceState *Find(const Key &key) const;
  const std::map<Key, proto::DeviceState> &All() const { return m_devices; }
  std::size_t Size() const { return m_devices.size(); }
  void Clear() { m_devices.clear(); }
  // The same on the host and on a guest that has every record.
  std::uint32_t Digest() const;

private:
  std::map<Key, proto::DeviceState> m_devices;
};

// Host: who breached what (Breach Protocol, devices.md). A network is
// breached once for everyone, and so is an NPC through the quickhack: the
// first breach of each is granted, any later one refused, whoever asks.
// The device record has the last word on a network: an access point it
// holds as breached by someone else (the host's world, another player) is
// refused too.
class Breaches {
public:
  using Verdict = proto::BreachVerdict;
  Verdict Judge(const Key &network, proto::PlayerId player, const Ledger &record) const;
  Verdict JudgeNpc(proto::NpcId npc) const;
  // Granted: remembered for good (a breach outlives its breacher).
  void Note(const Key &network, proto::PlayerId player) { m_networks.emplace(network, player); }
  void NoteNpc(proto::NpcId npc, proto::PlayerId player) { m_npcs.emplace(npc, player); }
  // Who breached it first (kNoPlayer: nobody, or the host's world).
  proto::PlayerId By(const Key &network, const Ledger &record) const;
  proto::PlayerId ByNpc(proto::NpcId npc) const;
  std::size_t Size() const { return m_networks.size() + m_npcs.size(); }

private:
  std::map<Key, proto::PlayerId> m_networks;
  std::map<proto::NpcId, proto::PlayerId> m_npcs;
};

// What a granted breach makes of its access point's record: breached and
// open, by the breacher (from what the record has, else on).
proto::DeviceState BreachedState(const Key &network, proto::PlayerId player,
                                 const proto::DeviceState *record);

// The breacher's side of the host's grant (devices.md, "Breach Protocol"):
// a won breach pays its rewards (the datamine's money, components and
// shards) only once the host has granted it. One hold per minigame won,
// with each network (or the NPC) it breached; granted for any of them, it
// is paid; refused for all, it is not, and `by` says who was first. The
// host's game holds its own the same way (its answer comes at once).
// Unanswered for kBreachHoldUs (a host that froze), or the session over:
// paid, as alone.
inline constexpr std::uint64_t kBreachHoldUs = 3'000'000;
class BreachHolds {
public:
  struct Target {
    Key network;
    proto::NpcId npc{};
    friend auto operator<=>(const Target &, const Target &) = default;
  };
  struct Decision {
    std::uint32_t hold{};
    bool paid{};
    proto::PlayerId by{}; // refused: who breached it first (kNoPlayer: the host's world)
    bool answered{};      // false: nobody answered (the hold ran out, the session ended)
    friend bool operator==(const Decision &, const Decision &) = default;
  };
  // Our breach `hold` waits for the host's word on `target` too.
  void Hold(std::uint32_t hold, Target target, std::uint64_t nowUs);
  // The host's answer on one target of our oldest hold that waits for it:
  // the hold it settles, if any.
  std::optional<Decision> Answer(const proto::BreachResult &result);
  // The holds the host left unanswered for kBreachHoldUs.
  std::vector<Decision> Expire(std::uint64_t nowUs);
  // The session is over: every hold is settled (our world is our own again).
  std::vector<Decision> End();
  std::size_t Size() const { return m_holds.size(); }

private:
  struct Held {
    std::uint32_t hold{};
    std::vector<Target> waiting;
    proto::PlayerId by{};
    std::uint64_t sinceUs{};
  };
  std::vector<Held> m_holds;
};

// ---- Glass and breakable scenery (devices.md) ----
//
// A break is told by the game that saw it and replayed by the others.
// Replaying one breaks the piece here too, which our game sees as a break
// of its own: Fractures tells the two apart, and one pane shattering in
// many pieces from the stream of them.

// What a game may say (a magazine through a glass front), and what the host
// passes on for each guest.
inline constexpr double kFractureBurst = 24, kFracturesPerSecond = 12;
inline constexpr double kHostFractureBurst = 48, kHostFracturesPerSecond = 24;
// Two breaks this close, this soon after each other, are one piece going.
inline constexpr float kFractureSpot = 0.75f;
inline constexpr std::uint64_t kFractureWindowUs = 1'500'000;
// A break this close to one we just replayed is that replay's own.
inline constexpr float kFractureEcho = 1.5f;
inline constexpr std::uint64_t kFractureEchoUs = 2'000'000;
inline constexpr std::size_t kMaxFractureSpots = 256, kMaxFracturedParts = 4096;

class Fractures {
public:
  // Our game saw something break. True: to tell the others (not the echo of
  // a break we replayed, not a piece already told, within our rate).
  bool Ours(const proto::Fracture &fracture, std::uint64_t nowUs);
  // Another game's break. True: ours to replay (not one that broke here
  // already, by our own doing or an earlier word).
  bool Theirs(const proto::Fracture &fracture, std::uint64_t nowUs);
  // A new session: nothing is remembered.
  void Clear();
  std::uint64_t Told() const { return m_told; }
  std::uint64_t Replayed() const { return m_replayed; }
  std::uint64_t Echoes() const { return m_echoes; }
  std::uint64_t Repeats() const { return m_repeats; }
  std::uint64_t Held() const { return m_held; }

private:
  struct Spot {
    proto::Vec3 at;
    std::uint64_t atUs{};
    bool ours{};
  };
  enum class Whose { Anyone, Ours, Theirs };
  using Part = std::pair<std::uint64_t, std::uint64_t>; // entity, component
  const Spot *Near(const proto::Vec3 &at, float within, std::uint64_t nowUs, std::uint64_t window,
                   Whose whose) const;
  void Note(const proto::Fracture &fracture, std::uint64_t nowUs, bool ours);
  // Pieces with no name of their own, lately: where, and whose word.
  std::deque<Spot> m_spots;
  // The named parts broken this session, and whether ours told it first.
  std::map<Part, bool> m_parts;
  comms::Limiter m_rate{kFractureBurst, kFracturesPerSecond};
  std::uint64_t m_told{}, m_replayed{}, m_echoes{}, m_repeats{}, m_held{};
};

// Guest: when to ask for the record again. As for the other domains
// (consistency::Checker): it must differ twice in a row, and is asked for at
// most once per cooldown.
class DigestCheck {
public:
  explicit DigestCheck(std::uint64_t cooldownUs = 5'000'000) : m_cooldownUs(cooldownUs) {}
  bool Compare(const proto::DeviceDigest &host, const proto::DeviceDigest &ours,
               std::uint64_t nowUs);
  std::uint32_t Mismatches() const { return m_mismatches; }
  std::uint32_t Requests() const { return m_requests; }

private:
  std::uint64_t m_cooldownUs;
  bool m_last{};
  std::uint64_t m_askedUs{};
  std::uint32_t m_mismatches{}, m_requests{};
};
} // namespace tandem::devices
