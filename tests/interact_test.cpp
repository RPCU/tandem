// Quest interactions (docs/architecture/story.md, "Quest interactions"): the
// verdicts kept, first come first served, the reach check, then a real host
// and real guests over loopback UDP in one process. The "game" of each side
// is played by the test: the host's judges each question the way
// scripts/tandem/Interact.reds does (by what its own copy is), a guest's
// counts the verdicts it hears.

#include "ports.hpp"
#include "tandem/endpoint.hpp"
#include "tandem/interact.hpp"

#include <doctest/doctest.h>

#include <functional>
#include <map>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
using test::NextPort;

constexpr std::uint64_t kSwitch = 0x5EED00000000AA01ULL; // a quest switch, placed
constexpr std::uint64_t kLink = 0x5EED00000000AA02ULL;   // an access point (personal link)
constexpr std::uint64_t kCrate = 0x5EED00000000AA03ULL;  // a crate, nothing of a quest
constexpr std::uint64_t kUse = 0xC0FFEE01, kJackIn = 0xC0FFEE02, kLoot = 0xC0FFEE03;

proto::QuestInteract Ask(std::uint64_t target, std::uint64_t choice, std::uint32_t seq,
                         proto::Vec3 at = {}) {
  return {.target = target, .choice = choice, .detail = 1, .seq = seq, .position = at};
}

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

struct Game {
  Endpoint *endpoint{};
  // Host: what its game says of each object's choice (as Interact.reds
  // finds it: performs it, or it casts the doer, or it is nothing of a
  // quest), and whether it answers at all (busy: not yet).
  std::map<std::uint64_t, std::uint8_t> judge;
  bool answers{true};
  int questions{}, performed{};
  std::vector<proto::QuestInteract> held; // questions not answered yet
  // Guest: the verdicts it heard, in order.
  std::vector<proto::QuestInteract> answers_;

  void See(const std::vector<SessionEvent> &events) {
    for (const auto &event : events) {
      const auto *story = std::get_if<events::Story>(&event);
      if (!story)
        continue;
      const auto *m = std::get_if<proto::QuestInteract>(&story->message);
      if (!m)
        continue;
      if (m->verdict != proto::kInteractAsk) {
        answers_.push_back(*m);
        continue;
      }
      ++questions;
      if (!answers) {
        held.push_back(*m);
        continue;
      }
      Answer(*m);
    }
  }
  void Answer(proto::QuestInteract m) {
    const auto it = judge.find(m.target);
    m.verdict = it == judge.end() ? proto::kInteractMissing : it->second;
    performed += m.verdict == proto::kInteractPerformed ? 1 : 0;
    endpoint->SendStory(m, m.player);
  }
  std::uint8_t Last() const { return answers_.empty() ? 0xFF : answers_.back().verdict; }
};

constexpr Timing kQuick{.digest = 100ms, .resyncCooldown = 200ms};

struct Session {
  std::unique_ptr<Endpoint> host, a, b;
  Game hostGame, aGame, bGame;

  std::vector<Endpoint *> All() const {
    std::vector<Endpoint *> out{host.get()};
    if (a)
      out.push_back(a.get());
    if (b)
      out.push_back(b.get());
    return out;
  }
  bool Run(const std::function<bool()> &done, std::chrono::milliseconds limit = 3s) {
    return Pump(
        All(),
        [&](std::size_t i, const auto &events) {
          Game *games[] = {&hostGame, a ? &aGame : &bGame, &bGame};
          games[i]->See(events);
          return done();
        },
        limit);
  }
  void RunFor(std::chrono::milliseconds time) {
    Run([] { return false; }, time);
  }
};

Session Start(bool two, Timing timing = kQuick) {
  const auto port = NextPort();
  std::string error;
  Session s;
  s.host = Endpoint::Host({"host"}, port, error, timing);
  s.a = Endpoint::Join({"a"}, "127.0.0.1", port, error, timing);
  if (two)
    s.b = Endpoint::Join({"b"}, "127.0.0.1", port, error, timing);
  REQUIRE((s.host && s.a && (!two || s.b)));
  s.hostGame.endpoint = s.host.get();
  s.aGame.endpoint = s.a.get();
  s.bGame.endpoint = s.b.get();
  s.hostGame.judge = {{kSwitch, proto::kInteractPerformed},
                      {kLink, proto::kInteractHostOnly},
                      {kCrate, proto::kInteractLocal}};
  REQUIRE(s.Run([&] {
    return s.a->Players().size() == (two ? 3u : 2u) && (!two || s.b->Players().size() == 3u);
  }));
  return s;
}
} // namespace

