// A partner's quickhacks (docs/architecture/comms.md, "A partner's
// quickhacks"): what a game shows of another player's uploads and effects
// (comms::HackBoard), only while the two are linked; and the same over
// loopback UDP, from the host's own hack messages.

#include "tandem/comms.hpp"
#include "tandem/endpoint.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <functional>
#include <vector>

using namespace std::chrono_literals;
using namespace tandem;
using K = comms::HackChange::Kind;

namespace {
constexpr std::uint64_t kSecond = 1'000'000;
}

TEST_CASE("a partner's quickhacks: shown while linked, hidden at a break, gone with the hacker") {
  comms::HackBoard board;
  const std::uint64_t t = 50 * kSecond, blind = 0xB11D, blinded = 0xB11E;
  // Not linked: kept, nothing shown.
  CHECK(board.Upload(70, 2, blind, 3.f, t).empty());
  CHECK(board.Shown() == 0);
  // Linked: what runs shows at once, with what is left of it.
  auto changes = board.SetLinked(2, true, t + kSecond);
  REQUIRE(changes.size() == 1);
  CHECK(changes[0].kind == K::ShowUpload);
  CHECK(changes[0].action == blind);
  CHECK(changes[0].seconds == doctest::Approx(3.f)); // 3 s and the 1 s grace, 1 s later
  // Done: the upload goes, the effect it left shows with the program's icon.
  changes = board.UploadEnded(70, 2, blind);
  REQUIRE(changes.size() == 1);
  CHECK(changes[0].kind == K::HideUpload);
  changes = board.Effect(70, 2, blinded, true, 12.f, t + 4 * kSecond);
  REQUIRE(changes.size() == 1);
  CHECK(changes[0].kind == K::ShowEffect);
  CHECK(changes[0].action == blind);
  CHECK(changes[0].effect == blinded);
  // An NPC's own effect, or nobody's: not a partner's.
  CHECK(board.Effect(70, proto::kNoPlayer, 0xF1, true, 5.f, t).empty());

  SUBCASE("the link breaks: hidden at once; made again: back") {
    changes = board.SetLinked(2, false, t + 5 * kSecond);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].kind == K::HideEffect);
    CHECK(board.Shown() == 0);
    changes = board.SetLinked(2, true, t + 6 * kSecond);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].kind == K::ShowEffect);
    CHECK(changes[0].seconds == doctest::Approx(10.f));
  }
  SUBCASE("the effect ends (the host says, with no player): hidden") {
    changes = board.Effect(70, proto::kNoPlayer, blinded, false, 0.f, t + 5 * kSecond);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].kind == K::HideEffect);
  }
  SUBCASE("it runs out, or the NPC goes") {
    CHECK(board.Expire(t + 15 * kSecond).empty());
    CHECK(board.Expire(t + 16 * kSecond).size() == 1);
    board.Upload(71, 2, blind, 2.f, t);
    CHECK(board.ForgetNpc(71).size() == 1);
  }
  SUBCASE("the hacker vanishes: nothing of theirs stays") {
    board.Upload(71, 2, blind, 2.f, t);
    board.Upload(72, 3, blind, 2.f, t); // another player, not linked
    CHECK(board.Forget(2).size() == 2);
    CHECK_FALSE(board.Linked(2));
    CHECK(board.SetLinked(3, true, t).size() == 1);
    CHECK(board.Clear().size() == 1);
  }
  SUBCASE("a new upload of the same player on the same NPC replaces its indicator") {
    board.Upload(73, 2, blind, 2.f, t);
    changes = board.Upload(73, 2, 0xC0, 4.f, t + kSecond);
    REQUIRE(changes.size() == 2);
    CHECK(changes[0].kind == K::HideUpload);
    CHECK(changes[1].kind == K::ShowUpload);
    CHECK(changes[1].action == 0xC0);
  }
}

