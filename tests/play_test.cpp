// Gameplay recordings (tandem/play.hpp): the file, the replay's timing and
// mapping, and a recording made through one session replayed through another.

#include "ports.hpp"
#include "slowdown.hpp"
#include "tandem/endpoint.hpp"
#include "tandem/play.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <functional>
#include <string>
#include <unistd.h>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
std::filesystem::path TempFile(const std::string &name) {
  return std::filesystem::temp_directory_path() /
         ("tandem-" + std::to_string(getpid()) + "-" + name + ".tplay");
}

constexpr std::uint64_t kStart = 1'800'000'000'000'000;

bool PumpUntil(std::vector<Endpoint *> endpoints,
               const std::function<void(std::size_t, const SessionEvent &)> &seen,
               const std::function<bool()> &done, std::chrono::milliseconds limit = 3s) {
  const auto deadline = std::chrono::steady_clock::now() + test::Patience(limit);
  while (std::chrono::steady_clock::now() < deadline) {
    for (std::size_t i = 0; i < endpoints.size(); ++i)
      for (const auto &event : endpoints[i]->Update(1ms))
        seen(i, event);
    if (done())
      return true;
  }
  return false;
}

// One step of what a V's game reports, as a guest would see it.
struct Seen {
  std::string what;
  float x{};
  std::uint64_t id{};
  friend bool operator==(const Seen &, const Seen &) = default;
};

std::optional<Seen> SeenOf(const SessionEvent &event) {
  if (const auto *e = std::get_if<events::RemoteState>(&event))
    return Seen{"state", e->state.position.x, e->state.stance};
  if (const auto *e = std::get_if<events::RemoteAppearance>(&event))
    return Seen{"look", 0.f, e->appearance.items.empty() ? 0 : e->appearance.items[0]};
  if (const auto *e = std::get_if<events::RemoteHeld>(&event))
    return Seen{"held", 0.f, e->held.right};
  if (const auto *e = std::get_if<events::RemoteShot>(&event))
    return Seen{"shot", 0.f, e->shot.effect};
  if (const auto *e = std::get_if<events::RemoteSeat>(&event))
    return Seen{"seat", 0.f, e->seat.vehicle};
  if (const auto *e = std::get_if<events::RemoteVehicle>(&event))
    return Seen{"car", e->vehicle.position.x, 0};
  return std::nullopt;
}
} // namespace

TEST_CASE("a recording keeps what a V's game says about its own V") {
  CHECK(play::Keeps(proto::PlayerState{}));
  CHECK(play::Keeps(proto::PoseState{}));
  CHECK(play::Keeps(proto::Seat{}));
  CHECK(play::Keeps(proto::NpcStrike{}));
  CHECK(play::Keeps(proto::TimeEffect{}));
  CHECK_FALSE(play::Keeps(proto::NpcStates{}));
  CHECK_FALSE(play::Keeps(proto::Ping{}));
  CHECK_FALSE(play::Keeps(proto::World{}));
}

TEST_CASE("a recording file round-trips, in order, times relative to its start") {
  const auto path = TempFile("roundtrip");
  std::string error;
  play::Writer writer;
  REQUIRE_MESSAGE(
      writer.Open(path, {.startHostUs = kStart, .player = 3, .body = proto::Body::Male}, error),
      error);
  writer.Add(kStart - 50, proto::Held{.right = 7}); // before the start: at 0
  writer.Add(kStart + 1000, proto::PlayerState{.position = {1, 2, 3}});
  writer.Add(kStart + 500, proto::Shot{.effect = 16}); // late: never back in time
  writer.Add(kStart + 2'000'000, proto::Seat{.vehicle = 9, .owner = 3});
  writer.Close();

  const auto recording = play::Load(path, error);
  REQUIRE_MESSAGE(recording, error);
  CHECK(recording->header.player == 3);
  CHECK(recording->header.body == proto::Body::Male);
  CHECK(recording->header.startHostUs == kStart);
  REQUIRE(recording->entries.size() == 4);
  CHECK(recording->entries[0].atUs == 0);
  CHECK(recording->entries[1].atUs == 1000);
  CHECK(recording->entries[2].atUs == 1000);
  CHECK(recording->DurationUs() == 2'000'000);
  CHECK(std::get<proto::Seat>(recording->entries[3].message).vehicle == 9);
  REQUIRE(recording->FirstState());
  CHECK(recording->FirstState()->position == proto::Vec3{1, 2, 3});
  std::filesystem::remove(path);

  CHECK_FALSE(play::Load(TempFile("missing"), error));
}

