#pragma once

// Story sync's queues between the net thread and the game thread
// (scripts/tandem/Story.reds through plugin/src/story_natives.cpp). Kept
// apart from Runtime: the story has its own messages and its own script.

#include "tandem/protocol.hpp"
#include "tandem/story.hpp"

#include <cstdint>
#include <mutex>
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

private:
  std::vector<proto::PlayerId> GuestsNear(const proto::Vec3 &host) const;

  std::mutex m_lock; // the two queues
  std::vector<StoryInbound> m_inbound;
  std::vector<std::pair<proto::Message, proto::PlayerId>> m_outbound;

  // Game thread only.
  proto::PlayerId m_you{}, m_host{};
  std::set<proto::PlayerId> m_others;
  story::Participation m_participation;
};
} // namespace tandem::plugin
