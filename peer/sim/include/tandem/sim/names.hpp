#pragma once

// Names the game's way, and small maths shared by the models.

#include "tandem/protocol.hpp"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <string_view>

namespace tandem::sim {
// TweakDBID of a record name: CRC32 of the name, its length in the top byte.
constexpr std::uint64_t TweakDBID(std::string_view name) {
  std::uint32_t crc = 0xFFFFFFFF;
  for (const char raw : name) {
    crc ^= static_cast<unsigned char>(raw);
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320 & (0u - (crc & 1)));
  }
  return static_cast<std::uint64_t>(~crc) | (static_cast<std::uint64_t>(name.size()) << 32);
}

// CName hash: FNV-1a 64.
constexpr std::uint64_t CName(std::string_view name) {
  std::uint64_t hash = 0xCBF29CE484222325;
  for (const char raw : name)
    hash = (hash ^ static_cast<unsigned char>(raw)) * 0x100000001B3;
  return hash;
}

// A seeded generator with its own distributions, so a seed gives the same
// run whatever the standard library (std::uniform_* may differ).
class Random {
public:
  explicit Random(std::uint64_t seed) : m_state(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) {
    Next();
    Next();
  }
  std::uint64_t Next() { // splitmix64
    std::uint64_t z = (m_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  // 0 <= x < 1.
  double Unit() { return static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0); }
  float Between(float low, float high) { return low + static_cast<float>(Unit()) * (high - low); }
  // 0 <= n < count (count > 0).
  std::size_t Index(std::size_t count) { return static_cast<std::size_t>(Next() % count); }
  bool Chance(double probability) { return Unit() < probability; }
  // Another generator, independent of what this one is asked later.
  Random Fork() { return Random(Next()); }

private:
  std::uint64_t m_state;
};

inline constexpr float kPi = std::numbers::pi_v<float>;

inline proto::Vec3 operator+(const proto::Vec3 &a, const proto::Vec3 &b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline proto::Vec3 operator-(const proto::Vec3 &a, const proto::Vec3 &b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline proto::Vec3 operator*(const proto::Vec3 &a, float s) { return {a.x * s, a.y * s, a.z * s}; }
// On the ground plane.
inline float Distance(const proto::Vec3 &a, const proto::Vec3 &b) {
  return std::hypot(a.x - b.x, a.y - b.y);
}
inline float Length(const proto::Vec3 &v) { return std::hypot(v.x, v.y); }
// Heading (radians, 0 = +Y, counter-clockwise: the game's yaw) from a to b.
inline float YawTo(const proto::Vec3 &a, const proto::Vec3 &b) {
  return std::atan2(-(b.x - a.x), b.y - a.y);
}
inline proto::Vec3 Forward(float yaw) { return {-std::sin(yaw), std::cos(yaw), 0.f}; }
inline proto::Quat QuatOfYaw(float yaw) {
  return {0.f, 0.f, std::sin(yaw / 2.f), std::cos(yaw / 2.f)};
}
inline float YawOf(const proto::Quat &q) {
  return std::atan2(2.f * (q.r * q.k + q.i * q.j), 1.f - 2.f * (q.j * q.j + q.k * q.k));
}
} // namespace tandem::sim
