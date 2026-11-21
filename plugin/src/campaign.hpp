#pragma once

// A shared campaign in the plugin (docs/architecture/campaign.md): the copy
// this game plays, the records of tandem's campaigns folder, the other
// players' copies heard in a session and the verdict drawn from them, and
// the file work of an export or an import, done on a thread of its own
// (filesystem only, never the engine). The rules are core's
// (tandem/campaign.hpp); scripts/tandem/Campaign.reds asks through
// campaign_natives.cpp.
//
// Threads: the game thread (natives), the net thread (TakeReport, the
// session's events), our worker (files), and whatever thread the game's
// save callback comes on (Saved, which only stores).

#include "tandem/campaign.hpp"
#include "tandem/endpoint.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace tandem::plugin {
// What an export, a start or an import is doing (Campaign.reds polls it).
enum class CampaignJob : std::int32_t {
  Idle = 0,
  Saving = 1,  // waiting for the game's save
  Writing = 2, // the zip, or the new slot
  Done = 3,
  Failed = 4,
};
enum class CampaignPurpose : std::int32_t { None = 0, Start = 1, Export = 2, Import = 3 };

class Campaigns {
public:
  static Campaigns &Get();
  ~Campaigns();

  // main.cpp, at load: on or off (tandem.ini `campaign`), tandem's folder
  // for archives and records, the game's saves folder ("": not found), our
  // other mods (recorded in an export).
  void Configure(bool on, std::filesystem::path folder, std::filesystem::path saves,
                 std::vector<mods::Mod> ours);
  bool On() const { return m_on; }
  std::filesystem::path Folder() const { return m_folder; }
  void Stop();

  // ---- Game thread ----
  // A save loaded with these words (its persistent field): the words of the
  // copy this play is (a child of the loaded one), or the same words (not a
  // campaign, or campaign = off), or none (a copy set aside: a normal save
  // from now on).
  std::vector<std::uint64_t> Loaded(std::span<const std::uint64_t> words, std::string who);
  // This save starts a campaign: its first copy's words.
  std::vector<std::uint64_t> StartNew(std::string who);
  // The words of the copy we play now; true once after they changed (an export).
  std::vector<std::uint64_t> Words();
  bool TakeWordsChanged();
  campaign::Copy Running();
  // A copy elsewhere is newer than the one just loaded (told once).
  std::optional<campaign::Record> TakeLoadWarning();
  // The copy we play left the campaign at its load (set aside).
  bool TakeLeft();

  // Any thread: the last quest done (ignored while a guest in a session:
  // that is the host's story), and the game's word on a save.
  void NoteQuest(std::string title);
  void Saved(bool success);

  // An export (or a start) waits for the save the script asks for now;
  // an import installs `zip` as a new slot. False: another is under way.
  bool AskSave(CampaignPurpose purpose);
  bool BeginImport(const std::filesystem::path &zip);
  CampaignJob Job() const { return m_job.load(); }
  CampaignPurpose Purpose() const { return m_purpose.load(); }
  std::string JobText();
  void JobSeen();

  std::vector<campaign::Listed> Archives();
  std::vector<std::string> MissingMods(const campaign::Record &record);
  bool Known(const campaign::Record &record);

  // The session: the verdict (its revision changes with what it tells),
  // and the players' choice.
  campaign::Verdict Verdict();
  std::uint32_t News() const { return m_news.load(); }
  proto::PlayerId You();
  void Choose(std::uint64_t copy);
  std::uint64_t Chosen();
  std::map<proto::PlayerId, std::uint64_t> ChosenByOthers();
  std::vector<std::string> Report();

  // ---- Net thread ----
  void Joined(proto::PlayerId you, proto::PlayerId host);
  void OnRemote(const events::RemoteCampaign &remote);
  void Left(proto::PlayerId player);
  void Ended();
  // Our copy to say, when it changed since `seen` (a new session: seen 0).
  std::optional<proto::Campaign> TakeReport(std::uint64_t &seen);

private:
  void Work(std::stop_token stop);
  void Changed(); // under m_mutex: our report and the verdict
  proto::Campaign ReportLocked() const;
  void JudgeLocked();
  void Put(const campaign::Record &record); // under m_mutex: kept, written by the worker
  std::uint64_t NewId();

  bool m_on{};
  std::filesystem::path m_folder, m_lineage, m_saves;
  std::vector<mods::Mod> m_mods;

  mutable std::mutex m_mutex;
  std::condition_variable_any m_wake;
  std::vector<campaign::Record> m_records;
  std::vector<campaign::Record> m_toWrite; // for the worker
  campaign::Copy m_running;
  std::string m_who; // our player's name, who plays m_running
  std::string m_lastQuest;
  bool m_wordsChanged{}, m_left{};
  std::optional<campaign::Record> m_loadWarning;
  // The newest save seen (its sav.dat's time) while we play m_running.
  std::filesystem::file_time_type m_seenSave{};
  bool m_seenKnown{};
  // A job.
  std::atomic<CampaignJob> m_job{CampaignJob::Idle};
  std::atomic<CampaignPurpose> m_purpose{CampaignPurpose::None};
  std::filesystem::file_time_type m_askedAt{};
  std::uint64_t m_askedUnix{};
  std::filesystem::path m_importZip;
  std::string m_jobText;
  std::atomic<int> m_saved{-1};    // the game's word: 1 saved, 0 failed, -1 none yet
  std::atomic<bool> m_kick{false}; // the worker has something to do
  // The session.
  proto::PlayerId m_you{}, m_host{};
  std::map<proto::PlayerId, campaign::Member> m_members;
  std::uint64_t m_hostCampaign{};
  std::uint64_t m_chosen{};
  campaign::Verdict m_verdict;
  std::atomic<std::uint32_t> m_news{0};
  std::atomic<std::uint64_t> m_revision{1};

  std::jthread m_worker;
};
} // namespace tandem::plugin
