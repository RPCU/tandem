#pragma once

// The settings page (scripts/tandem/Coop.reds, docs/architecture/coop-menu.md)
// changes tandem.ini one key at a time: the player's other keys, comments and
// layout stay as they were.

#include <optional>
#include <string>
#include <string_view>

namespace tandem::settings {
// The keys the page may change. Never the session's (role, address, port,
// join), which the co-op menu chooses, nor the test harness's.
bool IsEditable(std::string_view key);

// `text` (an ini file) with `key = value`: the first line that sets the key
// is rewritten (a comment after its value kept, its line ending too), else
// the line is appended. nullopt: a value the file cannot hold (a line break,
// or `;` and `#`, which start a comment).
std::optional<std::string> SetKey(std::string_view text, std::string_view key,
                                  std::string_view value);
} // namespace tandem::settings
