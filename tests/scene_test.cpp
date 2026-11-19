// Scene replay without the game (docs/architecture/scene-replay.md): what
// the host says of its scene, what a guest does with it, where a guest's V
// may stand, and the whole path through real endpoints over loopback UDP: a
// guest arriving mid-scene, a skip, an interruption, a host that vanishes,
// the epoch fence and the digests.

#include "ports.hpp"
#include "tandem/endpoint.hpp"
#include "tandem/pose.hpp"
#include "tandem/scene.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <functional>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
using test::NextPort;
using Kind = scene::Cue::Kind;

constexpr std::uint64_t kSecond = 1'000'000;
constexpr std::uint64_t kT0 = 5'000 * kSecond; // a host clock
constexpr std::uint64_t kScene = 0xabcdef0123ull;

bool Pump(std::vector<Endpoint *> endpoints,
          const std::function<bool(std::size_t, const std::vector<SessionEvent> &)> &done,
          std::chrono::milliseconds limit = 3s) {
  const auto deadline = std::chrono::steady_clock::now() + limit;
  bool finished = false;
  while (!finished && std::chrono::steady_clock::now() < deadline)
    for (std::size_t i = 0; i < endpoints.size(); ++i)
      finished = done(i, endpoints[i]->Update(1ms)) || finished;
  return finished;
}

constexpr Timing kFast{.timeout = 1000ms, .takeover = 400ms};

template <class T> const T *Find(const std::vector<proto::Message> &messages) {
  for (const auto &message : messages)
    if (const auto *found = std::get_if<T>(&message))
      return found;
  return nullptr;
}

std::size_t Count(const std::vector<scene::Cue> &cues, Kind kind) {
  return static_cast<std::size_t>(
      std::ranges::count_if(cues, [&](const auto &cue) { return cue.kind == kind; }));
}

const scene::Cue *First(const std::vector<scene::Cue> &cues, Kind kind) {
  for (const auto &cue : cues)
    if (cue.kind == kind)
      return &cue;
  return nullptr;
}

scene::HostSample Talk(std::uint64_t which = kScene) {
  return {.scene = which,
          .tier = 3,
          .host = {10.f, 10.f, 5.f},
          .actors = {{.npc = 7, .position = {10.f, 13.f, 5.f}},
                     {.npc = 4, .position = {12.f, 12.f, 5.f}, .flags = proto::kActorGhost}}};
}

// A guest's game as the plugin drives it: every scene message through a
// Replay at that endpoint's host clock.
struct Watcher {
  scene::Replay replay;
  std::vector<scene::Cue> cues;
  void Take(Endpoint &endpoint, const std::vector<SessionEvent> &events) {
    for (const auto &event : events) {
      if (const auto *story = std::get_if<events::Story>(&event)) {
        auto more = replay.Note(story->message, endpoint.HostNowUs());
        cues.insert(cues.end(), more.begin(), more.end());
      } else if (std::holds_alternative<events::Ended>(event)) {
        auto more = replay.Reset();
        cues.insert(cues.end(), more.begin(), more.end());
      }
    }
  }
};
} // namespace

