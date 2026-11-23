// The observer: see observer.hpp and docs/architecture/observer.md.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "observer.hpp"

#include "degradations.hpp"
#include "log.hpp"
#include "recording.hpp"
#include "runtime.hpp"

#include "tandem/session.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <fstream>
#include <utility>

namespace tandem::plugin {
namespace {
constexpr std::uint64_t kBeforeUs = 30'000'000; // a capture's log before its trigger
constexpr std::uint64_t kAfterUs = 5'000'000;   // and after it
constexpr std::uint64_t kStallUs = 30'000'000;  // the game thread silent this long
constexpr std::uint64_t kSummaryUs = 30ull * 60'000'000;
constexpr std::uint64_t kRowGapUs = 10'000'000; // trigger rows of a kind not captured

std::uint64_t SteadyNs() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
}

const char *LevelName(char level) {
  switch (level) {
  case 'E':
    return "error";
  case 'W':
    return "warning";
  case 'D':
    return "debug";
  default:
    return "info";
  }
}

std::string Where(const proto::Vec3 &at) {
  return std::format("({:.1f}, {:.1f}, {:.1f})", at.x, at.y, at.z);
}

void Max(std::atomic<std::uint64_t> &slot, std::uint64_t value) {
  auto seen = slot.load(std::memory_order_relaxed);
  while (value > seen && !slot.compare_exchange_weak(seen, value, std::memory_order_relaxed)) {
  }
}
} // namespace

namespace log {
void Observe(char level, const std::string &message) { Observer::Get().Line(level, message); }
} // namespace log

Observer &Observer::Get() {
  static Observer instance;
  return instance;
}

std::string Observer::Clock(std::uint64_t us) const {
  const auto ms = m_startSystemMs + static_cast<std::int64_t>(us / 1000) -
                  static_cast<std::int64_t>(m_startUs / 1000);
  return std::format(
      "{:%F %T}", std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(ms)));
}

void Observer::Configure(Settings settings) {
  if (!settings.enabled || On())
    return;
  m_settings = std::move(settings);
  m_startUs = net::NowUs();
  const auto now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
  // Local time, as RED4ext's tandem-*.log writes it (the CRT's own zone:
  // std::chrono's time zone database is not there under Wine).
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  std::tm local{}, utc{};
  localtime_s(&local, &seconds);
  gmtime_s(&utc, &seconds);
  utc.tm_isdst = local.tm_isdst;
  const auto offsetS = static_cast<std::int64_t>(std::mktime(&local) - std::mktime(&utc));
  m_startSystemMs = now.time_since_epoch().count() + offsetS * 1000;
  m_stamp = std::format("{:%Y%m%d-%H%M%S}",
                        std::chrono::sys_seconds(std::chrono::seconds(m_startSystemMs / 1000)));
  std::error_code error;
  std::filesystem::create_directories(m_settings.dir / "captures", error);
  if (error) {
    log::Warn("observer: off, {} cannot be made: {}", m_settings.dir.string(), error.message());
    return;
  }
  Prune();
  m_limiter = observer::Limiter(60'000'000, m_settings.maxCaptures);
  m_nextSummaryUs = m_startUs + kSummaryUs;
  m_writer = std::jthread([this](std::stop_token stop) { Run(stop); });
  log::g_observe.store(true);
  log::Info("observer: on, session-{}.tsv in {} (drift {:.1f} m, frames over {} ms, {} captures "
            "at most)",
            m_stamp, m_settings.dir.string(), m_settings.driftMetres, m_settings.frameMs,
            m_settings.maxCaptures);
}

void Observer::Shutdown() {
  if (!On())
    return;
  {
    std::lock_guard _(m_lock);
    RowLocked("observer", "the game closes", net::NowUs());
    m_summaryWanted = "the game closes";
  }
  m_writer.request_stop();
  if (m_writer.joinable())
    m_writer.join();
  log::g_observe.store(false);
}

