#include "tandem/props.hpp"

#include "tandem/consistency.hpp"
#include "tandem/traffic.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tandem::props {
namespace {
float Distance(const proto::Vec3 &a, const proto::Vec3 &b) {
  return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

float Nearest(const proto::Vec3 &at, std::span<const proto::Vec3> viewers) {
  float best = std::numeric_limits<float>::max();
  for (const auto &viewer : viewers)
    best = std::min(best, Distance(at, viewer));
  return best;
}

proto::Vec3 Lerp(const proto::Vec3 &a, const proto::Vec3 &b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

proto::Quat Nlerp(const proto::Quat &a, proto::Quat b, float t) {
  // The shorter way round.
  if (a.i * b.i + a.j * b.j + a.k * b.k + a.r * b.r < 0.f)
    b = {-b.i, -b.j, -b.k, -b.r};
  proto::Quat q{a.i + (b.i - a.i) * t, a.j + (b.j - a.j) * t, a.k + (b.k - a.k) * t,
                a.r + (b.r - a.r) * t};
  const float length = std::sqrt(q.i * q.i + q.j * q.j + q.k * q.k + q.r * q.r);
  if (length < 1e-6f)
    return a;
  return {q.i / length, q.j / length, q.k / length, q.r / length};
}

std::int64_t Centi(float value) { return std::llround(static_cast<double>(value) * 100.0); }
std::int64_t Milli(float value) { return std::llround(static_cast<double>(value) * 1000.0); }
} // namespace

proto::PropEntry Pack(const Prop &prop) {
  return {.entity = prop.entity,
          .part = prop.part,
          .position = prop.position,
          .orientation = traffic::PackOrientation(prop.orientation),
          .velocity = traffic::PackVelocity(prop.velocity)};
}

Prop Unpack(const proto::PropEntry &entry) {
  return {.entity = entry.entity,
          .part = entry.part,
          .position = entry.position,
          .orientation = traffic::UnpackOrientation(entry.orientation),
          .velocity = traffic::UnpackVelocity(entry.velocity)};
}

bool Speakers::Claim(const Key &key, proto::PlayerId player, std::uint64_t nowUs, bool ownsArea) {
  auto it = m_speakers.find(key);
  if (it != m_speakers.end() && nowUs - it->second.lastUs <= kHoldUs && it->second.player != player)
    return false;
  // A game that speaks for a prop already finishes it (its rest), even
  // once its area went to another game; a new one needs its area.
  const bool speaking =
      it != m_speakers.end() && it->second.player == player && nowUs - it->second.lastUs <= kHoldUs;
  if (!speaking && !ownsArea)
    return false;
  m_speakers[key] = {player, nowUs};
  return true;
}

std::optional<proto::PlayerId> Speakers::Of(const Key &key, std::uint64_t nowUs) const {
  const auto it = m_speakers.find(key);
  if (it == m_speakers.end() || nowUs - it->second.lastUs > kHoldUs)
    return std::nullopt;
  return it->second.player;
}

void Speakers::Heard(const Key &key, proto::PlayerId player, std::uint64_t nowUs) {
  m_speakers[key] = {player, nowUs};
}

void Speakers::Forget(proto::PlayerId player) {
  std::erase_if(m_speakers, [&](const auto &entry) { return entry.second.player == player; });
}

void Speakers::Expire(std::uint64_t nowUs) {
  std::erase_if(m_speakers,
                [&](const auto &entry) { return nowUs - entry.second.lastUs > kHoldUs; });
}

bool Record::Note(const proto::PropRest &rest) {
  const Key key{rest.entity, rest.part};
  auto it = m_rests.find(key);
  if (it == m_rests.end()) {
    if (Full()) {
      ++m_refused;
      return false;
    }
    m_rests.emplace(key, rest);
    m_order.push_back(key);
    return true;
  }
  // The same place and pose (whoever said it): nothing changes.
  const auto &known = it->second;
  if (known.position == rest.position && known.orientation == rest.orientation)
    return false;
  it->second = rest;
  return true;
}

const proto::PropRest *Record::Find(const Key &key) const {
  const auto it = m_rests.find(key);
  return it == m_rests.end() ? nullptr : &it->second;
}

std::uint32_t Record::Digest() const {
  // By key (the map's order): the same however the two heard it. Positions
  // to the centimetre, the pose to the thousandth: the wire carries floats
  // exactly, so a guest's record is the host's to the bit, and this leaves
  // room for nothing but a real difference.
  consistency::Hasher hash;
  for (const auto &[key, rest] : m_rests) {
    hash.Add(key.first).Add(std::uint64_t{key.second});
    for (const float v : {rest.position.x, rest.position.y, rest.position.z})
      hash.Add(static_cast<std::uint64_t>(Centi(v)));
    for (const float v :
         {rest.orientation.i, rest.orientation.j, rest.orientation.k, rest.orientation.r})
      hash.Add(static_cast<std::uint64_t>(Milli(v)));
  }
  return hash.Value();
}

std::vector<proto::PropRest> Record::InOrder() const {
  std::vector<proto::PropRest> out;
  out.reserve(m_order.size());
  for (const auto &key : m_order)
    out.push_back(m_rests.at(key));
  return out;
}

std::vector<proto::PropRest> Record::Around(const proto::Vec3 &at, float radius,
                                            std::size_t most) const {
  std::vector<proto::PropRest> out;
  for (const auto &key : m_order) {
    const auto &rest = m_rests.at(key);
    if (Distance(rest.position, at) <= radius) {
      out.push_back(rest);
      if (out.size() >= most)
        break;
    }
  }
  return out;
}

void Record::Clear() {
  m_rests.clear();
  m_order.clear();
  m_refused = 0;
}

bool Track::Add(std::uint64_t timeUs, const Prop &prop) {
  if (!m_samples.empty() && timeUs <= m_samples.back().first)
    return false;
  m_samples.emplace_back(timeUs, prop);
  // Two seconds of states are more than any draw needs.
  while (m_samples.size() > 2 && m_samples.front().first + 2'000'000 < timeUs)
    m_samples.pop_front();
  return true;
}

std::optional<Prop> Track::At(std::uint64_t timeUs) const {
  if (m_samples.empty())
    return std::nullopt;
  if (timeUs <= m_samples.front().first)
    return m_samples.front().second;
  for (std::size_t i = 1; i < m_samples.size(); ++i) {
    const auto &[t1, b] = m_samples[i];
    if (timeUs > t1)
      continue;
    const auto &[t0, a] = m_samples[i - 1];
    const float t = static_cast<float>(timeUs - t0) / static_cast<float>(t1 - t0);
    Prop out = b;
    out.position = Lerp(a.position, b.position, t);
    out.orientation = Nlerp(a.orientation, b.orientation, t);
    out.velocity = Lerp(a.velocity, b.velocity, t);
    return out;
  }
  // Past the last state: carried on along its velocity a little, then held.
  const auto &[last, prop] = m_samples.back();
  const float ahead =
      static_cast<float>(std::min<std::uint64_t>(timeUs - last, kExtrapolateUs)) / 1e6f;
  Prop out = prop;
  out.position = {prop.position.x + prop.velocity.x * ahead,
                  prop.position.y + prop.velocity.y * ahead,
                  prop.position.z + prop.velocity.z * ahead};
  return out;
}

std::vector<proto::PropEntry> Budget::Pick(std::span<const Prop> moving,
                                           std::span<const proto::Vec3> viewers) {
  // Nearest first; each round a prop is left out it counts as 20 m nearer.
  std::vector<std::pair<float, const Prop *>> order;
  order.reserve(moving.size());
  for (const auto &prop : moving) {
    const auto waited =
        m_waited.contains({prop.entity, prop.part}) ? m_waited.at({prop.entity, prop.part}) : 0u;
    order.emplace_back(Nearest(prop.position, viewers) - 20.f * static_cast<float>(waited), &prop);
  }
  std::ranges::stable_sort(order, {}, &std::pair<float, const Prop *>::first);
  std::map<Key, std::uint32_t> waited;
  std::vector<proto::PropEntry> out;
  for (const auto &[score, prop] : order) {
    const Key key{prop->entity, prop->part};
    if (out.size() < proto::kMaxPropsPerState)
      out.push_back(Pack(*prop));
    else
      waited[key] = (m_waited.contains(key) ? m_waited.at(key) : 0u) + 1;
  }
  m_waited = std::move(waited);
  return out;
}

bool Near(const proto::Vec3 &at, std::span<const proto::Vec3> viewers) {
  return Nearest(at, viewers) <= kShareRange;
}
} // namespace tandem::props
