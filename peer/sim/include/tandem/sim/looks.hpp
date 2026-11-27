#pragma once

// A mock V's look: the body keys of a real V, kept on disk, and random
// clothes. Shared by tandem-peer's modes and the simulated game (sim.md).

#include "tandem/protocol.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tandem::sim {
// The body keys of a real V (face, body, arms: the game's appearance keys,
// a resource path hash and a name hash each), one file per body type:
// "RESOURCE DEFINITION" in hex, one key a line. Saved from every look heard
// with keys (a game's own V: 15 of them), so a random look wears a real
// body; a file may also be written by hand.
//
// The directory: `option`, else $XDG_CACHE_HOME/tandem/looks, else
// ~/.cache/tandem/looks.
std::filesystem::path LookKeysDir(const std::string &option);
std::filesystem::path LookKeysFile(const std::filesystem::path &dir, proto::Body body);
// Empty when the file is missing or unreadable.
std::vector<proto::AppearanceKey> LoadLookKeys(const std::filesystem::path &file);

// The body to wear when none is asked for: male when male.keys holds a real
// V's keys, else female when female.keys does, else none (the caller's own
// default; the game then puts a stand-in body on the puppet).
std::optional<proto::Body> SavedBody(const std::filesystem::path &dir);

// A look of our own: a real V body of ours when its keys are known (else
// none: the game puts a stand-in body on the puppet) and one random piece
// per slot from clothing records the game's own scripts name (their
// crafting specs end in _Crafting; the item is the same name without it),
// then `extra` (pieces a mod would add, with their hint). `picked`: the
// pieces' names, for a log line.
proto::Appearance
RandomLook(proto::Body body, unsigned seed, std::vector<proto::AppearanceKey> keys,
           const std::vector<std::pair<std::string, proto::RecordHint>> &extra = {},
           std::string *picked = nullptr);
} // namespace tandem::sim
