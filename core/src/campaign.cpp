#include "tandem/campaign.hpp"

#include "tandem/report.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <ctime>
#include <format>
#include <fstream>
#include <limits>
#include <system_error>
#include <tuple>
#include <utility>

namespace tandem::campaign {
namespace {
namespace fs = std::filesystem;

bool Contains(std::span<const std::uint64_t> ids, std::uint64_t id) {
  return std::ranges::find(ids, id) != ids.end();
}

// A copy and the copies it came from, newest first.
std::vector<std::uint64_t> Chain(const Copy &copy) {
  std::vector<std::uint64_t> chain{copy.copy};
  chain.insert(chain.end(), copy.ancestors.begin(), copy.ancestors.end());
  return chain;
}

// A folder name the game gave a slot: letters, digits, '-' and '_'.
bool PlainName(std::string_view name) {
  return !name.empty() && name.size() <= 64 && std::ranges::all_of(name, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '_';
  });
}

// A save's file name inside an archive: one name, no folder, no way out.
bool PlainFile(std::string_view name) {
  return !name.empty() && name.size() <= 64 && name != "." && name != ".." &&
         std::ranges::all_of(name, [](char c) {
           return static_cast<unsigned char>(c) >= 0x20 && c != '/' && c != '\\' && c != ':' &&
                  c != '*' && c != '?' && c != '"' && c != '<' && c != '>' && c != '|' && c != 0x7F;
         });
}

std::optional<std::uint64_t> Hex(std::string_view text) {
  std::uint64_t value = 0;
  if (text.starts_with("0x"))
    text.remove_prefix(2);
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
  if (text.empty() || error != std::errc{} || end != text.data() + text.size())
    return std::nullopt;
  return value;
}

std::optional<std::uint64_t> Decimal(std::string_view text) {
  std::uint64_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || error != std::errc{} || end != text.data() + text.size())
    return std::nullopt;
  return value;
}

std::string_view Trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
    text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
    text.remove_suffix(1);
  return text;
}

std::optional<std::string> ReadFile(const fs::path &path, std::uintmax_t limit) {
  std::error_code ec;
  const auto size = fs::file_size(path, ec);
  if (ec || size > limit)
    return std::nullopt;
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return std::nullopt;
  std::string data(static_cast<std::size_t>(size), '\0');
  in.read(data.data(), static_cast<std::streamsize>(size));
  if (static_cast<std::uintmax_t>(in.gcount()) != size)
    return std::nullopt;
  return data;
}

// Written aside, then renamed over `path`: a reader never sees half a file.
bool WriteFile(const fs::path &path, std::string_view data, std::string &error) {
  auto part = path;
  part += ".part";
  {
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.close();
    if (!out) {
      error = std::format("cannot write {}", part.filename().string());
      std::error_code ignored;
      fs::remove(part, ignored);
      return false;
    }
  }
  std::error_code ec;
  fs::rename(part, path, ec);
  if (ec) {
    error = std::format("cannot write {}: {}", path.filename().string(), ec.message());
    fs::remove(part, ec);
    return false;
  }
  return true;
}

std::uint32_t Get16(std::span<const std::byte> bytes, std::size_t at) {
  return std::to_integer<std::uint32_t>(bytes[at]) | std::to_integer<std::uint32_t>(bytes[at + 1])
                                                         << 8;
}

std::uint32_t Get32(std::span<const std::byte> bytes, std::size_t at) {
  return Get16(bytes, at) | Get16(bytes, at + 2) << 16;
}

std::string Text(std::span<const std::byte> bytes, std::size_t at, std::size_t size) {
  std::string text(size, '\0');
  for (std::size_t i = 0; i < size; ++i)
    text[i] = static_cast<char>(bytes[at + i]);
  return text;
}

