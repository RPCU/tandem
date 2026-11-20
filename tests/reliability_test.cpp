// Reliability on real connections (docs/architecture/netcode.md,
// "Reliability"): large reliable messages cut into fragments and put back
// whole over a lossy link, a session kept alive by ENet's pings alone, an
// outage shorter than the timeout ridden out, a longer one ending the
// session cleanly and a new join working once the link is back.

#include "slowdown.hpp"
#include "tandem/endpoint.hpp"
#include "tandem/shaping.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <functional>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
// 150 ms each way, 5 % lost in bursts, some overtaken: the worst link the
// netcode is tuned for.
constexpr net::LinkShape kWorst{
    .latencyMs = 150, .jitterMs = 50, .loss = 0.05f, .reorder = 0.03f, .burst = 4};

struct Pair {
  std::unique_ptr<Endpoint> host, guest;
  std::unique_ptr<net::Shaper> shaper;
  bool guestEnded{};
  std::vector<events::Story> stories;
  std::uint32_t states{};

  Pair(const net::LinkShape &shape, Timing timing = {}) {
    std::string error;
    host = Endpoint::Host({"host"}, 0, error, timing);
    REQUIRE_MESSAGE(host, error);
    shaper = net::Shaper::Start(0, "127.0.0.1", host->LocalPort(), shape, error, 77);
    REQUIRE_MESSAGE(shaper, error);
    Join(timing);
  }
  void Join(Timing timing) {
    std::string error;
    guest = Endpoint::Join({"guest"}, "127.0.0.1", shaper->Port(), error, timing);
    REQUIRE_MESSAGE(guest, error);
    guestEnded = false;
  }
  bool Pump(std::chrono::milliseconds time, const std::function<bool()> &done) {
    const auto deadline = std::chrono::steady_clock::now() + time;
    while (std::chrono::steady_clock::now() < deadline && !done()) {
      host->Update(1ms);
      for (auto &event : guest->Update(1ms)) {
        if (std::holds_alternative<events::Ended>(event))
          guestEnded = true;
        else if (auto *story = std::get_if<events::Story>(&event))
          stories.push_back(std::move(*story));
        else if (std::holds_alternative<events::RemoteState>(event))
          ++states;
      }
    }
    return done();
  }
  bool Joined() { return guest->IsJoined() && host->Players().size() == 2; }
};

proto::Journal Snapshot(std::size_t entries) {
  proto::Journal journal{.snapshot = true};
  for (std::size_t i = 0; i < entries; ++i)
    journal.entries.push_back(
        {.hash = static_cast<std::uint32_t>(0x9E3779B9u * (i + 1)), .state = 2, .notify = 0});
  return journal;
}
} // namespace

TEST_CASE("a large journal snapshot crosses a bad link whole, in fragments") {
  for (const std::size_t entries : {std::size_t{1000}, proto::kMaxJournalEntries}) {
    CAPTURE(entries);
    Pair pair(kWorst);
    REQUIRE(pair.Pump(test::Patience(15s), [&] { return pair.Joined(); }));
    const auto snapshot = Snapshot(entries);
    const auto bytes = proto::Encode(snapshot).size();
    const auto start = std::chrono::steady_clock::now();
    pair.host->SendStory(snapshot);
    const auto whole = [&] {
      for (const auto &story : pair.stories)
        if (const auto *journal = std::get_if<proto::Journal>(&story.message);
            journal && journal->snapshot && journal->entries.size() == entries)
          return true;
      return false;
    };
    REQUIRE(pair.Pump(test::Patience(30s), whole));
    const auto took = std::chrono::duration<double>(std::chrono::steady_clock::now() - start);
    MESSAGE(entries, " entries (", bytes / 1024, " KiB, ", (bytes + 1023) / 1024,
            " fragments of 1 KiB or so) across 150 ms / 5 %: ", took.count(), " s");
    for (const auto &story : pair.stories)
      if (const auto *journal = std::get_if<proto::Journal>(&story.message);
          journal && journal->snapshot)
        CHECK(*journal == snapshot);
    // A thousand entries are a join's worth: there in a few round trips.
    if (entries == 1000)
      CHECK(took.count() < 3.0);
    CHECK_FALSE(pair.guestEnded);
  }
}

TEST_CASE("a quiet session stays up on pings alone, over a lossy link") {
  const Timing timing{.timeout = 2'000ms, .takeover = 800ms};
  Pair pair({.latencyMs = 80, .jitterMs = 20, .loss = 0.05f, .burst = 3}, timing);
  REQUIRE(pair.Pump(test::Patience(10s), [&] { return pair.Joined(); }));
  pair.Pump(6s, [] { return false; }); // three timeouts with nothing to say
  CHECK_FALSE(pair.guestEnded);
  CHECK(pair.Joined());
}

TEST_CASE("an outage shorter than the timeout is ridden out; a longer one ends, and joining works "
          "again") {
  const Timing timing{.timeout = 2'000ms, .takeover = 800ms};
  Pair pair({.latencyMs = 30, .jitterMs = 10, .loss = 0.01f}, timing);
  REQUIRE(pair.Pump(test::Patience(10s), [&] { return pair.Joined(); }));
  const auto send = [&](int frames) {
    for (int i = 0; i < frames; ++i) {
      pair.guest->SendLocalState({.position = {static_cast<float>(i), 0.f, 0.f}});
      pair.host->SendLocalState({.position = {0.f, static_cast<float>(i), 0.f}});
      pair.Pump(33ms, [] { return false; });
    }
  };
  send(10);
  // 1.2 s of nothing either way (a Wi-Fi drop): the session holds, and the
  // states flow again.
  pair.shaper->SetOutage(true);
  send(36);
  pair.shaper->SetOutage(false);
  const auto before = pair.states;
  send(30);
  CHECK_FALSE(pair.guestEnded);
  CHECK(pair.Joined());
  CHECK(pair.states > before + 10);
  // 3 s: longer than the timeout. Both sides end it on their own.
  pair.shaper->SetOutage(true);
  REQUIRE(pair.Pump(test::Patience(6s),
                    [&] { return pair.guestEnded && pair.host->Players().size() == 1; }));
  pair.shaper->SetOutage(false);
  // The game's net thread joins again (net_thread.cpp, every 5 s): a new
  // session, the player back in.
  pair.Join(timing);
  CHECK(pair.Pump(test::Patience(10s), [&] { return pair.Joined(); }));
}
