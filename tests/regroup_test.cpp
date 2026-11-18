// Regroup offers and answers (milestone 4: fast travel together) over
// loopback UDP: each reaches only whom it is for, stamped by the host.

#include "tandem/endpoint.hpp"

#include <doctest/doctest.h>

#include <map>
#include <vector>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
struct Heard {
  std::vector<proto::Regroup> offers;
  std::vector<proto::RegroupAnswer> answers;
  bool joined{};
};

// Runs the endpoints for `time`, collecting what each one hears.
void Run(const std::vector<Endpoint *> &endpoints, std::vector<Heard> &heard,
         std::chrono::milliseconds time) {
  heard.resize(endpoints.size());
  const auto deadline = std::chrono::steady_clock::now() + time;
  while (std::chrono::steady_clock::now() < deadline)
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

void Clear(std::vector<Heard> &heard) {
  for (auto &h : heard)
    h.offers.clear(), h.answers.clear();
}
} // namespace

TEST_CASE("a regroup offer reaches whom it is for, stamped with who made it; answers go back") {
  std::string error;
  auto host = Endpoint::Host({"host"}, 0, error);
  REQUIRE_MESSAGE(host, error);
  const auto port = host->LocalPort();
  REQUIRE(port != 0);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(a, error);
  std::vector<Heard> heard;
  std::vector<Endpoint *> all{host.get(), a.get()};
  Run(all, heard, 300ms);
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(b, error);
  all.push_back(b.get());
  Run(all, heard, 300ms);
  REQUIRE(a->You() == 2);
  REQUIRE(b->You() == 3);

  SUBCASE("to everyone: every other player, never back to its sender") {
    a->SendRegroup({.from = 99, // a guest cannot offer in someone else's name
                    .to = proto::kNoPlayer,
                    .offer = 1,
                    .reason = proto::RegroupReason::FastTravel,
                    .point = 0x1234,
                    .position = {100.f, 200.f, 10.f}});
    Run(all, heard, 300ms);
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
    Run(all, heard, 300ms);
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
    Run(all, heard, 300ms);
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
    Run(all, heard, 300ms);
    REQUIRE(heard[1].offers.size() == 1);
    CHECK(heard[1].offers[0].label.size() == proto::kMaxRegroupLabel);
  }
  Clear(heard);
}
