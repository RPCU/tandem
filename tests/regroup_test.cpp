// Regroup offers and answers (milestone 4: fast travel together) over
// loopback UDP: each reaches only whom it is for, stamped by the host.

#include "referee_expect.hpp"
#include "slowdown.hpp"
#include "tandem/endpoint.hpp"

#include <doctest/doctest.h>

#include <functional>
#include <map>
#include <optional>
#include <vector>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
struct Heard {
  std::vector<proto::Regroup> offers;
  std::vector<proto::RegroupAnswer> answers;
  bool joined{};
};

// Runs the endpoints, collecting what each one hears, until `done` holds
// (5 s at most: a loaded machine is slow, not wrong), then 150 ms more so
// what should not arrive has had its chance to.
void Run(const std::vector<Endpoint *> &endpoints, std::vector<Heard> &heard,
         const std::function<bool()> &done) {
  heard.resize(endpoints.size());
  const auto cap = std::chrono::steady_clock::now() + test::Patience(5s);
  std::optional<std::chrono::steady_clock::time_point> settled;
  while (!settled || std::chrono::steady_clock::now() < *settled) {
    if (!settled && (done() || std::chrono::steady_clock::now() >= cap))
      settled = std::chrono::steady_clock::now() + 150ms;
    for (std::size_t i = 0; i < endpoints.size(); ++i)
      for (const auto &event : endpoints[i]->Update(1ms)) {
        if (const auto *offer = std::get_if<events::RegroupOffered>(&event))
          heard[i].offers.push_back(offer->offer);
        else if (const auto *answer = std::get_if<events::RegroupAnswered>(&event))
          heard[i].answers.push_back(answer->answer);
        else if (std::holds_alternative<events::Joined>(event))
          heard[i].joined = true;
      }
  }
}

void Clear(std::vector<Heard> &heard) {
  for (auto &h : heard)
    h.offers.clear(), h.answers.clear();
}
} // namespace

TEST_CASE("a regroup offer reaches whom it is for, stamped with who made it; answers go back") {
  // Answers to offers never made: only the routing is tested.
  test::ExpectReferee({"ids.unknown-offer"});
  std::string error;
  auto host = Endpoint::Host({"host"}, 0, error);
  REQUIRE_MESSAGE(host, error);
  const auto port = host->LocalPort();
  REQUIRE(port != 0);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(a, error);
  std::vector<Heard> heard;
  std::vector<Endpoint *> all{host.get(), a.get()};
  Run(all, heard, [&] { return a->IsJoined(); });
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(b, error);
  all.push_back(b.get());
  Run(all, heard, [&] { return b->IsJoined() && host->Players().size() == 3; });
  REQUIRE(a->You() == 2);
  REQUIRE(b->You() == 3);

  SUBCASE("to everyone: every other player, never back to its sender") {
    a->SendRegroup({.from = 99, // a guest cannot offer in someone else's name
                    .to = proto::kNoPlayer,
                    .offer = 1,
                    .reason = proto::RegroupReason::FastTravel,
                    .point = 0x1234,
                    .position = {100.f, 200.f, 10.f}});
    Run(all, heard, [&] { return !heard[0].offers.empty() && !heard[2].offers.empty(); });
    REQUIRE(heard[0].offers.size() == 1);
    REQUIRE(heard[2].offers.size() == 1);
    CHECK(heard[1].offers.empty());
    CHECK(heard[0].offers[0].from == 2);
    CHECK(heard[2].offers[0].from == 2);
    CHECK(heard[2].offers[0].point == 0x1234);
    CHECK(heard[2].offers[0].position == proto::Vec3{100.f, 200.f, 10.f});
  }

  SUBCASE("to one player: only them") {
    a->SendRegroup({.to = 3, .offer = 2, .reason = proto::RegroupReason::Asked});
    host->SendRegroup(
        {.to = 2, .offer = 1, .reason = proto::RegroupReason::Story, .label = "scene"});
    Run(all, heard, [&] { return !heard[1].offers.empty() && !heard[2].offers.empty(); });
    CHECK(heard[0].offers.empty());
    REQUIRE(heard[1].offers.size() == 1);
    CHECK(heard[1].offers[0].from == 1);
    CHECK(heard[1].offers[0].label == "scene");
    REQUIRE(heard[2].offers.size() == 1);
    CHECK(heard[2].offers[0].from == 2);
  }

  SUBCASE("answers reach the offer's sender only") {
    b->SendRegroupAnswer({.from = 1, .to = 2, .offer = 7, .accepted = true});
    a->SendRegroupAnswer({.to = 1, .offer = 3, .accepted = false});
    b->SendRegroupAnswer({.to = proto::kNoPlayer, .offer = 1, .accepted = true}); // nobody
    Run(all, heard, [&] { return !heard[1].answers.empty() && !heard[0].answers.empty(); });
    REQUIRE(heard[1].answers.size() == 1);
    CHECK(heard[1].answers[0].from == 3);
    CHECK(heard[1].answers[0].offer == 7);
    CHECK(heard[1].answers[0].accepted);
    REQUIRE(heard[0].answers.size() == 1);
    CHECK(heard[0].answers[0].from == 2);
    CHECK_FALSE(heard[0].answers[0].accepted);
    CHECK(heard[2].answers.empty());
  }

  SUBCASE("an over-long label is cut, not refused") {
    host->SendRegroup({.offer = 5, .label = std::string(500, 'q')});
    Run(all, heard, [&] { return !heard[1].offers.empty(); });
    REQUIRE(heard[1].offers.size() == 1);
    CHECK(heard[1].offers[0].label.size() == proto::kMaxRegroupLabel);
  }
  Clear(heard);
}