// One stored entry of a zip at `at` (a local header). None: not one, or
// not one we read (compressed, sizes after the data, a bad CRC).
struct ZipEntry {
  std::string name;
  std::string data;
  std::size_t next{};
};
constexpr std::uint32_t kLocalHeader = 0x04034b50;
constexpr std::size_t kLocalHeaderBytes = 30;
std::optional<ZipEntry> EntryAt(std::span<const std::byte> bytes, std::size_t at,
                                std::string &error) {
  if (at + kLocalHeaderBytes > bytes.size() || Get32(bytes, at) != kLocalHeader) {
    error = "not a zip";
    return std::nullopt;
  }
  const auto flags = Get16(bytes, at + 6);
  const auto method = Get16(bytes, at + 8);
  const auto crc = Get32(bytes, at + 14);
  const auto stored = Get32(bytes, at + 18);
  const auto size = Get32(bytes, at + 22);
  const auto nameSize = Get16(bytes, at + 26);
  const auto extraSize = Get16(bytes, at + 28);
  if (method != 0 || (flags & 0x0008) != 0 || stored != size) {
    error = "not a campaign archive of tandem's (its files are compressed)";
    return std::nullopt;
  }
  const std::size_t data = at + kLocalHeaderBytes + nameSize + extraSize;
  if (data + size > bytes.size()) {
    error = "the archive is cut short";
    return std::nullopt;
  }
  ZipEntry entry{.name = Text(bytes, at + kLocalHeaderBytes, nameSize),
                 .data = Text(bytes, data, size),
                 .next = data + size};
  if (report::Crc32(entry.data) != crc) {
    error = std::format("{} is damaged (its checksum differs)", entry.name);
    return std::nullopt;
  }
  return entry;
}

std::tm Utc(std::uint64_t unix) {
  const auto at = std::chrono::sys_seconds{std::chrono::seconds{static_cast<std::int64_t>(unix)}};
  const auto day = std::chrono::floor<std::chrono::days>(at);
  const std::chrono::year_month_day date{day};
  const std::chrono::hh_mm_ss clock{at - day};
  std::tm tm{};
  tm.tm_year = static_cast<int>(date.year()) - 1900;
  tm.tm_mon = static_cast<int>(static_cast<unsigned>(date.month())) - 1;
  tm.tm_mday = static_cast<int>(static_cast<unsigned>(date.day()));
  tm.tm_hour = static_cast<int>(clock.hours().count());
  tm.tm_min = static_cast<int>(clock.minutes().count());
  return tm;
}
} // namespace

std::string Clean(std::string_view text) {
  std::string out;
  for (const char c : text)
    if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F)
      out.push_back(c);
  if (out.size() <= kMaxText)
    return out;
  out.resize(kMaxText);
  // Not half a character: back to the start of the last one, and drop it
  // if it does not fit whole.
  auto start = out.size() - 1;
  while (start > 0 && (static_cast<unsigned char>(out[start]) & 0xC0) == 0x80)
    --start;
  const auto lead = static_cast<unsigned char>(out[start]);
  const std::size_t length = lead < 0x80             ? 1
                             : (lead & 0xE0) == 0xC0 ? 2
                             : (lead & 0xF0) == 0xE0 ? 3
                             : (lead & 0xF8) == 0xF0 ? 4
                                                     : 1;
  if (out.size() - start < length)
    out.resize(start);
  return out;
}

Copy Start(std::uint64_t campaign, std::uint64_t copy, std::string_view who,
           std::uint64_t nowUnix) {
  return Copy{.campaign = campaign, .copy = copy, .savedUnix = nowUnix, .who = Clean(who)};
}

Copy Advance(const Copy &from, std::uint64_t copy) {
  Copy next = from;
  next.copy = copy;
  next.generation = from.generation + 1;
  next.ancestors.clear();
  next.ancestors.push_back(from.copy);
  for (const auto ancestor : from.ancestors)
    if (next.ancestors.size() < kMaxAncestors)
      next.ancestors.push_back(ancestor);
  return next;
}

std::vector<std::uint64_t> Pack(const Copy &copy) {
  if (!copy.Is())
    return {};
  std::vector<std::uint64_t> words{copy.campaign, copy.copy, copy.generation};
  words.insert(words.end(), copy.ancestors.begin(), copy.ancestors.end());
  return words;
}

std::optional<Copy> Unpack(std::span<const std::uint64_t> words) {
  if (words.size() < 3 || words.size() > kMaxWords || words[0] == 0 || words[1] == 0 ||
      words[2] > std::numeric_limits<std::uint32_t>::max())
    return std::nullopt;
  Copy copy{.campaign = words[0],
            .copy = words[1],
            .generation = static_cast<std::uint32_t>(words[2]),
            .ancestors = {words.begin() + 3, words.end()}};
  if (copy.ancestors.size() > copy.generation || Contains(copy.ancestors, copy.copy) ||
      Contains(copy.ancestors, 0))
    return std::nullopt;
  return copy;
}