TEST_CASE("a replay hands each message over when its time comes, lap after lap") {
  play::Recording recording;
  recording.entries = {{0, proto::PlayerState{.seq = 1}},
                       {100'000, proto::PlayerState{.seq = 2}},
                       {300'000, proto::PlayerState{.seq = 3}}};
  play::Player once(recording);
  CHECK(once.Due(0).size() == 1);
  CHECK(once.Due(50'000).empty());
  CHECK(once.Due(300'000).size() == 2);
  CHECK(once.Due(10'000'000).empty());
  CHECK(once.Done());

  play::Player looping(recording, true, 200'000);
  CHECK(looping.Due(300'000).size() == 3);
  CHECK(looping.Due(400'000).empty()); // the gap between laps
  const auto second = looping.Due(500'000);
  CHECK(second.size() == 1);
  CHECK(looping.Lap() == 1);
  CHECK(looping.LapStartUs() == 500'000);
  CHECK_FALSE(looping.Done());
}

TEST_CASE("a replay stands for the recorded player: positions, cars, NPCs, times") {
  play::Mapping mapping{.self = 1, .other = 5, .offset = {10, 0, 0}};
  mapping.npc = [](proto::NpcId recorded, const proto::Vec3 *near) -> std::optional<proto::NpcId> {
    if (recorded == 42)
      return near ? 100 : 101;
    return std::nullopt; // no such NPC here
  };

  const auto state = play::Adapt(proto::PlayerState{.position = {1, 2, 3}}, mapping, 0);
  CHECK(std::get<proto::PlayerState>(*state).position == proto::Vec3{11, 2, 3});
  // Our own car stays ours (the endpoint fills in who we are); a ride in
  // another player's car becomes one in the other player's.
  const auto own = play::Adapt(proto::Seat{.vehicle = 9, .owner = 1}, mapping, 0);
  CHECK(std::get<proto::Seat>(*own).owner == proto::kNoPlayer);
  const auto ride = play::Adapt(proto::Seat{.vehicle = 9, .owner = 2}, mapping, 0);
  CHECK(std::get<proto::Seat>(*ride).owner == 5);
  // NPCs: by where the hit landed, else by id; one not here is dropped.
  const auto hit = play::Adapt(proto::NpcHit{.npc = 42, .position = {0, 0, 0}}, mapping, 0);
  CHECK(std::get<proto::NpcHit>(*hit).npc == 100);
  CHECK(std::get<proto::NpcHit>(*hit).position == proto::Vec3{10, 0, 0});
  CHECK(std::get<proto::NpcAction>(*play::Adapt(proto::NpcAction{.npc = 42}, mapping, 0)).npc ==
        101);
  CHECK_FALSE(play::Adapt(proto::NpcHit{.npc = 7}, mapping, 0));
  // Absolute times move with the replay.
  const auto radio = play::Adapt(proto::VehicleRadio{.trackStartUs = 1000}, mapping, 500);
  CHECK(std::get<proto::VehicleRadio>(*radio).trackStartUs == 1500);
  // Another player's car or V: the other player here, or nothing.
  CHECK(std::get<proto::VehicleHack>(*play::Adapt(proto::VehicleHack{.driver = 2}, mapping, 0))
            .driver == 5);
  play::Mapping alone{.self = 1};
  CHECK_FALSE(play::Adapt(proto::Revive{.target = 2}, alone, 0));
  // Not a V's own message: never replayed.
  CHECK_FALSE(play::Adapt(proto::NpcStates{}, mapping, 0));
}

TEST_CASE("a V's play recorded through one session replays the same through another") {
  const auto path = TempFile("loopback");
  std::string error;

  // Session 1: the recorder hosts, a guest watches. The recorder's game
  // makes its calls; its tap writes them.
  std::vector<Seen> watched;
  {
    const auto port = test::NextPort();
    auto recorder = Endpoint::Host({"recorder", proto::Body::Female}, port, error);
    REQUIRE_MESSAGE(recorder, error);
    auto guest = Endpoint::Join({"guest"}, "127.0.0.1", port, error);
    REQUIRE_MESSAGE(guest, error);
    REQUIRE(PumpUntil(
        {recorder.get(), guest.get()}, [](std::size_t, const SessionEvent &) {},
        [&] { return guest->IsJoined() && recorder->Players().size() == 2; }));

    play::Writer writer;
    REQUIRE(writer.Open(path,
                        {.startHostUs = recorder->HostNowUs(),
                         .player = recorder->You(),
                         .body = recorder->OwnBody()},
                        error));
    recorder->SetTap([&](const proto::Message &m) { writer.Add(recorder->HostNowUs(), m); });

    recorder->SetLocalAppearance({.items = {0xAB}});
    recorder->SetLocalHeld({.right = 0x77});
    for (int i = 0; i < 10; ++i) {
      recorder->SendLocalState(
          {.position = {static_cast<float>(i), 0, 0}, .stance = static_cast<std::uint8_t>(i % 3)});
      if (i == 4)
        recorder->SendLocalShot({.effect = 16});
      PumpUntil(
          {recorder.get(), guest.get()},
          [&](std::size_t who, const SessionEvent &event) {
            if (who == 1)
              if (auto seen = SeenOf(event))
                watched.push_back(*seen);
          },
          [] { return false; }, 20ms);
    }
    recorder->SetLocalSeat({.vehicle = 0x55});
    recorder->SendLocalVehicle({.position = {20, 0, 0}});
    PumpUntil(
        {recorder.get(), guest.get()},
        [&](std::size_t who, const SessionEvent &event) {
          if (who == 1)
            if (auto seen = SeenOf(event))
              watched.push_back(*seen);
        },
        [] { return false; }, 200ms);
    recorder->SetTap({});
    writer.Close();
    CHECK(writer.Count() == 15);
  }

  auto recording = play::Load(path, error);
  REQUIRE_MESSAGE(recording, error);
  CHECK(recording->header.body == proto::Body::Female);
  CHECK(recording->entries.size() == 15);

  // Session 2: tandem-peer's part, a replayer hosts and a guest watches it.
  std::vector<Seen> replayed;
  {
    const auto port = test::NextPort();
    auto replayer = Endpoint::Host({"replay", recording->header.body}, port, error);
    REQUIRE_MESSAGE(replayer, error);
    auto guest = Endpoint::Join({"guest"}, "127.0.0.1", port, error);
    REQUIRE_MESSAGE(guest, error);
    REQUIRE(PumpUntil(
        {replayer.get(), guest.get()}, [](std::size_t, const SessionEvent &) {},
        [&] { return guest->IsJoined() && replayer->Players().size() == 2; }));

    const play::Mapping mapping{.self = recording->header.player};
    play::Player player(std::move(*recording));
    const auto begin = std::chrono::steady_clock::now();
    REQUIRE(PumpUntil(
        {replayer.get(), guest.get()},
        [&](std::size_t who, const SessionEvent &event) {
          if (who == 1)
            if (auto seen = SeenOf(event))
              replayed.push_back(*seen);
        },
        [&] {
          const auto elapsed =
              static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                             std::chrono::steady_clock::now() - begin)
                                             .count());
          for (const auto *entry : player.Due(elapsed))
            if (auto message = play::Adapt(entry->message, mapping, 0))
              play::Apply(*replayer, *message);
          return player.Done() && replayed.size() >= watched.size();
        },
        5s));
  }
  std::filesystem::remove(path);

  // The same, in the same order on each channel (states and events travel
  // apart: their relative order is the network's).
  REQUIRE(watched.size() == 15);
  const auto only = [](const std::vector<Seen> &all, const std::string &what) {
    std::vector<Seen> out;
    for (const auto &seen : all)
      if (seen.what == what)
        out.push_back(seen);
    return out;
  };
  for (const auto *what : {"state", "look", "held", "shot", "seat", "car"})
    CHECK_MESSAGE(only(replayed, what) == only(watched, what), what);
  CHECK(only(watched, "state").size() == 10);
}

namespace {
// What a guest gets from its host, in one line each.
std::optional<Seen> HeardOf(const SessionEvent &event) {
  if (const auto *e = std::get_if<events::RemoteState>(&event))
    return Seen{"state", e->state.position.x, e->state.player};
  if (const auto *e = std::get_if<events::RemoteAppearance>(&event))
    return Seen{"look", 0.f, e->appearance.items.empty() ? 0 : e->appearance.items[0]};
  if (const auto *e = std::get_if<events::HostNpcSpawn>(&event))
    return Seen{"spawn", e->spawn.position.x, e->spawn.npc};
  if (const auto *e = std::get_if<events::HostNpcStates>(&event))
    return Seen{"npcs", e->states.npcs.empty() ? 0.f : e->states.npcs[0].position.x,
                e->states.npcs.size()};
  if (const auto *e = std::get_if<events::HostNpcGone>(&event))
    return Seen{"gone", 0.f, e->gone.npc};
  if (const auto *e = std::get_if<events::HostWorld>(&event))
    return Seen{"world", 0.f, e->world.weather};
  if (const auto *e = std::get_if<events::HitByNpc>(&event))
    return Seen{"hit", e->hit.physical, e->hit.npc};
  if (const auto *e = std::get_if<events::Story>(&event))
    if (const auto *journal = std::get_if<proto::Journal>(&e->message);
        journal && !journal->snapshot)
      return Seen{"journal", 0.f, journal->entries.empty() ? 0 : journal->entries[0].hash};
  return std::nullopt;
}
} // namespace

TEST_CASE("a session recording keeps a host's world, not the transport's") {
  CHECK(play::KeepsSession(proto::NpcSpawn{}));
  CHECK(play::KeepsSession(proto::PlayerHit{}));
  CHECK(play::KeepsSession(proto::Journal{}));
  CHECK_FALSE(play::KeepsSession(proto::Welcome{}));
  CHECK_FALSE(play::KeepsSession(proto::Digest{}));
  CHECK_FALSE(play::KeepsSession(proto::Pong{}));

  const play::SessionMapping mapping{.host = 1, .you = 3, .self = 1, .guest = 2};
  // The host's V is ours; another guest's is not replayed.
  CHECK(play::AdaptSession(proto::PlayerState{.player = 1}, mapping, 0));
  CHECK_FALSE(play::AdaptSession(proto::PlayerState{.player = 4}, mapping, 0));
  // A hit on the recording guest lands on the real one; on another, nowhere.
  const auto hit = play::AdaptSession(proto::PlayerHit{.player = 3, .npc = 9}, mapping, 0);
  CHECK(std::get<proto::PlayerHit>(*hit).player == 2);
  CHECK_FALSE(play::AdaptSession(proto::PlayerHit{.player = 4}, mapping, 0));
  // Who an NPC has detected follows the players.
  const auto states = play::AdaptSession(proto::NpcStates{.npcs = {{.npc = 9, .detected = 0b101}}},
                                         mapping, 0);                   // players 1 and 3
  CHECK(std::get<proto::NpcStates>(*states).npcs[0].detected == 0b011); // players 1 and 2
  // A timed hub's start moves with the replay.
  const auto hub = play::AdaptSession(proto::ChoiceHub{.timedStartUs = 100}, mapping, 50);
  CHECK(std::get<proto::ChoiceHub>(*hub).timedStartUs == 150);
  // The host's word on a device is replayed, by whoever of this session did
  // it (another guest's doing: the world's); the bookkeeping is not.
  const auto door = play::AdaptSession(
      proto::DeviceState{.entity = 7, .cls = 8, .flags = proto::kDeviceOpen, .by = 3}, mapping, 0);
  REQUIRE(door);
  CHECK(std::get<proto::DeviceState>(*door).by == 2);
  const auto other =
      play::AdaptSession(proto::DeviceState{.entity = 7, .cls = 8, .by = 4}, mapping, 0);
  REQUIRE(other);
  CHECK(std::get<proto::DeviceState>(*other).by == proto::kNoPlayer);
  CHECK_FALSE(play::AdaptSession(proto::DeviceState{.how = proto::kDeviceAsk}, mapping, 0));
  CHECK_FALSE(play::KeepsSession(proto::DeviceUse{}));
  CHECK_FALSE(play::KeepsSession(proto::DeviceDigest{}));
  CHECK_FALSE(play::KeepsSession(proto::DeviceSet{}));
}

TEST_CASE("a host's session recorded by one guest replays the same to another") {
  const auto path = TempFile("session");
  std::string error;
  std::vector<Seen> heard;
  {
    const auto port = test::NextPort();
    auto host = Endpoint::Host({"host", proto::Body::Male}, port, error);
    REQUIRE_MESSAGE(host, error);
    host->SetLocalAppearance({.body = proto::Body::Male, .items = {0xAB}});
    host->SetWorld({.gameTimeSeconds = 3600, .weather = 0x77});
    auto guest = Endpoint::Join({"recorder"}, "127.0.0.1", port, error);
    REQUIRE_MESSAGE(guest, error);

    // The recording guest's side (tandem-peer join --record-session).
    play::Writer writer;
    std::vector<proto::Message> early;
    guest->SetReceivedTap([&](const proto::Message &m) {
      if (!play::KeepsSession(m))
        return;
      if (writer.IsOpen())
        writer.Add(guest->HostNowUs(), m);
      else
        early.push_back(m);
    });
    const auto seen = [&](std::size_t who, const SessionEvent &event) {
      if (who == 1)
        if (auto line = HeardOf(event))
          heard.push_back(*line);
    };
    REQUIRE(PumpUntil({host.get(), guest.get()}, seen,
                      [&] { return guest->IsJoined() && host->Players().size() == 2; }));
    REQUIRE(writer.Open(path,
                        {.startHostUs = guest->HostNowUs(),
                         .player = guest->You(),
                         .kind = play::Kind::Session,
                         .host = guest->HostPlayer()},
                        error));
    for (const auto &m : early)
      writer.Add(guest->HostNowUs(), m);

    // The real host plays: its V, an NPC that walks, hits our guest and goes,
    // a journal change.
    host->SendNpcSpawn({.npc = 7, .record = 0x55, .position = {5, 0, 0}});
    for (int i = 0; i < 6; ++i) {
      host->SendLocalState({.position = {static_cast<float>(i), 0, 0}});
      host->SendNpcStates({.npcs = {{.npc = 7, .position = {5.f + i, 0, 0}}}});
      if (i == 2)
        CHECK(host->SendPlayerHit({.player = guest->You(), .npc = 7, .physical = 10.f}));
      if (i == 3)
        host->SendStory(proto::Journal{.entries = {{.hash = 0xBEEF, .state = 1}}});
      PumpUntil({host.get(), guest.get()}, seen, [] { return false; }, 30ms);
    }
    host->SendNpcGone({.npc = 7, .reason = proto::kGoneAway});
    PumpUntil({host.get(), guest.get()}, seen, [] { return false; }, 300ms);
    writer.Close();
  }

  auto recording = play::Load(path, error);
  REQUIRE_MESSAGE(recording, error);
  CHECK(recording->header.kind == play::Kind::Session);
  CHECK(recording->header.host == 1);

  // tandem-peer host --play-session: a mock host replays it to a real guest.
  std::vector<Seen> replayed;
  {
    const auto port = test::NextPort();
    auto replayer = Endpoint::Host({"replay", proto::Body::Male}, port, error);
    REQUIRE_MESSAGE(replayer, error);
    auto guest = Endpoint::Join({"guest"}, "127.0.0.1", port, error);
    REQUIRE_MESSAGE(guest, error);
    const auto seen = [&](std::size_t who, const SessionEvent &event) {
      if (who == 1)
        if (auto line = HeardOf(event))
          replayed.push_back(*line);
    };
    REQUIRE(PumpUntil({replayer.get(), guest.get()}, seen,
                      [&] { return guest->IsJoined() && replayer->Players().size() == 2; }));
    const play::SessionMapping mapping{.host = recording->header.host,
                                       .you = recording->header.player,
                                       .self = replayer->You(),
                                       .guest = guest->You()};
    play::Player player(std::move(*recording));
    const auto begin = std::chrono::steady_clock::now();
    PumpUntil(
        {replayer.get(), guest.get()}, seen,
        [&] {
          const auto elapsed =
              static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                             std::chrono::steady_clock::now() - begin)
                                             .count());
          for (const auto *entry : player.Due(elapsed))
            if (auto message = play::AdaptSession(entry->message, mapping, 0))
              play::ApplyHost(*replayer, *message, mapping.guest);
          return player.Done() && replayed.size() >= heard.size();
        },
        5s);
  }
  std::filesystem::remove(path);

  const auto only = [](const std::vector<Seen> &all, const std::string &what) {
    std::vector<Seen> out;
    for (const auto &seen : all)
      if (seen.what == what)
        out.push_back(seen);
    return out;
  };
  for (const auto *what : {"state", "look", "spawn", "npcs", "gone", "world", "hit", "journal"}) {
    CHECK_MESSAGE(!only(heard, what).empty(), what);
    CHECK_MESSAGE(only(replayed, what) == only(heard, what), what);
  }
}