TEST_CASE(
    "the host's stage: a scene begins with its cast and staging, and says only what changes") {
  scene::Stage stage;
  CHECK(stage.Update({}, kT0).empty());

  auto out = stage.Update(Talk(), kT0);
  REQUIRE(out.size() == 2);
  const auto *state = Find<proto::SceneState>(out);
  const auto *cast = Find<proto::SceneCast>(out);
  REQUIRE((state && cast));
  CHECK(state->scene == kScene);
  CHECK(state->play == 1);
  CHECK(state->startUs == kT0);
  CHECK(state->tier == 3);
  CHECK(state->rate == 1.f);
  CHECK((state->flags & proto::kSceneStaged) != 0);
  // The middle of the host's V and the two actors; facing it from the host.
  CHECK(state->anchor.x == doctest::Approx(32.f / 3.f));
  CHECK(state->anchor.y == doctest::Approx(35.f / 3.f));
  CHECK(state->radius > 1.f);
  CHECK(std::abs(state->yaw) < 1.f);
  REQUIRE(cast->actors.size() == 2);
  CHECK(cast->play == 1);
  CHECK(cast->actors[0].npc == 4); // sorted
  CHECK(cast->actors[0].flags == proto::kActorGhost);

  // The same again: nothing. A small shuffle: nothing.
  CHECK(stage.Update(Talk(), kT0 + kSecond).empty());
  auto shuffled = Talk();
  shuffled.actors[0].position.x += 0.3f;
  CHECK(stage.Update(shuffled, kT0 + 2 * kSecond).empty());
  // An actor walks off: the staging follows.
  auto walked = Talk();
  walked.actors[0].position.y += 6.f;
  out = stage.Update(walked, kT0 + 3 * kSecond);
  REQUIRE(out.size() == 1);
  CHECK(Find<proto::SceneState>(out)->anchor.y > state->anchor.y + 1.f);

  // Johnny goes: the cast says so, whole.
  walked.actors.pop_back();
  out = stage.Update(walked, kT0 + 4 * kSecond);
  const auto *smaller = Find<proto::SceneCast>(out);
  REQUIRE(smaller);
  CHECK(smaller->actors.size() == 1);

  // The host fast-forwards, then a hub holds the scene's time.
  walked.rate = 4.f;
  out = stage.Update(walked, kT0 + 5 * kSecond);
  REQUIRE(Find<proto::SceneState>(out));
  CHECK((Find<proto::SceneState>(out)->flags & proto::kSceneSkipping) != 0);
  walked.rate = 1.f;
  walked.held = true;
  out = stage.Update(walked, kT0 + 6 * kSecond);
  CHECK((Find<proto::SceneState>(out)->flags & proto::kSceneHeld) != 0);
  CHECK(Find<proto::SceneState>(out)->rate == 0.f);

  // Over, with nobody speaking: a plain end. The next scene is another play.
  out = stage.Update({}, kT0 + 7 * kSecond);
  REQUIRE(out.size() == 1);
  CHECK(Find<proto::SceneState>(out)->scene == 0);
  CHECK(Find<proto::SceneState>(out)->play == 1);
  CHECK(Find<proto::SceneState>(out)->flags == 0);
  CHECK_FALSE(stage.On());
  out = stage.Update(Talk(), kT0 + 8 * kSecond);
  CHECK(Find<proto::SceneState>(out)->play == 2);
  // One scene straight into another: a new play, a new cast.
  out = stage.Update(Talk(kScene + 1), kT0 + 9 * kSecond);
  REQUIRE(out.size() == 2);
  CHECK(Find<proto::SceneState>(out)->play == 3);
  CHECK(Find<proto::SceneCast>(out)->play == 3);
}

TEST_CASE("the host's stage: lines carry their start, a hide before the end is a cut, an end "
          "mid-line an interruption") {
  scene::Stage stage;
  stage.Update(Talk(), kT0);
  const auto line = stage.Line(501, 7, proto::kNoPlayer, 4.f, 0, kT0 + kSecond);
  REQUIRE(line);
  CHECK(line->play == 1);
  CHECK(line->startUs == kT0 + kSecond);
  CHECK(line->speaker == 7);
  // Its subtitle goes when it is over: nothing to say.
  CHECK_FALSE(stage.Hide(501, kT0 + 5 * kSecond).has_value());
  CHECK_FALSE(stage.Hide(501, kT0 + 5 * kSecond).has_value()); // nor twice

  // The host skips the next one: a cut.
  REQUIRE(stage.Line(502, 0, 1, 6.f, 0, kT0 + 6 * kSecond));
  const auto cut = stage.Hide(502, kT0 + 7 * kSecond);
  REQUIRE(cut);
  CHECK((cut->flags & proto::kVoiceStop) != 0);
  CHECK(cut->line == 502);

  // A bark and a holocall are no scene's.
  const auto bark = stage.Line(600, 9, proto::kNoPlayer, 2.f, proto::kVoiceOverhead, kT0);
  REQUIRE(bark);
  CHECK(bark->play == 0);

  // The scene ends while someone speaks: interrupted.
  REQUIRE(stage.Line(503, 7, proto::kNoPlayer, 8.f, 0, kT0 + 10 * kSecond));
  const auto out = stage.Update({}, kT0 + 11 * kSecond);
  REQUIRE(out.size() == 1);
  CHECK((Find<proto::SceneState>(out)->flags & proto::kSceneInterrupted) != 0);
}

