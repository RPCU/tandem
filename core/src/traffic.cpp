#include "tandem/traffic.hpp"

#include <algorithm>
#include <cmath>

namespace tandem::traffic {
namespace {
constexpr float kUnit = 32767.f;

std::int16_t ToUnit(float value) {
  return static_cast<std::int16_t>(std::lround(std::clamp(value, -1.f, 1.f) * kUnit));
}

std::int16_t ToCentimetres(float metres) {
  if (!std::isfinite(metres))
    return 0;
  return static_cast<std::int16_t>(std::lround(std::clamp(metres * 100.f, -32767.f, 32767.f)));
}

float Distance(const proto::Vec3 &a, const proto::Vec3 &b) {
  const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
  return std::sqrt(x * x + y * y + z * z);
}
} // namespace

std::array<std::int16_t, 3> PackOrientation(const proto::Quat &orientation) {
  float i = orientation.i, j = orientation.j, k = orientation.k, r = orientation.r;
  const float length = std::sqrt(i * i + j * j + k * k + r * r);
  if (!std::isfinite(length) || length < 1e-6f)
    return {}; // no rotation
  const float scale = (r < 0.f ? -1.f : 1.f) / length;
  return {ToUnit(i * scale), ToUnit(j * scale), ToUnit(k * scale)};
}

proto::Quat UnpackOrientation(const std::array<std::int16_t, 3> &packed) {
  const float i = packed[0] / kUnit, j = packed[1] / kUnit, k = packed[2] / kUnit;
  const float rest = 1.f - (i * i + j * j + k * k);
  return {.i = i, .j = j, .k = k, .r = rest > 0.f ? std::sqrt(rest) : 0.f};
}

std::array<std::int16_t, 3> PackVelocity(const proto::Vec3 &velocity) {
  return {ToCentimetres(velocity.x), ToCentimetres(velocity.y), ToCentimetres(velocity.z)};
}

proto::Vec3 UnpackVelocity(const std::array<std::int16_t, 3> &packed) {
  return {packed[0] / 100.f, packed[1] / 100.f, packed[2] / 100.f};
}

proto::TrafficEntry Pack(const Car &car) {
  return {.id = car.id,
          .position = car.position,
          .orientation = PackOrientation(car.orientation),
          .velocity = PackVelocity(car.velocity)};
}

Car Unpack(const proto::TrafficEntry &entry) {
  return {.id = entry.id,
          .position = entry.position,
          .orientation = UnpackOrientation(entry.orientation),
          .velocity = UnpackVelocity(entry.velocity)};
}

float Nearest(const proto::Vec3 &point, std::span<const proto::Vec3> viewers) {
  float best = 1e9f;
  for (const auto &viewer : viewers)
    best = std::min(best, Distance(point, viewer));
  return best;
}

std::vector<proto::TrafficEntry>
Budget::Pick(std::span<const Car> cars, std::span<const proto::Vec3> viewers, std::size_t max) {
  ++m_round;
  struct Candidate {
    const Car *car{};
    float distance{};
    std::uint32_t waited{}; // rounds since it was last sent
    bool resting{};
  };
  std::vector<Candidate> moving, stopping;
  for (const auto &car : cars) {
    const float speed =
        std::sqrt(car.velocity.x * car.velocity.x + car.velocity.y * car.velocity.y +
                  car.velocity.z * car.velocity.z);
    const auto said = m_said.find(car.id);
    const bool known = said != m_said.end();
    // At rest: slow, and where we last said it was.
    const bool resting =
        speed < kRestSpeed && known && Distance(said->second.position, car.position) < 0.05f;
    const std::uint32_t waited = known ? m_round - said->second.round : m_round;
    if (resting && said->second.resting && waited < kRestEvery)
      continue; // said where it stopped: nothing new
    Candidate candidate{&car, Nearest(car.position, viewers), waited, resting};
    // A car that just stopped must be said once more (its last state had
    // a velocity: the others would run it on), ahead of the keepalives.
    (resting && said->second.resting ? stopping : moving).push_back(candidate);
  }
  std::ranges::sort(moving, {}, &Candidate::distance);

  std::vector<const Candidate *> chosen;
  const std::size_t near =
      moving.size() <= max ? moving.size() : max * kNearShare / (kNearShare + 1);
  for (std::size_t i = 0; i < near; ++i)
    chosen.push_back(&moving[i]);
  // What is left of the packet: the others, the longest waiting first.
  std::vector<const Candidate *> rest;
  for (std::size_t i = near; i < moving.size(); ++i)
    rest.push_back(&moving[i]);
  for (const auto &candidate : stopping)
    rest.push_back(&candidate);
  std::ranges::stable_sort(
      rest, [](const Candidate *a, const Candidate *b) { return a->waited > b->waited; });
  for (const auto *candidate : rest) {
    if (chosen.size() >= max)
      break;
    chosen.push_back(candidate);
  }

  std::vector<proto::TrafficEntry> out;
  out.reserve(chosen.size());
  for (const auto *candidate : chosen) {
    out.push_back(Pack(*candidate->car));
    m_said[candidate->car->id] = {candidate->car->position, m_round, candidate->resting};
  }
  // Cars no longer offered are forgotten (gone, or out of range).
  if (m_said.size() > cars.size() * 2 + 64)
    std::erase_if(m_said, [&](const auto &entry) {
      return std::ranges::none_of(cars, [&](const Car &car) { return car.id == entry.first; });
    });
  return out;
}

void Budget::Forget(proto::TrafficId id) { m_said.erase(id); }

void Budget::Clear() {
  m_said.clear();
  m_round = 0;
}
} // namespace tandem::traffic
