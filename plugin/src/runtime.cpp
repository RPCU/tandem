// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "installed.hpp"
#include "log.hpp"
#include "net_thread.hpp"
#include "runtime.hpp"
#include "story.hpp"

#include "tandem/report.hpp"
#include "tandem/session.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>

namespace tandem::plugin {
Runtime &Runtime::Get() {
  static Runtime runtime;
  return runtime;
}

void Runtime::Start(proto::Body body) {
  std::lock_guard _(m_lock);
  if (m_net || m_choice.role == Config::Role::Off)
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
}

void Runtime::Choose(SessionChoice choice) {
  if (Started()) {
    log::Info("session: stopping for the new choice");
    Stop();
  }
  // Nothing to choose while a startup problem keeps co-op off: the menu
  // says why (CompatProblems), and the game stays the single-player one.
  if (choice.role != Config::Role::Off && Blocked()) {
    log::Warn("session: {} not chosen, co-op is off: {}", NameOf(choice.role),
              installation.startup.problems.front());
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
                     .versions = VersionsText(installation.facts, installation.startup,
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

void Runtime::Stop() {
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
    m_npcStates.reset();
    m_npcEvents.clear();
    m_npcReplies.clear();
    m_actionEvents.clear();
    m_npcDetected.clear();
    m_detectingUs = 0;
    m_npcStatesAtUs = 0;
    m_awarenessOfUs.clear();
    m_awarenessAtUs = 0;
    m_localAwareness.reset();
    m_localStealth.reset();
    m_stealth.clear();
    m_localNoises.clear();
    m_remoteNoises.clear();
    m_sharedNpcs.clear();
    m_roster.clear();
    m_epochRequest.reset();
    m_overflows = 0;
    m_hackOutbox.clear();
    m_hackEvents.clear();
    m_lootOutbox.clear();
    m_lootInbox.clear();
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
  net.reset(); // joins outside the lock: the net thread takes it too
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
}

bool Runtime::Started() const {
  std::lock_guard _(m_lock);
  return m_net != nullptr;
}

void Runtime::SubmitLocal(const proto::PlayerState &state) {
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
  return car->second.At(now > delayUs ? now - delayUs : 0);
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
  std::lock_guard _(m_lock);
  m_localPaused = paused;
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
  return std::exchange(m_shotHits, {});
}

std::uint8_t Runtime::NpcDetected(proto::NpcId npc) {
  std::lock_guard _(m_lock);
  const auto it = m_npcDetected.find(npc);
  return it == m_npcDetected.end() ? 0 : it->second;
}

void Runtime::SubmitNpcStates(const proto::NpcStates &states) {
  std::lock_guard _(m_lock);
  m_npcStates = states;
}

std::vector<proto::Message> Runtime::TakeNpcOutbox() {
  std::lock_guard _(m_lock);
  return std::exchange(m_npcOutbox, {});
}

std::optional<proto::NpcStates> Runtime::TakeNpcStates() {
  std::lock_guard _(m_lock);
  return std::exchange(m_npcStates, std::nullopt);
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
  return std::exchange(m_hackEvents, {});
}

void Runtime::SubmitLoot(proto::Message message) {
  std::lock_guard _(m_lock);
  if (m_lootOutbox.size() < 256)
    m_lootOutbox.push_back(std::move(message));
}

std::vector<proto::Message> Runtime::TakeLootOutbox() {
  std::lock_guard _(m_lock);
  return std::exchange(m_lootOutbox, {});
}

void Runtime::PushLoot(proto::Message message) {
  std::lock_guard _(m_lock);
  if (m_lootInbox.size() < 1024)
    m_lootInbox.push_back(std::move(message));
}

std::vector<proto::Message> Runtime::TakeLoot() {
  std::lock_guard _(m_lock);
  return std::exchange(m_lootInbox, {});
}

void Runtime::PushNpcStates(const proto::NpcStates &states, proto::PlayerId you) {
  {
    // The states list every NPC the host shares: who is not in them
    // detects nobody any more.
    std::lock_guard _(m_lock);
    m_npcDetected.clear();
    m_detectingUs = 0;
    const auto ours = you >= 1 && you <= 8 ? static_cast<std::uint8_t>(1u << (you - 1)) : 0;
    for (const auto &entry : states.npcs) {
      m_npcDetected[entry.npc] = entry.detected;
      if (entry.detected & ours)
        ++m_detectingUs;
    }
    m_npcStatesAtUs = HostNowUs();
  }
  // Each NPC is a track of its own, drawn like a remote player.
  for (const auto &entry : states.npcs) {
    proto::PlayerState state;
    state.player = kNpcPuppetBase + entry.npc;
    state.seq = states.seq;
    state.timeUs = states.timeUs;
    state.position = entry.position;
    state.orientation = sync::FromYaw(entry.yaw);
    PushRemote(state);
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

void Runtime::PushNpcAnims(const proto::NpcAnims &anims) {
  std::lock_guard _(m_lock);
  for (const auto &anim : anims.npcs)
    if (m_npcAnimsIn.size() < 1024)
      m_npcAnimsIn.push_back(anim);
}

std::vector<proto::NpcAnim> Runtime::TakeNpcAnims() {
  std::lock_guard _(m_lock);
  return std::exchange(m_npcAnimsIn, {});
}

void Runtime::PushNpcShot(const proto::NpcShot &shot) {
  std::lock_guard _(m_lock);
  if (m_npcShotsIn.size() < 256)
    m_npcShotsIn.push_back(shot);
}

std::vector<proto::NpcShot> Runtime::TakeNpcShots() {
  std::lock_guard _(m_lock);
  return std::exchange(m_npcShotsIn, {});
}

std::int32_t Runtime::NpcsDetectingUs() {
  const auto now = HostNowUs();
  std::lock_guard _(m_lock);
  // The host went quiet (or left): nobody can be hunting us on its say.
  // (Signed: the clock offset may move a stamp a little into the future.)
  return !m_npcStatesAtUs || static_cast<std::int64_t>(now - m_npcStatesAtUs) > 3'000'000
             ? 0
             : m_detectingUs;
}

float Runtime::NpcAwarenessOfUs(proto::NpcId npc) {
  const auto now = HostNowUs();
  std::lock_guard _(m_lock);
  if (!m_awarenessAtUs || static_cast<std::int64_t>(now - m_awarenessAtUs) > 1'000'000)
    return 0.f;
  const auto it = m_awarenessOfUs.find(npc);
  return it == m_awarenessOfUs.end() ? 0.f : it->second;
}

void Runtime::PushNpcAwareness(const proto::NpcAwareness &awareness, proto::PlayerId you) {
  std::lock_guard _(m_lock);
  m_awarenessOfUs.clear();
  for (const auto &entry : awareness.entries)
    if (entry.player == you)
      m_awarenessOfUs[entry.npc] = static_cast<float>(entry.level) / 255.f;
  m_awarenessAtUs = HostNowUs();
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
  {
    std::lock_guard _(m_lock);
    m_localShots.clear();
    m_remoteShots.clear();
    m_npcOutbox.clear();
    m_npcStates.reset();
    m_npcEvents.clear();
    m_playerHits.clear();
    m_hackOutbox.clear();
    m_hackEvents.clear();
    m_npcDetected.clear();
    m_detectingUs = 0;
    m_npcStatesAtUs = 0;
    m_awarenessOfUs.clear();
    m_awarenessAtUs = 0;
    m_localAwareness.reset();
    m_localNoises.clear();
    m_remoteNoises.clear();
    m_sharedNpcs.clear();
    m_roster.clear();
    m_epochRequest.reset();
    // Bags, claims, revives and offers name the old session's numbers.
    m_lootOutbox.clear();
    m_lootInbox.clear();
    m_localRevives.clear();
    m_revives.clear();
    m_regroupOutbox.clear();
    m_regroupEvents.clear();
  }
  // The NPCs' tracks: the next session's ids are other NPCs.
  std::lock_guard _(m_tracksLock);
  std::erase_if(m_tracks, [](const auto &entry) { return entry.first >= kNpcPuppetBase; });
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
  std::lock_guard _(m_posesLock);
  const auto it = m_poses.find(player);
  return it != m_poses.end() && it->second.At(now > delayUs ? now - delayUs : 0, out);
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
  return std::exchange(m_events, {});
}

void Runtime::PushRemote(const proto::PlayerState &state) {
  const auto arrival = HostNowUs();
  std::lock_guard _(m_tracksLock);
  m_tracks.try_emplace(state.player).first->second.Push(state, arrival);
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
    std::lock_guard _(m_tracksLock);
    m_tracks.erase(player);
    m_vehicles.erase(player);
  }
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
