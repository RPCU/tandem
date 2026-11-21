#include "tandem/prefabs.hpp"

#include <algorithm>

namespace tandem::prefabs {
std::vector<Step> Mirror::Update(bool grouped, const Query &local) {
  if (!grouped)
    return Release(false, local);
  std::vector<Step> steps = Release(true, local);
  for (const auto prefab : m_host) {
    const auto here = local(prefab);
    if (m_ours.contains(prefab)) {
      if (here.held) {
        m_ours.erase(prefab); // our own quest's now as well: it releases it
        continue;
      }
      if (!here.on) {
        ++m_reapplied; // our own quest's phase turned it off
        steps.push_back({prefab, true});
      }
      continue;
    }
    if (here.on || here.held)
      continue; // our own quest's: never ours to touch
    m_ours.insert(prefab);
    steps.push_back({prefab, true});
  }
  return steps;
}

std::vector<Step> Mirror::Lift(const Query &local) {
  auto steps = Release(false, local);
  m_host.clear();
  return steps;
}

std::vector<Step> Mirror::Release(bool keepWanted, const Query &local) {
  std::vector<Step> steps;
  for (auto it = m_ours.begin(); it != m_ours.end();) {
    const auto prefab = *it;
    if (keepWanted && std::ranges::binary_search(m_host, prefab)) {
      ++it;
      continue;
    }
    const auto here = local(prefab);
    // Our own quest asked for it meanwhile: left on, its to release.
    if (here.on && !here.held)
      steps.push_back({prefab, false});
    it = m_ours.erase(it);
  }
  return steps;
}
} // namespace tandem::prefabs

namespace tandem::gates {
namespace {
std::uint8_t OpenBit(Gate gate) {
  return gate == Gate::Watson ? proto::kGateWatsonOpen : proto::kGateDogtownOpen;
}
std::uint8_t InBit(Gate gate) {
  return gate == Gate::Watson ? proto::kGateInWatson : proto::kGateInDogtown;
}
std::uint8_t Bit(Gate gate) { return static_cast<std::uint8_t>(1u << static_cast<unsigned>(gate)); }
} // namespace

bool Open(std::uint8_t flags, Gate gate) { return (flags & OpenBit(gate)) != 0; }
bool Inside(std::uint8_t flags, Gate gate) { return (flags & InBit(gate)) != 0; }

std::vector<Gate> Behind(std::uint8_t host, std::uint8_t guest) {
  std::vector<Gate> out;
  for (const auto gate : kGates)
    if (Open(host, gate) && !Open(guest, gate))
      out.push_back(gate);
  return out;
}

bool Past(std::uint8_t host, std::uint8_t guest, Gate gate) {
  if (!Open(host, gate) || Open(guest, gate))
    return false;
  // Watson's lockdown keeps the guest in; Dogtown's gate keeps it out.
  const bool guestKept = gate == Gate::Watson ? Inside(guest, gate) : !Inside(guest, gate);
  const bool hostPast = gate == Gate::Watson ? !Inside(host, gate) : Inside(host, gate);
  return guestKept && hostPast;
}

std::optional<Gate> Met(std::uint8_t host, std::uint8_t guest, bool hostKnown, float distance) {
  if (!hostKnown || !(distance >= 0.f) || distance > kBarrierReach)
    return std::nullopt;
  for (const auto gate : kGates)
    if (Past(host, guest, gate))
      return gate;
  return std::nullopt;
}

std::optional<Gate> Watcher::Update(std::uint8_t host, std::uint8_t guest, bool hostKnown,
                                    float distance) {
  // Re-armed once the host is on our side again or our story opened it (not
  // by distance: walking off and back is the same crossing).
  for (const auto gate : kGates)
    if (!Past(host, guest, gate))
      m_told &= static_cast<std::uint8_t>(~Bit(gate));
  const auto met = Met(host, guest, hostKnown, distance);
  if (!met || (m_told & Bit(*met)))
    return std::nullopt;
  m_told |= Bit(*met);
  return met;
}
} // namespace tandem::gates
