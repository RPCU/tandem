#include "combat.hpp"

#include <utility>

namespace tandem::plugin {
namespace {
// Scripts drain both every frame; a pile-up means nobody is listening.
constexpr std::size_t kMaxQueued = 512;
} // namespace

Combat &Combat::Get() {
  static Combat combat;
  return combat;
}

void Combat::PushInbound(proto::Message message) {
  std::lock_guard _(m_lock);
  if (m_inbound.size() < kMaxQueued)
    m_inbound.push_back(std::move(message));
}

std::vector<proto::Message> Combat::TakeOutbound() {
  std::lock_guard _(m_lock);
  return std::exchange(m_outbound, {});
}

std::vector<proto::Message> Combat::TakeInbound() {
  std::lock_guard _(m_lock);
  return std::exchange(m_inbound, {});
}

void Combat::Send(proto::Message message) {
  std::lock_guard _(m_lock);
  if (m_outbound.size() < kMaxQueued)
    m_outbound.push_back(std::move(message));
}
} // namespace tandem::plugin
