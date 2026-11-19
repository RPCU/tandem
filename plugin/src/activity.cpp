#include "activity.hpp"

#include "tandem/activity.hpp"

#include <utility>

namespace tandem::plugin {
namespace {
// Scripts drain these every tick; a pile-up means nobody is listening.
constexpr std::size_t kMaxQueued = 32;
} // namespace

Activities &Activities::Get() {
  static Activities activities;
  return activities;
}

void Activities::SetLocal(const proto::Activity &state) {
  std::lock_guard _(m_lock);
  const auto safe = activity::Sanitized(state);
  if (safe == m_local)
    return;
  m_local = safe;
  m_localToSend = safe;
}

bool Activities::LocalFrozen() {
  std::lock_guard _(m_lock);
  return activity::Frozen(m_local);
}

void Activities::Send(SkipOut message) {
  std::lock_guard _(m_lock);
  if (m_outbound.size() < kMaxQueued)
    m_outbound.push_back(std::move(message));
}

std::map<proto::PlayerId, proto::Activity> Activities::Remote(std::uint32_t &changes) {
  std::lock_guard _(m_lock);
  changes = m_remoteChanges;
  return m_remote;
}

std::vector<SkipEvent> Activities::TakeEvents() {
  std::lock_guard _(m_lock);
  return std::exchange(m_events, {});
}

std::optional<proto::Activity> Activities::TakeLocal() {
  std::lock_guard _(m_lock);
  return std::exchange(m_localToSend, std::nullopt);
}

std::vector<SkipOut> Activities::TakeOutbound() {
  std::lock_guard _(m_lock);
  return std::exchange(m_outbound, {});
}

void Activities::PushRemote(const proto::Activity &state) {
  std::lock_guard _(m_lock);
  if (state.kind == proto::ActivityKind::None)
    m_remote.erase(state.player);
  else
    m_remote[state.player] = state;
  ++m_remoteChanges;
}

void Activities::PushEvent(SkipEvent event) {
  std::lock_guard _(m_lock);
  if (m_events.size() < kMaxQueued)
    m_events.push_back(std::move(event));
}

void Activities::EndSession() {
  std::lock_guard _(m_lock);
  if (!m_remote.empty()) {
    m_remote.clear();
    ++m_remoteChanges;
  }
  m_outbound.clear();
  m_events.clear();
}
} // namespace tandem::plugin
