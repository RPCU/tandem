#pragma once

// The recorder (tandem.ini `recorder = on`, docs/ARCHITECTURE.md "Recorder"):
// what the game sends, or would send to a guest, written as it plays, solo
// or in a session, for later replays and tests.
//
// Segments: `<dir>/YYYYMMDD-HHMMSS.tsession`, one per `segmentSeconds` (10
// minutes by default), in the .tplay layout (tandem/play.hpp) with the
// session magic: play::Load reads them, a crash's partial one too (it ends
// inside an entry at worst; flushed every second). Each closed segment gets
// a line in `<dir>/index.jsonl` (what happened in it, where, what recording
// cost); a segment a crash left without one is indexed at the next start.
// The oldest segments go once the folder is over `maxBytes`.
//
// Safe by design: every file error disables the recorder (logged once), it
// never throws, it only writes inside its folder. Not thread-bound: calls
// are serialized by a lock, and the writing thread is the caller's (the net
// thread or the recorder's own, never the game thread).

#include "tandem/play.hpp"
#include "tandem/protocol.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace tandem::record {
// What a segment holds, for its index line.
struct Summary {
  std::uint64_t messages{};
  std::uint64_t states{}, poses{}, shots{}, hits{}, npcSpawns{}, vehicleStates{};
  std::uint64_t subtitles{}, choiceHubs{}, calls{}, journal{}, travels{}, deaths{}, items{};
  std::uint64_t world{};
  std::optional<proto::Vec3> first, last; // our V's positions
  double distance{};                      // metres our V moved (on foot or driving)

  void Add(const proto::Message &message);
  // "combat", "driving", "scene", "call", "fast travel", "death"... (what to
  // look for in a segment), from the counts.
  std::vector<std::string> Tags() const;
};

struct Options {
  std::filesystem::path dir;            // the recordings folder (created)
  std::uint64_t segmentUs{600'000'000}; // 10 minutes of host clock
  std::uint64_t maxBytes{2ull << 30};   // 2 GB
  std::uint64_t flushUs{1'000'000};
  bool solo{}; // no session: the header says so (player 1)
  proto::PlayerId player{1};
  proto::Body body{};
};

// One index.jsonl line (no trailing newline).
struct IndexLine {
  std::string file;
  std::string started; // local "YYYY-MM-DD HH:MM:SS"
  double seconds{};
  std::uint64_t bytes{};
  bool solo{};
  bool partial{}; // a crash's segment, indexed afterwards
  Summary summary;
  std::string cost; // a JSON object (the caller's measurements), "" none
};
std::string FormatIndex(const IndexLine &line);
// The `file` of each line of an index (to find segments without one).
std::vector<std::string> IndexedFiles(const std::string &text);

// Deletes the oldest segments (by name) of `dir` until it holds at most
// `maxBytes` of them; `keep` (the open one) is never deleted. Returns the
// names deleted.
std::vector<std::string> Prune(const std::filesystem::path &dir, std::uint64_t maxBytes,
                               const std::string &keep = {});

// "YYYYMMDD-HHMMSS" for a wall clock time (local time).
std::string SegmentName(std::int64_t unixSeconds);

class Recorder {
public:
  using Log = std::function<void(const std::string &)>;
  // The caller's cost measurements for the segment that closes (a JSON object).
  using Cost = std::function<std::string()>;

  // Opens the folder: indexes what a crash left, prunes. False: disabled
  // (the reason went to `log`).
  bool Start(Options options, Log log, Cost cost = {});
  // `hostUs`: when it was sent, on the session's clock (solo: the local one).
  void Add(std::uint64_t hostUs, const proto::Message &message);
  // Closes a segment that ran its time, flushes; call a few times a second.
  void Tick(std::uint64_t hostUs);
  // Closes the current segment now (the next message opens one).
  void Cut();
  void Stop();

  bool Active() const;
  bool Failed() const;
  std::string Current() const; // the open segment's file name, "" none
  std::uint64_t Segments() const;

private:
  bool OpenSegment(std::uint64_t hostUs);
  void CloseSegment(bool partial);
  void Fail(const std::string &why);
  void WriteIndex(const IndexLine &line);
  void IndexLeftovers();

  mutable std::mutex m_lock;
  Options m_options;
  Log m_log;
  Cost m_cost;
  bool m_started{}, m_failed{};
  play::Writer m_writer;
  std::string m_name, m_startedText;
  std::uint64_t m_startUs{}, m_lastFlushUs{}, m_lastUs{};
  std::uint64_t m_segments{};
  Summary m_summary;
};
} // namespace tandem::record
