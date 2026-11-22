#include "tandem/regions.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numbers>

namespace tandem::regions {
float Distance(const proto::Vec3 &a, const proto::Vec3 &b) {
  const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool Links::Linked(proto::PlayerId a, proto::PlayerId b) const {
  if (a == b || m_waiting.contains(a) || m_waiting.contains(b))
    return false;
  if (m_fromWire && (!m_named.contains(a) || !m_named.contains(b)))
    return true; // someone the host has not placed yet
  return !m_apart.contains(Key(a, b));
}

bool Links::Update(const Places &places) {
  bool changed = false;
  // A pair with a player that left is no pair.
  std::erase_if(m_apart, [&](const Pair &pair) {
    return !places.contains(pair.first) || !places.contains(pair.second);
  });
  std::erase_if(m_waiting, [&](proto::PlayerId player) { return !places.contains(player); });
  for (auto a = places.begin(); a != places.end(); ++a)
    for (auto b = std::next(a); b != places.end(); ++b) {
      const auto key = Key(a->first, b->first);
      bool apart = m_apart.contains(key);
      if (m_waiting.contains(a->first) || m_waiting.contains(b->first)) {
        // A joiner (Linked says apart already): apart until both are
        // placed, then linked only once closer than kLinkIn.
        if (!apart)
          m_apart.insert(key);
        apart = true;
        if (!a->second.known || !b->second.known)
          continue;
      } else if (!a->second.known || !b->second.known) {
        // Not placed (yet, or any more): together until we know better.
        if (apart) {
          m_apart.erase(key);
          changed = true;
        }
        continue;
      }
      const auto distance = Distance(a->second.position, b->second.position);
      if (!apart && distance > kLinkOut) {
        m_apart.insert(key);
        changed = true;
      } else if (apart && distance < kLinkIn) {
        m_apart.erase(key);
        changed = true;
      }
    }
  // Placed: from now on as everyone else.
  std::erase_if(m_waiting, [&](proto::PlayerId player) { return places.at(player).known; });
  return changed;
}

void Links::Set(const proto::Regions &regions) {
  m_fromWire = true;
  m_named.clear();
  m_apart.clear();
  std::set<Pair> linked;
  for (std::size_t i = 0; i + 1 < regions.pairs.size(); i += 2) {
    const auto a = regions.pairs[i], b = regions.pairs[i + 1];
    m_named.insert(a);
    m_named.insert(b);
    if (a != b)
      linked.insert(Key(a, b));
  }
  // Two named players that are not listed together are apart.
  for (auto a = m_named.begin(); a != m_named.end(); ++a)
    for (auto b = std::next(a); b != m_named.end(); ++b)
      if (!linked.contains(Key(*a, *b)))
        m_apart.insert(Key(*a, *b));
}

std::vector<proto::PlayerId> Links::Pairs(const std::set<proto::PlayerId> &players) const {
  std::vector<proto::PlayerId> out;
  for (auto a = players.begin(); a != players.end(); ++a) {
    out.push_back(*a);
    out.push_back(*a);
    for (auto b = std::next(a); b != players.end(); ++b)
      if (Linked(*a, *b)) {
        out.push_back(*a);
        out.push_back(*b);
      }
  }
  return out;
}

void Links::Forget(proto::PlayerId player) {
  std::erase_if(m_apart,
                [&](const Pair &pair) { return pair.first == player || pair.second == player; });
  m_named.erase(player);
  m_waiting.erase(player);
}

void Links::Clear() {
  m_apart.clear();
  m_named.clear();
  m_waiting.clear();
  m_fromWire = false;
}

proto::PlayerId Taker(const proto::Vec3 &npc, proto::PlayerId owner, const Places &places,
                      const Links &links, proto::PlayerId host, const Holders &holders) {
  const auto mine = places.find(owner);
  if (holders.contains(owner) || mine == places.end() || !mine->second.known ||
      Distance(npc, mine->second.position) <= kKeep)
    return proto::kNoPlayer;
  proto::PlayerId best = proto::kNoPlayer;
  for (const auto &[player, place] : places) {
    if (player == owner || !place.known || !links.Linked(owner, player))
      continue;
    if (Distance(npc, place.position) >= kTake)
      continue;
    if (best == proto::kNoPlayer || Outranks(player, best, host, holders))
      best = player;
  }
  return best;
}

proto::PlayerId Heir(const proto::Vec3 &npc, const Places &places,
                     const std::set<proto::PlayerId> &candidates, proto::PlayerId host,
                     const Holders &holders) {
  proto::PlayerId best = proto::kNoPlayer;
  for (const auto player : candidates) {
    const auto place = places.find(player);
    if (place == places.end() || !place->second.known ||
        Distance(npc, place->second.position) > kKeep)
      continue;
    if (best == proto::kNoPlayer || Outranks(player, best, host, holders))
      best = player;
  }
  return best;
}

std::set<proto::PlayerId> GroupOf(proto::PlayerId player, const std::set<proto::PlayerId> &players,
                                  const Links &links) {
  std::set<proto::PlayerId> group{player};
  std::vector<proto::PlayerId> open{player};
  while (!open.empty()) {
    const auto from = open.back();
    open.pop_back();
    for (const auto other : players)
      if (!group.contains(other) && links.Linked(from, other)) {
        group.insert(other);
        open.push_back(other);
      }
  }
  return group;
}

proto::PlayerId Leader(const std::set<proto::PlayerId> &group, proto::PlayerId host,
                       const Holders &holders) {
  proto::PlayerId best = proto::kNoPlayer;
  for (const auto player : group)
    if (best == proto::kNoPlayer || Outranks(player, best, host, holders))
      best = player;
  return best;
}

proto::PlayerId AreaOwner(proto::PlayerId player, const std::set<proto::PlayerId> &players,
                          const Places &places, const Links &links, proto::PlayerId host,
                          const Holders &holders, proto::PlayerId current) {
  const auto mine = places.find(player);
  const bool placed = mine != places.end() && mine->second.known;
  proto::PlayerId best = player;
  for (const auto other : players) {
    if (other == player || !links.Linked(player, other) || !Outranks(other, player, host, holders))
      continue;
    const auto theirs = places.find(other);
    const bool near = !placed || theirs == places.end() || !theirs->second.known ||
                      Distance(mine->second.position, theirs->second.position) <
                          (other == current ? kRadius : kQuietIn);
    if (near && (best == player || Outranks(other, best, host, holders)))
      best = other;
  }
  return best;
}

std::vector<Cover> Covers(proto::PlayerId player, const std::set<proto::PlayerId> &players,
                          const Places &places, const Links &links, proto::PlayerId host,
                          const Holders &holders) {
  std::vector<Cover> covers;
  for (const auto other : players) {
    if (other == player || !links.Linked(player, other) || !Outranks(other, player, host, holders))
      continue;
    const auto place = places.find(other);
    if (place == places.end() || !place->second.known)
      continue;
    covers.push_back({.by = other, .center = place->second.position});
  }
  std::ranges::sort(
      covers, [&](const Cover &a, const Cover &b) { return Outranks(a.by, b.by, host, holders); });
  return covers;
}

proto::PlayerId CoveredBy(const proto::Vec3 &point, const std::vector<Cover> &covers,
                          float radius) {
  for (const auto &cover : covers)
    if (Distance(point, cover.center) < radius)
      return cover.by;
  return proto::kNoPlayer;
}

float CrowdShare(const proto::Vec3 &self, const std::vector<Cover> &covers, bool quiet) {
  float share = 1.f;
  for (const auto &cover : covers) {
    const float d = Distance(self, cover.center);
    if (d < (quiet ? kRadius : kQuietIn))
      return 0.f;
    if (d >= 2.f * kRadius)
      continue;
    // Two discs of one radius, their centres d apart: the lens they share,
    // as a part of one disc.
    const float half = d / (2.f * kRadius);
    const float lens = 2.f * std::acos(half) - 2.f * half * std::sqrt(1.f - half * half);
    share = std::min(share, 1.f - lens / std::numbers::pi_v<float>);
  }
  return std::round(share * 10.f) / 10.f;
}

std::vector<NullStep> NullAreas::Update(const std::vector<Cover> &covers, double now) {
  std::vector<NullStep> steps;
  for (auto it = m_areas.begin(); it != m_areas.end();) {
    const auto by = it->first;
    if (std::ranges::any_of(covers, [&](const Cover &cover) { return cover.by == by; })) {
      ++it;
      continue;
    }
    steps.push_back({.by = by, .place = false, .center = it->second.center});
    it = m_areas.erase(it);
  }
  for (const auto &cover : covers) {
    const auto known = m_areas.find(cover.by);
    if (known != m_areas.end() && now - known->second.at < kNullRenew &&
        Distance(known->second.center, cover.center) < kNullStep)
      continue;
    m_areas[cover.by] = {.center = cover.center, .at = now};
    steps.push_back({.by = cover.by, .place = true, .center = cover.center});
  }
  return steps;
}

std::vector<NullStep> NullAreas::Clear() {
  std::vector<NullStep> steps;
  for (const auto &[by, area] : m_areas)
    steps.push_back({.by = by, .place = false, .center = area.center});
  m_areas.clear();
  return steps;
}
} // namespace tandem::regions
