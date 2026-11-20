#pragma once

// The referee (docs/architecture/sim.md): every message one game receives
// from another, checked against the protocol and the authority table
// (docs/ARCHITECTURE.md). It watches an Endpoint's wire (Endpoint::SetWireTap)
// from one side, host or guest, keeps its own record of what was said either
// way, and reports each breach with the rule it broke.
//
// Two kinds of finding:
// - a violation (`referee: [rule] ...`): the other game did what the
//   protocol forbids. Certain, whatever the network does: every scenario
//   rejects such a line.
// - a hint (`referee-hint: [rule] ...`): legal on the wire but suspect (a
//   state held for very long, a burst, a health bar that fell more often
//   than it was hit). Counted and shown, never a failure by itself.
//
// Needs no network: tests feed it messages (tests/referee_test.cpp).

#include "tandem/endpoint.hpp"
#include "tandem/protocol.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace tandem::sim {
struct Finding {
  std::string rule; // "authority.host-only", "ids.unknown-npc"...
  std::string text;
  bool hint{};
  std::uint64_t atUs{};
  // "referee: [rule] text", or "referee-hint: ...".
  std::string Line() const;
};

class Referee {
public:
  // Every time below is in microseconds.
  struct Limits {
    // Rates, per sender and message type, over `rateWindowUs`.
    std::uint64_t rateWindowUs{3'000'000};
    float playerStateHz{80.f}, poseHz{80.f}, vehicleHz{80.f}, shotHz{60.f};
    float npcStatesHz{30.f}, awarenessHz{15.f}, digestHz{3.f}, pingHz{6.f};
    float eventsHz{400.f};      // every reliable message together (hint)
    float bytesPerSecond{2e5f}; // everything together (hint)
    std::uint32_t resyncsPer10s{6};
    // Sizes (encoded bytes).
    std::size_t playerStateBytes{64};
    std::size_t stateBytes{1372}; // beyond it ENet fragments, reliably
    // A state held and never released.
    std::uint64_t takedownHoldUs{30'000'000};
    std::uint64_t grappleHoldUs{300'000'000};
    std::uint64_t timeEffectUs{180'000'000};
    std::uint64_t reviveUs{30'000'000};
    std::uint64_t downUs{600'000'000};
    std::uint64_t outUs{180'000'000};
    std::uint64_t ringingUs{120'000'000};
    std::uint64_t holdAfterLeaveUs{2'000'000};
    // A guest's shot, action or upload while its V is down or out.
    std::uint64_t downGraceUs{1'500'000};
    // A host's Epoch is acknowledged within this.
    std::uint64_t epochAckUs{5'000'000};
    // A domain's digest may differ for this long (two digests, a resync,
    // its cooldown); Attach sets both from the endpoint's timings.
    std::uint64_t digestGraceUs{20'000'000};
    std::uint64_t digestSilenceUs{20'000'000};
    // A state's time against the host clock.
    std::uint64_t clockSkewUs{3'000'000};
    // A vehicle moved by a V that sits in someone else's.
    std::uint64_t seatGraceUs{1'500'000};
    // An NPC still in the host's states this long after it went.
    std::uint64_t goneGraceUs{3'000'000};
    // An owner's word to us (a hit, a cue, a shot, its animations) on an NPC
    // that left our view this long ago (authority.not-shared).
    std::uint64_t notSharedGraceUs{1'000'000};
  };
  struct Options {
    // What we send is lost or doubled on purpose (--drop, --dup, the chaos
    // persona): the other side's resyncs and repeats are our doing.
    bool faulty{};
    Limits limits{};
  };

  Referee(bool host, Options options);
  explicit Referee(bool host) : Referee(host, Options{}) {}

  // Watches this endpoint's wire from now on (the endpoint must outlive
  // the referee's use; call Tick() now and then for what time alone finds).
  void Attach(Endpoint &endpoint);
  void Tick();

