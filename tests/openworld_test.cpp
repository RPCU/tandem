// The host's open-world activities on a guest's map, without the game
// (docs/architecture/story.md, "Open-world activities"): the host's done set,
// the guest's mirror (what it sets, the only pins it puts back, what it
// credits), the story record (late joiners, the digest, a repeat) and the
// message through real endpoints over loopback UDP (stamped, sorted, a late
// joiner, the explicit none of a change, never from a guest).

#include "ports.hpp"
#include "slowdown.hpp"
#include "tandem/endpoint.hpp"
#include "tandem/openworld.hpp"
#include "tandem/story.hpp"

#include <doctest/doctest.h>

#include <functional>
#include <map>
#include <optional>

using namespace std::chrono_literals;
using namespace tandem;
using namespace tandem::openworld;

namespace {
using test::NextPort;

constexpr Timing kFast{.timeout = 1000ms, .takeover = 400ms};
constexpr std::uint32_t kKabuki = 0x1001, kNorthside = 0x1002, kOwnDone = 0x1003, kFar = 0x1004;

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

// A guest's pins as the plugin sees them: each marker's phase now.
struct Map {
  std::map<std::uint32_t, std::uint8_t> phase;
  std::vector<Seen> Pins() const {
    std::vector<Seen> out;
    for (const auto &[marker, p] : phase)
      out.push_back({marker, p});
    return out;
  }
  void Apply(const std::vector<Step> &steps) {
    for (const auto &step : steps)
      if (phase.contains(step.marker))
        phase[step.marker] = step.phase;
  }
};

const proto::OpenWorldDone *DoneIn(const std::vector<SessionEvent> &events) {
  for (const auto &event : events)
    if (const auto *story = std::get_if<events::Story>(&event))
      if (const auto *set = std::get_if<proto::OpenWorldDone>(&story->message))
        return set;
  return nullptr;
}
} // namespace

TEST_CASE("open world: the host's done set is its completed pins, one way") {
  const std::vector<Seen> pins{{kNorthside, kCompleted}, {kKabuki, kCompleted},
                               {0, kCompleted},          {kFar, kDiscovered},
                               {kKabuki, kCompleted},    {kOwnDone, kDefault}};
  CHECK(DoneOf(pins) == std::vector<std::uint32_t>{kKabuki, kNorthside});
  CHECK(Wanted(true, kDiscovered) == kCompleted);
  CHECK(Wanted(false, kCompleted) == kDiscovered);
  CHECK(Wanted(false, kUndiscovered) == kUndiscovered);
}

TEST_CASE("open world: a grouped guest shows the host's pins and puts back only its own changes") {
  Map map;
  map.phase = {{kKabuki, kDiscovered},
               {kNorthside, kUndiscovered},
               {kOwnDone, kCompleted},
               {kFar, kDefault}};
  Mirror mirror;
  // Nothing heard yet: nothing changes, not even our own done pin.
  CHECK(mirror.Update(true, map.Pins()).empty());

  // The host's first set is its past: nothing to credit.
  CHECK(mirror.Host(proto::OpenWorldDone{.player = 1, .markers = {kKabuki, kNorthside}}).empty());
  map.Apply(mirror.Update(true, map.Pins()));
  CHECK(map.phase[kKabuki] == kCompleted);
  CHECK(map.phase[kNorthside] == kCompleted);
  CHECK(map.phase[kOwnDone] == kDiscovered); // open in the host's world
  CHECK(map.phase[kFar] == kDefault);        // as both worlds have it
  CHECK(mirror.Ours().size() == 3);
  CHECK(mirror.Ours().at(kKabuki) == kDiscovered);
  // A repeat check changes nothing.
  CHECK(mirror.Update(true, map.Pins()).empty());

  // Our game puts a pin back (streamed in again from its own record): set again.
  map.phase[kKabuki] = kDiscovered;
  map.Apply(mirror.Update(true, map.Pins()));
  CHECK(map.phase[kKabuki] == kCompleted);
  CHECK(mirror.Reapplied() == 1);

  // The host finishes kFar with us: credited, shown.
  CHECK(mirror.Host(proto::OpenWorldDone{.player = 1, .markers = {kKabuki, kNorthside, kFar}}) ==
        std::vector<std::uint32_t>{kFar});
  map.Apply(mirror.Update(true, map.Pins()));
  CHECK(map.phase[kFar] == kCompleted);
  // A resync of the same set credits nothing again.
  CHECK(mirror.Host(proto::OpenWorldDone{.player = 1, .markers = {kKabuki, kNorthside, kFar}})
            .empty());

  // Apart: every pin back as our own game has it.
  map.Apply(mirror.Update(false, map.Pins()));
  CHECK(map.phase[kKabuki] == kDiscovered);
  CHECK(map.phase[kNorthside] == kUndiscovered);
  CHECK(map.phase[kOwnDone] == kCompleted);
  CHECK(map.phase[kFar] == kDefault);
  CHECK(mirror.Ours().empty());

  // Grouped again, then the session ends: back again, the host forgotten.
  map.Apply(mirror.Update(true, map.Pins()));
  CHECK(map.phase[kNorthside] == kCompleted);
  map.Apply(mirror.Lift());
  CHECK(map.phase[kNorthside] == kUndiscovered);
  CHECK(map.phase[kOwnDone] == kCompleted);
  CHECK(mirror.HostSet().empty());
  CHECK(mirror.Update(true, map.Pins()).empty());
}

