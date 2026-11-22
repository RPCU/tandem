#pragma once

// Story sync, the parts that need no game (docs/research/coop-quests-design.md):
// what the host keeps for late joiners, who took part in a quest (for its
// rewards), and who answered a call first.

#include "tandem/moments.hpp"
#include "tandem/protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
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
  // a guest's proxies for good), the host's story restrictions (a lost
  // lift would keep a guest's weapons away for good) and its world prefabs
  // (a lost change would keep an interior loaded, or missing, for good). Calls, hubs, effects and
  // lines are short-lived and sent again on every change; StoryStates and StoryGates are left out
  // (a guest is never sent its own). `shown`: the NPCs the digest's viewer is
  // told of (the host's digest for one guest: its cast and props name only
  // those, as Endpoint::SceneFor sends them); empty: every one.
  std::uint32_t Digest(const std::function<bool(proto::NpcId)> &shown = {}) const;
  // A player left: its StoryState and StoryGates go; a call it answered is
  // the host's (`host`) from now on, for a newcomer never heard of it.
  void Forget(proto::PlayerId player, proto::PlayerId host);
  // What to send a newcomer, in order: the journal snapshot first. With the
  // host's clock, not the lines that are over (a bark of minutes ago names
  // an NPC the newcomer may never be told of).
  std::vector<proto::Message> ForJoiner(std::uint64_t hostNowUs = 0) const;
  std::size_t JournalSize() const { return m_journal.size(); }
  // A scene plays (scene replay).
  bool SceneOn() const { return m_scene.has_value(); }
  // Its cast as last said (none: no scene, or no cast said yet).
  const std::optional<proto::SceneCast> &Cast() const { return m_cast; }
  // What of the scene playing names its NPCs: its cast, then its props.
  std::vector<proto::Message> SceneCastNow() const;
  // The host's V has story restrictions on now.
  bool RestrictionsOn() const { return !m_restrictions.effects.empty(); }
  // The host's quests have prefabs loaded now (WorldPrefabs).
  bool PrefabsOn() const { return !m_prefabs.prefabs.empty(); }
  const proto::WorldPrefabs &Prefabs() const { return m_prefabs; }
  // A player's story gates as last said (none: never said).
  std::optional<proto::StoryGates> GatesOf(proto::PlayerId player) const;

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
  std::map<std::uint32_t, proto::SceneProp> m_props;    // held in the play, by prop number
  std::map<std::uint32_t, proto::ScenePlace> m_placed;  // set down in the play, by prop number
  std::vector<proto::SceneLine> m_lines;                // begun and not cut, newest last
  proto::StoryRestrictions m_restrictions;              // on the host's V now (empty: none)
  proto::WorldPrefabs m_prefabs;                        // the host's quests' now (empty: none)
  std::map<proto::PlayerId, proto::StoryGates> m_gates; // every player's, as said
};
// How many begun lines the record keeps (ForJoiner leaves out the ones that
// are over, and a joiner drops those it gets too late: scene::Replay).
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
  // The player who answered left: the call is the host's (`host`) from now
  // on (a joiner never heard of that player; nobody else may answer it).
  void Forget(proto::PlayerId player, proto::PlayerId host);

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

// ---- Spectating (docs/architecture/story.md, "Spectating") ----
//
// A scripted moment of the host's V, read from its own game, no quest list:
// what it is in now (MomentReasons). Spectating is the last resort: a guest
// that shared the moment plays it from its own V (scene replay, the
// restrictions, the ride along) and watches through the host's camera, its
// own V parked, only while it cannot take part from its own body
// (Spectator's reasons).
struct MomentSignals {
  std::uint8_t tier{}; // the host's scene tier (GameplayTier 1..5)
  bool isPrivate{};    // in a private scene: never watched
  bool restricted{};   // quest restrictions on how it moves (not only weapons away)
  bool questMount{};   // mounted by its scene or quest (transit kind Scene)
  bool mode{};         // a scripted body or camera mode (NoMovement, ForceCrouch...)
  bool camera{};       // the quest's camera animation holds the host's view
  bool elsewhere{};    // cyberspace, a braindance, playing as Johnny
  bool test{};         // cmd story spectate on (the host's test moment)
  // The moment list (tandem/moments.hpp): what its scene, quest phase or
  // tracked entry is marked, if anything.
  moments::Verdict listed{};
};
inline constexpr std::uint16_t kMomentScene = 1, kMomentRestricted = 2, kMomentMounted = 4,
                               kMomentMode = 8, kMomentElsewhere = 16, kMomentTest = 32,
                               kMomentCutscene = 64, kMomentCamera = 128, kMomentListed = 256;