proto::Campaign ToWire(const Copy &copy) {
  if (!copy.Is())
    return {};
  return proto::Campaign{.campaign = copy.campaign,
                         .copy = copy.copy,
                         .generation = copy.generation,
                         .ancestors = copy.ancestors,
                         .savedUnix = copy.savedUnix,
                         .who = Clean(copy.who),
                         .quest = Clean(copy.quest)};
}

Copy FromWire(const proto::Campaign &wire) {
  return Copy{.campaign = wire.campaign,
              .copy = wire.copy,
              .generation = wire.generation,
              .ancestors = wire.ancestors,
              .savedUnix = wire.savedUnix,
              .who = Clean(wire.who),
              .quest = Clean(wire.quest)};
}

Relation Relate(const Copy &ours, const Copy &theirs) {
  if (!ours.Is() || !theirs.Is())
    return Relation::None;
  if (ours.campaign != theirs.campaign)
    return Relation::Unrelated;
  if (ours.copy == theirs.copy) {
    // One copy, one machine: the later save is ahead on the same line.
    const auto a = std::pair(ours.generation, ours.savedUnix);
    const auto b = std::pair(theirs.generation, theirs.savedUnix);
    return a == b ? Relation::Same : a < b ? Relation::Behind : Relation::Ahead;
  }
  if (Contains(theirs.ancestors, ours.copy))
    return Relation::Behind;
  if (Contains(ours.ancestors, theirs.copy))
    return Relation::Ahead;
  return Relation::Fork;
}

std::string_view NameOf(Relation relation) {
  switch (relation) {
  case Relation::None:
    return "none";
  case Relation::Unrelated:
    return "unrelated";
  case Relation::Same:
    return "same";
  case Relation::Ahead:
    return "ahead";
  case Relation::Behind:
    return "behind";
  case Relation::Fork:
    return "fork";
  }
  return "?";
}

std::uint64_t CommonAncestor(const Copy &a, const Copy &b) {
  if (!a.Is() || a.campaign != b.campaign)
    return 0;
  const auto theirs = Chain(b);
  for (const auto id : Chain(a))
    if (Contains(theirs, id))
      return id;
  return 0;
}

Verdict Judge(std::span<const Member> members, proto::PlayerId self, proto::PlayerId host) {
  Verdict verdict;
  const auto hosted =
      std::ranges::find_if(members, [&](const Member &m) { return m.player == host; });
  if (hosted == members.end() || !hosted->copy.Is())
    return verdict;
  std::vector<const Member *> copies;
  for (const auto &member : members)
    if (member.copy.campaign == hosted->copy.campaign)
      copies.push_back(&member);
  if (copies.size() < 2)
    return verdict;
  // A head: a copy nobody's is ahead of; the same copy twice is one head.
  for (const auto *member : copies) {
    const bool behind = std::ranges::any_of(copies, [&](const Member *other) {
      return Relate(member->copy, other->copy) == Relation::Behind;
    });
    const bool counted = std::ranges::any_of(verdict.heads, [&](const Member &head) {
      return Relate(member->copy, head.copy) == Relation::Same;
    });
    if (!behind && !counted)
      verdict.heads.push_back(*member);
  }
  const auto isHead = [&](const Member &member) {
    return std::ranges::any_of(verdict.heads, [&](const Member &head) {
      return Relate(member.copy, head.copy) == Relation::Same;
    });
  };
  const bool allSame = std::ranges::all_of(copies, [&](const Member *member) {
    return Relate(member->copy, copies.front()->copy) == Relation::Same;
  });
  verdict.kind = allSame                     ? Verdict::Kind::Same
                 : verdict.heads.size() == 1 ? Verdict::Kind::FastForward
                                             : Verdict::Kind::Fork;
  const auto ours =
      std::ranges::find_if(copies, [&](const Member *member) { return member->player == self; });
  verdict.selfBehind = ours != copies.end() && !isHead(**ours);
  verdict.hostBehind = !isHead(*hosted);
  return verdict;
}

