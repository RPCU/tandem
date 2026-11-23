#pragma once

// The observer (tandem.ini `observer = on`; docs/architecture/observer.md):
// every play session leaves a story trail, automatic captures of the last
// 30 s of the detailed log when something goes wrong, and a summary, under
// red4ext/plugins/tandem/observer/. Off by default; off, it costs one atomic
// load per log line and hook.
//
// Threads: every entry point takes one short lock of its own and does no
// I/O and no engine call (log lines arrive from every thread, the quest
// node hook and the puppet controllers run on job threads). A writer thread
// of its own writes the files, watches the game thread for a stall, and
// writes the summary every 30 minutes; it takes other locks (Runtime's, the
// registry's) only while holding none of its own.

#include "observing.hpp"

#include "tandem/observer.hpp"
#include "tandem/protocol.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace tandem::plugin {
class Observer {
public:
  static Observer &Get();
  // Cheap, from any thread: whether anything is observed.
  static bool On() { return log::g_observe.load(std::memory_order_relaxed); }

  struct Settings {
    bool enabled{};
    std::filesystem::path dir;        // observer/ beside tandem.ini
    float driftMetres{5.f};           // a copy this far from its owner's transform, 1 s
    std::uint32_t frameMs{50};        // a frame over this
    std::uint32_t maxCaptures{30};    // a game run
    std::uint64_t maxBytes{1u << 30}; // the folder; the oldest files go first
  };
  // Plugin load (main.cpp): the files are named, the writer starts.
  void Configure(Settings settings);
  // Plugin unload: the summary, the writer stops.
  void Shutdown();
  float DriftMetres() const { return m_settings.driftMetres; }

  // Any thread (log.hpp): a log line as written, debug lines too.
  void Line(char level, const std::string &text);
  // A story message our game sends (game thread, Story::Send) or heard
  // (net thread, Story::PushInbound).
  void Story(const proto::Message &message, bool heard);
  // Any thread (the quest node hook): a quest phase resource runs.
  void Phase(std::uint64_t resource);
  // Game thread (StoryWorld.reds, once a second): the district record.
  void District(std::string_view name);
  // Any thread: a row of the trail (scene-kind, epoch…).
  void Note(std::string_view kind, std::string text);
  // Game thread, every frame (TakeEvents): a frame over the limit triggers.
  void Frame();
  // Animation thread (the puppet controller): player's puppet has stood
  // `metres` from where its owner says for a second.
  void Drift(proto::PlayerId player, float metres, const proto::Vec3 &body,
             const proto::Vec3 &target);
  // Our V where the scripts sampled it (game thread); another player's as
  // its state came (net thread). For the captures' context.
  void Local(const proto::Vec3 &position);
  void Remote(proto::PlayerId player, const proto::Vec3 &position);
  // Net thread: the player left; its last state goes from the context.
  void Forget(proto::PlayerId player);
  // Runtime::Start / Stop (game thread): a session began (`what`: its role
  // and address) or ended (the summary is written).
  void SessionStarted(std::string what);
  void SessionEnded();
  // (cmd observer capture) a capture now, of kind manual.
  void Capture(std::string reason);
  // (cmd observer, cmd observer summary) what it did and costs; the
  // summary written now.
  std::string Status();
  void SummaryNow(std::string_view why);

private:
  struct Pending {
    observer::Trigger kind{};
    std::uint64_t us{}, dueUs{};
    double severity{};
    std::uint32_t number{};
    std::string detail, context, file;
  };
  struct Seen {
    proto::Vec3 position;
    std::uint64_t us{};
  };

  void Run(std::stop_token stop);
  // Under m_lock.
  void HitLocked(observer::Hit hit, std::uint64_t us, std::uint64_t delayUs);
  void RowLocked(std::string kind, std::string text, std::uint64_t us);
  std::string ContextLocked(std::uint64_t us) const;
  // The writer, holding no lock of ours.
  void WriteRows(std::vector<observer::Row> rows);
  void WriteCapture(const Pending &pending);
  void WriteSummary(std::string_view why);
  void WatchStall(std::uint64_t now);
  void Prune();
  std::string Clock(std::uint64_t us) const;

  Settings m_settings;
  std::string m_stamp; // the run's files: session-STAMP.tsv, summary-STAMP.txt
  std::uint64_t m_startUs{};
  std::int64_t m_startSystemMs{};

  mutable std::mutex m_lock; // everything below to the writer's own
  observer::Ring m_ring;
  observer::Limiter m_limiter;
  observer::Trail m_trail;
  observer::Tally m_tally;
  std::vector<observer::Row> m_rows;
  std::vector<Pending> m_pending;
  std::optional<Seen> m_local;
  std::map<proto::PlayerId, Seen> m_remote;
  std::string m_session; // "" no session
  std::uint32_t m_captures{}, m_written{}, m_refused{};
  std::uint64_t m_rowsWritten{};
  std::string m_summaryWanted; // why the writer writes the summary next ("" not yet)
  // Trigger rows of each kind: the last written, how many since then were not.
  std::uint64_t m_lastRowUs[observer::kTriggers]{};
  std::uint32_t m_unrowed[observer::kTriggers]{};
  std::uint64_t m_lastFrameUs{}; // game thread
  bool m_stalled{};              // the writer's: a stall capture for this silence
  std::uint64_t m_stalledSinceUs{};

  // Cost (any thread).
  std::atomic<std::uint64_t> m_lines{}, m_lineNs{}, m_lineMaxNs{};
  std::atomic<std::uint64_t> m_frames{}, m_gameNs{};

  std::uint64_t m_nextSummaryUs{};
  std::jthread m_writer;
};
} // namespace tandem::plugin
