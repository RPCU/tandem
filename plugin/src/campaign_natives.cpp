// The player's co-op playthroughs (tandem/campaigns.hpp, campaigns.txt next
// to the settings; docs/architecture/coop-menu.md, "Campaigns"):
// `TandemCampaignNative` static functions and its struct. Declared on the
// script side in scripts/tandem/CampaignNatives.reds: keep the two in sync
// (tools/check-natives.py reads every pair). The main menu tells what saves
// there are (SaveSeen, from its metadata callback) and which it loads; the
// world says when it loaded and saved, and whether it is a co-op campaign;
// the net thread says which host our V joined (campaign.hpp). Every function
// only takes our own lock (no engine call under it): any thread may call it.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "campaign.hpp"
#include "log.hpp"

#include "tandem/campaigns.hpp"

#include <RedLib.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace tandem::plugin {
namespace {
// A world's playthrough time at its load and the save the menu loaded it
// from disagree by more than this: not that save after all.
constexpr double kLoadedFromSeconds = 30.0;
constexpr std::size_t kMaxWorldTimes = 16;

struct State {
  std::mutex lock;
  std::filesystem::path file;
  bool loaded{};
  campaigns::Book book;
  // The main menu's list of saves, as its metadata came.
  std::vector<campaigns::Save> saves;
  // The save the main menu is loading (index), or -1 none known.
  std::int32_t loading{-1};
  campaigns::World world;
  bool campaign{};
  std::vector<std::string> joined;
  // (tests) the next world loads as a co-op campaign, as one whose save says so.
  bool testNext{};
};

State &The() {
  static State state;
  return state;
}

// Under the lock: the book, read once.
void Load(State &state) {
  if (state.loaded)
    return;
  state.loaded = true;
  if (state.file.empty())
    return;
  std::ifstream file(state.file);
  std::stringstream text;
  text << file.rdbuf();
  state.book = campaigns::Parse(text.str());
}

// Under the lock: the book written (false: it could not be).
bool Store(State &state) {
  if (state.file.empty())
    return true;
  std::ofstream file(state.file, std::ios::trunc);
  file << campaigns::Format(state.book);
  return static_cast<bool>(file);
}

// Under the lock: the world noted again for what it is, after a new time.
bool Note(State &state) {
  bool changed = false;
  if (state.campaign)
    changed |= campaigns::MarkCampaign(state.book, state.world);
  for (const auto &host : state.joined)
    changed |= campaigns::JoinedWith(state.book, state.world, host);
  return changed;
}

std::string Shown(const std::string &playthrough) {
  return playthrough.empty() ? std::string("not known") : playthrough;
}
} // namespace

// One save, for the menu. Plain fields only (the script VM aligns struct
// locals to 8).
struct TandemSave {
  std::int32_t index{-1}; // -1: none
  std::int32_t level{};
  Red::CString location;
  Red::CString playthrough;
  bool modded{};
};

namespace {
TandemSave ToScript(const std::optional<campaigns::Save> &save) {
  TandemSave out;
  if (!save)
    return out;
  out.index = save->index;
  out.level = save->level;
  out.location = Red::CString(save->location.c_str());
  out.playthrough = Red::CString(save->playthrough.c_str());
  out.modded = save->modded;
  return out;
}
} // namespace

namespace campaign {
void SetFile(std::filesystem::path file) {
  auto &state = The();
  std::lock_guard _(state.lock);
  state.file = std::move(file);
  state.loaded = false;
}

void JoinedWith(const std::string &host) {
  auto &state = The();
  bool changed = false, stored = true;
  std::string playthrough;
  {
    std::lock_guard _(state.lock);
    Load(state);
    if (std::ranges::find(state.joined, host) == state.joined.end())
      state.joined.push_back(host);
    changed = campaigns::JoinedWith(state.book, state.world, host);
    if (changed)
      stored = Store(state);
    playthrough = state.world.playthrough;
  }
  // (Said whether the book changed or not: a new V has no time to note yet,
  // its saves will say.)
  log::Info("campaign: our V noted for {} (playthrough {}{})", host, Shown(playthrough),
            playthrough.empty() ? ", learnt from its saves' times at the next main menu" : "");
  if (!stored)
    log::Warn("campaign: could not write campaigns.txt");
}
} // namespace campaign

struct TandemCampaignNative : Red::IScriptable {
  // The main menu's list starts over (its saves are being read).
  static void SavesReset() {
    auto &state = The();
    std::lock_guard _(state.lock);
    state.saves.clear();
  }

  // One save's metadata (the main menu's SaveMetadataInfo). Pending worlds
  // whose time it matches are learnt.
  static void SaveSeen(std::int32_t index, Red::CString playthrough, float seconds, float level,
                       Red::CString location, std::uint64_t stamp, bool modded) {
    auto &state = The();
    bool learnt = false, stored = true;
    std::size_t campaignsKnown = 0, guestsKnown = 0;
    {
      std::lock_guard _(state.lock);
      Load(state);
      std::erase_if(state.saves, [&](const campaigns::Save &s) { return s.index == index; });
      state.saves.push_back({.index = index,
                             .playthrough = playthrough.c_str(),
                             .seconds = seconds,
                             .level = static_cast<std::int32_t>(std::lround(level)),
                             .location = location.c_str(),
                             .stamp = stamp,
                             .modded = modded});
      learnt = campaigns::Learn(state.book, state.saves);
      if (learnt)
        stored = Store(state);
      campaignsKnown = state.book.campaigns.size();
      guestsKnown = state.book.guests.size();
    }
    if (learnt)
      log::Info("campaign: learnt from the saves: {} co-op campaign(s), a V for {} host(s)",
                campaignsKnown, guestsKnown);
    if (!stored)
      log::Warn("campaign: could not write campaigns.txt");
  }

