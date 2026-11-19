#pragma once

// The recorder in the plugin (tandem.ini `recorder = on`; the format and
// files: core tandem/recorder.hpp). Independent of the role: solo, a writer
// thread of its own takes what the scripts submit (Runtime's outboxes, the
// same ones the net thread drains in a session) and writes it; in a session
// the net thread hands it everything its Endpoint sends (SetRecordTap). It
// never touches the engine, never writes outside recordings/auto/, and any
// error turns it off for the rest of the run, nothing else.

#include "tandem/protocol.hpp"
#include "tandem/recorder.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace tandem::plugin {
class Recording {
public:
  static Recording &Get();

  // At load: whether tandem.ini asks for it, and where (`dir`), how.
  void Configure(bool enabled, std::filesystem::path dir, std::uint64_t maxBytes,
                 std::uint64_t segmentUs);
  // Game thread: a game is loaded (the writer starts, once per run);
  // its world is gone (the segment closes).
  void GameLoaded(proto::Body body);
  void GameLeft();
  // The co-op menu's switch (until the game closes; tandem.ini stays as it is).
  void SetEnabled(bool enabled);
  bool Enabled() const { return m_enabled.load(); }
  // Recording now (enabled, a game loaded, not failed).
  bool Active() const;
  // The segment being written, or why not ("off", "waiting for a game", "failed").
  std::string Status() const;
  void Cut();
  void Shutdown(); // plugin unload

  // Any thread but the game thread: one message our game sent.
  void Add(const proto::Message &message);
  // Game thread: the scripts' recording work this frame, timed.
  void TickBegin();
  void TickEnd();

  // Runtime::Start / Stop: the solo drain stops before the net thread takes
  // the outboxes, and the clock of a segment is never mixed.
  void SessionStarting();
  void SessionStopped();

private:
  void Run(std::stop_token stop);
  void DrainSolo();
  std::string Cost();

  std::atomic<bool> m_enabled{false};
  std::atomic<bool> m_loaded{false};
  std::atomic<bool> m_session{false};
  std::atomic<proto::Body> m_body{};
  std::filesystem::path m_dir;
  std::uint64_t m_maxBytes{}, m_segmentUs{};
  record::Recorder m_recorder;
  std::mutex m_lifecycle; // starting and stopping the writer
  std::mutex m_drain;     // the solo drain against a session starting
  std::jthread m_writer;
  bool m_started{};
  std::uint64_t m_lastPoseUs{};

  // Cost, per segment (reset when one closes).
  std::atomic<std::uint64_t> m_tickStart{}, m_tickUs{}, m_tickMaxUs{}, m_ticks{};
  std::atomic<std::uint64_t> m_writeUs{}, m_messages{};
};
} // namespace tandem::plugin
