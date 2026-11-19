#pragma once

// Keeping every game's copy of the shared state the same (docs/architecture/consistency.md,
// "Consistency"): the digests the host sends and the guests check, the
// guest's decision to ask for a domain again, notices that never spam,
// time-sensitive events that go stale, and the faults tests inject.
//
// Platform-independent: the endpoint uses it on both sides, the plugin and
// the mock player on top of it.

#include "tandem/protocol.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace tandem::consistency {
// FNV-1a over explicit field values (never a struct's bytes: padding).
class Hasher {
public:
  Hasher &Add(std::uint64_t value);
  Hasher &Add(float value);
  Hasher &Add(std::string_view text);
  std::uint32_t Value() const { return m_hash; }

private:
  void Byte(std::uint8_t byte);
  std::uint32_t m_hash{2166136261u};
};

// Each domain's hash, the same on the host (its record) and on a guest (what
// it received). Positions are left out: they are state, newest wins.
std::uint32_t WorldDigest(const std::optional<proto::World> &world,
                          const std::optional<proto::Difficulty> &difficulty = std::nullopt,
                          const std::optional<proto::Rules> &rules = std::nullopt);
// `holds`: who holds which NPC (grapples, carried bodies). `owners`: who runs
// each NPC, where it is not the game that announced it (regions.hpp). With
// `only`, the NPCs it accepts (id, owner) and no others: a guest is sent, and
// hashes, the NPCs of the owners it is linked with.
using NpcFilter = std::function<bool(proto::NpcId, proto::PlayerId)>;
std::uint32_t NpcDigest(const std::map<proto::NpcId, proto::NpcSpawn> &npcs,
                        const std::map<proto::NpcId, proto::NpcHealth> &health,
                        const std::map<proto::NpcId, proto::NpcHold> &holds = {},
                        const std::map<proto::NpcId, proto::PlayerId> &owners = {},
                        const NpcFilter &only = {}, std::uint16_t *count = nullptr);
// The shared world vehicles and how each stands (part of the NPC domain: a
// difference is resynced with it).
std::uint32_t TrafficDigest(const std::map<proto::TrafficId, proto::TrafficSpawn> &traffic,
                            const std::map<proto::TrafficId, proto::TrafficStatus> &status);

// The players as `self` sees them: everyone's look, held items, seat, radio,
// stealth, pause menu and time ability but its own (a game is never sent
// its own back), and the whole roster.
struct PlayersView {
  const std::map<proto::PlayerId, proto::PlayerInfo> &roster;
  const std::map<proto::PlayerId, proto::Appearance> &appearances;
  const std::map<proto::PlayerId, proto::Held> &held;
  const std::map<proto::PlayerId, proto::Seat> &seats;
  const std::map<proto::PlayerId, proto::VehicleRadio> &radios;
  const std::map<proto::PlayerId, proto::Stealth> &stealth;
  const std::set<proto::PlayerId> &paused;
  const std::map<proto::PlayerId, proto::TimeEffect> &time;
  // Who is down or out (a V that is up counts like one never heard of).
  const std::map<proto::PlayerId, proto::Vitals> &lives;
  // Who is in another world (a V in the city counts like one never heard of).
  const std::map<proto::PlayerId, proto::Elsewhere> &elsewhere;
  // Who is wanted (a V with no heat counts like one never heard of).
  const std::map<proto::PlayerId, proto::Heat> &heat;
  // Who is linked with whom (Regions.pairs: regions.md): the same for every
  // player. A lost Regions is otherwise never sent again until links change,
  // and an owner that believes it is linked with nobody sends no NPC states.
  const std::vector<proto::PlayerId> *links = nullptr;
};
std::uint32_t PlayersDigest(const PlayersView &players, proto::PlayerId self);

// The domains (kDomain* bits) where two digests differ.
std::uint8_t Differences(const proto::Digest &a, const proto::Digest &b);

// Guest: when to ask the host for a domain again. A domain must differ in
// two digests in a row (the first may have raced a large transfer on the
// bulk channel, e.g. the journal snapshot at join), and is asked for at most
// once per cooldown.
class Checker {
public:
  explicit Checker(std::uint64_t cooldownUs = 5'000'000) : m_cooldownUs(cooldownUs) {}
  // The host's digest and ours at the same point of the stream: the domains
  // to ask for now (0: none).
  std::uint8_t Compare(const proto::Digest &host, const proto::Digest &ours, std::uint64_t nowUs);
  std::uint32_t Mismatches() const { return m_mismatches; }
  std::uint32_t Requests() const { return m_requests; }

private:
  std::uint64_t m_cooldownUs;
  std::uint8_t m_last{};
  std::array<std::uint64_t, 4> m_askedUs{};
  std::uint32_t m_mismatches{}, m_requests{};
};

// Something worth telling the player (a resync, a lost host), at most once
// per `gapUs` for each key: subtle, never spam.
class NoticeGate {
public:
  bool Allow(std::string_view key, std::uint64_t nowUs, std::uint64_t gapUs);

private:
  std::map<std::string, std::uint64_t, std::less<>> m_lastUs;
};

// A time-sensitive event (a hit on our V, a shot to draw), kept with when it
// arrived: after a loading screen or a stall it is worth nothing, and
// applying a pile of them at once would be wrong (every queued hit at once).
template <class T> struct Timed {
  T value;
  std::uint64_t atUs{};
};
// Keeps the events younger than `maxAgeUs` at `nowUs`, in order; returns how
// many went.
template <class T>
std::size_t DropStale(std::vector<Timed<T>> &items, std::uint64_t nowUs, std::uint64_t maxAgeUs) {
  const auto before = items.size();
  std::erase_if(items, [&](const Timed<T> &item) {
    return nowUs > item.atUs && nowUs - item.atUs > maxAgeUs;
  });
  return before - items.size();
}

// A one-shot event: applying it twice would count twice (a hit, an effect,
// a reward, a shot). ENet delivers a reliable message exactly once per
// connection and a new connection starts with empty outboxes, so these are
// never replayed; state messages are, by resyncs, and must be idempotent.
bool IsOneShot(const proto::Message &message);

// Faults injected on sends (tests, tandem-peer --drop/--dup), to show that
// the session converges anyway. `drop`: a message is not sent at all, even a
// reliable one (as if a bug lost it: only a digest and a resync can bring it
// back). `dup`: a state message is sent twice (a one-shot never is, see
// IsOneShot). The handshake (Hello, Welcome, Refused, Bye) is never touched.
struct Faults {
  double drop{};
  double dup{};
  std::uint32_t seed{1};
};

class FaultyLink {
public:
  enum class Action { Send, Drop, Twice };
  void Set(const Faults &faults);
  bool Active() const { return m_faults.drop > 0 || m_faults.dup > 0; }
  Action Decide(const proto::Message &message);
  std::uint64_t Dropped() const { return m_dropped; }
  std::uint64_t Duplicated() const { return m_duplicated; }

private:
  Faults m_faults;
  std::mt19937 m_random{1};
  std::uint64_t m_dropped{}, m_duplicated{};
};
} // namespace tandem::consistency
