#pragma once

// Trade's queues between the net thread and the game thread, and our seat
// at the table (scripts/tandem/Trade.reds through
// plugin/src/trade_natives.cpp). Kept apart from Runtime, like hit outcomes
// and the story: its own messages (proto::IsTrade).

#include "tandem/protocol.hpp"
#include "tandem/trade.hpp"

#include <deque>
#include <mutex>
#include <vector>

namespace tandem::plugin {
class TradeLink {
public:
  static TradeLink &Get();

  // Net thread: what the host says to our seat, and what our seat says.
  void PushInbound(proto::Message message);
  std::vector<proto::Message> TakeOutbound();

  // Game thread.
  std::deque<proto::Message> TakeInbound();
  void Send(proto::Message message);
  void Send(const std::vector<proto::Message> &messages);

private:
  std::mutex m_lock;
  std::deque<proto::Message> m_inbound;
  std::vector<proto::Message> m_outbound;
};
} // namespace tandem::plugin
