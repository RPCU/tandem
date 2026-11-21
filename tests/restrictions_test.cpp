// Story restrictions without the game (docs/architecture/story.md,
// "Restrictions"): who shares the host's story moment, what a guest puts on
// its own V and takes off (never what its own game put on), and the host's
// set through real endpoints over loopback UDP: a guest in the moment, a
// late joiner, a lift, a lost lift healed by the digests, a host that
// vanishes.

#include "ports.hpp"
#include "slowdown.hpp"
#include "tandem/endpoint.hpp"
#include "tandem/story.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <functional>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
using test::NextPort;

constexpr Timing kFast{.timeout = 1000ms, .takeover = 400ms};
constexpr std::uint64_t kNoWeapons = 0x1001, kNoSprint = 0x1002, kNoJump = 0x1003,
                        kNoCombat = 0x1004;

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

story::MomentInput Near(float metres) {
  return {.grouped = true,
          .hostKnown = true,
          .host = {0.f, 0.f, 0.f},
          .guest = {metres, 0.f, 0.f},
          .tier = 3};
}

// A guest's game as the script drives it: the host's set from the session,
// what its V has, the restrictions it applies while in the moment.
struct Guest {
  story::Restrictions restrictions;
  std::set<std::uint64_t> v; // what our V has now
  std::vector<std::uint64_t> heard;
  bool ended{};
  void Take(const std::vector<SessionEvent> &events) {
    for (const auto &event : events) {
      if (const auto *story = std::get_if<events::Story>(&event)) {
        if (const auto *set = std::get_if<proto::StoryRestrictions>(&story->message)) {
          restrictions.Host(*set);
          heard = set->effects;
        }
      } else if (std::holds_alternative<events::Ended>(event)) {
        ended = true;
        Apply(restrictions.Lift());
      }
    }
  }
  void Apply(const story::Restrictions::Change &change) {
    for (const auto effect : change.apply)
      v.insert(effect);
    for (const auto effect : change.remove)
      v.erase(effect);
  }
  void Tick(bool inMoment) { Apply(restrictions.Update(inMoment)); }
};
} // namespace

TEST_CASE("the story moment: grouped, near the host or inside its staged scene, while it lasts") {
  // Near the host in its scene: in; far: out.
  CHECK(story::InMoment(Near(10.f), false));
  CHECK(story::InMoment(Near(39.f), false));
  CHECK_FALSE(story::InMoment(Near(45.f), false));
  // Walking out: in until 50 m (no flicker at the edge).
  CHECK(story::InMoment(Near(45.f), true));
  CHECK_FALSE(story::InMoment(Near(51.f), true));

  // No scene and no restriction on the host's side: no moment.
  auto in = Near(5.f);
  in.tier = 1;
  CHECK_FALSE(story::InMoment(in, false));
  // A quest restriction on the host's V outside a scene is a moment.
  in.restricted = true;
  CHECK(story::InMoment(in, false));
  // A private scene too (tier 2): its area is no place to fire either.
  in = Near(5.f);
  in.tier = 2;
  CHECK(story::InMoment(in, false));

  // Apart (regions), in another world, the host's V unknown: never.
  in = Near(5.f);
  in.grouped = false;
  CHECK_FALSE(story::InMoment(in, false));
  in = Near(5.f);
  in.away = true;
  CHECK_FALSE(story::InMoment(in, false));
  in = Near(5.f);
  in.hostKnown = false;
  CHECK_FALSE(story::InMoment(in, true));

  // A staged scene reaches beyond the host: its actors' spread plus 12 m,
  // capped at 30 m of spread; 8 m more to leave.
  in = Near(60.f);
  in.staged = true;
  in.anchor = {55.f, 0.f, 0.f};
  in.radius = 6.f;
  CHECK(story::InMoment(in, false)); // 5 m from the middle
  in.guest = {75.f, 0.f, 0.f};       // 20 m from the middle, 75 from the host
  CHECK_FALSE(story::InMoment(in, false));
  CHECK(story::InMoment(in, true)); // 6 + 20
  in.radius = 500.f;                // a cast spread over a district
  in.guest = {55.f + 43.f, 0.f, 0.f};
  CHECK_FALSE(story::InMoment(in, false));
  in.radius = -1.f; // not staged after all
  in.guest = {56.f, 0.f, 0.f};
  CHECK_FALSE(story::InMoment(in, false));
}

