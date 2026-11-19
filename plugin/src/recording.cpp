// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "recording.hpp"

#include "activity.hpp"
#include "combat.hpp"
#include "log.hpp"
#include "runtime.hpp"
#include "story.hpp"

#include "tandem/session.hpp"

#include <chrono>
#include <format>

namespace tandem::plugin {
using namespace std::chrono_literals;

Recording &Recording::Get() {
  static Recording recording;
  return recording;
}

void Recording::Configure(bool enabled, std::filesystem::path dir, std::uint64_t maxBytes,
                          std::uint64_t segmentUs) {
  m_dir = std::move(dir);
  m_maxBytes = maxBytes;
  m_segmentUs = segmentUs;
  m_enabled = enabled;
}

void Recording::GameLoaded(proto::Body body) {
  m_body = body;
  m_loaded = true;
  if (!m_enabled)
    return;
  std::lock_guard _(m_lifecycle);
  if (m_started)
    return;
  m_started = true;
  try {
    const bool ok = m_recorder.Start(
        {.dir = m_dir,
         .segmentUs = m_segmentUs,
         .maxBytes = m_maxBytes,
         .solo = !m_session,
         .kind = play::Kind::Session,
         .player = proto::kNoPlayer,
         .host = 1,
         .body = body},
        [](const std::string &line) { log::Info("{}", line); }, [this] { return Cost(); });
    if (!ok)
      return;
    m_writer = std::jthread([this](std::stop_token stop) { Run(stop); });
    log::Info("recorder: on, in {} (segments of {} min, at most {} MB)", m_dir.string(),
              m_segmentUs / 60'000'000, m_maxBytes >> 20);
  } catch (const std::exception &e) {
    log::Warn("recorder: off for this run: {}", e.what());
  }
}

void Recording::GameLeft() {
  m_loaded = false;
  // The world it recorded is gone: the next game starts a segment of its own.
  m_recorder.Cut();
}

void Recording::SetEnabled(bool enabled) {
  if (m_enabled.exchange(enabled) == enabled)
    return;
  log::Info("recorder: switched {} in the co-op menu", enabled ? "on" : "off");
  if (!enabled) {
    m_recorder.Cut();
    return;
  }
  if (m_loaded)
    GameLoaded(m_body.load());
}

bool Recording::Active() const { return m_enabled && m_loaded && m_recorder.Active(); }

std::string Recording::Status() const {
  if (!m_enabled)
    return "off";
  if (m_recorder.Failed())
    return "failed (see tandem's log)";
  if (!m_loaded || !m_recorder.Active())
    return "waiting for a game";
  const auto current = m_recorder.Current();
  return current.empty() ? std::string("recording") : "recording " + current;
}

void Recording::Cut() { m_recorder.Cut(); }

void Recording::Shutdown() {
  {
    std::lock_guard _(m_lifecycle);
    if (m_writer.joinable()) {
      m_writer.request_stop();
      m_writer.join();
    }
  }
  m_recorder.Stop();
}

void Recording::Add(const proto::Message &message) {
  if (!m_enabled || !m_loaded)
    return;
  const auto start = net::NowUs();
  m_recorder.Add(start, message);
  m_writeUs += net::NowUs() - start;
  ++m_messages;
}

void Recording::TickBegin() { m_tickStart = net::NowUs(); }

void Recording::TickEnd() {
  const auto start = m_tickStart.exchange(0);
  if (start == 0)
    return;
  const auto us = net::NowUs() - start;
  m_tickUs += us;
  ++m_ticks;
  auto max = m_tickMaxUs.load();
  while (us > max && !m_tickMaxUs.compare_exchange_weak(max, us)) {
  }
}

void Recording::SessionStarting(bool guest) {
  std::lock_guard _(m_drain);
  m_session = true;
  // A segment keeps one kind of play.
  m_recorder.Switch(false, guest ? play::Kind::Play : play::Kind::Session, proto::kNoPlayer,
                    guest ? proto::kNoPlayer : 1);
}

void Recording::SessionStopped() {
  std::lock_guard _(m_drain);
  m_session = false;
  m_recorder.Switch(true, play::Kind::Session, proto::kNoPlayer, 1);
}

void Recording::Joined(bool asHost, proto::PlayerId you, proto::PlayerId host) {
  if (asHost)
    return; // as set when it started
  m_recorder.Switch(false, play::Kind::Play, you, host);
}

std::string Recording::Cost() {
  const auto ticks = m_ticks.exchange(0);
  const auto tickUs = m_tickUs.exchange(0);
  const auto out =
      std::format("{{\"script_ticks\":{},\"script_us_avg\":{:.1f},\"script_us_max\":{},"
                  "\"write_ms\":{:.1f},\"messages\":{}}}",
                  ticks, ticks ? static_cast<double>(tickUs) / static_cast<double>(ticks) : 0.0,
                  m_tickMaxUs.exchange(0), static_cast<double>(m_writeUs.exchange(0)) / 1000.0,
                  m_messages.exchange(0));
  return out;
}

// The writer: solo, what the scripts submitted; always, the segments' time.
void Recording::Run(std::stop_token stop) {
  try {
    while (!stop.stop_requested()) {
      std::this_thread::sleep_for(10ms);
      {
        std::lock_guard _(m_drain);
        if (!m_session && m_enabled && m_loaded)
          DrainSolo();
      }
      const auto start = net::NowUs();
      m_recorder.Tick(start);
      m_writeUs += net::NowUs() - start;
    }
  } catch (const std::exception &e) {
    log::Warn("recorder: the writer stopped: {}", e.what());
  }
}

// Solo: the outboxes the net thread drains in a session, written instead of
// sent. Everything is taken (nothing piles up), whatever the scripts filled.
void Recording::DrainSolo() {
  auto &runtime = Runtime::Get();
  const auto now = net::NowUs();
  const auto add = [this](proto::Message message) { Add(message); };
  if (auto local = runtime.TakeLocal()) {
    local->player = 1;
    local->timeUs = now;
    add(*local);
  }
  if (auto pose = runtime.TakeLocalPose()) {
    // 30 a second at most, as a session sends them.
    if (now - m_lastPoseUs >= 33'000) {
      m_lastPoseUs = now;
      pose->player = 1;
      pose->timeUs = now;
      add(*pose);
    }
  }
  if (auto appearance = runtime.TakeLocalAppearance())
    add(std::move(*appearance));
  if (auto held = runtime.TakeLocalHeld())
    add(*held);
  for (auto &shot : runtime.TakeLocalShots())
    add(shot);
  if (auto seat = runtime.TakeLocalSeat())
    add(*seat);
  if (auto vehicle = runtime.TakeLocalVehicle())
    add(*vehicle);
  if (auto transit = runtime.TakeLocalTransit())
    add(*transit);
  for (auto &radio : runtime.TakeLocalRadios())
    add(radio);
  if (auto paused = runtime.TakeLocalPaused())
    add(proto::Pause{.player = 1, .paused = *paused});
  if (auto time = runtime.TakeLocalTime())
    add(*time);
  if (auto away = runtime.TakeLocalElsewhere())
    add(*away);
  if (auto busy = Activities::Get().TakeLocal())
    add(*busy);
  Activities::Get().TakeOutbound(); // time skips are a session's
  if (auto world = runtime.TakeWorld())
    add(*world);
  for (auto &message : runtime.TakeNpcOutbox())
    add(std::move(message));
  for (auto &message : runtime.TakeHackOutbox())
    add(std::move(message));
  for (auto &[player, reply] : runtime.TakeNpcReplies())
    add(reply);
  if (auto npcs = runtime.TakeNpcStates())
    add(std::move(*npcs));
  if (auto awareness = runtime.TakeNpcAwareness())
    add(std::move(*awareness));
  if (auto stealth = runtime.TakeLocalStealth())
    add(*stealth);
  for (auto &noise : runtime.TakeLocalNoises())
    add(noise);
  for (auto &[message, to] : Story::Get().TakeOutbound())
    add(std::move(message));
  for (auto &message : runtime.TakeRegroupOutbox())
    add(std::move(message));
  for (auto &message : runtime.TakeLootOutbox())
    add(std::move(message));
  if (auto vitals = runtime.TakeLocalVitals())
    add(*vitals);
  for (auto &revive : runtime.TakeLocalRevives())
    add(revive);
  for (auto &message : Combat::Get().TakeOutbound())
    add(std::move(message));
}
} // namespace tandem::plugin
