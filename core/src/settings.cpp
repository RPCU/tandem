#include "tandem/settings.hpp"

#include <array>

namespace tandem::settings {
namespace {
std::string_view Trim(std::string_view text) {
  const auto first = text.find_first_not_of(" \t\r");
  if (first == std::string_view::npos)
    return {};
  const auto last = text.find_last_not_of(" \t\r");
  return text.substr(first, last - first + 1);
}
} // namespace

bool IsEditable(std::string_view key) {
  // (code_salt: not on the page, saved by its "new code" action;
  // last_address: by the co-op menu's Join by address.)
  static constexpr std::array<std::string_view, 20> kKeys{
      "name",    "recorder",     "friendly_fire", "enemy_scaling", "notices",
      "markers", "nameplates",   "party_hud",     "relay",         "relay_token",
      "story",   "quest_freeze", "scene_privacy", "time_host",     "direct",
      "code",    "code_salt",    "link",          "pings",         "last_address"};
  for (const auto k : kKeys)
    if (k == key)
      return true;
  return false;
}

std::optional<std::string> SetKey(std::string_view text, std::string_view key,
                                  std::string_view value) {
  if (key.empty() || value.find_first_of("\r\n;#") != std::string_view::npos ||
      key.find_first_of("\r\n;#= \t") != std::string_view::npos)
    return std::nullopt;
  const std::string line = std::string(key) + " = " + std::string(Trim(value));
  std::string out;
  out.reserve(text.size() + line.size() + 2);
  bool done = false;
  std::string_view ending = "\n";
  for (std::size_t start = 0; start < text.size();) {
    auto end = text.find('\n', start);
    const bool last = end == std::string_view::npos;
    auto row = text.substr(start, last ? std::string_view::npos : end - start);
    const bool crlf = !row.empty() && row.back() == '\r';
    if (crlf) {
      row.remove_suffix(1);
      ending = "\r\n";
    }
    const auto comment = row.find_first_of(";#");
    const auto code = row.substr(0, comment);
    const auto equals = code.find('=');
    if (!done && equals != std::string_view::npos && Trim(code.substr(0, equals)) == key) {
      out += line;
      if (comment != std::string_view::npos)
        out.append(" ").append(row.substr(comment));
      done = true;
    } else {
      out += row;
    }
    if (!last)
      out += crlf ? "\r\n" : "\n";
    start = last ? text.size() : end + 1;
  }
  if (!done) {
    if (!out.empty() && out.back() != '\n')
      out += ending;
    out += line;
    out += ending;
  }
  return out;
}
} // namespace tandem::settings
