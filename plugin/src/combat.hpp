#pragma once

// Hit outcomes' queues between the net thread and the game thread
// (scripts/tandem/Outcomes.reds through plugin/src/combat_natives.cpp). Kept
// apart from Runtime, like the story: its own messages (proto::IsCombat).

#include "tandem/protocol.hpp"

#include <mutex>
#include <vector>

namespace tandem::plugin {
class Combat {
public:
  static Combat &Get();

  // Net thread: what the session says (NpcStrike from a guest, which a
  // guest's older NpcHit also becomes; NpcWound, NpcPoise, Difficulty from
  // the host; AreaHit on our V), and what we have to say.
  void PushInbound(proto::Message message);
  std::vector<proto::Message> TakeOutbound();

  // Game thread.
  std::vector<proto::Message> TakeInbound();
  void Send(proto::Message message);

private:
  std::mutex m_lock;
  std::vector<proto::Message> m_inbound, m_outbound;
};
} // namespace tandem::plugin