TEST_CASE("a partner's quickhacks in a session: seen by the players linked with the hacker only") {
  std::string error;
  const Timing quick{.timeout = 1000ms, .takeover = 400ms};
  auto host = Endpoint::Host({"host"}, 0, error, quick);
  REQUIRE_MESSAGE(host, error);
  const auto port = host->LocalPort();
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, quick);
  REQUIRE_MESSAGE(a, error);
  // Each watching guest's game: its endpoint, its consent, its view of the
  // links, its board, what it changed on screen.
  struct Game {
    std::unique_ptr<Endpoint> endpoint;
    proto::Link own;
    comms::Links links;
    comms::HackBoard hacks;
    std::vector<comms::HackChange> seen;
  };
  std::vector<Game> games(2);
  games[0].endpoint = Endpoint::Join({"b"}, "127.0.0.1", port, error, quick); // linked with a
  games[0].own = {.mode = proto::LinkMode::Listed, .players = {2}};
  games[1].endpoint = Endpoint::Join({"c"}, "127.0.0.1", port, error, quick); // with nobody
  games[1].own = {.mode = proto::LinkMode::Listed};
  REQUIRE((games[0].endpoint && games[1].endpoint));
  host->SetLocalLink({.mode = proto::LinkMode::Everyone});
  a->SetLocalLink({.mode = proto::LinkMode::Everyone});
  for (auto &game : games)
    game.endpoint->SetLocalLink(game.own);

  const auto pump = [&](const std::function<bool()> &done) {
    const auto cap = std::chrono::steady_clock::now() + 4s;
    while (!done() && std::chrono::steady_clock::now() < cap) {
      host->Update(1ms);
      if (a)
        a->Update(1ms);
      for (auto &game : games) {
        const auto events = game.endpoint->Update(1ms);
        const auto now = net::NowUs();
        game.links.SetYou(game.endpoint->You());
        game.links.SetOwn(game.own);
        for (const auto &[id, info] : game.endpoint->Players())
          if (id != game.endpoint->You())
            game.links.Join(id);
        for (const auto &[id, link] : game.endpoint->Links())
          game.links.Note(link);
        for (const auto &event : events) {
          std::vector<comms::HackChange> changes;
          if (const auto *state = std::get_if<events::HostNpcHackState>(&event)) {
            const auto &s = state->state;
            changes = s.step == proto::HackStep::Started
                          ? game.hacks.Upload(s.npc, s.player, s.action, s.uploadMs / 1000.f, now)
                          : game.hacks.UploadEnded(s.npc, s.player, s.action);
          } else if (const auto *status = std::get_if<events::HostNpcStatus>(&event)) {
            const auto &s = status->status;
            changes = game.hacks.Effect(s.npc, s.player, s.effect, s.applied,
                                        s.applied ? 10.f : 0.f, now);
          } else if (const auto *left = std::get_if<events::PlayerLeft>(&event)) {
            game.links.Forget(left->player);
            changes = game.hacks.Forget(left->player);
          }
          game.seen.insert(game.seen.end(), changes.begin(), changes.end());
        }
        for (const auto id : game.links.Players()) {
          const auto more = game.hacks.SetLinked(id, game.links.LinkedWith(id), now);
          game.seen.insert(game.seen.end(), more.begin(), more.end());
        }
      }
    }
    return done();
  };
  const auto has = [](const Game &game, K kind) {
    return std::ranges::any_of(game.seen, [&](const auto &c) { return c.kind == kind; });
  };
  REQUIRE(pump([&] {
    return games[0].links.LinkedWith(2) && games[1].links.Players().contains(2) &&
           host->Players().size() == 4;
  }));
  CHECK_FALSE(games[1].links.LinkedWith(2));

  // a's upload on a host NPC, then the effect it left (the host says both).
  host->SendNpcHackState({.npc = 70,
                          .player = 2,
                          .action = 0xB11D,
                          .step = proto::HackStep::Started,
                          .uploadMs = 3000});
  host->SendNpcStatus({.npc = 70, .player = 2, .effect = 0xB11E, .applied = true});
  REQUIRE(pump([&] { return has(games[0], K::ShowUpload) && has(games[0], K::ShowEffect); }));
  pump([] { return false; }); // the unlinked game has had its chance
  CHECK(games[1].seen.empty());

  // a breaks its link with b: b drops a's indicators at once.
  games[0].seen.clear();
  a->SetLocalLink({.mode = proto::LinkMode::Everyone, .players = {3}});
  REQUIRE(pump([&] { return has(games[0], K::HideEffect); }));
  // (The upload, 3 s long, has run out by now: nothing of it to hide.)

  // Linked again (what still runs shows), then a vanishes: nothing of a's stays.
  a->SetLocalLink({.mode = proto::LinkMode::Everyone});
  REQUIRE(pump([&] { return games[0].hacks.Shown() > 0; }));
  games[0].seen.clear();
  a.reset();
  REQUIRE(pump([&] { return has(games[0], K::HideEffect); }));
  CHECK(games[0].hacks.Shown() == 0);
  CHECK(games[1].seen.empty());
}