TEST_CASE("a private scene is never announced, nor its lines") {
  scene::Stage stage;
  auto date = Talk();
  date.isPrivate = true;
  CHECK(stage.Update(date, kT0).empty());
  CHECK_FALSE(stage.On());
  CHECK_FALSE(stage.Line(501, 7, proto::kNoPlayer, 4.f, 0, kT0).has_value());
  CHECK_FALSE(stage.Line(502, 7, proto::kNoPlayer, 4.f, proto::kVoiceHolocall, kT0).has_value());

  // A scene everyone watched turns private (the others walk off, the
  // classification arrives late): it ends for them, and stays ended.
  scene::Stage open;
  REQUIRE(open.Update(Talk(), kT0).size() == 2);
  const auto out = open.Update(date, kT0 + kSecond);
  REQUIRE(out.size() == 1);
  CHECK(Find<proto::SceneState>(out)->scene == 0);
  CHECK(open.Update(date, kT0 + 2 * kSecond).empty());
  CHECK_FALSE(open.Line(503, 7, proto::kNoPlayer, 4.f, 0, kT0 + 2 * kSecond).has_value());
  // Private no more (a public scene follows): lines go again.
  open.Update(Talk(kScene + 5), kT0 + 3 * kSecond);
  CHECK(open.Line(504, 7, proto::kNoPlayer, 4.f, 0, kT0 + 3 * kSecond).has_value());
}

