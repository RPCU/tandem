#pragma once

// Other mods (docs/architecture/mod-compat.md).
//
// 1. What a game's mods add, sorted from their files (Classify, Scan), and
//    what two games' sets differ in (Compare): said to the players at a
//    join, never a reason to refuse one.
// 2. One resolver for every record another game names (Resolver): the
//    record itself when this game has it, else a vanilla record like it
//    (by the hint the sender sent), else a fixed vanilla default for its
//    kind, else nothing. Every outcome but the first is logged once and
//    told to the player (grouped per sender and kind), and kept for the
//    co-op menu's list.

#include "tandem/protocol.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace tandem::mods {
// ---- Names the game's way ----

// TweakDBID of a record name: CRC32 of the name, its length above it.
constexpr std::uint64_t RecordId(std::string_view name) {
  std::uint32_t crc = 0xFFFFFFFF;
  for (const char raw : name) {
    crc ^= static_cast<unsigned char>(raw);
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320 & (0u - (crc & 1)));
  }
  return static_cast<std::uint64_t>(~crc) | (static_cast<std::uint64_t>(name.size()) << 32);
}

// CName hash: FNV-1a 64.
constexpr std::uint64_t NameHash(std::string_view name) {
  std::uint64_t hash = 0xCBF29CE484222325;
  for (const char raw : name)
    hash = (hash ^ static_cast<unsigned char>(raw)) * 0x100000001B3;
  return hash;
}

// ---- What a mod adds ----

// Categories, bits of one byte (proto::ModEntry::categories).
inline constexpr std::uint8_t kLocal = 1 << 0;    // textures, lighting, sound, UI: fine either way
inline constexpr std::uint8_t kItems = 1 << 1;    // new item records
inline constexpr std::uint8_t kVehicles = 1 << 2; // new vehicle records
inline constexpr std::uint8_t kLooks = 1 << 3;    // new appearances: clothes, hair, NPC looks
inline constexpr std::uint8_t kNpcs = 1 << 4;     // new character records
inline constexpr std::uint8_t kGameplay = 1 << 5; // stats, difficulty, AI, quickhacks, the world
inline constexpr std::uint8_t kUnknown = 1 << 6;  // cannot be told from its files (a DLL)
inline constexpr std::uint8_t kContent = kItems | kVehicles | kLooks | kNpcs;
// The one a mod is counted under when it has several, most telling first.
inline constexpr std::array kCategoryOrder{kItems,    kVehicles, kNpcs, kLooks,
                                           kGameplay, kUnknown,  kLocal};
std::uint8_t Primary(std::uint8_t categories);
// "items", "vehicles", "characters", "looks", "gameplay", "unknown", "local".
std::string_view CategoryName(std::uint8_t category);

// Where an entry of a game's mods lives.
enum class Source : std::uint8_t {
  Red4ext, // red4ext/plugins/NAME/
  Cet,     // bin/x64/plugins/cyber_engine_tweaks/mods/NAME/
  Script,  // r6/scripts/NAME (a folder or a .reds file)
  Tweak,   // r6/tweaks/NAME (a folder or a .yaml, .yml, .tweak file)
  Archive, // archive/pc/mod/NAME.archive, and its ArchiveXL NAME.xl / NAME.archive.xl
  Redmod,  // mods/NAME/
};
// One file of an entry: its path relative to the entry, and its text when
// it was read ("" otherwise: then only the name tells).
struct File {
  std::string path;
  std::string text;
};
// What one entry adds, from its files: their contents when read, else
// their names.
std::uint8_t Classify(Source source, std::string_view name, std::span<const File> files);

// The name two games compare a mod by: lower case, without its extensions
// (.archive, .xl, .reds...) or a load-order prefix ("##", "zz-"). The
// entries of one mod share it ("Quickhack_Fixes.archive", its .xl, its
// scripts and tweaks folders).
std::string Key(std::string_view entry);

