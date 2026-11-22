// The game's registry of what it runs without: see degradations.hpp.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "degradations.hpp"
#include "log.hpp"

#include <algorithm>
#include <utility>

namespace tandem::plugin {
namespace {
degraded::Entry EntryOf(const degraded::Registry &registry, std::string_view id,
                        degraded::Kind kind) {
  for (auto &entry : registry.List())
    if (entry.id == id && entry.kind == kind)
      return entry;
  return {.id = std::string(id), .kind = kind};
}

degraded::Entry EntryOf(const proto::DegradedEntry &said) {
  return {.id = said.id,
          .klass = (said.flags & proto::kDegradedCore) != 0 ? degraded::Class::Core
                                                            : degraded::Class::Degraded,
          .kind = degraded::Kind::State,
          .chosen = (said.flags & proto::kDegradedChosen) != 0,
          .known = degraded::Find(said.id) != nullptr,
          .count = 1};
}
} // namespace

Degradations &Degradations::Get() {
  static Degradations instance;
  return instance;
}

void Degradations::Queue(News news) {
  std::lock_guard _(m_lock);
  // Never without bound (nobody takes the news while no V is in a session).
  if (m_news.size() < 256)
    m_news.push_back(std::move(news));
}

void Degradations::Start(std::string_view id, std::string_view reason, bool chosen) {
  if (!m_local.Start(id, reason, chosen))
    return;
  const auto entry = EntryOf(m_local, id, degraded::Kind::State);
  if (!entry.known)
    log::Error("degraded: {} is not a state of the catalogue (core/src/degraded.cpp)", id);
  if (entry.chosen)
    log::Warn("degraded: {} is off by choice: {}", id, reason);
  else if (entry.klass == degraded::Class::Core)
    log::Error("degraded: {} is OFF, co-op cannot run without it: {}", id, reason);
  else
    log::Warn("degraded: {} is off: {}", id, reason);
  Queue({.entry = entry});
}

void Degradations::End(std::string_view id) {
  const auto entry = EntryOf(m_local, id, degraded::Kind::State);
  if (!m_local.End(id))
    return;
  log::Info("degraded: {} is back", id);
  Queue({.entry = entry, .over = true});
}

void Degradations::Fallback(std::string_view id, std::string_view reason) {
  const auto count = m_local.Count(id, reason);
  if (count > 1) {
    log::Debug("degraded: fallback {} x{}: {}", id, count, reason);
    return;
  }
  const auto entry = EntryOf(m_local, id, degraded::Kind::Fallback);
  if (!entry.known)
    log::Error("degraded: {} is not a fallback of the catalogue (core/src/degraded.cpp)", id);
  log::Warn("degraded: fallback {}: {}", id, reason);
  Queue({.entry = entry});
}

void Degradations::ResetFallbacks() { m_local.ResetFallbacks(); }

void Degradations::NoteRemote(const proto::Degraded &said) {
  std::vector<News> news;
  {
    std::lock_guard _(m_lock);
    const auto it = m_remote.find(said.player);
    const auto before = it == m_remote.end() ? proto::Degraded{.player = said.player} : it->second;
    if (before == said)
      return;
    const auto has = [](const proto::Degraded &list, const proto::DegradedEntry &entry) {
      return std::ranges::find(list.entries, entry.id, &proto::DegradedEntry::id) !=
             list.entries.end();
    };
    for (const auto &entry : said.entries)
      if (!has(before, entry))
        news.push_back({.player = said.player, .entry = EntryOf(entry)});
    for (const auto &entry : before.entries)
      if (!has(said, entry))
        news.push_back({.player = said.player, .entry = EntryOf(entry), .over = true});
    if (said.entries.empty())
      m_remote.erase(said.player);
    else
      m_remote[said.player] = said;
  }
  log::Warn("degraded: player {}'s game runs without: {}", said.player, degraded::Describe(said));
  for (auto &one : news)
    Queue(std::move(one));
}

std::vector<proto::Degraded> Degradations::Remote() const {
  std::lock_guard _(m_lock);
  std::vector<proto::Degraded> out;
  for (const auto &[player, said] : m_remote)
    out.push_back(said);
  return out;
}

void Degradations::BeginSession() {
  const auto entries = m_local.List();
  std::lock_guard _(m_lock);
  m_remote.clear();
  m_news.clear();
  m_nextNewsUs = 0;
  // The states only: a fallback taken before this session was said then.
  for (const auto &entry : entries)
    if (entry.kind == degraded::Kind::State)
      m_news.push_back({.entry = entry});
}

void Degradations::EndSession() {
  std::lock_guard _(m_lock);
  m_remote.clear();
  std::erase_if(m_news, [](const News &news) { return news.player != 0; });
}

std::vector<Degradations::News> Degradations::TakeNews(std::uint64_t nowUs) {
  std::lock_guard _(m_lock);
  if (m_news.empty() || nowUs < m_nextNewsUs)
    return {};
  // One notice: what reads as one sentence with the first queued.
  const auto sort = [](const News &news) {
    return news.over                                     ? 0
           : news.entry.kind == degraded::Kind::Fallback ? 1
           : news.entry.chosen                           ? 2
                                                         : 3;
  };
  const auto player = m_news.front().player;
  const auto kind = sort(m_news.front());
  std::vector<News> out, rest;
  for (auto &news : m_news)
    (news.player == player && sort(news) == kind ? out : rest).push_back(std::move(news));
  m_news = std::move(rest);
  m_nextNewsUs = nowUs + kNewsGapUs;
  return out;
}
} // namespace tandem::plugin
