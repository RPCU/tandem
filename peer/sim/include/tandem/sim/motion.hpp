#pragma once

// How the simulated V moves (docs/architecture/sim.md, "The simulated V"):
// stitched from real play when there is some (recordings/play/*.tplay, the
// game's own V: its strides, stops, turns, stairs and movement flags), else
// a plain walk at the game's speeds. A recording's on-foot stretches are cut
// into clips of a few seconds; each clip is laid down where the V stands,
// turned so that it heads for the goal, and the next one starts where it
// ended.

#include "tandem/play.hpp"
#include "tandem/sim/names.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tandem::sim {
enum class Gait : std::uint8_t { Stand, Walk, Run, Sprint };
const char *NameOf(Gait gait);

// Our V's body as a PlayerState carries it.
struct Body {
  proto::Vec3 position, velocity;
  float yaw{};
  std::uint8_t stance{}; // gamePSMLocomotionStates: 0 default, 1 crouch, 2 sprint
  std::uint16_t flags{}; // tandem/movement.hpp
};

class Motion {
public:
  // Every .tplay in `dir` (none: the synthetic walk). Returns the clips cut.
  std::size_t Load(const std::filesystem::path &dir, std::string &log);
  // A recording's on-foot stretches (tests: a recording made on the spot).
  std::size_t Add(const play::Recording &recording);
  std::size_t Clips() const { return m_clips.size(); }
  bool Recorded() const { return !m_clips.empty(); }

  // `body` moves toward `goal` for `dt` seconds at `gait` (crouched: slower,
  // stance 1). Within `arrive` metres of the goal it stands.
  void Step(Body &body, const proto::Vec3 &goal, Gait gait, bool crouch, float dt, Random &random,
            float arrive = 1.f);
  // A jump to elsewhere (a teleport, a fast travel): any clip in play ends.
  void Teleport(Body &body, const proto::Vec3 &to);
  // Clips started so far, and how many of them came from recordings.
  std::uint64_t Stitched() const { return m_stitched; }

private:
  struct Sample {
    float t{};
    proto::Vec3 offset; // in the clip's frame: it heads along +Y from 0
    float yaw{};
    proto::Vec3 velocity;
    std::uint8_t stance{};
    std::uint16_t flags{};
  };
  struct Clip {
    Gait gait{};
    float duration{}, distance{};
    std::vector<Sample> samples;
  };
  void Cut(const std::vector<proto::PlayerState> &run);
  bool Start(const Body &body, const proto::Vec3 &goal, Gait gait, Random &random);
  void Synthetic(Body &body, const proto::Vec3 &goal, Gait gait, bool crouch, float dt,
                 float arrive);

  std::vector<Clip> m_clips;
  int m_clip{-1};
  float m_t{};
  proto::Vec3 m_origin;
  float m_turn{};
  std::uint64_t m_stitched{};
};
} // namespace tandem::sim
