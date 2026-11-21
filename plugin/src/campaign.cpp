// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "campaign.hpp"

#include "log.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <random>
#include <utility>

namespace tandem::plugin {
namespace {
namespace fs = std::filesystem;
using namespace std::chrono_literals;

std::uint64_t NowUnix() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count());
}

// A save's files stop changing: its size and time the same twice, a second
// apart (at most `tries` seconds).
bool Settled(const fs::path &slot, int tries) {
  std::error_code ec;
  auto size = fs::file_size(slot / campaign::kSaveFile, ec);
  auto time = fs::last_write_time(slot / campaign::kSaveFile, ec);
  for (int i = 0; i < tries; ++i) {
    std::this_thread::sleep_for(1s);
    const auto nextSize = fs::file_size(slot / campaign::kSaveFile, ec);
    const auto nextTime = fs::last_write_time(slot / campaign::kSaveFile, ec);
    if (!ec && nextSize == size && nextTime == time)
      return true;
    size = nextSize;
    time = nextTime;
  }
  return false;
}
} // namespace

Campaigns &Campaigns::Get() {
  static Campaigns campaigns;
  return campaigns;
}

// At exit without Unload (the process going): never wait for the worker
// under the loader's lock.
Campaigns::~Campaigns() {
  if (m_worker.joinable()) {
    m_worker.request_stop();
    m_worker.detach();
  }
}

void Campaigns::Configure(bool on, fs::path folder, fs::path saves, std::vector<mods::Mod> ours) {
  {
    std::scoped_lock lock(m_mutex);
    m_on = on;
    m_folder = std::move(folder);
    m_lineage = m_folder / "lineage";
    m_saves = std::move(saves);
    m_mods = std::move(ours);
    m_records = campaign::LoadRecords(m_lineage);
  }
  if (!on) {
    log::Info("campaign: off (tandem.ini)");
    return;
  }
  log::Info("campaign: {} copies known, archives in {}, saves {}", m_records.size(),
            campaign::Utf8(m_folder), m_saves.empty() ? "not found" : campaign::Utf8(m_saves));
  m_worker = std::jthread([this](std::stop_token stop) { Work(stop); });
}

void Campaigns::Stop() {
  if (m_worker.joinable()) {
    m_worker.request_stop();
    m_wake.notify_all();
    m_worker.join();
  }
}

std::uint64_t Campaigns::NewId() {
  static std::mt19937_64 random{
      std::random_device{}() ^
      static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())};
  std::uint64_t id = 0;
  while (id == 0)
    id = random();
  return id;
}

// ---- Game thread ----

std::vector<std::uint64_t> Campaigns::Loaded(std::span<const std::uint64_t> words,
                                             std::string who) {
  std::scoped_lock lock(m_mutex);
  m_who = campaign::Clean(who);
  m_left = false;
  m_wordsChanged = false;
  m_loadWarning.reset();
  m_lastQuest.clear();
  m_seenSave = fs::file_time_type::clock::now();
  m_seenKnown = true;
  std::vector<std::uint64_t> same(words.begin(), words.end());
  auto copy = m_on ? campaign::Unpack(words) : std::nullopt;
  if (!copy) {
    m_running = {};
    Changed();
    return same;
  }
  const auto record = campaign::Find(m_records, copy->campaign, copy->copy);
  if (record) {
    copy->savedUnix = record->copy.savedUnix;
    copy->who = record->copy.who;
    copy->quest = record->copy.quest;
  }
  if (record && record->setAside) {
    // The players went on from another copy: this one is a normal save now.
    log::Info("campaign: copy {:016x} was set aside: this save leaves the campaign", copy->copy);
    m_running = {};
    m_left = true;
    Changed();
    return {};
  }
  m_loadWarning = campaign::NewerElsewhere(m_records, *copy);
  m_running = campaign::Advance(*copy, NewId());
  m_running.who = m_who;
  log::Info("campaign: loaded copy {:016x} (generation {}) of campaign {:016x}: playing it as "
            "copy {:016x}{}",
            copy->copy, copy->generation, copy->campaign, m_running.copy,
            m_loadWarning ? std::format(" ({}'s copy is newer)", m_loadWarning->copy.who) : "");
  Changed();
  return campaign::Pack(m_running);
}

