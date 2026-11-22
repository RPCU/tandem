// What the scripts say and ask about what the game runs without:
// `TandemDegraded` static functions and its struct
// (docs/architecture/degradations.md). Declared on the script side in
// scripts/tandem/DegradedNatives.reds: keep the two in sync
// (tools/check-natives.py reads every pair). State, Chosen, Over and
// Fallback only take a lock of their own and write a log line: any thread
// may call them (a hook on a job thread too). The others run on the game
// thread.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "degradations.hpp"
#include "log.hpp"
#include "runtime.hpp"

#include "tandem/degraded.hpp"

#include <RedLib.hpp>

namespace tandem::plugin {
// One entry of the registry, or one thing to tell the player. Plain fields
// only: the script VM aligns struct locals to 8.
struct TandemDegradedEntry {
  std::uint32_t player{}; // 0: this game; else the player whose game said it
  Red::CString id;
  Red::CString reason; // ours only (the others say none)
  bool core{};
  bool chosen{};
  bool fallback{};      // taken once (counted), not a state that holds
  bool over{};          // news only: the state ended
  std::int32_t count{}; // a fallback's times
};
static_assert(alignof(TandemDegradedEntry) <= 8, "the script VM aligns struct locals to 8");

namespace {
TandemDegradedEntry ForScripts(const degraded::Entry &entry, proto::PlayerId player, bool over) {
  return {.player = player,
          .id = Red::CString(entry.id.c_str()),
          .reason = Red::CString(entry.reason.c_str()),
          .core = entry.klass == degraded::Class::Core,
          .chosen = entry.chosen,
          .fallback = entry.kind == degraded::Kind::Fallback,
          .over = over,
          .count = static_cast<std::int32_t>(entry.count)};
}
} // namespace

struct TandemDegraded : Red::IScriptable {
  // A path of the sync is off or lesser for now (a state: it holds until
  // Over): logged as a warning, shown to the player, told to the others.
  // `id`: one of degraded::Catalogue() (core/src/degraded.cpp).
  static void State(Red::CString id, Red::CString reason) {
    Degradations::Get().Start(id.c_str(), reason.c_str());
  }
  // The same by the player's own tandem.ini switch: listed, no failure.
  static void Chosen(Red::CString id, Red::CString reason) {
    Degradations::Get().Start(id.c_str(), reason.c_str(), true);
  }
  static void Over(Red::CString id) { Degradations::Get().End(id.c_str()); }
  // A lesser path was taken once, for one body: counted, with this reason.
  static void Fallback(Red::CString id, Red::CString reason) {
    Degradations::Get().Fallback(id.c_str(), reason.c_str());
  }

  // What this game runs without (states that hold, then fallbacks taken),
  // then what each other player's game said.
  static Red::DynArray<TandemDegradedEntry> Entries() {
    Red::DynArray<TandemDegradedEntry> out;
    for (const auto &entry : Degradations::Get().Local().List())
      out.EmplaceBack(ForScripts(entry, 0, false));
    for (const auto &said : Degradations::Get().Remote())
      for (const auto &entry : said.entries)
        out.EmplaceBack(TandemDegradedEntry{.player = said.player,
                                            .id = Red::CString(entry.id.c_str()),
                                            .core = (entry.flags & proto::kDegradedCore) != 0,
                                            .chosen = (entry.flags & proto::kDegradedChosen) != 0,
                                            .count = 1});
    return out;
  }
  // The core features that are off: no session can be hosted or joined
  // (Runtime::Blocked; CompatProblems has a sentence for each).
  static Red::DynArray<TandemDegradedEntry> CoreOff() {
    Red::DynArray<TandemDegradedEntry> out;
    for (const auto &entry : Degradations::Get().Local().CoreOff())
      out.EmplaceBack(ForScripts(entry, 0, false));
    return out;
  }
  // Every tick of a session: what to tell the player since the last call (a
  // state that started or ended, the first fallback of an id, another
  // player's game).
  static Red::DynArray<TandemDegradedEntry> TakeNews() {
    Red::DynArray<TandemDegradedEntry> out;
    for (const auto &news : Degradations::Get().TakeNews())
      out.EmplaceBack(ForScripts(news.entry, news.player, news.over));
    return out;
  }
  // True once after a session tandem.ini or the menu had chosen was dropped
  // because a core feature is off (the player is told why nothing starts).
  static bool TakeRefused() { return Runtime::Get().TakeCoreRefusal(); }
  // `degraded: none`, or the list on one line.
  static Red::CString Summary() {
    return Red::CString(Degradations::Get().Local().Summary().c_str());
  }
  // (tools/game.sh degraded, cmd degraded) every entry, in the log.
  static void LogRegistry() {
    const auto lines = Degradations::Get().Local().Report();
    for (std::size_t i = 0; i + 1 < lines.size(); ++i)
      log::Always("{}", lines[i]);
    for (const auto &said : Degradations::Get().Remote())
      log::Always("degraded: player {}'s game runs without: {}", said.player,
                  degraded::Describe(said));
    log::Always("{}", lines.back());
  }
  // (test reset) The fallbacks counted so far are forgotten; states stay.
  static void ResetFallbacks() { Degradations::Get().ResetFallbacks(); }

  RTTI_IMPL_TYPEINFO(TandemDegraded);
  RTTI_IMPL_ALLOCATOR();
};
} // namespace tandem::plugin

RTTI_DEFINE_CLASS(tandem::plugin::TandemDegradedEntry, "TandemDegradedEntry", {
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(id);
  RTTI_PROPERTY(reason);
  RTTI_PROPERTY(core);
  RTTI_PROPERTY(chosen);
  RTTI_PROPERTY(fallback);
  RTTI_PROPERTY(over);
  RTTI_PROPERTY(count);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemDegraded, "TandemDegraded", {
  RTTI_METHOD(State);
  RTTI_METHOD(Chosen);
  RTTI_METHOD(Over);
  RTTI_METHOD(Fallback);
  RTTI_METHOD(Entries);
  RTTI_METHOD(CoreOff);
  RTTI_METHOD(TakeNews);
  RTTI_METHOD(TakeRefused);
  RTTI_METHOD(Summary);
  RTTI_METHOD(LogRegistry);
  RTTI_METHOD(ResetFallbacks);
});
