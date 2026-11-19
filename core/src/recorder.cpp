#include "tandem/recorder.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <format>
#include <fstream>
#include <sstream>
#include <type_traits>

namespace tandem::record {
namespace {
constexpr const char *kExtension = ".tsession";
constexpr const char *kIndex = "index.jsonl";

std::int64_t UnixNow() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::tm LocalTime(std::int64_t unixSeconds) {
  const std::time_t t = static_cast<std::time_t>(unixSeconds);
  std::tm out{};
#ifdef _WIN32
  localtime_s(&out, &t);
#else
  localtime_r(&t, &out);
#endif
  return out;
}

std::string Started(std::int64_t unixSeconds) {
  const auto t = LocalTime(unixSeconds);
  return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}", t.tm_year + 1900, t.tm_mon + 1,
                     t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
}

// JSON string contents: quotes, backslashes and control characters escaped.
std::string Escape(const std::string &text) {
  std::string out;
  for (const char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (static_cast<unsigned char>(c) < 0x20) {
      out += std::format("\\u{:04x}", static_cast<int>(c));
    } else {
      out += c;
    }
  }
  return out;
}

bool IsSegment(const std::filesystem::path &path) { return path.extension() == kExtension; }
} // namespace

void Summary::Add(const proto::Message &message) {
  ++messages;
  std::visit(
      [&](const auto &m) {
        using M = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<M, proto::PlayerState>) {
          ++states;
          if (last) {
            const auto dx = m.position.x - last->x, dy = m.position.y - last->y,
                       dz = m.position.z - last->z;
            const auto step = std::sqrt(dx * dx + dy * dy + dz * dz);
            // A teleport (fast travel, a load) is not distance moved.
            if (step < 50.f)
              distance += step;
          }
          if (!first)
            first = m.position;
          last = m.position;
        } else if constexpr (std::is_same_v<M, proto::PoseState>) {
          ++poses;
        } else if constexpr (std::is_same_v<M, proto::Shot>) {
          ++shots;
        } else if constexpr (std::is_same_v<M, proto::NpcHit> ||
                             std::is_same_v<M, proto::PlayerHit> ||
                             std::is_same_v<M, proto::ShotHit> ||
                             std::is_same_v<M, proto::NpcStrike> ||
                             std::is_same_v<M, proto::AreaHit>) {
          ++hits;
        } else if constexpr (std::is_same_v<M, proto::NpcSpawn>) {
          ++npcSpawns;
        } else if constexpr (std::is_same_v<M, proto::VehicleState>) {
          ++vehicleStates;
        } else if constexpr (std::is_same_v<M, proto::Subtitle>) {
          ++subtitles;
        } else if constexpr (std::is_same_v<M, proto::ChoiceHub>) {
          ++choiceHubs;
        } else if constexpr (std::is_same_v<M, proto::PhoneCall>) {
          ++calls;
        } else if constexpr (std::is_same_v<M, proto::Journal>) {
          ++journal;
        } else if constexpr (std::is_same_v<M, proto::Regroup>) {
          ++travels;
        } else if constexpr (std::is_same_v<M, proto::Vitals>) {
          if (m.life != proto::Life::Up)
            ++deaths;
        } else if constexpr (std::is_same_v<M, proto::GameOver>) {
          ++deaths;
        } else if constexpr (std::is_same_v<M, proto::WorldItem> ||
                             std::is_same_v<M, proto::WorldItemTaken> ||
                             std::is_same_v<M, proto::SharedItem>) {
          ++items;
        } else if constexpr (std::is_same_v<M, proto::World>) {
          ++world;
        }
      },
      message);
}

std::vector<std::string> Summary::Tags() const {
  std::vector<std::string> tags;
  if (shots > 0 || hits > 0)
    tags.emplace_back("combat");
  if (vehicleStates >= 30 * 10) // ten seconds at the wheel
    tags.emplace_back("driving");
  if (subtitles > 0 || choiceHubs > 0)
    tags.emplace_back("scene");
  if (calls > 0)
    tags.emplace_back("call");
  if (travels > 0)
    tags.emplace_back("fast travel");
  if (deaths > 0)
    tags.emplace_back("death");
  if (items > 0)
    tags.emplace_back("loot");
  if (journal > 0)
    tags.emplace_back("quest");
  return tags;
}

