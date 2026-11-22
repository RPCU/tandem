// Progression rules (core/include/tandem/progress.hpp): the session's level
// and the NPC modifiers for it, a guest's way out of a session to its own
// saves, and the levels over a real session (loopback UDP).

#include "ports.hpp"
#include "slowdown.hpp"
#include "tandem/endpoint.hpp"
#include "tandem/progress.hpp"
#include "tandem/vehicle.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <functional>

using namespace std::chrono_literals;
using namespace tandem;
using namespace tandem::progress;

TEST_CASE("the session's level is the mean of its players' levels, rounded") {
  CHECK(MeanLevel({}) == 0);
  CHECK(MeanLevel(std::vector<std::uint8_t>{37}) == 37);
  CHECK(MeanLevel(std::vector<std::uint8_t>{50, 10}) == 30);
  CHECK(MeanLevel(std::vector<std::uint8_t>{50, 11}) == 31);   // 30.5 goes up
  CHECK(MeanLevel(std::vector<std::uint8_t>{60, 1, 1}) == 21); // 20.67
  CHECK(MeanLevel(std::vector<std::uint8_t>{100, 100, 100, 100}) == 100);
}

TEST_CASE("level_gap keeps the session's level within reach of its lowest player") {
  CHECK(SessionLevelOf({}, 8) == 0);
  // A new V beside a veteran: never more than the gap above the new one.
  CHECK(SessionLevelOf(std::vector<std::uint8_t>{40, 1}, 8) == 9);
  CHECK(SessionLevelOf(std::vector<std::uint8_t>{40, 1}, 0) == 21); // off: the mean
  // Close levels: the mean, untouched.
  CHECK(SessionLevelOf(std::vector<std::uint8_t>{12, 8}, 8) == 10);
  CHECK(SessionLevelOf(std::vector<std::uint8_t>{60, 1, 1}, 5) == 6);
  CHECK(SessionLevelOf(std::vector<std::uint8_t>{37}, 8) == 37);
  CHECK(SessionLevelOf(std::vector<std::uint8_t>{100, 100}, kMaxLevelGap) == 100);
}

TEST_CASE("a host NPC is moved from the host's level to the session's") {
  // No curve read: by the difference of the two levels.
  CHECK(NpcShift({.level = 50.f, .hostLevel = 50.f, .sessionLevel = 30.f}) == -20.f);
  CHECK(NpcShift({.level = 12.f, .hostLevel = 10.f, .sessionLevel = 30.f}) == 20.f);
  // Alone, or everyone at the host's level: nothing.
  CHECK(NpcShift({.level = 50.f, .hostLevel = 50.f, .sessionLevel = 0.f}) == 0.f);
  CHECK(NpcShift({.level = 50.f, .hostLevel = 50.f, .sessionLevel = 50.f}) == 0.f);
  // Our modifier is already on it: the same answer, not twice.
  CHECK(NpcShift({.level = 30.f, .applied = -20.f, .hostLevel = 50.f, .sessionLevel = 30.f}) ==
        -20.f);
  // The session's level moved (a level-up): from its own level again.
  CHECK(NpcShift({.level = 30.f, .applied = -20.f, .hostLevel = 50.f, .sessionLevel = 31.f}) ==
        -19.f);
}

TEST_CASE("an NPC with a level curve follows the curve; a constant one keeps its level") {
  // A district that never goes under level 20: the curve says so.
  CHECK(NpcShift({.level = 50.f,
                  .hostLevel = 50.f,
                  .sessionLevel = 10.f,
                  .curveAtHost = 50.f,
                  .curveAtSession = 20.f}) == -30.f);
  // The curve is flat between the two: nothing to move.
  CHECK(NpcShift({.level = 45.f,
                  .hostLevel = 50.f,
                  .sessionLevel = 48.f,
                  .curveAtHost = 45.f,
                  .curveAtSession = 45.f}) == 0.f);
  CHECK(NpcShift({.level = 35.f, .hostLevel = 50.f, .sessionLevel = 30.f, .fixed = true}) == 0.f);
}

TEST_CASE("a moved NPC stays within the game's levels") {
  CHECK(NpcShift({.level = 5.f, .hostLevel = 30.f, .sessionLevel = 10.f}) == -4.f); // down to 1
  CHECK(NpcShift({.level = 58.f, .hostLevel = 20.f, .sessionLevel = 40.f}) == 2.f); // up to 60
  CHECK(NpcShift({.level = 58.f, .hostLevel = 20.f, .sessionLevel = 40.f, .maxLevel = 80.f}) ==
        20.f);
  // One the game itself put outside them is left alone.
  CHECK(NpcShift({.level = 0.f, .hostLevel = 30.f, .sessionLevel = 10.f}) == 0.f);
  CHECK(NpcShift({.level = 75.f, .hostLevel = 30.f, .sessionLevel = 40.f}) == 0.f);
}

