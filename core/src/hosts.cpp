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

// A code entry for this host name (any case).
bool SameName(const Recent &recent, std::string_view name) {
  return !recent.code.empty() && recent.name.size() == name.size() &&
         std::ranges::equal(recent.name, name, [](char a, char b) {
           return std::tolower(static_cast<unsigned char>(a)) ==
                  std::tolower(static_cast<unsigned char>(b));
         });
}

bool HostNameChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
}

bool Digits(std::string_view text) {
  return !text.empty() && std::ranges::all_of(text, [](char c) {
    return std::isdigit(static_cast<unsigned char>(c));
  });
}

bool Ipv6Char(char c) {
  return std::isxdigit(static_cast<unsigned char>(c)) || c == ':' || c == '.';
}

// A port's digits, 1 to 65535.
std::optional<std::uint16_t> PortOf(std::string_view text) {
  std::uint16_t value{};
  const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (!Digits(text) || ec != std::errc{} || end != text.data() + text.size() || value == 0)
    return std::nullopt;
  return value;
}

// A host name, or a dotted IPv4 address when every label is digits.
bool ValidName(std::string_view name) {
  if (name.empty() || name.size() > 253)
    return false;
  std::vector<std::string_view> labels;
  for (std::size_t start = 0;;) {
    const auto dot = name.find('.', start);
    const auto label = name.substr(start, dot == std::string_view::npos ? dot : dot - start);
    if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-' ||
        !std::ranges::all_of(label, HostNameChar))
      return false;
    labels.push_back(label);
    if (dot == std::string_view::npos)
      break;
    start = dot + 1;
  }
  if (!std::ranges::all_of(labels, Digits))
    return true;
  return labels.size() == 4 && std::ranges::all_of(labels, [](std::string_view label) {
           int value{};
           std::from_chars(label.data(), label.data() + label.size(), value);
           return label.size() <= 3 && value <= 255;
         });
}
} // namespace

std::string_view ProblemWord(Problem problem) {
  switch (problem) {
  case Problem::None:
    return "";
  case Problem::Empty:
    return "empty";
  case Problem::Port:
    return "port";
  case Problem::Name:
    return "name";
  case Problem::Ipv6:
    return "ipv6";
  }
  return "name";
}

namespace {
Checked Refused(Problem problem) {
  Checked checked;
  checked.problem = problem;
  return checked;
}
} // namespace

Checked Check(std::string_view text, std::uint16_t defaultPort) {
  text = Trim(text);
  if (text.empty())
    return Refused(Problem::Empty);
  Target target{.port = defaultPort};
  if (text.front() == '[') {
    // "[IPv6]" or "[IPv6]:port": its shape checked, then refused.
    const auto close = text.find(']');
    if (close == std::string_view::npos)
      return Refused(Problem::Name);
    const auto inside = text.substr(1, close - 1);
    const auto rest = text.substr(close + 1);
    if (!rest.empty() && (rest.front() != ':' || !PortOf(rest.substr(1))))
      return Refused(Problem::Port);
    if (inside.find(':') == std::string_view::npos || !std::ranges::all_of(inside, Ipv6Char))
      return Refused(Problem::Name);
    return Refused(Problem::Ipv6);
  }
  if (std::ranges::count(text, ':') > 1)
    return Refused(std::ranges::all_of(text, Ipv6Char) ? Problem::Ipv6 : Problem::Name);
  if (const auto colon = text.find(':'); colon != std::string_view::npos) {
    const auto port = PortOf(text.substr(colon + 1));
    if (!port)
      return Refused(Problem::Port);
    target.port = *port;
    text = text.substr(0, colon);
  }
  if (!ValidName(text))
    return Refused(Problem::Name);
  if (target.port == 0)
    return Refused(Problem::Port);
  target.address.reserve(text.size());
  for (const char c : text)
    target.address.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  Checked checked;
  checked.target = std::move(target);
  return checked;
}

std::optional<Target> Parse(std::string_view text, std::uint16_t defaultPort) {
  return Check(text, defaultPort).target;
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
    std::string name =
        space == std::string_view::npos ? std::string{} : std::string(Trim(line.substr(space)));
    if (where.starts_with("code:")) {
      // A join code: six letters and digits, known by the host's name.
      const auto code = where.substr(5);
      const bool valid = code.size() == 6 && std::ranges::all_of(code, [](char c) {
                           return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
                         });
      if (valid && !name.empty() &&
          std::ranges::none_of(out, [&](const Recent &r) { return SameName(r, name); }))
        out.push_back({{}, std::move(name), std::string(code)});
      continue;
    }
    // A line without a port is not one we wrote.
    if (where.find(':') == std::string_view::npos)
      continue;
    auto target = Parse(where, 0);
    if (!target || std::ranges::any_of(
                       out, [&](const Recent &r) { return r.code.empty() && r.target == *target; }))
      continue;
    out.push_back({*target, std::move(name), {}});
  }
  return out;
}

std::string FormatRecent(const std::vector<Recent> &list) {
  std::string out;
  for (const auto &recent : list) {
    out += recent.code.empty() ? Format(recent.target) : "code:" + recent.code;
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
  const auto old = std::ranges::find_if(
      list, [&](const Recent &r) { return r.code.empty() && r.target == target; });
  if (old != list.end()) {
    if (name.empty())
      name = old->name;
    list.erase(old);
  }
  list.insert(list.begin(), Recent{target, std::move(name), {}});
  if (list.size() > kMaxRecent)
    list.resize(kMaxRecent);
}

void RememberCode(std::vector<Recent> &list, std::string code, std::string name) {
  std::ranges::replace(name, '\n', ' ');
  std::ranges::replace(name, '\r', ' ');
  if (name.empty() || code.empty())
    return;
  std::erase_if(list, [&](const Recent &r) { return SameName(r, name); });
  list.insert(list.begin(), Recent{{}, std::move(name), std::move(code)});
  if (list.size() > kMaxRecent)
    list.resize(kMaxRecent);
}
} // namespace tandem::hosts