std::vector<std::uint64_t> Campaigns::StartNew(std::string who) {
  std::scoped_lock lock(m_mutex);
  if (!m_on)
    return {};
  m_who = campaign::Clean(who);
  m_running = campaign::Start(NewId(), NewId(), m_who, NowUnix());
  m_left = false;
  log::Info("campaign: a new campaign {:016x}, copy {:016x}", m_running.campaign, m_running.copy);
  Changed();
  return campaign::Pack(m_running);
}

std::vector<std::uint64_t> Campaigns::Words() {
  std::scoped_lock lock(m_mutex);
  return campaign::Pack(m_running);
}

bool Campaigns::TakeWordsChanged() {
  std::scoped_lock lock(m_mutex);
  return std::exchange(m_wordsChanged, false);
}

campaign::Copy Campaigns::Running() {
  std::scoped_lock lock(m_mutex);
  return m_running;
}

std::optional<campaign::Record> Campaigns::TakeLoadWarning() {
  std::scoped_lock lock(m_mutex);
  return std::exchange(m_loadWarning, std::nullopt);
}

bool Campaigns::TakeLeft() {
  std::scoped_lock lock(m_mutex);
  return std::exchange(m_left, false);
}

void Campaigns::NoteQuest(std::string title) {
  std::scoped_lock lock(m_mutex);
  // A guest's journal shows the host's story: not our copy's.
  if (m_you != proto::kNoPlayer && m_you != m_host)
    return;
  m_lastQuest = campaign::Clean(title);
}

void Campaigns::Saved(bool success) {
  m_saved.store(success ? 1 : 0);
  m_kick.store(true);
  m_wake.notify_all();
}

bool Campaigns::AskSave(CampaignPurpose purpose) {
  std::scoped_lock lock(m_mutex);
  const auto job = m_job.load();
  if (!m_on || job == CampaignJob::Saving || job == CampaignJob::Writing || m_saves.empty() ||
      !m_running.Is())
    return false;
  m_purpose.store(purpose);
  m_askedAt = fs::file_time_type::clock::now() - 2s;
  m_askedUnix = NowUnix();
  m_saved.store(-1);
  m_jobText.clear();
  m_job.store(CampaignJob::Saving);
  m_kick.store(true);
  m_wake.notify_all();
  return true;
}

bool Campaigns::BeginImport(const fs::path &zip) {
  std::scoped_lock lock(m_mutex);
  const auto job = m_job.load();
  if (!m_on || job == CampaignJob::Saving || job == CampaignJob::Writing || m_saves.empty())
    return false;
  m_purpose.store(CampaignPurpose::Import);
  m_importZip = zip;
  m_jobText.clear();
  m_job.store(CampaignJob::Writing);
  m_kick.store(true);
  m_wake.notify_all();
  return true;
}

std::string Campaigns::JobText() {
  std::scoped_lock lock(m_mutex);
  return m_jobText;
}

void Campaigns::JobSeen() {
  auto job = m_job.load();
  if (job == CampaignJob::Done || job == CampaignJob::Failed)
    m_job.compare_exchange_strong(job, CampaignJob::Idle);
}

std::vector<campaign::Listed> Campaigns::Archives() {
  return m_on ? campaign::ListArchives(m_folder) : std::vector<campaign::Listed>{};
}

std::vector<std::string> Campaigns::MissingMods(const campaign::Record &record) {
  std::scoped_lock lock(m_mutex);
  return campaign::MissingMods(record.mods, m_mods);
}

bool Campaigns::Known(const campaign::Record &record) {
  std::scoped_lock lock(m_mutex);
  const auto found = campaign::Find(m_records, record.copy.campaign, record.copy.copy);
  return found && !found->foreign;
}

