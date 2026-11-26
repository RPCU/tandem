#pragma once

// The observer (docs/architecture/observer.md; tandem.ini `observer = on`):
// what a play session leaves behind for debugging, with no marking by hand.
// Its platform-independent parts, tested on Linux (tests/observer_test.cpp):
//
// - Classify: which log lines are triggers (a degraded state or fallback, a
//   body respawned, a one-sided copy repaired, an error) and how bad;
// - Ring: the last seconds of the detailed log, in memory, bounded;
// - Limiter: at most one capture per trigger kind a minute, a cap a run;
// - Trail: story messages (the host's as sent, a guest's as heard) and the
//   plugin's own notes as compact rows, only on a change;
// - Tally: the summary (counts per trigger, the worst moments ranked, the
//   quests, scenes and districts seen).
//
// The plugin (plugin/src/observer.hpp) owns the files, the threads and the
// clock; nothing here reads one.

#include "tandem/protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace tandem::observer {

enum class Trigger : std::uint8_t {
  Stall,       // the game thread silent long enough to be a crash or a hang
  Degraded,    // a state of the registry started (this game's or another's)
  Fallback,    // a fallback of the registry was taken
  Respawn,     // a body was gone and spawned again (proxy, street vehicle)
  OneSided,    // a copy one game had and the other did not (resync, orphan, missing)
  Error,       // an error line of the plugin
  ScriptError, // a script line that says it failed
  Drift,       // a copy far from where its owner says it is
  Frame,       // a frame over the limit
  Manual,      // asked for (cmd observer capture)
};
inline constexpr std::size_t kTriggers = 10;
const char *NameOf(Trigger kind);
std::optional<Trigger> TriggerNamed(std::string_view name);

// A trigger: its kind, what it is about (the line, or the reason), how bad
// (0..100: ranks the worst moments).
struct Hit {
  Trigger kind{};
  std::string detail;
  double severity{};
};
// Severity of a drift of `metres` and of a frame of `ms`.
double DriftSeverity(float metres);
double FrameSeverity(float ms);
// The frame to judge at a script tick, in us (0: none): `previousUs` the
// last tick (0: this is the session's first), `nowUs` this one, and the
// engine's frames between them (`engineFrames`, the longest
// `engineLongestUs`). A gap that held 3 engine frames or more is a pause
// menu or a loading screen holding the script tick: its longest engine
// frame is the frame. The session's first tick judges nothing: the engine
// frames counted before it are the save's loading screen (a loading frame
// of 1370 ms read as the session start's, 2026-11-26).
std::uint64_t FrameToJudgeUs(std::uint64_t previousUs, std::uint64_t nowUs,
                             std::uint32_t engineFrames, std::uint64_t engineLongestUs);

// A log line (its text as written, without the time; `level` 'E', 'W', 'I'
// or 'D') that is a trigger, or none. The observer's own lines never are.
std::optional<Hit> Classify(char level, std::string_view line);

// The detailed log of the last `keepUs`, at most `maxBytes` of text.
struct Line {
  std::uint64_t us{};
  char level{};
  std::string text;
};
class Ring {
public:
  explicit Ring(std::uint64_t keepUs = 40'000'000, std::size_t maxBytes = 8u << 20);
  void Push(std::uint64_t us, char level, std::string text);
  // Lines from `fromUs` to `toUs` (inclusive), oldest first.
  std::vector<Line> Between(std::uint64_t fromUs, std::uint64_t toUs) const;
  std::size_t Size() const { return m_lines.size(); }
  std::size_t Bytes() const { return m_bytes; }
  // Lines that left early, the byte cap reached before their time was up.
  std::uint64_t Squeezed() const { return m_squeezed; }

private:
  std::deque<Line> m_lines;
  std::uint64_t m_keepUs;
  std::size_t m_maxBytes, m_bytes{};
  std::uint64_t m_squeezed{};
};

