#pragma once

// tandem-report: one zip to attach to a bug report, made by the co-op
// menu ("Write a bug report"), `tools/game.sh cmd coop report`, or
// tandem-report.exe when the game does not start.
//
// What goes in, and nothing else (dist/INSTALL.md lists it for players):
//   versions.txt        tandem's build, the game's and the needed mods'
//                       versions, tandem's file hashes, the number of other
//                       mods and their hash (no names), the startup checks
//   tandem.log          the newest red4ext/logs/tandem-*.log (its last 2 MB)
//   tandem.previous.log the one before (the session that crashed)
//   red4ext.log         the newest red4ext/logs/red4ext-*.log, its last
//                       200 lines (was the plugin loaded, and why not)
//   redscript.log       r6/logs/redscript_rCURRENT.log, its last 200 lines
//   tandem.ini          without relay_token and join
//   last-session.txt    the last session's summary (plugin: session_summary)
// Every text is passed through `Redact`: the game's folder becomes <game>,
// a user folder's name <user>, an IPv4 address keeps its first half.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace tandem::report {
// The game's folder, user names in paths and IPv4 addresses taken out.
std::string Redact(std::string text, const std::filesystem::path &game);
// tandem.ini without its secrets (relay_token, join), then redacted.
std::string RedactIni(std::string_view ini, const std::filesystem::path &game);
// The last `lines` lines of a text (all of it when shorter).
std::string TailLines(std::string_view text, std::size_t lines);

// A zip with stored (uncompressed) files: every unzip reads it.
struct Entry {
  std::string name;
  std::string data;
};
std::vector<std::byte> Zip(const std::vector<Entry> &entries);

struct Options {
  std::filesystem::path game; // the game's folder
  std::filesystem::path out;  // where the zip goes (a folder, created)
  std::string versions;       // versions.txt, from the caller
  std::string stamp;          // in the file's name; "": the time now
};
struct Result {
  std::filesystem::path zip;         // empty: failed
  std::vector<std::string> included; // the entries' names
  std::string error;
};
Result Write(const Options &options);
} // namespace tandem::report
