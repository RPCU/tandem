// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "mods.hpp"

#include "degradations.hpp"
#include "log.hpp"
#include "runtime.hpp"

#include "tandem/session.hpp"

#include <RED4ext/ResourceDepot.hpp>
#include <RED4ext/TweakDB.hpp>

#include <format>
#include <string>
#include <vector>

namespace tandem::plugin {
ModRecords &ModRecords::Get() {
  static ModRecords mods;
  return mods;
}

void ModRecords::SetOurs(std::vector<mods::Mod> ours) {
  std::scoped_lock lock(m_mutex);
  m_ours = std::move(ours);
}

std::vector<proto::ModEntry> ModRecords::OursForIdentity() {
  std::scoped_lock lock(m_mutex);
  return mods::ToWire(m_ours, true);
}

// The game's own checks: never under m_mutex (the net thread takes it every
// pass; an engine call that waits must not stop it too).
bool ModRecords::Known(mods::Kind kind, std::uint64_t record) {
  switch (kind) {
  case mods::Kind::LookPiece: {
    if (m_depot.load() != Depot::Trusted)
      return true;
    auto *depot = RED4ext::ResourceDepot::Get();
    return !depot || depot->ResourceExists(RED4ext::ResourcePath(record));
  }
  case mods::Kind::NpcLook:
  case mods::Kind::Radio:
    // Checked by the scripts (Note): what comes here is kept.
    return true;
  default: {
    auto *tweaks = RED4ext::TweakDB::Get();
    return !tweaks || tweaks->GetRecord(RED4ext::TweakDBID(record));
  }
  }
}

void ModRecords::CheckDepot(const std::vector<proto::AppearanceKey> &ours) {
  if (m_depot.load() != Depot::Unchecked || ours.empty())
    return;
  auto *depot = RED4ext::ResourceDepot::Get();
  std::size_t found = 0;
  for (const auto &key : ours)
    if (depot && depot->ResourceExists(RED4ext::ResourcePath(key.resource)))
      ++found;
  // Our own V's look is on our disk by definition: a depot that denies it
  // answers something else, and its answers are not used.
  const auto verdict = found == ours.size() ? Depot::Trusted : Depot::Ignored;
  auto unchecked = Depot::Unchecked;
  if (!m_depot.compare_exchange_strong(unchecked, verdict))
    return; // another thread checked first
  log::Info("mods: the resource depot {} our own look ({} of {} pieces): {}",
            verdict == Depot::Trusted ? "has" : "does not see", found, ours.size(),
            verdict == Depot::Trusted ? "a partner's look piece we lack is left off"
                                      : "look pieces are never left off");
}

std::uint64_t ModRecords::Resolve(mods::Kind kind, std::uint64_t record,
                                  const proto::RecordHint &hint, proto::PlayerId player) {
  if (record == 0)
    return 0;
  const auto who = Runtime::Get().PlayerName(player);
  bool memoized = false;
  {
    std::scoped_lock lock(m_mutex);
    memoized = m_resolver.Memoized(kind, record, hint);
  }
  // The game's checks (TweakDB, the depot) outside the lock, and the log
  // lines after it.
  const auto decided =
      memoized ? mods::Resolved{}
               : mods::Resolve(kind, record, hint,
                               [this](mods::Kind k, std::uint64_t r) { return Known(k, r); });
  mods::Resolved resolved;
  std::vector<std::string> lines;
  {
    std::scoped_lock lock(m_mutex);
    // (Memoized, then the session's resolver cleared since: the plain
    // Resolve checks again, under the lock, as it always did.)
    resolved = memoized
                   ? m_resolver.Resolve(kind, record, hint, player, who, net::NowUs())
                   : m_resolver.Resolve(kind, record, hint, player, who, net::NowUs(), decided);
    lines = m_resolver.TakeLog();
  }
  // A record of theirs shown otherwise here: never only in the log
  // (degradations.hpp; the notice is Mods.reds').
  for (const auto &line : lines) {
    log::Info("{}", line);
    Degradations::Get().Fallback("record substituted", line);
  }
  return resolved.record;
}

void ModRecords::Note(mods::Kind kind, std::uint64_t record, std::uint64_t shown,
                      proto::PlayerId player) {
  const auto who = Runtime::Get().PlayerName(player);
  std::vector<std::string> lines;
  {
    std::scoped_lock lock(m_mutex);
    m_resolver.Note(kind, record,
                    {shown, shown == 0 ? mods::Outcome::Skipped : mods::Outcome::Default}, player,
                    who, net::NowUs());
    lines = m_resolver.TakeLog();
  }
  for (const auto &line : lines) {
    log::Info("{}", line);
    Degradations::Get().Fallback("record substituted", line);
  }
}

void ModRecords::OnCompared(const events::RemoteMods &mods) {
  {
    std::scoped_lock lock(m_mutex);
    auto &theirs = m_theirs[mods.player];
    theirs.name = mods.name;
    theirs.comparison = mods.comparison;
    if (mods.comparison.Differ())
      m_notices.push_back(
          {.what = ModNotice::What::Compared, .player = mods.player, .name = mods.name});
  }
  log::Info("mods: {}", mods::Describe(mods.comparison, mods.name).empty()
                            ? std::format("{} has the same mods as we do ({})", mods.name,
                                          mods.comparison.shared)
                            : mods::Describe(mods.comparison, mods.name));
}

void ModRecords::OnNames(const events::ModNamesFrom &names) {
  std::string all;
  for (const auto &mod : names.mods)
    all += (all.empty() ? "" : ", ") + mod.name;
  {
    std::scoped_lock lock(m_mutex);
    auto &theirs = m_theirs[names.player];
    theirs.names.clear();
    for (const auto &mod : names.mods)
      theirs.names.push_back(mod.name);
    m_notices.push_back({.what = ModNotice::What::NamesCame,
                         .player = names.player,
                         .name = names.name,
                         .count = static_cast<std::int32_t>(names.mods.size())});
  }
  log::Info("mods: {}'s mods we lack: {}", names.name, all);
}

void ModRecords::OnSeen(const events::SubstitutesFrom &seen) {
  {
    std::scoped_lock lock(m_mutex);
    m_notices.push_back({.what = ModNotice::What::Seen,
                         .player = seen.substitutes.seer,
                         .name = seen.name,
                         .kind = seen.substitutes.kinds,
                         .count = seen.substitutes.count});
  }
  log::Info("mods: {}'s game shows substitutes for {} of ours (kinds {:#x})", seen.name,
            seen.substitutes.count, seen.substitutes.kinds);
}

void ModRecords::Ended() {
  std::scoped_lock lock(m_mutex);
  m_resolver.Clear();
  m_theirs.clear();
  m_notices.clear();
  m_nameAsks.clear();
}

std::vector<proto::Substitutes> ModRecords::TakeTold() {
  std::scoped_lock lock(m_mutex);
  return m_resolver.TakeTold();
}

std::vector<proto::PlayerId> ModRecords::TakeNameAsks() {
  std::scoped_lock lock(m_mutex);
  return std::exchange(m_nameAsks, {});
}

std::vector<ModNotice> ModRecords::TakeNotices(std::uint64_t nowUs) {
  std::scoped_lock lock(m_mutex);
  auto out = std::exchange(m_notices, {});
  for (const auto &notice : m_resolver.TakeNotices(nowUs))
    out.push_back({.what = ModNotice::What::Replaced,
                   .player = notice.from,
                   .name = notice.who,
                   .kind = static_cast<std::int32_t>(notice.kind),
                   .count = notice.count,
                   .outcome = static_cast<std::int32_t>(notice.outcome)});
  return out;
}

std::vector<proto::PlayerId> ModRecords::Compared() {
  std::scoped_lock lock(m_mutex);
  std::vector<proto::PlayerId> out;
  for (const auto &[player, theirs] : m_theirs)
    out.push_back(player);
  return out;
}

std::vector<std::int32_t> ModRecords::Counts(proto::PlayerId player, bool theirs) {
  std::scoped_lock lock(m_mutex);
  std::vector<std::int32_t> out;
  const auto found = m_theirs.find(player);
  if (found == m_theirs.end())
    return out;
  const auto &counts =
      theirs ? found->second.comparison.theirsOnly : found->second.comparison.oursOnly;
  for (const auto count : counts.by)
    out.push_back(count);
  out.push_back(counts.total);
  return out;
}

std::vector<std::string> ModRecords::Names(proto::PlayerId player) {
  std::scoped_lock lock(m_mutex);
  const auto found = m_theirs.find(player);
  return found == m_theirs.end() ? std::vector<std::string>{} : found->second.names;
}

void ModRecords::AskNames(proto::PlayerId player) {
  std::scoped_lock lock(m_mutex);
  m_nameAsks.push_back(player);
}

std::vector<mods::Substitution> ModRecords::Substitutions() {
  std::scoped_lock lock(m_mutex);
  return m_resolver.List();
}

std::vector<std::string> ModRecords::Report() {
  std::scoped_lock lock(m_mutex);
  std::vector<std::string> out;
  out.push_back(std::format("our mods: {}", m_ours.size()));
  for (const auto &mod : m_ours)
    out.push_back(std::format("  {} ({})", mod.name, mods::CategoryName(mod.categories)));
  for (const auto &[player, theirs] : m_theirs) {
    const auto words = mods::Describe(theirs.comparison, theirs.name);
    out.push_back(words.empty() ? std::format("{}: the same mods", theirs.name) : words);
    for (const auto &name : theirs.names)
      out.push_back(std::format("  {} has {}", theirs.name, name));
  }
  for (const auto &s : m_resolver.List()) {
    const auto shown = mods::NameOf(s.to.record);
    out.push_back(std::format("replaced: {}'s {} {:#018x} -> {}", s.who, mods::KindName(s.kind),
                              s.record,
                              s.to.record == 0 ? std::string("nothing")
                              : shown.empty()  ? std::format("{:#018x}", s.to.record)
                                               : std::string(shown)));
  }
  return out;
}
} // namespace tandem::plugin
