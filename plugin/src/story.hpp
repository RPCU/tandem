#pragma once

// Story sync's queues between the net thread and the game thread
// (scripts/tandem/Story.reds through plugin/src/story_natives.cpp). Kept
// apart from Runtime: the story has its own messages and its own script.

#include "tandem/interact.hpp"
#include "tandem/protocol.hpp"
#include "tandem/scene.hpp"
#include "tandem/story.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace tandem::plugin {
// What the net thread tells the story script.
struct StoryInbound {
  enum class Kind { InSession, Ended, Joined, Left, Message };
  Kind kind{};
  proto::PlayerId player{}; // InSession: us; Joined, Left: who
  proto::PlayerId host{};   // InSession
  proto::Message message;   // Message
};

class Story {
public:
  static Story &Get();

  // Net thread.
  void PushInbound(StoryInbound inbound);
  std::vector<std::pair<proto::Message, proto::PlayerId>> TakeOutbound();

  // Game thread.
  std::vector<StoryInbound> TakeInbound();
  void Send(proto::Message message, proto::PlayerId to = proto::kNoPlayer);

  // Game thread: the session as the story script sees it (from TakeInbound).
  void Note(const StoryInbound &inbound);
  proto::PlayerId You() const { return m_you; }
  proto::PlayerId Host() const { return m_host; }
  const std::set<proto::PlayerId> &Others() const { return m_others; }

  // Game thread, host: taking part (core/story.hpp), with the guests' V
  // positions from their tracks.
  void QuestMoved(std::uint32_t quest, const proto::Vec3 &host);
  story::Participation::Award Reward(const proto::Vec3 &host);

  // Private scenes (design §2.12). Game thread: our V is in one. Net thread:
  // while it is, our V is sent standing where the scene began and its pose
  // is not sent (FilterLocal: false drops the message).
  void SetPrivate(bool on);
  bool Private() const { return m_private.load(); }
  void FilterLocal(proto::PlayerState &state);
  // Game thread writes, animation threads read: a busy player's puppet
  // shows its own idle, not that player's pose.
  void PausePose(proto::PlayerId player, bool paused);
  bool PosePaused(proto::PlayerId player) const;

  // Scene replay (docs/architecture/scene-replay.md), game thread. Host:
  // what its game samples becomes what its guests are told. Guest: what the
  // host says becomes cues for the script.
  scene::Stage &Stage() { return m_stage; }
  scene::Replay &Replay() { return m_replay; }
  // The bodies around a scene, as the script lists them (actors, the host's
  // V, the other watchers), for WatchSpot and KeepOut.
  std::vector<proto::Vec3> &Bodies() { return m_bodies; }
  std::vector<scene::ActorSample> &Actors() { return m_actors; }
  // Host: what the scene's actors hold, as the script lists it for the next sample.
  std::vector<scene::PropSample> &Props() { return m_props; }
  // Host: what the scene set down that nobody holds, for the next sample.
  std::vector<scene::PlaceSample> &Placed() { return m_placed; }

  // The watch camera (scene-replay.md, "The watch camera"). Host, game
  // thread: our game's view now, for the guests (the newest only; the net
  // thread takes it). Guest: the host's views as the net thread hears them,
  // and the one to show now (game thread).
  void SubmitCamera(const proto::SceneCamera &camera);
  std::optional<proto::SceneCamera> TakeCamera();
  void PushCamera(const proto::SceneCamera &camera);
  bool CameraAt(std::uint64_t hostNowUs, std::uint32_t play, scene::CameraView &out);
  // Guest, spectating: the host's view now, whatever play it is of (a scene
  // or a moment outside one).
  bool CameraNow(std::uint64_t hostNowUs, scene::CameraView &out);
  void ClearCamera();
  std::uint32_t CameraReceived();
  // Host, game thread: the last view sampled and when (cuts are told from it).
  scene::CameraView &LastView() { return m_lastView; }
  std::uint64_t &LastViewUs() { return m_lastViewUs; }

  // Story restrictions (docs/architecture/story.md, "Restrictions"), game
  // thread. Guest: the host's set, what our V holds of it, whether it is in
  // the host's moment (last check), the removals the script has to make.
  // Host: the set last sent.
  story::Restrictions &Restrictions() { return m_restrictions; }
  bool &InMoment() { return m_inMoment; }
  std::vector<std::uint64_t> &Removals() { return m_removals; }
  std::vector<std::uint64_t> &Sent() { return m_sent; }
  // Spectating (docs/architecture/story.md, "Spectating"), game thread.
  // Host: its V's scripted moment. Guest: whether it watches it; whether
  // the player turned it off for the session (cmd story spectate off).
  story::HostMoment &Moment() { return m_moment; }
  story::Spectator &Spectator() { return m_spectator; }
  bool &SpectateOff() { return m_spectateOff; }
  // Host: cmd story spectate on: a test moment.
  bool &TestMoment() { return m_testMoment; }
  // Host: the backstop on a guest's acts on the scene's actors.
  story::Backstop &Backstop() { return m_backstop; }

  // Quest interactions (docs/architecture/story.md, "Quest interactions"),
  // game thread. Guest: what the host answered of each object's choices
  // (a known host-only prompt is greyed before it is pressed), our count of
  // questions. Host: each guest's question as it came, and when (the reach
  // check rewinds to it), until our game answers it.
  struct HeldAsk {
    proto::QuestInteract ask;
    std::uint64_t arrivalUs{};
  };
  interact::Known &InteractKnown() { return m_interactKnown; }
  std::uint32_t NextInteractSeq() { return ++m_interactSeq; }
  std::map<std::pair<proto::PlayerId, std::uint32_t>, HeldAsk> &InteractAsks() {
    return m_interactAsks;
  }

private:
  std::vector<proto::PlayerId> GuestsNear(const proto::Vec3 &host) const;

  interact::Known m_interactKnown;
  std::uint32_t m_interactSeq{};
  std::map<std::pair<proto::PlayerId, std::uint32_t>, HeldAsk> m_interactAsks;

  scene::Stage m_stage;
  scene::Replay m_replay;
  std::vector<proto::Vec3> m_bodies;
  std::vector<scene::ActorSample> m_actors;
  std::vector<scene::PropSample> m_props;
  std::vector<scene::PlaceSample> m_placed;
  story::Restrictions m_restrictions;
  bool m_inMoment{};
  std::vector<std::uint64_t> m_removals;
  std::vector<std::uint64_t> m_sent;
  story::Backstop m_backstop;
  story::HostMoment m_moment;
  story::Spectator m_spectator;
  bool m_spectateOff{};
  bool m_testMoment{};

  std::atomic_bool m_private{false};
  std::optional<proto::PlayerState> m_held; // net thread
  std::atomic_bool m_releaseHeld{false};
  std::array<std::atomic<proto::PlayerId>, 8> m_posePaused{};

  std::mutex m_cameraLock; // the three below
  std::optional<proto::SceneCamera> m_cameraOut;
  scene::CameraTrack m_cameraTrack;
  bool m_cameraCutOwed{};
  scene::CameraView m_lastView; // game thread
  std::uint64_t m_lastViewUs{};

  std::mutex m_lock; // the two queues
  std::vector<StoryInbound> m_inbound;
  std::vector<std::pair<proto::Message, proto::PlayerId>> m_outbound;

  // Game thread only.
  proto::PlayerId m_you{}, m_host{};
  std::set<proto::PlayerId> m_others;
  story::Participation m_participation;
};
} // namespace tandem::plugin
