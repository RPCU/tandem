#include "tandem/sim/motion.hpp"

#include "tandem/movement.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace tandem::sim {
namespace {
namespace p = proto;

constexpr float kClipSeconds = 3.f;
constexpr float kGapSeconds = 0.5f; // a longer silence ends a stretch
constexpr float kJumpMetres = 4.f;  // so does a teleport

// Turned by `angle` (counter-clockwise, the game's yaw) on the ground plane.
p::Vec3 Turned(const p::Vec3 &v, float angle) {
  const float c = std::cos(angle), s = std::sin(angle);
  return {v.x * c - v.y * s, v.x * s + v.y * c, v.z};
}

float Wrapped(float angle) { return std::remainder(angle, 2.f * kPi); }

float SpeedOf(Gait gait) {
  switch (gait) {
  case Gait::Walk:
    return 1.5f;
  case Gait::Run:
    return 4.f;
  case Gait::Sprint:
    return 6.5f;
  default:
    return 0.f;
  }
}

Gait GaitOf(float speed) {
  if (speed < 0.4f)
    return Gait::Stand;
  if (speed < 2.4f)
    return Gait::Walk;
  if (speed < 5.2f)
    return Gait::Run;
  return Gait::Sprint;
}
} // namespace

const char *NameOf(Gait gait) {
  constexpr const char *names[] = {"stand", "walk", "run", "sprint"};
  return names[static_cast<std::size_t>(gait)];
}

std::size_t Motion::Load(const std::filesystem::path &dir, std::string &log) {
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) {
    log = std::format("motion: no {}: the synthetic walk", dir.string());
    return 0;
  }
  std::vector<std::filesystem::path> files;
  for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
    if (entry.path().extension() == ".tplay")
      files.push_back(entry.path());
  std::ranges::sort(files);
  std::size_t clips = 0;
  for (const auto &file : files) {
    std::string error;
    if (auto recording = play::Load(file, error);
        recording && recording->header.kind == play::Kind::Play)
      clips += Add(*recording);
  }
  log = clips ? std::format("motion: {} clips stitched from {} recording(s) in {}", clips,
                            files.size(), dir.string())
              : std::format("motion: no on-foot play in {}: the synthetic walk", dir.string());
  return clips;
}

std::size_t Motion::Add(const play::Recording &recording) {
  const auto before = m_clips.size();
  std::vector<p::PlayerState> run;
  std::uint64_t lastUs = 0;
  bool seated = false;
  const auto flush = [&] {
    if (run.size() > 1)
      Cut(run);
    run.clear();
  };
  for (const auto &entry : recording.entries) {
    if (const auto *seat = std::get_if<p::Seat>(&entry.message)) {
      seated = seat->vehicle != 0;
      flush();
      continue;
    }
    const auto *state = std::get_if<p::PlayerState>(&entry.message);
    if (!state || seated)
      continue;
    auto copy = *state;
    copy.timeUs = entry.atUs; // the recording's own clock
    const bool gap =
        !run.empty() && (static_cast<float>(entry.atUs - lastUs) / 1e6f > kGapSeconds ||
                         Distance(run.back().position, copy.position) > kJumpMetres);
    // A V in the water, on a ladder or in a workspot is not walking.
    const auto placement = movement::PlacementOf(copy.flags);
    if (gap || placement == movement::Placement::Swim || placement == movement::Placement::Ladder ||
        placement == movement::Placement::Platform || placement == movement::Placement::Down)
      flush();
    if (placement == movement::Placement::Ground || placement == movement::Placement::Air ||
        placement == movement::Placement::Climb)
      run.push_back(copy);
    lastUs = entry.atUs;
  }
  flush();
  return m_clips.size() - before;
}

void Motion::Cut(const std::vector<p::PlayerState> &run) {
  std::size_t begin = 0;
  while (begin + 1 < run.size()) {
    std::size_t end = begin + 1;
    while (end < run.size() &&
           static_cast<float>(run[end].timeUs - run[begin].timeUs) / 1e6f < kClipSeconds)
      ++end;
    if (end == run.size())
      --end;
    const auto &first = run[begin], &last = run[end];
    const float duration = static_cast<float>(last.timeUs - first.timeUs) / 1e6f;
    if (duration < 1.f)
      break;
    Clip clip;
    clip.duration = duration;
    clip.distance = Distance(first.position, last.position);
    clip.gait = GaitOf(clip.distance / duration);
    // Heading along its own travel (standing: along the V's own facing).
    const float heading =
        clip.gait == Gait::Stand ? YawOf(first.orientation) : YawTo(first.position, last.position);
    for (std::size_t i = begin; i <= end; ++i) {
      const auto &state = run[i];
      clip.samples.push_back({.t = static_cast<float>(state.timeUs - first.timeUs) / 1e6f,
                              .offset = Turned(state.position - first.position, -heading),
                              .yaw = Wrapped(YawOf(state.orientation) - heading),
                              .velocity = Turned(state.velocity, -heading),
                              .stance = state.stance,
                              .flags = state.flags});
    }
    m_clips.push_back(std::move(clip));
    begin = end;
  }
}

