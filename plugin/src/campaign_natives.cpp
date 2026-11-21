// What scripts/tandem/Campaign.reds can call: `TandemCampaignNative` static
// functions and their structs (docs/architecture/campaign.md). Declared on
// the script side in scripts/tandem/CampaignNatives.reds: keep the two in
// sync (tools/check-natives.py reads every pair). All of these run on the
// game thread but CampaignQuest and CampaignSaved, which only store (they
// are called from script callbacks: tools/scripts-threads.py).

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "campaign.hpp"
#include "log.hpp"
#include "runtime.hpp"

#include <RedLib.hpp>

#include <ctime>
#include <format>

namespace tandem::plugin {
namespace {
// A save's time as the player reads it: local, to the minute ("" none).
std::string When(std::uint64_t unix) {
  if (unix == 0)
    return {};
  const auto time = static_cast<std::time_t>(unix);
  std::tm local{};
#ifdef _WIN32
  if (localtime_s(&local, &time) != 0)
    return {};
#else
  if (!localtime_r(&time, &local))
    return {};
#endif
  return std::format("{:04}-{:02}-{:02} {:02}:{:02}", local.tm_year + 1900, local.tm_mon + 1,
                     local.tm_mday, local.tm_hour, local.tm_min);
}

Red::DynArray<std::uint64_t> Words(const std::vector<std::uint64_t> &words) {
  Red::DynArray<std::uint64_t> out;
  for (const auto word : words)
    out.EmplaceBack(word);
  return out;
}
} // namespace

// One copy of the campaign, for the menus. Plain fields only: the script VM
// aligns struct locals to 8.
struct TandemCampaignCopy {
  std::uint32_t player{}; // whose (0: none)
  Red::CString name;      // that player's name in the session
  Red::CString who;       // who played it last
  Red::CString lastQuest; // the last quest done in it
  Red::CString when;      // its newest save, local time ("": none yet)
  std::uint32_t generation{};
  std::uint64_t copy{};
  bool mine{};
};

// What the session's copies say (campaign::Verdict): kind 0 nothing, 1 the
// same, 2 one ahead (a fast-forward), 3 a fork.
struct TandemCampaignVerdict {
  std::int32_t kind{};
  bool selfBehind{};
  bool hostBehind{};
  bool hosting{};
};

// An archive in the campaigns folder, for Import campaign.
struct TandemCampaignArchive {
  Red::CString path;
  Red::CString file;
  Red::CString who;
  Red::CString lastQuest;
  Red::CString when;
  std::uint32_t generation{};
  std::int32_t missing{};    // mods its game had that ours lacks
  Red::CString missingNames; // the first of them, joined
  bool known{};              // we have that copy already
};

struct TandemCampaignNative : Red::IScriptable {
  static bool CampaignOn() { return Campaigns::Get().On(); }
  // The campaigns folder, where exports go and imports are read from.
  static Red::CString CampaignFolder() {
    return Red::CString(campaign::Utf8(Campaigns::Get().Folder()).c_str());
  }
  // A save loaded with these words: the words to keep in it now.
  static Red::DynArray<std::uint64_t> CampaignLoaded(Red::DynArray<std::uint64_t> words) {
    const std::vector<std::uint64_t> saved(words.begin(), words.end());
    return Words(Campaigns::Get().Loaded(saved, Runtime::Get().config.name));
  }
  static Red::DynArray<std::uint64_t> CampaignStart() {
    return Words(Campaigns::Get().StartNew(Runtime::Get().config.name));
  }
  static Red::DynArray<std::uint64_t> CampaignWords() { return Words(Campaigns::Get().Words()); }
  static bool CampaignWordsChanged() { return Campaigns::Get().TakeWordsChanged(); }
  static bool CampaignPlaying() { return Campaigns::Get().Running().Is(); }
  static TandemCampaignCopy CampaignRunning() {
    const auto copy = Campaigns::Get().Running();
    return {.who = Red::CString(copy.who.c_str()),
            .lastQuest = Red::CString(copy.quest.c_str()),
            .when = Red::CString(When(copy.savedUnix).c_str()),
            .generation = copy.generation,
            .copy = copy.copy,
            .mine = true};
  }
  // Once after a load: a copy elsewhere is newer (copy 0: none).
  static TandemCampaignCopy CampaignLoadWarning() {
    const auto warning = Campaigns::Get().TakeLoadWarning();
    if (!warning)
      return {};
    return {.who = Red::CString(warning->copy.who.c_str()),
            .lastQuest = Red::CString(warning->copy.quest.c_str()),
            .when = Red::CString(When(warning->copy.savedUnix).c_str()),
            .generation = warning->copy.generation,
            .copy = warning->copy.copy};
  }
  static bool CampaignLeft() { return Campaigns::Get().TakeLeft(); }
  // Any thread (a journal callback): only stores.
  static void CampaignQuest(Red::CString title) {
    Campaigns::Get().NoteQuest(std::string(title.c_str()));
  }
  // Any thread (the save callback): only stores.
  static void CampaignSaved(bool success) { Campaigns::Get().Saved(success); }
  // 1: a start's save, 2: an export's. False: one is under way, or no
  // saves folder.
  static bool CampaignAskSave(std::int32_t purpose) {
    return Campaigns::Get().AskSave(static_cast<CampaignPurpose>(purpose));
  }
  static bool CampaignImport(Red::CString path) {
    return Campaigns::Get().BeginImport(campaign::PathOf(path.c_str()));
  }
  static std::int32_t CampaignJob() { return static_cast<std::int32_t>(Campaigns::Get().Job()); }
  static std::int32_t CampaignJobPurpose() {
    return static_cast<std::int32_t>(Campaigns::Get().Purpose());
  }
  static Red::CString CampaignJobText() { return Red::CString(Campaigns::Get().JobText().c_str()); }
  static void CampaignJobSeen() { Campaigns::Get().JobSeen(); }
  static Red::DynArray<TandemCampaignArchive> CampaignArchives() {
    Red::DynArray<TandemCampaignArchive> out;
    auto &campaigns = Campaigns::Get();
    for (const auto &listed : campaigns.Archives()) {
      const auto missing = campaigns.MissingMods(listed.record);
      std::string names;
      for (std::size_t i = 0; i < missing.size() && i < 6; ++i)
        names += std::format("{}{}", i == 0 ? "" : ", ", missing[i]);
      if (missing.size() > 6)
        names += std::format(" (+{})", missing.size() - 6);
      out.EmplaceBack(
          TandemCampaignArchive{.path = Red::CString(campaign::Utf8(listed.zip).c_str()),
                                .file = Red::CString(campaign::Utf8(listed.zip.filename()).c_str()),
                                .who = Red::CString(listed.record.copy.who.c_str()),
                                .lastQuest = Red::CString(listed.record.copy.quest.c_str()),
                                .when = Red::CString(When(listed.record.copy.savedUnix).c_str()),
                                .generation = listed.record.copy.generation,
                                .missing = static_cast<std::int32_t>(missing.size()),
                                .missingNames = Red::CString(names.c_str()),
                                .known = campaigns.Known(listed.record)});
    }
    return out;
  }
  // The session's copies: its revision changes with what it tells.
  static std::uint32_t CampaignNews() { return Campaigns::Get().News(); }
  static TandemCampaignVerdict CampaignVerdict() {
    auto &campaigns = Campaigns::Get();
    const auto verdict = campaigns.Verdict();
    const auto status = Runtime::Get().Status();
    return {.kind = static_cast<std::int32_t>(verdict.kind),
            .selfBehind = verdict.selfBehind,
            .hostBehind = verdict.hostBehind,
            .hosting = status.you != 0 && status.you == status.host};
  }
  // The copies to go on from: one (a fast-forward) or several (a fork).
  static Red::DynArray<TandemCampaignCopy> CampaignHeads() {
    Red::DynArray<TandemCampaignCopy> out;
    auto &campaigns = Campaigns::Get();
    const auto you = campaigns.You();
    for (const auto &head : campaigns.Verdict().heads)
      out.EmplaceBack(TandemCampaignCopy{.player = head.player,
                                         .name = Red::CString(head.name.c_str()),
                                         .who = Red::CString(head.copy.who.c_str()),
                                         .lastQuest = Red::CString(head.copy.quest.c_str()),
                                         .when = Red::CString(When(head.copy.savedUnix).c_str()),
                                         .generation = head.copy.generation,
                                         .copy = head.copy.copy,
                                         .mine = head.player == you});
    return out;
  }
  static void CampaignChoose(std::uint64_t copy) { Campaigns::Get().Choose(copy); }
  static std::uint64_t CampaignChosen() { return Campaigns::Get().Chosen(); }
  // (tools/game.sh cmd campaign) everything, in the log.
  static void CampaignReport() {
    for (const auto &line : Campaigns::Get().Report())
      log::Info("{}", line);
  }

