#pragma once

// Activities' queues between the net thread and the game thread
// (scripts/tandem/Activities.reds through plugin/src/activity_natives.cpp;
// docs/architecture/activities.md). Kept apart from Runtime, like combat:
// what our V is busy with (newest only), the others' (latest, with a count
// of changes), and time skips both ways (queued).

#include "tandem/protocol.hpp"

#include <map>
#include <mutex>
#include <optional>
#include <variant>
#include <vector>

namespace tandem::plugin {
// A time skip, for the scripts: asked of us, a vote on ours, or how one
// ended.
struct SkipEvent {
  enum class Kind : std::int32_t { Asked = 1, Voted = 2, Ended = 3 };
  Kind kind{};
  proto::TimeSkip skip;         // Asked
  proto::TimeSkipVote vote;     // Voted
  proto::TimeSkipResult result; // Ended
};

// Ours to the session (game -> net).
struct SkipAsk {
  proto::SkipReason reason{};
  std::int32_t fromSeconds{}, toSeconds{};
};
struct SkipAnswer {
  proto::PlayerId asker{};
  std::uint32_t request{};
  bool accepted{};
  std::uint8_t why{};
};
struct SkipWithdraw {};
using SkipOut = std::variant<SkipAsk, SkipAnswer, SkipWithdraw>;

// A street race (Races.reds): a word of the session's, or what our own
// game said (our race HUD's mode, a reward our V got), queued for the tick.
struct RaceNote {
  enum class Kind : std::int32_t { Heard = 1, Ui = 2, Reward = 3 };
  Kind kind{};
  proto::Race race;                                     // Heard
  std::int32_t mode{}, maxPosition{}, maxCheckpoints{}; // Ui (vehicleRaceUI)
  std::uint64_t reward{};                               // Reward (TweakDB id)
};

class Activities {
public:
  static Activities &Get();

  // Game thread: our V's activity (sent when it changes); our skip requests
  // and answers; the others' activities and the skip events.
  void SetLocal(const proto::Activity &state);
  bool LocalFrozen();
  void Send(SkipOut message);
  std::map<proto::PlayerId, proto::Activity> Remote(std::uint32_t &changes);
  std::vector<SkipEvent> TakeEvents();
  // Any thread (net thread, or a script hook that only queues): a race's
  // note; the game thread takes them on its tick.
  void PushRace(RaceNote note);
  std::vector<RaceNote> TakeRaces();

  // Net thread.
  std::optional<proto::Activity> TakeLocal();
  std::vector<SkipOut> TakeOutbound();
  void PushRemote(const proto::Activity &state);
  void PushEvent(SkipEvent event);
  // The session ended: nobody is busy, nothing asked is pending.
  void EndSession();

private:
  std::mutex m_lock;
  proto::Activity m_local;
  std::optional<proto::Activity> m_localToSend;
  std::vector<SkipOut> m_outbound;
  std::map<proto::PlayerId, proto::Activity> m_remote;
  std::uint32_t m_remoteChanges{};
  std::vector<SkipEvent> m_events;
  std::vector<RaceNote> m_races;
};
} // namespace tandem::plugin