std::string FormatIndex(const IndexLine &line) {
  const auto &s = line.summary;
  std::string tags;
  for (const auto &tag : s.Tags())
    tags += std::format("{}\"{}\"", tags.empty() ? "" : ",", tag);
  const auto where = [](const std::optional<proto::Vec3> &v) {
    return v ? std::format("[{:.1f},{:.1f},{:.1f}]", v->x, v->y, v->z) : std::string("null");
  };
  return std::format(
      "{{\"file\":\"{}\",\"started\":\"{}\",\"seconds\":{:.1f},\"bytes\":{},\"solo\":{},"
      "\"partial\":{},\"tags\":[{}],\"from\":{},\"to\":{},\"metres\":{:.0f},\"counts\":{{"
      "\"messages\":{},\"states\":{},\"poses\":{},\"shots\":{},\"hits\":{},\"npcs\":{},"
      "\"driving_s\":{},\"subtitles\":{},\"hubs\":{},\"calls\":{},\"journal\":{},"
      "\"travels\":{},\"deaths\":{},\"items\":{}}}{}}}",
      Escape(line.file), Escape(line.started), line.seconds, line.bytes, line.solo, line.partial,
      tags, where(s.first), where(s.last), s.distance, s.messages, s.states, s.poses, s.shots,
      s.hits, s.npcSpawns, s.vehicleStates / 30, s.subtitles, s.choiceHubs, s.calls, s.journal,
      s.travels, s.deaths, s.items, line.cost.empty() ? std::string() : ",\"cost\":" + line.cost);
}

std::vector<std::string> IndexedFiles(const std::string &text) {
  std::vector<std::string> out;
  std::istringstream in(text);
  std::string line;
  const std::string key = "{\"file\":\"";
  while (std::getline(in, line)) {
    if (!line.starts_with(key))
      continue;
    const auto end = line.find('"', key.size());
    if (end != std::string::npos)
      out.push_back(line.substr(key.size(), end - key.size()));
  }
  return out;
}

std::vector<std::string> Prune(const std::filesystem::path &dir, std::uint64_t maxBytes,
                               const std::string &keep) {
  std::vector<std::string> deleted;
  std::error_code ec;
  std::vector<std::pair<std::string, std::uint64_t>> segments;
  for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file(ec) || !IsSegment(entry.path()))
      continue;
    const auto size = entry.file_size(ec);
    segments.emplace_back(entry.path().filename().string(), ec ? 0 : size);
  }
  std::ranges::sort(segments);
  std::uint64_t total = 0;
  for (const auto &[name, size] : segments)
    total += size;
  for (const auto &[name, size] : segments) {
    if (total <= maxBytes)
      break;
    if (name == keep)
      continue;
    if (std::filesystem::remove(dir / name, ec)) {
      total -= std::min(total, size);
      deleted.push_back(name);
    }
  }
  return deleted;
}

std::string SegmentName(std::int64_t unixSeconds) {
  const auto t = LocalTime(unixSeconds);
  return std::format("{:04}{:02}{:02}-{:02}{:02}{:02}", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                     t.tm_hour, t.tm_min, t.tm_sec);
}

bool Recorder::Start(Options options, Log log, Cost cost) {
  std::lock_guard _(m_lock);
  m_options = std::move(options);
  m_log = std::move(log);
  m_cost = std::move(cost);
  m_failed = false;
  try {
    std::error_code ec;
    std::filesystem::create_directories(m_options.dir, ec);
    if (ec || !std::filesystem::is_directory(m_options.dir)) {
      Fail(std::format("cannot make {}: {}", m_options.dir.string(), ec.message()));
      return false;
    }
    IndexLeftovers();
    for (const auto &name : Prune(m_options.dir, m_options.maxBytes))
      if (m_log)
        m_log(std::format("recorder: {} deleted (over {} MB)", name, m_options.maxBytes >> 20));
  } catch (const std::exception &e) {
    Fail(e.what());
    return false;
  }
  m_started = !m_failed;
  return m_started;
}

void Recorder::Add(std::uint64_t hostUs, const proto::Message &message) {
  std::lock_guard _(m_lock);
  if (!m_started || m_failed)
    return;
  try {
    if (!m_writer.IsOpen() && !OpenSegment(hostUs))
      return;
    m_writer.Add(hostUs, message);
    m_summary.Add(message);
    m_lastUs = std::max(m_lastUs, hostUs);
    if (!m_writer.Good())
      Fail("write error in " + m_name);
  } catch (const std::exception &e) {
    Fail(e.what());
  }
}

void Recorder::Tick(std::uint64_t hostUs) {
  std::lock_guard _(m_lock);
  if (!m_started || m_failed || !m_writer.IsOpen())
    return;
  try {
    if (hostUs >= m_startUs + m_options.segmentUs) {
      CloseSegment(false);
      return;
    }
    if (hostUs >= m_lastFlushUs + m_options.flushUs) {
      m_lastFlushUs = hostUs;
      if (!m_writer.Flush())
        Fail("write error in " + m_name);
    }
  } catch (const std::exception &e) {
    Fail(e.what());
  }
}

void Recorder::Cut() {
  std::lock_guard _(m_lock);
  if (m_writer.IsOpen()) {
    try {
      CloseSegment(false);
    } catch (const std::exception &e) {
      Fail(e.what());
    }
  }
}

