#include "tandem/moments.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace tandem::moments {
namespace {
std::string_view Trim(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
    text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    text.remove_suffix(1);
  return text;
}

std::string Lower(std::string_view text) {
  std::string out(text);
  std::ranges::transform(out, out.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

// "0x" and 1 to 16 hex digits: the hash itself (the log's way of saying it).
std::optional<std::uint64_t> Hex(std::string_view text) {
  if (text.size() < 3 || text.size() > 18 || text[0] != '0' || (text[1] != 'x' && text[1] != 'X'))
    return std::nullopt;
  std::uint64_t value = 0;
  const auto *end = text.data() + text.size();
  const auto [at, error] = std::from_chars(text.data() + 2, end, value, 16);
  if (error != std::errc{} || at != end)
    return std::nullopt;
  return value;
}
} // namespace

std::optional<Entry> ParseKey(std::string_view key, Verdict verdict, PathHash hash,
                              std::string &error) {
  key = Trim(key);
  const auto colon = key.find(':');
  if (colon == std::string_view::npos) {
    error = "a key is scene:PATH, phase:PATH or quest:TEXT";
    return std::nullopt;
  }
  const auto kind = Lower(Trim(key.substr(0, colon)));
  const auto value = Trim(key.substr(colon + 1));
  if (value.empty()) {
    error = "nothing after " + kind + ":";
    return std::nullopt;
  }
  Entry entry{.verdict = verdict, .text = std::string(value)};
  if (kind == "quest") {
    entry.kind = Kind::Quest;
    entry.text = Lower(value);
    return entry;
  }
  if (kind != "scene" && kind != "phase") {
    error = "unknown kind " + kind + " (scene, phase or quest)";
    return std::nullopt;
  }
  entry.kind = kind == "scene" ? Kind::Scene : Kind::Phase;
  if (const auto direct = Hex(value))
    entry.hash = *direct;
  else if (hash)
    entry.hash = hash(value);
  if (entry.hash == 0) {
    error = "no hash for " + std::string(value);
    return std::nullopt;
  }
  return entry;
}

std::optional<Entry> ParseLine(std::string_view line, PathHash hash, std::string &error) {
  error.clear();
  std::string_view note;
  if (const auto mark = line.find('#'); mark != std::string_view::npos) {
    note = Trim(line.substr(mark + 1));
    line = line.substr(0, mark);
  }
  line = Trim(line);
  if (line.empty())
    return std::nullopt;
  const auto space = line.find_first_of(" \t");
  if (space == std::string_view::npos) {
    error = "a line is `watch KEY` or `play KEY`";
    return std::nullopt;
  }
  const auto word = Lower(line.substr(0, space));
  if (word != "watch" && word != "play") {
    error = "unknown verdict " + word + " (watch or play)";
    return std::nullopt;
  }
  auto entry = ParseKey(line.substr(space + 1), word == "watch" ? Verdict::Watch : Verdict::Play,
                        hash, error);
  if (entry)
    entry->note = std::string(note);
  return entry;
}

void List::Add(Entry entry) { m_entries.push_back(std::move(entry)); }

Verdict List::Of(const Now &now, const Entry **matched) const {
  const auto quest = Lower(now.quest);
  for (const auto kind : {Kind::Scene, Kind::Phase, Kind::Quest}) {
    const Entry *found = nullptr;
    for (const auto &entry : m_entries) {
      if (entry.kind != kind)
        continue;
      bool match = false;
      if (kind == Kind::Scene)
        match = now.scene != 0 && entry.hash == now.scene;
      else if (kind == Kind::Phase)
        match = std::ranges::find(now.phases, entry.hash) != now.phases.end();
      else
        match = !quest.empty() && quest.find(entry.text) != std::string::npos;
      if (match && (!found || entry.verdict == Verdict::Watch))
        found = &entry;
    }
    if (found) {
      if (matched)
        *matched = found;
      return found->verdict;
    }
  }
  if (matched)
    *matched = nullptr;
  return Verdict::None;
}

bool List::HasPhases() const {
  return std::ranges::any_of(m_entries,
                             [](const Entry &entry) { return entry.kind == Kind::Phase; });
}

const char *NameOf(Verdict verdict) {
  switch (verdict) {
  case Verdict::Watch:
    return "watch";
  case Verdict::Play:
    return "play";
  default:
    return "none";
  }
}
} // namespace tandem::moments
