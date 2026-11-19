#include "tandem/scene.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace tandem::scene {
namespace {
constexpr float kPi = std::numbers::pi_v<float>;

float Flat(const proto::Vec3 &a, const proto::Vec3 &b) { return std::hypot(a.x - b.x, a.y - b.y); }

std::uint64_t Micros(float seconds) {
  return seconds <= 0.f ? 0 : static_cast<std::uint64_t>(static_cast<double>(seconds) * 1e6);
}

// When a line is over (a line of unknown length: a while after it began).
std::uint64_t EndOf(const proto::SceneLine &line) {
  return line.startUs + Micros(line.duration > 0.f ? line.duration : kUnknownLineSeconds);
}

// The yaw that faces `to` from `from` (forward is +Y turned by the yaw).
float YawTo(const proto::Vec3 &from, const proto::Vec3 &to) {
  return std::atan2(-(to.x - from.x), to.y - from.y);
}

bool IsScenes(const proto::SceneLine &line) {
  return (line.flags & (proto::kVoiceOverhead | proto::kVoiceHolocall)) == 0;
}
} // namespace

// ---- host ----

bool Stage::Speaking(std::uint64_t hostNowUs) const {
  return std::ranges::any_of(m_lines, [&](const auto &entry) {
    const auto &line = entry.second;
    if (!IsScenes(line))
      return false;
    const auto end = EndOf(line);
    return hostNowUs + Micros(kCutSeconds) < end;
  });
}

std::vector<proto::Message> Stage::Update(const HostSample &sample, std::uint64_t hostNowUs) {
  std::vector<proto::Message> out;
  m_private = sample.scene != 0 && sample.isPrivate;
  if (sample.scene == 0 || sample.isPrivate) {
    if (On()) {
      // Over; before its end if someone was still speaking.
      out.push_back(proto::SceneState{.play = m_play,
                                      .startUs = hostNowUs,
                                      .flags = Speaking(hostNowUs) ? proto::kSceneInterrupted
                                                                   : std::uint8_t{0}});
      m_state = {};
      m_cast = {};
      m_lines.clear();
    }
    return out;
  }

  const bool begin = !On() || m_state.scene != sample.scene;
  auto next = m_state;
  if (begin) {
    next = {};
    next.scene = sample.scene;
    next.play = ++m_play;
    next.startUs = hostNowUs;
    m_lines.clear();
  }
  next.tier = sample.tier;
  next.rate = sample.held ? 0.f : std::clamp(sample.rate, 0.f, proto::kMaxSceneRate);
  next.flags &= proto::kSceneStaged;
  if (sample.held)
    next.flags |= proto::kSceneHeld;
  else if (next.rate > 1.05f)
    next.flags |= proto::kSceneSkipping;

  // The staging: the middle of the actors that show and the host's V.
  proto::Vec3 middle = sample.host;
  int bodies = 1;
  for (const auto &actor : sample.actors)
    if (!(actor.flags & proto::kActorHidden)) {
      middle.x += actor.position.x;
      middle.y += actor.position.y;
      middle.z += actor.position.z;
      ++bodies;
    }
  if (bodies > 1) {
    middle.x /= static_cast<float>(bodies);
    middle.y /= static_cast<float>(bodies);
    middle.z /= static_cast<float>(bodies);
    float radius = Flat(middle, sample.host);
    for (const auto &actor : sample.actors)
      if (!(actor.flags & proto::kActorHidden))
        radius = std::max(radius, Flat(middle, actor.position));
    const bool staged = (next.flags & proto::kSceneStaged) != 0;
    const bool moved = Flat(middle, next.anchor) > kStageMoveMetres ||
                       std::abs(middle.z - next.anchor.z) > kStageMoveMetres ||
                       std::abs(radius - next.radius) > kStageMoveMetres;
    if (!staged || (moved && hostNowUs - m_stagedUs >= kStageEveryUs)) {
      next.flags |= proto::kSceneStaged;
      next.anchor = middle;
      next.radius = radius;
      next.yaw = YawTo(sample.host, middle);
      m_stagedUs = hostNowUs;
    }
  }
  if (next != m_state) {
    m_state = next;
    out.push_back(next);
  }

  proto::SceneCast cast{.play = m_play};
  for (const auto &actor : sample.actors)
    if (actor.npc != 0)
      cast.actors.push_back({actor.npc, actor.flags});
  std::ranges::sort(cast.actors, {}, &proto::SceneActor::npc);
  const auto [first, last] = std::ranges::unique(cast.actors, {}, &proto::SceneActor::npc);
  cast.actors.erase(first, last);
  if (cast.actors.size() > proto::kMaxSceneActors)
    cast.actors.resize(proto::kMaxSceneActors);
  if (begin || cast != m_cast) {
    m_cast = cast;
    out.push_back(std::move(cast));
  }
  return out;
}