  RTTI_IMPL_TYPEINFO(TandemCampaignNative);
  RTTI_IMPL_ALLOCATOR();
};
} // namespace tandem::plugin

RTTI_DEFINE_CLASS(tandem::plugin::TandemCampaignCopy, "TandemCampaignCopy", {
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(name);
  RTTI_PROPERTY(who);
  RTTI_PROPERTY(lastQuest);
  RTTI_PROPERTY(when);
  RTTI_PROPERTY(generation);
  RTTI_PROPERTY(copy);
  RTTI_PROPERTY(mine);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemCampaignVerdict, "TandemCampaignVerdict", {
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(selfBehind);
  RTTI_PROPERTY(hostBehind);
  RTTI_PROPERTY(hosting);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemCampaignArchive, "TandemCampaignArchive", {
  RTTI_PROPERTY(path);
  RTTI_PROPERTY(file);
  RTTI_PROPERTY(who);
  RTTI_PROPERTY(lastQuest);
  RTTI_PROPERTY(when);
  RTTI_PROPERTY(generation);
  RTTI_PROPERTY(missing);
  RTTI_PROPERTY(missingNames);
  RTTI_PROPERTY(known);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemCampaignNative, "TandemCampaignNative", {
  RTTI_METHOD(CampaignOn);
  RTTI_METHOD(CampaignFolder);
  RTTI_METHOD(CampaignLoaded);
  RTTI_METHOD(CampaignStart);
  RTTI_METHOD(CampaignWords);
  RTTI_METHOD(CampaignWordsChanged);
  RTTI_METHOD(CampaignPlaying);
  RTTI_METHOD(CampaignRunning);
  RTTI_METHOD(CampaignLoadWarning);
  RTTI_METHOD(CampaignLeft);
  RTTI_METHOD(CampaignQuest);
  RTTI_METHOD(CampaignSaved);
  RTTI_METHOD(CampaignAskSave);
  RTTI_METHOD(CampaignImport);
  RTTI_METHOD(CampaignJob);
  RTTI_METHOD(CampaignJobPurpose);
  RTTI_METHOD(CampaignJobText);
  RTTI_METHOD(CampaignJobSeen);
  RTTI_METHOD(CampaignArchives);
  RTTI_METHOD(CampaignNews);
  RTTI_METHOD(CampaignVerdict);
  RTTI_METHOD(CampaignHeads);
  RTTI_METHOD(CampaignChoose);
  RTTI_METHOD(CampaignChosen);
  RTTI_METHOD(CampaignReport);
});
