#pragma once

// Per-player regions between the two threads (docs/architecture/regions.md):
// the net thread hands the game thread who is linked with us, who owns each
// NPC and the hand-overs to act on; the game thread hands back its answers.
// The rules are in core/ (tandem/regions.hpp, Endpoint); the game's side is
// scripts/tandem/Regions.reds through region_natives.cpp.

#include "tandem/protocol.hpp"

#include <map>
#include <mutex>
#include <set>

#include <variant>
#include <vector>

namespace tandem::plugin {
struct RegionEvent {
  enum class Kind : std::int32_t {
    Give = 1,   // hand `npc` over to `player`: answer Give or Refuse
    Take = 2,   // `npc` is ours to run from now (state): answer Adopt
    Owner = 3,  // `npc` is `player`'s now (0: it left the session); `was`: before
    Linked = 4, // the players linked with us changed
  };
  Kind kind{};
  proto::NpcId npc{};
  proto::PlayerId player{};
  proto::PlayerId was{};
  proto::NpcHandoverState state; // Take
};

// What the game answers (game thread -> net thread).
struct RegionRefuse {
  proto::NpcId npc{};
};
struct RegionAdopt {
  proto::NpcId npc{};
  bool accepted{};
};
struct RegionPin {
  proto::NpcId npc{};
};
// A quest of our own world runs where our V stands: we keep our region.
struct RegionHeld {
  bool held{};
};
using RegionAnswer =
    std::variant<proto::NpcHandoverState, RegionRefuse, RegionAdopt, RegionPin, RegionHeld>;

class Regions {
public:
  static Regions &Get();

  // Net thread.
  void Push(RegionEvent event);
  void SetOwners(std::map<proto::NpcId, proto::PlayerId> owners);
  void SetLinked(std::vector<proto::PlayerId> linked);
  // Our group's leader (0: no session) and the players that hold their region.
  void SetGroup(proto::PlayerId leader, std::set<proto::PlayerId> holders);
  // Who runs the world around each player (Endpoint::AreaOwner).
  void SetAreas(std::map<proto::PlayerId, proto::PlayerId> areas);
  std::vector<RegionAnswer> TakeAnswers();
  // A session starts or ends: nothing of the last one is for the next.
  void Reset();

  // Game thread.
  std::vector<RegionEvent> Take();
  void Answer(RegionAnswer answer);
  // Who runs an NPC, as the session's record has it (0: not in it).
  proto::PlayerId OwnerOf(proto::NpcId npc);
  bool IsLinked(proto::PlayerId player);
  std::vector<proto::PlayerId> Linked();
  // Grouped or apart, for quests: any thread. Apart: in a session, linked
  // with nobody. The leader is the player whose world's quests are played
  // where we are (us when apart, or when we hold our region).
  bool IsApart();
  proto::PlayerId Leader();
  bool Holds(proto::PlayerId player);
  std::set<proto::PlayerId> Holders();
  // Any thread: the game that runs the world around `player` (its new NPCs,
  // crowd, traffic, police): its own unless deep in a higher-ranked linked
  // player's region (0: no session, or nothing heard of it yet).
  proto::PlayerId AreaOwner(proto::PlayerId player);

private:
  std::mutex m_lock;
  std::vector<RegionEvent> m_events;
  std::vector<RegionAnswer> m_answers;
  std::map<proto::NpcId, proto::PlayerId> m_owners;
  std::vector<proto::PlayerId> m_linked;
  proto::PlayerId m_leader{};
  std::set<proto::PlayerId> m_holders;
  std::map<proto::PlayerId, proto::PlayerId> m_areas;
};
} // namespace tandem::plugin