campaign::Verdict Campaigns::Verdict() {
  std::scoped_lock lock(m_mutex);
  return m_verdict;
}

proto::PlayerId Campaigns::You() {
  std::scoped_lock lock(m_mutex);
  return m_you;
}

void Campaigns::Choose(std::uint64_t copy) {
  std::scoped_lock lock(m_mutex);
  m_chosen = copy;
  // The heads not chosen are set aside: ours a normal save from its next
  // load, theirs no longer offered.
  for (const auto &head : m_verdict.heads) {
    if (head.copy.copy == copy)
      continue;
    auto record =
        campaign::Find(m_records, head.copy.campaign, head.copy.copy)
            .value_or(campaign::Record{.copy = head.copy, .foreign = head.player != m_you});
    record.setAside = true;
    Put(record);
  }
  log::Info("campaign: we go on from copy {:016x}", copy);
  Changed();
}

std::uint64_t Campaigns::Chosen() {
  std::scoped_lock lock(m_mutex);
  return m_chosen;
}

std::map<proto::PlayerId, std::uint64_t> Campaigns::ChosenByOthers() {
  std::scoped_lock lock(m_mutex);
  std::map<proto::PlayerId, std::uint64_t> out;
  for (const auto &[player, member] : m_members)
    if (member.chosen != 0)
      out[player] = member.chosen;
  return out;
}

std::vector<std::string> Campaigns::Report() {
  std::scoped_lock lock(m_mutex);
  std::vector<std::string> lines;
  lines.push_back(std::format("campaign: {}, {} copies known, playing {}", m_on ? "on" : "off",
                              m_records.size(),
                              m_running.Is() ? std::format("copy {:016x} (generation {}) of "
                                                           "campaign {:016x}, after \"{}\"",
                                                           m_running.copy, m_running.generation,
                                                           m_running.campaign, m_running.quest)
                                             : std::string("no campaign")));
  for (const auto &[player, member] : m_members)
    lines.push_back(std::format(
        "campaign: {} (player {}) has copy {:016x}, generation {}: ours "
        "{}",
        member.name, player, member.copy.copy, member.copy.generation,
        campaign::NameOf(campaign::Relate(campaign::FromWire(ReportLocked()), member.copy))));
  lines.push_back(std::format("campaign: verdict {}, {} head(s){}{}",
                              static_cast<int>(m_verdict.kind), m_verdict.heads.size(),
                              m_verdict.selfBehind ? ", ours behind" : "",
                              m_verdict.hostBehind ? ", the host's behind" : ""));
  lines.push_back(std::format("campaign: job {} ({}) {}", static_cast<int>(m_job.load()),
                              static_cast<int>(m_purpose.load()), m_jobText));
  return lines;
}

// ---- Net thread ----

void Campaigns::Joined(proto::PlayerId you, proto::PlayerId host) {
  std::scoped_lock lock(m_mutex);
  // Records put in the folder since (an import by hand, a test's): ours too.
  for (auto &record : campaign::LoadRecords(m_lineage))
    if (!campaign::Find(m_records, record.copy.campaign, record.copy.copy))
      m_records.push_back(std::move(record));
  m_you = you;
  m_host = host;
  m_members.clear();
  m_hostCampaign = 0;
  m_chosen = 0;
  Changed();
}

