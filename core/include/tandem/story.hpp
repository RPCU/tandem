#pragma once

// Story sync, the parts that need no game (docs/research/coop-quests-design.md):
// what the host keeps for late joiners, who took part in a quest (for its
// rewards), and who answered a call first.

#include "tandem/protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace tandem::story {
// The host's story as a player joining now needs it: the journal (its
// snapshot plus every change since), the tracked entry, the call that is up,
// the open choice hub, every player's StoryState, and the scene that plays
// (tandem/scene.hpp: its state, its cast, the lines being spoken).
class Record {
public:
  // A story message the host sent (or relayed); a guest notes what it
  // receives the same way. False when it changed nothing (a repeat).
  bool Note(const proto::Message &message);
  // The story domain's hash for digests (tandem/consistency.hpp): the
  // journal and the tracked entry, what lasts and what a resync (ForJoiner)
  // sends whole, the scene that plays with its cast (a lost end would hold
  // a guest's proxies for good), and the host's story restrictions (a lost
  // lift would keep a guest's weapons away for good). Calls, hubs, effects and lines are
  // short-lived and sent again on every change; StoryStates are left out (a
  // guest is never sent its own).
  std::uint32_t Digest() const;
  // A player left: its StoryState goes.
  void Forget(proto::PlayerId player);
  // What to send a newcomer, in order: the journal snapshot first.
  std::vector<proto::Message> ForJoiner() const;
  std::size_t JournalSize() const { return m_journal.size(); }
  // A scene plays (scene replay).
  bool SceneOn() const { return m_scene.has_value(); }
  // The host's V has story restrictions on now.
  bool RestrictionsOn() const { return !m_restrictions.effects.empty(); }

private:
  std::map<std::uint32_t, proto::JournalChange> m_journal;
  bool m_haveJournal{};
  std::optional<proto::Tracked> m_tracked;
  std::optional<proto::PhoneCall> m_call;
  std::optional<proto::ChoiceHub> m_hub;
  std::map<proto::PlayerId, proto::StoryState> m_states;
  std::map<std::uint64_t, proto::StoryEffect> m_effects; // on now
  std::optional<proto::SceneState> m_scene;              // playing now
  std::optional<proto::SceneCast> m_cast;
  std::map<std::uint32_t, proto::SceneProp> m_props; // held in the play, by prop number
  std::vector<proto::SceneLine> m_lines;             // begun and not cut, newest last
  proto::StoryRestrictions m_restrictions;           // on the host's V now (empty: none)
};
// How many begun lines the record keeps (a joiner drops the ones that are
// over: scene::Replay).
inline constexpr std::size_t kKeptLines = 8;

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
// ---- Story restrictions (docs/architecture/story.md, "Restrictions") ----
//
// Who shares the host's story moment. The moment is on while the host's V
// is in a scene (tier 2 and up, private scenes included: a guest inside
// one's area could break it as well) or has story restrictions on. A guest
// is in it when it is grouped with the host (the same world, not apart),
// neither V is in another world, and it stands inside the moment's area:
// within kMomentNear of the host's V (the earshot of the host's scene
// voices, scene-replay.md §5; 10 m more to leave it, so walking along the
// edge does not flicker), or inside the staged scene when scene replay
// says where it is (its actors' spread, at most kMomentStageCap, plus
// kMomentStageMargin; 8 m more to leave).
inline constexpr float kMomentNear = 40.f;
inline constexpr float kMomentNearLeave = 50.f;
inline constexpr float kMomentStageCap = 30.f;
inline constexpr float kMomentStageMargin = 12.f;
inline constexpr float kMomentStageLeave = 20.f;

struct MomentInput {
  bool grouped{};      // the host's world is ours (TandemStorySystem.Grouped)
  bool away{};         // our V or the host's is in another world (cyberspace...)
  bool hostKnown{};    // the host's V's position is known
  proto::Vec3 host;    // the host's V
  proto::Vec3 guest;   // our V
  std::uint8_t tier{}; // the host's scene tier
  bool restricted{};   // the host has story restrictions on
  bool staged{};       // scene replay knows where the scene is staged
  proto::Vec3 anchor;  // the staging area's middle
  float radius{};      // its actors' farthest distance from it
};
// `was`: our V was in the moment at the last check (hysteresis).
bool InMoment(const MomentInput &in, bool was);

