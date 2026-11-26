// The observer's platform-independent parts (core/include/tandem/observer.hpp,
// docs/architecture/observer.md): which log lines trigger a capture, the
// log ring, the rate limit, the story trail, the summary.

#include "tandem/observer.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace tandem;
using observer::Trigger;

namespace {
std::optional<Trigger> KindOf(char level, std::string_view line) {
  const auto hit = observer::Classify(level, line);
  return hit ? std::optional(hit->kind) : std::nullopt;
}
} // namespace

TEST_CASE("observer: the registry's lines, respawns, one-sided copies and errors trigger; the "
          "rest does not") {
  CHECK(KindOf('W', "degraded: vehicle move is off: the vehicle layout check failed") ==
        Trigger::Degraded);
  CHECK(KindOf('E', "degraded: quest nodes is OFF, co-op cannot run without it: layout") ==
        Trigger::Degraded);
  CHECK(KindOf('W', "degraded: player 2's game runs without: looks") == Trigger::Degraded);
  CHECK(KindOf('W', "degraded: fallback prompt drawn: vendor: no prompt for 3 s") ==
        Trigger::Fallback);
  CHECK(KindOf('D', "degraded: fallback proxy respawned x3: npc 12 lost its body") ==
        Trigger::Respawn);
  CHECK(KindOf('W', "degraded: fallback street body respawned: car 4") == Trigger::Respawn);
  CHECK(KindOf('I', "resync: asking the host for npcs (our copy differed from its digest)") ==
        Trigger::OneSided);
  CHECK(KindOf('I', "[script] consistency: proxy 7 was missing, spawned from the session's "
                    "record") == Trigger::OneSided);
  CHECK(KindOf('I', "[script] street: check: vehicle 3 is no longer shared: proxy removed") ==
        Trigger::OneSided);
  CHECK(KindOf('E', "quest: no quests system: 4 held signals dropped") == Trigger::Error);
  CHECK(KindOf('I', "[script] test reset: failed (on foot, spot)") == Trigger::ScriptError);
  // A choice, an end, the observer itself and ordinary lines: nothing.
  CHECK_FALSE(KindOf('W', "degraded: quest freeze is off by choice: tandem.ini"));
  CHECK_FALSE(KindOf('I', "degraded: vehicle move is back"));
  CHECK_FALSE(KindOf('W', "session start: degraded: none"));
  CHECK_FALSE(KindOf('E', "observer: capture 3 written"));
  CHECK_FALSE(KindOf('I', "puppet 2: 300 frames, body (1.0, 2.0, 3.0) 0.10 m from target"));
  CHECK_FALSE(KindOf('W', "watchdog: game thread silent for 5 s (crashed, hung, paused or "
                          "loading)"));
  // Worse things rank higher.
  CHECK(observer::Classify('E', "degraded: x is OFF, co-op cannot run without it: y")->severity >
        observer::Classify('W', "degraded: fallback y: z")->severity);
  CHECK(observer::DriftSeverity(40.f) > observer::DriftSeverity(6.f));
  CHECK(observer::FrameSeverity(400.f) > observer::FrameSeverity(60.f));
}