TEST_CASE("a proxy takes the host's NPC's level, whatever our game gave it") {
  CHECK(ProxyShift(10.f, 0.f, 30.f) == 20.f);
  CHECK(ProxyShift(30.f, 20.f, 30.f) == 20.f); // already there: the same modifier
  CHECK(ProxyShift(30.f, 20.f, 25.f) == 15.f); // the host's moved
  CHECK(ProxyShift(30.f, 0.f, 30.f) == 0.f);
  CHECK(ProxyShift(10.f, 0.f, 0.f) == 0.f); // not told yet
}

namespace {
// The machine driven like the game drives it: one call a tick.
struct Leaving {
  Leave leave;
  LeaveInput in;
  explicit Leaving(LeaveMode mode) : leave(mode) {}
  LeaveAction At(double now) {
    in.now = now;
    return leave.Tick(in);
  }
  // The first action that is not None until `until`, a tick every 0.25 s.
  LeaveAction Next(double until) {
    for (double now = in.now + 0.25; now <= until; now += 0.25)
      if (const auto action = At(now); action != LeaveAction::None)
        return action;
    return LeaveAction::None;
  }
};
} // namespace

TEST_CASE("leave = stay: saving is locked in a session and unlocks once our world is back") {
  Leaving l(LeaveMode::Stay);
  CHECK_FALSE(l.leave.Locked());
  l.in.session = true;
  CHECK(l.At(1.0) == LeaveAction::None);
  CHECK(l.leave.Locked());
  // Out: not at once (bodies are still going), and never while co-op state is left.
  l.in.session = false;
  l.in.left = kLeftBodies | kLeftDifficulty;
  CHECK(l.At(10.0) == LeaveAction::None);
  CHECK(l.leave.Step() == LeaveStep::Cleaning);
  CHECK(l.Next(14.0) == LeaveAction::None);
  CHECK(l.leave.Locked());
  l.in.left = 0;
  CHECK(l.Next(15.0) == LeaveAction::Clean);
  CHECK_FALSE(l.leave.Locked());
  CHECK(l.leave.Step() == LeaveStep::Solo);
  // Nothing more: no save, no load.
  CHECK(l.Next(400.0) == LeaveAction::None);
}

TEST_CASE("a clean world still waits for the session's last events") {
  Leaving l(LeaveMode::Stay);
  l.in.session = true;
  l.At(0.0);
  l.in.session = false;
  l.At(5.0);
  CHECK(l.At(5.0 + kLeaveSettle - 0.1) == LeaveAction::None);
  CHECK(l.At(5.0 + kLeaveSettle) == LeaveAction::Clean);
}

TEST_CASE("co-op state that stays keeps saving locked for good") {
  Leaving l(LeaveMode::Reload);
  l.in.session = true;
  l.At(0.0);
  l.in.session = false;
  l.in.chosen = true;
  l.in.left = kLeftStory;
  l.At(1.0);
  CHECK(l.Next(1.0 + kLeaveCleanBy - 0.5) == LeaveAction::None);
  CHECK(l.Next(1.0 + kLeaveCleanBy + 0.5) == LeaveAction::Dirty);
  CHECK(l.leave.Locked());
  // Even if it clears later: the verdict stands until a load...
  l.in.left = 0;
  CHECK(l.Next(200.0) == LeaveAction::None);
  CHECK(l.leave.Locked());
  // ...or another session, which gets its own verdict.
  l.in.session = true;
  l.At(201.0);
  l.in.session = false;
  l.At(202.0);
  CHECK(l.Next(210.0) == LeaveAction::Clean);
  l.leave.Reset();
  CHECK_FALSE(l.leave.Locked());
}

TEST_CASE("leave = save: one save once the game allows it, no load") {
  Leaving l(LeaveMode::Save);
  l.in.session = true;
  l.At(0.0);
  l.in.session = false;
  l.in.chosen = true;
  l.in.blocked = true; // still in combat
  l.At(1.0);
  CHECK(l.Next(5.0) == LeaveAction::Clean);
  CHECK_FALSE(l.leave.Locked()); // ours is off: the game's own locks decide now
  CHECK(l.Next(30.0) == LeaveAction::None);
  l.in.blocked = false;
  CHECK(l.Next(31.0) == LeaveAction::Save);
  CHECK(l.Next(33.0) == LeaveAction::None); // being written
  l.in.saved = 1;
  CHECK(l.Next(34.0) == LeaveAction::Saved);
  CHECK(l.leave.Step() == LeaveStep::Solo);
  CHECK(l.Next(400.0) == LeaveAction::None); // once
}

