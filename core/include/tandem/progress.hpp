#pragma once

// Progression in co-op (docs/architecture/progression.md): the rules that
// are plain arithmetic or a state machine, kept here so the plugin and the
// mock player apply them the same way and they are tested on Linux.
//
//   levels  the session's level (the mean of its players' levels) and the
//           power level modifier a host NPC and its proxies carry for it;
//   saves   what a guest's game does about saving when it leaves a session.

#include "tandem/protocol.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace tandem::progress {
// ---- levels ----

// The session's level: the mean of its players' character levels, rounded
// to the nearest (a half goes up). 0 with no player.
std::uint8_t MeanLevel(std::span<const std::uint8_t> levels);

// The highest power level an NPC is moved to (BaseStats.PowerLevel's Max is
// read in game; this is the fallback).
inline constexpr float kMaxNpcLevel = 60.f;

// One host NPC, as its game sees it now.
struct NpcScale {
  float level{};        // its power level now, our modifier included
  float applied{};      // our modifier on it now (0: none)
  float hostLevel{};    // the host V's level
  float sessionLevel{}; // the session's level (0: the host plays alone)
  // The NPC's content assignment: the level its curve gives a player of the
  // host's level and one of the session's (both < 0: no curve was read),
  // or a constant (fixed: the game does not scale it to the player).
  float curveAtHost{-1.f}, curveAtSession{-1.f};
  bool fixed{};
  float maxLevel{kMaxNpcLevel};
};
// The additive PowerLevel modifier that NPC should carry so that it is
// scaled to the session's level instead of the host's (0: none). By the
// game's own curve when the NPC has one, else by the difference of the two
// levels; an NPC of a constant level keeps it; the result stays within
// 1..maxLevel.
float NpcShift(const NpcScale &npc);

// Guest: the modifier a proxy should carry so that its power level is the
// host's NPC's (`hostNpcLevel`), given its level now and our modifier on it.
float ProxyShift(float level, float applied, float hostNpcLevel);

// ---- saves ----

// tandem.ini `leave`: what a guest's game does once it is out of a session
// and its own world is back.
//   Stay    go on in place (saving unlocks);
//   Save    the same, then one save of the game's own (a quick save);
//   Reload  the same, then load that save: the world is rebuilt from it.
//           Only when the player chose to leave; a lost host is a Save.
enum class LeaveMode : std::uint8_t { Stay, Save, Reload };

// Co-op state still in a guest's game after a session (bits). While any is
// set no save is made: a save must never hold co-op state.
inline constexpr std::uint32_t kLeftStory = 1;      // the host's journal, or our story on hold
inline constexpr std::uint32_t kLeftDifficulty = 2; // the host's difficulty
inline constexpr std::uint32_t kLeftBodies = 4;     // puppets, proxies or car copies
inline constexpr std::uint32_t kLeftLevels = 8;     // level modifiers on NPCs
inline constexpr std::uint32_t kLeftHolds = 16;     // restrictions, god mode, effects on our V
inline constexpr std::uint32_t kLeftWorld = 32;     // the host's clock, weather, crowd setting
inline constexpr std::uint32_t kLeftDown = 64;      // our V is down, out or dying
inline constexpr std::uint32_t kLeftSession =
    128; // a ride, a pause or a time bubble of the session
// "story, bodies": for the log and the notice.
std::string LeftNames(std::uint32_t left);

enum class LeaveStep : std::uint8_t {
  Solo,     // not in a session, nothing owed
  Session,  // a guest in a session: no save
  Cleaning, // out of it: waiting for our own world to be back
  Waiting,  // clean; waiting for the game to allow a save (combat, a scene)
  Saving,   // a save was asked for
  Loading,  // the load of that save was asked for
  Dirty,    // our world did not come back: no save until a load
};
enum class LeaveAction : std::uint8_t {
  None,
  Clean,      // our world is back: saving unlocks
  Save,       // ask the game for the save now
  Saved,      // it was written (and there is no load to do)
  Load,       // it was written: load it
  SaveFailed, // not written, or no word of it: nothing is loaded
  GaveUp,     // the game never allowed a save: the player saves by hand
  Dirty,      // co-op state stayed: saving stays locked
};

struct LeaveInput {
  double now{};   // seconds on any steady clock
  bool session{}; // we are a guest in a session now
  bool chosen{};  // the session ended by our choice (the co-op menu's Leave)
  // A save is wanted whatever the mode, and no load: the pause menu's Exit
  // (the game quits after it) or a checkpoint (we join again after it). Out
  // of a session, it asks for the save the last one still owes (Owed).
  bool save{};
  std::uint32_t left{}; // kLeft*
  bool blocked{};       // the game's own save locks are on
  std::int8_t saved{};  // since Save: 0 no word yet, 1 written, -1 failed
};

inline constexpr double kLeaveSettle = 2.0;   // out of a session this long before a verdict
inline constexpr double kLeaveCleanBy = 20.0; // still not clean then: Dirty
inline constexpr double kLeaveSaveBy = 120.0; // the game still locked then: GaveUp
inline constexpr double kLeaveSavedBy = 30.0; // no word of the save then: SaveFailed
// With `save` (the player waits for it): the game still locked then: GaveUp.
inline constexpr double kLeaveSaveSoonBy = 15.0;

// A guest's way out of a session, one step per call. It never asks for a
// load without word that the save was written, and never for a save while
// anything of the session is left.
class Leave {
public:
  explicit Leave(LeaveMode mode = LeaveMode::Stay) : m_mode(mode) {}
  void SetMode(LeaveMode mode) { m_mode = mode; }
  LeaveMode Mode() const { return m_mode; }
  LeaveAction Tick(const LeaveInput &input);
  // Our save lock: on in a session and until our world is back.
  bool Locked() const;
  LeaveStep Step() const { return m_step; }
  // A session ended in this world and no save of ours was written since:
  // what our V earned there is only in the running game.
  bool Owed() const { return m_owed; }
  // A world was loaded: whatever was left went with the old one.
  void Reset() {
    m_step = LeaveStep::Solo;
    m_owed = false;
  }

private:
  LeaveMode m_mode;
  LeaveStep m_step{LeaveStep::Solo};
  double m_since{};
  bool m_chosen{};
  bool m_save{};
  bool m_owed{};
};

const char *NameOf(LeaveMode mode);
const char *NameOf(LeaveStep step);
const char *NameOf(LeaveAction action);
} // namespace tandem::progress