TEST_CASE("observer: the frame judged at a tick") {
  // The session's first tick: the engine frames before it (the save's
  // loading screen, one of 1370 ms) are not its frame.
  CHECK(observer::FrameToJudgeUs(0, 5'000'000, 900, 1'370'000) == 0);
  // A tick after a tick: the gap between them.
  CHECK(observer::FrameToJudgeUs(1'000'000, 1'016'000, 1, 16'000) == 16'000);
  CHECK(observer::FrameToJudgeUs(1'000'000, 1'080'000, 2, 70'000) == 80'000);
  // A pause menu held the script tick over 104 engine frames: the longest
  // of them is the frame, not the pause.
  CHECK(observer::FrameToJudgeUs(1'000'000, 1'682'000, 104, 22'000) == 22'000);
  // A clock that went back judges nothing.
  CHECK(observer::FrameToJudgeUs(2'000'000, 1'000'000, 1, 0) == 0);
}

TEST_CASE("observer: every trigger kind has a name, and the name gives it back") {
  for (std::size_t i = 0; i < observer::kTriggers; ++i) {
    const auto kind = static_cast<Trigger>(i);
    CAPTURE(i);
    CHECK(observer::TriggerNamed(observer::NameOf(kind)) == kind);
  }
  CHECK_FALSE(observer::TriggerNamed("nothing"));
}

TEST_CASE("observer: the ring keeps its last seconds within its bytes") {
  observer::Ring ring(10'000'000, 1000);
  for (std::uint64_t s = 0; s < 30; ++s)
    ring.Push(s * 1'000'000, 'D', std::string(10, 'a'));
  // 10 s kept: lines from 19 s to 29 s.
  CHECK(ring.Size() == 11);
  CHECK(ring.Between(0, 100'000'000).front().us == 19'000'000);
  CHECK(ring.Between(25'000'000, 27'000'000).size() == 3);
  CHECK(ring.Squeezed() == 0);
  // The bytes cap squeezes the oldest out before their time.
  for (int i = 0; i < 200; ++i)
    ring.Push(30'000'000, 'D', std::string(10, 'b'));
  CHECK(ring.Bytes() <= 1000);
  CHECK(ring.Squeezed() > 0);
  // A line larger than the cap still stays, alone.
  ring.Push(31'000'000, 'E', std::string(5000, 'c'));
  CHECK(ring.Size() == 1);
}

TEST_CASE("observer: one capture per kind a minute, and no more than the cap a run") {
  observer::Limiter limiter(60'000'000, 3);
  using V = observer::Limiter::Verdict;
  CHECK(limiter.Ask(Trigger::Frame, 0) == V::Capture);
  CHECK(limiter.Ask(Trigger::Frame, 30'000'000) == V::TooSoon);
  CHECK(limiter.Ask(Trigger::Drift, 30'000'000) == V::Capture);
  CHECK(limiter.Ask(Trigger::Frame, 60'000'000) == V::Capture);
  CHECK(limiter.Taken() == 3);
  CHECK(limiter.Ask(Trigger::Stall, 70'000'000) == V::Capped);
  CHECK(limiter.Ask(Trigger::Frame, 200'000'000) == V::Capped);
}

TEST_CASE("observer: the trail shows what changed in the story, once") {
  observer::Trail trail;
  auto rows = trail.Note(proto::Message(proto::Tracked{.hash = 0x1234}), 1);
  REQUIRE(rows.size() == 1);
  CHECK(rows[0].kind == "tracked");
  CHECK(rows[0].text == "0x1234");
  CHECK(trail.Note(proto::Message(proto::Tracked{.hash = 0x1234}), 2).empty());

  rows = trail.Note(proto::Message(proto::Journal{
                        .entries = {{.hash = 0xab, .state = 2}, {.hash = 0xcd, .state = 3}}}),
                    3);
  REQUIRE(rows.size() == 1);
  CHECK(rows[0].text == "0xab active, 0xcd succeeded");
  CHECK(trail.Quests() == std::vector<std::uint32_t>{0xab});

  // A scene: its start, its cast, its end.
  rows = trail.Note(proto::Message(proto::SceneState{.scene = 0x99, .play = 1, .tier = 3}), 4);
  REQUIRE(rows.size() == 1);
  CHECK(rows[0].text == "start 0x99 play 1 tier 3");
  CHECK(trail.Note(proto::Message(proto::SceneState{.scene = 0x99, .play = 1, .section = 7}), 5)
            .empty());
  rows = trail.Note(
      proto::Message(proto::SceneCast{.play = 1, .actors = {{.npc = 4}, {.npc = 9, .flags = 1}}}),
      6);
  REQUIRE(rows.size() == 1);
  CHECK(rows[0].text == "play 1: npc 4, npc 9 (ghost)");
  CHECK(trail.Now().find("scene 0x99 (play 1, 2 actors)") != std::string::npos);
  rows = trail.Note(
      proto::Message(proto::SceneState{.scene = 0, .play = 1, .flags = proto::kSceneInterrupted}),
      7);
  REQUIRE(rows.size() == 1);
  CHECK(rows[0].text == "end of play 1 (interrupted)");
  CHECK(trail.Scenes() == std::vector<std::uint64_t>{0x99});

  // Fast travel locked and opened again; the tier on its own row.
  rows = trail.Note(proto::Message(proto::StoryState{.tier = 1}), 8);
  REQUIRE(rows.size() == 1);
  CHECK(rows[0].kind == "tier");
  rows = trail.Note(
      proto::Message(proto::StoryState{.tier = 1, .flags = proto::kStoryNoFastTravel}), 9);
  REQUIRE(rows.size() == 1);
  CHECK(rows[0].kind == "fast-travel");
  CHECK(rows[0].text == "ours: locked");
  rows = trail.Note(proto::Message(proto::StoryState{.tier = 1, .flags = proto::kStoryInCall}), 10);
  REQUIRE(rows.size() == 2);
  CHECK(rows[0].text == "ours: open");
  CHECK(rows[1].text == "ours: call");
  // Another player's is theirs.
  rows = trail.Note(proto::Message(proto::StoryState{.player = 3, .tier = 2}), 11, 3);
  REQUIRE(rows.size() == 1);
  CHECK(rows[0].text == "player 3: tier 2");

  // Restrictions and prefabs as what came and went.
  rows = trail.Note(proto::Message(proto::StoryRestrictions{.effects = {1, 2}}), 12);
  REQUIRE(rows.size() == 1);
  CHECK(rows[0].text == "2 now +0x1, 0x2");
  rows = trail.Note(proto::Message(proto::StoryRestrictions{.effects = {2, 3}}), 13);
  CHECK(rows[0].text == "2 now +0x3 -0x1");
  CHECK(trail.Note(proto::Message(proto::StoryRestrictions{.effects = {2, 3}}), 14).empty());
  rows = trail.Note(proto::Message(proto::WorldPrefabs{.prefabs = {5}}), 15);
  CHECK(rows[0].kind == "prefabs");

  // Gates: ours on a change.
  rows = trail.Note(proto::Message(proto::StoryGates{.flags = proto::kGateWatsonOpen}), 16);
  REQUIRE(rows.size() == 1);
  CHECK(rows[0].text.starts_with("Watson lockdown lifted"));
  CHECK(trail.Note(proto::Message(proto::StoryGates{.flags = proto::kGateWatsonOpen}), 17).empty());

  // What the trail does not show.
  CHECK(trail.Note(proto::Message(proto::Ping{}), 18).empty());
}

TEST_CASE("observer: a phase and a district make a row the first time; a session's end "
          "forgets what is now, not what was seen") {
  observer::Trail trail;
  CHECK(trail.Phase(0x10, 1));
  CHECK_FALSE(trail.Phase(0x10, 2));
  CHECK(trail.Phase(0x11, 3));
  CHECK_FALSE(trail.Phase(0x10, 4)); // seen before
  CHECK_FALSE(trail.Phase(0, 5));
  CHECK(trail.Phases() == 2);
  const auto row = trail.District("Districts.Watson", 6);
  REQUIRE(row);
  CHECK(row->text == "Districts.Watson (from none)");
  CHECK_FALSE(trail.District("Districts.Watson", 7));
  CHECK(trail.District("Districts.Westbrook", 8));
  CHECK(trail.Districts() == std::vector<std::string>{"Districts.Watson", "Districts.Westbrook"});
  trail.Note(proto::Message(proto::Tracked{.hash = 7}), 9);
  trail.Reset();
  CHECK(trail.Now().find("tracked none") != std::string::npos);
  CHECK(trail.Districts().size() == 2);
  CHECK(trail.Phases() == 2);
  // Tracked again after the reset: a row again.
  CHECK(trail.Note(proto::Message(proto::Tracked{.hash = 7}), 10).size() == 1);
}

TEST_CASE("observer: the summary counts every trigger and ranks the worst moments") {
  observer::Tally tally;
  tally.Add({.us = 1, .kind = Trigger::Frame, .severity = 25, .detail = "frame 60 ms"});
  tally.Add({.us = 2, .kind = Trigger::Stall, .severity = 95, .detail = "silent", .capture = "c1"});
  tally.Add({.us = 3, .kind = Trigger::Frame, .severity = 25, .detail = "frame 61 ms"});
  for (int i = 0; i < 20; ++i)
    tally.Add({.us = 10u + i, .kind = Trigger::Fallback, .severity = 30, .detail = "f"});
  CHECK(tally.Count(Trigger::Frame) == 2);
  CHECK(tally.Count(Trigger::Fallback) == 20);
  const auto worst = tally.Worst(3);
  REQUIRE(worst.size() == 3);
  CHECK(worst[0].kind == Trigger::Stall);
  CHECK(worst[1].kind == Trigger::Fallback);
  CHECK(worst[1].us == 10); // equal ones: the earliest first
  CHECK(tally.Worst(100).size() == observer::Tally::kKeep);

  observer::Trail trail;
  trail.District("Districts.Watson", 1);
  const auto text =
      tally.Summary("run", trail, 1, [](std::uint64_t us) { return std::to_string(us); });
  CHECK(text.find("== run ==") == 0);
  CHECK(text.find("triggers: 23 (stall 1, fallback 20, frame 2); captures written: 1") !=
        std::string::npos);
  CHECK(text.find("  1. 2 stall (95): silent [c1]") != std::string::npos);
  CHECK(text.find("districts: Districts.Watson") != std::string::npos);
}

TEST_CASE("observer: a TSV field holds no tab or line end") {
  CHECK(observer::Field("a\tb\nc\rd") == "a b c d");
  CHECK(std::string(observer::JournalStateName(3)) == "succeeded");
  CHECK(std::string(observer::JournalStateName(9)) == "?");
}