TEST_CASE("leave = reload: the load only after word that the save was written") {
  Leaving l(LeaveMode::Reload);
  l.in.session = true;
  l.At(0.0);
  l.in.session = false;
  l.in.chosen = true;
  l.At(1.0);
  CHECK(l.Next(5.0) == LeaveAction::Clean);
  CHECK(l.Next(6.0) == LeaveAction::Save);
  SUBCASE("written: loaded") {
    l.in.saved = 1;
    CHECK(l.Next(7.0) == LeaveAction::Load);
    CHECK(l.leave.Step() == LeaveStep::Loading);
    // Nothing else until the world is loaded, not even a session.
    l.in.session = true;
    CHECK(l.Next(60.0) == LeaveAction::None);
    CHECK(l.leave.Step() == LeaveStep::Loading);
    l.leave.Reset();
    CHECK(l.leave.Step() == LeaveStep::Solo);
  }
  SUBCASE("failed: nothing is loaded") {
    l.in.saved = -1;
    CHECK(l.Next(7.0) == LeaveAction::SaveFailed);
    CHECK(l.Next(400.0) == LeaveAction::None);
    CHECK(l.leave.Step() == LeaveStep::Solo);
  }
  SUBCASE("no word of it: nothing is loaded") {
    CHECK(l.Next(l.in.now + kLeaveSavedBy - 1.0) == LeaveAction::None);
    CHECK(l.Next(l.in.now + 2.0) == LeaveAction::SaveFailed);
    // Word that comes after we gave up loads nothing either.
    l.in.saved = 1;
    CHECK(l.Next(400.0) == LeaveAction::None);
  }
}

TEST_CASE("a lost host is a save, never a reload; the game never allowing a save is said") {
  Leaving l(LeaveMode::Reload);
  l.in.session = true;
  l.At(0.0);
  l.in.session = false; // the host went: not our choice
  l.At(1.0);
  CHECK(l.Next(5.0) == LeaveAction::Clean);
  SUBCASE("saved, and we go on in place") {
    CHECK(l.Next(6.0) == LeaveAction::Save);
    l.in.saved = 1;
    CHECK(l.Next(7.0) == LeaveAction::Saved);
  }
  SUBCASE("back in a session before the save: no save") {
    l.in.blocked = true;
    CHECK(l.Next(8.0) == LeaveAction::None);
    l.in.session = true;
    l.in.blocked = false;
    CHECK(l.Next(60.0) == LeaveAction::None);
    CHECK(l.leave.Locked());
  }
  SUBCASE("locked by the game for two minutes: given up") {
    l.in.blocked = true;
    CHECK(l.Next(l.in.now + kLeaveSaveBy - 1.0) == LeaveAction::None);
    CHECK(l.Next(l.in.now + 2.0) == LeaveAction::GaveUp);
    l.in.blocked = false;
    CHECK(l.Next(600.0) == LeaveAction::None);
  }
  SUBCASE("something of the session comes back while waiting: no save until it is gone") {
    l.in.blocked = true;
    l.Next(6.0);
    l.in.left = kLeftBodies;
    l.in.blocked = false;
    CHECK(l.Next(8.0) == LeaveAction::None);
    CHECK(l.leave.Locked());
    l.in.left = 0;
    CHECK(l.Next(12.0) == LeaveAction::Clean);
    CHECK(l.Next(13.0) == LeaveAction::Save);
  }
}

TEST_CASE("the pause menu's Exit saves whatever the mode, and never loads") {
  for (const auto mode : {LeaveMode::Stay, LeaveMode::Save, LeaveMode::Reload}) {
    CAPTURE(NameOf(mode));
    Leaving l(mode);
    l.in.session = true;
    l.At(0.0);
    CHECK_FALSE(l.leave.Owed()); // nothing owed while in it
    l.in.session = false;
    l.in.chosen = true;
    l.in.save = true;
    l.in.left = kLeftBodies;
    l.At(1.0);
    CHECK(l.leave.Owed());
    // Never while co-op state is left, as any leave.
    CHECK(l.Next(4.0) == LeaveAction::None);
    CHECK(l.leave.Locked());
    l.in.left = 0;
    CHECK(l.Next(5.0) == LeaveAction::Clean);
    CHECK(l.Next(6.0) == LeaveAction::Save);
    l.in.saved = 1;
    CHECK(l.Next(7.0) == LeaveAction::Saved);
    CHECK(l.leave.Step() == LeaveStep::Solo);
    CHECK_FALSE(l.leave.Owed());
  }
}

TEST_CASE("an exit the game keeps locked gives up soon, and the save stays owed") {
  Leaving l(LeaveMode::Stay);
  l.in.session = true;
  l.At(0.0);
  l.in.session = false;
  l.in.save = true;
  l.in.blocked = true; // a fight the precheck did not see
  l.At(1.0);
  CHECK(l.Next(5.0) == LeaveAction::Clean);
  const double since = l.in.now;
  CHECK(l.Next(since + kLeaveSaveSoonBy - 0.5) == LeaveAction::None);
  CHECK(l.Next(since + kLeaveSaveSoonBy + 0.5) == LeaveAction::GaveUp);
  CHECK(l.leave.Owed());
  // Exit again later, the game unlocked: the owed save, then the exit.
  l.in.blocked = false;
  l.in.save = false;
  CHECK(l.Next(100.0) == LeaveAction::None);
  l.in.save = true;
  CHECK(l.Next(101.0) == LeaveAction::Save);
  l.in.saved = 1;
  CHECK(l.Next(102.0) == LeaveAction::Saved);
  CHECK_FALSE(l.leave.Owed());
}