  // The main menu loads this save of its list (-1: a new game, or one it
  // does not know).
  static void Loading(std::int32_t index) {
    auto &state = The();
    std::lock_guard _(state.lock);
    state.loading = index;
  }

  // A world is loaded (its playthrough time now; whether its save says it
  // is a co-op campaign). True: it is one (its save says so, or a test).
  static bool WorldLoaded(float seconds, bool marked) {
    auto &state = The();
    bool campaign = false, stored = true, distrusted = false;
    std::string playthrough;
    {
      std::lock_guard _(state.lock);
      Load(state);
      state.world = {};
      state.joined.clear();
      const auto from = std::ranges::find_if(
          state.saves, [&](const campaigns::Save &s) { return s.index == state.loading; });
      if (state.loading >= 0 && from != state.saves.end()) {
        if (std::abs(from->seconds - seconds) <= kLoadedFromSeconds)
          state.world.playthrough = from->playthrough;
        else
          distrusted = true;
      }
      state.loading = -1;
      state.world.seconds.push_back(seconds);
      state.campaign = marked || state.testNext;
      state.testNext = false;
      campaign = state.campaign;
      if (Note(state))
        stored = Store(state);
      playthrough = state.world.playthrough;
    }
    log::Info("campaign: a world loaded at {:.1f} s of playthrough time, playthrough {}{}{}",
              seconds, Shown(playthrough), distrusted ? " (not the save the menu loaded)" : "",
              campaign ? ": a co-op campaign" : "");
    if (!stored)
      log::Warn("campaign: could not write campaigns.txt");
    return campaign;
  }

  // The world was saved (its playthrough time then).
  static void WorldSaved(float seconds) {
    auto &state = The();
    bool stored = true;
    {
      std::lock_guard _(state.lock);
      Load(state);
      if (state.world.seconds.size() < kMaxWorldTimes)
        state.world.seconds.push_back(seconds);
      if (Note(state))
        stored = Store(state);
    }
    if (!stored)
      log::Warn("campaign: could not write campaigns.txt");
  }

  // This world is a co-op campaign from now on (on), or no longer (off).
  static void Mark(bool on) {
    auto &state = The();
    bool changed = false, stored = true;
    std::string playthrough;
    {
      std::lock_guard _(state.lock);
      Load(state);
      state.campaign = on;
      changed = on ? campaigns::MarkCampaign(state.book, state.world)
                   : campaigns::Unmark(state.book, state.world);
      if (changed)
        stored = Store(state);
      playthrough = state.world.playthrough;
    }
    log::Info("campaign: this world is {}a co-op campaign (playthrough {})", on ? "" : "no longer ",
              Shown(playthrough));
    if (!stored)
      log::Warn("campaign: could not write campaigns.txt");
  }

  static bool IsCampaign() {
    auto &state = The();
    std::lock_guard _(state.lock);
    return state.campaign;
  }

  // The newest save of a co-op campaign in the main menu's list.
  static TandemSave CampaignSave() {
    auto &state = The();
    std::lock_guard _(state.lock);
    Load(state);
    return ToScript(campaigns::LatestCampaign(state.book, state.saves));
  }

  // The newest save of the V this host was last joined with (its key:
  // TandemHost.key).
  static TandemSave LastV(Red::CString host) {
    auto &state = The();
    std::lock_guard _(state.lock);
    Load(state);
    return ToScript(campaigns::LatestOf(state.book, host.c_str(), state.saves));
  }

  // (tests: coop campaign next) The next world loads as a co-op campaign.
  static void TestNext() {
    auto &state = The();
    std::lock_guard _(state.lock);
    state.testNext = true;
  }

  // What is known, in one line (coop campaign).
  static Red::CString CampaignReport() {
    auto &state = The();
    std::lock_guard _(state.lock);
    Load(state);
    const auto line = std::format(
        "campaign: {} co-op campaign(s), a V for {} host(s), {} world(s) to learn; this world: "
        "playthrough {}, co-op campaign {}; the menu's saves: {}",
        state.book.campaigns.size(), state.book.guests.size(), state.book.pending.size(),
        Shown(state.world.playthrough), state.campaign ? "yes" : "no", state.saves.size());
    return Red::CString(line.c_str());
  }

  RTTI_IMPL_TYPEINFO(TandemCampaignNative);
  RTTI_IMPL_ALLOCATOR();
};
} // namespace tandem::plugin

RTTI_DEFINE_CLASS(tandem::plugin::TandemSave, "TandemSave", {
  RTTI_PROPERTY(index);
  RTTI_PROPERTY(level);
  RTTI_PROPERTY(location);
  RTTI_PROPERTY(playthrough);
  RTTI_PROPERTY(modded);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemCampaignNative, "TandemCampaignNative", {
  RTTI_METHOD(SavesReset);
  RTTI_METHOD(SaveSeen);
  RTTI_METHOD(Loading);
  RTTI_METHOD(WorldLoaded);
  RTTI_METHOD(WorldSaved);
  RTTI_METHOD(Mark);
  RTTI_METHOD(IsCampaign);
  RTTI_METHOD(CampaignSave);
  RTTI_METHOD(LastV);
  RTTI_METHOD(TestNext);
  RTTI_METHOD(CampaignReport);
});
