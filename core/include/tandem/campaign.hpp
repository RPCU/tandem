#pragma once

// A shared campaign (docs/architecture/campaign.md): a group shares one
// campaign save. Whoever hosts it plays its V; the others join with their
// own V as guests; anyone may also play it solo. Saves cannot be merged, so
// each copy of the campaign carries its lineage, like a commit: the
// campaign, this copy, its generation, the copies it came from, when it was
// last saved, who played it last and the last quest done in it.
//
// Where it lives:
//   - in the save (a persistent script field, Campaign.reds): Pack's words,
//     the campaign, the copy, its generation and its ancestors. A save and
//     its identity are written by the same save and lost by the same load;
//   - in tandem's own folder (red4ext/plugins/tandem/campaigns/lineage/),
//     one small text file per copy (Record): the words, plus who, when, the
//     last quest, the save slot of its newest save, and the players'
//     choices. Never written beside the game's own saves.
//
// Moving on: every load of a campaign save starts a new copy, a child of the
// loaded one (Advance): a co-op session or a solo play is one generation. An
// export ends a copy too (the running game goes on as a child of the one
// exported), so a copy that left this machine never moves on here.
//
// Transfer, this step: by hand. Export writes the newest save of the copy
// and its record to one zip (stored files, report::Zip) in tandem's
// campaigns folder; Import installs one as a new manual save slot, never
// over an existing one. The seam for a transfer through the session is
// Archive (bytes in, bytes out): a later Bulk-channel stream needs only
// MakeArchive / ReadArchive and Install.
//
// No game needed: everything here is tested on Linux.

#include "tandem/mods.hpp"
#include "tandem/protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tandem::campaign {
inline constexpr std::size_t kMaxAncestors = proto::kMaxCampaignAncestors;
inline constexpr std::size_t kMaxText = proto::kMaxCampaignText;
// The words of a save's identity: campaign, copy, generation, ancestors.
inline constexpr std::size_t kMaxWords = 3 + kMaxAncestors;
// An archive's limits: a save is some megabytes (sav.dat, its metadata and
// its screenshot).
inline constexpr std::uintmax_t kMaxArchiveBytes = 512ull << 20;
inline constexpr std::size_t kMaxArchiveFiles = 16;
// The archive's own entries.
inline constexpr std::string_view kRecordEntry = "tandem-campaign.txt";
inline constexpr std::string_view kSaveEntryPrefix = "save/";
inline constexpr std::string_view kSaveFile = "sav.dat";
// The game's manual save slots: "ManualSave-" and a number, the next one
// taken by the game itself (inkISystemRequestsHandler.ManualSave("ManualSave-")).
inline constexpr std::string_view kManualPrefix = "ManualSave-";

// One copy of a campaign.
struct Copy {
  std::uint64_t campaign{}; // 0: not a campaign
  std::uint64_t copy{};
  std::uint32_t generation{};           // 0: the copy the campaign started with
  std::vector<std::uint64_t> ancestors; // parent first, at most kMaxAncestors
  std::uint64_t savedUnix{};            // its newest save (UTC seconds; 0: none yet)
  std::string who;                      // who played it last
  std::string quest;                    // the last quest done in it
  bool Is() const { return campaign != 0; }
  friend bool operator==(const Copy &, const Copy &) = default;
};

// A path as UTF-8, and back (the scripts' strings; never the system's code
// page, which cannot hold every name).
std::string Utf8(const std::filesystem::path &path);
std::filesystem::path PathOf(std::string_view utf8);

// A text as a copy keeps it: one line, at most kMaxText bytes (cut at a
// character boundary), no control characters.
std::string Clean(std::string_view text);

// A new campaign, its first copy.
Copy Start(std::uint64_t campaign, std::uint64_t copy, std::string_view who, std::uint64_t nowUnix);
// The copy that goes on from `from`: generation + 1, `from` first among its
// ancestors (the oldest dropped past kMaxAncestors). Who, when and the
// quest are `from`'s until it is saved.
Copy Advance(const Copy &from, std::uint64_t copy);

// The save's words (Campaign.reds keeps them in a persistent field).
std::vector<std::uint64_t> Pack(const Copy &copy);
// Their copy (who, when and the quest are the record's); none: no
// campaign, or words that do not make one.
std::optional<Copy> Unpack(std::span<const std::uint64_t> words);

proto::Campaign ToWire(const Copy &copy);
Copy FromWire(const proto::Campaign &wire);

// How `theirs` stands to `ours`.
enum class Relation : std::uint8_t {
  None = 0,      // one of them is not a campaign copy
  Unrelated = 1, // copies of two campaigns
  Same = 2,      // the same copy, saved at the same time
  Ahead = 3,     // ours went on from theirs (theirs is older)
  Behind = 4,    // theirs went on from ours: a fast-forward to theirs
  Fork = 5,      // each went on separately from a common copy
};
Relation Relate(const Copy &ours, const Copy &theirs);
std::string_view NameOf(Relation relation);
// The newest copy both came from (0: none known: past kMaxAncestors, or
// one is the other's ancestor... then that one).
std::uint64_t CommonAncestor(const Copy &a, const Copy &b);