TEST_CASE("a guest's restrictions: only what tandem put on comes off") {
  story::Restrictions r;
  r.Host({.player = 1, .effects = {kNoWeapons, kNoSprint}});

  // Out of the moment: nothing.
  CHECK(r.Update(false).Empty());
  // In: both on, once.
  auto change = r.Update(true);
  CHECK(change.apply == std::vector<std::uint64_t>{kNoWeapons, kNoSprint});
  CHECK(change.remove.empty());
  CHECK(r.Update(true).Empty());
  CHECK(r.OursCount() == 2);

  // The host lifts one: off; adds another: on.
  r.Host({.player = 1, .effects = {kNoWeapons, kNoJump}});
  change = r.Update(true);
  CHECK(change.apply == std::vector<std::uint64_t>{kNoJump});
  CHECK(change.remove == std::vector<std::uint64_t>{kNoSprint});

  // We walk out: every one of ours off.
  change = r.Update(false);
  CHECK(change.remove == std::vector<std::uint64_t>{kNoWeapons, kNoJump});
  CHECK(r.OursCount() == 0);

  // Our V had no-weapons of its own (a safe zone of its world): ours never
  // touches it, coming or going.
  change = r.Update(true);
  REQUIRE(change.apply.size() == 2);
  r.Theirs(kNoWeapons);
  CHECK_FALSE(r.Ours(kNoWeapons));
  CHECK(r.Holds(kNoWeapons));
  CHECK(r.Update(true).Empty()); // not applied again either
  change = r.Update(false);
  CHECK(change.remove == std::vector<std::uint64_t>{kNoJump});

  // Our own game took one off while the moment lasts: on again.
  r.Update(true);
  r.Gone(kNoJump);
  change = r.Update(true);
  CHECK(change.apply == std::vector<std::uint64_t>{kNoJump});

  // The scene tier's holds join the host's set: one effect, held once.
  change = r.Update(true, {kNoCombat, kNoWeapons});
  CHECK(change.apply == std::vector<std::uint64_t>{kNoCombat});
  change = r.Update(false, {kNoCombat});
  CHECK(change.remove == std::vector<std::uint64_t>{kNoWeapons, kNoJump});
  CHECK(r.Ours(kNoCombat));

  // The session ends: all of ours off, the host's set forgotten.
  change = r.Lift();
  CHECK(change.remove == std::vector<std::uint64_t>{kNoCombat});
  CHECK(r.HostSet().empty());
  CHECK(r.Update(true).Empty());
}

TEST_CASE("the story record keeps the host's restrictions for joiners and hashes them") {
  story::Record record;
  const auto none = record.Digest();
  CHECK(record.Note(proto::StoryRestrictions{.player = 1, .effects = {kNoWeapons}}));
  CHECK_FALSE(record.Note(proto::StoryRestrictions{.player = 1, .effects = {kNoWeapons}}));
  CHECK(record.RestrictionsOn());
  CHECK(record.Digest() != none);
  const auto joiner = record.ForJoiner();
  REQUIRE(joiner.size() == 1);
  CHECK(std::get<proto::StoryRestrictions>(joiner[0]).effects ==
        std::vector<std::uint64_t>{kNoWeapons});
  // Lifted: nothing for a joiner, the digest of none.
  CHECK(record.Note(proto::StoryRestrictions{.player = 1}));
  CHECK_FALSE(record.Note(proto::StoryRestrictions{.player = 1}));
  CHECK_FALSE(record.RestrictionsOn());
  CHECK(record.ForJoiner().empty());
  CHECK(record.Digest() == none);
}

