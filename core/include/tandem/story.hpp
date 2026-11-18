#pragma once

// Story sync, the parts that need no game (docs/research/coop-quests-design.md):
// what the host keeps for late joiners, who took part in a quest (for its
// rewards), and who answered a call first.

#include "tandem/protocol.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace tandem::story {
// The host's story as a player joining now needs it: the journal (its
// snapshot plus every change since), the tracked entry, the call that is up,
// the open choice hub and every player's StoryState.
class Record {
public:
  // A story message the host sent (or relayed); a guest notes what it
  // receives the same way. False when it changed nothing (a repeat).
  bool Note(const proto::Message &message);
  // The story domain's hash for digests (tandem/consistency.hpp): the
  // journal and the tracked entry, what lasts and what a resync (ForJoiner)
  // sends whole. Calls, hubs and effects are short-lived and sent again on
  // every change; StoryStates are left out (a guest is never sent its own).
  std::uint32_t Digest() const;
  // A player left: its StoryState goes.
  void Forget(proto::PlayerId player);
  // What to send a newcomer, in order: the journal snapshot first.
  std::vector<proto::Message> ForJoiner() const;
  std::size_t JournalSize() const { return m_journal.size(); }

private:
  std::map<std::uint32_t, proto::JournalChange> m_journal;
  bool m_haveJournal{};
  std::optional<proto::Tracked> m_tracked;
  std::optional<proto::PhoneCall> m_call;
  std::optional<proto::ChoiceHub> m_hub;
  std::map<proto::PlayerId, proto::StoryState> m_states;
  std::map<std::uint64_t, proto::StoryEffect> m_effects; // on now
};

// "Taking part" (design §4.3): a guest takes part in a quest when it is
// within kNearMetres of the host's V as one of the quest's objectives
// changes, or as the quest completes. A reward is the quest's whose journal
// changed last within kRewardWindowUs before it; with none, the guests near
// the host now get it.
inline constexpr float kNearMetres = 150.f;
inline constexpr std::uint64_t kRewardWindowUs = 10'000'000;

class Participation {
public:
  // The quest (journal hash of its gameJournalQuest) changed, with these
  // guests near the host's V.
  void Note(std::uint32_t quest, const std::vector<proto::PlayerId> &near, std::uint64_t nowUs);
  struct Award {
    std::uint32_t quest{}; // 0: no quest changed lately
    std::vector<proto::PlayerId> players;
  };
  // A reward event on the host now: who gets it.
  Award Attribute(std::uint64_t nowUs, const std::vector<proto::PlayerId> &nearNow) const;
  std::set<proto::PlayerId> Of(std::uint32_t quest) const;
  void Forget(proto::PlayerId player);

private:
  std::map<std::uint32_t, std::set<proto::PlayerId>> m_players;
  std::uint32_t m_lastQuest{};
  std::uint64_t m_lastUs{};
};

// Calls: anyone may answer for the crew, the first answer wins (design §3.2).
// The host feeds it its PhoneSystem's call and the answers it gets.
class CallArbiter {
public:
  // The host's call changed phase (a new call resets who answered).
  void Update(const proto::PhoneCall &call);
  // Someone answers or rejects (kPhoneAnswer / kPhoneReject) the call from
  // `contact`. True when this is the answer that counts: the call is
  // ringing, from that contact, and nobody answered yet.
  bool TryAnswer(proto::PlayerId player, std::uint8_t action, std::uint64_t contact);
  const std::optional<proto::PhoneCall> &Call() const { return m_call; }

private:
  std::optional<proto::PhoneCall> m_call;
};
// Text replies need no arbiter here: the host applies them in the order
// they arrive, on its game thread, and a reply whose choice entry is no
// longer Active (another reply won) is dropped there (Story.reds).
} // namespace tandem::story