TEST_CASE("a guest's replay: begin, cast, voices, a skip, the end") {
  scene::Stage stage;
  scene::Replay replay;
  std::vector<scene::Cue> cues;
  const auto feed = [&](const std::vector<proto::Message> &messages, std::uint64_t now) {
    cues.clear();
    for (const auto &message : messages) {
      auto more = replay.Note(message, now);
      cues.insert(cues.end(), more.begin(), more.end());
    }
  };
  feed(stage.Update(Talk(), kT0), kT0 + 40'000);
  REQUIRE(cues.size() == 3);
  CHECK(cues[0].kind == Kind::Begin);
  CHECK(cues[0].state.scene == kScene);
  CHECK(Count(cues, Kind::Actor) == 2);
  CHECK(replay.On());
  CHECK(replay.Acts(7));
  CHECK_FALSE(replay.Acts(8));

  // A line, heard 40 ms after it began: from its start.
  const auto first = *stage.Line(501, 7, proto::kNoPlayer, 4.f, 0, kT0 + kSecond);
  feed({first}, kT0 + kSecond + 40'000);
  REQUIRE(cues.size() == 1);
  CHECK(cues[0].kind == Kind::Voice);
  CHECK(cues[0].seconds == 0.f);
  CHECK(cues[0].line.speaker == 7);
  // The same again (a resync): it plays already.
  feed({first}, kT0 + 2 * kSecond);
  CHECK(cues.empty());
  // The same line said again later (a bark twice): it starts over.
  auto again = first;
  again.startUs += kSecond;
  feed({again}, kT0 + 2 * kSecond + 100'000);
  REQUIRE(cues.size() == 2);
  CHECK(cues[0].kind == Kind::VoiceStop);
  CHECK(cues[1].kind == Kind::Voice);
  CHECK(replay.Speaking() == 1);
  // The same speaker's next line stops the first.
  feed({*stage.Line(502, 7, proto::kNoPlayer, 4.f, 0, kT0 + 3 * kSecond)}, kT0 + 3 * kSecond);
  REQUIRE(cues.size() == 2);
  CHECK(cues[0].kind == Kind::VoiceStop);
  CHECK(cues[0].line.line == 501);
  CHECK(cues[1].kind == Kind::Voice);
  // The host's V answers over it: both play.
  feed({*stage.Line(503, 0, 1, 5.f, 0, kT0 + 4 * kSecond)}, kT0 + 4 * kSecond);
  CHECK(Count(cues, Kind::Voice) == 1);
  CHECK(Count(cues, Kind::VoiceStop) == 0);
  CHECK(replay.Speaking() == 2);

  // The host fast-forwards: the voices stop, lines begun meanwhile are not
  // played, the scene goes on.
  auto fast = Talk();
  fast.rate = 4.f;
  feed(stage.Update(fast, kT0 + 5 * kSecond), kT0 + 5 * kSecond);
  CHECK(Count(cues, Kind::VoiceStop) == 2);
  CHECK(Count(cues, Kind::Change) == 1);
  CHECK(replay.Speaking() == 0);
  feed({*stage.Line(504, 7, proto::kNoPlayer, 4.f, 0, kT0 + 5 * kSecond)}, kT0 + 5 * kSecond);
  CHECK(cues.empty());
  feed(stage.Update(Talk(), kT0 + 6 * kSecond), kT0 + 6 * kSecond);
  CHECK(Count(cues, Kind::Change) == 1);

  // A choice made, the scene goes on with another cast.
  auto after = Talk();
  after.actors.erase(after.actors.begin());
  after.actors.push_back({.npc = 9, .position = {9.f, 12.f, 5.f}});
  feed(stage.Update(after, kT0 + 7 * kSecond), kT0 + 7 * kSecond);
  REQUIRE(Count(cues, Kind::ActorGone) == 1);
  CHECK(First(cues, Kind::ActorGone)->actor.npc == 7);
  REQUIRE(Count(cues, Kind::Actor) == 1);
  CHECK(First(cues, Kind::Actor)->actor.npc == 9);

  // A cut line, then the end: voices stop, every actor is let go.
  feed({*stage.Line(505, 9, proto::kNoPlayer, 9.f, 0, kT0 + 8 * kSecond)}, kT0 + 8 * kSecond);
  feed(stage.Update({}, kT0 + 9 * kSecond), kT0 + 9 * kSecond);
  CHECK(Count(cues, Kind::VoiceStop) == 1);
  CHECK(Count(cues, Kind::ActorGone) == 2);
  REQUIRE(Count(cues, Kind::End) == 1);
  CHECK((cues.back().state.flags & proto::kSceneInterrupted) != 0);
  CHECK(cues.back().kind == Kind::End);
  CHECK_FALSE(replay.On());
  CHECK(replay.Cast().empty());
}

TEST_CASE(
    "a guest's replay: late lines seek or are dropped, old plays are void, a reset frees all") {
  scene::Replay replay;
  proto::SceneState state{.scene = kScene, .play = 4, .startUs = kT0, .tier = 4};
  // Heard 12 s into the scene (a late joiner).
  auto cues = replay.Note(state, kT0 + 12 * kSecond);
  REQUIRE(cues.size() == 1);
  CHECK(cues[0].seconds == doctest::Approx(12.f));
  CHECK(replay.Seconds(kT0 + 13 * kSecond) == doctest::Approx(13.f));
  replay.Note(proto::SceneCast{.play = 4, .actors = {{7, 0}}}, kT0 + 12 * kSecond);

  // A line begun 2 s ago of 6: from 2 s in.
  cues = replay.Note(
      proto::SceneLine{
          .play = 4, .line = 1, .speaker = 7, .startUs = kT0 + 10 * kSecond, .duration = 6.f},
      kT0 + 12 * kSecond);
  REQUIRE(cues.size() == 1);
  CHECK(cues[0].kind == Kind::Voice);
  CHECK(cues[0].seconds == doctest::Approx(2.f));
  // One that is all but over: not played.
  cues = replay.Note(
      proto::SceneLine{
          .play = 4, .line = 2, .speakerPlayer = 1, .startUs = kT0 + 8 * kSecond, .duration = 4.1f},
      kT0 + 12 * kSecond);
  CHECK(cues.empty());
  CHECK(replay.Stale() == 1);
  // A line or a cast of another play, the end of another play: void.
  CHECK(replay
            .Note(proto::SceneLine{.play = 3, .line = 3, .startUs = kT0 + 12 * kSecond},
                  kT0 + 12 * kSecond)
            .empty());
  CHECK(replay.Note(proto::SceneCast{.play = 3}, kT0 + 12 * kSecond).empty());
  CHECK(replay.Note(proto::SceneState{.play = 3}, kT0 + 12 * kSecond).empty());
  CHECK(replay.On());
  // A cut of a line we never played: nothing.
  CHECK(replay
            .Note(proto::SceneLine{.play = 4, .line = 2, .flags = proto::kVoiceStop},
                  kT0 + 12 * kSecond)
            .empty());
  // A bark outside any scene plays; it survives the scene's end.
  cues = replay.Note(proto::SceneLine{.line = 9,
                                      .speaker = 30,
                                      .startUs = kT0 + 12 * kSecond,
                                      .duration = 20.f,
                                      .flags = proto::kVoiceOverhead},
                     kT0 + 12 * kSecond);
  CHECK(Count(cues, Kind::Voice) == 1);

  // Another scene with no end of this one in between: ours ends first.
  state.play = 5;
  state.scene = kScene + 1;
  cues = replay.Note(state, kT0 + 13 * kSecond);
  REQUIRE(cues.size() == 4);
  CHECK(cues[0].kind == Kind::VoiceStop);
  CHECK(cues[0].line.line == 1);
  CHECK(cues[1].kind == Kind::ActorGone);
  CHECK(cues[2].kind == Kind::End);
  CHECK(cues[3].kind == Kind::Begin);
  CHECK(replay.Speaking() == 1); // the bark

  // Lines end by themselves.
  replay.Tick(kT0 + 40 * kSecond);
  CHECK(replay.Speaking() == 0);

  // The host is gone: the scene stops, as an interruption.
  replay.Note(proto::SceneCast{.play = 5, .actors = {{7, 0}, {8, proto::kActorGhost}}},
              kT0 + 41 * kSecond);
  replay.Note(proto::SceneLine{.play = 5, .line = 11, .speaker = 8, .startUs = kT0 + 41 * kSecond},
              kT0 + 41 * kSecond);
  cues = replay.Reset();
  CHECK(Count(cues, Kind::VoiceStop) == 1);
  CHECK(Count(cues, Kind::ActorGone) == 2);
  REQUIRE(Count(cues, Kind::End) == 1);
  CHECK((First(cues, Kind::End)->state.flags & proto::kSceneInterrupted) != 0);
  CHECK_FALSE(replay.On());
  CHECK(replay.Reset().empty());
}

TEST_CASE("a place to watch from: beside the host, clear of everyone, out of its line of sight") {
  // The host at the origin looks north at two actors.
  const proto::Vec3 host{0.f, 0.f, 2.f};
  const std::vector<proto::Vec3> actors{{-0.6f, 3.f, 2.f}, {0.7f, 3.2f, 2.f}};
  proto::SceneState state{.scene = kScene,
                          .play = 1,
                          .flags = proto::kSceneStaged,
                          .anchor = {0.f, 2.f, 2.f},
                          .radius = 2.f};
  // Our V arrives from the east.
  const proto::Vec3 own{9.f, 1.f, 2.4f};
  const auto spot = scene::WatchSpot(state, actors, host, own);
  REQUIRE(spot.found);
  CHECK(spot.position.x > 0.f);                // our side
  CHECK(spot.position.y < state.anchor.y + 1); // the host's half of the ring
  CHECK(spot.position.z == host.z);            // the host's floor
  for (const auto &body : actors)
    CHECK(std::hypot(spot.position.x - body.x, spot.position.y - body.y) >=
          scene::kSpotClearMetres);
  CHECK(std::hypot(spot.position.x, spot.position.y) >= scene::kSpotClearMetres);
  // Not between the host and the actors, nor right behind the host.
  const float bearing = std::atan2(spot.position.y - state.anchor.y, spot.position.x);
  const float toHost = std::atan2(host.y - state.anchor.y, host.x);
  CHECK(std::abs(std::remainder(bearing - toHost, 2.f * 3.14159265f)) > 0.4f);
  // It faces the scene: forward (-sin yaw, cos yaw) points at the middle.
  const float fx = -std::sin(spot.yaw), fy = std::cos(spot.yaw);
  const float tx = state.anchor.x - spot.position.x, ty = state.anchor.y - spot.position.y;
  CHECK((fx * tx + fy * ty) / std::hypot(tx, ty) > 0.99f);

  // Another watcher already stands there: the next place.
  std::vector<proto::Vec3> taken = actors;
  taken.push_back(spot.position);
  const auto second = scene::WatchSpot(state, taken, host, own);
  REQUIRE(second.found);
  CHECK(std::hypot(second.position.x - spot.position.x, second.position.y - spot.position.y) >=
        scene::kSpotClearMetres);

  // No staging, no place.
  state.flags = 0;
  CHECK_FALSE(scene::WatchSpot(state, actors, host, own).found);
  CHECK_FALSE(scene::WatchSpot({}, actors, host, own).found);
}

TEST_CASE("our V is never left inside an actor") {
  const std::vector<proto::Vec3> actors{{0.f, 0.f, 0.f}, {5.f, 0.f, 0.f}};
  // Clear: no step.
  auto step = scene::KeepOut({2.f, 0.f, 0.f}, actors, 0.6f, 0.15f);
  CHECK(step.x == 0.f);
  CHECK(step.y == 0.f);
  // An actor walked into us: out the short way, a step at a time.
  step = scene::KeepOut({0.2f, 0.f, 0.f}, actors, 0.6f, 0.15f);
  CHECK(step.x == doctest::Approx(0.15f));
  CHECK(step.y == doctest::Approx(0.f));
  step = scene::KeepOut({0.55f, 0.f, 0.f}, actors, 0.6f, 0.15f);
  CHECK(step.x == doctest::Approx(0.05f));
  // Right on it: any way out, never nowhere.
  step = scene::KeepOut({0.f, 0.f, 0.f}, actors, 0.6f, 0.15f);
  CHECK(std::hypot(step.x, step.y) == doctest::Approx(0.15f));
  // The floor above is not in the way.
  step = scene::KeepOut({0.f, 0.f, 3.f}, actors, 0.6f, 0.15f);
  CHECK(std::hypot(step.x, step.y) == 0.f);
}

TEST_CASE("the story record keeps the scene for a joiner and hashes it") {
  story::Record host, guest;
  const proto::SceneState state{.scene = kScene, .play = 2, .startUs = kT0, .tier = 4};
  const proto::SceneCast cast{.play = 2, .actors = {{7, proto::kActorPosed}}};
  const proto::SceneLine line{
      .play = 2, .line = 31, .speaker = 7, .startUs = kT0 + kSecond, .duration = 5.f};
  const auto before = host.Digest();
  for (auto *record : {&host, &guest}) {
    CHECK(record->Note(state));
    CHECK_FALSE(record->Note(state)); // a repeat changes nothing
    CHECK(record->Note(cast));
    CHECK_FALSE(record->Note(cast));
    CHECK(record->Note(line));
    CHECK_FALSE(record->Note(line));
  }
  CHECK(host.Digest() != before);
  CHECK(host.Digest() == guest.Digest());

  // A joiner: the state, then the cast, then the line being spoken.
  const auto joiner = host.ForJoiner();
  REQUIRE(joiner.size() == 3);
  CHECK(std::get<proto::SceneState>(joiner[0]) == state);
  CHECK(std::get<proto::SceneCast>(joiner[1]) == cast);
  CHECK(std::get<proto::SceneLine>(joiner[2]) == line);

  // A cast or a line of another play is not kept.
  CHECK_FALSE(host.Note(proto::SceneCast{.play = 1, .actors = {{9, 0}}}));
  CHECK_FALSE(host.Note(proto::SceneLine{.play = 1, .line = 40}));
  // A cut line goes; a staging that moves does not change the digest.
  auto cut = line;
  cut.flags = proto::kVoiceStop;
  CHECK(host.Note(cut));
  CHECK_FALSE(host.Note(cut));
  auto moved = state;
  moved.anchor = {1.f, 2.f, 3.f};
  moved.flags = proto::kSceneStaged;
  const auto staged = host.Digest();
  CHECK(host.Note(moved));
  CHECK(host.Digest() == staged);

  // The guest never heard the end: the digests differ until it does.
  CHECK(host.Note(proto::SceneState{.play = 2}));
  CHECK(host.Digest() != guest.Digest());
  CHECK(host.Digest() == before);
  CHECK(host.ForJoiner().empty());
  CHECK(guest.Note(proto::SceneState{.play = 2}));
  CHECK(host.Digest() == guest.Digest());
  // An end is always passed on (the game lets its proxies go again).
  CHECK(guest.Note(proto::SceneState{.play = 2}));
}

TEST_CASE("a scene through the endpoints: a guest watches, another arrives mid-scene") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast);
  REQUIRE_MESSAGE(host, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
  REQUIRE(a);
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t, const auto &) {
    return a->IsJoined() && host->Players().size() == 2;
  }));
  host->SendNpcSpawn({.npc = 7, .record = 1});
  host->SendNpcSpawn({.npc = 4, .record = 2});
  REQUIRE(Pump({host.get(), a.get()},
               [&](std::size_t, const auto &) { return a->SharedNpcs().size() == 2; }));

  // The host's game: a scene begins, an actor speaks.
  scene::Stage stage;
  for (const auto &message : stage.Update(Talk(), host->HostNowUs()))
    host->SendStory(message);
  host->SendStory(*stage.Line(501, 7, proto::kNoPlayer, 30.f, 0, host->HostNowUs()));
  // A guest cannot speak for the scene.
  a->SendStory(proto::SceneState{.scene = 99, .play = 9});
  a->SendStory(proto::SceneLine{.line = 77});

  Watcher atA;
  std::size_t hostHeard = 0;
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1)
      atA.Take(*a, events);
    else
      for (const auto &event : events)
        hostHeard += std::holds_alternative<events::Story>(event);
    return Count(atA.cues, Kind::Voice) == 1;
  }));
  CHECK(hostHeard == 0);
  CHECK(Count(atA.cues, Kind::Begin) == 1);
  CHECK(Count(atA.cues, Kind::Actor) == 2);
  CHECK(First(atA.cues, Kind::Voice)->seconds == 0.f);
  CHECK(atA.replay.State().scene == kScene);

  // Poses: for the actors the guest knows, never for others, never from a guest.
  proto::ActorPose pose{.npc = 7};
  pose.hips = {0, 0, 8192};
  host->SendActorPose(pose);
  pose.npc = 55; // not shared
  host->SendActorPose(pose);
  pose.npc = 4;
  a->SendActorPose(pose); // a guest's word: nothing
  std::vector<proto::ActorPose> poses;
  Pump(
      {host.get(), a.get()},
      [&](std::size_t i, const auto &events) {
        for (const auto &event : events)
          if (const auto *heard = std::get_if<events::HostActorPose>(&event)) {
            CHECK(i == 1);
            poses.push_back(heard->pose);
          }
        return false;
      },
      300ms);
  REQUIRE(poses.size() == 1);
  CHECK(poses[0].npc == 7);
  CHECK(poses[0].timeUs != 0);
  // It is a pose a track takes.
  pose::PoseTrack track;
  track.Push(pose::FromActor(poses[0]));
  CHECK(track.Received() == 1);

  // Two seconds in, b joins: the scene at its current time, the line from
  // where it is now.
  Pump({host.get(), a.get()}, [](std::size_t, const auto &) { return false; }, 2000ms);
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error, kFast);
  REQUIRE(b);
  Watcher atB;
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    if (i == 2)
      atB.Take(*b, events);
    return Count(atB.cues, Kind::Voice) == 1;
  }));
  REQUIRE(Count(atB.cues, Kind::Begin) == 1);
  CHECK(First(atB.cues, Kind::Begin)->seconds > 1.5f);
  CHECK(First(atB.cues, Kind::Begin)->seconds < 4.f);
  CHECK(Count(atB.cues, Kind::Actor) == 2);
  CHECK(First(atB.cues, Kind::Voice)->seconds > 1.5f);
  CHECK(First(atB.cues, Kind::Voice)->seconds < 4.f);
  CHECK(First(atB.cues, Kind::Voice)->line.speaker == 7);

  atA.cues.clear();
  atB.cues.clear();

  // The scene ends: both let go, and all three records agree.
  for (const auto &message : stage.Update({}, host->HostNowUs()))
    host->SendStory(message);
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1)
      atA.Take(*a, events);
    if (i == 2)
      atB.Take(*b, events);
    return Count(atA.cues, Kind::End) == 1 && Count(atB.cues, Kind::End) == 1;
  }));
  for (const auto *watcher : {&atA, &atB}) {
    CHECK(Count(watcher->cues, Kind::VoiceStop) == 1);
    CHECK(Count(watcher->cues, Kind::ActorGone) == 2);
    CHECK((First(watcher->cues, Kind::End)->state.flags & proto::kSceneInterrupted) != 0);
    CHECK_FALSE(watcher->replay.On());
  }
}

