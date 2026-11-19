#pragma once

// Players coordinating (docs/architecture/comms.md): pings, the link that
// shares scans and Ping-quickhack reveals, waypoints. What the games decide
// about them, apart from the engine: the sender's rate and its ping toggles
// (Pinger), what a receiver shows and for how long (Board, TagLedger), who
// is linked with whom (Links). The plugin runs these on its game thread; the
// endpoint uses Limiter and Linked to relay.

#include "tandem/protocol.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace tandem::comms {
// A token bucket: `burst` at once, then `perSecond`.
class Limiter {
public:
  Limiter(double burst, double perSecond) : m_tokens(burst), m_burst(burst), m_rate(perSecond) {}
  // Takes a token if there is one.
  bool Allow(std::uint64_t nowUs);

private:
  double m_tokens, m_burst, m_rate;
  std::uint64_t m_lastUs{};
};

// The sender's rate (a ping key held down or mashed), and the host's for
// each guest (a game that does not hold to its own: modded, or a bug).
inline constexpr double kPingBurst = 3, kPingsPerSecond = 0.5;
inline constexpr double kHostMarkBurst = 6, kHostMarksPerSecond = 1;
inline constexpr double kHostTagBurst = 12, kHostTagsPerSecond = 4;
// Marks one player has on show at once: a fourth replaces its oldest.
inline constexpr std::size_t kMarksPerPlayer = 3;
// How long a mark of that kind shows when its sender did not say (0), and
// the bounds of what it may say.
std::uint8_t LifeOf(proto::MarkKind kind, std::uint8_t asked);
inline constexpr std::uint8_t kMinMarkSeconds = 3, kMaxMarkSeconds = 60;

// The same thing: the same target, or two places within `metres` (neither
// with a target).
bool SameThing(const proto::Mark &a, const proto::Mark &b, float metres = 2.f);

// Our own pings. A press marks what we look at; a second press on the same
// thing soon after (kDangerWindowUs) turns that mark into a Danger (same id:
// it replaces the first); a press on what one of our live marks shows, later,
// withdraws it. Rate-limited; the upgrade and the withdrawal are not counted.
class Pinger {
public:
  static constexpr std::uint64_t kDangerWindowUs = 1'500'000;
  Pinger() : m_limiter(kPingBurst, kPingsPerSecond) {}
  // The Mark to send, or nullopt (too fast). `kind` None is refused.
  std::optional<proto::Mark> Ping(proto::MarkKind kind, const proto::TargetRef &target,
                                  const proto::Vec3 &position, std::uint64_t nowUs);
  // Withdraw our mark `id` (nullopt: none such alive).
  std::optional<proto::Mark> Withdraw(std::uint32_t id, std::uint64_t nowUs);
  // Our team target (the "Mark target" quickhack): a new one replaces our
  // last (receivers do the same: one per sender), the same target again
  // takes it back. Rate-limited with the pings, not counted in their cap.
  std::optional<proto::Mark> Target(const proto::TargetRef &target, const proto::Vec3 &position,
                                    std::uint64_t nowUs);
  // Take our team target back (it died, was destroyed); nullopt: none.
  std::optional<proto::Mark> ClearTarget(std::uint64_t nowUs);
  // Our marks alive now.
  std::vector<proto::Mark> Alive(std::uint64_t nowUs) const;
  void Clear() { m_live.clear(); }

private:
  struct Live {
    proto::Mark mark;
    std::uint64_t sentUs{}, untilUs{};
  };
  void Prune(std::uint64_t nowUs);
  Limiter m_limiter;
  std::uint32_t m_next{};
  std::vector<Live> m_live;
};

// What a receiver changes on screen: show a mark (new), hide one (withdrawn,
// expired, replaced, its sender gone). A replacement is a Hide then a Show.
struct Change {
  enum class Kind { Show, Hide };
  Kind kind{};
  proto::Mark mark;
};