TEST_CASE("the host's backstop refuses a guest's strikes, takedowns and quickhacks on actors") {
  using story::GuestAct;
  using story::SceneActor;
  // Who is an actor: the cast, or a quest or god-mode NPC in a scene.
  CHECK(story::IsSceneActor({.cast = true}));
  CHECK(story::IsSceneActor({.quest = true, .inScene = true}));
  CHECK(story::IsSceneActor({.kept = true, .inScene = true}));
  CHECK_FALSE(story::IsSceneActor({.quest = true}));   // a quest NPC out of the scene
  CHECK_FALSE(story::IsSceneActor({.inScene = true})); // an extra in the scene's area
  CHECK_FALSE(story::IsSceneActor({}));

  story::Backstop backstop;
  const SceneActor actor{.quest = true, .inScene = true}, extra{.inScene = true};
  // Outside the host's moment, or on someone else: the combat path's, as always.
  CHECK(backstop.Judge(false, actor, GuestAct::Quickhack, 0) == story::Backstop::Verdict{});
  CHECK(backstop.Judge(true, extra, GuestAct::Quickhack, 0) == story::Backstop::Verdict{});
  CHECK(backstop.Refused(GuestAct::Quickhack) == 0);

  // In it: every act refused; said once a second for each kind of act.
  const std::uint64_t t = 5'000'000;
  CHECK(backstop.Judge(true, actor, GuestAct::Quickhack, t) ==
        story::Backstop::Verdict{.refuse = true, .log = true});
  CHECK(backstop.Judge(true, actor, GuestAct::Quickhack, t + 300'000) ==
        story::Backstop::Verdict{.refuse = true, .log = false});
  // A strike in the same second (the quickhack's damage tick) has its own line.
  CHECK(backstop.Judge(true, actor, GuestAct::Strike, t + 400'000) ==
        story::Backstop::Verdict{.refuse = true, .log = true});
  CHECK(backstop.Judge(true, {.cast = true}, GuestAct::Takedown, t + 500'000).log);
  CHECK(backstop.Judge(true, actor, GuestAct::Quickhack, t + 1'000'000) ==
        story::Backstop::Verdict{.refuse = true, .log = true});
  CHECK(backstop.Refused(GuestAct::Quickhack) == 3);
  CHECK(backstop.Refused(GuestAct::Strike) == 1);
  CHECK(backstop.Refused(GuestAct::Takedown) == 1);
  // The very first refusal is said even at time zero.
  story::Backstop fresh;
  CHECK(fresh.Judge(true, actor, GuestAct::Strike, 0).log);
}

TEST_CASE("restrictions through the endpoints: a guest, a late joiner, a lift, a vanished host") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
  REQUIRE((host && a));
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t, const auto &) { return a->IsJoined(); }));

  // The host's scene holsters its V and keeps it walking: unsorted, a
  // repeat inside; the guest hears one sorted set, stamped with the host.
  host->SendStory(
      proto::StoryRestrictions{.player = 9, .effects = {kNoSprint, kNoWeapons, kNoSprint}});
  // A guest cannot speak for the host's restrictions.
  a->SendStory(proto::StoryRestrictions{.effects = {kNoJump}});
  Guest atA;
  std::size_t heardByHost = 0;
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1)
      atA.Take(events);
    else
      for (const auto &event : events)
        heardByHost += std::holds_alternative<events::Story>(event);
    return !atA.heard.empty();
  }));
  CHECK(atA.heard == std::vector<std::uint64_t>{kNoWeapons, kNoSprint});
  atA.Tick(true);
  CHECK(atA.v == std::set<std::uint64_t>{kNoWeapons, kNoSprint});

  // The same set again is not sent again.
  host->SendStory(proto::StoryRestrictions{.effects = {kNoWeapons, kNoSprint}});
  int repeats = 0;
  Pump(
      {host.get(), a.get()},
      [&](std::size_t i, const auto &events) {
        if (i == 1)
          for (const auto &event : events)
            repeats += std::holds_alternative<events::Story>(event);
        return false;
      },
      200ms);
  CHECK(repeats == 0);
  CHECK(heardByHost == 0);

  // A late joiner gets the set on now.
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error, kFast);
  REQUIRE(b);
  Guest atB;
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    if (i == 2)
      atB.Take(events);
    return !atB.heard.empty();
  }));
  CHECK(atB.heard == std::vector<std::uint64_t>{kNoWeapons, kNoSprint});
  // Out of the moment, b's V stays free.
  atB.Tick(false);
  CHECK(atB.v.empty());

  // The host's scene ends: lifted for everyone.
  host->SendStory(proto::StoryRestrictions{});
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1)
      atA.Take(events);
    if (i == 2)
      atB.Take(events);
    return atA.heard.empty() && atB.heard.empty();
  }));
  atA.Tick(true);
  CHECK(atA.v.empty());

  // On again, then the host vanishes without a word: the guest's V is free.
  host->SendStory(proto::StoryRestrictions{.effects = {kNoJump}});
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1)
      atA.Take(events);
    return !atA.heard.empty();
  }));
  atA.Tick(true);
  CHECK(atA.v == std::set<std::uint64_t>{kNoJump});
  host.reset();
  REQUIRE(Pump({a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    if (i == 0)
      atA.Take(events);
    return atA.ended;
  }));
  CHECK(atA.v.empty());
  CHECK(atA.restrictions.OursCount() == 0);
}