  // The same, fed by hand. `nowUs`: a steady clock; `hostUs`: the host
  // clock at the same moment (0: unknown). `ours`: our own digest at this
  // point of the stream (a guest receiving a Digest).
  void Received(proto::PlayerId from, const proto::Message &message, std::uint64_t nowUs,
                std::uint64_t hostUs = 0, const proto::Digest *ours = nullptr);
  void Sent(proto::PlayerId to, const proto::Message &message, std::uint64_t nowUs);
  void Tick(std::uint64_t nowUs);
  // The session is over: nothing more is owed, time finds nothing more.
  void End() { m_ended = true; }

  // Called with every finding as it is made (also those Take() no longer
  // keeps), on the thread that fed the referee.
  void OnFinding(std::function<void(const Finding &)> callback) {
    m_onFinding = std::move(callback);
  }
  // Findings since the last call, in order.
  std::vector<Finding> Take();
  std::uint32_t Violations() const { return m_violations; }
  std::uint32_t Hints() const { return m_hints; }
  // How often each rule was broken (violations and hints).
  const std::map<std::string, std::uint32_t> &Counts() const { return m_counts; }
  // Messages seen, by Message alternative.
  const std::vector<std::uint64_t> &ReceivedCounts() const { return m_received; }
  const std::vector<std::uint64_t> &SentCounts() const { return m_sent; }

  bool IsHost() const { return m_host; }
  proto::PlayerId Self() const { return m_self; }

private:
  struct Npc {
    std::uint64_t record{}, appearance{};
    bool live{};
    bool dead{}; // it died, or its body was disposed of
    std::uint8_t goneReason{};
    std::uint64_t goneUs{};
    bool lateFlagged{};
  };
  struct Held {
    proto::PlayerId player{};
    proto::NpcHoldKind how{};
    std::uint64_t sinceUs{};
    bool flagged{};
  };
  struct Since {
    std::uint64_t sinceUs{};
    bool flagged{};
  };
  struct LifeOf {
    proto::Life life{};
    std::uint64_t sinceUs{};
    bool flagged{};
  };
  struct Rate {
    std::deque<std::uint64_t> times;
    std::uint64_t flaggedUs{};
  };
  using TypeAndPlayer = std::pair<std::size_t, proto::PlayerId>;

  void Add(std::string rule, std::string text, bool hint = false);
  void Hint(std::string rule, std::string text) { Add(std::move(rule), std::move(text), true); }
  void Forget(proto::PlayerId player);
  void CheckValues(const proto::Message &message, std::uint64_t hostUs);
  void CheckRate(proto::PlayerId from, const proto::Message &message, std::size_t bytes);
  void CheckSequence(proto::PlayerId from, const proto::Message &message);
  void CheckOneShot(proto::PlayerId from, const proto::Message &message,
                    const std::vector<std::byte> &bytes);
  void FromGuest(proto::PlayerId from, const proto::Message &message);
  void FromHost(const proto::Message &message, const proto::Digest *ours);
  // Both directions: the session's record as said on the wire.
  void Note(const proto::Message &message, bool sent);
  void NeedNpc(proto::NpcId npc, const char *what);
  // An owner's word on an NPC that left our view (authority.not-shared).
  void NeedShown(proto::NpcId npc, const char *what);
  void NeedPlayer(proto::PlayerId player, const char *what);
  // Who runs an NPC: its announcer, until the host says otherwise.
  proto::PlayerId OwnerOf(proto::NpcId npc) const;
  void Moved(proto::PlayerId subject);

  bool m_host{};
  Options m_options;
  Endpoint *m_endpoint{};
  std::uint64_t m_nowUs{};
  proto::PlayerId m_self{proto::kNoPlayer}, m_hostPlayer{proto::kNoPlayer};
  bool m_welcomed{}, m_ended{}, m_left{};

  std::vector<Finding> m_new;
  std::function<void(const Finding &)> m_onFinding;
  std::uint32_t m_violations{}, m_hints{};
  std::map<std::string, std::uint32_t> m_counts;
  std::vector<std::uint64_t> m_received, m_sent;