std::optional<proto::SceneLine> Stage::Line(std::uint64_t line, proto::NpcId speaker,
                                            proto::PlayerId speakerPlayer, float duration,
                                            std::uint8_t flags, std::uint64_t hostNowUs) {
  // A private scene and whatever the host hears in it are the host's own.
  if (m_private || line == 0)
    return std::nullopt;
  flags &= static_cast<std::uint8_t>(~proto::kVoiceStop);
  proto::SceneLine spoken{.line = line,
                          .speaker = speaker,
                          .speakerPlayer = speakerPlayer,
                          .startUs = hostNowUs,
                          .duration = std::max(duration, 0.f),
                          .flags = flags};
  if (On() && IsScenes(spoken))
    spoken.play = m_play;
  // Old lines go (a hide that never came).
  std::erase_if(m_lines, [&](const auto &entry) { return EndOf(entry.second) <= hostNowUs; });
  m_lines[line] = spoken;
  return spoken;
}

std::optional<proto::SceneLine> Stage::Hide(std::uint64_t line, std::uint64_t hostNowUs) {
  const auto it = m_lines.find(line);
  if (it == m_lines.end())
    return std::nullopt;
  auto spoken = it->second;
  m_lines.erase(it);
  if (spoken.duration > 0.f && hostNowUs + Micros(kCutSeconds) >= EndOf(spoken))
    return std::nullopt; // it ended
  spoken.flags |= proto::kVoiceStop;
  return spoken;
}

// ---- guest ----

bool Stage::Casts(proto::NpcId npc) const {
  return On() &&
         std::ranges::any_of(m_cast.actors, [&](const auto &actor) { return actor.npc == npc; });
}

bool Replay::Acts(proto::NpcId npc) const {
  return std::ranges::any_of(m_cast, [&](const auto &actor) { return actor.npc == npc; });
}

float Replay::Seconds(std::uint64_t hostNowUs) const {
  if (!On() || hostNowUs <= m_state.startUs)
    return 0.f;
  return static_cast<float>(static_cast<double>(hostNowUs - m_state.startUs) / 1e6);
}

void Replay::StopVoices(std::vector<Cue> &out, bool sceneOnly) {
  for (auto it = m_lines.begin(); it != m_lines.end();) {
    if (sceneOnly && !IsScenes(it->second.line)) {
      ++it;
      continue;
    }
    out.push_back({.kind = Cue::Kind::VoiceStop, .line = it->second.line});
    it = m_lines.erase(it);
  }
}

void Replay::Leave(std::vector<Cue> &out, std::uint8_t flags) {
  if (!On())
    return;
  StopVoices(out, true);
  for (const auto &actor : m_cast)
    out.push_back({.kind = Cue::Kind::ActorGone, .actor = actor});
  m_cast.clear();
  auto ended = m_state;
  ended.flags = flags;
  m_state = {};
  out.push_back({.kind = Cue::Kind::End, .state = ended});
}

std::vector<Cue> Replay::Reset() {
  std::vector<Cue> out;
  Leave(out, proto::kSceneInterrupted);
  StopVoices(out, false);
  m_stale = 0;
  return out;
}

void Replay::Tick(std::uint64_t hostNowUs) {
  std::erase_if(m_lines, [&](const auto &entry) { return entry.second.endUs <= hostNowUs; });
}