// A guest's restrictions: what its own V has from the host's moment and the
// session's scene holds, so that only what tandem put on comes off. An
// effect our V already had (its own game's: a zone of its own world, its
// own quest) is "theirs": never applied, never removed by us.
class Restrictions {
public:
  // The host's set, as heard (sorted, as the protocol has it).
  void Host(const proto::StoryRestrictions &message) { m_host = message.effects; }
  const std::vector<std::uint64_t> &HostSet() const { return m_host; }

  struct Change {
    std::vector<std::uint64_t> apply;  // put on our V (ours from now on)
    std::vector<std::uint64_t> remove; // take off (ours, no longer wanted)
    bool Empty() const { return apply.empty() && remove.empty(); }
  };
  // Each check: in the moment or not, plus the holds the host's scene tier
  // asks for (Story.reds: weapons lowered, invulnerable). Applies what is
  // wanted and not held yet; removes what is ours and no longer wanted.
  Change Update(bool inMoment, const std::vector<std::uint64_t> &holds = {});
  // Our V had `effect` already when we came to apply it, or its own game
  // put it on again while ours was on: it is theirs from now on.
  void Theirs(std::uint64_t effect);
  // Our V lost `effect`, not through us (its own quest, a cleanse): we hold
  // nothing of it; the next Update puts it on again if still wanted.
  void Gone(std::uint64_t effect);
  // The session is over, the host gone, or our game loads: every one of
  // ours off, the host's set forgotten.
  Change Lift();

  bool Holds(std::uint64_t effect) const { return m_held.contains(effect); }
  bool Ours(std::uint64_t effect) const;
  std::size_t OursCount() const;

private:
  std::vector<std::uint64_t> m_host;
  std::map<std::uint64_t, bool> m_held; // effect: true ours, false theirs
};

// The host's backstop to the guests' own restrictions: while the host's V is
// in a restricted moment (a scene, tier 2 and up, or story restrictions on),
// a guest's act on one of the scene's actors is refused: a strike, a
// takedown, a quickhack (its upload and the effects it lands). An actor is
// one of scene replay's cast, or an NPC in a scene that the quest marks
// (IsQuest) or keeps alive (god mode). Each refusal is said in the log at
// most once a second per kind of act, so a burst of quickhack damage ticks
// does not hide the upload's line, nor fill the log.
enum class GuestAct : std::uint8_t { Strike = 0, Takedown = 1, Quickhack = 2 };
inline constexpr std::uint64_t kBackstopLogGapUs = 1'000'000;

struct SceneActor {
  bool cast{};    // scene replay casts it in the scene the host announces
  bool quest{};   // the quest marks it (GameObject.IsQuest)
  bool kept{};    // god mode keeps it alive (Immortal or Invulnerable)
  bool inScene{}; // it is in a scene now (IsEntityInScene)
};
bool IsSceneActor(const SceneActor &npc);

class Backstop {
public:
  struct Verdict {
    bool refuse{};
    bool log{}; // say so now (the first in a second for this act)
    friend bool operator==(const Verdict &, const Verdict &) = default;
  };
  // `moment`: our V (the host's) is in a restricted moment now.
  Verdict Judge(bool moment, const SceneActor &npc, GuestAct act, std::uint64_t nowUs);
  std::uint32_t Refused(GuestAct act) const { return m_refused[Index(act)]; }

private:
  static std::size_t Index(GuestAct act) { return static_cast<std::size_t>(act) % 3; }
  std::array<std::optional<std::uint64_t>, 3> m_loggedUs{};
  std::array<std::uint32_t, 3> m_refused{};
};

// Text replies need no arbiter here: the host applies them in the order
// they arrive, on its game thread, and a reply whose choice entry is no
// longer Active (another reply won) is dropped there (Story.reds).
} // namespace tandem::story