// The group's copies of the host's campaign, at a join.
struct Member {
  proto::PlayerId player{};
  std::string name;
  Copy copy;
  std::uint64_t chosen{}; // the copy it chose to go on from (0: none)
};
struct Verdict {
  enum class Kind : std::uint8_t {
    None = 0,        // the host plays no campaign, or nobody else has a copy of it
    Same = 1,        // everyone who has one has the same copy
    FastForward = 2, // one copy is ahead of all the others
    Fork = 3,        // several copies went on separately
  };
  Kind kind{};
  // The copies nobody is ahead of (one per copy, in members' order): the
  // one to go on from (FastForward), or the choice (Fork).
  std::vector<Member> heads;
  bool selfBehind{}; // our copy is behind a head (or, in a fork, not a head)
  bool hostBehind{}; // the host is about to play an older copy than someone has
  friend bool operator==(const Verdict &, const Verdict &) = default;
};
// `members` holds the host's copy and everyone else's (ours among them);
// copies of other campaigns are left out.
Verdict Judge(std::span<const Member> members, proto::PlayerId self, proto::PlayerId host);
// Two verdicts tell the player the same (a repeat is not told again).
bool SameNews(const Verdict &a, const Verdict &b);

// ---- The record of a copy, in tandem's own folder ----

struct Record {
  Copy copy;
  std::string slot;            // the save folder of its newest save ("ManualSave-12"; "": unknown)
  bool foreign{};              // another player's copy, heard in a session (no save of it here)
  bool setAside{};             // a copy the players chose not to go on from: a normal save again
  std::uint64_t chosen{};      // (this campaign) the copy we chose to go on from
  std::vector<mods::Mod> mods; // the exporting game's other mods (an archive's record)
  friend bool operator==(const Record &, const Record &) = default;
};
std::string Write(const Record &record);
std::optional<Record> Read(std::string_view text);

// The records in a folder (lineage/), unreadable files skipped.
std::vector<Record> LoadRecords(const std::filesystem::path &folder);
// One record, written aside then renamed over its file
// (<campaign>-<copy>.txt). False: `error` says why.
bool SaveRecord(const std::filesystem::path &folder, const Record &record, std::string &error);
// Our newest own copy of `campaign` (not foreign, not set aside): the
// newest save; none.
std::optional<Record> NewestOf(std::span<const Record> records, std::uint64_t campaign);
// Our copy's record, if we have one.
std::optional<Record> Find(std::span<const Record> records, std::uint64_t campaign,
                           std::uint64_t copy);
// A copy of `campaign` someone else has that is ahead of ours or forked from
// it, the newest (the host warned before it hosts an older copy).
std::optional<Record> NewerElsewhere(std::span<const Record> records, const Copy &ours);

// ---- Archives: one zip per exported copy ----

// The files of a save slot, by name ("sav.dat", "metadata.9.json"...).
struct File {
  std::string name;
  std::string data;
};
// An archive's bytes: the record first, then the save's files.
std::vector<std::byte> MakeArchive(const Record &record, std::span<const File> files);
struct Archive {
  Record record;
  std::vector<File> files;
};
// An archive read back, every entry checked (stored, its CRC, a plain file
// name under save/, sav.dat among them, the record readable). None: `error`.
std::optional<Archive> ReadArchive(std::span<const std::byte> bytes, std::string &error);
// Only the record (the zip's first entry), from the start of a file: for
// the import list, without reading the save.
std::optional<Record> PeekRecord(std::span<const std::byte> head);

// The zip's name for an export: campaign-WHO-YYYYMMDD-HHMM-gN.zip.
std::string ArchiveName(const Record &record, std::uint64_t nowUnix);

// The slot folders' names in a saves folder.
std::vector<std::string> Slots(const std::filesystem::path &saves);
// A new manual slot's name: one past the highest manual slot there (never a
// slot that exists; the game numbers its own the same way or by the first
// free one, and either misses ours).
std::string NextManualSlot(std::span<const std::string> slots);
// The slot whose sav.dat was written last, at `since` or later; "": none.
std::string NewestSlot(const std::filesystem::path &saves, std::filesystem::file_time_type since);

struct Exported {
  std::filesystem::path zip; // empty: failed
  std::string error;
};
// The slot's files and the record to `folder`/ArchiveName.
Exported Export(const std::filesystem::path &slot, Record record,
                const std::filesystem::path &folder, std::uint64_t nowUnix);

struct Listed {
  std::filesystem::path zip;
  Record record;
};
// The archives in `folder`, newest save first (files that are not ours skipped).
std::vector<Listed> ListArchives(const std::filesystem::path &folder);

// What our game lacks of the mods an archive's game had (their names), the
// mods that only change what one game shows left out.
std::vector<std::string> MissingMods(std::span<const mods::Mod> theirs,
                                     std::span<const mods::Mod> ours);

struct Installed {
  std::string slot; // "": failed
  Record record;    // as recorded here (slot set)
  std::string error;
};
// The archive as a new manual slot in `saves`: never over a slot that
// exists. Its metadata naming the slot it was exported from names the new
// one. The record is not saved here (the caller's registry).
Installed Install(const Archive &archive, const std::filesystem::path &saves);
// The same from a zip file.
Installed Import(const std::filesystem::path &zip, const std::filesystem::path &saves);
} // namespace tandem::campaign