void Recorder::Stop() {
  std::lock_guard _(m_lock);
  if (m_writer.IsOpen()) {
    try {
      CloseSegment(false);
    } catch (const std::exception &e) {
      Fail(e.what());
    }
  }
  m_started = false;
}

bool Recorder::Active() const {
  std::lock_guard _(m_lock);
  return m_started && !m_failed;
}

bool Recorder::Failed() const {
  std::lock_guard _(m_lock);
  return m_failed;
}

std::string Recorder::Current() const {
  std::lock_guard _(m_lock);
  return m_writer.IsOpen() ? m_name : std::string();
}

std::uint64_t Recorder::Segments() const {
  std::lock_guard _(m_lock);
  return m_segments;
}

bool Recorder::OpenSegment(std::uint64_t hostUs) {
  const auto now = UnixNow();
  auto name = SegmentName(now) + kExtension;
  // Two segments in one second (a cut): the next free name.
  std::error_code ec;
  for (int i = 1; std::filesystem::exists(m_options.dir / name, ec) && i < 100; ++i)
    name = std::format("{}-{}{}", SegmentName(now), i, kExtension);
  std::string error;
  if (!m_writer.Open(m_options.dir / name,
                     {.startHostUs = hostUs, .player = m_options.player, .body = m_options.body},
                     error, play::kSessionMagic)) {
    Fail(error);
    return false;
  }
  m_name = name;
  m_startedText = Started(now);
  m_startUs = hostUs;
  m_lastFlushUs = hostUs;
  m_lastUs = hostUs;
  m_summary = {};
  if (m_log)
    m_log(std::format("recorder: segment {} opened", name));
  return true;
}

void Recorder::CloseSegment(bool partial) {
  const auto bytes = m_writer.Bytes();
  const auto seconds = static_cast<double>(m_lastUs - m_startUs) / 1e6;
  m_writer.Close();
  ++m_segments;
  IndexLine line{.file = m_name,
                 .started = m_startedText,
                 .seconds = seconds,
                 .bytes = bytes,
                 .solo = m_options.solo,
                 .partial = partial,
                 .summary = m_summary};
  if (m_cost)
    line.cost = m_cost();
  WriteIndex(line);
  if (m_log) {
    std::string tags;
    for (const auto &tag : m_summary.Tags())
      tags += " " + tag;
    m_log(std::format("recorder: segment {} closed, {:.0f} s, {} messages, {:.1f} MB,{}{}", m_name,
                      seconds, m_summary.messages, static_cast<double>(bytes) / 1048576.0,
                      tags.empty() ? " nothing notable" : tags,
                      line.cost.empty() ? "" : ", cost " + line.cost));
  }
  m_name.clear();
  for (const auto &name : Prune(m_options.dir, m_options.maxBytes))
    if (m_log)
      m_log(std::format("recorder: {} deleted (over {} MB)", name, m_options.maxBytes >> 20));
}

void Recorder::Fail(const std::string &why) {
  if (m_failed)
    return;
  m_failed = true;
  if (m_writer.IsOpen())
    m_writer.Close();
  if (m_log)
    m_log("recorder: off for this run: " + why);
}

void Recorder::WriteIndex(const IndexLine &line) {
  std::ofstream out(m_options.dir / kIndex, std::ios::app);
  out << FormatIndex(line) << '\n';
  if (!out)
    Fail("cannot write the index");
}

// Segments a crash left without an index line: read back and indexed.
void Recorder::IndexLeftovers() {
  std::string text;
  {
    std::ifstream in(m_options.dir / kIndex);
    std::stringstream all;
    all << in.rdbuf();
    text = all.str();
  }
  const auto indexed = IndexedFiles(text);
  std::vector<std::filesystem::path> leftovers;
  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(m_options.dir, ec)) {
    const auto name = entry.path().filename().string();
    if (IsSegment(entry.path()) && std::ranges::find(indexed, name) == indexed.end())
      leftovers.push_back(entry.path());
  }
  std::ranges::sort(leftovers);
  for (const auto &path : leftovers) {
    std::string error;
    const auto recording = play::Load(path, error);
    IndexLine line{.file = path.filename().string(),
                   .bytes = static_cast<std::uint64_t>(std::filesystem::file_size(path, ec)),
                   .solo = m_options.solo,
                   .partial = true};
    if (recording) {
      for (const auto &entry : recording->entries)
        line.summary.Add(entry.message);
      line.seconds = static_cast<double>(recording->DurationUs()) / 1e6;
    }
    WriteIndex(line);
    if (m_log)
      m_log(std::format("recorder: {} indexed after the fact ({} messages{})", line.file,
                        line.summary.messages, recording ? "" : ", unreadable: " + error));
  }
}
} // namespace tandem::record