TEST_CASE("a guest that missed the lift is freed by the story resync") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
  REQUIRE((host && a));
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t, const auto &) { return a->IsJoined(); }));
  host->SendStory(proto::StoryRestrictions{.effects = {kNoWeapons}});
  Guest atA;
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1)
      atA.Take(events);
    return !atA.heard.empty();
  }));
  atA.Tick(true);
  REQUIRE(atA.v.size() == 1);

  // The lift is lost on the way: the host's record has none, the guest's
  // still has one. The story digest differs; the resync says "none".
  host->SetFaults({.drop = 1.f});
  host->SendStory(proto::StoryRestrictions{});
  Pump({host.get(), a.get()}, [](std::size_t, const auto &) { return false; }, 100ms);
  host->SetFaults({});
  CHECK_FALSE(atA.heard.empty());
  REQUIRE(Pump(
      {host.get(), a.get()},
      [&](std::size_t i, const auto &events) {
        if (i == 1)
          atA.Take(events);
        return atA.heard.empty();
      },
      15s));
  atA.Tick(true);
  CHECK(atA.v.empty());
}

TEST_CASE("the host's scripted moment: read from its own game, held across short gaps") {
  using story::MomentReasons;
  CHECK(MomentReasons({}) == 0); // free roam, a fight, its own driving
  CHECK(MomentReasons({.tier = 1}) == 0);
  CHECK(MomentReasons({.tier = 2}) == story::kMomentScene);
  CHECK(MomentReasons({.tier = 5}) == (story::kMomentScene | story::kMomentCutscene));
  CHECK(MomentReasons({.restricted = true, .questMount = true}) ==
        (story::kMomentRestricted | story::kMomentMounted));
  CHECK(MomentReasons({.mode = true}) == story::kMomentMode);
  CHECK(MomentReasons({.camera = true}) == story::kMomentCamera);
  CHECK(MomentReasons({.elsewhere = true}) == story::kMomentElsewhere);
  CHECK(MomentReasons({.test = true}) == story::kMomentTest);
  // A private scene is never watched, whatever else holds the V.
  CHECK(MomentReasons({.tier = 4, .isPrivate = true, .restricted = true}) == 0);
  // Played from the guests' own V: a scene of tier 2 to 4, restrictions, a
  // quest ride, a body mode. Only the host's view shows the rest.
  for (const auto played :
       {story::kMomentScene, story::kMomentRestricted, story::kMomentMounted, story::kMomentMode})
    CHECK((played & story::kMomentWatch) == 0);
  for (const auto watched :
       {story::kMomentCutscene, story::kMomentElsewhere, story::kMomentCamera, story::kMomentTest})
    CHECK((watched & story::kMomentWatch) != 0);

  story::HostMoment moment;
  CHECK_FALSE(moment.Update(0, 1'000'000));
  CHECK(moment.Update(story::kMomentScene, 2'000'000));
  CHECK_FALSE(moment.Watch());
  // Between two sections: still on, then off once the hold is over.
  CHECK(moment.Update(0, 2'000'000 + story::kMomentHoldUs - 1));
  CHECK(moment.Update(story::kMomentMounted, 3'000'000));
  CHECK(moment.Update(0, 3'000'000 + story::kMomentHoldUs - 1));
  CHECK_FALSE(moment.Update(0, 3'000'000 + story::kMomentHoldUs));
  CHECK(moment.Update(story::kMomentMode, 9'000'000));
  // A cutscene in it: to watch, held alike; the moment goes on after it.
  CHECK(moment.Update(story::kMomentScene | story::kMomentCutscene, 10'000'000));
  CHECK(moment.Watch());
  CHECK(moment.Update(story::kMomentScene, 10'000'000 + story::kMomentHoldUs - 1));
  CHECK(moment.Watch());
  CHECK(moment.Update(story::kMomentScene, 10'000'000 + story::kMomentHoldUs));
  CHECK_FALSE(moment.Watch());
  moment.Clear();
  CHECK_FALSE(moment.On());
}

