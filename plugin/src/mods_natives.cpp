// What scripts/tandem/Mods.reds can call: `TandemModsNative` static
// functions and their structs (docs/architecture/mod-compat.md). Declared
// on the script side in scripts/tandem/ModsNatives.reds: keep the two in
// sync (tools/check-natives.py reads every pair). All of these run on the
// game thread.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "log.hpp"
#include "mods.hpp"

#include "tandem/mods.hpp"
#include "tandem/session.hpp"

#include <RedLib.hpp>

#include <format>

namespace tandem::plugin {
// One thing to tell the player (ModNotice). Plain fields only: the script
// VM aligns struct locals to 8.
struct TandemModNotice {
  std::int32_t what{}; // ModNotice::What
  std::uint32_t player{};
  Red::CString name;
  std::int32_t kind{}; // 1: mods::Kind; 3: its bits
  std::int32_t count{};
  std::int32_t outcome{}; // mods::Outcome
};
static_assert(alignof(TandemModNotice) <= 8, "the script VM aligns struct locals to 8");

// One replacement this session (the co-op menu's list).
struct TandemModSubstitution {
  std::uint32_t player{};
  Red::CString name;
  std::int32_t kind{};    // mods::Kind
  std::int32_t outcome{}; // mods::Outcome
  Red::CString record;    // theirs, as an id (our game has no name for it)
  Red::CString shown;     // what ours shows: a record name, "" for nothing
};

struct TandemModsNative : Red::IScriptable {
  // Every tick: what to tell the player since the last call.
  static Red::DynArray<TandemModNotice> ModNotices() {
    Red::DynArray<TandemModNotice> out;
    for (const auto &notice : ModRecords::Get().TakeNotices(net::NowUs()))
      out.EmplaceBack(TandemModNotice{.what = static_cast<std::int32_t>(notice.what),
                                      .player = notice.player,
                                      .name = Red::CString(notice.name.c_str()),
                                      .kind = notice.kind,
                                      .count = notice.count,
                                      .outcome = notice.outcome});
    return out;
  }
  // The players whose mods were compared with ours.
  static Red::DynArray<std::uint32_t> ModPlayers() {
    Red::DynArray<std::uint32_t> out;
    for (const auto player : ModRecords::Get().Compared())
      out.EmplaceBack(player);
    return out;
  }
  // How many mods `player` has that we lack (theirs), or we have that they
  // lack: per category (items, vehicles, characters, looks, gameplay,
  // unknown, local), then the total. Empty: not compared.
  static Red::DynArray<std::int32_t> ModCounts(std::uint32_t player, bool theirs) {
    Red::DynArray<std::int32_t> out;
    for (const auto count : ModRecords::Get().Counts(player, theirs))
      out.EmplaceBack(count);
    return out;
  }
  // The names of `player`'s mods we lack, once asked for and answered.
  static Red::DynArray<Red::CString> ModNamesOf(std::uint32_t player) {
    Red::DynArray<Red::CString> out;
    for (const auto &name : ModRecords::Get().Names(player))
      out.EmplaceBack(name.c_str());
    return out;
  }
  static void AskModNames(std::uint32_t player) { ModRecords::Get().AskNames(player); }
  static Red::DynArray<TandemModSubstitution> ModSubstitutions() {
    Red::DynArray<TandemModSubstitution> out;
    for (const auto &s : ModRecords::Get().Substitutions())
      out.EmplaceBack(TandemModSubstitution{
          .player = s.from,
          .name = Red::CString(s.who.c_str()),
          .kind = static_cast<std::int32_t>(s.kind),
          .outcome = static_cast<std::int32_t>(s.to.outcome),
          .record = Red::CString(std::format("{:#018x}", s.record).c_str()),
          .shown = Red::CString(s.to.record == 0 ? ""
                                : mods::NameOf(s.to.record).empty()
                                    ? std::format("{:#018x}", s.to.record).c_str()
                                    : std::string(mods::NameOf(s.to.record)).c_str())});
    return out;
  }
  // A replacement the script decided (`kind`: mods::Kind; an NPC look by its
  // name, a radio station by its number; nothing shown instead).
  static void ModNote(std::int32_t kind, Red::CName name, std::int32_t number,
                      std::uint32_t player) {
    const auto what = name.hash != 0 ? name.hash : static_cast<std::uint64_t>(number) + 1;
    ModRecords::Get().Note(static_cast<mods::Kind>(kind), what, 0, player);
  }
  // A record from `player` our game may lack, of a kind with no hint: what
  // to use (none: nothing).
  static Red::TweakDBID ResolveRecord(std::int32_t kind, Red::TweakDBID record,
                                      std::uint32_t player) {
    return ModRecords::Get().Resolve(static_cast<mods::Kind>(kind), record.value, {}, player);
  }
  // (tools/game.sh cmd coop mods) everything, in the log.
  static void ModReport() {
    for (const auto &line : ModRecords::Get().Report())
      log::Info("mods: {}", line);
  }

  RTTI_IMPL_TYPEINFO(TandemModsNative);
  RTTI_IMPL_ALLOCATOR();
};
} // namespace tandem::plugin

RTTI_DEFINE_CLASS(tandem::plugin::TandemModNotice, "TandemModNotice", {
  RTTI_PROPERTY(what);
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(name);
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(count);
  RTTI_PROPERTY(outcome);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemModSubstitution, "TandemModSubstitution", {
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(name);
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(outcome);
  RTTI_PROPERTY(record);
  RTTI_PROPERTY(shown);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemModsNative, "TandemModsNative", {
  RTTI_METHOD(ModNotices);
  RTTI_METHOD(ModPlayers);
  RTTI_METHOD(ModCounts);
  RTTI_METHOD(ModNamesOf);
  RTTI_METHOD(AskModNames);
  RTTI_METHOD(ModSubstitutions);
  RTTI_METHOD(ModNote);
  RTTI_METHOD(ResolveRecord);
  RTTI_METHOD(ModReport);
});
