#include "tandem/campaigns.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <set>

namespace tandem::campaigns {
namespace {
std::string_view Trim(std::string_view text) {
  const auto first = text.find_first_not_of(" \t\r");
  if (first == std::string_view::npos)
    return {};
  const auto last = text.find_last_not_of(" \t\r");
  return text.substr(first, last - first + 1);
}

// The first word of `line`, and the rest (trimmed) in `rest`.
std::string_view Word(std::string_view line, std::string_view &rest) {
  const auto space = line.find(' ');
  rest = space == std::string_view::npos ? std::string_view{} : Trim(line.substr(space + 1));
  return line.substr(0, space);
}

// A playthrough id is one word of printable characters.
bool ValidId(std::string_view id) {
  return !id.empty() && id.size() <= 128 &&
         std::ranges::all_of(id, [](char c) { return c > ' ' && c < 127; });
}

// A host key is one line.
bool ValidHost(std::string_view host) {
  return !host.empty() && host.size() <= 128 &&
         std::ranges::none_of(host, [](char c) { return c == '\n' || c == '\r'; });
}

bool Matches(double a, double b) { return std::abs(a - b) <= kMatchSeconds; }

void AddCampaign(Book &book, const std::string &playthrough) {
  std::erase(book.campaigns, playthrough);
  book.campaigns.insert(book.campaigns.begin(), playthrough);
  if (book.campaigns.size() > kMaxCampaigns)
    book.campaigns.resize(kMaxCampaigns);
}

void AddGuest(Book &book, std::string_view host, const std::string &playthrough) {
  std::erase_if(book.guests, [&](const Guest &g) { return g.host == host; });
  book.guests.insert(book.guests.begin(), Guest{std::string(host), playthrough});
  if (book.guests.size() > kMaxGuests)
    book.guests.resize(kMaxGuests);
}

// Pending entries for every time of the world not noted yet. True: added.
bool AddPending(Book &book, const World &world, std::string_view host) {
  bool changed = false;
  for (const double seconds : world.seconds) {
    if (seconds < kMinSeconds)
      continue;
    const bool known = std::ranges::any_of(book.pending, [&](const Pending &p) {
      return p.host == host && Matches(p.seconds, seconds);
    });
    if (known)
      continue;
    book.pending.insert(book.pending.begin(), Pending{std::string(host), seconds});
    changed = true;
  }
  if (book.pending.size() > kMaxPending)
    book.pending.resize(kMaxPending);
  return changed;
}

// The newest of these saves: by the game's timestamp, else its index.
std::optional<Save> Newest(const std::vector<Save> &saves, auto &&wanted) {
  std::optional<Save> best;
  for (const auto &save : saves) {
    if (!wanted(save))
      continue;
    if (!best || save.stamp > best->stamp ||
        (save.stamp == best->stamp && save.index < best->index))
      best = save;
  }
  return best;
}
} // namespace

Book Parse(std::string_view text) {
  Book book;
  while (!text.empty()) {
    const auto end = text.find('\n');
    const auto line = Trim(text.substr(0, end));
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    std::string_view rest;
    const auto kind = Word(line, rest);
    if (kind == "campaign") {
      if (ValidId(rest) && !IsCampaign(book, rest) && book.campaigns.size() < kMaxCampaigns)
        book.campaigns.emplace_back(rest);
    } else if (kind == "guest") {
      std::string_view host;
      const auto id = Word(rest, host);
      const bool known =
          std::ranges::any_of(book.guests, [&](const Guest &g) { return g.host == host; });
      if (ValidId(id) && ValidHost(host) && !known && book.guests.size() < kMaxGuests)
        book.guests.push_back({std::string(host), std::string(id)});
    } else if (kind == "pending") {
      std::string_view host;
      const auto number = Word(rest, host);
      double seconds{};
      const auto [at, error] =
          std::from_chars(number.data(), number.data() + number.size(), seconds);
      if (error == std::errc{} && at == number.data() + number.size() && seconds >= 0 &&
          (host.empty() || ValidHost(host)) && book.pending.size() < kMaxPending)
        book.pending.push_back({std::string(host), seconds});
    }
  }
  return book;
}

std::string Format(const Book &book) {
  std::string out;
  for (const auto &id : book.campaigns)
    out += std::format("campaign {}\n", id);
  for (const auto &guest : book.guests)
    out += std::format("guest {} {}\n", guest.playthrough, guest.host);
  for (const auto &pending : book.pending)
    out += pending.host.empty() ? std::format("pending {:.1f}\n", pending.seconds)
                                : std::format("pending {:.1f} {}\n", pending.seconds, pending.host);
  return out;
}

std::string HostOfAddress(std::string_view address, std::uint16_t port) {
  std::string lower;
  for (const char c : address)
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return std::format("{}:{}", lower, port);
}

std::string HostOfName(std::string_view name) {
  std::string key = "name:";
  for (const char c : Trim(name)) {
    if (c == '\n' || c == '\r')
      continue;
    key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return key;
}

bool MarkCampaign(Book &book, const World &world) {
  if (!world.playthrough.empty()) {
    if (!book.campaigns.empty() && book.campaigns.front() == world.playthrough)
      return false;
    AddCampaign(book, world.playthrough);
    return true;
  }
  return AddPending(book, world, "");
}

bool JoinedWith(Book &book, const World &world, std::string_view host) {
  if (!ValidHost(host))
    return false;
  if (!world.playthrough.empty()) {
    if (!book.guests.empty() && book.guests.front().host == host &&
        book.guests.front().playthrough == world.playthrough)
      return false;
    AddGuest(book, host, world.playthrough);
    return true;
  }
  return AddPending(book, world, host);
}

bool Unmark(Book &book, const World &world) {
  bool changed = false;
  if (!world.playthrough.empty())
    changed = std::erase(book.campaigns, world.playthrough) > 0;
  changed |= std::erase_if(book.pending, [&](const Pending &p) {
               return p.host.empty() && std::ranges::any_of(world.seconds, [&](double s) {
                        return Matches(p.seconds, s);
                      });
             }) > 0;
  return changed;
}

bool IsCampaign(const Book &book, std::string_view playthrough) {
  return std::ranges::find(book.campaigns, playthrough) != book.campaigns.end();
}

bool Learn(Book &book, const std::vector<Save> &saves) {
  bool changed = false;
  // The oldest first: what a newer world says comes last and wins.
  for (auto it = book.pending.rbegin(); it != book.pending.rend();) {
    std::set<std::string> playthroughs;
    for (const auto &save : saves)
      if (!save.playthrough.empty() && save.seconds >= kMinSeconds &&
          Matches(save.seconds, it->seconds))
        playthroughs.insert(save.playthrough);
    if (playthroughs.size() != 1) {
      ++it;
      continue;
    }
    const auto &playthrough = *playthroughs.begin();
    if (it->host.empty())
      AddCampaign(book, playthrough);
    else
      AddGuest(book, it->host, playthrough);
    it = decltype(it)(book.pending.erase(std::next(it).base()));
    changed = true;
  }
  return changed;
}

std::optional<Save> LatestCampaign(const Book &book, const std::vector<Save> &saves) {
  return Newest(saves, [&](const Save &save) { return IsCampaign(book, save.playthrough); });
}

std::optional<Save> LatestOf(const Book &book, std::string_view host,
                             const std::vector<Save> &saves) {
  const auto guest =
      std::ranges::find_if(book.guests, [&](const Guest &g) { return g.host == host; });
  if (guest == book.guests.end())
    return std::nullopt;
  return Newest(saves, [&](const Save &save) { return save.playthrough == guest->playthrough; });
}
} // namespace tandem::campaigns
