// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "log.hpp"
#include "net_thread.hpp"
#include "runtime.hpp"

#include "tandem/session.hpp"

namespace tandem::plugin {
Runtime &Runtime::Get() {
  static Runtime runtime;
  return runtime;
}

void Runtime::Start(proto::Body body) {
  std::lock_guard _(m_lock);
  if (m_net || config.role == Config::Role::Off)
    return;
  m_body = body;
  m_net = std::make_unique<NetThread>(config, body);
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
  config.role = Config::Role::Join;
  config.joinCode = *code;
  config.addressSet = false; // the code's host, not tandem.ini's address
  log::Info("join by code {} through relay {}:{}", net::DisplayJoinCode(*code), config.relay,
            config.relayPort);
  bool running = false;
  proto::Body body{};
  {
    std::lock_guard _(m_lock);
    running = m_net != nullptr;
    body = m_body;
  }
  if (running) {
    Stop();
    Start(body);
  }
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
    m_localRadios.clear();
    m_localTrack = m_localTrackStartUs = 0;
    m_radios.clear();
    m_localWorld.reset();
    m_hostWorld.reset();
    m_hostWorldChanges = 0;
    m_npcOutbox.clear();
    m_npcStates.reset();
    m_npcEvents.clear();
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
    m_hackOutbox.clear();
    m_hackEvents.clear();
    m_localPaused.reset();
    // Nobody pauses us any more: the change count moves so scripts let go.
    if (!m_remotePaused.empty()) {
      m_remotePaused.clear();
      ++m_remotePausedChanges;
    }
    m_names.clear();
    m_localTime.reset();
    // Nobody bends time around us any more: the count moves so scripts let go.
    if (!m_remoteTimes.empty()) {
      m_remoteTimes.clear();
      ++m_remoteTimeChanges;
    }
  }
  net.reset(); // joins outside the lock: the net thread takes it too
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
  m_localPose = pose;
}

std::optional<proto::PoseState> Runtime::TakeLocalPose() {
  std::lock_guard _(m_lock);
  return std::exchange(m_localPose, std::nullopt);
}

void Runtime::SubmitLocalAppearance(proto::Appearance appearance) {
  std::lock_guard _(m_lock);
  m_localAppearance = std::move(appearance);
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
    m_remoteShots.push_back(shot);
}

std::vector<proto::Shot> Runtime::TakeRemoteShots() {
  std::lock_guard _(m_lock);
  return std::exchange(m_remoteShots, {});
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

void Runtime::NotePlayerName(proto::PlayerId player, std::string name) {
  std::lock_guard _(m_lock);
  m_names[player] = std::move(name);
}

std::string Runtime::PlayerName(proto::PlayerId player) {
  std::lock_guard _(m_lock);
  const auto it = m_names.find(player);
  return it == m_names.end() ? std::string{} : it->second;
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

void Runtime::SubmitPlayerHit(const proto::PlayerHit &hit) {
  std::lock_guard _(m_lock);
  m_npcOutbox.push_back(hit);
}

void Runtime::PushPlayerHit(const proto::PlayerHit &hit) {
  std::lock_guard _(m_lock);
  if (m_playerHits.size() < 256)
    m_playerHits.push_back(hit);
}

std::vector<proto::PlayerHit> Runtime::TakePlayerHits() {
  std::lock_guard _(m_lock);
  return std::exchange(m_playerHits, {});
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
}

std::vector<NpcEvent> Runtime::TakeNpcEvents() {
  std::lock_guard _(m_lock);
  return std::exchange(m_npcEvents, {});
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
  if (m_hackEvents.size() < 512)
    m_hackEvents.push_back(event);
}

std::vector<HackEvent> Runtime::TakeHackEvents() {
  std::lock_guard _(m_lock);
  return std::exchange(m_hackEvents, {});
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
    m_remoteNoises.push_back(noise);
}

std::vector<proto::Noise> Runtime::TakeRemoteNoises() {
  std::lock_guard _(m_lock);
  return std::exchange(m_remoteNoises, {});
}

void Runtime::PushRemotePose(const proto::PoseState &pose) {
  std::lock_guard _(m_posesLock);
  m_poses[pose.player].Push(pose);
}

bool Runtime::PoseOf(proto::PlayerId player, pose::Pose &out) {
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
  std::lock_guard _(m_snapsLock);
  return m_snaps.erase(player) != 0;
}
} // namespace tandem::plugin