bool SameNews(const Verdict &a, const Verdict &b) {
  if (a.kind != b.kind || a.selfBehind != b.selfBehind || a.hostBehind != b.hostBehind ||
      a.heads.size() != b.heads.size())
    return false;
  for (std::size_t i = 0; i < a.heads.size(); ++i)
    if (a.heads[i].player != b.heads[i].player ||
        Relate(a.heads[i].copy, b.heads[i].copy) != Relation::Same)
      return false;
  return true;
}

// ---- Records ----

std::string Write(const Record &record) {
  const auto &copy = record.copy;
  std::string ancestors;
  for (const auto ancestor : copy.ancestors)
    ancestors += std::format("{}{:016x}", ancestors.empty() ? "" : ",", ancestor);
  auto out =
      std::format("tandem-campaign 1\n"
                  "campaign = {:016x}\n"
                  "copy = {:016x}\n"
                  "generation = {}\n"
                  "ancestors = {}\n"
                  "saved = {}\n"
                  "who = {}\n"
                  "quest = {}\n"
                  "slot = {}\n"
                  "foreign = {}\n"
                  "set_aside = {}\n"
                  "chosen = {:016x}\n",
                  copy.campaign, copy.copy, copy.generation, ancestors, copy.savedUnix,
                  Clean(copy.who), Clean(copy.quest), PlainName(record.slot) ? record.slot : "",
                  record.foreign ? 1 : 0, record.setAside ? 1 : 0, record.chosen);
  for (const auto &mod : record.mods) {
    std::string name;
    for (const char c : mod.name)
      if (static_cast<unsigned char>(c) >= 0x20)
        name.push_back(c);
    out += std::format("mod = {:016x} {} {}\n", mod.hash, mod.categories, name);
  }
  return out;
}

std::optional<Record> Read(std::string_view text) {
  constexpr std::string_view kHeader = "tandem-campaign 1";
  const auto first = text.find('\n');
  if (Trim(text.substr(0, first)) != kHeader)
    return std::nullopt;
  text.remove_prefix(first == std::string_view::npos ? text.size() : first + 1);
  Record record;
  std::vector<std::uint64_t> words(3, 0);
  bool ancestorsOk = true;
  while (!text.empty()) {
    const auto end = text.find('\n');
    const auto line = text.substr(0, end);
    text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
    const auto equals = line.find('=');
    if (equals == std::string_view::npos)
      continue;
    const auto key = Trim(line.substr(0, equals));
    const auto value = Trim(line.substr(equals + 1));
    if (key == "campaign") {
      words[0] = Hex(value).value_or(0);
    } else if (key == "copy") {
      words[1] = Hex(value).value_or(0);
    } else if (key == "generation") {
      words[2] = Decimal(value).value_or(std::numeric_limits<std::uint64_t>::max());
    } else if (key == "ancestors") {
      for (std::string_view rest = value; !rest.empty();) {
        const auto comma = rest.find(',');
        const auto id = Hex(Trim(rest.substr(0, comma)));
        if (!id)
          ancestorsOk = false;
        else
          words.push_back(*id);
        rest.remove_prefix(comma == std::string_view::npos ? rest.size() : comma + 1);
      }
    } else if (key == "saved") {
      record.copy.savedUnix = Decimal(value).value_or(0);
    } else if (key == "who") {
      record.copy.who = Clean(value);
    } else if (key == "quest") {
      record.copy.quest = Clean(value);
    } else if (key == "slot") {
      record.slot = PlainName(value) ? std::string(value) : std::string();
    } else if (key == "foreign") {
      record.foreign = value == "1";
    } else if (key == "set_aside") {
      record.setAside = value == "1";
    } else if (key == "chosen") {
      record.chosen = Hex(value).value_or(0);
    } else if (key == "mod" && record.mods.size() < proto::kMaxModEntries) {
      // HASH CATEGORIES NAME
      const auto space = value.find(' ');
      const auto second = space == std::string_view::npos ? space : value.find(' ', space + 1);
      const auto hash = Hex(value.substr(0, space));
      const auto categories = space == std::string_view::npos
                                  ? std::nullopt
                                  : Decimal(value.substr(space + 1, second == std::string_view::npos
                                                                        ? std::string_view::npos
                                                                        : second - space - 1));
      if (hash && categories && *categories <= 0xFF)
        record.mods.push_back(
            {.name = second == std::string_view::npos ? "" : std::string(value.substr(second + 1)),
             .hash = *hash,
             .categories = static_cast<std::uint8_t>(*categories)});
    }
  }
  const auto copy = ancestorsOk ? Unpack(words) : std::nullopt;
  if (!copy)
    return std::nullopt;
  record.copy.campaign = copy->campaign;
  record.copy.copy = copy->copy;
  record.copy.generation = copy->generation;
  record.copy.ancestors = copy->ancestors;
  return record;
}