TEST_CASE("a guest plays the host's moment from its own V, watches only what it cannot join") {
  using Change = story::Spectator::Change;
  using Why = story::Spectator::Why;
  story::SpectateInput in{.on = true,
                          .hostMoment = true,
                          .grouped = true,
                          .hostTier = 2,
                          .hostKnown = true,
                          .host = {0.f, 0.f, 0.f},
                          .guest = {10.f, 0.f, 0.f}};
  story::Spectator spectator;
  // A dialogue, a walk and talk, restrictions, a ride along: played.
  CHECK(spectator.Update(in) == Change::None);
  CHECK(spectator.Shared());
  CHECK_FALSE(spectator.Watching());
  in.hostRiding = true;
  in.inVehicle = true; // seated in the copy, or carried along
  in.host = {1000.f, 0.f, 0.f};
  in.guest = {1000.f, 1.f, 0.f};
  CHECK(spectator.Update(in) == Change::None);

  // (a) a cutscene: watched, wherever we are; over, we play again at once,
  // the moment going on.
  in.hostWatch = true;
  in.hostTier = 5;
  CHECK(spectator.Update(in) == Change::Start);
  CHECK(spectator.Reason() == Why::Cutscene);
  in.hostWatch = false;
  in.hostTier = 3;
  CHECK(spectator.Update(in) == Change::Stop);
  CHECK(spectator.Reason() == Why::None);
  // (b) the host's V in another world, (d) the quest's camera.
  in.hostWatch = true;
  in.hostElsewhere = true;
  CHECK(spectator.Update(in) == Change::Start);
  CHECK(spectator.Reason() == Why::Elsewhere);
  in.hostElsewhere = false;
  CHECK(spectator.Update(in) == Change::None);
  CHECK(spectator.Reason() == Why::View);
  in.hostWatch = false;
  CHECK(spectator.Update(in) == Change::Stop);
  // A cutscene over while the host rides on and we were carried in its
  // copy (riding nothing ourselves): watched on until the ride ends.
  in.inVehicle = false;
  in.hostWatch = true;
  in.hostTier = 5;
  CHECK(spectator.Update(in) == Change::Start);
  in.hostWatch = false;
  in.hostTier = 3;
  CHECK(spectator.Update(in) == Change::None);
  CHECK(spectator.Reason() == Why::Taken);
  in.hostRiding = false;
  CHECK(spectator.Update(in) == Change::Stop);
  in.hostRiding = true;

  // (c) taken beyond our reach while we ride nothing: watched, carried in
  // its copy until its ride ends, then in reach: played again.
  in.inVehicle = false;
  in.guest = {1000.f - story::kSpectateReach - 5.f, 0.f, 0.f};
  CHECK(spectator.Update(in) == Change::Start);
  CHECK(spectator.Reason() == Why::Taken);
  in.guest = {1000.f, 1.f, 0.f}; // carried in the copy
  CHECK(spectator.Update(in) == Change::None);
  in.hostRiding = false;
  CHECK(spectator.Update(in) == Change::Stop);
  // On foot: beyond reach, then carried back to it.
  in.guest = {1000.f + story::kSpectateReach + 5.f, 0.f, 0.f};
  CHECK(spectator.Update(in) == Change::Start);
  in.guest = {1000.f + story::kSpectateReachBack + 5.f, 0.f, 0.f};
  CHECK(spectator.Update(in) == Change::None);
  in.guest = {1003.f, 0.f, 0.f};
  CHECK(spectator.Update(in) == Change::Stop);
  // Driving after it in a car of our own: played, however far.
  in.inVehicle = true;
  in.guest = {0.f, 0.f, 0.f};
  CHECK(spectator.Update(in) == Change::None);

  // The moment is over: a guest that never stood in the next one does not
  // watch its cutscene.
  in.hostMoment = false;
  CHECK(spectator.Update(in) == Change::None);
  CHECK_FALSE(spectator.Shared());
  in.hostMoment = true;
  in.hostWatch = true;
  in.hostTier = 5;
  in.guest = {1000.f + story::kMomentNear + 5.f, 0.f, 0.f};
  CHECK(spectator.Update(in) == Change::None);

  // A private scene, our own other world, apart, switched off: none.
  in.guest = {1005.f, 0.f, 0.f};
  for (auto change : {&story::SpectateInput::hostPrivate, &story::SpectateInput::ourAway}) {
    auto other = in;
    other.*change = true;
    CHECK(story::Spectator{}.Update(other) == Change::None);
  }
  auto apart = in;
  apart.grouped = false;
  CHECK(story::Spectator{}.Update(apart) == Change::None);
  auto off = in;
  off.on = false;
  CHECK(story::Spectator{}.Update(off) == Change::None);

  // Watching, then the host's scene turns private: we stop.
  REQUIRE(spectator.Update(in) == Change::Start);
  auto turned = in;
  turned.hostPrivate = true;
  CHECK(spectator.Update(turned) == Change::Stop);

  // The player leaves: not again for this reason, again for the next one.
  REQUIRE(spectator.Update(in) == Change::Start);
  CHECK(spectator.Leave() == Change::Stop);
  CHECK(spectator.Leave() == Change::None);
  CHECK(spectator.Update(in) == Change::None);
  CHECK(spectator.Declined());
  auto over = in;
  over.hostWatch = false;
  CHECK(spectator.Update(over) == Change::None);
  CHECK_FALSE(spectator.Declined());
  CHECK(spectator.Update(in) == Change::Start);

  // The session ends, or the host vanishes.
  CHECK(spectator.Lift() == Change::Stop);
  CHECK_FALSE(spectator.Watching());
  CHECK(spectator.Lift() == Change::None);
}