void Campaigns::OnRemote(const events::RemoteCampaign &remote) {
  std::scoped_lock lock(m_mutex);
  const auto copy = campaign::FromWire(remote.campaign);
  if (remote.player == m_host)
    m_hostCampaign = copy.campaign;
  if (!copy.Is()) {
    m_members.erase(remote.player);
  } else {
    m_members[remote.player] = {.player = remote.player,
                                .name = remote.name,
                                .copy = copy,
                                .chosen = remote.campaign.chosen};
    // Kept for the next time we host this campaign (an older copy is told
    // before anyone joins): only of a campaign we have, never over our own.
    const auto ours = campaign::NewestOf(m_records, copy.campaign);
    const auto known = campaign::Find(m_records, copy.campaign, copy.copy);
    if ((ours || copy.campaign == m_running.campaign) && (!known || known->foreign)) {
      auto record = known.value_or(campaign::Record{.foreign = true});
      record.copy = copy;
      record.foreign = true;
      Put(record);
    }
  }
  log::Info("campaign: {} (player {}) has {}", remote.name, remote.player,
            copy.Is() ? std::format("copy {:016x} (generation {}) of campaign {:016x}", copy.copy,
                                    copy.generation, copy.campaign)
                      : std::string("no copy"));
  Changed();
}

void Campaigns::Left(proto::PlayerId player) {
  std::scoped_lock lock(m_mutex);
  if (m_members.erase(player) > 0)
    Changed();
}

void Campaigns::Ended() {
  std::scoped_lock lock(m_mutex);
  m_you = m_host = proto::kNoPlayer;
  m_members.clear();
  m_hostCampaign = 0;
  m_chosen = 0;
  Changed();
}

std::optional<proto::Campaign> Campaigns::TakeReport(std::uint64_t &seen) {
  const auto revision = m_revision.load();
  if (revision == seen)
    return std::nullopt;
  seen = revision;
  std::scoped_lock lock(m_mutex);
  return ReportLocked();
}

// ---- Under m_mutex ----

void Campaigns::Changed() {
  ++m_revision;
  JudgeLocked();
}

proto::Campaign Campaigns::ReportLocked() const {
  proto::Campaign report;
  if (m_running.Is()) {
    report = campaign::ToWire(m_running);
    report.flags = proto::kCampaignPlaying;
  } else if (m_you != proto::kNoPlayer && m_you != m_host && m_hostCampaign != 0) {
    // A guest with its own V: its newest copy of the host's campaign.
    if (const auto newest = campaign::NewestOf(m_records, m_hostCampaign))
      report = campaign::ToWire(newest->copy);
  }
  if (report.campaign != 0)
    report.chosen = m_chosen;
  return report;
}

void Campaigns::JudgeLocked() {
  std::vector<campaign::Member> members;
  if (const auto ours = campaign::FromWire(ReportLocked()); ours.Is() && m_you != 0)
    members.push_back({.player = m_you, .name = m_who, .copy = ours, .chosen = m_chosen});
  for (const auto &[player, member] : m_members)
    members.push_back(member);
  auto verdict = campaign::Judge(members, m_you, m_host);
  if (!campaign::SameNews(verdict, m_verdict)) {
    ++m_news;
    if (verdict.kind != campaign::Verdict::Kind::None)
      log::Info("campaign: the session's copies: {} ({} head(s)){}{}",
                verdict.kind == campaign::Verdict::Kind::Same          ? "the same"
                : verdict.kind == campaign::Verdict::Kind::FastForward ? "one ahead"
                                                                       : "a fork",
                verdict.heads.size(), verdict.selfBehind ? "; ours is behind" : "",
                verdict.hostBehind ? "; the host's is behind" : "");
  }
  m_verdict = std::move(verdict);
}

void Campaigns::Put(const campaign::Record &record) {
  const auto found = std::ranges::find_if(m_records, [&](const campaign::Record &r) {
    return r.copy.campaign == record.copy.campaign && r.copy.copy == record.copy.copy;
  });
  if (found != m_records.end())
    *found = record;
  else
    m_records.push_back(record);
  m_toWrite.push_back(record);
  m_kick.store(true);
  m_wake.notify_all();
}

// ---- The worker: files only ----

