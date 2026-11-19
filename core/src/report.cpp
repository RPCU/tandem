#include "tandem/report.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <fstream>
#include <system_error>

namespace tandem::report {
namespace {
constexpr std::size_t kLogBytes = 2 * 1024 * 1024;
constexpr std::size_t kTailLines = 200;

char Lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool IsSeparator(char c) { return c == '/' || c == '\\'; }

// `needle` (ASCII case ignored, either slash) becomes `with`.
void ReplacePath(std::string &text, std::string_view needle, std::string_view with) {
  if (needle.size() < 4) // "C:\" alone would eat every path
    return;
  const auto same = [](char a, char b) {
    return (IsSeparator(a) && IsSeparator(b)) || Lower(a) == Lower(b);
  };
  for (std::size_t at = 0; at + needle.size() <= text.size();) {
    if (std::equal(needle.begin(), needle.end(), text.begin() + static_cast<std::ptrdiff_t>(at),
                   same)) {
      text.replace(at, needle.size(), with);
      at += with.size();
    } else
      ++at;
  }
}

// The folder after /home/ or \Users\ is someone's name.
void HideUsers(std::string &text) {
  for (const std::string_view marker : {"users", "home"}) {
    for (std::size_t at = 0; at + marker.size() + 2 < text.size(); ++at) {
      if (!IsSeparator(text[at]) || !IsSeparator(text[at + marker.size() + 1]))
        continue;
      if (!std::equal(marker.begin(), marker.end(),
                      text.begin() + static_cast<std::ptrdiff_t>(at + 1),
                      [](char a, char b) { return a == Lower(b); }))
        continue;
      // A Windows name may hold spaces (too much hidden beats too little);
      // a Linux one does not.
      const bool spaces = marker == "users";
      const auto start = at + marker.size() + 2;
      auto end = start;
      while (end < text.size() && !IsSeparator(text[end]) && text[end] != '\n' &&
             text[end] != '\r' && text[end] != '"' && text[end] != '\'' &&
             (spaces || text[end] != ' '))
        ++end;
      if (end == start)
        continue;
      text.replace(start, end - start, "<user>");
    }
  }
}

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

// 203.0.113.7 -> 203.0.x.x (loopback and 0.0.0.0 say nothing: kept).
void MaskAddresses(std::string &text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t at = 0; at < text.size();) {
    const bool starts =
        IsDigit(text[at]) && (at == 0 || (!IsDigit(text[at - 1]) && text[at - 1] != '.'));
    if (!starts) {
      out.push_back(text[at++]);
      continue;
    }
    std::array<std::size_t, 5> dots{}; // the start of each part, and the end
    std::array<int, 4> parts{};
    std::size_t cursor = at;
    int count = 0;
    for (; count < 4; ++count) {
      dots[static_cast<std::size_t>(count)] = cursor;
      int value = 0, digits = 0;
      while (cursor < text.size() && IsDigit(text[cursor]) && digits < 4) {
        value = value * 10 + (text[cursor] - '0');
        ++cursor;
        ++digits;
      }
      if (digits == 0 || digits > 3 || value > 255)
        break;
      parts[static_cast<std::size_t>(count)] = value;
      if (count < 3) {
        if (cursor >= text.size() || text[cursor] != '.')
          break;
        ++cursor;
      }
    }
    const bool whole =
        count == 4 &&
        (cursor == text.size() ||
         (!IsDigit(text[cursor]) &&
          !(text[cursor] == '.' && cursor + 1 < text.size() && IsDigit(text[cursor + 1]))));
    const bool plain = parts[0] == 127 || (parts[0] == 0 && parts[1] == 0);
    if (!whole || plain) {
      // Not an address: copy the digits we looked at and go on after them.
      const auto next = std::max(cursor, at + 1);
      out.append(text, at, next - at);
      at = next;
      continue;
    }
    out.append(text, at, dots[2] - at);
    out += "x.x";
    at = cursor;
  }
  text = std::move(out);
}

std::string Bytes(const std::filesystem::path &path) {
  const auto text = path.u8string();
  return {reinterpret_cast<const char *>(text.data()), text.size()};
}

// The end of a file: at most `limit` bytes, from the start of a line.
std::string ReadTail(const std::filesystem::path &path, std::size_t limit) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    return {};
  const auto size = static_cast<std::size_t>(std::max<std::streamoff>(file.tellg(), 0));
  const auto take = std::min(size, limit);
  file.seekg(static_cast<std::streamoff>(size - take));
  std::string text(take, '\0');
  file.read(text.data(), static_cast<std::streamsize>(take));
  text.resize(static_cast<std::size_t>(std::max<std::streamsize>(file.gcount(), 0)));
  if (take < size)
    if (const auto line = text.find('\n'); line != std::string::npos)
      text.erase(0, line + 1);
  return text;
}