// A receiver's marks on show. A sender's team target (MarkKind::Target)
// replaces its last one and is not counted in its kMarksPerPlayer; pings and
// team targets never replace each other.
class Board {
public:
  std::vector<Change> Add(const proto::Mark &mark, std::uint64_t nowUs);
  std::vector<Change> Expire(std::uint64_t nowUs);
  // Its sender left (or vanished), or the session ended.
  std::vector<Change> Forget(proto::PlayerId player);
  std::vector<Change> Clear();
  std::size_t Size() const { return m_shown.size(); }

private:
  struct Shown {
    proto::Mark mark;
    std::uint64_t sinceUs{}, untilUs{};
  };
  std::vector<Shown> m_shown;
};

// Whom a link consent lets in (null: no consent given).
bool Consents(const proto::Link *link, proto::PlayerId other);
// a and b each consent to the other.
bool Linked(const std::map<proto::PlayerId, proto::Link> &links, proto::PlayerId a,
            proto::PlayerId b);
// `own` with `other` let in (on) or left out.
proto::Link WithConsent(proto::Link own, proto::PlayerId other, bool on);

// Between us and another player: neither consents; we do (we asked, they
// have not answered); they do (they ask us); both.
enum class LinkState : std::uint8_t { None = 0, Asking = 1, Offered = 2, Linked = 3 };
struct LinkChange {
  proto::PlayerId player{};
  LinkState before{}, after{};
};

// Our game's view of the links: ours and the others' consents.
class Links {
public:
  // Our own consent changed / another player's arrived. The changes it makes
  // to our state with each player we know of.
  std::vector<LinkChange> SetOwn(const proto::Link &own);
  std::vector<LinkChange> Note(const proto::Link &theirs);
  // A player we know of (joined); one gone.
  std::vector<LinkChange> Join(proto::PlayerId player);
  std::vector<LinkChange> Forget(proto::PlayerId player);
  void Clear();
  void SetYou(proto::PlayerId you) { m_you = you; }
  LinkState StateOf(proto::PlayerId player) const;
  bool LinkedWith(proto::PlayerId player) const { return StateOf(player) == LinkState::Linked; }
  const proto::Link &Own() const { return m_own; }
  const std::set<proto::PlayerId> &Players() const { return m_players; }

private:
  std::map<proto::PlayerId, LinkState> Snapshot() const;
  std::vector<LinkChange> Diff(const std::map<proto::PlayerId, LinkState> &before) const;
  proto::PlayerId m_you{};
  proto::Link m_own;
  std::map<proto::PlayerId, proto::Link> m_theirs;
  std::set<proto::PlayerId> m_players;
};

// What a receiver outlines for the others (tags and reveals of linked
// players). Several senders can hold one target: it goes when the last does.
struct TagChange {
  bool on{};
  proto::TagReason reason{};
  proto::TargetRef target;
  float seconds{}; // on, Reveal: how long it lasts (0: until off)
};

class TagLedger {
public:
  static constexpr std::size_t kMaxHeld = 64;
  static constexpr std::uint8_t kMaxRevealSeconds = 60;
  std::vector<TagChange> Apply(const proto::Tag &tag, std::uint64_t nowUs);
  std::vector<TagChange> Expire(std::uint64_t nowUs);
  // Its sender left, or is no longer linked with us.
  std::vector<TagChange> Forget(proto::PlayerId player);
  std::vector<TagChange> Clear();
  std::size_t Size() const { return m_held.size(); }

private:
  struct Key {
    proto::TagReason reason{};
    proto::TargetRef target;
    auto operator<=>(const Key &other) const {
      if (reason != other.reason)
        return reason <=> other.reason;
      if (target.kind != other.target.kind)
        return target.kind <=> other.target.kind;
      return target.id <=> other.target.id;
    }
    bool operator==(const Key &) const = default;
  };
  // Who holds each target, until when (0: until off).
  std::map<Key, std::map<proto::PlayerId, std::uint64_t>> m_held;
  std::vector<TagChange> Release(const Key &key, proto::PlayerId player);
};

// A waypoint worth sending again: another kind, or moved more than 1 m.
bool Moved(const proto::Waypoint &a, const proto::Waypoint &b);
} // namespace tandem::comms