// The oldest files of the folder go while it is over its size.
void Observer::Prune() {
  struct File {
    std::filesystem::path path;
    std::uintmax_t size{};
    std::filesystem::file_time_type time;
  };
  std::vector<File> files;
  std::uintmax_t total = 0;
  std::error_code error;
  for (auto it = std::filesystem::recursive_directory_iterator(m_settings.dir, error);
       !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
    if (!it->is_regular_file(error))
      continue;
    File file{.path = it->path(), .size = it->file_size(error), .time = it->last_write_time(error)};
    total += file.size;
    files.push_back(std::move(file));
  }
  std::ranges::sort(files, {}, &File::time);
  std::size_t removed = 0;
  for (const auto &file : files) {
    if (total <= m_settings.maxBytes)
      break;
    if (std::filesystem::remove(file.path, error)) {
      total -= file.size;
      ++removed;
    }
  }
  if (removed)
    log::Info("observer: {} old file(s) removed (the folder is kept under {} MB)", removed,
              m_settings.maxBytes >> 20);
}

// ---- entry points ----

void Observer::Line(char level, const std::string &text) {
  if (!On())
    return;
  const auto start = SteadyNs();
  const auto us = net::NowUs();
  auto hit = observer::Classify(level, text);
  {
    std::lock_guard _(m_lock);
    m_ring.Push(us, level, text);
    if (text.starts_with("watchdog: "))
      RowLocked("watchdog", text.substr(10), us);
    if (hit)
      HitLocked(std::move(*hit), us, kAfterUs);
  }
  const auto ns = SteadyNs() - start;
  m_lines.fetch_add(1, std::memory_order_relaxed);
  m_lineNs.fetch_add(ns, std::memory_order_relaxed);
  Max(m_lineMaxNs, ns);
}

void Observer::HitLocked(observer::Hit hit, std::uint64_t us, std::uint64_t delayUs) {
  observer::Moment moment{
      .us = us, .kind = hit.kind, .severity = hit.severity, .detail = hit.detail.substr(0, 300)};
  const auto verdict = m_limiter.Ask(hit.kind, us);
  const auto kind = static_cast<std::size_t>(hit.kind);
  if (verdict == observer::Limiter::Verdict::Capture) {
    const auto number = ++m_captures;
    moment.capture = std::format("{}-{:02}-{}.txt", m_stamp, number, observer::NameOf(hit.kind));
    m_pending.push_back({.kind = hit.kind,
                         .us = us,
                         .dueUs = us + delayUs,
                         .severity = hit.severity,
                         .number = number,
                         .detail = hit.detail,
                         .context = ContextLocked(us),
                         .file = moment.capture});
  } else if (verdict == observer::Limiter::Verdict::Capped) {
    ++m_refused;
  }
  // The trail: every capture, else a row a kind every 10 s (a fallback
  // taken every frame stays one row, with how many it stands for).
  if (!moment.capture.empty() || us >= m_lastRowUs[kind] + kRowGapUs) {
    const auto more = std::exchange(m_unrowed[kind], 0);
    RowLocked("trigger",
              std::format("{} ({:.0f}){}{}: {}", observer::NameOf(hit.kind), hit.severity,
                          moment.capture.empty() ? "" : " captures/" + moment.capture,
                          more ? std::format(" (+{} since the last row)", more) : "",
                          moment.detail),
              us);
    m_lastRowUs[kind] = us;
  } else {
    ++m_unrowed[kind];
  }
  m_tally.Add(std::move(moment));
}