TEST_CASE("the host vanishes mid-scene: the scene stops and every proxy is released") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
  REQUIRE((host && a));
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t, const auto &) { return a->IsJoined(); }));
  host->SendNpcSpawn({.npc = 7, .record = 1});
  host->SendNpcSpawn({.npc = 4, .record = 2});
  scene::Stage stage;
  for (const auto &message : stage.Update(Talk(), host->HostNowUs()))
    host->SendStory(message);
  host->SendStory(*stage.Line(501, 4, proto::kNoPlayer, 60.f, 0, host->HostNowUs()));
  Watcher atA;
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1)
      atA.Take(*a, events);
    return Count(atA.cues, Kind::Voice) == 1;
  }));
  atA.cues.clear();

  host.reset(); // no goodbye
  REQUIRE(Pump({a.get()}, [&](std::size_t, const auto &events) {
    atA.Take(*a, events);
    return Count(atA.cues, Kind::End) == 1;
  }));
  CHECK(Count(atA.cues, Kind::VoiceStop) == 1);
  CHECK(Count(atA.cues, Kind::ActorGone) == 2);
  CHECK_FALSE(atA.replay.On());
  CHECK(atA.replay.Speaking() == 0);
}

TEST_CASE("actor poses of the host's old world are void, as its NPC states are") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
  REQUIRE((host && a));
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t, const auto &) { return a->IsJoined(); }));
  host->SendNpcSpawn({.npc = 7, .record = 1});
  REQUIRE(Pump({host.get(), a.get()},
               [&](std::size_t, const auto &) { return a->SharedNpcs().contains(7); }));
  // The NPC goes; a pose for it sent after is for nobody.
  host->SendNpcGone({.npc = 7});
  host->SendActorPose({.npc = 7});
  int heard = 0;
  Pump(
      {host.get(), a.get()},
      [&](std::size_t, const auto &events) {
        for (const auto &event : events)
          heard += std::holds_alternative<events::HostActorPose>(event);
        return false;
      },
      300ms);
  CHECK(heard == 0);
  CHECK_FALSE(a->SharedNpcs().contains(7));
}

