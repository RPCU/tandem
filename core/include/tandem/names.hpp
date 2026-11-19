#pragma once

// Players' names (docs/architecture/presence.md, "Names"): the pseudo a
// player types, what a host shows for the name a guest sent, and names made
// unique in a session.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tandem::names {

// A name is 1 to 16 characters (Unicode code points, not bytes).
inline constexpr std::size_t kMaxLength = 16;
// tandem.ini's default: a player who never chose one.
inline constexpr std::string_view kDefault = "V";

// The default, or nothing: the co-op menu asks for a name.
bool IsDefault(std::string_view name);

// Why a typed name is refused ("" when it is not): "empty", "too long",
// "not text" (not UTF-8), "a control character", "; or #" (tandem.ini's
// comment signs). Spaces around it are not counted.
std::string_view Problem(std::string_view typed);

// The typed name trimmed of spaces, or nothing when Problem refuses it.
std::optional<std::string> Clean(std::string_view typed);

// What a host shows for a name a guest sent, whatever it sent (never
// refused): trimmed, invalid bytes and control characters dropped, ; and #
// too, cut to kMaxLength characters; nothing left is the default.
std::string Sanitize(std::string_view sent);

// Two names a reader takes for one: the same but for the case of ASCII
// letters ("v" and "V").
bool Same(std::string_view a, std::string_view b);

// `wanted` when nobody in `taken` has it (Same), else the first free of
// "wanted (2)", "wanted (3)"...: what a host shows for a player joining
// under a name already in the session.
std::string Unique(std::string_view wanted, const std::vector<std::string> &taken);

// The number of characters (code points) of UTF-8 text.
std::size_t Length(std::string_view text);
} // namespace tandem::names