// The reasons only the host's view shows (kStoryWatch): a cutscene (tier 5),
// another world, the quest's camera, a moment the list marks watch, the test
// moment. The others (a scene of tier 2 to 4, restrictions, a quest ride, a
// body mode) are played.
inline constexpr std::uint16_t kMomentWatch =
    kMomentCutscene | kMomentElsewhere | kMomentCamera | kMomentListed | kMomentTest;
// The reasons the host's V is in a moment now (0: none; always 0 in a
// private scene). A fight or the player's own driving is none of them. A
// moment the list marks watch is one (kMomentListed) whatever else holds;
// one it marks play has no cutscene nor camera to watch: a tier 5 scene is
// a scene, the quest's camera a body mode, both played.
std::uint16_t MomentReasons(const MomentSignals &signals);

// The host's moment, held kMomentHoldUs after its reasons go, so that a
// scene's sections, a loading screen or a mount between two rides do not
// end it and start another; its watch reasons (kMomentWatch) held alike.
inline constexpr std::uint64_t kMomentHoldUs = 1'500'000;
class HostMoment {
public:
  // True while the moment is on.
  bool Update(std::uint16_t reasons, std::uint64_t nowUs);
  bool On() const { return m_on; }
  // A watch reason is on (kStoryWatch).
  bool Watch() const { return m_watch; }
  // A private scene, or the session's end: off at once.
  void Clear();

private:
  bool m_on{}, m_watch{};
  std::uint64_t m_lastUs{}, m_watchLastUs{};
};

// How far the host's V may be taken from ours before we cannot follow it
// from our own body (the NPC links' reach, regions.md); back in reach at
// kSpectateReachBack. A moment's ride that leaves without us (no seat, or we
// did not get in) has taken it once it is kSpectateLeftBehind away: we watch
// the ride, carried in its copy, rather than stand where it left us.
inline constexpr float kSpectateReach = 150.f;
inline constexpr float kSpectateReachBack = 120.f;
inline constexpr float kSpectateLeftBehind = 30.f;
// The host's quests have interiors on that our game does not load (quest
// prefabs: the layout check failed, or quest_prefabs = off): we watch while
// the host is within kSpectateReach (kSpectateMissingLeave to stop), as its
// V walks where our world has nothing.
inline constexpr float kSpectateMissingLeave = 170.f;

struct SpectateInput {
  bool on{};            // tandem.ini spectate, and not turned off for the session
  bool hostMoment{};    // the host says its V is in a moment (kStoryMoment)
  bool hostWatch{};     // ... one only its view shows (kStoryWatch)
  bool hostPrivate{};   // the host is in a private scene (kStoryPrivate)
  bool grouped{};       // the host's world is ours
  bool ourAway{};       // our own V is in another world
  bool hostElsewhere{}; // the host's V is in another world (Elsewhere)
  std::uint8_t hostTier{};
  bool hostRiding{}; // the host's V is in a vehicle (its copy here)
  // The host's moment is restrictions alone: no scene (tier under 2) and no
  // ride its scene or quest drives. Its V walking or driving off then is
  // its own free roam: never (c).
  bool hostFree{};
  bool inVehicle{};  // our V rides (along with the host, or a car of its own)
  bool cannotLoad{}; // the host's quests have prefabs on that our game does not load
  bool hostKnown{};
  proto::Vec3 host; // the host's V
  proto::Vec3 guest;
};
// A guest's rule. It shares the host's moment when it stands in it once
// (InMoment's: grouped, in this world, within kMomentNear), and from then
// on, until the moment ends, watches only while one of these holds:
// (a) a cutscene, (b) the host's V in another world, (d) the quest's camera
// (the host's kStoryWatch: tier 5, Elsewhere, else d), or (c) the host's V
// taken beyond our reach (more than kSpectateReach away while we ride
// nothing; in reach again at kSpectateReachBack; watching while it rides,
// carried in its copy, it stays (c) until its ride ends; a ride of the
// moment that leaves without us takes it at kSpectateLeftBehind). Apart from
// any moment: (e) the host's V among quest interiors our game does not load,
// within kSpectateReach. The reason gone, it plays again at once. "Stop
// watching" declines until the reason ends.
class Spectator {
public:
  enum class Change : std::uint8_t { None = 0, Start = 1, Stop = 2 };
  enum class Why : std::uint8_t {
    None = 0,
    Cutscene = 1,
    Elsewhere = 2,
    Taken = 3,
    View = 4,
    Missing = 5
  };
  Change Update(const SpectateInput &in);
  // The player pressed "Stop watching".
  Change Leave();
  // The session is over or the host vanished.
  Change Lift();
  bool Watching() const { return m_watching; }
  bool Declined() const { return m_declined; }
  bool Shared() const { return m_shared; }
  // Why we watch (None: we do not).
  Why Reason() const { return m_why; }

private:
  bool m_watching{};
  bool m_declined{}; // left: until the reason ends
  bool m_shared{};   // stood in this moment once
  Why m_why{};
};

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