TEST_CASE("a guest that missed a scene's end is told by the resync that none plays") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
  REQUIRE((host && a));
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t, const auto &) { return a->IsJoined(); }));
  host->SendNpcSpawn({.npc = 7, .record = 1});
  scene::Stage stage;
  for (const auto &message : stage.Update(Talk(), host->HostNowUs()))
    host->SendStory(message);
  Watcher atA;
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1)
      atA.Take(*a, events);
    return atA.replay.Cast().size() == 2;
  }));
  atA.cues.clear();

  // The end is lost on the way (a bug, a full queue): the host's record has
  // no scene, the guest's still does. What the digests then ask for, the
  // story again, says that no scene plays.
  host->SetFaults({.drop = 1.f});
  for (const auto &message : stage.Update({}, host->HostNowUs()))
    host->SendStory(message);
  Pump({host.get(), a.get()}, [](std::size_t, const auto &) { return false; }, 100ms);
  host->SetFaults({});
  CHECK(atA.replay.On());
  REQUIRE(Pump(
      {host.get(), a.get()},
      [&](std::size_t i, const auto &events) {
        if (i == 1)
          atA.Take(*a, events);
        return !atA.replay.On();
      },
      15s));
  CHECK(Count(atA.cues, Kind::ActorGone) == 2);
  CHECK(Count(atA.cues, Kind::End) == 1);
}