void Observer::RowLocked(std::string kind, std::string text, std::uint64_t us) {
  if (m_rows.size() < 10'000)
    m_rows.push_back({.us = us, .kind = std::move(kind), .text = std::move(text)});
}

std::string Observer::ContextLocked(std::uint64_t us) const {
  std::string out = std::format("session: {}\n", m_session.empty() ? "none" : m_session);
  if (m_local)
    out += std::format("our V: {} ({:.1f} s before)\n", Where(m_local->position),
                       static_cast<double>(us - std::min(us, m_local->us)) / 1e6);
  else
    out += "our V: not sampled\n";
  for (const auto &[player, seen] : m_remote)
    out += std::format("player {}: {} as it said {:.1f} s before\n", player, Where(seen.position),
                       static_cast<double>(us - std::min(us, seen.us)) / 1e6);
  out += std::format("story now: {}\n", m_trail.Now());
  return out;
}

void Observer::Story(const proto::Message &message, bool heard) {
  if (!On())
    return;
  const auto start = SteadyNs();
  const auto us = net::NowUs();
  {
    std::lock_guard _(m_lock);
    for (auto &row : m_trail.Note(message, us, heard ? proto::PlayerId{1} : proto::kNoPlayer))
      RowLocked(std::move(row.kind), std::move(row.text), row.us);
  }
  if (!heard)
    m_gameNs.fetch_add(SteadyNs() - start, std::memory_order_relaxed);
}

void Observer::Phase(std::uint64_t resource) {
  if (!On() || resource == 0)
    return;
  const auto us = net::NowUs();
  std::lock_guard _(m_lock);
  if (auto row = m_trail.Phase(resource, us))
    RowLocked(std::move(row->kind), std::move(row->text), row->us);
}

void Observer::District(std::string_view name) {
  if (!On())
    return;
  const auto us = net::NowUs();
  std::lock_guard _(m_lock);
  if (auto row = m_trail.District(name, us))
    RowLocked(std::move(row->kind), std::move(row->text), row->us);
}

void Observer::Note(std::string_view kind, std::string text) {
  if (!On())
    return;
  const auto us = net::NowUs();
  std::lock_guard _(m_lock);
  RowLocked(std::string(kind), std::move(text), us);
}

void Observer::Frame() {
  if (!On())
    return;
  const auto start = SteadyNs();
  const auto now = net::NowUs();
  const auto previous = std::exchange(m_lastFrameUs, now);
  m_frames.fetch_add(1, std::memory_order_relaxed);
  auto gap = previous ? now - previous : 0;
  // A gap that held several of the engine's frames is a pause menu or a
  // loading screen holding the script tick (a 682 ms pause read as a frame
  // of 682 ms, 2026-11-23): its longest engine frame is the frame time.
  const auto engine = Runtime::Get().TakeEngineFrames();
  if (engine.count > 2)
    gap = engine.longestUs;
  // Over 2 s is a loading screen or a pause: the stall watch's, not a frame.
  if (gap > std::uint64_t{m_settings.frameMs} * 1000 && gap < 2'000'000) {
    const auto ms = static_cast<float>(gap) / 1000.f;
    std::lock_guard _(m_lock);
    HitLocked({.kind = observer::Trigger::Frame,
               .detail = std::format("a frame of {:.0f} ms (over {} ms)", ms, m_settings.frameMs),
               .severity = observer::FrameSeverity(ms)},
              now, kAfterUs);
  }
  m_gameNs.fetch_add(SteadyNs() - start, std::memory_order_relaxed);
}

void Observer::Drift(proto::PlayerId player, float metres, const proto::Vec3 &body,
                     const proto::Vec3 &target) {
  if (!On())
    return;
  const auto us = net::NowUs();
  std::lock_guard _(m_lock);
  HitLocked({.kind = observer::Trigger::Drift,
             .detail = std::format("player {}'s puppet {:.1f} m from where its owner says, for "
                                   "1 s: body {}, owner {}",
                                   player, metres, Where(body), Where(target)),
             .severity = observer::DriftSeverity(metres)},
            us, kAfterUs);
}

void Observer::Local(const proto::Vec3 &position) {
  if (!On())
    return;
  const auto us = net::NowUs();
  std::lock_guard _(m_lock);
  m_local = Seen{.position = position, .us = us};
}

void Observer::Remote(proto::PlayerId player, const proto::Vec3 &position) {
  if (!On())
    return;
  const auto us = net::NowUs();
  std::lock_guard _(m_lock);
  if (m_remote.size() < 64 || m_remote.contains(player))
    m_remote[player] = Seen{.position = position, .us = us};
}

void Observer::Forget(proto::PlayerId player) {
  if (!On())
    return;
  std::lock_guard _(m_lock);
  m_remote.erase(player);
}

void Observer::SessionStarted(std::string what) {
  if (!On())
    return;
  const auto us = net::NowUs();
  m_lastFrameUs = 0;
  std::lock_guard _(m_lock);
  m_trail.Reset();
  m_remote.clear();
  RowLocked("session", "start as " + what, us);
  m_session = std::move(what);
}

void Observer::SessionEnded() {
  if (!On())
    return;
  const auto us = net::NowUs();
  std::lock_guard _(m_lock);
  RowLocked("session", "end of " + m_session, us);
  m_session.clear();
  m_remote.clear();
  m_trail.Reset();
  m_summaryWanted = "a session ended";
}

void Observer::Capture(std::string reason) {
  if (!On())
    return;
  const auto us = net::NowUs();
  std::lock_guard _(m_lock);
  HitLocked(
      {.kind = observer::Trigger::Manual,
       .detail = "asked for: " + (reason.empty() ? std::string("cmd observer capture") : reason),
       .severity = 50},
      us, kAfterUs);
}

void Observer::SummaryNow(std::string_view why) {
  if (!On())
    return;
  std::lock_guard _(m_lock);
  m_summaryWanted = std::string(why);
}

std::string Observer::Status() {
  if (!On())
    return "observer: off (tandem.ini observer = on)";
  std::lock_guard _(m_lock);
  const auto lines = m_lines.load();
  const auto frames = m_frames.load();
  return std::format(
      "observer: on, {} trail row(s) written, captures {} written of {} taken ({} at most, {} "
      "refused by the cap); ring {} lines, {:.1f} MB (about {:.1f} MB in memory, {} squeezed "
      "out early); cost: {} log lines, {:.2f} us each on average, {:.1f} us at most; the game "
      "thread {:.2f} us a frame over {} frames; files session-{}.tsv in {}",
      m_rowsWritten, m_written, m_captures, m_settings.maxCaptures, m_refused, m_ring.Size(),
      static_cast<double>(m_ring.Bytes()) / 1048576.0,
      static_cast<double>(m_ring.Bytes() + m_ring.Size() * 72) / 1048576.0, m_ring.Squeezed(),
      lines,
      lines ? static_cast<double>(m_lineNs.load()) / 1000.0 / static_cast<double>(lines) : 0.0,
      static_cast<double>(m_lineMaxNs.load()) / 1000.0,
      frames ? static_cast<double>(m_gameNs.load()) / 1000.0 / static_cast<double>(frames) : 0.0,
      frames, m_stamp, m_settings.dir.string());
}

// ---- the writer ----

void Observer::Run(std::stop_token stop) {
  while (true) {
    const bool stopping = stop.stop_requested();
    const auto now = net::NowUs();
    if (!stopping)
      WatchStall(now);
    std::vector<observer::Row> rows;
    std::vector<Pending> due;
    std::string summary;
    {
      std::lock_guard _(m_lock);
      rows.swap(m_rows);
      for (auto it = m_pending.begin(); it != m_pending.end();)
        if (stopping || it->dueUs <= now) {
          due.push_back(std::move(*it));
          it = m_pending.erase(it);
        } else {
          ++it;
        }
      if (!stopping && now >= m_nextSummaryUs) {
        m_nextSummaryUs = now + kSummaryUs;
        if (m_summaryWanted.empty())
          m_summaryWanted = "every 30 minutes";
      }
      summary = std::exchange(m_summaryWanted, std::string());
    }
    WriteRows(std::move(rows));
    for (const auto &pending : due)
      WriteCapture(pending);
    if (!summary.empty())
      WriteSummary(summary);
    if (stopping) {
      // What the last steps queued (their rows).
      std::vector<observer::Row> last;
      {
        std::lock_guard _(m_lock);
        last.swap(m_rows);
      }
      WriteRows(std::move(last));
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void Observer::WatchStall(std::uint64_t now) {
  bool inSession = false;
  {
    std::lock_guard _(m_lock);
    inSession = !m_session.empty();
  }
  const auto last = Runtime::Get().LastGameFrameUs();
  if (!inSession || last == 0 || now < last) {
    m_stalled = false;
    return;
  }
  const auto silent = now - last;
  // A pause menu or a loading screen holds the script tick while the
  // engine's frames run on (Runtime::EngineFramesRun): no stall, whatever
  // its length (the watchdog's "paused" rows say it). A stall is the
  // engine's frames silent too.
  if (!m_stalled && silent > kStallUs && !Runtime::Get().EngineFramesRun(now)) {
    m_stalled = true;
    m_stalledSinceUs = last;
    std::lock_guard _(m_lock);
    HitLocked({.kind = observer::Trigger::Stall,
               .detail = std::format("the game thread and the engine's frames silent for {:.0f} "
                                     "s: under Proton a crash (tools/game.sh bt), else a hang",
                                     static_cast<double>(silent) / 1e6),
               .severity = 95},
              now, 0);
  } else if (m_stalled && silent < 1'000'000) {
    m_stalled = false;
    std::lock_guard _(m_lock);
    RowLocked("watchdog",
              std::format("the game thread is back after {:.1f} s: the stall was a hang that "
                          "ended",
                          static_cast<double>(last - m_stalledSinceUs) / 1e6),
              now);
  }
}

void Observer::WriteRows(std::vector<observer::Row> rows) {
  if (rows.empty())
    return;
  const auto path = m_settings.dir / std::format("session-{}.tsv", m_stamp);
  const bool fresh = !std::filesystem::exists(path);
  std::ofstream file(path, std::ios::app);
  if (!file)
    return;
  if (fresh)
    file << "time\tt\tkind\ttext\n";
  for (const auto &row : rows)
    file << Clock(row.us) << '\t'
         << std::format("{:.3f}", static_cast<double>(row.us - std::min(row.us, m_startUs)) / 1e6)
         << '\t' << observer::Field(row.kind) << '\t' << observer::Field(row.text) << '\n';
  file.flush();
  std::lock_guard _(m_lock);
  m_rowsWritten += rows.size();
}

void Observer::WriteCapture(const Pending &pending) {
  std::vector<observer::Line> lines;
  {
    std::lock_guard _(m_lock);
    lines = m_ring.Between(pending.us - std::min(pending.us, kBeforeUs), pending.dueUs);
  }
  // What the rest of the plugin says now (its own locks; we hold none).
  std::string later = std::format("you: player {}\n", Runtime::Get().You());
  for (const auto &player : Runtime::Get().RosterPlayers()) {
    const auto view = Runtime::Get().ViewOf(player.id);
    later += std::format("roster: player {} \"{}\"{}\n", player.id, player.name,
                         view.valid ? " at " + Where(view.position) : "");
  }
  later += Degradations::Get().Local().Summary() + "\n";
  later += std::format("recorder: {}\n", Recording::Get().Status());

  const auto path = m_settings.dir / "captures" / pending.file;
  std::ofstream file(path, std::ios::trunc);
  if (!file) {
    log::Warn("observer: capture {} not written: {} cannot be opened", pending.number,
              path.string());
    return;
  }
  file << std::format("tandem observer capture {}: {} (severity {:.0f})\n", pending.number,
                      observer::NameOf(pending.kind), pending.severity)
       << std::format("at: {} (t = {:.3f} s)\n", Clock(pending.us),
                      static_cast<double>(pending.us - std::min(pending.us, m_startUs)) / 1e6)
       << "what: " << pending.detail << '\n'
       << "screenshot: none (the plugin takes none; tools/game.sh shot by hand)\n"
       << "-- at the trigger\n"
       << pending.context << "-- when written\n"
       << later
       << std::format("-- the log, {} s before to {} s after ({} lines; >>> the trigger)\n",
                      kBeforeUs / 1'000'000, (pending.dueUs - pending.us) / 1'000'000,
                      lines.size());
  bool marked = false;
  for (const auto &line : lines) {
    const bool mark = !marked && line.us >= pending.us;
    marked = marked || mark;
    file << (mark ? ">>> " : "") << '[' << Clock(line.us) << "] [" << LevelName(line.level) << "] "
         << line.text << '\n';
  }
  file.close();
  {
    std::lock_guard _(m_lock);
    ++m_written;
  }
  log::Info("observer: capture {} ({}) written: captures/{} ({} lines)", pending.number,
            observer::NameOf(pending.kind), pending.file, lines.size());
}

void Observer::WriteSummary(std::string_view why) {
  const auto now = net::NowUs();
  std::string text;
  {
    std::lock_guard _(m_lock);
    text = m_tally.Summary(std::format("{} at {} ({})", m_stamp, Clock(now), why), m_trail,
                           m_written, [this](std::uint64_t us) { return Clock(us); });
    RowLocked("summary", std::format("{}: summary-{}.txt", why, m_stamp), now);
  }
  std::ofstream file(m_settings.dir / std::format("summary-{}.txt", m_stamp), std::ios::app);
  if (file)
    file << text << '\n';
  log::Info("observer: summary written ({})", why);
}
} // namespace tandem::plugin