TEST_CASE("leave = stay owes the session's save until one is written; a load clears it") {
  Leaving l(LeaveMode::Stay);
  // Solo from the start: nothing owed, an exit asks for nothing.
  l.in.save = true;
  CHECK(l.Next(2.0) == LeaveAction::None);
  CHECK(l.leave.Step() == LeaveStep::Solo);
  l.in.save = false;
  l.in.session = true;
  l.At(3.0);
  l.in.session = false; // the host went
  l.At(4.0);
  CHECK(l.Next(8.0) == LeaveAction::Clean);
  CHECK(l.leave.Step() == LeaveStep::Solo);
  CHECK(l.leave.Owed());
  CHECK(l.Next(300.0) == LeaveAction::None); // stay writes nothing by itself
  SUBCASE("the exit's save") {
    l.in.save = true;
    CHECK(l.Next(301.0) == LeaveAction::Save);
    SUBCASE("written") {
      l.in.saved = 1;
      CHECK(l.Next(302.0) == LeaveAction::Saved);
      CHECK_FALSE(l.leave.Owed());
    }
    SUBCASE("failed: still owed") {
      l.in.saved = -1;
      CHECK(l.Next(302.0) == LeaveAction::SaveFailed);
      CHECK(l.leave.Owed());
    }
  }
  SUBCASE("a world loaded") {
    l.leave.Reset();
    CHECK_FALSE(l.leave.Owed());
    l.in.save = true;
    CHECK(l.Next(310.0) == LeaveAction::None);
  }
}

TEST_CASE("an exit asked while leave = save waits switches to the shorter wait") {
  Leaving l(LeaveMode::Save);
  l.in.session = true;
  l.At(0.0);
  l.in.session = false;
  l.in.blocked = true;
  l.At(1.0);
  CHECK(l.Next(5.0) == LeaveAction::Clean);
  CHECK(l.Next(60.0) == LeaveAction::None); // the two minutes' wait
  l.in.save = true;
  CHECK(l.Next(60.0 + kLeaveSaveSoonBy - 0.5) == LeaveAction::None);
  CHECK(l.Next(60.0 + kLeaveSaveSoonBy + 0.5) == LeaveAction::GaveUp);
}

TEST_CASE("a checkpoint's save is dropped when the session is back first") {
  Leaving l(LeaveMode::Stay);
  l.in.session = true;
  l.At(0.0);
  l.in.session = false;
  l.in.save = true;
  l.in.blocked = true;
  l.At(1.0);
  CHECK(l.Next(5.0) == LeaveAction::Clean);
  l.in.session = true;
  l.in.save = false;
  CHECK(l.Next(8.0) == LeaveAction::None);
  CHECK(l.leave.Step() == LeaveStep::Session);
  // Out again without asking: stay's own way, no save.
  l.in.session = false;
  l.in.blocked = false;
  l.At(9.0);
  CHECK(l.Next(14.0) == LeaveAction::Clean);
  CHECK(l.Next(60.0) == LeaveAction::None);
}

TEST_CASE("what is left is named") {
  CHECK(LeftNames(0).empty());
  CHECK(LeftNames(kLeftStory) == "story");
  CHECK(LeftNames(kLeftDifficulty | kLeftLevels) == "difficulty, levels");
  CHECK(LeftNames(kLeftSession | 1024) == "session state, other");
  CHECK(std::string_view(NameOf(LeaveStep::Dirty)) == "dirty");
  CHECK(std::string_view(NameOf(LeaveAction::GaveUp)) == "gave up");
  CHECK(std::string_view(NameOf(LeaveMode::Reload)) == "reload");
}

// ---- over a session ----

namespace {
using test::NextPort;

bool Pump(std::vector<Endpoint *> endpoints,
          const std::function<bool(std::size_t, const std::vector<SessionEvent> &)> &done,
          std::chrono::milliseconds limit = 3s) {
  const auto deadline = std::chrono::steady_clock::now() + test::Patience(limit);
  bool finished = false;
  while (!finished && std::chrono::steady_clock::now() < deadline)
    for (std::size_t i = 0; i < endpoints.size(); ++i)
      finished = done(i, endpoints[i]->Update(1ms)) || finished;
  return finished;
}

void PumpFor(std::vector<Endpoint *> endpoints, std::chrono::milliseconds time,
             const std::function<void(std::size_t, const std::vector<SessionEvent> &)> &each) {
  const auto deadline = std::chrono::steady_clock::now() + time;
  while (std::chrono::steady_clock::now() < deadline)
    for (std::size_t i = 0; i < endpoints.size(); ++i)
      each(i, endpoints[i]->Update(1ms));
}

// What each game's scripts hear about levels.
struct Heard {
  std::map<std::size_t, std::vector<proto::Message>> at;
  void operator()(std::size_t i, const std::vector<SessionEvent> &events) {
    for (const auto &event : events)
      if (const auto *combat = std::get_if<events::Combat>(&event))
        at[i].push_back(combat->message);
  }
  std::vector<proto::SessionLevel> Sessions(std::size_t i) const {
    std::vector<proto::SessionLevel> out;
    if (const auto it = at.find(i); it != at.end())
      for (const auto &message : it->second)
        if (const auto *level = std::get_if<proto::SessionLevel>(&message))
          out.push_back(*level);
    return out;
  }
  std::vector<proto::NpcLevel> Npcs(std::size_t i) const {
    std::vector<proto::NpcLevel> out;
    if (const auto it = at.find(i); it != at.end())
      for (const auto &message : it->second)
        if (const auto *level = std::get_if<proto::NpcLevel>(&message))
          out.push_back(*level);
    return out;
  }
};

constexpr Timing kFast{.timeout = 1000ms, .takeover = 400ms};
} // namespace