struct Mod {
  std::string name;          // as installed, of its first entry ("Quickhack_Fixes")
  std::uint64_t hash{};      // NameHash(Key): the same mod in every game, any version
  std::uint8_t categories{}; // k*
  friend bool operator==(const Mod &, const Mod &) = default;
};
struct Entry {
  Source source{};
  std::string name; // the file or folder's name
  std::uint8_t categories{};
};
// Entries grouped into mods by Key, sorted by hash.
std::vector<Mod> Group(std::span<const Entry> entries);

// A game folder's other mods, tandem's own left out. Reads the text of
// script, tweak, ArchiveXL and CET files (small ones, a bounded total).
std::vector<Entry> ScanEntries(const std::filesystem::path &game);
std::vector<Mod> Scan(const std::filesystem::path &game);
// A game's mods as a list of file names, without their contents: one
// "source<TAB>entry<TAB>file" line per file ("red4ext", "cet", "script",
// "tweak", "archive", "redmod"; "#" starts a comment), sorted by names
// alone (tests/data/mods-sample.tsv).
std::vector<Entry> EntriesOfList(std::string_view list);

// For the wire (names left out) and back.
std::vector<proto::ModEntry> ToWire(std::span<const Mod> mods, bool names = false);

// ---- Two games' mods ----

struct Counts {
  std::uint16_t total{};
  std::array<std::uint16_t, kCategoryOrder.size()> by{}; // per kCategoryOrder, by Primary
  friend bool operator==(const Counts &, const Counts &) = default;
};
struct Comparison {
  Counts theirsOnly; // mods they have and we do not
  Counts oursOnly;
  std::uint16_t shared{};
  std::vector<std::uint64_t> theirsOnlyHashes; // to ask their names
  bool Differ() const { return theirsOnly.total > 0 || oursOnly.total > 0; }
};
Comparison Compare(std::span<const proto::ModEntry> ours, std::span<const proto::ModEntry> theirs);
// In words, for the log and tests: "Judy has mods you don't (3): 2 with new
// items, 1 changing gameplay" (the game words it in Text.reds, both
// languages, from the counts).
std::string Describe(const Comparison &comparison, std::string_view who);

// ---- Records from another game ----

// What a record from another game is, which decides its vanilla stand-ins.
enum class Kind : std::uint8_t {
  Weapon,    // what a V holds (Held)
  Clothing,  // a piece of a V's clothes (Appearance::items)
  LookPiece, // a part of a V's body or face (Appearance::look: a resource)
  Vehicle,   // a V's car (Seat), a shared world vehicle, a garage car
  Npc,       // an NPC's character record (NpcSpawn)
  NpcLook,   // an NPC's appearance name
  Item,      // an item given, dropped, traded or rewarded: never replaced
  Effect,    // a status effect on an NPC
  Radio,     // a car radio's station
};
inline constexpr std::size_t kKindCount = 9;
// "weapon", "clothing"... (the log, Text.reds keys "Mods-Kind-<name>").
std::string_view KindName(Kind kind);

enum class Outcome : std::uint8_t {
  Same,    // this game has it
  Similar, // a vanilla record of the same type (the hint)
  Default, // the kind's fixed vanilla default
  Skipped, // nothing: shown as nothing, or refused (Item)
};
struct Resolved {
  std::uint64_t record{}; // what to use here (0: nothing)
  Outcome outcome{};
  friend bool operator==(const Resolved &, const Resolved &) = default;
};

// Does this game have the record (a TweakDB record, a resource, a station)?
using Known = std::function<bool(Kind, std::uint64_t)>;

// The vanilla stand-ins for a record of `kind` with `hint`, best first, by
// name (the log's), with their ids. None: the kind has no stand-in.
struct Candidate {
  std::uint64_t record{};
  std::string_view name;
  Outcome outcome{};
};
std::vector<Candidate> Candidates(Kind kind, const proto::RecordHint &hint);
// The name of a vanilla record tandem knows (a stand-in), "" for another.
std::string_view NameOf(std::uint64_t record);