bool Motion::Start(const Body &body, const p::Vec3 &goal, Gait gait, Random &random) {
  const float distance = Distance(body.position, goal);
  // A clip of the gait asked for that does not overshoot by much; else any
  // of the gait; standing clips for standing.
  std::vector<int> fit, any;
  for (int i = 0; i < static_cast<int>(m_clips.size()); ++i) {
    const auto &clip = m_clips[i];
    if (clip.gait != gait)
      continue;
    any.push_back(i);
    if (gait == Gait::Stand || clip.distance <= distance + 1.5f)
      fit.push_back(i);
  }
  const auto &from = fit.empty() ? any : fit;
  if (from.empty())
    return false;
  m_clip = from[random.Index(from.size())];
  m_t = 0.f;
  m_origin = body.position;
  m_turn = gait == Gait::Stand ? body.yaw - m_clips[m_clip].samples.front().yaw
                               : YawTo(body.position, goal);
  ++m_stitched;
  return true;
}

void Motion::Teleport(Body &body, const p::Vec3 &to) {
  body.position = to;
  body.velocity = {};
  m_clip = -1;
}

void Motion::Synthetic(Body &body, const p::Vec3 &goal, Gait gait, bool crouch, float dt,
                       float arrive) {
  using movement::Detailed;
  const float distance = Distance(body.position, goal);
  float speed = SpeedOf(gait) * (crouch ? 0.5f : 1.f);
  if (distance <= arrive)
    speed = 0.f;
  movement::Movement move{.known = true, .detailed = Detailed::Stand};
  if (speed > 0.f) {
    body.yaw = YawTo(body.position, goal);
    const float step = std::min(speed * dt, distance);
    body.velocity = Forward(body.yaw) * speed;
    body.position = body.position + Forward(body.yaw) * step;
    body.position.z += (goal.z - body.position.z) * std::min(dt, 1.f);
    if (gait == Gait::Sprint)
      move.detailed = Detailed::Sprint;
  } else {
    body.velocity = {};
  }
  if (crouch)
    move.detailed = speed > 0.f && gait == Gait::Sprint ? Detailed::CrouchSprint : Detailed::Crouch;
  body.flags = movement::Pack(move);
  body.stance = crouch ? 1 : (gait == Gait::Sprint && speed > 0.f ? 2 : 0);
}

void Motion::Step(Body &body, const p::Vec3 &goal, Gait gait, bool crouch, float dt, Random &random,
                  float arrive) {
  if (Distance(body.position, goal) <= arrive)
    gait = Gait::Stand;
  // Crouching has no recorded stride of its own: the synthetic one.
  if (m_clips.empty() || crouch) {
    m_clip = -1;
    Synthetic(body, goal, gait, crouch, dt, arrive);
    return;
  }
  if (m_clip >= 0 && (m_clips[m_clip].gait != gait || m_t >= m_clips[m_clip].duration))
    m_clip = -1;
  if (m_clip < 0 && !Start(body, goal, gait, random)) {
    Synthetic(body, goal, gait, crouch, dt, arrive);
    return;
  }
  m_t += dt;
  const auto &clip = m_clips[m_clip];
  const auto &samples = clip.samples;
  const auto after = std::ranges::upper_bound(samples, m_t, {}, &Sample::t);
  const auto &b = after == samples.end() ? samples.back() : *after;
  const auto &a = after == samples.begin() ? samples.front() : *(after - 1);
  const float span = b.t - a.t;
  const float w = span > 0.f ? std::clamp((m_t - a.t) / span, 0.f, 1.f) : 1.f;
  const auto offset = a.offset + (b.offset - a.offset) * w;
  body.position = m_origin + Turned(offset, m_turn);
  body.yaw = Wrapped(a.yaw + Wrapped(b.yaw - a.yaw) * w + m_turn);
  body.velocity = Turned(a.velocity + (b.velocity - a.velocity) * w, m_turn);
  body.stance = w < 0.5f ? a.stance : b.stance;
  body.flags = w < 0.5f ? a.flags : b.flags;
}
} // namespace tandem::sim
