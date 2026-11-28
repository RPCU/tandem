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
    // A late word on where a throw is: the next one, or its end, says more.
    if (std::holds_alternative<proto::ThrowPath>(entry.message) &&
        now - entry.arrivalUs > combat::kThrowPathStaleUs)
      continue;
    out.push_back(std::move(entry.message));
  }
  return out;
}

void Combat::NoteLend(const proto::CarLend &lend) {
  std::lock_guard _(m_lock);
  m_lends[lend.player] = lend.lend;
}

proto::Lend Combat::LendOf(proto::PlayerId player) {
  std::lock_guard _(m_lock);
  const auto it = m_lends.find(player);
  return it == m_lends.end() ? proto::Lend::Crew : it->second;
}

void Combat::NotePaint(const proto::CarPaint &paint) {
  std::lock_guard _(m_lock);
  auto &kept = m_paints[paint.player];
  if (kept.paint == paint)
    return;
  kept.paint = paint;
  ++kept.changes;
}

Combat::Paint Combat::PaintOf(proto::PlayerId player) {
  std::lock_guard _(m_lock);
  const auto it = m_paints.find(player);
  return it == m_paints.end() ? Paint{.paint = {.player = player}} : it->second;
}

void Combat::NoteVendor(const proto::NpcVendor &vendor) {
  std::lock_guard _(m_lock);
  m_vendors.Note(vendor);
}

std::uint64_t Combat::VendorOf(proto::NpcId npc) {
  std::lock_guard _(m_lock);
  return m_vendors.EntityOf(npc);
}

bool Combat::VendorDead(std::uint64_t entity) {
  std::lock_guard _(m_lock);
  return m_vendors.Dead(entity);
}

std::vector<std::uint64_t> Combat::TakeDeadVendors() {
  std::lock_guard _(m_lock);
  return m_vendors.TakeDead();
}

void Combat::ClearVendors() {
  std::lock_guard _(m_lock);
  m_vendors.Clear();
}

void Combat::NoteBoss(const proto::BossState &boss) {
  std::lock_guard _(m_lock);
  // A few bosses at a time; a session that met many keeps the latest.
  if (m_bosses.size() >= 64 && !m_bosses.contains(boss.npc))
    m_bosses.clear();
  m_bosses[boss.npc] = boss;
}

proto::BossState Combat::BossOf(proto::NpcId npc) {
  std::lock_guard _(m_lock);
  const auto it = m_bosses.find(npc);
  return it == m_bosses.end() ? proto::BossState{.npc = npc} : it->second;
}

void Combat::ForgetBoss(proto::NpcId npc) {
  std::lock_guard _(m_lock);
  m_bosses.erase(npc);
}

void Combat::QueueBossAiEvent(BossAiEvent event) {
  std::lock_guard _(m_lock);
  if (m_bossAiEvents.size() < 64)
    m_bossAiEvents.push_back(event);
}

std::vector<Combat::BossAiEvent> Combat::TakeBossAiEvents() {
  std::lock_guard _(m_lock);
  return std::exchange(m_bossAiEvents, {});
}

void Combat::QueueBossKill(BossKill kill) {
  std::lock_guard _(m_lock);
  if (m_bossKills.size() < 16)
    m_bossKills.push_back(kill);
}

std::vector<Combat::BossKill> Combat::TakeBossKills() {
  std::lock_guard _(m_lock);
  return std::exchange(m_bossKills, {});
}

void Combat::EndSession() {
  {
    std::lock_guard _(m_lock);
    m_vendors.Clear();
    m_bosses.clear();
    m_bossAiEvents.clear();
    m_bossKills.clear();
    m_lends.clear();
    m_paints.clear();
  }
  SetRules({});
}

void Combat::Send(proto::Message message) {
  std::lock_guard _(m_lock);
  if (m_outbound.size() < kMaxQueued)
    m_outbound.push_back(std::move(message));
}
} // namespace tandem::plugin