std::vector<Record> LoadRecords(const fs::path &folder) {
  std::vector<Record> records;
  std::error_code ec;
  for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec) || it->path().extension() != ".txt")
      continue;
    if (const auto text = ReadFile(it->path(), 1 << 20))
      if (auto record = Read(*text))
        records.push_back(std::move(*record));
  }
  // The same order on every machine and every run.
  std::ranges::sort(records, [](const Record &a, const Record &b) {
    return std::tie(a.copy.campaign, a.copy.copy) < std::tie(b.copy.campaign, b.copy.copy);
  });
  return records;
}

bool SaveRecord(const fs::path &folder, const Record &record, std::string &error) {
  if (!record.copy.Is()) {
    error = "not a campaign copy";
    return false;
  }
  std::error_code ec;
  fs::create_directories(folder, ec);
  if (ec) {
    error = std::format("cannot make {}: {}", folder.string(), ec.message());
    return false;
  }
  return WriteFile(folder /
                       std::format("{:016x}-{:016x}.txt", record.copy.campaign, record.copy.copy),
                   Write(record), error);
}

std::optional<Record> NewestOf(std::span<const Record> records, std::uint64_t campaign) {
  const Record *best = nullptr;
  for (const auto &record : records)
    if (record.copy.campaign == campaign && campaign != 0 && !record.foreign && !record.setAside &&
        (!best || std::pair(record.copy.savedUnix, record.copy.generation) >
                      std::pair(best->copy.savedUnix, best->copy.generation)))
      best = &record;
  return best ? std::optional(*best) : std::nullopt;
}

std::optional<Record> Find(std::span<const Record> records, std::uint64_t campaign,
                           std::uint64_t copy) {
  const auto found = std::ranges::find_if(records, [&](const Record &record) {
    return record.copy.campaign == campaign && record.copy.copy == copy;
  });
  return found != records.end() ? std::optional(*found) : std::nullopt;
}

std::optional<Record> NewerElsewhere(std::span<const Record> records, const Copy &ours) {
  const Record *best = nullptr;
  for (const auto &record : records) {
    if (!record.foreign || record.setAside)
      continue;
    const auto relation = Relate(ours, record.copy);
    if ((relation == Relation::Behind || relation == Relation::Fork) &&
        (!best || record.copy.savedUnix > best->copy.savedUnix))
      best = &record;
  }
  return best ? std::optional(*best) : std::nullopt;
}

// ---- Archives ----

std::vector<std::byte> MakeArchive(const Record &record, std::span<const File> files) {
  std::vector<report::Entry> entries{{std::string(kRecordEntry), Write(record)}};
  for (const auto &file : files)
    entries.push_back({std::string(kSaveEntryPrefix) + file.name, file.data});
  return report::Zip(entries);
}

