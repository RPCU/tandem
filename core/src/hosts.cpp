#include "tandem/hosts.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>

namespace tandem::hosts {
namespace {
std::string_view Trim(std::string_view text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos)
    return {};
  const auto last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

bool HostNameChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_';
}
} // namespace

std::optional<Target> Parse(std::string_view text, std::uint16_t defaultPort) {
  text = Trim(text);
  Target target{.port = defaultPort};
  if (const auto colon = text.rfind(':'); colon != std::string_view::npos) {
    const auto port = text.substr(colon + 1);
    std::uint16_t value{};
    const auto [end, ec] = std::from_chars(port.data(), port.data() + port.size(), value);
    if (port.empty() || ec != std::errc{} || end != port.data() + port.size() || value == 0)
      return std::nullopt;
    target.port = value;
    text = text.substr(0, colon);
  }
  if (text.empty() || target.port == 0 || text.size() > 253 ||
      !std::ranges::all_of(text, HostNameChar))
    return std::nullopt;
  target.address.reserve(text.size());
  for (const char c : text)
    target.address.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return target;
}

std::string Format(const Target &target) {
  return std::format("{}:{}", target.address, target.port);
}

std::vector<Recent> ParseRecent(std::string_view text) {
  std::vector<Recent> out;
  while (!text.empty() && out.size() < kMaxRecent) {
    const auto end = text.find('\n');
    auto line = Trim(text.substr(0, end));
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    const auto space = line.find(' ');
    const auto where = line.substr(0, space);
    // A line without a port is not one we wrote.
    if (where.find(':') == std::string_view::npos)
      continue;
    auto target = Parse(where, 0);
    if (!target || std::ranges::any_of(out, [&](const Recent &r) { return r.target == *target; }))
      continue;
    out.push_back({*target, space == std::string_view::npos
                                ? std::string{}
                                : std::string(Trim(line.substr(space)))});
  }
  return out;
}

std::string FormatRecent(const std::vector<Recent> &list) {
  std::string out;
  for (const auto &recent : list) {
    out += Format(recent.target);
    if (!recent.name.empty())
      out += ' ' + recent.name;
    out += '\n';
  }
  return out;
}

void Remember(std::vector<Recent> &list, const Target &target, std::string name) {
  // A name is one line.
  std::ranges::replace(name, '\n', ' ');
  std::ranges::replace(name, '\r', ' ');
  const auto old = std::ranges::find_if(list, [&](const Recent &r) { return r.target == target; });
  if (old != list.end()) {
    if (name.empty())
      name = old->name;
    list.erase(old);
  }
  list.insert(list.begin(), Recent{target, std::move(name)});
  if (list.size() > kMaxRecent)
    list.resize(kMaxRecent);
}
} // namespace tandem::hosts