// The logs named PREFIX*.log in a folder, newest first.
std::vector<std::filesystem::path> Newest(const std::filesystem::path &folder,
                                          std::string_view prefix) {
  std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> found;
  std::error_code ec;
  for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end;
       it.increment(ec)) {
    const auto name = Bytes(it->path().filename());
    std::error_code ignored;
    if (name.starts_with(prefix) && name.ends_with(".log") && it->is_regular_file(ignored))
      found.emplace_back(it->last_write_time(ignored), it->path());
  }
  std::ranges::sort(found, std::greater{});
  std::vector<std::filesystem::path> out;
  for (auto &entry : found)
    out.push_back(std::move(entry.second));
  return out;
}

std::uint32_t Crc32(std::string_view data) {
  static const auto table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k)
        c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  std::uint32_t crc = 0xFFFFFFFFu;
  for (const char c : data)
    crc = table[(crc ^ static_cast<unsigned char>(c)) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

void Put16(std::vector<std::byte> &out, std::uint32_t value) {
  out.push_back(static_cast<std::byte>(value & 0xFF));
  out.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
}

void Put32(std::vector<std::byte> &out, std::uint32_t value) {
  Put16(out, value & 0xFFFF);
  Put16(out, value >> 16);
}

void PutText(std::vector<std::byte> &out, std::string_view text) {
  for (const char c : text)
    out.push_back(static_cast<std::byte>(c));
}
} // namespace

std::string Redact(std::string text, const std::filesystem::path &game) {
  if (!game.empty())
    ReplacePath(text, Bytes(game), "<game>");
  HideUsers(text);
  MaskAddresses(text);
  return text;
}

std::string RedactIni(std::string_view ini, const std::filesystem::path &game) {
  std::string out;
  while (!ini.empty()) {
    const auto end = ini.find('\n');
    auto line = ini.substr(0, end);
    ini.remove_prefix(end == std::string_view::npos ? ini.size() : end + 1);
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);
    // Also when commented out: a token someone parked behind a ';'.
    auto key = line.substr(0, line.find('='));
    while (!key.empty() &&
           (key.front() == ' ' || key.front() == '\t' || key.front() == ';' || key.front() == '#'))
      key.remove_prefix(1);
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
      key.remove_suffix(1);
    if (line.find('=') != std::string_view::npos && (key == "relay_token" || key == "join"))
      out += std::format("{}= <removed>\n", line.substr(0, line.find('=')));
    else
      out += std::format("{}\n", line);
  }
  return Redact(std::move(out), game);
}

std::string TailLines(std::string_view text, std::size_t lines) {
  auto at = text.size();
  // A last line with no newline counts as one.
  if (at > 0 && text[at - 1] == '\n')
    --at;
  for (std::size_t seen = 0; at > 0; --at)
    if (text[at - 1] == '\n' && ++seen == lines)
      break;
  return std::string(text.substr(at));
}