std::optional<Archive> ReadArchive(std::span<const std::byte> bytes, std::string &error) {
  if (bytes.size() > kMaxArchiveBytes) {
    error = "the archive is too large";
    return std::nullopt;
  }
  Archive archive;
  bool first = true, save = false;
  for (std::size_t at = 0; at + 4 <= bytes.size() && Get32(bytes, at) == kLocalHeader;) {
    auto entry = EntryAt(bytes, at, error);
    if (!entry)
      return std::nullopt;
    at = entry->next;
    if (first) {
      first = false;
      auto record = entry->name == kRecordEntry ? Read(entry->data) : std::nullopt;
      if (!record) {
        error = "not a campaign archive of tandem's (no lineage)";
        return std::nullopt;
      }
      archive.record = std::move(*record);
      continue;
    }
    const std::string_view name = entry->name;
    if (!name.starts_with(kSaveEntryPrefix) || !PlainFile(name.substr(kSaveEntryPrefix.size()))) {
      error = std::format("the archive holds {}, which is not a save's file", entry->name);
      return std::nullopt;
    }
    const auto file = std::string(name.substr(kSaveEntryPrefix.size()));
    if (std::ranges::any_of(archive.files, [&](const File &f) { return f.name == file; })) {
      error = std::format("the archive holds {} twice", file);
      return std::nullopt;
    }
    if (archive.files.size() >= kMaxArchiveFiles) {
      error = "the archive holds too many files";
      return std::nullopt;
    }
    save = save || file == kSaveFile;
    archive.files.push_back({file, std::move(entry->data)});
  }
  if (first) {
    error = "not a zip";
    return std::nullopt;
  }
  if (!save) {
    error = "the archive holds no save (sav.dat)";
    return std::nullopt;
  }
  return archive;
}

std::optional<Record> PeekRecord(std::span<const std::byte> head) {
  std::string error;
  const auto entry = EntryAt(head, 0, error);
  if (!entry || entry->name != kRecordEntry)
    return std::nullopt;
  return Read(entry->data);
}

std::string ArchiveName(const Record &record, std::uint64_t nowUnix) {
  std::string who;
  for (const char c : record.copy.who)
    who.push_back((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                          c == '-'
                      ? c
                      : '_');
  if (who.empty())
    who = "V";
  const auto tm = Utc(nowUnix);
  return std::format("campaign-{}-{:04}{:02}{:02}-{:02}{:02}-g{}.zip", who, tm.tm_year + 1900,
                     tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, record.copy.generation);
}

std::vector<std::string> Slots(const fs::path &saves) {
  std::vector<std::string> slots;
  std::error_code ec;
  for (fs::directory_iterator it(saves, ec), end; !ec && it != end; it.increment(ec))
    if (it->is_directory(ec))
      slots.push_back(it->path().filename().string());
  std::ranges::sort(slots);
  return slots;
}

std::string NextManualSlot(std::span<const std::string> slots) {
  std::int64_t highest = -1;
  for (const std::string_view slot : slots) {
    if (!slot.starts_with(kManualPrefix))
      continue;
    if (const auto number = Decimal(slot.substr(kManualPrefix.size()));
        number && *number < 1'000'000'000)
      highest = std::max(highest, static_cast<std::int64_t>(*number));
  }
  return std::format("{}{}", kManualPrefix, highest + 1);
}

std::string NewestSlot(const fs::path &saves, fs::file_time_type since) {
  std::string newest;
  fs::file_time_type best = since;
  std::error_code ec;
  for (fs::directory_iterator it(saves, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_directory(ec))
      continue;
    std::error_code fileEc;
    const auto written = fs::last_write_time(it->path() / kSaveFile, fileEc);
    if (!fileEc && written >= best) {
      best = written;
      newest = it->path().filename().string();
    }
  }
  return newest;
}

Exported Export(const fs::path &slot, Record record, const fs::path &folder,
                std::uint64_t nowUnix) {
  Exported result;
  std::vector<File> files;
  std::uintmax_t total = 0;
  std::error_code ec;
  for (fs::directory_iterator it(slot, ec), end; !ec && it != end; it.increment(ec)) {
    const auto name = it->path().filename().string();
    if (!it->is_regular_file(ec) || !PlainFile(name) || name.ends_with(".part"))
      continue;
    auto data = ReadFile(it->path(), kMaxArchiveBytes);
    if (!data) {
      result.error = std::format("cannot read {}", name);
      return result;
    }
    total += data->size();
    if (total > kMaxArchiveBytes || files.size() >= kMaxArchiveFiles) {
      result.error = "the save is too large to export";
      return result;
    }
    files.push_back({name, std::move(*data)});
  }
  if (ec) {
    result.error =
        std::format("cannot read the save {}: {}", slot.filename().string(), ec.message());
    return result;
  }
  if (std::ranges::none_of(files, [](const File &f) { return f.name == kSaveFile; })) {
    result.error = std::format("{} holds no save (sav.dat)", slot.filename().string());
    return result;
  }
  std::ranges::sort(files, {}, &File::name);
  record.slot = slot.filename().string();
  record.foreign = false;
  fs::create_directories(folder, ec);
  if (ec) {
    result.error = std::format("cannot make {}: {}", folder.string(), ec.message());
    return result;
  }
  const auto bytes = MakeArchive(record, files);
  const auto name = ArchiveName(record, nowUnix);
  auto target = folder / name;
  for (int n = 2; fs::exists(target, ec) && n < 100; ++n)
    target = folder / std::format("{}-{}.zip", name.substr(0, name.size() - 4), n);
  std::string error;
  if (!WriteFile(target,
                 std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()),
                 error)) {
    result.error = error;
    return result;
  }
  result.zip = target;
  return result;
}

std::vector<Listed> ListArchives(const fs::path &folder) {
  std::vector<Listed> listed;
  std::error_code ec;
  for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec) || it->path().extension() != ".zip")
      continue;
    std::ifstream in(it->path(), std::ios::binary);
    std::vector<std::byte> head(64 * 1024);
    in.read(reinterpret_cast<char *>(head.data()), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<std::size_t>(std::max<std::streamsize>(in.gcount(), 0)));
    if (auto record = PeekRecord(head))
      listed.push_back({it->path(), std::move(*record)});
  }
  std::ranges::sort(listed, [](const Listed &a, const Listed &b) {
    return std::pair(a.record.copy.savedUnix, a.zip.filename().string()) >
           std::pair(b.record.copy.savedUnix, b.zip.filename().string());
  });
  return listed;
}