TEST_CASE("open world: a pin the host's world matches again is put back and forgotten") {
  Map map;
  map.phase = {{kOwnDone, kCompleted}};
  Mirror mirror;
  mirror.Host(proto::OpenWorldDone{.player = 1});
  map.Apply(mirror.Update(true, map.Pins()));
  CHECK(map.phase[kOwnDone] == kDiscovered);
  // The host's world finishes it too: it shows done, and is ours no longer.
  mirror.Host(proto::OpenWorldDone{.player = 1, .markers = {kOwnDone}});
  map.Apply(mirror.Update(true, map.Pins()));
  CHECK(map.phase[kOwnDone] == kCompleted);
  CHECK(mirror.Ours().empty());
  CHECK(mirror.Lift().empty());
}

TEST_CASE("open world: the story record keeps the host's set, one change at a time") {
  story::Record record;
  const auto unsaid = record.Digest();
  // Nothing said: a joiner is told nothing.
  for (const auto &message : record.ForJoiner())
    CHECK_FALSE(std::holds_alternative<proto::OpenWorldDone>(message));
  // The first word, an empty one too, is news; its repeat is not.
  CHECK(record.Note(proto::OpenWorldDone{.player = 1}));
  CHECK_FALSE(record.Note(proto::OpenWorldDone{.player = 1}));
  const auto none = record.Digest();
  CHECK(none != unsaid);
  const auto saidNone = [&] {
    for (const auto &message : record.ForJoiner())
      if (const auto *set = std::get_if<proto::OpenWorldDone>(&message))
        return set->markers.empty();
    return false;
  };
  CHECK(saidNone());
  CHECK(record.Note(proto::OpenWorldDone{.player = 1, .markers = {kKabuki}}));
  CHECK_FALSE(record.Note(proto::OpenWorldDone{.player = 1, .markers = {kKabuki}}));
  CHECK(record.Digest() != none);
  bool sent = false;
  for (const auto &message : record.ForJoiner())
    if (const auto *set = std::get_if<proto::OpenWorldDone>(&message))
      sent = set->markers == std::vector<std::uint32_t>{kKabuki};
  CHECK(sent);
  // None again: the digest of a record that heard none, and a joiner told so.
  CHECK(record.Note(proto::OpenWorldDone{.player = 1}));
  CHECK(record.Digest() == none);
  CHECK(saidNone());
}

TEST_CASE("open world: the host's set through the endpoints") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
  REQUIRE((host && a));
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t, const auto &) { return a->IsJoined(); }));

  // Unsorted, a repeat and a zero inside: the guest hears one sorted set,
  // stamped with the host. A guest cannot speak for the host's world.
  host->SendStory(proto::OpenWorldDone{.player = 9, .markers = {kNorthside, kKabuki, 0, kKabuki}});
  a->SendStory(proto::OpenWorldDone{.markers = {kOwnDone}});
  std::optional<proto::OpenWorldDone> heard;
  std::size_t hostHeard = 0;
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1) {
      if (const auto *set = DoneIn(events))
        heard = *set;
    } else if (DoneIn(events)) {
      ++hostHeard;
    }
    return heard.has_value();
  }));
  CHECK(heard->player == 1);
  CHECK(heard->markers == std::vector<std::uint32_t>{kKabuki, kNorthside});
  CHECK(hostHeard == 0);

  // A late joiner gets the host's set.
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error, kFast);
  REQUIRE(b);
  std::optional<proto::OpenWorldDone> atB;
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    if (i == 2)
      if (const auto *set = DoneIn(events))
        atB = *set;
    return atB.has_value();
  }));
  CHECK(atB->markers == std::vector<std::uint32_t>{kKabuki, kNorthside});

  // The host's world says none (a new game loaded): none, for everyone.
  host->SendStory(proto::OpenWorldDone{});
  bool emptyAtA = false, emptyAtB = false;
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    if (const auto *set = DoneIn(events); set && set->markers.empty()) {
      if (i == 1)
        emptyAtA = true;
      if (i == 2)
        emptyAtB = true;
    }
    return emptyAtA && emptyAtB;
  }));
}

TEST_CASE("open world: a host whose world has done none says so to the guests") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
  REQUIRE((host && a));
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t, const auto &) { return a->IsJoined(); }));
  // Its first word, empty: news all the same (the guest's mirror waits for it).
  host->SendStory(proto::OpenWorldDone{});
  bool none = false;
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    if (const auto *set = DoneIn(events); i == 1 && set && set->markers.empty())
      none = true;
    return none;
  }));
  // A late joiner hears the same none.
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error, kFast);
  REQUIRE(b);
  bool noneAtB = false;
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    if (const auto *set = DoneIn(events); i == 2 && set && set->markers.empty())
      noneAtB = true;
    return noneAtB;
  }));
}
