#include "tandem/vendors.hpp"

#include <utility>

namespace tandem::vendors {
Shop Decide(const Ask &ask) {
  if (!ask.vendorRecord)
    return Shop::NotVendor;
  if (ask.entity == 0)
    return Shop::Unknown;
  if (!(ask.distance <= kReach)) // NaN: out of reach
    return Shop::Far;
  if (ask.proxyDead || ask.sessionDead)
    return Shop::Dead;
  if (!ask.free)
    return Shop::Busy;
  // Our own world decides from here on: our save's vendor, or none.
  if (!ask.ownFound || !ask.ownVendor)
    return Shop::NotHere;
  if (!ask.ownAlive)
    return Shop::Dead;
  if (!ask.accessible)
    return Shop::Locked;
  return Shop::Open;
}

bool Prompts(Shop shop) {
  return shop != Shop::NotVendor && shop != Shop::Unknown && shop != Shop::Far;
}

const char *NameOf(Shop shop) {
  switch (shop) {
  case Shop::Open:
    return "open";
  case Shop::NotVendor:
    return "not a vendor";
  case Shop::Unknown:
    return "no placed vendor";
  case Shop::Far:
    return "out of reach";
  case Shop::Dead:
    return "dead";
  case Shop::Busy:
    return "not now";
  case Shop::NotHere:
    return "not in our world";
  case Shop::Locked:
    return "locked for us";
  }
  return "?";
}

bool Book::Note(const proto::NpcVendor &vendor) {
  if (vendor.entity == 0)
    return false;
  if ((vendor.flags & proto::kVendorDead) != 0) {
    if (m_dead.contains(vendor.entity))
      return false;
    if (m_dead.size() >= kMaxDead)
      return false; // a session never kills that many shops: a flood
    m_dead.insert(vendor.entity);
    m_news.push_back(vendor.entity);
    return true;
  }
  if (vendor.npc == 0)
    return false;
  if (const auto it = m_live.find(vendor.npc); it != m_live.end()) {
    if (it->second == vendor.entity)
      return false;
    it->second = vendor.entity;
    return true;
  }
  // Full (never in play): the lowest id goes, the host's oldest.
  if (m_live.size() >= kMaxLive)
    m_live.erase(m_live.begin());
  m_live[vendor.npc] = vendor.entity;
  return true;
}

std::uint64_t Book::EntityOf(proto::NpcId npc) const {
  const auto it = m_live.find(npc);
  return it != m_live.end() ? it->second : 0;
}

std::vector<std::uint64_t> Book::TakeDead() { return std::exchange(m_news, {}); }

void Book::Clear() {
  m_live.clear();
  m_dead.clear();
  m_news.clear();
}
} // namespace tandem::vendors