// The first of: the record, its similar stand-ins, its default, nothing.
Resolved Resolve(Kind kind, std::uint64_t record, const proto::RecordHint &hint,
                 const Known &known);

// One replacement this session, for the list on demand.
struct Substitution {
  proto::PlayerId from{};
  std::string who;
  Kind kind{};
  std::uint64_t record{};
  Resolved to;
};
// What to tell our player: `count` records of one kind from one player are
// shown otherwise (first: the first notice of that kind from them).
struct Notice {
  proto::PlayerId from{};
  std::string who;
  Kind kind{};
  std::uint16_t count{};
  Outcome outcome{}; // the first one's (Skipped: shown as nothing)
};

// The session's resolver: Resolve memoized per record, each replacement
// logged once, kept, and told. Notices are grouped: the first replacement
// of a kind from a player waits kGatherUs for the others that come with it
// (a look's pieces), then one notice says how many; later ones of that kind
// from that player are only listed. Not thread-safe.
class Resolver {
public:
  static constexpr std::uint64_t kGatherUs = 2'000'000;

  explicit Resolver(Known known) : m_known(std::move(known)) {}

  // What to use for `record` (of `kind`, with `hint`) from player `from`
  // named `who`, at `nowUs` (any steady clock). 0 stays 0, never noticed.
  Resolved Resolve(Kind kind, std::uint64_t record, const proto::RecordHint &hint,
                   proto::PlayerId from, std::string_view who, std::uint64_t nowUs);
  // The same with the game's checks made by the caller, outside the lock it
  // keeps this resolver under: `decided` is mods::Resolve(kind, record, hint,
  // known). The memo's answer wins over it when there is one (another thread
  // resolved the record meanwhile). The known callback is not called.
  Resolved Resolve(Kind kind, std::uint64_t record, const proto::RecordHint &hint,
                   proto::PlayerId from, std::string_view who, std::uint64_t nowUs,
                   const Resolved &decided);
  // `record` (of `kind`, with `hint`) has its answer already: Resolve makes no check.
  bool Memoized(Kind kind, std::uint64_t record, const proto::RecordHint &hint) const;
  // A replacement decided elsewhere (a check only the game can make, such
  // as an NPC's appearance): kept and told like the others.
  void Note(Kind kind, std::uint64_t record, Resolved to, proto::PlayerId from,
            std::string_view who, std::uint64_t nowUs);

  // Lines for the log, once per replaced record:
  // "mods: unknown weapon 0x0000001a2b3c4d5e from Judy, shown as Items.Preset_Lexington_Default".
  std::vector<std::string> TakeLog();
  // Notices due at `nowUs`.
  std::vector<Notice> TakeNotices(std::uint64_t nowUs);
  // For the other side (proto::Substitutes): per player, the kinds first
  // replaced since the last call, and how many of theirs so far.
  std::vector<proto::Substitutes> TakeTold();

  const std::vector<Substitution> &List() const { return m_list; }
  // A new session.
  void Clear();

private:
  struct Pending {
    std::string who;
    std::uint64_t sinceUs{};
    std::uint16_t count{};
    Outcome outcome{};
    bool told{};
  };
  using MemoKey = std::tuple<Kind, std::uint64_t, std::uint64_t, std::uint8_t>;
  void Add(Kind kind, std::uint64_t record, Resolved to, proto::PlayerId from, std::string_view who,
           std::uint64_t nowUs);

  Known m_known;
  std::map<MemoKey, Resolved> m_memo;
  // Per (player, kind): the records already listed, and the notice state.
  std::map<std::tuple<proto::PlayerId, Kind, std::uint64_t>, bool> m_seen;
  std::map<std::pair<proto::PlayerId, Kind>, Pending> m_pending;
  std::map<proto::PlayerId, proto::Substitutes> m_told;
  std::map<proto::PlayerId, std::uint16_t> m_toldSent; // the kinds already told
  std::vector<Substitution> m_list;
  std::vector<std::string> m_log;
};
} // namespace tandem::mods