// Whether a trigger is captured: one per kind every `gapUs`, `cap` a run.
class Limiter {
public:
  explicit Limiter(std::uint64_t gapUs = 60'000'000, std::uint32_t cap = 30);
  enum class Verdict { Capture, TooSoon, Capped };
  Verdict Ask(Trigger kind, std::uint64_t nowUs);
  std::uint32_t Taken() const { return m_taken; }
  std::uint32_t Cap() const { return m_cap; }

private:
  std::uint64_t m_gapUs;
  std::uint32_t m_cap, m_taken{};
  std::map<Trigger, std::uint64_t> m_last;
};

// One row of the story trail: `kind` one word (session, phase, scene, cast,
// scene-kind, journal, tracked, call, reward, effect, restrictions, prefabs,
// gates, tier, fast-travel, story-flags, district, epoch, watchdog, trigger,
// capture, summary, observer), `text` one line.
struct Row {
  std::uint64_t us{};
  std::string kind;
  std::string text;
};

class Trail {
public:
  // A story message our game sent (`from` kNoPlayer) or heard (any other
  // `from`; a StoryState or StoryGates names its player itself): its rows,
  // none when it changes nothing the trail shows.
  std::vector<Row> Note(const proto::Message &message, std::uint64_t us,
                        proto::PlayerId from = proto::kNoPlayer);
  // A quest phase resource running: a row the first time this run.
  std::optional<Row> Phase(std::uint64_t resource, std::uint64_t us);
  // The district our V is in (its record's name): a row when it changed.
  std::optional<Row> District(std::string_view name, std::uint64_t us);
  // A session's start or end: what is "now" is forgotten (scenes, gates…);
  // what was seen stays for the summary.
  void Reset();

  // What is going on now, one line (for a capture's context).
  std::string Now() const;
  // Seen this run, in order of first sight.
  const std::vector<std::string> &Districts() const { return m_districts; }
  const std::vector<std::uint32_t> &Quests() const { return m_quests; }
  const std::vector<std::uint64_t> &Scenes() const { return m_scenes; }
  std::size_t Phases() const { return m_phases.size(); }

private:
  struct Per {
    std::uint8_t tier{}, flags{};
    bool stated{};
  };
  std::map<proto::PlayerId, Per> m_story; // StoryState by player (0: ours)
  std::uint64_t m_scene{};
  std::uint32_t m_play{};
  std::vector<proto::SceneActor> m_cast;
  std::uint32_t m_tracked{};
  std::vector<std::uint64_t> m_restrictions, m_prefabs;
  std::int32_t m_gates{-1};
  std::string m_district;
  std::uint64_t m_lastPhase{};
  std::set<std::uint64_t> m_phases;
  std::vector<std::string> m_districts;
  std::vector<std::uint32_t> m_quests;
  std::vector<std::uint64_t> m_scenes;
};

// A trigger that happened (captured or not: `capture` is its file, "" none).
struct Moment {
  std::uint64_t us{};
  Trigger kind{};
  double severity{};
  std::string detail;
  std::string capture;
};

class Tally {
public:
  void Add(Moment moment);
  std::uint32_t Count(Trigger kind) const { return m_counts[static_cast<std::size_t>(kind)]; }
  // The worst `n` moments, worst first (equal ones: earliest first).
  std::vector<Moment> Worst(std::size_t n) const;
  // The summary: `title`, the counts, the worst moments (times as `clock`
  // makes them), the captures written, what the trail saw.
  std::string Summary(std::string_view title, const Trail &trail, std::uint32_t captures,
                      const std::function<std::string(std::uint64_t)> &clock) const;
  static constexpr std::size_t kKeep = 10;

private:
  std::uint32_t m_counts[kTriggers]{};
  std::vector<Moment> m_worst; // the kKeep worst
};

// A TSV field: tabs and line ends become spaces.
std::string Field(std::string_view text);
// Names a journal entry state (gameJournalEntryState) for the trail.
const char *JournalStateName(std::uint8_t state);
} // namespace tandem::observer