std::vector<std::byte> Zip(const std::vector<Entry> &entries) {
  // One date for every file: now (UTC), in the format's two 16-bit fields.
  const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
  const auto day = std::chrono::floor<std::chrono::days>(now);
  const std::chrono::year_month_day date{day};
  const std::chrono::hh_mm_ss clock{now - day};
  const auto year = std::max(static_cast<int>(date.year()), 1980);
  const auto dosDate =
      static_cast<std::uint32_t>(((year - 1980) << 9) | (static_cast<unsigned>(date.month()) << 5) |
                                 static_cast<unsigned>(date.day()));
  const auto dosTime =
      static_cast<std::uint32_t>((clock.hours().count() << 11) | (clock.minutes().count() << 5) |
                                 (clock.seconds().count() / 2));

  std::vector<std::byte> out, directory;
  for (const auto &entry : entries) {
    const auto offset = static_cast<std::uint32_t>(out.size());
    const auto crc = Crc32(entry.data);
    const auto size = static_cast<std::uint32_t>(entry.data.size());
    const auto name = static_cast<std::uint32_t>(entry.name.size());
    // Local header, then the file as it is (method 0: stored).
    Put32(out, 0x04034b50);
    Put16(out, 20);     // version needed
    Put16(out, 0x0800); // names are UTF-8
    Put16(out, 0);
    Put16(out, dosTime);
    Put16(out, dosDate);
    Put32(out, crc);
    Put32(out, size);
    Put32(out, size);
    Put16(out, name);
    Put16(out, 0);
    PutText(out, entry.name);
    PutText(out, entry.data);

    Put32(directory, 0x02014b50);
    Put16(directory, 20);
    Put16(directory, 20);
    Put16(directory, 0x0800);
    Put16(directory, 0);
    Put16(directory, dosTime);
    Put16(directory, dosDate);
    Put32(directory, crc);
    Put32(directory, size);
    Put32(directory, size);
    Put16(directory, name);
    Put16(directory, 0); // extra
    Put16(directory, 0); // comment
    Put16(directory, 0); // disk
    Put16(directory, 0); // internal attributes
    Put32(directory, 0); // external attributes
    Put32(directory, offset);
    PutText(directory, entry.name);
  }
  const auto start = static_cast<std::uint32_t>(out.size());
  out.insert(out.end(), directory.begin(), directory.end());
  Put32(out, 0x06054b50);
  Put16(out, 0);
  Put16(out, 0);
  Put16(out, static_cast<std::uint32_t>(entries.size()));
  Put16(out, static_cast<std::uint32_t>(entries.size()));
  Put32(out, static_cast<std::uint32_t>(directory.size()));
  Put32(out, start);
  Put16(out, 0);
  return out;
}

std::string Format(const SessionSummary &summary) {
  std::string out;
  out += std::format("tandem: {}\n", summary.build);
  out += std::format("role: {}{}\n", summary.role, summary.relay ? " (through the relay)" : "");
  out += std::format("started: {} UTC\n", summary.startedUtc);
  out += std::format("lasted: {} min {} s\n", summary.seconds / 60, summary.seconds % 60);
  out += std::format("players at most: {}\n", summary.peakPlayers);
  out += std::format("ended: {}\n", summary.ended);
  if (summary.rttMs >= 0)
    out +=
        std::format("link to the host: {} ms, {:.1f}% lost\n", summary.rttMs, summary.lossPercent);
  out += std::format("consistency: {} digest mismatch(es), {} resync(s)\n", summary.mismatches,
                     summary.resyncs);
  for (const auto &note : summary.notes)
    out += std::format("note: {}\n", note);
  return out;
}

Result Write(const Options &options) {
  Result result;
  const auto &game = options.game;
  std::vector<Entry> entries;
  const auto add = [&](std::string name, std::string text) {
    if (text.empty())
      return;
    result.included.push_back(name);
    entries.push_back({std::move(name), std::move(text)});
  };

  add("versions.txt", options.versions);
  const auto logs = game / "red4ext" / "logs";
  const auto ours = Newest(logs, "tandem-");
  if (!ours.empty())
    add("tandem.log", Redact(ReadTail(ours[0], kLogBytes), game));
  if (ours.size() > 1)
    add("tandem.previous.log", Redact(ReadTail(ours[1], kLogBytes), game));
  if (const auto loader = Newest(logs, "red4ext-"); !loader.empty())
    add("red4ext.log", Redact(TailLines(ReadTail(loader[0], kLogBytes), kTailLines), game));
  add("redscript.log",
      Redact(TailLines(ReadTail(game / "r6" / "logs" / "redscript_rCURRENT.log", kLogBytes),
                       kTailLines),
             game));
  const auto plugin = game / "red4ext" / "plugins" / "tandem";
  add("tandem.ini", RedactIni(ReadTail(plugin / "tandem.ini", kLogBytes), game));
  add("last-session.txt", Redact(ReadTail(plugin / "last-session.txt", kLogBytes), game));

  if (entries.empty()) {
    result.error = "nothing to report: no log, no tandem.ini";
    return result;
  }
  const auto stamp =
      !options.stamp.empty()
          ? options.stamp
          : std::format("{:%Y%m%d-%H%M%S}",
                        std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
  std::error_code ec;
  std::filesystem::create_directories(options.out, ec);
  const auto path = options.out / std::format("tandem-report-{}.zip", stamp);
  const auto bytes = Zip(entries);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  file.close();
  if (!file) {
    result.error = std::format("could not write {}", Bytes(path));
    result.included.clear();
    return result;
  }
  result.zip = path;
  return result;
}
} // namespace tandem::report