TEST_CASE("the session's level follows who is in it: joins, level-ups, leaves, vanishes") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast);
  REQUIRE_MESSAGE(host, error);
  Heard heard;
  // The host alone: its own level.
  CHECK(host->SendCombat(proto::PlayerLevel{.player = 9, .level = 50}));
  PumpFor({host.get()}, 50ms, std::ref(heard));
  REQUIRE(host->SessionLevel());
  CHECK(*host->SessionLevel() == proto::SessionLevel{.level = 50, .players = 1});
  REQUIRE(heard.Sessions(0).size() == 1);

  // A guest that knew its level before it was in: said as it joins.
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
  REQUIRE_MESSAGE(a, error);
  CHECK_FALSE(a->SendCombat(proto::PlayerLevel{.level = 10})); // kept, not sent yet
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return a->SessionLevel() && a->SessionLevel()->players == 2;
  }));
  CHECK(*a->SessionLevel() == proto::SessionLevel{.level = 30, .players = 2});
  CHECK(*host->SessionLevel() == proto::SessionLevel{.level = 30, .players = 2});
  CHECK(heard.Sessions(0).back().level == 30); // the host's scripts scale to it

  // A second guest; it levels up; nobody but the host hears whose level it is.
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error, kFast);
  REQUIRE_MESSAGE(b, error);
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return b->IsJoined() && b->SessionLevel().has_value();
  }));
  // Late as it was, it got the session's level without having said its own.
  CHECK(b->SessionLevel()->level == 30);
  CHECK(b->SendCombat(proto::PlayerLevel{.level = 60}));
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return a->SessionLevel()->players == 3 && b->SessionLevel()->players == 3;
  }));
  CHECK(a->SessionLevel()->level == 40);                 // (50 + 10 + 60) / 3
  CHECK(b->SendCombat(proto::PlayerLevel{.level = 60})); // again: nothing new
  CHECK(a->SendCombat(proto::PlayerLevel{.level = 13}));
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return b->SessionLevel()->level == 41;
  }));
  for (const std::size_t game : {0u, 1u, 2u})
    for (const auto &message : heard.at[game])
      CHECK_FALSE(std::holds_alternative<proto::PlayerLevel>(message));
  // Not a game's to say, and nothing out of range.
  CHECK_FALSE(host->SendCombat(proto::SessionLevel{.level = 5, .players = 1}));
  CHECK_FALSE(a->SendCombat(proto::SessionLevel{.level = 5, .players = 1}));
  CHECK_FALSE(a->SendCombat(proto::PlayerLevel{.level = 0}));
  CHECK_FALSE(a->SendCombat(proto::PlayerLevel{.level = 101}));

  // b leaves; a vanishes without a goodbye: the mean is of those left.
  b->Leave();
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return a->SessionLevel()->players == 2;
  }));
  CHECK(a->SessionLevel()->level == 32); // (50 + 13) / 2 = 31.5
  a.reset();
  REQUIRE(Pump({host.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return host->SessionLevel()->players == 1;
  }));
  CHECK(host->SessionLevel()->level == 50);
  CHECK(heard.Sessions(0).back() == proto::SessionLevel{.level = 50, .players = 1});
}

TEST_CASE("the host's level_gap caps the session's level for a new V") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast);
  REQUIRE_MESSAGE(host, error);
  host->SetLevelGap(8);
  Heard heard;
  CHECK(host->SendCombat(proto::PlayerLevel{.player = 1, .level = 40}));
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
  REQUIRE_MESSAGE(a, error);
  CHECK_FALSE(a->SendCombat(proto::PlayerLevel{.level = 1}));
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return a->SessionLevel() && a->SessionLevel()->players == 2;
  }));
  // The mean would be 21 (40 and 1): 1 + 8.
  CHECK(*a->SessionLevel() == proto::SessionLevel{.level = 9, .players = 2});
  CHECK(heard.Sessions(0).back().level == 9);
  // The new V levels up: the cap moves with it, up to the mean.
  CHECK(a->SendCombat(proto::PlayerLevel{.level = 30}));
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return a->SessionLevel()->level == 35;
  }));
}