std::vector<std::string> MissingMods(std::span<const mods::Mod> theirs,
                                     std::span<const mods::Mod> ours) {
  std::vector<std::string> missing;
  for (const auto &mod : theirs) {
    if (mod.categories == mods::kLocal)
      continue;
    if (std::ranges::none_of(ours, [&](const mods::Mod &own) { return own.hash == mod.hash; }))
      missing.push_back(mod.name.empty() ? std::format("{:016x}", mod.hash) : mod.name);
  }
  return missing;
}

Installed Install(const Archive &archive, const fs::path &saves) {
  Installed result;
  std::error_code ec;
  if (!fs::is_directory(saves, ec)) {
    result.error = std::format("no saves folder at {}", saves.string());
    return result;
  }
  // A new slot, made here: never one that exists.
  auto slots = Slots(saves);
  std::string slot;
  for (int tries = 0; tries < 16 && slot.empty(); ++tries) {
    const auto name = NextManualSlot(slots);
    if (fs::create_directory(saves / name, ec) && !ec)
      slot = name;
    else
      slots.push_back(name);
  }
  if (slot.empty()) {
    result.error = "cannot make a new save slot";
    return result;
  }
  const auto folder = saves / slot;
  // The save's own file last: a slot without it is no save to the game.
  auto files = archive.files;
  std::stable_partition(files.begin(), files.end(),
                        [](const File &f) { return f.name != kSaveFile; });
  for (auto &file : files) {
    // Metadata that names the slot it came from names the new one.
    if (file.name.starts_with("metadata.") && file.name.ends_with(".json") &&
        PlainName(archive.record.slot) && archive.record.slot != slot) {
      const auto from = std::format("\"{}\"", archive.record.slot);
      const auto to = std::format("\"{}\"", slot);
      for (auto at = file.data.find(from); at != std::string::npos;
           at = file.data.find(from, at + to.size()))
        file.data.replace(at, from.size(), to);
    }
    std::string error;
    if (!WriteFile(folder / file.name, file.data, error)) {
      fs::remove_all(folder, ec);
      result.error = error;
      return result;
    }
  }
  result.slot = slot;
  result.record = archive.record;
  result.record.slot = slot;
  result.record.foreign = false;
  result.record.setAside = false;
  return result;
}

Installed Import(const fs::path &zip, const fs::path &saves) {
  Installed result;
  const auto data = ReadFile(zip, kMaxArchiveBytes);
  if (!data) {
    result.error = std::format("cannot read {}", zip.filename().string());
    return result;
  }
  const auto bytes = std::as_bytes(std::span(data->data(), data->size()));
  auto archive = ReadArchive(bytes, result.error);
  if (!archive)
    return result;
  return Install(*archive, saves);
}
} // namespace tandem::campaign
