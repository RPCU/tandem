#include "tandem/vehicle.hpp"
#include "tandem/pose.hpp"

#include <algorithm>

namespace tandem::sync {
void VehicleTrack::Push(const proto::VehicleState &state) {
  if (m_haveSeq && static_cast<std::int32_t>(state.seq - m_lastSeq) <= 0)
    return; // late or repeated
  m_lastSeq = state.seq;
  m_haveSeq = true;
  if (!m_samples.empty() && state.timeUs <= m_samples.back().timeUs)
    m_samples.clear(); // the driver's clock jumped: start over
  m_samples.push_back(state);
  while (m_samples.size() > kMaxSamples)
    m_samples.pop_front();
}

VehicleView VehicleTrack::At(std::uint64_t renderUs) const {
  VehicleView view;
  if (m_samples.empty())
    return view;
  view.valid = true;
  const auto &first = m_samples.front();
  if (renderUs <= first.timeUs) {
    view.position = first.position;
    view.orientation = first.orientation;
    view.velocity = first.velocity;
    return view;
  }
  for (std::size_t i = 1; i < m_samples.size(); ++i) {
    const auto &b = m_samples[i];
    if (renderUs > b.timeUs)
      continue;
    const auto &a = m_samples[i - 1];
    const float t =
        static_cast<float>(renderUs - a.timeUs) / static_cast<float>(b.timeUs - a.timeUs);
    const auto lerp = [t](const proto::Vec3 &x, const proto::Vec3 &y) {
      return proto::Vec3{x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t};
    };
    view.position = lerp(a.position, b.position);
    view.velocity = lerp(a.velocity, b.velocity);
    view.orientation = pose::Slerp(a.orientation, b.orientation, t);
    return view;
  }
  // Past the newest state: along its velocity for a while, then held.
  const auto &last = m_samples.back();
  const auto ahead = std::min<std::uint64_t>(renderUs - last.timeUs, kExtrapolateUs);
  const float dt = static_cast<float>(ahead) / 1e6f;
  view.position = {last.position.x + last.velocity.x * dt, last.position.y + last.velocity.y * dt,
                   last.position.z + last.velocity.z * dt};
  view.orientation = last.orientation;
  view.velocity = renderUs - last.timeUs > kExtrapolateUs ? proto::Vec3{} : last.velocity;
  view.extrapolated = true;
  return view;
}
} // namespace tandem::sync