TEST_CASE("an NPC's level reaches every guest once, late joiners too, and goes with the NPC") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  REQUIRE((host && a));
  Heard heard;
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return a->IsJoined() && host->Players().size() == 2;
  }));
  // Of an NPC the host does not share yet: not sent now, said with its
  // spawn (the game's two queues may deliver them either way round).
  CHECK_FALSE(host->SendCombat(proto::NpcLevel{.npc = 7, .level = 30.f}));
  CHECK_FALSE(host->SendCombat(proto::NpcLevel{.npc = 99, .level = 5.f}));
  host->SendNpcSpawn({.npc = 7, .record = 1});
  host->SendNpcSpawn({.npc = 8, .record = 1});
  CHECK(host->NpcLevels().contains(7));
  CHECK_FALSE(host->NpcLevels().contains(99));
  CHECK(host->SendCombat(proto::NpcLevel{.npc = 7, .level = 30.f}));
  CHECK(host->SendCombat(proto::NpcLevel{.npc = 7, .level = 30.f})); // the same: said once
  CHECK(host->SendCombat(proto::NpcLevel{.npc = 8, .level = 12.f}));
  // A guest does not speak for the host's NPCs.
  CHECK_FALSE(a->SendCombat(proto::NpcLevel{.npc = 7, .level = 1.f}));
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return a->NpcLevels().size() == 2;
  }));
  PumpFor({host.get(), a.get()}, 100ms, std::ref(heard));
  CHECK(heard.Npcs(1).size() == 2);
  CHECK(a->NpcLevels().at(7).level == 30.f);
  CHECK(heard.Npcs(0).empty()); // the host's own

  // It changes (the session's level moved): said again.
  CHECK(host->SendCombat(proto::NpcLevel{.npc = 7, .level = 35.f}));
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return a->NpcLevels().at(7).level == 35.f;
  }));
  // NPC 8 goes: a late joiner hears of 7 only.
  host->SendNpcGone({.npc = 8, .reason = 0});
  auto late = Endpoint::Join({"late"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(late, error);
  REQUIRE(Pump({host.get(), a.get(), late.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return late->IsJoined() && !late->NpcLevels().empty();
  }));
  PumpFor({host.get(), a.get(), late.get()}, 200ms, std::ref(heard));
  REQUIRE(late->NpcLevels().size() == 1);
  CHECK(late->NpcLevels().at(7).level == 35.f);
  CHECK(a->NpcLevels().size() == 1);
  CHECK(heard.Npcs(2).size() == 1);
}

TEST_CASE("a vehicle unlock goes to the guest it is for, never to all, never from a guest") {
  using namespace proto;
  const VehicleUnlock unlock{.vehicle = 0xCA7, .quest = 100};
  const auto decoded = Decode(Encode(unlock));
  REQUIRE(decoded.has_value());
  CHECK(std::get<VehicleUnlock>(*decoded) == unlock);
  CHECK(IsStory(unlock));
  CHECK_FALSE(IsTrade(unlock));
  CHECK(ChannelOf(unlock) == Channel::Event);
  CHECK(consistency::IsOneShot(unlock));
  CHECK(Message(VehicleUnlock{}).index() == Message(TradeCommit{}).index() + 1); // appended
  CHECK(std::string_view(NameOf(unlock)) == "VehicleUnlock");

  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error);
  REQUIRE((host && a && b));
  std::map<std::size_t, std::vector<VehicleUnlock>> heard;
  const auto collect = [&](std::size_t i, const std::vector<SessionEvent> &events) {
    for (const auto &event : events)
      if (const auto *story = std::get_if<events::Story>(&event))
        if (const auto *v = std::get_if<VehicleUnlock>(&story->message))
          heard[i].push_back(*v);
  };
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    collect(i, events);
    return a->Players().size() == 3 && b->Players().size() == 3;
  }));
  // To a (it took part); not to everyone; a guest cannot send one.
  host->SendStory(unlock, a->You());
  host->SendStory(VehicleUnlock{.vehicle = 0xBAD}); // no one named: nobody
  a->SendStory(VehicleUnlock{.vehicle = 0xBAD}, b->You());
  PumpFor({host.get(), a.get(), b.get()}, 300ms, collect);
  REQUIRE(heard[1].size() == 1);
  CHECK(heard[1][0] == unlock);
  CHECK(heard[0].empty());
  CHECK(heard[2].empty());
  // A late joiner is not told of a vehicle it did not earn.
  auto late = Endpoint::Join({"late"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(late, error);
  PumpFor({host.get(), a.get(), b.get(), late.get()}, 500ms, collect);
  CHECK(heard[3].empty());
  CHECK(heard[1].size() == 1);
}

TEST_CASE("a partner may drive a car by its owner's word: crew, with the owner, never") {
  using proto::Lend;
  using sync::DriveAsk;
  using sync::MayDrive;
  // Crew (the default): aboard or not, while the owner is in the session.
  CHECK(MayDrive({.lend = Lend::Crew, .ownerAboard = true, .ownerInSession = true}));
  CHECK(MayDrive({.lend = Lend::Crew, .ownerAboard = false, .ownerInSession = true}));
  CHECK(MayDrive(DriveAsk{.ownerInSession = true}));
  // With the owner: only with them aboard.
  CHECK(MayDrive({.lend = Lend::WithOwner, .ownerAboard = true, .ownerInSession = true}));
  CHECK_FALSE(MayDrive({.lend = Lend::WithOwner, .ownerAboard = false, .ownerInSession = true}));
  // Never, and a car whose owner left (no one's): nobody.
  CHECK_FALSE(MayDrive({.lend = Lend::Never, .ownerAboard = true, .ownerInSession = true}));
  CHECK_FALSE(MayDrive({.lend = Lend::Crew, .ownerAboard = false, .ownerInSession = false}));
  CHECK_FALSE(MayDrive({.lend = Lend::Crew, .ownerAboard = true, .ownerInSession = false}));
}

TEST_CASE("each player's lending reaches everyone once, late joiners too, and goes with them") {
  using namespace proto;
  const CarLend never{.player = 3, .lend = Lend::Never};
  const auto decoded = Decode(Encode(never));
  REQUIRE(decoded.has_value());
  CHECK(std::get<CarLend>(*decoded) == never);
  CHECK(IsCombat(never));
  CHECK_FALSE(IsStory(never));
  CHECK(ChannelOf(never) == Channel::Event);
  CHECK_FALSE(consistency::IsOneShot(never)); // state: a repeat is applied nowhere
  CHECK(Message(CarLend{}).index() == Message(VehicleUnlock{}).index() + 1); // appended
  CHECK(std::string_view(NameOf(never)) == "CarLend");
  // Out of range: refused on the wire and by the endpoint.
  CHECK_FALSE(Decode(Encode(CarLend{.lend = static_cast<Lend>(7)})).has_value());
  CHECK(Decode(Encode(CarLend{.lend = Lend::Never})).has_value());

  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast);
  REQUIRE_MESSAGE(host, error);
  CHECK_FALSE(host->SendCombat(CarLend{.lend = static_cast<Lend>(9)}));
  CHECK(host->SendCombat(CarLend{.player = 77, .lend = Lend::WithOwner})); // who: stamped
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
  REQUIRE_MESSAGE(a, error);
  // Said before it is in: kept, said as it joins.
  CHECK_FALSE(a->SendCombat(CarLend{.player = 1, .lend = Lend::Never}));
  Heard heard;
  const auto lends = [&](std::size_t game) {
    std::vector<CarLend> out;
    for (const auto &message : heard.at[game])
      if (const auto *lend = std::get_if<CarLend>(&message))
        out.push_back(*lend);
    return out;
  };
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return host->CarLends().size() == 2 && a->CarLends().size() == 1;
  }));
  CHECK(host->CarLends().at(host->You()).lend == Lend::WithOwner);
  CHECK(host->CarLends().at(a->You()).lend == Lend::Never); // a's own id, not 1
  CHECK(a->CarLends().at(host->You()).lend == Lend::WithOwner);
  CHECK_FALSE(a->CarLends().contains(a->You()));
  // Said again: nothing new anywhere.
  CHECK(a->SendCombat(CarLend{.lend = Lend::Never}));
  CHECK(host->SendCombat(CarLend{.lend = Lend::WithOwner}));
  PumpFor({host.get(), a.get()}, 150ms, std::ref(heard));
  CHECK(lends(0).size() == 1); // the host's game heard a's once
  CHECK(lends(1).size() == 1); // a's game heard the host's once

  // A late joiner hears both; a changes its mind; a vanishes.
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error, kFast);
  REQUIRE_MESSAGE(b, error);
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return b->CarLends().size() == 2;
  }));
  CHECK(a->SendCombat(CarLend{.lend = Lend::Crew}));
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return b->CarLends().at(a->You()).lend == Lend::Crew;
  }));
  const auto aId = a->You();
  a.reset();
  REQUIRE(Pump({host.get(), b.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return !host->CarLends().contains(aId) && !b->CarLends().contains(aId);
  }));
  CHECK(b->CarLends().size() == 1);
}

