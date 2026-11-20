// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "mods.hpp"

#include "log.hpp"
#include "runtime.hpp"

#include "tandem/session.hpp"

#include <RED4ext/ResourceDepot.hpp>
#include <RED4ext/TweakDB.hpp>

#include <format>

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

// Called with m_mutex held (from the resolver).
bool ModRecords::Known(mods::Kind kind, std::uint64_t record) {
  switch (kind) {
  case mods::Kind::LookPiece: {
    if (m_depot != Depot::Trusted)
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
  std::scoped_lock lock(m_mutex);
  if (m_depot != Depot::Unchecked || ours.empty())
    return;
  auto *depot = RED4ext::ResourceDepot::Get();
  std::size_t found = 0;
  for (const auto &key : ours)
    if (depot && depot->ResourceExists(RED4ext::ResourcePath(key.resource)))
      ++found;
  // Our own V's look is on our disk by definition: a depot that denies it
  // answers something else, and its answers are not used.
  m_depot = found == ours.size() ? Depot::Trusted : Depot::Ignored;
  log::Info("mods: the resource depot {} our own look ({} of {} pieces): {}",
            m_depot == Depot::Trusted ? "has" : "does not see", found, ours.size(),
            m_depot == Depot::Trusted ? "a partner's look piece we lack is left off"
                                      : "look pieces are never left off");
}

std::uint64_t ModRecords::Resolve(mods::Kind kind, std::uint64_t record,
                                  const proto::RecordHint &hint, proto::PlayerId player) {
  if (record == 0)
    return 0;
  const auto who = Runtime::Get().PlayerName(player);
  std::scoped_lock lock(m_mutex);
  const auto resolved = m_resolver.Resolve(kind, record, hint, player, who, net::NowUs());
  for (const auto &line : m_resolver.TakeLog())
    log::Info("{}", line);
  return resolved.record;
}

void ModRecords::Note(mods::Kind kind, std::uint64_t record, std::uint64_t shown,
                      proto::PlayerId player) {
  const auto who = Runtime::Get().PlayerName(player);
  std::scoped_lock lock(m_mutex);
  m_resolver.Note(kind, record,
                  {shown, shown == 0 ? mods::Outcome::Skipped : mods::Outcome::Default}, player,
                  who, net::NowUs());
  for (const auto &line : m_resolver.TakeLog())
    log::Info("{}", line);
}

void ModRecords::OnCompared(const events::RemoteMods &mods) {
  std::scoped_lock lock(m_mutex);
  auto &theirs = m_theirs[mods.player];
  theirs.name = mods.name;
  theirs.comparison = mods.comparison;
  log::Info("mods: {}", mods::Describe(mods.comparison, mods.name).empty()
                            ? std::format("{} has the same mods as we do ({})", mods.name,
                                          mods.comparison.shared)
                            : mods::Describe(mods.comparison, mods.name));
  if (mods.comparison.Differ())
    m_notices.push_back(
        {.what = ModNotice::What::Compared, .player = mods.player, .name = mods.name});
}

void ModRecords::OnNames(const events::ModNamesFrom &names) {
  std::scoped_lock lock(m_mutex);
  auto &theirs = m_theirs[names.player];
  theirs.names.clear();
  for (const auto &mod : names.mods)
    theirs.names.push_back(mod.name);
  log::Info("mods: {}'s mods we lack: {}", names.name, [&] {
    std::string all;
    for (const auto &name : theirs.names)
      all += (all.empty() ? "" : ", ") + name;
    return all;
  }());
  m_notices.push_back({.what = ModNotice::What::NamesCame,
                       .player = names.player,
                       .name = names.name,
                       .count = static_cast<std::int32_t>(names.mods.size())});
}

void ModRecords::OnSeen(const events::SubstitutesFrom &seen) {
  std::scoped_lock lock(m_mutex);
  log::Info("mods: {}'s game shows substitutes for {} of ours (kinds {:#x})", seen.name,
            seen.substitutes.count, seen.substitutes.kinds);
  m_notices.push_back({.what = ModNotice::What::Seen,
                       .player = seen.substitutes.seer,
                       .name = seen.name,
                       .kind = seen.substitutes.kinds,
                       .count = seen.substitutes.count});
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
