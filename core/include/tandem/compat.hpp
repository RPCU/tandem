#pragma once

// Do two installations fit together, and does this one fit its game?
//
// At startup the plugin gathers what is installed (the game's version, the
// mods tandem needs, tandem's own files) and `Check` says what is wrong in
// words for the player: a problem turns co-op off, a warning only tells.
// At a join each side sends its `proto::Install`; `Differences` compares
// the two and words what differs. Nothing here blocks a session: the
// protocol version (endpoint.cpp) is the only hard gate between players.
// docs/architecture/release.md has the table.

#include "tandem/protocol.hpp"

#include <compare>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tandem::compat {
// A file version (the game's executable, a mod's DLL) or a release number.
struct Version {
  std::uint16_t a{}, b{}, c{}, d{}; // a.b.c.d (major and minor are macros on Linux)
  friend auto operator<=>(const Version &, const Version &) = default;
};
// "1.21.2", "v1.21.2", "3.0.88.46235"; missing parts are 0.
std::optional<Version> ParseVersion(std::string_view text);
// Three parts, four when the last is not 0.
std::string Format(const Version &version);

// This build of tandem: the release and the commit, "0.2.0+2fa03ca" (the
// release alone when it was built outside git).
const char *Build();
const char *Release(); // "0.2.0"
// The same build: equal, or one has no commit and the releases are equal.
bool SameBuild(std::string_view a, std::string_view b);

// What this build was made against (docs/architecture/release.md): the
// game's executable (patch 2.50), and the lowest version of each mod.
inline constexpr Version kGame{3, 0, 88, 46235};
inline constexpr std::string_view kGamePatch = "2.50";
struct Requirement {
  std::string_view name;
  std::string_view dll; // from the game's folder
  Version minimum;
  bool needed; // false: named in reports only
};
inline constexpr Requirement kRequirements[] = {
    {"RED4ext", "red4ext/RED4ext.dll", {1, 31, 0, 0}, true},
    {"Codeware", "red4ext/plugins/Codeware/Codeware.dll", {1, 21, 2, 0}, true},
    {"TweakXL", "red4ext/plugins/TweakXL/TweakXL.dll", {1, 12, 0, 0}, true},
    {"ArchiveXL", "red4ext/plugins/ArchiveXL/ArchiveXL.dll", {1, 28, 6, 0}, false},
};
// redscript has no version to read in the game's folder: its compiler.
inline constexpr std::string_view kRedscript = "engine/tools/scc.exe";
inline constexpr std::string_view kRedscriptVersion = "0.5.32";

// What the plugin found (plugin/src/installed.cpp reads the file versions).
struct Found {
  std::string name;
  bool present{};
  std::optional<Version> version; // none: the file says nothing
};
struct Facts {
  std::optional<Version> game;
  std::vector<Found> mods; // one per kRequirements entry found or not
  bool redscript{true};
  bool scripts{true}; // r6/scripts/tandem has files
  bool tweaks{true};  // r6/tweaks/tandem has files
  // archive/pc/ep1 has files: Phantom Liberty's data is on the disk. Only
  // reported: whether it is owned and enabled is the game's to say (Expansion).
  bool expansionFiles{};
};
struct Startup {
  // Co-op stays off, with these words for the player.
  std::vector<std::string> problems;
  // Said, nothing more.
  std::vector<std::string> warnings;
  bool Blocked() const { return !problems.empty(); }
};
Startup Check(const Facts &facts);

// The files under `folder` as one number: their names (relative, lower
// case) and contents (carriage returns left out, so a checkout with other
// line endings is the same). 0: no file there.
std::uint64_t HashTree(const std::filesystem::path &folder);

// The other mods of a game, by name only: RED4ext plugins, archives,
// script and tweak folders, CET and REDmod mods. tandem's own are left out.
struct Mods {
  std::vector<std::string> names; // "red4ext:codeware", sorted
  std::uint64_t hash{};           // 0: none
};
Mods InstalledMods(const std::filesystem::path &game);

// This installation, for the other players (`game`: the game's folder; ""
// leaves the files unknown, as for the mock player).
proto::Install LocalInstall(const std::filesystem::path &game, std::optional<Version> gameVersion);

// What differs between two games, one sentence each for our player; `who`
// is the other player's name. Empty: nothing known differs.
std::vector<std::string> Differences(const proto::Install &ours, const proto::Install &theirs,
                                     std::string_view who);

// Phantom Liberty, as the game itself says once its menus run (scripts:
// inkISystemRequestsHandler.IsAdditionalContentEnabled / Owned of EP1).
// tandem needs it enabled on every player's game: a V carries items, perks
// of the Relic tree and vehicles whose records only exist with it, and a
// game without them cannot show or apply what the other one sends.
enum class Expansion : std::int32_t {
  Unknown = 0,  // the game has not said yet (before its first menu)
  Enabled = 1,  // owned, installed and enabled
  Disabled = 2, // owned, but not installed or turned off
  Missing = 3,  // not owned
};
// Why co-op is off for it, for the player; "" when it is not.
std::string ExpansionProblem(Expansion state);

// A guest whose protocol version is not the host's: what the guest reads
// (after "refused: ") and what the host is told.
std::string RefusalForGuest(std::uint16_t theirProtocol);
std::string RefusalForHost(std::string_view who, std::uint16_t theirProtocol,
                           std::string_view theirBuild);
// A join refused for Phantom Liberty: what the guest reads when its game
// lacks it, or the host's does, and what the host is told.
inline constexpr std::string_view kExpansionGuestLacks =
    "your game has no Phantom Liberty: tandem needs the expansion on every player's game";
inline constexpr std::string_view kExpansionHostLacks =
    "the host's game has no Phantom Liberty: tandem needs the expansion on every player's game";
std::string ExpansionRefusalForHost(std::string_view who, bool oursLacks);
// True for a refusal trying again cannot cure (the words above).
bool IsFinalRefusal(std::string_view reason);
} // namespace tandem::compat