TEST_CASE("a car's custom paint reaches everyone once, late joiners too, and goes with its owner") {
  using namespace proto;
  const CarPaint red{.vehicle = 9, .flags = kPaintPrimary, .primaryR = 255};
  const CarPaint blue{.vehicle = 9, .flags = kPaintPrimary, .primaryB = 255};
  CHECK_FALSE(consistency::IsOneShot(red)); // state: a repeat is applied nowhere

  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast);
  REQUIRE_MESSAGE(host, error);
  // What the wire would refuse is not said; stock paint is kept nowhere.
  CHECK_FALSE(host->SendCombat(CarPaint{.vehicle = 9, .flags = kPaintSecondary}));
  CHECK(host->SendCombat(CarPaint{.vehicle = 9}));
  CHECK(host->CarPaints().empty());
  CHECK(host->SendCombat(CarPaint{.player = 77, .vehicle = 9, .flags = kPaintLights})); // stamped
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
  REQUIRE_MESSAGE(a, error);
  // Said before it is in: kept, said as it joins.
  CHECK_FALSE(a->SendCombat(red));
  Heard heard;
  const auto paints = [&](std::size_t game) {
    std::vector<CarPaint> out;
    for (const auto &message : heard.at[game])
      if (const auto *paint = std::get_if<CarPaint>(&message))
        out.push_back(*paint);
    return out;
  };
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return host->CarPaints().size() == 2 && a->CarPaints().size() == 1;
  }));
  CHECK(host->CarPaints().at(host->You()).flags == kPaintLights);
  CHECK(host->CarPaints().at(a->You()).primaryR == 255); // a's own id
  CHECK(a->CarPaints().at(host->You()).flags == kPaintLights);
  CHECK_FALSE(a->CarPaints().contains(a->You()));
  // Said again: nothing new anywhere.
  CHECK(a->SendCombat(red));
  CHECK(host->SendCombat(CarPaint{.vehicle = 9, .flags = kPaintLights}));
  PumpFor({host.get(), a.get()}, 150ms, std::ref(heard));
  CHECK(paints(0).size() == 1); // the host's game heard a's once
  CHECK(paints(1).size() == 1); // a's game heard the host's once

  // A late joiner hears both; a repaints; the host goes back to stock paint
  // (said to everyone, kept nowhere); a vanishes.
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error, kFast);
  REQUIRE_MESSAGE(b, error);
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return b->CarPaints().size() == 2;
  }));
  CHECK(a->SendCombat(blue));
  CHECK(host->SendCombat(CarPaint{.vehicle = 9}));
  const auto hostId = host->You();
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return b->CarPaints().size() == 1 && b->CarPaints().contains(a->You()) &&
           b->CarPaints().at(a->You()).primaryB == 255 && !a->CarPaints().contains(hostId);
  }));
  CHECK(host->CarPaints().size() == 1);
  // Stock paint is said as nothing but whose car it is.
  CHECK(std::ranges::count(paints(2), CarPaint{.player = hostId}) == 1);
  const auto aId = a->You();
  a.reset();
  REQUIRE(Pump({host.get(), b.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return !host->CarPaints().contains(aId) && !b->CarPaints().contains(aId);
  }));
  CHECK(b->CarPaints().empty());
}