TEST_CASE("interact: only what a choice is stays known, the oldest goes first") {
  interact::Known known;
  const interact::Key a{1, 2, 3}, b{1, 2, 4};
  known.Note(a, proto::kInteractHostOnly);
  known.Note(b, proto::kInteractTaken); // of the moment: not kept
  CHECK(known.Get(a) == proto::kInteractHostOnly);
  CHECK_FALSE(known.Get(b));
  known.Note(a, proto::kInteractPerformed); // the host's game changed its mind
  CHECK(known.Get(a) == proto::kInteractPerformed);
  for (std::uint64_t i = 0; i < interact::kMaxKnown + 10; ++i)
    known.Note({100 + i, 0, 0}, proto::kInteractLocal);
  CHECK(known.Size() == interact::kMaxKnown);
  CHECK_FALSE(known.Get(a)); // the oldest went
  CHECK(known.Get({100 + interact::kMaxKnown + 9, 0, 0}));
}

TEST_CASE("interact: first come, first served, per object") {
  interact::Referee referee;
  using Claim = interact::Referee::Claim;
  CHECK(referee.Ask(kSwitch, 2, 1, 1'000) == Claim::Granted);
  CHECK(referee.Ask(kSwitch, 3, 1, 1'500) == Claim::Taken);   // with the host's game
  CHECK(referee.Ask(kCrate, 3, 2, 1'500) == Claim::Granted);  // another object
  CHECK(referee.Ask(kSwitch, 2, 2, 2'000) == Claim::Granted); // its own, again
  CHECK_FALSE(referee.Answered(kSwitch, 2, 1, proto::kInteractPerformed, 3'000)); // superseded
  CHECK(referee.Answered(kSwitch, 2, 2, proto::kInteractPerformed, 3'000));
  CHECK_FALSE(referee.Answered(kSwitch, 2, 2, proto::kInteractPerformed, 3'100)); // once
  // Done: held a moment, then free.
  CHECK(referee.Ask(kSwitch, 3, 3, 3'000 + interact::kHoldUs / 2) == Claim::Taken);
  CHECK(referee.Ask(kSwitch, 3, 4, 3'000 + interact::kHoldUs) == Claim::Granted);
  // Anything but done frees it at once.
  CHECK(referee.Answered(kSwitch, 3, 4, proto::kInteractHostOnly, 3'000 + interact::kHoldUs));
  CHECK(referee.Ask(kSwitch, 2, 5, 3'000 + interact::kHoldUs) == Claim::Granted);
  // A question the game never answers is let go.
  CHECK(referee.Pending(2, 5));
  CHECK(referee.Expire(10'000'000) == 2);
  CHECK_FALSE(referee.Pending(2, 5));
  // A vanished guest's questions and holds go with it.
  CHECK(referee.Ask(kSwitch, 2, 6, 20'000'000) == Claim::Granted);
  CHECK(referee.Forget(2) == 1);
  CHECK(referee.Ask(kSwitch, 3, 7, 20'000'000) == Claim::Granted);
  CHECK_FALSE(referee.Answered(kSwitch, 2, 6, proto::kInteractPerformed, 20'000'001));
}

TEST_CASE("interact: too many questions at once are refused") {
  interact::Referee referee;
  for (std::uint64_t i = 0; i < interact::kMaxPending; ++i)
    CHECK(referee.Ask(1000 + i, 2, static_cast<std::uint32_t>(i), 1) ==
          interact::Referee::Claim::Granted);
  CHECK(referee.Ask(99, 2, 99, 1) == interact::Referee::Claim::Full);
}

TEST_CASE("interact: the reach is the guest's word within its track, else its track") {
  lagcomp::History track;
  for (std::uint64_t t = 0; t <= 1'000'000; t += 100'000)
    track.Push(t, {static_cast<float>(t) / 100'000.f, 0, 0}); // 1 m every 100 ms
  // It says it was at x = 9 at 900 ms, beside the object at x = 11: believed.
  auto reach = interact::CheckReach(&track, {9, 0, 0}, {11, 0, 0}, 900'000, 1'000'000, 60);
  CHECK(reach.ok);
  CHECK(reach.claimOff == doctest::Approx(0.f));
  CHECK(reach.distance == doctest::Approx(2.f));
  // Its word is 6 m off its own track: the track is believed, and that is too far.
  reach = interact::CheckReach(&track, {15, 0, 0}, {16, 0, 0}, 900'000, 1'000'000, 60);
  CHECK(reach.claimOff == doctest::Approx(6.f));
  CHECK_FALSE(reach.ok);
  // A time far back is clamped to the rewind bound (400 ms): x = 6.
  reach = interact::CheckReach(&track, {6, 0, 0}, {6, 0, 3}, 10, 1'000'000, 60);
  CHECK(reach.claimOff == doctest::Approx(0.f));
  CHECK(reach.ok);
  // No track (the host never heard the V): its word, and the object's distance.
  reach = interact::CheckReach(nullptr, {0, 0, 0}, {0, 9, 0}, 0, 1'000'000, 60);
  CHECK_FALSE(reach.ok);
  CHECK(interact::CheckReach(nullptr, {0, 0, 0}, {0, 3, 0}, 0, 1, 0).ok);
}

TEST_CASE("interact: QuestInteract round-trips and refuses an unknown verdict or flag") {
  const proto::QuestInteract m{.target = kSwitch,
                               .choice = kUse,
                               .detail = 0xABCDEF0123ULL,
                               .layer = 0x77,
                               .seq = 9,
                               .position = {1.5f, -2.f, 3.25f},
                               .timeUs = 123456789,
                               .player = 2,
                               .verdict = proto::kInteractHostOnly,
                               .flags = proto::kInteractChecked | proto::kInteractCasts};
  const auto back = proto::Decode(proto::Encode(m));
  REQUIRE(back);
  CHECK(std::get<proto::QuestInteract>(*back) == m);
  CHECK(proto::IsStory(m));
  CHECK(proto::IsReliable(m));
  CHECK(std::string(proto::NameOf(m)) == "QuestInteract");
  CHECK(proto::Encode(m).size() <= 72);
  auto bad = m;
  bad.verdict = proto::kInteractVerdictMax + 1;
  CHECK_FALSE(proto::Decode(proto::Encode(bad)));
  bad = m;
  bad.flags = 0x80;
  CHECK_FALSE(proto::Decode(proto::Encode(bad)));
  bad = m;
  bad.target = 0;
  CHECK_FALSE(proto::Decode(proto::Encode(bad)));
}

TEST_CASE("interact: a guest's use is performed by the host's game, and asked again each time") {
  auto s = Start(false);
  s.a->SendStory(Ask(kSwitch, kUse, 1));
  REQUIRE(s.Run([&] { return !s.aGame.answers_.empty(); }));
  CHECK(s.hostGame.questions == 1);
  CHECK(s.hostGame.performed == 1);
  CHECK(s.aGame.Last() == proto::kInteractPerformed);
  CHECK(s.aGame.answers_.back().seq == 1);
  CHECK(s.aGame.answers_.back().player == s.a->You());
  // A second press is the host's game's again (a switch flips twice).
  s.a->SendStory(Ask(kSwitch, kUse, 2));
  REQUIRE(s.Run([&] { return s.aGame.answers_.size() == 2; }));
  CHECK(s.hostGame.performed == 2);
  CHECK(s.host->InteractInstant() == 0);
}

TEST_CASE("interact: host only, known for the session: the second ask never leaves the guest") {
  auto s = Start(true);
  s.a->SendStory(Ask(kLink, kJackIn, 1));
  REQUIRE(s.Run([&] { return !s.aGame.answers_.empty(); }));
  CHECK(s.aGame.Last() == proto::kInteractHostOnly);
  CHECK(s.a->InteractKnown().Get({kLink, kJackIn, 1}) == proto::kInteractHostOnly);
  // Again: answered by the guest's own endpoint, at once.
  s.a->SendStory(Ask(kLink, kJackIn, 2));
  REQUIRE(s.Run([&] { return s.aGame.answers_.size() == 2; }));
  CHECK(s.aGame.Last() == proto::kInteractHostOnly);
  CHECK(s.a->InteractInstant() == 1);
  CHECK(s.hostGame.questions == 1);
  // Another guest's first ask: the host knows it, its game is not asked.
  s.b->SendStory(Ask(kLink, kJackIn, 1));
  REQUIRE(s.Run([&] { return !s.bGame.answers_.empty(); }));
  CHECK(s.bGame.Last() == proto::kInteractHostOnly);
  CHECK(s.host->InteractInstant() == 1);
  CHECK(s.hostGame.questions == 1);
  // Nothing of a quest: local, and known the same way.
  s.a->SendStory(Ask(kCrate, kLoot, 3));
  REQUIRE(s.Run([&] { return s.aGame.answers_.size() == 3; }));
  CHECK(s.aGame.Last() == proto::kInteractLocal);
  s.a->SendStory(Ask(kCrate, kLoot, 4));
  REQUIRE(s.Run([&] { return s.aGame.answers_.size() == 4; }));
  CHECK(s.a->InteractInstant() == 2);
  CHECK(s.hostGame.questions == 2);
  // What happened once (missing) is not kept: asked again.
  s.a->SendStory(Ask(0x1234, kUse, 5));
  REQUIRE(s.Run([&] { return s.aGame.answers_.size() == 5; }));
  CHECK(s.aGame.Last() == proto::kInteractMissing);
  s.a->SendStory(Ask(0x1234, kUse, 6));
  REQUIRE(s.Run([&] { return s.aGame.answers_.size() == 6; }));
  CHECK(s.hostGame.questions == 4);
}

TEST_CASE("interact: two guests at one object: the first is performed, the second is told") {
  auto s = Start(true);
  s.hostGame.answers = false; // the host's game takes its time with the first
  s.a->SendStory(Ask(kSwitch, kUse, 1));
  REQUIRE(s.Run([&] { return s.hostGame.questions == 1; }));
  s.b->SendStory(Ask(kSwitch, kUse, 1));
  REQUIRE(s.Run([&] { return !s.bGame.answers_.empty(); }));
  CHECK(s.bGame.Last() == proto::kInteractTaken);
  CHECK(s.host->InteractTaken() == 1);
  CHECK(s.hostGame.questions == 1); // the second never reached the host's game
  s.hostGame.Answer(s.hostGame.held.front());
  REQUIRE(s.Run([&] { return !s.aGame.answers_.empty(); }));
  CHECK(s.aGame.Last() == proto::kInteractPerformed);
  // Within the hold the object is still a's; after it, b's turn.
  s.hostGame.answers = true;
  s.b->SendStory(Ask(kSwitch, kUse, 2));
  REQUIRE(s.Run([&] { return s.bGame.answers_.size() == 2; }));
  CHECK(s.bGame.Last() == proto::kInteractTaken);
  s.RunFor(std::chrono::milliseconds(interact::kHoldUs / 1000 + 100));
  s.b->SendStory(Ask(kSwitch, kUse, 3));
  REQUIRE(s.Run([&] { return s.bGame.answers_.size() == 3; }));
  CHECK(s.bGame.Last() == proto::kInteractPerformed);
}

TEST_CASE("interact: a guest vanishes mid-request: its answer goes nowhere, the object is free") {
  auto s = Start(true, {.timeout = 1000ms, .takeover = 400ms, .digest = 100ms});
  s.hostGame.answers = false;
  s.a->SendStory(Ask(kSwitch, kUse, 1));
  REQUIRE(s.Run([&] { return s.hostGame.questions == 1; }));
  const auto gone = s.a->You();
  s.a.reset(); // no Bye
  bool left = false;
  REQUIRE(Pump({s.host.get(), s.b.get()}, [&](std::size_t i, const auto &events) {
    if (i == 0)
      for (const auto &event : events)
        left = left || std::holds_alternative<events::PlayerLeft>(event);
    return left;
  }));
  CHECK(s.host->InteractReferee().PendingCount() == 0);
  // The host's game answers now: nobody to tell, and nothing is learnt.
  s.hostGame.Answer(s.hostGame.held.front());
  CHECK_FALSE(s.host->InteractKnown().Get({kSwitch, kUse, 1}));
  CHECK(gone != s.b->You());
  // The object is free for the next player at once.
  s.bGame.endpoint = s.b.get();
  s.hostGame.answers = true;
  s.b->SendStory(Ask(kSwitch, kUse, 1));
  REQUIRE(Pump({s.host.get(), s.b.get()}, [&](std::size_t i, const auto &events) {
    (i == 0 ? s.hostGame : s.bGame).See(events);
    return !s.bGame.answers_.empty();
  }));
  CHECK(s.bGame.Last() == proto::kInteractPerformed);
  CHECK(s.bGame.answers_.back().player == s.b->You());
}

TEST_CASE("interact: a guest's answer is never another's, and a guest never answers") {
  auto s = Start(true);
  // A guest that tries to answer for the host: dropped by the host.
  auto fake = Ask(kSwitch, kUse, 1);
  fake.verdict = proto::kInteractPerformed;
  s.a->SendStory(fake);
  s.RunFor(150ms);
  CHECK(s.hostGame.questions == 0);
  CHECK(s.bGame.answers_.empty());
  // The host's game answers a: b hears nothing of it.
  s.a->SendStory(Ask(kCrate, kLoot, 2));
  REQUIRE(s.Run([&] { return !s.aGame.answers_.empty(); }));
  s.RunFor(100ms);
  CHECK(s.bGame.answers_.empty());
  // The host's own question to itself is nothing.
  s.host->SendStory(Ask(kSwitch, kUse, 3));
  s.RunFor(100ms);
  CHECK(s.hostGame.questions == 1);
}