std::vector<Cue> Replay::Note(const proto::Message &message, std::uint64_t hostNowUs) {
  std::vector<Cue> out;
  Tick(hostNowUs);
  if (const auto *state = std::get_if<proto::SceneState>(&message)) {
    if (state->scene == 0) {
      // The end of another play than ours is old news.
      if (On() && (state->play == 0 || state->play == m_state.play))
        Leave(out, state->flags & proto::kSceneInterrupted);
      return out;
    }
    if (On() && state->play != m_state.play)
      Leave(out, proto::kSceneInterrupted); // its end never came: another scene plays
    if (!On()) {
      m_state = *state;
      out.push_back({.kind = Cue::Kind::Begin, .state = *state, .seconds = Seconds(hostNowUs)});
    } else if (*state != m_state) {
      const bool skips = (state->flags & proto::kSceneSkipping) != 0 &&
                         (m_state.flags & proto::kSceneSkipping) == 0;
      m_state = *state;
      // Fast-forward: the host hears no line out, nor do we.
      if (skips)
        StopVoices(out, true);
      out.push_back({.kind = Cue::Kind::Change, .state = *state, .seconds = Seconds(hostNowUs)});
    }
    return out;
  }
  if (const auto *cast = std::get_if<proto::SceneCast>(&message)) {
    if (!On() || cast->play != m_state.play)
      return out;
    for (const auto &old : m_cast)
      if (std::ranges::none_of(cast->actors, [&](const auto &a) { return a.npc == old.npc; }))
        out.push_back({.kind = Cue::Kind::ActorGone, .actor = old});
    for (const auto &actor : cast->actors)
      if (std::ranges::find(m_cast, actor) == m_cast.end())
        out.push_back({.kind = Cue::Kind::Actor, .actor = actor});
    m_cast = cast->actors;
    return out;
  }
  if (const auto *line = std::get_if<proto::SceneLine>(&message)) {
    if (line->flags & proto::kVoiceStop) {
      if (const auto it = m_lines.find(line->line); it != m_lines.end()) {
        out.push_back({.kind = Cue::Kind::VoiceStop, .line = it->second.line});
        m_lines.erase(it);
      }
      return out;
    }
    if (line->line == 0)
      return out;
    if (const auto it = m_lines.find(line->line); it != m_lines.end()) {
      if (it->second.line.startUs == line->startUs)
        return out; // a repeat (a resync): it plays already
      // Said again (a bark, a rewound section): from its new start.
      out.push_back({.kind = Cue::Kind::VoiceStop, .line = it->second.line});
      m_lines.erase(it);
    }
    // A scene's line belongs to the scene on now.
    if (line->play != 0 && (!On() || line->play != m_state.play))
      return out;
    if (line->play != 0 && (m_state.flags & proto::kSceneSkipping))
      return out;
    float seconds = hostNowUs > line->startUs
                        ? static_cast<float>(static_cast<double>(hostNowUs - line->startUs) / 1e6)
                        : 0.f;
    const float length = line->duration > 0.f ? line->duration : kUnknownLineSeconds;
    if (seconds > length - kLateSeconds) {
      ++m_stale;
      return out;
    }
    if (seconds < kSeekSeconds)
      seconds = 0.f;
    // One voice per speaker: its earlier line stops.
    for (auto it = m_lines.begin(); it != m_lines.end();) {
      const auto &other = it->second.line;
      const bool same = (line->speaker != 0 && other.speaker == line->speaker) ||
                        (line->speaker == 0 && line->speakerPlayer != proto::kNoPlayer &&
                         other.speaker == 0 && other.speakerPlayer == line->speakerPlayer);
      if (same) {
        out.push_back({.kind = Cue::Kind::VoiceStop, .line = other});
        it = m_lines.erase(it);
      } else
        ++it;
    }
    m_lines[line->line] = {*line, EndOf(*line)};
    out.push_back({.kind = Cue::Kind::Voice, .line = *line, .seconds = seconds});
    return out;
  }
  return out;
}

// ---- where a guest's own V stands ----

Spot WatchSpot(const proto::SceneState &state, std::span<const proto::Vec3> bodies,
               const proto::Vec3 &host, const proto::Vec3 &own) {
  Spot best;
  if (state.scene == 0 || !(state.flags & proto::kSceneStaged))
    return best;
  const auto &middle = state.anchor;
  const float ring = std::clamp(state.radius + kSpotBeyondActors, kSpotMinRing, kSpotMaxRing);
  // The bearing of the host from the middle: watchers stand on that side.
  const float towardHost = Flat(host, middle) > 0.05f
                               ? std::atan2(host.y - middle.y, host.x - middle.x)
                               : std::atan2(own.y - middle.y, own.x - middle.x);
  constexpr int kSteps = 48;
  float bestScore = 0.f;
  for (int pass = 0; pass < 2 && !best.found; ++pass)
    for (int step = 0; step < kSteps; ++step) {
      const float offset = static_cast<float>(step) * 2.f * kPi / kSteps;
      // First pass: beside the host (25° to 80° off its bearing); second:
      // anywhere but in its line of sight (less than 25° off).
      const float off = offset > kPi ? 2.f * kPi - offset : offset;
      const float degrees = off * 180.f / kPi;
      if (degrees < 25.f || (pass == 0 && degrees > 80.f))
        continue;
      const float angle = towardHost + offset;
      const proto::Vec3 at{middle.x + ring * std::cos(angle), middle.y + ring * std::sin(angle),
                           host.z};
      bool clear = Flat(at, host) >= kSpotClearMetres;
      for (const auto &body : bodies)
        clear = clear && Flat(at, body) >= kSpotClearMetres;
      if (!clear)
        continue;
      const float score = Flat(at, own);
      if (!best.found || score < bestScore) {
        best = {true, at, YawTo(at, middle)};
        bestScore = score;
      }
    }
  return best;
}

proto::Vec3 KeepOut(const proto::Vec3 &own, std::span<const proto::Vec3> bodies, float radius,
                    float maxStep) {
  proto::Vec3 push{};
  for (const auto &body : bodies) {
    // Another floor is not in the way.
    if (std::abs(body.z - own.z) > 1.5f)
      continue;
    const float distance = Flat(own, body);
    if (distance >= radius)
      continue;
    float dx = own.x - body.x, dy = own.y - body.y;
    if (distance < 0.01f) {
      dx = 1.f; // right inside it: any way out
      dy = 0.f;
    } else {
      dx /= distance;
      dy /= distance;
    }
    push.x += dx * (radius - distance);
    push.y += dy * (radius - distance);
  }
  const float length = std::hypot(push.x, push.y);
  if (length > maxStep && length > 0.f) {
    push.x *= maxStep / length;
    push.y *= maxStep / length;
  }
  return push;
}
} // namespace tandem::scene