  // The record.
  std::set<proto::PlayerId> m_players, m_everPlayers;
  std::map<proto::NpcId, Npc> m_npcs;
  std::map<proto::NpcId, proto::PlayerId> m_owners; // NpcOwner said so
  std::set<proto::NpcId> m_given;                   // handed to us, not yet run
  std::map<proto::NpcId, Held> m_holds;
  std::map<proto::PlayerId, LifeOf> m_lives;
  std::map<proto::PlayerId, Since> m_time;
  std::map<proto::PlayerId, std::pair<proto::Seat, std::uint64_t>> m_seats;
  std::map<std::pair<proto::PlayerId, proto::PlayerId>, Since> m_revives; // reviver, target
  std::map<proto::PlayerId, std::uint64_t> m_wasDownUs;                   // last seen down at
  std::map<proto::PlayerId, std::uint64_t> m_leftUs;                      // left the roster at
  std::optional<Since> m_ringing;
  // One-shots.
  std::set<std::pair<std::uint64_t, std::uint32_t>> m_rewards;
  std::set<std::uint32_t> m_gameOvers;
  std::set<std::tuple<proto::PlayerId, std::uint64_t, std::uint32_t>> m_sharedItems;
  std::set<std::pair<proto::PlayerId, std::uint32_t>> m_dropTokens;
  std::set<std::pair<proto::NpcId, std::uint64_t>> m_shotsAt;
  std::map<TypeAndPlayer, std::pair<std::vector<std::byte>, std::uint64_t>> m_lastOneShot;
  // World items: who took each stack.
  std::map<proto::WorldItemId, std::vector<proto::PlayerId>> m_items;
  // Sequences and rates, by message type and player.
  std::map<TypeAndPlayer, std::uint32_t> m_seqs;
  std::map<TypeAndPlayer, Rate> m_rates;
  std::map<proto::PlayerId, Rate> m_events;
  std::deque<std::pair<std::uint64_t, std::size_t>> m_bytes;
  std::size_t m_bytesInWindow{};
  std::uint64_t m_bytesFlaggedUs{};
  // Requests of ours awaiting the host's answer (guest).
  std::map<std::pair<proto::NpcId, proto::NpcActionKind>, std::uint32_t> m_asked;
  std::set<std::pair<proto::NpcId, proto::NpcActionKind>> m_askedOnDead;
  std::set<std::pair<proto::WorldItemId, std::uint8_t>> m_claimed;
  std::set<std::pair<proto::NpcId, std::uint64_t>> m_uploads;
  // Breach Protocol: our breaches awaiting the host's answer (network, NPC),
  // and who breached each network as the host said.
  std::map<std::tuple<std::uint64_t, std::uint64_t, proto::NpcId>, std::uint32_t> m_breachesAsked;
  std::map<std::pair<std::uint64_t, std::uint64_t>, proto::PlayerId> m_breached;
  std::deque<std::uint64_t> m_pings;
  std::uint32_t m_offers{}; // the Regroup offers we made
  // Epochs.
  std::map<proto::PlayerId, std::uint32_t> m_epochs;
  std::uint32_t m_ourEpoch{1}, m_hostEpoch{};
  std::map<proto::PlayerId, Since> m_epochOwed; // host: guests that owe an acknowledgement
  // Digests.
  std::array<Since, 4> m_differs{};
  std::uint64_t m_lastDigestUs{};
  bool m_digestSilent{};
  std::map<std::pair<proto::PlayerId, std::uint8_t>, std::deque<std::uint64_t>> m_resyncs;
  // Hits we dealt and health drops heard, per NPC (guest; a hint).
  std::map<proto::NpcId, std::pair<std::uint32_t, std::uint32_t>> m_hitsAndDrops;
  std::map<proto::NpcId, float> m_health;
  std::uint64_t m_othersFoughtUs{};
};

// "PlayerState", "NpcStrike"... for a Message index.
const char *MessageName(std::size_t index);

// Who may send a message (by Message index): only the host, only a guest,
// or either, checked by its own rules (the sender's name, the NPC's owner).
// Every message has one: referee.cpp does not compile without it.
enum class Speaker : std::uint8_t { Host, Guest, Either };
Speaker SpeakerOf(std::size_t index);
} // namespace tandem::sim
