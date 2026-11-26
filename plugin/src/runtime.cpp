// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "comms.hpp"
#include "degradations.hpp"
#include "installed.hpp"
#include "log.hpp"
#include "net_thread.hpp"
#include "observer.hpp"
#include "perf.hpp"
#include "recording.hpp"
#include "runtime.hpp"

#include "presence.hpp"
#include "story.hpp"

#include "tandem/names.hpp"
#include "tandem/observer.hpp"
#include "tandem/report.hpp"
#include "tandem/session.hpp"
#include "tandem/settings.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

namespace tandem::plugin {
Runtime &Runtime::Get() {
  static Runtime runtime;
  return runtime;
}

void Runtime::Start(proto::Body body) {
  bool guest = false;
  {
    std::lock_guard _(m_lock);
    if (m_net || m_choice.role == Config::Role::Off)
      return;
    guest = m_choice.role == Config::Role::Join;
  }
  // Outside our lock: the recorder's solo drain takes it under its own.
  // (Start and Stop only run on the game thread.)
  Recording::Get().SessionStarting(guest);
  std::lock_guard _(m_lock);
  if (m_net)
    return;
  // The net thread runs on its own copy: tandem.ini's settings, the chosen session.
  auto session = config;
  session.role = m_choice.role;
  session.address = m_choice.address;
  session.port = m_choice.port;
  session.joinCode = m_choice.joinCode;
  session.addressSet = m_choice.addressSet;
  m_body = body;
  m_status = {.state = SessionStatus::State::Starting};
  std::string what;
  if (session.role == Config::Role::Host)
    what = std::format(" on UDP {}", session.port);
  else if (!session.joinCode.empty())
    what = std::format(" of code {}", net::DisplayJoinCode(session.joinCode));
  else
    what = std::format(" of {}:{}", session.address, session.port);
  log::Info("session: starting as {}{}", NameOf(session.role), what);
  m_net = std::make_unique<NetThread>(session, body);
  Observer::Get().SessionStarted(std::format("{}{}", NameOf(session.role), what));
}

void Runtime::Choose(SessionChoice choice) {
  if (Started()) {
    log::Info("session: stopping for the new choice");
    Stop();
  }
  // Nothing to choose while a startup problem keeps co-op off: the menu
  // says why (CompatProblems), and the game stays the single-player one.
  if (choice.role != Config::Role::Off && Blocked()) {
    log::Warn("session: {} not chosen, co-op is off: {}", NameOf(choice.role), Problems().front());
    if (!Degradations::Get().Local().CoreOff().empty())
      m_coreRefused = true;
    choice = {.fromMenu = choice.fromMenu};
  }
  std::lock_guard _(m_lock);
  m_choice = std::move(choice);
  std::string what;
  if (m_choice.role == Config::Role::Join && !m_choice.joinCode.empty())
    what = std::format(" code {}", net::DisplayJoinCode(m_choice.joinCode));
  else if (m_choice.role == Config::Role::Join)
    what = std::format(" {}:{}", m_choice.address, m_choice.port);
  else if (m_choice.role == Config::Role::Host)
    what = std::format(" on UDP {}", m_choice.port);
  log::Info("session: chosen {}{}{}", NameOf(m_choice.role), what,
            m_choice.fromMenu ? " (co-op menu)" : " (tandem.ini)");
}

std::vector<std::string> Runtime::Problems() const {
  auto problems = installation.startup.problems;
  if (auto expansion = compat::ExpansionProblem(Expansion()); !expansion.empty())
    problems.push_back(std::move(expansion));
  // A core feature this game turned off: co-op would not be right.
  for (const auto &entry : Degradations::Get().Local().CoreOff())
    problems.push_back(degraded::CoreProblem(entry));
  return problems;
}

void Runtime::RecheckChoice() {
  if (Started())
    return;
  const auto choice = Choice();
  if (choice.role != Config::Role::Off && Blocked())
    Choose(choice);
}

void Runtime::SetExpansion(compat::Expansion state) {
  if (m_expansion.exchange(static_cast<std::int32_t>(state)) == static_cast<std::int32_t>(state))
    return;
  constexpr const char *names[] = {"not known", "enabled", "owned, not installed or not enabled",
                                   "not owned"};
  const auto index = static_cast<std::size_t>(state);
  log::Info("Phantom Liberty: {}", index < std::size(names) ? names[index] : "?");
  const auto problem = compat::ExpansionProblem(state);
  if (problem.empty())
    return;
  log::Error("startup check: {}", problem);
  if (!config.compat) {
    log::Warn("startup check: ignored (compat = off in tandem.ini)");
    return;
  }
  // A session tandem.ini or the menu had chosen before the game said so.
  const auto choice = Choice();
  if (choice.role != Config::Role::Off)
    Choose({.fromMenu = choice.fromMenu});
}

void Runtime::NoteDifference(std::string line) {
  std::lock_guard _(m_compatLock);
  if (std::ranges::find(m_differences, line) == m_differences.end() && m_differences.size() < 32)
    m_differences.push_back(std::move(line));
}

void Runtime::ClearDifferences() {
  std::lock_guard _(m_compatLock);
  m_differences.clear();
}

std::vector<std::string> Runtime::Differences() const {
  std::lock_guard _(m_compatLock);
  return m_differences;
}

std::string Runtime::WriteReport(std::string &error) {
  if (installation.game.empty()) {
    error = "the game's folder is not known";
    return {};
  }
  const auto result =
      report::Write({.game = installation.game,
                     .out = installation.game / "red4ext" / "plugins" / "tandem" / "reports",
                     .versions = VersionsText(installation.facts, installation.startup, Expansion(),
                                              installation.install, Differences())});
  if (result.zip.empty()) {
    error = result.error;
    log::Warn("report: {}", error);
    return {};
  }
  std::string names;
  for (const auto &name : result.included)
    names += names.empty() ? name : ", " + name;
  // The path as the player finds it, from the game's folder.
  const auto shown = result.zip.lexically_relative(installation.game).generic_string();
  log::Info("report: written to {} ({})", shown, names);
  return shown;
}

void Runtime::ResetChoice() {
  Choose({.role = config.role,
          .address = config.address,
          .port = config.port,
          .joinCode = config.joinCode,
          .addressSet = config.addressSet});
}

SessionChoice Runtime::Choice() const {
  std::lock_guard _(m_lock);
  return m_choice;
}

void Runtime::ChooseArrival() {
  std::lock_guard _(m_lock);
  if (m_choice.role != Config::Role::Join)
    return;
  m_choice.arrive = true;
  log::Info("session: the join arrives beside the host");
}

bool Runtime::Arriving() const {
  std::lock_guard _(m_lock);
  return m_choice.arrive;
}

bool Runtime::TakeArrival() {
  std::lock_guard _(m_lock);
  return std::exchange(m_choice.arrive, false);
}

Config::Role Runtime::ChosenRole() const {
  std::lock_guard _(m_lock);
  return m_choice.role;
}

void Runtime::SetStatus(SessionStatus status) {
  std::lock_guard _(m_lock);
  // Not after Stop took the thread away: its last words are not the state.
  if (m_net)
    m_status = std::move(status);
}

SessionStatus Runtime::Status() const {
  std::lock_guard _(m_lock);
  return m_net ? m_status : SessionStatus{};
}

std::vector<hosts::Recent> Runtime::RecentHosts() const {
  std::lock_guard _(m_recentLock);
  if (config.recentFile.empty())
    return {};
  std::ifstream file(config.recentFile);
  std::stringstream text;
  text << file.rdbuf();
  return hosts::ParseRecent(text.str());
}

void Runtime::RememberHost(const hosts::Target &target, std::string name) {
  auto list = RecentHosts();
  hosts::Remember(list, target, std::move(name));
  std::lock_guard _(m_recentLock);
  if (config.recentFile.empty())
    return;
  std::ofstream file(config.recentFile, std::ios::trunc);
  file << hosts::FormatRecent(list);
  if (!file)
    log::Warn("could not write {}", config.recentFile.filename().string());
}

void Runtime::SetRelay(std::string code, std::string status) {
  std::lock_guard _(m_lock);
  m_relayCode = std::move(code);
  m_relayStatus = std::move(status);
}

std::string Runtime::RelayCode() const {
  std::lock_guard _(m_lock);
  return m_relayCode;
}

std::string Runtime::RelayStatus() const {
  std::lock_guard _(m_lock);
  return m_relayStatus;
}

void Runtime::SetRelayDetails(RelayView view) {
  std::lock_guard _(m_lock);
  m_relayView = std::move(view);
}

Runtime::RelayView Runtime::RelayDetails() const {
  std::lock_guard _(m_lock);
  return m_relayView;
}

bool Runtime::JoinByCode(std::string_view text) {
  const auto code = net::NormalizeJoinCode(text);
  if (!code) {
    log::Warn("join by code: {} is not a join code", text);
    return false;
  }
  if (config.relay.empty()) {
    log::Warn("join by code: no relay in tandem.ini");
    return false;
  }
  log::Info("join by code {} through relay {}:{}", net::DisplayJoinCode(*code), config.relay,
            config.relayPort);
  bool running = false;
  proto::Body body{};
  {
    std::lock_guard _(m_lock);
    running = m_net != nullptr;
    body = m_body;
  }
  // The code's host, not tandem.ini's address. Choose stops a running
  // session; it is started again at once for the code.
  Choose({.role = Config::Role::Join,
          .port = config.port,
          .joinCode = *code,
          .addressSet = false,
          .fromMenu = true});
  if (running)
    Start(body);
  return true;
}

namespace {
// What a running session was set up with: changed for the next one.
bool ForTheNextSession(std::string_view key) {
  return key == "name" || key == "relay" || key == "relay_token" || key == "story" ||
         key == "time_host" || key == "code";
}
} // namespace

bool Runtime::ChangeSetting(const std::string &key, const std::string &value) {
  if (!settings::IsEditable(key)) {
    log::Warn("setting: {} is not one the settings page changes", key);
    return false;
  }
  auto saved = config;
  for (const auto &[k, v] : m_pendingSettings)
    ApplySetting(saved, k, v, 0);
  if (!ApplySetting(saved, key, value, 0))
    return false;
  std::string text;
  if (std::ifstream in(config.file, std::ios::binary); in) {
    std::stringstream s;
    s << in.rdbuf();
    text = s.str();
  }
  // Saved as it applies: a name trimmed of its spaces.
  const auto stored = key == "name" ? names::Clean(value).value_or(value) : value;
  const auto updated = settings::SetKey(text, key, stored);
  if (!updated) {
    log::Warn("setting: {} cannot hold that value", key);
    return false;
  }
  const bool later = ForTheNextSession(key) && Started();
  if (later)
    m_pendingSettings.emplace_back(key, stored);
  else
    ApplySetting(config, key, stored, 0);
  if (key == "recorder")
    Recording::Get().SetEnabled(config.recorder);
  if (key == "direct")
    directOn = config.direct;
  if (key == "link" || key == "pings")
    ConfigureComms(config);
  if (key == "code_salt")
    codeSalt = config.codeSalt;
  const auto shown = key == "relay_token" ? std::string("(a token)") : stored;
  if (config.file.empty()) {
    log::Info("setting: {} = {} (no tandem.ini to save it in)", key, shown);
    return true;
  }
  // A whole file or none: written aside, then put in its place.
  auto aside = config.file;
  aside += ".new";
  {
    std::ofstream out(aside, std::ios::binary | std::ios::trunc);
    out << *updated;
    if (!out) {
      log::Warn("setting: could not write {}", aside.filename().string());
      return true;
    }
  }
  std::error_code error;
  std::filesystem::rename(aside, config.file, error);
  if (error)
    log::Warn("setting: could not replace {}: {}", config.file.filename().string(),
              error.message());
  else
    log::Info("setting: {} = {}{} (saved to {})", key, shown,
              later ? ", from the next session" : "", config.file.filename().string());
  return true;
}

std::string Runtime::SavedSetting(std::string_view key) const {
  // Read every frame by some scripts (nameplates): no copy unless a change waits.
  if (m_pendingSettings.empty() || !ForTheNextSession(key))
    return SettingOf(config, key);
  auto saved = config;
  for (const auto &[k, v] : m_pendingSettings)
    ApplySetting(saved, k, v, 0);
  return SettingOf(saved, key);
}

bool Runtime::NameToAsk() const { return !m_nameAsked && names::IsDefault(SavedSetting("name")); }

void Runtime::Stop() {
  for (const auto &[key, value] : m_pendingSettings)
    ApplySetting(config, key, value, 0);
  m_pendingSettings.clear();
  std::unique_ptr<NetThread> net;
  {
    std::lock_guard _(m_lock);
    net = std::move(m_net);
    m_local.reset();
    m_localPose.reset();
    m_localAppearance.reset();
    m_appearances.clear();
    m_localHeld.reset();
    m_localShots.clear();
    m_remoteShots.clear();
    m_held.clear();
    m_localSeat.reset();
    m_localVehicle.reset();
    m_seats.clear();
    m_localTransit.reset();
    m_transits.clear();
    m_localRadios.clear();
    m_localTrack = m_localTrackStartUs = 0;
    m_radios.clear();
    m_localWorld.reset();
    m_hostWorld.reset();
    m_hostWorldChanges = 0;
    m_npcOutbox.clear();
    m_npcStates.clear();
    m_npcEvents.clear();
    m_npcReplies.clear();
    m_actionEvents.clear();
    m_npcDetected.clear();
    m_awarenessOfUs.clear();
    m_localAwareness.reset();
    m_localStealth.reset();
    m_stealth.clear();
    m_localNoises.clear();
    m_noiseGate.Reset();
    m_remoteNoises.clear();
    m_sharedNpcs.clear();
    m_roster.clear();
    m_epochRequest.reset();
    m_overflows = 0;
    m_hackOutbox.clear();
    m_hackEvents.clear();
    m_lootOutbox.clear();
    m_lootInbox.clear();
    m_deviceOutbox.clear();
    m_deviceInbox.clear();
    m_breachOutbox.clear();
    m_breachInbox.clear();
    m_objectOutbox.clear();
    m_objectInbox.clear();
    m_npcAnimsIn.clear();
    m_npcShotsIn.clear();
    m_localPaused.reset();
    // Nobody pauses us any more: the change count moves so scripts let go.
    if (!m_remotePaused.empty()) {
      m_remotePaused.clear();
      ++m_remotePausedChanges;
    }
    m_names.clear();
    // What the old session said is not for the next one.
    m_events.clear();
    m_npcDetected.clear();
    m_status = {};
    m_localTime.reset();
    // Nobody bends time around us any more: the count moves so scripts let go.
    if (!m_remoteTimes.empty()) {
      m_remoteTimes.clear();
      ++m_remoteTimeChanges;
    }
    m_regroupOutbox.clear();
    m_regroupEvents.clear();
    // Counts keep going (scripts compare them with what they saw).
    m_localVitals.reset();
    if (!m_othersLives.empty()) {
      m_othersLives.clear();
      ++m_othersLivesChanges;
    }
    m_localRevives.clear();
    m_revives.clear();
  }
  const bool hadSession = net != nullptr;
  net.reset(); // joins outside the lock: the net thread takes it too
  if (hadSession) {
    Recording::Get().SessionStopped();
    Observer::Get().SessionEnded();
  }
  m_you.store(0);
  {
    std::lock_guard _(m_heldLock);
    m_heldPuppets.clear();
  }
  {
    // Anything it queued on its way out.
    std::lock_guard _(m_lock);
    m_events.clear();
  }
  {
    std::lock_guard _(m_tracksLock);
    m_tracks.clear();
    m_vehicles.clear();
  }
  std::lock_guard _(m_posesLock);
  m_poses.clear();
  m_actorPoses.clear();
  m_actorOut.clear();
  m_corpseReads.clear();
  m_corpseRead.clear();
  m_corpsePoses.clear();
  m_corpseArrivals.clear();
}

bool Runtime::Started() const {
  std::lock_guard _(m_lock);
  return m_net != nullptr;
}

void Runtime::SubmitLocal(const proto::PlayerState &state) {
  Observer::Get().Local(state.position);
  std::lock_guard _(m_lock);
  m_local = state;
}

std::optional<proto::PlayerState> Runtime::TakeLocal() {
  std::lock_guard _(m_lock);
  return std::exchange(m_local, std::nullopt);
}

void Runtime::SubmitLocalPose(const proto::PoseState &pose) {
  if (const auto loop = m_poseLoopback.load()) {
    auto looped = pose;
    looped.player = loop;
    looped.timeUs = HostNowUs();
    std::lock_guard _(m_posesLock);
    looped.seq = ++m_loopSeq;
    m_poses[loop].Push(looped);
  }
  std::lock_guard _(m_lock);
  m_poseHistory.Push(net::NowUs(), pose);
  if (!m_poseHold)
    m_localPose = pose;
}

std::optional<proto::PoseState> Runtime::TakeLocalPose() {
  std::lock_guard _(m_lock);
  if (m_poseHold) {
    // Sent again now and then: a pose is unreliable, and the stream may stop
    // reading (V's entity changes in a replacer).
    const auto now = net::NowUs();
    if (now - m_poseHoldSentUs < 100'000)
      return std::nullopt;
    m_poseHoldSentUs = now;
    return m_poseHold;
  }
  return std::exchange(m_localPose, std::nullopt);
}

void Runtime::HoldLocalPose(std::uint64_t timeUs) {
  std::lock_guard _(m_lock);
  // No pose yet: nothing to hold, the live one goes on (the others hide
  // or show the body by the flags alone).
  m_poseHold = m_poseHistory.At(timeUs);
  m_poseHoldSentUs = 0;
  m_localPose.reset();
}

void Runtime::ReleaseLocalPose() {
  std::lock_guard _(m_lock);
  m_poseHold.reset();
}

bool Runtime::LocalPoseHeld() const {
  std::lock_guard _(m_lock);
  return m_poseHold.has_value();
}

void Runtime::SubmitLocalAppearance(proto::Appearance appearance) {
  std::lock_guard _(m_lock);
  NoteStandIn(appearance);
  m_localAppearance = std::move(appearance);
}

void Runtime::NoteStandIn(const proto::Appearance &appearance) {
  if (!appearance.look.empty() && appearance.body != proto::Body::Unknown)
    m_standIns[appearance.body] = appearance.look;
}

std::vector<proto::AppearanceKey> Runtime::StandInLook(proto::Body body) const {
  std::lock_guard _(m_lock);
  const auto it = m_standIns.find(body);
  return it == m_standIns.end() ? std::vector<proto::AppearanceKey>{} : it->second;
}

std::optional<proto::Appearance> Runtime::TakeLocalAppearance() {
  std::lock_guard _(m_lock);
  return std::exchange(m_localAppearance, std::nullopt);
}

void Runtime::PushRemoteAppearance(const proto::Appearance &appearance) {
  std::lock_guard _(m_lock);
  auto &[changes, latest] = m_appearances[appearance.player];
  ++changes;
  latest = appearance;
  NoteStandIn(appearance);
}

std::optional<proto::Appearance> Runtime::RemoteAppearance(proto::PlayerId player,
                                                           std::uint32_t &changes) {
  std::lock_guard _(m_lock);
  const auto it = m_appearances.find(player);
  changes = it == m_appearances.end() ? 0 : it->second.first;
  if (it == m_appearances.end())
    return std::nullopt;
  return it->second.second;
}

void Runtime::SubmitLocalHeld(const proto::Held &held) {
  std::lock_guard _(m_lock);
  m_localHeld = held;
}

std::optional<proto::Held> Runtime::TakeLocalHeld() {
  std::lock_guard _(m_lock);
  return std::exchange(m_localHeld, std::nullopt);
}

void Runtime::SubmitLocalShot(const proto::Shot &shot) {
  std::lock_guard _(m_lock);
  if (m_localShots.size() < 64)
    m_localShots.push_back(shot);
}

std::vector<proto::Shot> Runtime::TakeLocalShots() {
  std::lock_guard _(m_lock);
  return std::exchange(m_localShots, {});
}

void Runtime::PushRemoteHeld(const proto::Held &held) {
  std::lock_guard _(m_lock);
  auto &[changes, latest] = m_held[held.player];
  ++changes;
  latest = held;
}

std::optional<proto::Held> Runtime::RemoteHeld(proto::PlayerId player, std::uint32_t &changes) {
  std::lock_guard _(m_lock);
  const auto it = m_held.find(player);
  changes = it == m_held.end() ? 0 : it->second.first;
  if (it == m_held.end())
    return std::nullopt;
  return it->second.second;
}

void Runtime::PushRemoteShot(const proto::Shot &shot) {
  std::lock_guard _(m_lock);
  // Scripts drain these every frame; a pile-up means nobody is listening.
  if (m_remoteShots.size() < 256)
    m_remoteShots.push_back({shot, net::NowUs()});
  else
    ++m_overflows;
}

std::vector<proto::Shot> Runtime::TakeRemoteShots() {
  std::lock_guard _(m_lock);
  // A shot half a second old (a loading screen, a stall) is not drawn: a
  // burst of muzzle flashes at once would be wrong.
  consistency::DropStale(m_remoteShots, net::NowUs(), 500'000);
  std::vector<proto::Shot> out;
  NoteDepth(13, m_remoteShots.size());
  for (const auto &shot : std::exchange(m_remoteShots, {}))
    out.push_back(shot.value);
  return out;
}

void Runtime::SubmitLocalSeat(const proto::Seat &seat) {
  std::lock_guard _(m_lock);
  m_localSeat = seat;
}

std::optional<proto::Seat> Runtime::TakeLocalSeat() {
  std::lock_guard _(m_lock);
  return std::exchange(m_localSeat, std::nullopt);
}

void Runtime::SubmitLocalVehicle(const proto::VehicleState &vehicle) {
  std::lock_guard _(m_lock);
  m_localVehicle = vehicle;
}

std::optional<proto::VehicleState> Runtime::TakeLocalVehicle() {
  std::lock_guard _(m_lock);
  return std::exchange(m_localVehicle, std::nullopt);
}

void Runtime::PushRemoteSeat(const proto::Seat &seat) {
  std::lock_guard _(m_lock);
  auto &[changes, latest] = m_seats[seat.player];
  ++changes;
  latest = seat;
}

std::optional<proto::Seat> Runtime::RemoteSeat(proto::PlayerId player, std::uint32_t &changes) {
  std::lock_guard _(m_lock);
  const auto it = m_seats.find(player);
  changes = it == m_seats.end() ? 0 : it->second.first;
  if (it == m_seats.end())
    return std::nullopt;
  return it->second.second;
}

void Runtime::PushLights(proto::TrafficLights lights) {
  std::lock_guard _(m_lock);
  if (m_lightsIn.size() < 64)
    m_lightsIn.push_back(std::move(lights));
}

std::vector<proto::TrafficLights> Runtime::TakeLights() {
  std::lock_guard _(m_lock);
  return std::exchange(m_lightsIn, {});
}

void Runtime::NoteLightSaid(std::uint64_t entity, std::uint8_t color) {
  std::lock_guard _(m_lightsLock);
  if (m_lightsSaid.size() < 512)
    m_lightsSaid.push_back({.entity = entity, .color = color});
}

std::vector<proto::TrafficLight> Runtime::TakeLightsSaid() {
  std::lock_guard _(m_lightsLock);
  return std::exchange(m_lightsSaid, {});
}

void Runtime::FollowLights(std::vector<std::uint64_t> entities) {
  std::ranges::sort(entities);
  std::lock_guard _(m_lightsLock);
  m_lightsFollowed = std::move(entities);
}

bool Runtime::FollowsLight(std::uint64_t entity) const {
  std::lock_guard _(m_lightsLock);
  return std::ranges::binary_search(m_lightsFollowed, entity);
}

void Runtime::PushRemoteVehicle(const proto::VehicleState &vehicle) {
  std::lock_guard _(m_tracksLock);
  m_vehicles[vehicle.player].Push(vehicle);
}

sync::VehicleView Runtime::VehicleOf(proto::PlayerId player) {
  const auto now = HostNowUs();
  std::lock_guard _(m_tracksLock);
  const auto car = m_vehicles.find(player);
  if (car == m_vehicles.end())
    return {};
  std::uint64_t delayUs = 100'000;
  if (const auto body = m_tracks.find(player); body != m_tracks.end() && !body->second.Empty())
    delayUs = body->second.DelayUs();
  // The moment smoothed: a step of the clock estimate or of the delay is
  // spread over frames, not a jolt of the car (and of a passenger's camera).
  const auto target = now > delayUs ? now - delayUs : 0;
  return m_vehicleSmoothing.load(std::memory_order_relaxed) ? car->second.Draw(net::NowUs(), target)
                                                            : car->second.At(target);
}

void Runtime::SubmitLocalTransit(const proto::Transit &transit) {
  std::lock_guard _(m_lock);
  m_localTransit = transit;
}

std::optional<proto::Transit> Runtime::TakeLocalTransit() {
  std::lock_guard _(m_lock);
  return std::exchange(m_localTransit, std::nullopt);
}

void Runtime::RequestRecording(std::string name) {
  std::lock_guard _(m_lock);
  m_recordingRequest = std::move(name);
}

std::optional<std::string> Runtime::TakeRecordingRequest() {
  std::lock_guard _(m_lock);
  return std::exchange(m_recordingRequest, std::nullopt);
}

void Runtime::PushRemoteTransit(const proto::Transit &transit) {
  std::lock_guard _(m_lock);
  auto &[changes, latest] = m_transits[transit.player];
  ++changes;
  latest = transit;
}

std::optional<proto::Transit> Runtime::RemoteTransit(proto::PlayerId player,
                                                     std::uint32_t &changes) {
  std::lock_guard _(m_lock);
  const auto it = m_transits.find(player);
  changes = it == m_transits.end() ? 0 : it->second.first;
  if (it == m_transits.end() || it->second.second.kind == proto::TransitKind::None)
    return std::nullopt;
  return it->second.second;
}

void Runtime::SubmitLocalRadio(proto::VehicleRadio radio, bool trackStarted) {
  const auto now = HostNowUs();
  std::lock_guard _(m_lock);
  // The song's start: now if it just began, kept while it plays on.
  if (trackStarted && radio.track != 0)
    m_localTrackStartUs = now;
  else if (radio.track != m_localTrack)
    m_localTrackStartUs = 0;
  m_localTrack = radio.track;
  radio.trackStartUs = radio.on ? m_localTrackStartUs : 0;
  if (m_localRadios.size() < 16)
    m_localRadios.push_back(radio);
}

std::vector<proto::VehicleRadio> Runtime::TakeLocalRadios() {
  std::lock_guard _(m_lock);
  return std::exchange(m_localRadios, {});
}

void Runtime::PushRemoteRadio(const proto::VehicleRadio &radio) {
  std::lock_guard _(m_lock);
  auto &[changes, latest] = m_radios[radio.player];
  ++changes;
  latest = radio;
}

std::optional<proto::VehicleRadio> Runtime::RemoteRadio(proto::PlayerId player,
                                                        std::uint32_t &changes) {
  std::lock_guard _(m_lock);
  const auto it = m_radios.find(player);
  changes = it == m_radios.end() ? 0 : it->second.first;
  if (it == m_radios.end())
    return std::nullopt;
  return it->second.second;
}

void Runtime::SubmitWorld(const proto::World &world) {
  std::lock_guard _(m_lock);
  m_localWorld = world;
}

std::optional<proto::World> Runtime::TakeWorld() {
  std::lock_guard _(m_lock);
  return std::exchange(m_localWorld, std::nullopt);
}

void Runtime::PushHostWorld(const proto::World &world) {
  std::lock_guard _(m_lock);
  m_hostWorld = world;
  ++m_hostWorldChanges;
}

std::optional<proto::World> Runtime::HostWorld(std::uint32_t &changes) {
  std::lock_guard _(m_lock);
  changes = m_hostWorldChanges;
  return m_hostWorld;
}

void Runtime::SubmitLocalPaused(bool paused) {
  {
    std::lock_guard _(m_lock);
    m_localPaused = paused;
  }
  // The frames after the close are timed (NoteEngineFrame): a pause must
  // give the world back without a hitch.
  if (!paused)
    m_afterPauseFrames.store(kAfterPauseFrames, std::memory_order_relaxed);
}

bool Runtime::LoadingShown(std::uint64_t now) const {
  return observer::LoadingShown(LoadingSignUs(), LastGameFrameUs(), now);
}

bool Runtime::HeldNotStalled(std::uint64_t now) const {
  return observer::HeldNotStalled(m_lastEngineFrameUs.load(std::memory_order_relaxed),
                                  LoadingShown(now), now);
}

void Runtime::NoteEngineFrame() {
  const auto now = net::NowUs();
  const auto last = m_lastEngineFrameUs.exchange(now, std::memory_order_relaxed);
  if (last == 0 || now < last)
    return;
  // The frame a session started in counts from its start (MarkEngineFrames).
  const auto mark = m_engineMarkUs.load(std::memory_order_relaxed);
  const auto gap = mark > last && mark < now ? now - mark : now - last;
  m_engineFrames.fetch_add(1, std::memory_order_relaxed);
  auto longest = m_engineLongestUs.load(std::memory_order_relaxed);
  while (gap > longest &&
         !m_engineLongestUs.compare_exchange_weak(longest, gap, std::memory_order_relaxed)) {
  }
  // The frames after our pause menu closed: the first's length and the
  // longest, in one line once they are timed.
  const auto left = m_afterPauseFrames.load(std::memory_order_relaxed);
  if (left == 0)
    return;
  const auto index = kAfterPauseFrames - left;
  if (index == 0) {
    m_afterPauseFirstUs = gap;
    m_afterPauseLongestUs = 0;
  }
  if (gap > m_afterPauseLongestUs) {
    m_afterPauseLongestUs = gap;
    m_afterPauseLongestAt = index + 1;
  }
  m_afterPauseFrames.store(left - 1, std::memory_order_relaxed);
  if (left == 1)
    log::Info("pause: after our menu closed, the first frame took {:.1f} ms, the longest of the "
              "next {} {:.1f} ms (frame {})",
              static_cast<double>(m_afterPauseFirstUs) / 1000.0, kAfterPauseFrames,
              static_cast<double>(m_afterPauseLongestUs) / 1000.0, m_afterPauseLongestAt);
}

std::optional<bool> Runtime::TakeLocalPaused() {
  std::lock_guard _(m_lock);
  return std::exchange(m_localPaused, std::nullopt);
}

void Runtime::SetRemotePaused(std::vector<proto::PlayerId> players) {
  std::lock_guard _(m_lock);
  if (players == m_remotePaused)
    return;
  m_remotePaused = std::move(players);
  ++m_remotePausedChanges;
}

std::vector<proto::PlayerId> Runtime::RemotePaused(std::uint32_t &changes) {
  std::lock_guard _(m_lock);
  changes = m_remotePausedChanges;
  return m_remotePaused;
}

void Runtime::SubmitLocalTime(const proto::TimeEffect &effect) {
  std::lock_guard _(m_lock);
  m_localTime = effect;
}

std::optional<proto::TimeEffect> Runtime::TakeLocalTime() {
  std::lock_guard _(m_lock);
  return std::exchange(m_localTime, std::nullopt);
}

void Runtime::PushRemoteTime(const proto::TimeEffect &effect) {
  std::lock_guard _(m_lock);
  if (effect.abilities == 0)
    m_remoteTimes.erase(effect.player);
  else
    m_remoteTimes[effect.player] = effect;
  ++m_remoteTimeChanges;
}

std::map<proto::PlayerId, proto::TimeEffect> Runtime::RemoteTimes(std::uint32_t &changes) {
  std::lock_guard _(m_lock);
  changes = m_remoteTimeChanges;
  return m_remoteTimes;
}

void Runtime::SubmitLocalElsewhere(const proto::Elsewhere &state) {
  std::lock_guard _(m_lock);
  m_localElsewhere = state;
}

std::optional<proto::Elsewhere> Runtime::TakeLocalElsewhere() {
  std::lock_guard _(m_lock);
  return std::exchange(m_localElsewhere, std::nullopt);
}

void Runtime::PushRemoteElsewhere(const proto::Elsewhere &state) {
  std::lock_guard _(m_lock);
  if (state.place == proto::Place::City)
    m_remoteElsewhere.erase(state.player);
  else
    m_remoteElsewhere[state.player] = state;
  ++m_remoteElsewhereChanges;
}

void Runtime::ClearRemoteElsewhere() {
  std::lock_guard _(m_lock);
  if (m_remoteElsewhere.empty())
    return;
  m_remoteElsewhere.clear();
  ++m_remoteElsewhereChanges;
}

std::map<proto::PlayerId, proto::Elsewhere> Runtime::RemoteElsewhere(std::uint32_t &changes) {
  std::lock_guard _(m_lock);
  changes = m_remoteElsewhereChanges;
  return m_remoteElsewhere;
}

void Runtime::NotePlayerName(proto::PlayerId player, std::string name) {
  std::lock_guard _(m_lock);
  m_names[player] = std::move(name);
}

std::string Runtime::PlayerName(proto::PlayerId player) {
  std::lock_guard _(m_lock);
  const auto it = m_names.find(player);
  return it == m_names.end() ? std::string{} : it->second;
}

std::uint32_t Runtime::SubmitRegroup(proto::Regroup offer) {
  std::lock_guard _(m_lock);
  offer.offer = ++m_regroupOffers;
  if (m_regroupOutbox.size() < 16)
    m_regroupOutbox.push_back(std::move(offer));
  return m_regroupOffers;
}

void Runtime::SubmitRegroupAnswer(const proto::RegroupAnswer &answer) {
  std::lock_guard _(m_lock);
  if (m_regroupOutbox.size() < 16)
    m_regroupOutbox.push_back(answer);
}

std::vector<proto::Message> Runtime::TakeRegroupOutbox() {
  std::lock_guard _(m_lock);
  return std::exchange(m_regroupOutbox, {});
}

void Runtime::PushRegroupEvent(RegroupEvent event) {
  std::lock_guard _(m_lock);
  // Scripts drain these every tick; a pile-up means nobody is listening.
  if (m_regroupEvents.size() < 32)
    m_regroupEvents.push_back(std::move(event));
}

std::vector<RegroupEvent> Runtime::TakeRegroupEvents() {
  std::lock_guard _(m_lock);
  NoteDepth(12, m_regroupEvents.size());
  return std::exchange(m_regroupEvents, {});
}

void Runtime::SubmitNpcSpawn(const proto::NpcSpawn &spawn) {
  std::lock_guard _(m_lock);
  m_npcOutbox.push_back(spawn);
}

void Runtime::SubmitNpcGone(const proto::NpcGone &gone) {
  std::lock_guard _(m_lock);
  m_npcOutbox.push_back(gone);
}

void Runtime::SubmitNpcHealth(const proto::NpcHealth &health) {
  std::lock_guard _(m_lock);
  m_npcOutbox.push_back(health);
}

void Runtime::SubmitNpcHit(const proto::NpcHit &hit) {
  std::lock_guard _(m_lock);
  m_npcOutbox.push_back(hit);
}

void Runtime::SubmitNpcEffect(const proto::NpcEffect &effect) {
  std::lock_guard _(m_lock);
  m_npcOutbox.push_back(effect);
}

void Runtime::SubmitNpcSeat(const proto::NpcSeat &seat) {
  std::lock_guard _(m_lock);
  m_npcOutbox.push_back(seat);
}

void Runtime::SubmitPlayerHit(const proto::PlayerHit &hit) {
  std::lock_guard _(m_lock);
  m_npcOutbox.push_back(hit);
}

void Runtime::PushPlayerHit(const proto::PlayerHit &hit) {
  std::lock_guard _(m_lock);
  if (m_playerHits.size() < 256)
    m_playerHits.push_back({hit, net::NowUs()});
  else
    ++m_overflows;
}

std::vector<proto::PlayerHit> Runtime::TakePlayerHits() {
  std::lock_guard _(m_lock);
  // Hits that waited behind a loading screen or a stall are not applied:
  // every queued hit at once would kill V the moment the world comes back.
  if (const auto stale = consistency::DropStale(m_playerHits, net::NowUs(), 1'500'000))
    log::Info("consistency: {} hit(s) on our V dropped, too old to apply", stale);
  std::vector<proto::PlayerHit> out;
  NoteDepth(14, m_playerHits.size());
  for (const auto &hit : std::exchange(m_playerHits, {}))
    out.push_back(hit.value);
  return out;
}

void Runtime::SubmitShotAt(const proto::NpcShotAt &shot) {
  std::lock_guard _(m_lock);
  m_npcOutbox.push_back(shot);
}

void Runtime::PushShotAt(const proto::NpcShotAt &shot) {
  std::lock_guard _(m_lock);
  if (m_shotsAt.size() < 128)
    m_shotsAt.push_back(shot);
}

std::vector<proto::NpcShotAt> Runtime::TakeShotsAt() {
  std::lock_guard _(m_lock);
  NoteDepth(10, m_shotsAt.size());
  return std::exchange(m_shotsAt, {});
}

void Runtime::SubmitShotHit(const proto::ShotHit &hit) {
  std::lock_guard _(m_lock);
  m_npcOutbox.push_back(hit);
}

void Runtime::PushShotHit(const proto::ShotHit &hit) {
  std::lock_guard _(m_lock);
  if (m_shotHits.size() < 128)
    m_shotHits.push_back(hit);
}

std::vector<proto::ShotHit> Runtime::TakeShotHits() {
  std::lock_guard _(m_lock);
  NoteDepth(11, m_shotHits.size());
  return std::exchange(m_shotHits, {});
}

std::uint8_t Runtime::NpcDetected(proto::NpcId npc) {
  std::lock_guard _(m_lock);
  const auto it = m_npcDetected.find(npc);
  return it == m_npcDetected.end() ? 0 : it->second.first;
}

void Runtime::SubmitNpcStates(proto::NpcStates states, bool partial) {
  // When our game read them: the velocities (NpcVelocities) and the
  // receivers' timeline go by it, not by when the net thread sends them.
  const auto now = HostNowUs();
  states.timeUs = now;
  {
    // Where our NPCs were, for judging the guests' requests (ReachOf).
    std::lock_guard _(m_reachLock);
    for (const auto &entry : states.npcs)
      m_reach.Push(kNpcPuppetBase + entry.npc, now, entry.position);
  }
  std::lock_guard _(m_lock);
  // Taken every net pass (a few ms): more waiting is a stalled net thread,
  // whose oldest go.
  if (m_npcStates.size() >= 8)
    m_npcStates.erase(m_npcStates.begin());
  m_npcStates.push_back({std::move(states), partial});
}

lagcomp::Reach Runtime::ReachOf(proto::PlayerId player, proto::NpcId npc,
                                const proto::Vec3 &puppetAt, const proto::Vec3 &npcAt,
                                float limit) {
  std::uint32_t rttMs = 0;
  for (const auto &info : Status().players)
    if (info.id == player && info.pingMs > 0)
      rttMs = static_cast<std::uint32_t>(info.pingMs);
  const auto now = HostNowUs();
  std::lock_guard _(m_reachLock);
  return lagcomp::Check(m_reach.Of(player), m_reach.Of(kNpcPuppetBase + npc), puppetAt, npcAt, now,
                        rttMs, limit);
}

interact::Reach Runtime::ReachOfObject(proto::PlayerId player, const proto::Vec3 &claimed,
                                       const proto::Vec3 &object, std::uint64_t askedUs,
                                       std::uint64_t arrivalUs) {
  std::uint32_t rttMs = 0;
  for (const auto &info : Status().players)
    if (info.id == player && info.pingMs > 0)
      rttMs = static_cast<std::uint32_t>(info.pingMs);
  std::lock_guard _(m_reachLock);
  return interact::CheckReach(m_reach.Of(player), claimed, object, askedUs, arrivalUs, rttMs);
}

std::vector<proto::Message> Runtime::TakeNpcOutbox() {
  std::lock_guard _(m_lock);
  NoteDepth(6, m_npcOutbox.size());
  return std::exchange(m_npcOutbox, {});
}

std::vector<Runtime::NpcStatesOut> Runtime::TakeNpcStates() {
  std::lock_guard _(m_lock);
  return std::exchange(m_npcStates, {});
}

void Runtime::PushNpcEvent(const NpcEvent &event) {
  std::lock_guard _(m_lock);
  if (m_npcEvents.size() < 1024)
    m_npcEvents.push_back(event);
  else
    ++m_overflows; // the scripts' checks put the proxies right (SharedNpcs)
}

std::vector<NpcEvent> Runtime::TakeNpcEvents() {
  std::lock_guard _(m_lock);
  NoteDepth(1, m_npcEvents.size());
  return std::exchange(m_npcEvents, {});
}

void Runtime::SubmitNpcAction(const proto::NpcAction &action) {
  std::lock_guard _(m_lock);
  m_npcOutbox.push_back(action);
}

void Runtime::SubmitNpcHold(const proto::NpcHold &hold) {
  std::lock_guard _(m_lock);
  m_npcOutbox.push_back(hold);
}

void Runtime::SubmitNpcCue(const proto::NpcCue &cue) {
  std::lock_guard _(m_lock);
  m_npcOutbox.push_back(cue);
}

void Runtime::SubmitNpcActionReply(proto::PlayerId player, const proto::NpcActionReply &reply) {
  std::lock_guard _(m_lock);
  m_npcReplies.emplace_back(player, reply);
}

std::vector<std::pair<proto::PlayerId, proto::NpcActionReply>> Runtime::TakeNpcReplies() {
  std::lock_guard _(m_lock);
  return std::exchange(m_npcReplies, {});
}

void Runtime::PushActionEvent(const ActionEvent &event) {
  std::lock_guard _(m_lock);
  // Scripts drain these every frame; a pile-up means nobody is listening.
  if (m_actionEvents.size() < 256)
    m_actionEvents.push_back(event);
}

std::vector<ActionEvent> Runtime::TakeActionEvents() {
  std::lock_guard _(m_lock);
  NoteDepth(3, m_actionEvents.size());
  return std::exchange(m_actionEvents, {});
}

void Runtime::HoldPuppet(proto::PlayerId player, bool hold) {
  std::lock_guard _(m_heldLock);
  if (hold)
    m_heldPuppets.insert(player);
  else
    m_heldPuppets.erase(player);
}

bool Runtime::IsPuppetHeld(proto::PlayerId player) const {
  std::lock_guard _(m_heldLock);
  return m_heldPuppets.contains(player);
}

Runtime::Placement Runtime::PlacementOf(proto::PlayerId player) const {
  Placement placement;
  {
    std::lock_guard _(m_lock);
    if (const auto seat = m_seats.find(player); seat != m_seats.end())
      placement.seated = seat->second.second.vehicle != 0;
    if (const auto ride = m_transits.find(player); ride != m_transits.end())
      placement.riding = ride->second.second.kind == proto::TransitKind::Metro ||
                         ride->second.second.kind == proto::TransitKind::Along;
  }
  placement.held = IsPuppetHeld(player);
  return placement;
}

void Runtime::SubmitLocalVitals(const proto::Vitals &vitals) {
  std::lock_guard _(m_lock);
  m_localVitals = vitals;
}

std::optional<proto::Vitals> Runtime::TakeLocalVitals() {
  std::lock_guard _(m_lock);
  return std::exchange(m_localVitals, std::nullopt);
}

void Runtime::SetOthersLives(std::vector<proto::Vitals> others) {
  std::lock_guard _(m_lock);
  if (others == m_othersLives)
    return;
  m_othersLives = std::move(others);
  ++m_othersLivesChanges;
}

std::vector<proto::Vitals> Runtime::OthersLives(std::uint32_t &changes) {
  std::lock_guard _(m_lock);
  changes = m_othersLivesChanges;
  return m_othersLives;
}

void Runtime::SubmitRevive(const proto::Revive &revive) {
  std::lock_guard _(m_lock);
  if (m_localRevives.size() < 32)
    m_localRevives.push_back(revive);
}

std::vector<proto::Revive> Runtime::TakeLocalRevives() {
  std::lock_guard _(m_lock);
  return std::exchange(m_localRevives, {});
}

void Runtime::PushRevive(const proto::Revive &revive) {
  std::lock_guard _(m_lock);
  if (m_revives.size() < 32)
    m_revives.push_back(revive);
}

std::vector<proto::Revive> Runtime::TakeRevives() {
  std::lock_guard _(m_lock);
  return std::exchange(m_revives, {});
}

void Runtime::NoteSessionLost() {
  std::lock_guard _(m_lock);
  ++m_sessionLosses;
}

std::uint32_t Runtime::SessionLosses() {
  std::lock_guard _(m_lock);
  return m_sessionLosses;
}

void Runtime::SubmitHack(const proto::Message &message) {
  std::lock_guard _(m_lock);
  if (m_hackOutbox.size() < 256)
    m_hackOutbox.push_back(message);
}

std::vector<proto::Message> Runtime::TakeHackOutbox() {
  std::lock_guard _(m_lock);
  NoteDepth(7, m_hackOutbox.size());
  return std::exchange(m_hackOutbox, {});
}

void Runtime::PushHackEvent(const HackEvent &event) {
  std::lock_guard _(m_lock);
  // Scripts drain these every frame; a pile-up means nobody is listening.
  if (m_hackEvents.size() < 512) {
    m_hackEvents.push_back(event);
    m_hackEvents.back().atUs = net::NowUs();
  } else {
    ++m_overflows;
  }
}

std::vector<HackEvent> Runtime::TakeHackEvents() {
  std::lock_guard _(m_lock);
  // An NPC's hack on our V that waited behind a loading screen is void (its
  // netrunner is likely gone); upload states and effects are state: kept.
  const auto now = net::NowUs();
  std::erase_if(m_hackEvents, [&](const HackEvent &event) {
    return event.kind == HackEvent::Kind::PlayerHack && now > event.atUs &&
           now - event.atUs > 3'000'000;
  });
  NoteDepth(4, m_hackEvents.size());
  return std::exchange(m_hackEvents, {});
}

void Runtime::SubmitLoot(proto::Message message) {
  std::lock_guard _(m_lock);
  if (m_lootOutbox.size() < 256)
    m_lootOutbox.push_back(std::move(message));
}

std::vector<proto::Message> Runtime::TakeLootOutbox() {
  std::lock_guard _(m_lock);
  NoteDepth(8, m_lootOutbox.size());
  return std::exchange(m_lootOutbox, {});
}

void Runtime::PushLoot(proto::Message message) {
  std::lock_guard _(m_lock);
  if (m_lootInbox.size() < 1024)
    m_lootInbox.push_back(std::move(message));
}

std::vector<proto::Message> Runtime::TakeLoot() {
  std::lock_guard _(m_lock);
  NoteDepth(5, m_lootInbox.size());
  return std::exchange(m_lootInbox, {});
}

void Runtime::SubmitDevice(proto::Message message) {
  std::lock_guard _(m_lock);
  if (m_deviceOutbox.size() < 2048)
    m_deviceOutbox.push_back(std::move(message));
}

std::vector<proto::Message> Runtime::TakeDeviceOutbox() {
  std::lock_guard _(m_lock);
  return std::exchange(m_deviceOutbox, {});
}

void Runtime::PushDevice(proto::Message message) {
  std::lock_guard _(m_lock);
  // Room for a whole record at a join (devices::kMaxDevices) and more; what
  // does not fit is put right by the device digest.
  if (m_deviceInbox.size() < 8192)
    m_deviceInbox.push_back(std::move(message));
  else
    ++m_overflows;
}

std::vector<proto::Message> Runtime::TakeDevices() {
  std::lock_guard _(m_lock);
  return std::exchange(m_deviceInbox, {});
}

void Runtime::SubmitBreach(proto::Breach breach) {
  std::lock_guard _(m_lock);
  if (m_breachOutbox.size() < 64)
    m_breachOutbox.push_back(std::move(breach));
}

std::vector<proto::Breach> Runtime::TakeBreachOutbox() {
  std::lock_guard _(m_lock);
  return std::exchange(m_breachOutbox, {});
}

void Runtime::PushBreach(proto::Message message) {
  std::lock_guard _(m_lock);
  if (m_breachInbox.size() < 256)
    m_breachInbox.push_back(std::move(message));
  else
    ++m_overflows;
}

std::vector<proto::Message> Runtime::TakeBreaches() {
  std::lock_guard _(m_lock);
  return std::exchange(m_breachInbox, {});
}

void Runtime::SubmitObject(proto::Message message) {
  std::lock_guard _(m_lock);
  if (m_objectOutbox.size() < 512)
    m_objectOutbox.push_back(std::move(message));
}

std::vector<proto::Message> Runtime::TakeObjectOutbox() {
  std::lock_guard _(m_lock);
  return std::exchange(m_objectOutbox, {});
}

void Runtime::PushObject(proto::Message message) {
  std::lock_guard _(m_lock);
  if (m_objectInbox.size() < 1024)
    m_objectInbox.push_back(std::move(message));
  else
    ++m_overflows;
}

std::vector<proto::Message> Runtime::TakeObjects() {
  std::lock_guard _(m_lock);
  return std::exchange(m_objectInbox, {});
}

void Runtime::PushNpcStates(const proto::NpcStates &states, proto::PlayerId you) {
  {
    // Each owner's states list every NPC it shares, ten times a second: an
    // NPC not heard of for a second detects nobody any more.
    const auto now = HostNowUs();
    std::lock_guard _(m_lock);
    m_detectedFor = you;
    for (const auto &entry : states.npcs)
      m_npcDetected[entry.npc] = {entry.detected, now};
    std::erase_if(m_npcDetected, [&](const auto &entry) {
      return static_cast<std::int64_t>(now - entry.second.second) > 1'000'000;
    });
  }
  // Each NPC is a track of its own, drawn like a remote player (one lock
  // for them all: this comes up to 30 times a second with up to 48), with
  // its owner's velocity: a late state is carried on.
  const auto arrival = HostNowUs();
  std::lock_guard _(m_tracksLock);
  for (const auto &entry : states.npcs) {
    proto::PlayerState state;
    state.player = kNpcPuppetBase + entry.npc;
    state.seq = states.seq;
    state.timeUs = states.timeUs;
    state.position = entry.position;
    state.orientation = sync::FromYaw(entry.yaw);
    state.velocity = entry.velocity;
    m_tracks.try_emplace(state.player).first->second.Push(state, arrival);
  }
}

void Runtime::SubmitNpcAnim(const proto::NpcAnim &anim) {
  std::lock_guard _(m_lock);
  // Into the outbox's last message when it is a batch with room, so a state
  // follows its NPC's spawn and the net thread sends one message per batch.
  if (!m_npcOutbox.empty())
    if (auto *batch = std::get_if<proto::NpcAnims>(&m_npcOutbox.back());
        batch && batch->npcs.size() < proto::kMaxNpcAnims) {
      batch->npcs.push_back(anim);
      return;
    }
  m_npcOutbox.push_back(proto::NpcAnims{.npcs = {anim}});
}

void Runtime::SubmitNpcShot(const proto::NpcShot &shot) {
  std::lock_guard _(m_lock);
  m_npcOutbox.push_back(shot);
}

void Runtime::SubmitNpcReaction(const proto::NpcReaction &reaction) {
  std::lock_guard _(m_lock);
  m_npcOutbox.push_back(reaction);
}

void Runtime::SubmitNpcVoice(const proto::NpcVoice &voice) {
  std::lock_guard _(m_lock);
  m_npcOutbox.push_back(voice);
}

void Runtime::PushNpcAnims(const proto::NpcAnims &anims) {
  std::lock_guard _(m_lock);
  for (const auto &anim : anims.npcs)
    if (m_npcAnimsIn.size() < 1024)
      m_npcAnimsIn.push_back(anim);
}

std::vector<proto::NpcAnim> Runtime::TakeNpcAnims() {
  std::lock_guard _(m_lock);
  NoteDepth(2, m_npcAnimsIn.size());
  return std::exchange(m_npcAnimsIn, {});
}

void Runtime::PushNpcShot(const proto::NpcShot &shot) {
  std::lock_guard _(m_lock);
  if (m_npcShotsIn.size() < 256)
    m_npcShotsIn.push_back(shot);
}

std::vector<proto::NpcShot> Runtime::TakeNpcShots() {
  std::lock_guard _(m_lock);
  NoteDepth(9, m_npcShotsIn.size());
  return std::exchange(m_npcShotsIn, {});
}

std::int32_t Runtime::NpcsDetectingUs() {
  const auto now = HostNowUs();
  std::lock_guard _(m_lock);
  // An owner that went quiet (or left): nobody can be hunting us on its say.
  // (Signed: the clock offset may move a stamp a little into the future.)
  const auto you = m_detectedFor;
  const auto ours = you >= 1 && you <= 8 ? static_cast<std::uint8_t>(1u << (you - 1)) : 0;
  std::int32_t detecting = 0;
  for (const auto &[npc, entry] : m_npcDetected)
    if ((entry.first & ours) && static_cast<std::int64_t>(now - entry.second) <= 3'000'000)
      ++detecting;
  return detecting;
}

float Runtime::NpcAwarenessOfUs(proto::NpcId npc) {
  const auto now = HostNowUs();
  std::lock_guard _(m_lock);
  const auto it = m_awarenessOfUs.find(npc);
  if (it == m_awarenessOfUs.end() || static_cast<std::int64_t>(now - it->second.second) > 1'000'000)
    return 0.f;
  return it->second.first;
}

void Runtime::PushNpcAwareness(const proto::NpcAwareness &awareness, proto::PlayerId you) {
  // Each owner lists the NPCs noticing someone, five times a second: one
  // left out is no longer noticing us, and its entry goes stale.
  const auto now = HostNowUs();
  std::lock_guard _(m_lock);
  for (const auto &entry : awareness.entries)
    if (entry.player == you)
      m_awarenessOfUs[entry.npc] = {static_cast<float>(entry.level) / 255.f, now};
  std::erase_if(m_awarenessOfUs, [&](const auto &entry) {
    return static_cast<std::int64_t>(now - entry.second.second) > 400'000;
  });
}

void Runtime::SubmitNpcAwareness(proto::NpcAwareness awareness) {
  std::lock_guard _(m_lock);
  m_localAwareness = std::move(awareness);
}

std::optional<proto::NpcAwareness> Runtime::TakeNpcAwareness() {
  std::lock_guard _(m_lock);
  return std::exchange(m_localAwareness, std::nullopt);
}

void Runtime::SubmitLocalStealth(const proto::Stealth &stealth) {
  std::lock_guard _(m_lock);
  m_localStealth = stealth;
}

std::optional<proto::Stealth> Runtime::TakeLocalStealth() {
  std::lock_guard _(m_lock);
  return std::exchange(m_localStealth, std::nullopt);
}

void Runtime::PushRemoteStealth(const proto::Stealth &stealth) {
  std::lock_guard _(m_lock);
  auto &[changes, latest] = m_stealth[stealth.player];
  ++changes;
  latest = stealth;
}

std::optional<proto::Stealth> Runtime::RemoteStealth(proto::PlayerId player,
                                                     std::uint32_t &changes) {
  std::lock_guard _(m_lock);
  const auto it = m_stealth.find(player);
  changes = it == m_stealth.end() ? 0 : it->second.first;
  if (it == m_stealth.end())
    return std::nullopt;
  return it->second.second;
}

void Runtime::SubmitLocalNoise(const proto::Noise &noise) {
  std::lock_guard _(m_lock);
  // Footsteps come every frame of a run: a few a second are enough for an
  // NPC to hear the V (presence::NoiseGate).
  if (!m_noiseGate.ShouldSend(noise.stim, net::NowUs()))
    return;
  if (m_localNoises.size() < 32)
    m_localNoises.push_back(noise);
}

std::vector<proto::Noise> Runtime::TakeLocalNoises() {
  std::lock_guard _(m_lock);
  return std::exchange(m_localNoises, {});
}

void Runtime::PushRemoteNoise(const proto::Noise &noise) {
  std::lock_guard _(m_lock);
  if (m_remoteNoises.size() < 64)
    m_remoteNoises.push_back({noise, net::NowUs()});
}

std::vector<proto::Noise> Runtime::TakeRemoteNoises() {
  std::lock_guard _(m_lock);
  // A gunshot a second old is not heard now.
  consistency::DropStale(m_remoteNoises, net::NowUs(), 1'000'000);
  std::vector<proto::Noise> out;
  for (const auto &noise : std::exchange(m_remoteNoises, {}))
    out.push_back(noise.value);
  return out;
}

void Runtime::SetShared(const std::map<proto::NpcId, proto::NpcSpawn> &npcs,
                        const std::map<proto::PlayerId, proto::PlayerInfo> &players) {
  std::lock_guard _(m_lock);
  m_sharedNpcs = npcs;
  m_roster = players;
}

bool Runtime::IsNpcShared(proto::NpcId npc) {
  std::lock_guard _(m_lock);
  return m_sharedNpcs.contains(npc);
}

std::vector<proto::NpcSpawn> Runtime::SharedNpcs() {
  std::lock_guard _(m_lock);
  std::vector<proto::NpcSpawn> out;
  out.reserve(m_sharedNpcs.size());
  for (const auto &[id, spawn] : m_sharedNpcs)
    out.push_back(spawn);
  return out;
}

bool Runtime::InRoster(proto::PlayerId player) {
  std::lock_guard _(m_lock);
  return m_roster.contains(player);
}

std::vector<proto::PlayerInfo> Runtime::RosterPlayers() {
  std::lock_guard _(m_lock);
  std::vector<proto::PlayerInfo> out;
  for (const auto &[id, info] : m_roster)
    out.push_back(info);
  return out;
}

void Runtime::RequestEpoch(std::uint8_t reason) {
  if (Observer::On()) {
    constexpr const char *kReasons[] = {"join", "loading screen", "fast travel", "time skip",
                                        "frozen and back"};
    Observer::Get().Note("epoch", reason < std::size(kReasons) ? kReasons[reason] : "?");
  }
  std::lock_guard _(m_lock);
  m_epochRequest = reason;
}

std::optional<std::uint8_t> Runtime::TakeEpochRequest() {
  std::lock_guard _(m_lock);
  return std::exchange(m_epochRequest, std::nullopt);
}

std::uint32_t Runtime::TakeOverflows() {
  std::lock_guard _(m_lock);
  return std::exchange(m_overflows, 0);
}

void Runtime::ResetSession() {
  PresenceState::Get().Reset();
  {
    std::lock_guard _(m_reachLock);
    m_reach.Clear();
  }
  {
    std::lock_guard _(m_lock);
    m_localShots.clear();
    m_remoteShots.clear();
    m_npcOutbox.clear();
    m_npcStates.clear();
    m_npcEvents.clear();
    m_playerHits.clear();
    m_hackOutbox.clear();
    m_hackEvents.clear();
    m_npcDetected.clear();
    m_awarenessOfUs.clear();
    m_localAwareness.reset();
    m_localNoises.clear();
    m_noiseGate.Reset();
    m_remoteNoises.clear();
    m_sharedNpcs.clear();
    m_roster.clear();
    m_epochRequest.reset();
    // Bags, claims, revives and offers name the old session's numbers.
    m_lootOutbox.clear();
    m_lootInbox.clear();
    m_deviceOutbox.clear();
    m_deviceInbox.clear();
    m_breachOutbox.clear();
    m_breachInbox.clear();
    m_objectOutbox.clear();
    m_objectInbox.clear();
    m_localRevives.clear();
    m_revives.clear();
    m_regroupOutbox.clear();
    m_regroupEvents.clear();
    // The street: the next session's ids are other vehicles, and nobody is
    // wanted in it until its game says so.
    m_streetOutbox.clear();
    m_trafficStates.clear();
    m_streetEvents.clear();
    m_sharedTraffic.clear();
    m_trafficStatus.clear();
    m_trafficShown.clear();
    m_trafficOwners.clear();
    m_heat.clear();
    // Vehicle combat: the next session's vehicles are named anew (our car
    // too: by our player id in it); the scripts say our car's damage again.
    m_wheelsOutbox.clear();
    m_wheelsEvents.clear();
    m_vehicleDamage.clear();
    m_gunners.clear();
    m_aims.clear();
    m_seatCombat.clear();
    m_bumps.Clear();
    m_ownDamage.clear();
    // The old host's clock and weather are not the next host's: none until
    // it says its own (the count moves, so World.reds sees a fresh state with
    // nothing in it and leaves our weather be).
    if (m_hostWorld) {
      m_hostWorld.reset();
      ++m_hostWorldChanges;
    }
    // The next session names its players itself (a number may be reused).
    m_names.clear();
  }
  // The NPCs' poses and rig tracks (scenes): the next session's ids are
  // other NPCs.
  ForgetActorPoses();
  // The NPCs' tracks: the next session's ids are other NPCs.
  std::lock_guard _(m_tracksLock);
  std::erase_if(m_tracks, [](const auto &entry) { return entry.first >= kNpcPuppetBase; });
  m_trafficTracks.clear();
  m_trafficPace.clear();
}

void Runtime::SubmitStreet(proto::Message message) {
  std::lock_guard _(m_lock);
  if (m_streetOutbox.size() < 2048)
    m_streetOutbox.push_back(std::move(message));
  else
    ++m_overflows;
}

std::vector<proto::Message> Runtime::TakeStreetOutbox() {
  std::lock_guard _(m_lock);
  return std::exchange(m_streetOutbox, {});
}

void Runtime::SubmitTrafficCars(std::span<const traffic::Car> cars,
                                std::span<const proto::Vec3> viewers, bool nearOnly) {
  // When our game read them (the receivers' timeline goes by it).
  proto::TrafficStates states{.timeUs = HostNowUs()};
  states.cars = nearOnly ? traffic::PickNear(cars, viewers) : m_trafficBudget.Pick(cars, viewers);
  if (states.cars.empty())
    return;
  std::lock_guard _(m_lock);
  // Taken every net pass: more waiting is a stalled net thread.
  if (m_trafficStates.size() >= 8)
    m_trafficStates.erase(m_trafficStates.begin());
  m_trafficStates.push_back(std::move(states));
}

std::vector<proto::TrafficStates> Runtime::TakeTrafficStates() {
  std::lock_guard _(m_lock);
  return std::exchange(m_trafficStates, {});
}

void Runtime::PushStreetEvent(StreetEvent event) {
  event.atUs = net::NowUs();
  std::lock_guard _(m_lock);
  if (m_streetEvents.size() < 2048)
    m_streetEvents.push_back(std::move(event));
  else
    ++m_overflows; // the scripts' checks put the proxies right (SharedTraffic)
}

std::vector<StreetEvent> Runtime::TakeStreetEvents() {
  std::lock_guard _(m_lock);
  // A hit or a crime that waited behind a loading screen is worth nothing:
  // a pile of them at once would wreck a car, or cost five stars, the
  // moment the world comes back. What is shared is state: always applied.
  const auto now = net::NowUs();
  const auto before = m_streetEvents.size();
  std::erase_if(m_streetEvents, [&](const StreetEvent &event) {
    const bool timed =
        event.kind == StreetEvent::Kind::Hit || event.kind == StreetEvent::Kind::Crime;
    return timed && now > event.atUs && now - event.atUs > 3'000'000;
  });
  if (const auto stale = before - m_streetEvents.size())
    log::Info("consistency: {} vehicle hit(s) or crime(s) dropped, too old to apply", stale);
  return std::exchange(m_streetEvents, {});
}

void Runtime::PushTrafficStates(const proto::TrafficStates &states) {
  const auto arrival = HostNowUs();
  std::lock_guard _(m_tracksLock);
  for (const auto &entry : states.cars) {
    const auto car = traffic::Unpack(entry);
    const proto::VehicleState state{.seq = states.seq,
                                    .timeUs = states.timeUs,
                                    .position = car.position,
                                    .orientation = car.orientation,
                                    .velocity = car.velocity};
    m_trafficTracks[entry.id].Push(state);
    m_trafficPace.try_emplace(entry.id, 30)
        .first->second.Push({.seq = state.seq, .timeUs = state.timeUs, .position = state.position},
                            arrival);
  }
}

void Runtime::ForgetTraffic(proto::TrafficId id) {
  std::lock_guard _(m_tracksLock);
  m_trafficTracks.erase(id);
  m_trafficPace.erase(id);
}

sync::VehicleView Runtime::TrafficOf(proto::TrafficId id) {
  // Drawn as far in the past as its own stream needs (sync::Track: transit,
  // the interval between its states, jitter): about 100 ms for a car near
  // a V (30 a second), 200 for a far one (10, or every third round when the
  // street is full: traffic::Budget); 200 for every car before.
  const auto now = HostNowUs();
  std::lock_guard _(m_tracksLock);
  const auto car = m_trafficTracks.find(id);
  if (car == m_trafficTracks.end())
    return {};
  std::uint64_t delay = 200'000;
  if (const auto pace = m_trafficPace.find(id); pace != m_trafficPace.end()) {
    pace->second.At(now);
    delay = pace->second.DelayUs();
  }
  const auto target = now > delay ? now - delay : 0;
  return m_vehicleSmoothing.load(std::memory_order_relaxed) ? car->second.Draw(net::NowUs(), target)
                                                            : car->second.At(target);
}

void Runtime::SetSharedTraffic(const std::map<proto::TrafficId, proto::TrafficSpawn> &traffic,
                               const std::map<proto::TrafficId, proto::TrafficStatus> &status,
                               const std::map<proto::TrafficId, proto::TrafficSpawn> &shown,
                               std::map<proto::TrafficId, proto::PlayerId> owners) {
  std::lock_guard _(m_lock);
  m_sharedTraffic = traffic;
  m_trafficStatus = status;
  m_trafficShown.clear();
  for (const auto &[id, spawn] : shown)
    m_trafficShown.insert(id);
  m_trafficOwners = std::move(owners);
}

std::vector<std::pair<proto::TrafficSpawn, proto::TrafficStatus>> Runtime::SharedTraffic() {
  std::lock_guard _(m_lock);
  std::vector<std::pair<proto::TrafficSpawn, proto::TrafficStatus>> out;
  out.reserve(m_sharedTraffic.size());
  for (const auto &[id, spawn] : m_sharedTraffic) {
    if (!m_trafficShown.contains(id))
      continue; // ours: our game runs it
    // Without a status it stands as it was announced.
    proto::TrafficStatus status{
        .id = id, .flags = static_cast<std::uint16_t>(spawn.flags & proto::kTrafficStatusMask)};
    if (const auto it = m_trafficStatus.find(id); it != m_trafficStatus.end())
      status = it->second;
    out.emplace_back(spawn, status);
  }
  return out;
}

bool Runtime::IsTrafficShared(proto::TrafficId id) {
  std::lock_guard _(m_lock);
  return m_sharedTraffic.contains(id);
}

proto::PlayerId Runtime::TrafficOwnerOf(proto::TrafficId id) {
  std::lock_guard _(m_lock);
  if (const auto it = m_trafficOwners.find(id); it != m_trafficOwners.end())
    return it->second;
  return m_sharedTraffic.contains(id) ? proto::TrafficOwner(id) : proto::kNoPlayer;
}

void Runtime::SubmitLocalHeat(const proto::Heat &heat) {
  std::lock_guard _(m_lock);
  m_localHeat = heat;
}

std::optional<proto::Heat> Runtime::TakeLocalHeat() {
  std::lock_guard _(m_lock);
  return std::exchange(m_localHeat, std::nullopt);
}

void Runtime::PushRemoteHeat(const proto::Heat &heat) {
  std::lock_guard _(m_lock);
  if (heat.stage == 0)
    m_heat.erase(heat.player);
  else
    m_heat[heat.player] = heat;
}

proto::Heat Runtime::HeatOf(proto::PlayerId player) {
  std::lock_guard _(m_lock);
  const auto it = m_heat.find(player);
  return it == m_heat.end() ? proto::Heat{.player = player} : it->second;
}

void Runtime::SubmitWheels(proto::Message message) {
  std::lock_guard _(m_lock);
  if (m_wheelsOutbox.size() < 2048)
    m_wheelsOutbox.push_back(std::move(message));
  else
    ++m_overflows;
}

std::vector<proto::Message> Runtime::TakeWheelsOutbox() {
  std::lock_guard _(m_lock);
  return std::exchange(m_wheelsOutbox, {});
}

void Runtime::NoteOwnGrid(proto::VehicleRef vehicle, std::span<const float> cells) {
  std::lock_guard _(m_lock);
  auto &damage = m_ownDamage[vehicle];
  damage.vehicle = vehicle;
  damage.grid = vehiclecombat::PackGrid(cells);
}

void Runtime::NoteOwnPart(proto::VehicleRef vehicle, std::uint64_t part) {
  std::lock_guard _(m_lock);
  auto &damage = m_ownDamage[vehicle];
  damage.vehicle = vehicle;
  if (!std::ranges::contains(damage.parts, part) && damage.parts.size() < proto::kMaxDetachedParts)
    damage.parts.push_back(part);
}

proto::VehicleDamage Runtime::OwnDamage(proto::VehicleRef vehicle) {
  std::lock_guard _(m_lock);
  const auto it = m_ownDamage.find(vehicle);
  return it == m_ownDamage.end() ? proto::VehicleDamage{.vehicle = vehicle} : it->second;
}

void Runtime::SetOwnDamage(const proto::VehicleDamage &damage) {
  std::lock_guard _(m_lock);
  m_ownDamage[damage.vehicle] = damage;
}

void Runtime::ForgetOwnDamage(proto::VehicleRef vehicle) {
  std::lock_guard _(m_lock);
  m_ownDamage.erase(vehicle);
}

void Runtime::PushWheelsEvent(WheelsEvent event) {
  event.atUs = net::NowUs();
  std::lock_guard _(m_lock);
  if (m_wheelsEvents.size() < 2048)
    m_wheelsEvents.push_back(std::move(event));
  else
    ++m_overflows;
}

std::vector<WheelsEvent> Runtime::TakeWheelsEvents() {
  std::lock_guard _(m_lock);
  // A hit, a ram, an exit or a shot that waited behind a loading screen is
  // worth nothing: a pile of them at once would wreck a car, or play a
  // burst of muzzle flashes. What is state is always applied.
  const auto now = net::NowUs();
  const auto before = m_wheelsEvents.size();
  std::erase_if(m_wheelsEvents, [&](const WheelsEvent &event) {
    using Kind = WheelsEvent::Kind;
    const std::uint64_t maxAge = event.kind == Kind::Fire                             ? 500'000
                                 : event.kind == Kind::Exit                           ? 1'000'000
                                 : event.kind == Kind::Hit || event.kind == Kind::Ram ? 1'500'000
                                                                                      : 0;
    return maxAge != 0 && now > event.atUs && now - event.atUs > maxAge;
  });
  if (const auto stale = before - m_wheelsEvents.size())
    log::Info("consistency: {} vehicle hit(s), ram(s), exit(s) or shot(s) dropped, too old", stale);
  return std::exchange(m_wheelsEvents, {});
}

void Runtime::PushVehicleDamage(const proto::VehicleDamage &damage) {
  const auto hostNow = HostNowUs();
  std::lock_guard _(m_lock);
  const auto it = m_vehicleDamage.find(damage.vehicle);
  const auto before =
      it == m_vehicleDamage.end() ? std::nullopt : std::optional<proto::VehicleDamage>(it->second);
  const auto end = vehiclecombat::OnArrival(damage, before, hostNow);
  m_vehicleDamage[damage.vehicle] = damage;
  if (m_wheelsEvents.size() < 2048)
    m_wheelsEvents.push_back({.kind = WheelsEvent::Kind::Damage,
                              .player = damage.instigator,
                              .vehicle = damage.vehicle,
                              .a = static_cast<std::int32_t>(end),
                              .id = damage.record,
                              .amount = damage.health,
                              .atUs = net::NowUs()});
  else
    ++m_overflows;
}

std::optional<proto::VehicleDamage> Runtime::DamageOf(proto::VehicleRef vehicle) {
  std::lock_guard _(m_lock);
  const auto it = m_vehicleDamage.find(vehicle);
  if (it == m_vehicleDamage.end())
    return std::nullopt;
  return it->second;
}

void Runtime::SetGunner(const proto::Gunner &gunner) {
  std::lock_guard _(m_lock);
  if (gunner.gunner == proto::kNoPlayer) {
    m_gunners.erase(gunner.vehicle);
    m_aims.erase(gunner.vehicle);
  } else {
    m_gunners[gunner.vehicle] = gunner;
  }
}

proto::Gunner Runtime::GunnerOf(proto::VehicleRef vehicle) {
  std::lock_guard _(m_lock);
  const auto it = m_gunners.find(vehicle);
  return it == m_gunners.end() ? proto::Gunner{.vehicle = vehicle} : it->second;
}

void Runtime::PushGunnerAim(const proto::GunnerAim &aim) {
  std::lock_guard _(m_lock);
  auto &kept = m_aims[aim.vehicle];
  // Newest wins (State channel: an old one may come late).
  if (kept.player == aim.player && static_cast<std::int32_t>(aim.seq - kept.seq) <= 0)
    return;
  kept = aim;
}

std::optional<proto::GunnerAim> Runtime::AimOf(proto::VehicleRef vehicle) {
  std::lock_guard _(m_lock);
  const auto it = m_aims.find(vehicle);
  if (it == m_aims.end())
    return std::nullopt;
  return it->second;
}

void Runtime::SetSeatCombat(const proto::VehicleCombat &combat) {
  std::lock_guard _(m_lock);
  if (combat.mode == proto::SeatCombat::None)
    m_seatCombat.erase(combat.player);
  else
    m_seatCombat[combat.player] = combat;
}

proto::VehicleCombat Runtime::SeatCombatOf(proto::PlayerId player) {
  std::lock_guard _(m_lock);
  const auto it = m_seatCombat.find(player);
  return it == m_seatCombat.end() ? proto::VehicleCombat{.player = player} : it->second;
}

void Runtime::NoteContact(proto::PlayerId player) {
  const auto now = HostNowUs();
  std::lock_guard _(m_lock);
  m_bumps.Contact(player, now);
}

void Runtime::PushBump(const proto::VehicleBump &bump) {
  const auto now = HostNowUs();
  std::lock_guard _(m_lock);
  m_bumps.Receive(bump, now);
}

std::vector<vehiclecombat::BumpLedger::Push> Runtime::TakeDuePushes() {
  const auto now = HostNowUs();
  std::lock_guard _(m_lock);
  return m_bumps.Due(now);
}

void Runtime::PushRemotePose(const proto::PoseState &pose) {
  std::lock_guard _(m_posesLock);
  m_poses[pose.player].Push(pose);
}

bool Runtime::PoseOf(proto::PlayerId player, pose::Pose &out) {
  // A player in a private scene: its puppet shows its own idle (story.hpp).
  if (Story::Get().PosePaused(player))
    return false;
  // Drawn as far in the past as the player's position (interpolation.hpp);
  // a loopback pose, with no network, a fixed two frames.
  std::uint64_t delayUs = 33'000;
  {
    std::lock_guard _(m_tracksLock);
    if (const auto it = m_tracks.find(player); it != m_tracks.end() && !it->second.Empty())
      delayUs = it->second.DelayUs();
  }
  const auto now = HostNowUs();
  {
    std::lock_guard _(m_posesLock);
    const auto it = m_poses.find(player);
    if (it == m_poses.end() || !it->second.At(now > delayUs ? now - delayUs : 0, out))
      return false;
  }
  // A puppet that was just hit flinches over whatever it does (presence.hpp).
  const auto lean = PresenceState::Get().LeanOf(player, now);
  pose::LeanSpine(out, lean.pitch, lean.roll);
  return true;
}

void Runtime::SubmitActorPose(proto::NpcId npc, const proto::PoseState &pose) {
  std::lock_guard _(m_posesLock);
  // A corpse's rest, asked for once: not the scene's stream.
  if (m_corpseReads.erase(npc)) {
    m_corpseRead[npc] = pose;
    return;
  }
  m_actorOut[npc] = pose;
}

void Runtime::ReadCorpse(proto::NpcId npc) {
  std::lock_guard _(m_posesLock);
  m_corpseReads.insert(npc);
  m_corpseRead.erase(npc);
}

std::optional<proto::PoseState> Runtime::TakeCorpseRead(proto::NpcId npc) {
  std::lock_guard _(m_posesLock);
  const auto it = m_corpseRead.find(npc);
  if (it == m_corpseRead.end())
    return std::nullopt;
  auto pose = it->second;
  m_corpseRead.erase(it);
  return pose;
}

void Runtime::PushCorpsePose(const proto::CorpsePose &corpse) {
  std::lock_guard _(m_posesLock);
  // A malformed pose is not drawn: the copy keeps its own ragdoll's.
  const auto unpacked = pose::Unpack(pose::FromActor(corpse.pose));
  if (!unpacked)
    return;
  m_corpsePoses[corpse.pose.npc] = *unpacked;
  if (m_corpseArrivals.size() < 64)
    m_corpseArrivals.push_back(corpse);
}

std::vector<proto::CorpsePose> Runtime::TakeCorpseArrivals() {
  std::lock_guard _(m_posesLock);
  return std::exchange(m_corpseArrivals, {});
}

void Runtime::ForgetCorpse(proto::NpcId npc) {
  std::lock_guard _(m_posesLock);
  m_corpsePoses.erase(npc);
  m_corpseReads.erase(npc);
  m_corpseRead.erase(npc);
}

std::vector<proto::ActorPose> Runtime::TakeActorPoses() {
  std::vector<proto::ActorPose> out;
  std::lock_guard _(m_posesLock);
  out.reserve(m_actorOut.size());
  for (const auto &[npc, state] : m_actorOut)
    out.push_back(pose::ForActor(npc, state));
  m_actorOut.clear();
  return out;
}

void Runtime::PushActorPose(const proto::ActorPose &pose) {
  std::lock_guard _(m_posesLock);
  m_actorPoses[pose.npc].Push(pose::FromActor(pose));
  ++m_actorPosesIn;
}

bool Runtime::ActorPoseOf(proto::NpcId npc, pose::Pose &out) {
  // Drawn as far in the past as the proxy's position.
  std::uint64_t delayUs = 100'000;
  {
    std::lock_guard _(m_tracksLock);
    if (const auto it = m_tracks.find(kNpcPuppetBase + npc);
        it != m_tracks.end() && !it->second.Empty())
      delayUs = it->second.DelayUs();
  }
  const auto now = HostNowUs();
  std::lock_guard _(m_posesLock);
  const auto it = m_actorPoses.find(npc);
  // No scene streams it: a corpse's rest, held (CorpsePose).
  const auto rest = [&] {
    const auto corpse = m_corpsePoses.find(npc);
    if (corpse == m_corpsePoses.end())
      return false;
    out = corpse->second;
    return true;
  };
  if (it == m_actorPoses.end())
    return rest();
  // Its poses come as often as the scene's budget lets its stream go
  // (scene::Pacer): drawn one of their intervals in the past, so that there
  // are two to draw between.
  const auto interval = it->second.IntervalUs();
  if (interval > 50'000)
    delayUs = std::max(delayUs, interval + 20'000);
  // The stream stopped (the scene is over, the host is gone): the proxy is
  // not left frozen in its last pose.
  if (now > it->second.NewestUs() + delayUs + std::max(kActorPoseStaleUs, 3 * interval))
    return rest();
  return it->second.At(now > delayUs ? now - delayUs : 0, out);
}

void Runtime::ForgetActorPoses() {
  std::lock_guard _(m_posesLock);
  m_actorPoses.clear();
  m_actorOut.clear();
  m_rigOut.clear();
  m_rigTracks.clear();
  m_trackSets.clear();
}

void Runtime::SubmitRigPose(RigSample sample) {
  const auto key = std::uint64_t{sample.npc} << 8 | sample.part;
  std::lock_guard _(m_posesLock);
  m_rigOut[key] = std::move(sample);
}

std::vector<Runtime::RigSample> Runtime::TakeRigPoses() {
  std::vector<RigSample> out;
  std::lock_guard _(m_posesLock);
  out.reserve(m_rigOut.size());
  for (auto &[key, sample] : m_rigOut)
    out.push_back(std::move(sample));
  m_rigOut.clear();
  return out;
}

void Runtime::PushRigPose(const proto::RigPose &pose) {
  const auto key = std::uint64_t{pose.npc} << 8 | pose.part;
  std::lock_guard _(m_posesLock);
  if (m_rigTracks[key].Push(pose))
    ++m_rigPosesIn;
}

std::size_t Runtime::ApplyRigPose(proto::NpcId npc, std::uint8_t part, std::uint32_t rigHash,
                                  std::span<const rig::Bone> reference, std::span<rig::Bone> pose) {
  const auto key = std::uint64_t{npc} << 8 | part;
  const auto now = HostNowUs();
  std::lock_guard _(m_posesLock);
  const auto it = m_rigTracks.find(key);
  // The stream stopped (the line is over, the scene, the host is gone): the
  // rig's own animation again.
  if (it == m_rigTracks.end() || now > it->second.NewestUs() + 2 * kActorPoseStaleUs)
    return 0;
  return it->second.Apply(now, rigHash, reference, pose);
}

void Runtime::PushRigTracks(const proto::RigTracks &tracks) {
  const auto key = std::uint64_t{tracks.npc} << 8 | tracks.part;
  std::lock_guard _(m_posesLock);
  m_trackSets[key].Push(tracks);
}

std::size_t Runtime::ApplyRigTracks(proto::NpcId npc, std::uint8_t part, std::uint32_t trackHash,
                                    std::span<const float> reference, std::span<float> values) {
  const auto key = std::uint64_t{npc} << 8 | part;
  const auto now = HostNowUs();
  std::lock_guard _(m_posesLock);
  const auto it = m_trackSets.find(key);
  if (it == m_trackSets.end() || now > it->second.NewestUs() + 2 * kActorPoseStaleUs)
    return 0;
  return it->second.Apply(now, trackHash, reference, values);
}

std::uint32_t Runtime::RigPosesReceived() {
  std::lock_guard _(m_posesLock);
  return std::exchange(m_rigPosesIn, 0);
}

std::uint32_t Runtime::ActorPosesReceived() {
  std::lock_guard _(m_posesLock);
  return std::exchange(m_actorPosesIn, 0);
}

std::uint32_t Runtime::PosesReceived(proto::PlayerId player) {
  std::lock_guard _(m_posesLock);
  const auto it = m_poses.find(player);
  return it == m_poses.end() ? 0 : it->second.Received();
}

void Runtime::PushEvent(ScriptEvent event) {
  std::lock_guard _(m_lock);
  m_events.push_back(std::move(event));
}

std::vector<ScriptEvent> Runtime::TakeEvents() {
  std::lock_guard _(m_lock);
  NoteDepth(0, m_events.size());
  return std::exchange(m_events, {});
}

void Runtime::PushRemote(const proto::PlayerState &state, bool recovered) {
  const auto arrival = HostNowUs();
  if (state.player < kNpcPuppetBase) {
    Observer::Get().Remote(state.player, state.position);
    std::lock_guard _(m_reachLock);
    m_reach.Push(state.player, state.timeUs, state.position);
  }
  std::lock_guard _(m_tracksLock);
  m_tracks.try_emplace(state.player).first->second.Push(state, arrival, recovered);
}

void Runtime::ForgetRemote(proto::PlayerId player) {
  {
    std::lock_guard _(m_lock);
    m_appearances.erase(player);
    m_held.erase(player);
    m_seats.erase(player);
    m_transits.erase(player);
    m_radios.erase(player);
    m_stealth.erase(player);
    if (m_remoteTimes.erase(player) > 0)
      ++m_remoteTimeChanges;
  }
  {
    std::lock_guard _(m_reachLock);
    m_reach.Forget(player);
  }
  {
    std::lock_guard _(m_tracksLock);
    m_tracks.erase(player);
    m_vehicles.erase(player);
  }
  PresenceState::Get().Forget(player);
  // Its last state out of the captures' context (a player gone 20 min stayed
  // in it, "as it said 1287.9 s before", 2026-11-23).
  Observer::Get().Forget(player);
  std::lock_guard _(m_posesLock);
  m_poses.erase(player);
}

sync::View Runtime::ViewOf(proto::PlayerId player) {
  const auto now = HostNowUs();
  std::lock_guard _(m_tracksLock);
  const auto it = m_tracks.find(player);
  return it == m_tracks.end() ? sync::View{} : it->second.At(now);
}

std::optional<sync::TrackStats> Runtime::StatsOf(proto::PlayerId player) {
  std::lock_guard _(m_tracksLock);
  const auto it = m_tracks.find(player);
  if (it == m_tracks.end())
    return std::nullopt;
  return it->second.Stats();
}

std::uint64_t Runtime::HostNowUs() const {
  return static_cast<std::uint64_t>(static_cast<std::int64_t>(net::NowUs()) +
                                    m_clockOffsetUs.load());
}

void Runtime::RegisterPuppet(proto::PlayerId player, std::uint64_t entityHash) {
  UnregisterPuppet(player);
  for (auto &slot : m_puppets) {
    std::uint64_t empty = 0;
    if (slot.entity.compare_exchange_strong(empty, entityHash)) {
      slot.player.store(player);
      slot.wantsController.store(false);
      return;
    }
  }
  log::Warn("no room for another puppet (player {})", player);
  Degradations::Get().Fallback("no room", std::format("no puppet slot for player {}", player));
}

void Runtime::UnregisterPuppet(proto::PlayerId player) {
  for (auto &slot : m_puppets) {
    if (slot.player.load() == player && slot.entity.load() != 0) {
      slot.player.store(0);
      slot.entity.store(0);
      slot.wantsController.store(false);
    }
  }
}

proto::PlayerId Runtime::PuppetPlayer(std::uint64_t entityHash) const {
  if (entityHash == 0)
    return 0;
  for (const auto &slot : m_puppets) {
    if (slot.entity.load(std::memory_order_acquire) == entityHash)
      return slot.player.load(std::memory_order_acquire);
  }
  return 0;
}

void Runtime::RequestController(std::uint64_t entityHash) {
  for (auto &slot : m_puppets) {
    if (slot.entity.load() == entityHash)
      slot.wantsController.store(true);
  }
}

bool Runtime::TakeControllerRequest(proto::PlayerId player) {
  for (auto &slot : m_puppets) {
    if (slot.player.load() == player && slot.entity.load() != 0)
      return slot.wantsController.exchange(false);
  }
  return false;
}

void Runtime::RequestSnap(proto::PlayerId player) {
  std::lock_guard _(m_snapsLock);
  m_snaps.insert(player);
}

bool Runtime::TakeSnap(proto::PlayerId player) {
  // A held puppet (a grapple, a carrier, an NPC's workspot) is placed by
  // whoever holds it: a teleport asked for just before is dropped.
  const bool held = IsPuppetHeld(player);
  std::lock_guard _(m_snapsLock);
  return m_snaps.erase(player) != 0 && !held;
}
} // namespace tandem::plugin

namespace tandem::plugin {
namespace {
constexpr const char *kQueueNames[] = {
    "script events", "npc events",   "npc anims",   "action events", "hack events",
    "loot in",       "npc outbox",   "hack outbox", "loot outbox",   "npc shots",
    "shots at us",   "shot reports", "regroups",    "remote shots",  "hits on us"};
static_assert(std::size(kQueueNames) == Runtime::kQueues);
} // namespace

void Runtime::NoteDepth(std::size_t queue, std::size_t depth) {
  if (queue < kQueues && depth > m_queueMax[queue])
    m_queueMax[queue] = static_cast<std::uint32_t>(depth);
}

std::string Runtime::QueueReport() {
  std::string out;
  std::lock_guard _(m_lock);
  for (std::size_t i = 0; i < kQueues; ++i)
    if (const auto max = std::exchange(m_queueMax[i], 0))
      out += std::format("{}{} {}", out.empty() ? "" : ", ", kQueueNames[i], max);
  return out.empty() ? "all empty" : out;
}

void Runtime::RequestPerfReport() {
  if (!Started()) {
    // Solo: no net thread to say it, and nothing on the wire.
    log::Always("perf: {}; queues at most: {}", Perf::Get().Report(), QueueReport());
    return;
  }
  m_perfReport.store(true);
}
} // namespace tandem::plugin
