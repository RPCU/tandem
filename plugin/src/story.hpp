#pragma once

// Story sync's queues between the net thread and the game thread
// (scripts/tandem/Story.reds through plugin/src/story_natives.cpp). Kept
// apart from Runtime: the story has its own messages and its own script.

#include "tandem/protocol.hpp"
#include "tandem/scene.hpp"
#include "tandem/story.hpp"

#include <array>
#include <atomic>
#include <cstdint>
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

  // Story restrictions (docs/architecture/story.md, "Restrictions"), game
  // thread. Guest: the host's set, what our V holds of it, whether it is in
  // the host's moment (last check), the removals the script has to make.
  // Host: the set last sent.
  story::Restrictions &Restrictions() { return m_restrictions; }
  bool &InMoment() { return m_inMoment; }
  std::vector<std::uint64_t> &Removals() { return m_removals; }
  std::vector<std::uint64_t> &Sent() { return m_sent; }
  // Host: the backstop on a guest's acts on the scene's actors.
  story::Backstop &Backstop() { return m_backstop; }

private:
  std::vector<proto::PlayerId> GuestsNear(const proto::Vec3 &host) const;

  scene::Stage m_stage;
  scene::Replay m_replay;
  std::vector<proto::Vec3> m_bodies;
  std::vector<scene::ActorSample> m_actors;
  story::Restrictions m_restrictions;
  bool m_inMoment{};
  std::vector<std::uint64_t> m_removals;
  std::vector<std::uint64_t> m_sent;
  story::Backstop m_backstop;

  std::atomic_bool m_private{false};
  std::optional<proto::PlayerState> m_held; // net thread
  std::atomic_bool m_releaseHeld{false};
  std::array<std::atomic<proto::PlayerId>, 8> m_posePaused{};

  std::mutex m_lock; // the two queues
  std::vector<StoryInbound> m_inbound;
  std::vector<std::pair<proto::Message, proto::PlayerId>> m_outbound;

  // Game thread only.
  proto::PlayerId m_you{}, m_host{};
  std::set<proto::PlayerId> m_others;
  story::Participation m_participation;
};
} // namespace tandem::plugin