TEST_CASE("what a game runs without reaches everyone once, late joiners too, and goes with it") {
  using namespace proto;
  const Degraded looks{.entries = {{.id = "looks"}}};
  const Degraded both{
      .entries = {{.id = "looks"}, {.id = "vehicle move", .flags = kDegradedChosen}}};
  CHECK_FALSE(consistency::IsOneShot(looks)); // state: a repeat is applied nowhere

  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast);
  REQUIRE_MESSAGE(host, error);
  // What the wire would refuse is not said; nothing off is kept nowhere.
  CHECK_FALSE(host->SendCombat(Degraded{.entries = {{.id = "Looks"}}}));
  CHECK(host->SendCombat(Degraded{}));
  CHECK(host->Degradations().empty());
  CHECK(host->SendCombat(Degraded{.player = 77, .entries = {{.id = "workspots"}}})); // stamped
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
  REQUIRE_MESSAGE(a, error);
  // Said before it is in: kept, said as it joins.
  CHECK_FALSE(a->SendCombat(looks));
  Heard heard;
  const auto said = [&](std::size_t game) {
    std::vector<Degraded> out;
    for (const auto &message : heard.at[game])
      if (const auto *degraded = std::get_if<Degraded>(&message))
        out.push_back(*degraded);
    return out;
  };
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return host->Degradations().size() == 2 && a->Degradations().size() == 1;
  }));
  CHECK(host->Degradations().at(host->You()).entries.front().id == "workspots");
  CHECK(host->Degradations().at(a->You()).entries == looks.entries); // under a's own id
  CHECK(a->Degradations().at(host->You()).entries.front().id == "workspots");
  CHECK_FALSE(a->Degradations().contains(a->You()));
  // Said again: nothing new anywhere.
  CHECK(a->SendCombat(looks));
  CHECK(host->SendCombat(Degraded{.entries = {{.id = "workspots"}}}));
  PumpFor({host.get(), a.get()}, 150ms, std::ref(heard));
  CHECK(said(0).size() == 1); // the host's game heard a's once
  CHECK(said(1).size() == 1); // a's game heard the host's once

  // A late joiner hears both; a's list grows; the host's empties (said to
  // everyone, kept nowhere); a vanishes.
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error, kFast);
  REQUIRE_MESSAGE(b, error);
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return b->Degradations().size() == 2;
  }));
  CHECK(a->SendCombat(both));
  CHECK(host->SendCombat(Degraded{}));
  const auto hostId = host->You();
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return b->Degradations().size() == 1 && b->Degradations().contains(a->You()) &&
           b->Degradations().at(a->You()).entries == both.entries &&
           !a->Degradations().contains(hostId);
  }));
  CHECK(host->Degradations().size() == 1);
  // Nothing off is said as nothing but whose game it is.
  CHECK(std::ranges::count(said(2), Degraded{.player = hostId}) == 1);
  const auto aId = a->You();
  a.reset();
  REQUIRE(Pump({host.get(), b.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return !host->Degradations().contains(aId) && !b->Degradations().contains(aId);
  }));
  CHECK(b->Degradations().empty());
}
