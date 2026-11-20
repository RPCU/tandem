#include "regions.hpp"

#include <algorithm>
#include <utility>

namespace tandem::plugin {
Regions &Regions::Get() {
  static Regions instance;
  return instance;
}

void Regions::Push(RegionEvent event) {
  std::lock_guard _(m_lock);
  // Scripts drain these every frame; a pile-up means nobody is listening
  // (the host retries a hand-over that got no answer).
  if (m_events.size() < 512)
    m_events.push_back(std::move(event));
}

void Regions::SetOwners(std::map<proto::NpcId, proto::PlayerId> owners) {
  std::lock_guard _(m_lock);
  m_owners = std::move(owners);
}

void Regions::SetLinked(std::vector<proto::PlayerId> linked) {
  std::lock_guard _(m_lock);
  m_linked = std::move(linked);
}

void Regions::SetGroup(proto::PlayerId leader, std::set<proto::PlayerId> holders) {
  std::lock_guard _(m_lock);
  m_leader = leader;
  m_holders = std::move(holders);
}

void Regions::SetAreas(std::map<proto::PlayerId, proto::PlayerId> areas) {
  std::lock_guard _(m_lock);
  m_areas = std::move(areas);
}

proto::PlayerId Regions::AreaOwner(proto::PlayerId player) {
  std::lock_guard _(m_lock);
  const auto it = m_areas.find(player);
  return it == m_areas.end() ? proto::kNoPlayer : it->second;
}

bool Regions::IsApart() {
  std::lock_guard _(m_lock);
  return m_leader != proto::kNoPlayer && m_linked.empty();
}

proto::PlayerId Regions::Leader() {
  std::lock_guard _(m_lock);
  return m_leader;
}

bool Regions::Holds(proto::PlayerId player) {
  std::lock_guard _(m_lock);
  return m_holders.contains(player);
}

std::set<proto::PlayerId> Regions::Holders() {
  std::lock_guard _(m_lock);
  return m_holders;
}

std::vector<RegionAnswer> Regions::TakeAnswers() {
  std::lock_guard _(m_lock);
  return std::exchange(m_answers, {});
}

void Regions::Reset() {
  std::lock_guard _(m_lock);
  m_events.clear();
  m_answers.clear();
  m_owners.clear();
  m_linked.clear();
  m_leader = proto::kNoPlayer;
  m_holders.clear();
  m_areas.clear();
  m_placed.clear();
  m_deaths.Clear();
  m_deathNews.clear();
}

void Regions::NotePlaced(const proto::NpcPlaced &placed) {
  std::lock_guard _(m_lock);
  // Never in play: the lowest id goes, the host's oldest.
  if (m_placed.size() >= 2048 && !m_placed.contains(placed.npc))
    m_placed.erase(m_placed.begin());
  m_placed[placed.npc] = placed.entity;
}

std::uint64_t Regions::PlacedOf(proto::NpcId npc) {
  std::lock_guard _(m_lock);
  const auto it = m_placed.find(npc);
  return it == m_placed.end() ? 0 : it->second;
}

void Regions::NoteDeath(const proto::NpcDeath &death) {
  std::lock_guard _(m_lock);
  if (m_deaths.Note(death) && m_deathNews.size() < proto::kMaxDeaths)
    m_deathNews.push_back(death);
}

proto::PlayerId Regions::DiedBy(std::uint64_t entity) {
  std::lock_guard _(m_lock);
  const auto *death = m_deaths.Find(entity);
  return death ? death->by : proto::kNoPlayer;
}

std::vector<proto::NpcDeath> Regions::Deaths() {
  std::lock_guard _(m_lock);
  return m_deaths.InOrder();
}

std::vector<proto::NpcDeath> Regions::TakeDeaths() {
  std::lock_guard _(m_lock);
  return std::exchange(m_deathNews, {});
}

std::vector<RegionEvent> Regions::Take() {
  std::lock_guard _(m_lock);
  return std::exchange(m_events, {});
}

void Regions::Answer(RegionAnswer answer) {
  std::lock_guard _(m_lock);
  if (m_answers.size() < 512)
    m_answers.push_back(std::move(answer));
}

proto::PlayerId Regions::OwnerOf(proto::NpcId npc) {
  std::lock_guard _(m_lock);
  const auto it = m_owners.find(npc);
  return it == m_owners.end() ? proto::kNoPlayer : it->second;
}

bool Regions::IsLinked(proto::PlayerId player) {
  std::lock_guard _(m_lock);
  return std::ranges::find(m_linked, player) != m_linked.end();
}

std::vector<proto::PlayerId> Regions::Linked() {
  std::lock_guard _(m_lock);
  return m_linked;
}
} // namespace tandem::plugin
