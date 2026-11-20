#include "tandem/interact.hpp"

#include <algorithm>
#include <cmath>

namespace tandem::interact {
bool IsClassification(std::uint8_t verdict) {
  return verdict == proto::kInteractPerformed || verdict == proto::kInteractHostOnly ||
         verdict == proto::kInteractLocal;
}

const char *VerdictName(std::uint8_t verdict) {
  switch (verdict) {
  case proto::kInteractAsk:
    return "ask";
  case proto::kInteractPerformed:
    return "performed";
  case proto::kInteractHostOnly:
    return "host only";
  case proto::kInteractLocal:
    return "local";
  case proto::kInteractTaken:
    return "taken";
  case proto::kInteractTooFar:
    return "too far";
  case proto::kInteractMissing:
    return "missing";
  case proto::kInteractBusy:
    return "busy";
  default:
    return "?";
  }
}

std::optional<std::uint8_t> Known::Get(const Key &key) const {
  const auto it = m_verdicts.find(key);
  if (it == m_verdicts.end())
    return std::nullopt;
  return it->second;
}

void Known::Note(const Key &key, std::uint8_t verdict) {
  if (!IsClassification(verdict))
    return;
  const auto [it, added] = m_verdicts.insert_or_assign(key, verdict);
  (void)it;
  if (!added)
    return;
  m_order.push_back(key);
  while (m_order.size() > kMaxKnown) {
    m_verdicts.erase(m_order.front());
    m_order.pop_front();
  }
}

void Known::Clear() {
  m_verdicts.clear();
  m_order.clear();
}

Referee::Claim Referee::Ask(std::uint64_t target, proto::PlayerId asker, std::uint32_t seq,
                            std::uint64_t nowUs) {
  Expire(nowUs);
  if (const auto it = m_objects.find(target); it != m_objects.end()) {
    if (it->second.player != asker)
      return Claim::Taken;
    // Our own again (a second press): the newest question is the one.
    it->second = {asker, seq, false, nowUs};
    return Claim::Granted;
  }
  if (PendingCount() >= kMaxPending)
    return Claim::Full;
  m_objects[target] = {asker, seq, false, nowUs};
  return Claim::Granted;
}

bool Referee::Answered(std::uint64_t target, proto::PlayerId asker, std::uint32_t seq,
                       std::uint8_t verdict, std::uint64_t nowUs) {
  const auto it = m_objects.find(target);
  if (it == m_objects.end() || it->second.player != asker || it->second.seq != seq ||
      it->second.answered)
    return false;
  if (verdict == proto::kInteractPerformed) {
    it->second.answered = true;
    it->second.sinceUs = nowUs;
  } else {
    m_objects.erase(it);
  }
  return true;
}

std::size_t Referee::Forget(proto::PlayerId player) {
  return std::erase_if(m_objects, [&](const auto &entry) { return entry.second.player == player; });
}

std::size_t Referee::Expire(std::uint64_t nowUs) {
  return std::erase_if(m_objects, [&](const auto &entry) {
    const auto &use = entry.second;
    const auto limit = use.answered ? kHoldUs : kPendingUs;
    return nowUs >= use.sinceUs + limit;
  });
}

bool Referee::Pending(proto::PlayerId asker, std::uint32_t seq) const {
  return std::ranges::any_of(m_objects, [&](const auto &entry) {
    return entry.second.player == asker && entry.second.seq == seq && !entry.second.answered;
  });
}

std::size_t Referee::PendingCount() const {
  return static_cast<std::size_t>(
      std::ranges::count_if(m_objects, [](const auto &entry) { return !entry.second.answered; }));
}

namespace {
float Distance(const proto::Vec3 &a, const proto::Vec3 &b) {
  return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}
bool Finite(const proto::Vec3 &v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
} // namespace

Reach CheckReach(const lagcomp::History *track, const proto::Vec3 &claimed,
                 const proto::Vec3 &object, std::uint64_t askedUs, std::uint64_t arrivalUs,
                 std::uint32_t rttMs, float limit) {
  Reach reach;
  auto believed = claimed;
  if (track) {
    // The guest's V when it chose: its own states at the moment it says, or
    // half a round trip back; a slow link buys kMaxRewindUs at most.
    const auto earliest = lagcomp::SeenAtUs(arrivalUs, 0, lagcomp::kMaxRewindUs);
    const auto at = askedUs == 0 ? lagcomp::SeenAtUs(arrivalUs, rttMs / 2, 0)
                                 : std::clamp(askedUs, earliest, arrivalUs);
    if (const auto then = track->At(at)) {
      reach.claimOff = Finite(claimed) ? Distance(claimed, *then) : 1e9f;
      if (reach.claimOff > kClaimToleranceM)
        believed = *then;
    }
  }
  if (!Finite(believed) || !Finite(object)) {
    reach.distance = 1e9f;
    return reach;
  }
  reach.distance = Distance(believed, object);
  reach.ok = reach.distance <= limit + lagcomp::kToleranceM;
  return reach;
}
} // namespace tandem::interact
