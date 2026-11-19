#include "trade.hpp"

#include <utility>

namespace tandem::plugin {
namespace {
// Scripts drain both every frame; a pile-up means nobody is listening.
constexpr std::size_t kMaxQueued = 256;
} // namespace

TradeLink &TradeLink::Get() {
  static TradeLink link;
  return link;
}

void TradeLink::PushInbound(proto::Message message) {
  std::lock_guard _(m_lock);
  // A commit is never dropped: it is the one message that must be applied.
  if (m_inbound.size() < kMaxQueued || std::holds_alternative<proto::TradeCommit>(message))
    m_inbound.push_back(std::move(message));
}

std::vector<proto::Message> TradeLink::TakeOutbound() {
  std::lock_guard _(m_lock);
  return std::exchange(m_outbound, {});
}

std::deque<proto::Message> TradeLink::TakeInbound() {
  std::lock_guard _(m_lock);
  return std::exchange(m_inbound, {});
}

void TradeLink::Send(proto::Message message) {
  std::lock_guard _(m_lock);
  if (m_outbound.size() < kMaxQueued)
    m_outbound.push_back(std::move(message));
}

void TradeLink::Send(const std::vector<proto::Message> &messages) {
  for (const auto &message : messages)
    Send(message);
}
} // namespace tandem::plugin
