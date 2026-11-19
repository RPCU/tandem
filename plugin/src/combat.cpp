#include "combat.hpp"

#include "tandem/combat.hpp"
#include "tandem/session.hpp"

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
    m_inbound.push_back({std::move(message), net::NowUs()});
}

std::vector<proto::Message> Combat::TakeOutbound() {
  std::lock_guard _(m_lock);
  return std::exchange(m_outbound, {});
}

std::vector<proto::Message> Combat::TakeInbound() {
  std::vector<Inbound> taken;
  {
    std::lock_guard _(m_lock);
    taken = std::exchange(m_inbound, {});
  }
  const auto now = net::NowUs();
  std::vector<proto::Message> out;
  out.reserve(taken.size());
  for (auto &entry : taken) {
    if (std::holds_alternative<proto::Throw>(entry.message) &&
        now - entry.arrivalUs > combat::kThrowStaleUs) {
      m_staleThrows.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    out.push_back(std::move(entry.message));
  }
  return out;
}

void Combat::Send(proto::Message message) {
  std::lock_guard _(m_lock);
  if (m_outbound.size() < kMaxQueued)
    m_outbound.push_back(std::move(message));
}
} // namespace tandem::plugin
