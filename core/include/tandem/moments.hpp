#pragma once

// The moment list (docs/architecture/scripted-moments.md, "The moment
// list"): a scripted moment found broken in game is marked by the host's
// scene, quest phase or tracked journal entry, without a code change, in
// dist/moments.txt (shipped next to tandem.dll as moments.txt) or tandem.ini
// (moment_watch, moment_play). Rules only: the plugin hashes resource paths
// and reads what its game plays.
//   watch: the guests that share the host's moment watch it through the
//          host's eyes (spectating, reason d), whatever the generic rule says;
//   play:  a cutscene or the quest's camera in it is not watched: the guests
//          keep their own eyes (scene replay shows the actors). Another world
//          and a ride the guest cannot follow are watched all the same.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tandem::moments {
enum class Verdict : std::uint8_t { None = 0, Watch = 1, Play = 2 };
enum class Kind : std::uint8_t { Scene = 0, Phase = 1, Quest = 2 };

struct Entry {
  Kind kind{};
  Verdict verdict{};
  std::uint64_t hash{}; // Scene, Phase: the resource path's hash
  std::string text;     // as written (Quest: lower case, matched inside the path)
  std::string note;     // the line's comment, for the log
};

// A resource path's hash, as the engine's ResourcePath has it (the plugin's;
// the tests give their own).
using PathHash = std::uint64_t (*)(std::string_view path);

// One key: `scene:PATH`, `phase:PATH` (a .scene or .questphase resource
// path, or its hash written 0x...), `quest:TEXT` (a piece of the host's
// tracked journal path, e.g. `q101_` or `quests/main_quest/prologue`).
// nullopt and `error` set when it is none of these.
std::optional<Entry> ParseKey(std::string_view key, Verdict verdict, PathHash hash,
                              std::string &error);
// A line of moments.txt: `watch KEY [# why]` or `play KEY [# why]`. Blank
// lines and comments: nullopt, `error` empty.
std::optional<Entry> ParseLine(std::string_view line, PathHash hash, std::string &error);

// What the host's V is in now.
struct Now {
  std::uint64_t scene{};             // the scene it plays (0: none)
  std::vector<std::uint64_t> phases; // the quest phases that ran a node lately
  std::string quest;                 // its tracked journal entry's path ("": none)
};

class List {
public:
  void Add(Entry entry);
  void Clear() { m_entries.clear(); }
  // The most specific entry that matches: a scene, then a phase, then a
  // quest; one key listed both ways counts as watch. `matched`: the entry.
  Verdict Of(const Now &now, const Entry **matched = nullptr) const;
  // Some entry is a phase: the plugin then notes the phases that run.
  bool HasPhases() const;
  std::size_t Size() const { return m_entries.size(); }
  const std::vector<Entry> &Entries() const { return m_entries; }

private:
  std::vector<Entry> m_entries;
};

const char *NameOf(Verdict verdict);
} // namespace tandem::moments