void Campaigns::Work(std::stop_token stop) {
  std::unique_lock lock(m_mutex);
  while (!stop.stop_requested()) {
    const auto job = m_job.load();
    const bool busy = job == CampaignJob::Saving || job == CampaignJob::Writing;
    m_wake.wait_for(lock, stop, std::chrono::milliseconds(busy ? 500 : 5000),
                    [&] { return m_kick.load(); });
    m_kick.store(false);
    if (stop.stop_requested())
      break;
    auto writes = std::exchange(m_toWrite, {});
    const auto running = m_running;
    const auto since = m_seenSave + fs::file_time_type::duration{1};
    const bool watch = m_seenKnown && running.Is() && !m_saves.empty();
    lock.unlock();

    for (const auto &record : writes) {
      std::string error;
      if (!campaign::SaveRecord(m_lineage, record, error))
        log::Warn("campaign: {}", error);
    }

    // A new save of the copy we play: its record says when, where and after what.
    std::string slot;
    fs::file_time_type written{};
    if (watch) {
      slot = campaign::NewestSlot(m_saves, since);
      std::error_code ec;
      if (!slot.empty())
        written = fs::last_write_time(m_saves / slot / campaign::kSaveFile, ec);
    }
    lock.lock();
    std::optional<campaign::Record> saved;
    if (!slot.empty() && m_running.Is() && m_running.copy == running.copy) {
      m_seenSave = written;
      m_running.savedUnix = NowUnix();
      if (!m_lastQuest.empty())
        m_running.quest = m_lastQuest;
      auto record = campaign::Find(m_records, m_running.campaign, m_running.copy)
                        .value_or(campaign::Record{});
      record.copy = m_running;
      record.slot = slot;
      record.foreign = false;
      Put(record);
      saved = record;
      log::Info("campaign: copy {:016x} saved in {}", m_running.copy, slot);
      Changed();
    }

    const auto purpose = m_purpose.load();
    if (m_job.load() == CampaignJob::Saving) {
      if (saved && written >= m_askedAt) {
        m_job.store(CampaignJob::Writing);
        if (purpose == CampaignPurpose::Start) {
          m_jobText = slot;
          m_job.store(CampaignJob::Done);
        } else {
          // The export: the save as it settled, then the zip; this game
          // goes on as a child of the copy exported.
          auto record = *saved;
          record.mods = m_mods;
          lock.unlock();
          const bool settled = Settled(m_saves / slot, 10);
          const auto exported = campaign::Export(m_saves / slot, record, m_folder, NowUnix());
          lock.lock();
          if (!exported.zip.empty()) {
            m_jobText = campaign::Utf8(exported.zip);
            if (m_running.copy == record.copy.copy) {
              m_running = campaign::Advance(m_running, NewId());
              m_wordsChanged = true;
              Changed();
            }
            log::Info("campaign: exported copy {:016x} to {}{}", record.copy.copy, m_jobText,
                      settled ? "" : " (the save was still being written)");
            m_job.store(CampaignJob::Done);
          } else {
            m_jobText = exported.error;
            log::Warn("campaign: export failed: {}", exported.error);
            m_job.store(CampaignJob::Failed);
          }
        }
      } else if (m_saved.load() == 0 || NowUnix() > m_askedUnix + 60) {
        m_jobText = m_saved.load() == 0 ? "the game did not save" : "no save came";
        log::Warn("campaign: {}", m_jobText);
        m_job.store(CampaignJob::Failed);
      }
    } else if (m_job.load() == CampaignJob::Writing && purpose == CampaignPurpose::Import) {
      const auto zip = m_importZip;
      lock.unlock();
      const auto installed = campaign::Import(zip, m_saves);
      lock.lock();
      if (!installed.slot.empty()) {
        Put(installed.record);
        m_jobText = installed.slot;
        log::Info("campaign: {} installed as the new save {}", campaign::Utf8(zip.filename()),
                  installed.slot);
        m_job.store(CampaignJob::Done);
      } else {
        m_jobText = installed.error;
        log::Warn("campaign: import of {} failed: {}", campaign::Utf8(zip.filename()),
                  installed.error);
        m_job.store(CampaignJob::Failed);
      }
    }
  }
}
} // namespace tandem::plugin
