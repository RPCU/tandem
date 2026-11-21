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

// The angle between two orientations (unit ones: a prop's, as its game says).
float PlaceTurn(const proto::Quat &a, const proto::Quat &b) {
  const float dot = std::abs(a.i * b.i + a.j * b.j + a.k * b.k + a.r * b.r);
  return 2.f * std::acos(std::min(dot, 1.f));
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
      m_props.clear();
      m_placed.clear();
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
    m_props.clear();
    m_placed.clear();
    m_propCount = 0;
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

  // What its actors hold: a new thing gets the play's next number, one that
  // changed hands keeps its own, one that is no longer seen goes.
  std::map<std::uint64_t, proto::SceneProp> held;
  for (const auto &prop : sample.props) {
    if (prop.key == 0 || prop.record == 0 || held.size() >= proto::kMaxSceneProps ||
        held.contains(prop.key))
      continue;
    proto::SceneProp said{.play = m_play,
                          .record = prop.record,
                          .holder = prop.holder,
                          .holderPlayer = prop.holder != 0 ? proto::kNoPlayer : prop.holderPlayer,
                          .flags = prop.leftHand ? proto::kPropLeftHand : std::uint8_t{0}};
    const auto was = m_props.find(prop.key);
    said.prop = was != m_props.end() && was->second.record == prop.record ? was->second.prop
                                                                          : ++m_propCount;
    held.emplace(prop.key, said);
  }
  for (const auto &[key, was] : m_props) {
    const auto now = held.find(key);
    if (now == held.end() || now->second.prop != was.prop) {
      auto gone = was;
      gone.flags |= proto::kPropGone;
      out.push_back(gone);
    }
  }
  for (const auto &[key, now] : held) {
    const auto was = m_props.find(key);
    if (was == m_props.end() || was->second != now)
      out.push_back(now);
  }
  m_props = std::move(held);

  // What nobody holds: said where it is, again once it moved, gone when no
  // longer seen (or held: then it is a SceneProp).
  std::map<std::uint64_t, Placed> placed;
  for (const auto &sampled : sample.placed) {
    if (sampled.key == 0 || sampled.entity == 0 || placed.size() >= proto::kMaxSceneProps ||
        placed.contains(sampled.key) || m_props.contains(sampled.key))
      continue;
    proto::ScenePlace said{.play = m_play,
                           .entity = sampled.entity,
                           .appearance = sampled.appearance,
                           .position = sampled.position,
                           .orientation = sampled.orientation};
    const auto was = m_placed.find(sampled.key);
    if (was != m_placed.end() && was->second.said.entity == sampled.entity) {
      const auto &before = was->second.said;
      const float dx = said.position.x - before.position.x,
                  dy = said.position.y - before.position.y,
                  dz = said.position.z - before.position.z;
      const bool moved = std::sqrt(dx * dx + dy * dy + dz * dz) > kPlaceMoveMetres ||
                         PlaceTurn(said.orientation, before.orientation) > kPlaceTurnRadians ||
                         said.appearance != before.appearance;
      if (!moved || hostNowUs - was->second.saidUs < kPlaceEveryUs) {
        placed.emplace(sampled.key, was->second);
        continue;
      }
      said.prop = before.prop;
    } else
      said.prop = ++m_propCount;
    placed.emplace(sampled.key, Placed{said, hostNowUs});
  }
  for (const auto &[key, was] : m_placed) {
    const auto now = placed.find(key);
    if (now == placed.end() || now->second.said.prop != was.said.prop) {
      auto gone = was.said;
      gone.flags |= proto::kPropGone;
      out.push_back(gone);
    }
  }
  for (const auto &[key, now] : placed) {
    const auto was = m_placed.find(key);
    if (was == m_placed.end() || was->second.said != now.said)
      out.push_back(now.said);
  }
  m_placed = std::move(placed);
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

std::vector<proto::NpcId> Stage::Speakers(std::uint64_t hostNowUs) const {
  std::vector<proto::NpcId> out;
  if (!On())
    return out;
  for (const auto &[id, line] : m_lines)
    if (line.speaker != 0 && IsScenes(line) && hostNowUs < EndOf(line) + kSpeakerTailUs &&
        std::ranges::find(out, line.speaker) == out.end())
      out.push_back(line.speaker);
  return out;
}

// ---- guest ----

bool Stage::Casts(proto::NpcId npc) const {
  return On() &&
         std::ranges::any_of(m_cast.actors, [&](const auto &actor) { return actor.npc == npc; });
}

bool Replay::Holds(proto::NpcId npc, proto::PlayerId player) const {
  return std::ranges::any_of(m_props, [&](const auto &entry) {
    const auto &prop = entry.second;
    return npc != 0 ? prop.holder == npc
                    : prop.holder == 0 && player != proto::kNoPlayer && prop.holderPlayer == player;
  });
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
  // Its props go with it, before the hands that held them.
  for (const auto &[id, prop] : m_props)
    out.push_back({.kind = Cue::Kind::PropGone, .prop = prop});
  m_props.clear();
  for (const auto &[id, place] : m_placed)
    out.push_back({.kind = Cue::Kind::PlaceGone, .place = place});
  m_placed.clear();
  for (const auto &actor : m_cast)
    out.push_back({.kind = Cue::Kind::ActorGone, .actor = actor});
  m_cast.clear();
  auto ended = m_state;
  ended.flags = flags;
  m_state = {};
  out.push_back({.kind = Cue::Kind::End, .state = ended});
}

const proto::ScenePlace *Replay::PlaceOf(std::uint32_t prop) const {
  const auto it = m_placed.find(prop);
  return it == m_placed.end() ? nullptr : &it->second;
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
  if (const auto *prop = std::get_if<proto::SceneProp>(&message)) {
    if (!On() || prop->play != m_state.play)
      return out;
    const auto it = m_props.find(prop->prop);
    if (prop->flags & proto::kPropGone) {
      if (it != m_props.end()) {
        out.push_back({.kind = Cue::Kind::PropGone, .prop = it->second});
        m_props.erase(it);
      }
      return out;
    }
    if ((it != m_props.end() && it->second == *prop) ||
        (it == m_props.end() && m_props.size() >= proto::kMaxSceneProps))
      return out; // a repeat (a resync), or more than a scene holds
    m_props[prop->prop] = *prop;
    out.push_back({.kind = Cue::Kind::Prop, .prop = *prop});
    return out;
  }
  if (const auto *place = std::get_if<proto::ScenePlace>(&message)) {
    if (!On() || place->play != m_state.play)
      return out;
    const auto it = m_placed.find(place->prop);
    if (place->flags & proto::kPropGone) {
      if (it != m_placed.end()) {
        out.push_back({.kind = Cue::Kind::PlaceGone, .place = it->second});
        m_placed.erase(it);
      }
      return out;
    }
    if ((it != m_placed.end() && it->second == *place) ||
        (it == m_placed.end() && m_placed.size() >= proto::kMaxSceneProps))
      return out; // a repeat (a resync), or more than a scene holds
    m_placed[place->prop] = *place;
    out.push_back({.kind = Cue::Kind::Place, .place = *place});
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

// ---- the scene's streams share one budget ----

void Pacer::Split(std::uint64_t nowUs) {
  std::erase_if(m_streams,
                [&](const auto &entry) { return nowUs > entry.second.offeredUs + kStreamIdleUs; });
  m_splitUs = nowUs;
  m_splitCount = m_streams.size();
  std::vector<Stream *> order;
  for (auto &[name, stream] : m_streams) {
    stream.share = 0.f;
    order.push_back(&stream);
  }
  std::ranges::stable_sort(order,
                           [](const Stream *a, const Stream *b) { return a->weight > b->weight; });
  // Each stream that fits gets what a frame every kSlowestUs takes; what is
  // left of the budget goes by weight.
  const float slowest = static_cast<float>(kSlowestUs) / 1e6f;
  float left = static_cast<float>(m_budget), weights = 0.f;
  for (auto *stream : order) {
    const float need = (stream->bytes + static_cast<float>(kStreamOverhead)) / slowest;
    if (need > left)
      continue;
    left -= need;
    stream->share = need;
    weights += stream->weight;
  }
  if (weights > 0.f)
    for (auto *stream : order)
      if (stream->share > 0.f)
        stream->share += left * stream->weight / weights;
}

bool Pacer::Due(std::uint64_t stream, float weight, std::uint64_t nowUs) {
  const bool known = m_streams.contains(stream);
  auto &entry = m_streams[stream];
  entry.weight = std::clamp(weight, 0.05f, 16.f);
  entry.offeredUs = nowUs;
  // Another set of streams, or a while since: split again.
  if (!known || m_splitCount != m_streams.size() || nowUs < m_splitUs ||
      nowUs - m_splitUs >= 250'000)
    Split(nowUs);
  return entry.share > 0.f && nowUs >= entry.nextUs;
}

void Pacer::Sent(std::uint64_t stream, std::size_t bytes, std::uint64_t nowUs) {
  const auto it = m_streams.find(stream);
  if (it == m_streams.end())
    return;
  auto &entry = it->second;
  entry.bytes += (static_cast<float>(bytes) - entry.bytes) * 0.2f;
  if (entry.share <= 0.f)
    return;
  const auto interval = std::max<std::uint64_t>(
      kFastestUs, static_cast<std::uint64_t>(static_cast<double>(bytes + kStreamOverhead) /
                                             static_cast<double>(entry.share) * 1e6));
  // On time (within two frames of when it was due): the next one counts
  // from when it was due, so the rate is the share's, not a frame slower.
  const bool onTime = entry.nextUs != 0 && nowUs >= entry.nextUs && nowUs - entry.nextUs < 66'000;
  entry.nextUs = (onTime ? entry.nextUs : nowUs) + interval;
}

float Pacer::Share(std::uint64_t stream) const {
  const auto it = m_streams.find(stream);
  return it == m_streams.end() ? 0.f : it->second.share;
}

std::size_t Pacer::Served() const {
  return static_cast<std::size_t>(
      std::ranges::count_if(m_streams, [](const auto &entry) { return entry.second.share > 0.f; }));
}

// ---- the camera of a cutscene ----

namespace {
float Dot(const proto::Quat &a, const proto::Quat &b) {
  return a.i * b.i + a.j * b.j + a.k * b.k + a.r * b.r;
}

proto::Quat Unit(const proto::Quat &q) {
  const float length = std::sqrt(Dot(q, q));
  if (!(length > 1e-6f))
    return {};
  return {q.i / length, q.j / length, q.k / length, q.r / length};
}

// The angle between two orientations, radians.
float Turn(const proto::Quat &a, const proto::Quat &b) {
  return 2.f * std::acos(std::clamp(std::abs(Dot(Unit(a), Unit(b))), 0.f, 1.f));
}

proto::Quat Blend(const proto::Quat &from, const proto::Quat &to, float t) {
  // Normalized lerp the short way: the two are a frame apart.
  const float sign = Dot(from, to) < 0.f ? -1.f : 1.f;
  return Unit({from.i + (sign * to.i - from.i) * t, from.j + (sign * to.j - from.j) * t,
               from.k + (sign * to.k - from.k) * t, from.r + (sign * to.r - from.r) * t});
}

// A quaternion in 5 bytes, as pose::PackQuat5 (kept here: scene.cpp does
// not depend on the pose code): the index of the largest component in 2
// bits, the three others in 13, 13 and 12.
constexpr float kInvSqrt2 = 0.70710678f;
constexpr int kQuatBits[3] = {13, 13, 12};

void PackOrientation(const proto::Quat &orientation, std::uint8_t *out) {
  const auto q = Unit(orientation);
  const float c[4] = {q.i, q.j, q.k, q.r};
  int largest = 0;
  for (int i = 1; i < 4; ++i)
    if (std::abs(c[i]) > std::abs(c[largest]))
      largest = i;
  const float sign = c[largest] < 0.f ? -1.f : 1.f;
  std::uint64_t bits = static_cast<std::uint64_t>(largest);
  int shift = 2, n = 0;
  for (int i = 0; i < 4; ++i) {
    if (i == largest)
      continue;
    const auto max = (1u << kQuatBits[n]) - 1;
    const float unit = std::clamp((c[i] * sign / kInvSqrt2 + 1.f) * 0.5f, 0.f, 1.f);
    bits |= static_cast<std::uint64_t>(std::lround(unit * static_cast<float>(max))) << shift;
    shift += kQuatBits[n++];
  }
  for (std::size_t byte = 0; byte < proto::kPoseFrameRotationBytes; ++byte)
    out[byte] = static_cast<std::uint8_t>(bits >> (8 * byte));
}

proto::Quat UnpackOrientation(const std::uint8_t *in) {
  std::uint64_t bits = 0;
  for (std::size_t byte = 0; byte < proto::kPoseFrameRotationBytes; ++byte)
    bits |= static_cast<std::uint64_t>(in[byte]) << (8 * byte);
  const int largest = static_cast<int>(bits & 3);
  float c[4]{};
  float sum = 0.f;
  int shift = 2, n = 0;
  for (int i = 0; i < 4; ++i) {
    if (i == largest)
      continue;
    const auto max = (1u << kQuatBits[n]) - 1;
    const auto value = static_cast<float>((bits >> shift) & max) / static_cast<float>(max);
    c[i] = (value * 2.f - 1.f) * kInvSqrt2;
    sum += c[i] * c[i];
    shift += kQuatBits[n++];
  }
  c[largest] = std::sqrt(std::max(0.f, 1.f - sum));
  return Unit({c[0], c[1], c[2], c[3]});
}
} // namespace

bool IsCut(const CameraView &from, const CameraView &to, float seconds) {
  // A camera on a crane or a dolly does not do 12 m/s nor 240 degrees a
  // second; a new shot is somewhere else at once.
  const float time = std::clamp(seconds, 0.f, 0.5f);
  const float moved =
      std::sqrt((to.position.x - from.position.x) * (to.position.x - from.position.x) +
                (to.position.y - from.position.y) * (to.position.y - from.position.y) +
                (to.position.z - from.position.z) * (to.position.z - from.position.z));
  return moved > 0.4f + 12.f * time ||
         Turn(from.orientation, to.orientation) > 0.2f + 4.2f * time ||
         std::abs(to.fov - from.fov) > 6.f + 60.f * time;
}

proto::SceneCamera PackCamera(std::uint32_t play, const CameraView &view, bool cut) {
  proto::SceneCamera camera{.play = play,
                            .position = view.position,
                            .fov = std::clamp(view.fov, 1.f, 180.f),
                            .flags = cut ? proto::kCameraCut : std::uint8_t{0}};
  PackOrientation(view.orientation, camera.orientation.data());
  return camera;
}

CameraView UnpackCamera(const proto::SceneCamera &camera) {
  return {camera.position, UnpackOrientation(camera.orientation.data()), camera.fov};
}

bool CameraTrack::Push(const proto::SceneCamera &camera) {
  if (camera.play != m_play) {
    // Another scene's camera: the old one's views are not its.
    if (m_play != 0 && static_cast<std::int32_t>(camera.play - m_play) < 0)
      return false;
    Clear();
    m_play = camera.play;
  } else if (!m_samples.empty() && static_cast<std::int32_t>(camera.seq - m_lastSeq) <= 0) {
    return false;
  }
  if (!m_samples.empty() && camera.timeUs <= m_samples.back().timeUs)
    m_samples.clear(); // the host's clock jumped: start over
  if (!m_samples.empty() && camera.timeUs - m_samples.back().timeUs < 1'000'000)
    m_intervalUs +=
        (static_cast<double>(camera.timeUs - m_samples.back().timeUs) - m_intervalUs) / 8.0;
  m_lastSeq = camera.seq;
  ++m_received;
  m_samples.push_back(
      {camera.timeUs, UnpackCamera(camera), (camera.flags & proto::kCameraCut) != 0});
  if (m_samples.size() > kMaxSamples)
    m_samples.erase(m_samples.begin());
  return true;
}

std::uint64_t CameraTrack::DelayUs() const {
  return std::clamp<std::uint64_t>(static_cast<std::uint64_t>(m_intervalUs * 1.5) + 20'000, 60'000,
                                   400'000);
}

void CameraTrack::Clear() {
  m_samples.clear();
  m_play = 0;
  m_lastSeq = 0;
  m_intervalUs = 66'000;
}

bool CameraTrack::At(std::uint64_t nowUs, CameraView &out) const {
  if (m_samples.empty() || nowUs > m_samples.back().timeUs + DelayUs() + kStaleUs)
    return false;
  const auto renderUs = nowUs > DelayUs() ? nowUs - DelayUs() : 0;
  if (renderUs <= m_samples.front().timeUs) {
    out = m_samples.front().view;
    return true;
  }
  for (std::size_t i = 1; i < m_samples.size(); ++i) {
    const auto &b = m_samples[i];
    if (renderUs >= b.timeUs)
      continue;
    const auto &a = m_samples[i - 1];
    if (b.cut) {
      out = a.view; // the old shot until the new one's time
      return true;
    }
    const float t =
        static_cast<float>(renderUs - a.timeUs) / static_cast<float>(b.timeUs - a.timeUs);
    out.position = {a.view.position.x + (b.view.position.x - a.view.position.x) * t,
                    a.view.position.y + (b.view.position.y - a.view.position.y) * t,
                    a.view.position.z + (b.view.position.z - a.view.position.z) * t};
    out.orientation = Blend(a.view.orientation, b.view.orientation, t);
    out.fov = a.view.fov + (b.view.fov - a.view.fov) * t;
    return true;
  }
  out = m_samples.back().view; // past the newest: held
  return true;
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
