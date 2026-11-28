#include "story.hpp"
#include "observer.hpp"
#include "runtime.hpp"

#include "tandem/session.hpp"

#include <cmath>

namespace tandem::plugin {
namespace {
// A flood of journal changes (a quest that changes hundreds of entries in
// one go) is fine; this only stops a queue nobody drains from growing.
constexpr std::size_t kMaxQueued = 65536;
} // namespace

Story &Story::Get() {
  static Story story;
  return story;
}

void Story::PushInbound(StoryInbound inbound) {
  if (inbound.kind == StoryInbound::Kind::Message)
    Observer::Get().Story(inbound.message, true);
  std::lock_guard _(m_lock);
  if (m_inbound.size() < kMaxQueued)
    m_inbound.push_back(std::move(inbound));
}

void Story::SubmitCamera(const proto::SceneCamera &camera) {
  std::lock_guard _(m_cameraLock);
  // A cut the net thread never took is still owed with the newest view.
  m_cameraCutOwed = m_cameraCutOwed || (camera.flags & proto::kCameraCut) != 0;
  m_cameraOut = camera;
}

std::optional<proto::SceneCamera> Story::TakeCamera() {
  std::lock_guard _(m_cameraLock);
  auto camera = std::exchange(m_cameraOut, std::nullopt);
  if (camera && std::exchange(m_cameraCutOwed, false))
    camera->flags |= proto::kCameraCut;
  return camera;
}

void Story::PushCamera(const proto::SceneCamera &camera) {
  std::lock_guard _(m_cameraLock);
  m_cameraTrack.Push(camera);
}

bool Story::CameraAt(std::uint64_t hostNowUs, std::uint32_t play, scene::CameraView &out) {
  std::lock_guard _(m_cameraLock);
  return play != 0 && m_cameraTrack.Play() == play && m_cameraTrack.At(hostNowUs, out);
}

bool Story::CameraNow(std::uint64_t hostNowUs, scene::CameraView &out) {
  std::lock_guard _(m_cameraLock);
  return m_cameraTrack.Play() != 0 && m_cameraTrack.At(hostNowUs, out);
}

void Story::ClearCamera() {
  std::lock_guard _(m_cameraLock);
  m_cameraTrack.Clear();
  m_cameraOut.reset();
  m_cameraCutOwed = false;
}

std::uint32_t Story::CameraReceived() {
  std::lock_guard _(m_cameraLock);
  return m_cameraTrack.Received();
}

std::vector<std::pair<proto::Message, proto::PlayerId>> Story::TakeOutbound() {
  std::lock_guard _(m_lock);
  return std::exchange(m_outbound, {});
}

std::vector<StoryInbound> Story::TakeInbound() {
  std::lock_guard _(m_lock);
  return std::exchange(m_inbound, {});
}

void Story::Send(proto::Message message, proto::PlayerId to) {
  Observer::Get().Story(message, false);
  std::lock_guard _(m_lock);
  if (m_outbound.size() < kMaxQueued)
    m_outbound.emplace_back(std::move(message), to);
}

void Story::Note(const StoryInbound &inbound) {
  switch (inbound.kind) {
  case StoryInbound::Kind::InSession:
    m_you = inbound.player;
    m_host = inbound.host;
    break;
  case StoryInbound::Kind::Ended:
    m_you = m_host = proto::kNoPlayer;
    m_others.clear();
    m_participation = {};
    // The next session's scenes are counted from one again.
    m_stage = {};
    m_actors.clear();
    m_props.clear();
    m_placed.clear();
    m_bodies.clear();
    // The next session's host starts from no restriction. A guest's own
    // (what its V holds) stay for its script's leave (RestrictLift).
    m_sent.clear();
    // The next session's host sends its prefabs and everyone its gates
    // afresh. A guest's own prefabs stay for its script's leave (PrefabsLift).
    m_prefabsSent.clear();
    m_prefabsSaid = false;
    // Its open-world activities too; a guest's pins stay for its script's
    // leave (ActivitiesLift).
    m_openWorldSent.clear();
    m_openWorldSaid = false;
    m_openWorldAdded.clear();
    m_gatesSent = -1;
    m_gates.clear();
    m_gateWatcher.Clear();
    // Nobody's moment outlives the session (a guest's watching stops in its
    // script's leave, SpectateLift).
    m_moment.Clear();
    m_testMoment = false;
    m_spectateOff = false;
    // Nobody's pose stays held for a private scene (the next session's
    // player 2 is someone else), and ours is no longer held back.
    for (auto &slot : m_posePaused)
      slot.store(0);
    SetPrivate(false);
    // What the host's game answered was that session's world.
    m_interactKnown.Clear();
    m_interactAsks.clear();
    break;
  case StoryInbound::Kind::Joined:
    m_others.insert(inbound.player);
    break;
  case StoryInbound::Kind::Left:
    m_others.erase(inbound.player);
    m_participation.Forget(inbound.player);
    m_gates.erase(inbound.player);
    // Its questions are void (the endpoint sends no answer to them).
    std::erase_if(m_interactAsks,
                  [&](const auto &entry) { return entry.first.first == inbound.player; });
    // Gone in its private scene (it may have vanished without a word).
    PausePose(inbound.player, false);
    break;
  case StoryInbound::Kind::Message:
    break;
  }
}

std::vector<proto::PlayerId> Story::GuestsNear(const proto::Vec3 &host) const {
  std::vector<proto::PlayerId> near;
  for (const auto player : m_others) {
    if (player == m_host)
      continue;
    const auto view = Runtime::Get().ViewOf(player);
    if (!view.valid)
      continue;
    const float dx = view.position.x - host.x, dy = view.position.y - host.y,
                dz = view.position.z - host.z;
    if (std::sqrt(dx * dx + dy * dy + dz * dz) <= story::kNearMetres)
      near.push_back(player);
  }
  return near;
}

void Story::QuestMoved(std::uint32_t quest, const proto::Vec3 &host) {
  m_participation.Note(quest, GuestsNear(host), net::NowUs());
}

story::Participation::Award Story::Reward(const proto::Vec3 &host) {
  return m_participation.Attribute(net::NowUs(), GuestsNear(host));
}

void Story::SetPrivate(bool on) {
  if (m_private.exchange(on) != on && !on)
    m_releaseHeld = true;
}

void Story::FilterLocal(proto::PlayerState &state) {
  if (m_releaseHeld.exchange(false))
    m_held.reset();
  if (!m_private.load())
    return;
  if (!m_held)
    m_held = state;
  // Standing where the scene began: nothing of the scene shows.
  state.position = m_held->position;
  state.orientation = m_held->orientation;
  state.velocity = {};
  state.stance = 0;
}

void Story::PausePose(proto::PlayerId player, bool paused) {
  for (auto &slot : m_posePaused) {
    if (!paused && slot.load() == player)
      slot.store(0);
    else if (paused && slot.load() == player)
      return;
  }
  if (paused)
    for (auto &slot : m_posePaused) {
      proto::PlayerId empty = 0;
      if (slot.compare_exchange_strong(empty, player))
        return;
    }
}

bool Story::PosePaused(proto::PlayerId player) const {
  if (player == 0)
    return false;
  for (const auto &slot : m_posePaused)
    if (slot.load(std::memory_order_relaxed) == player)
      return true;
  return false;
}
} // namespace tandem::plugin
