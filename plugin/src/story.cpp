#include "story.hpp"
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
  std::lock_guard _(m_lock);
  if (m_inbound.size() < kMaxQueued)
    m_inbound.push_back(std::move(inbound));
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
    break;
  case StoryInbound::Kind::Joined:
    m_others.insert(inbound.player);
    break;
  case StoryInbound::Kind::Left:
    m_others.erase(inbound.player);
    m_participation.Forget(inbound.player);
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
} // namespace tandem::plugin
