// A real host and real guests over loopback UDP, in one process.

#include "ports.hpp"
#include "tandem/endpoint.hpp"

#include <doctest/doctest.h>

#include <functional>
#include <map>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
using test::NextPort;

template <class E> bool Has(const std::vector<SessionEvent> &events) {
  for (const auto &event : events)
    if (std::holds_alternative<E>(event))
      return true;
  return false;
}

// Runs every endpoint until `done` holds or `limit` passes. `wait` is each
// endpoint's turn; 0 busy-loops, so no endpoint waits on another's turn.
bool Pump(std::vector<Endpoint *> endpoints,
          const std::function<bool(std::size_t, const std::vector<SessionEvent> &)> &done,
          std::chrono::milliseconds wait = 1ms, std::chrono::milliseconds limit = 2s) {
  const auto deadline = std::chrono::steady_clock::now() + limit;
  bool finished = false;
  while (!finished && std::chrono::steady_clock::now() < deadline) {
    for (std::size_t i = 0; i < endpoints.size(); ++i)
      finished = done(i, endpoints[i]->Update(wait)) || finished;
  }
  return finished;
}

using Seen = std::function<void(std::size_t, const std::vector<SessionEvent> &)>;

// Runs the endpoints for `time`, whatever happens.
void PumpFor(
    std::vector<Endpoint *> endpoints, std::chrono::milliseconds time,
    const Seen &seen = [](std::size_t, const auto &) {}) {
  Pump(
      std::move(endpoints),
      [&](std::size_t i, const auto &events) {
        seen(i, events);
        return false;
      },
      1ms, time);
}

// Runs the endpoints until endpoint `which` reports the end of its session.
std::optional<std::string> EndOf(std::vector<Endpoint *> endpoints, std::size_t which,
                                 std::chrono::milliseconds limit = 3s) {
  std::optional<std::string> reason;
  Pump(
      std::move(endpoints),
      [&](std::size_t i, const auto &events) {
        for (const auto &event : events)
          if (const auto *ended = std::get_if<events::Ended>(&event); ended && i == which)
            reason = ended->reason;
        return reason.has_value();
      },
      1ms, limit);
  return reason;
}

double SecondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// Liveness tests run on a shorter clock than play (5 s, 2 s): the same rules,
// scaled down so the suite stays fast.
constexpr Timing kFast{.timeout = 1000ms, .takeover = 400ms};
} // namespace

TEST_CASE("a guest joins, both see each other's V") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  REQUIRE_MESSAGE(host, error);
  auto guest = Endpoint::Join({"guest"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(guest, error);

  bool guestJoined = false, hostSawGuest = false;
  REQUIRE(Pump({host.get(), guest.get()}, [&](std::size_t i, const auto &events) {
    if (i == 0)
      hostSawGuest |= Has<events::PlayerJoined>(events);
    else
      guestJoined |= Has<events::Joined>(events);
    return guestJoined && hostSawGuest;
  }));
  CHECK(guest->You() == 2);
  CHECK(host->Players().size() == 2);

  proto::PlayerState fromGuest;
  fromGuest.player = 99; // a guest can't speak for someone else
  fromGuest.position = {1.f, 2.f, 3.f};
  guest->SendLocalState(fromGuest);
  host->SendLocalState(proto::PlayerState{.position = {7.f, 8.f, 9.f}});

  std::optional<proto::PlayerState> atHost, atGuest;
  REQUIRE(Pump({host.get(), guest.get()}, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *remote = std::get_if<events::RemoteState>(&event))
        (i == 0 ? atHost : atGuest) = remote->state;
    return atHost && atGuest;
  }));
  CHECK(atHost->player == 2);
  CHECK(atHost->position == proto::Vec3{1.f, 2.f, 3.f});
  CHECK(atGuest->player == 1);
  CHECK(atGuest->position == proto::Vec3{7.f, 8.f, 9.f});
}

TEST_CASE("the host relays one guest to the other") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error);
  REQUIRE((host && a && b));

  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t, const auto &) {
    return a->Players().size() == 3 && b->Players().size() == 3;
  }));

  a->SendLocalState(proto::PlayerState{.position = {5.f, 5.f, 5.f}});
  std::optional<proto::PlayerState> atB;
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    if (i == 2)
      for (const auto &event : events)
        if (const auto *remote = std::get_if<events::RemoteState>(&event))
          atB = remote->state;
    return atB.has_value();
  }));
  CHECK(atB->player == a->You());
}

TEST_CASE("guests share the host's clock") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  auto guest = Endpoint::Join({"guest"}, "127.0.0.1", port, error);
  REQUIRE((host && guest));
  // A few pings for the offset to settle. The midpoint estimate is off by at
  // most half the round trip, so measure the method, not the test loop: no waits.
  Pump({host.get(), guest.get()}, [&](std::size_t, const auto &) { return false; }, 0ms);
  const auto skew =
      static_cast<std::int64_t>(guest->HostNowUs()) - static_cast<std::int64_t>(host->HostNowUs());
  CHECK(std::abs(skew) < 500); // under half a millisecond over loopback
}

TEST_CASE("a leaving guest is reported") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  auto guest = Endpoint::Join({"guest"}, "127.0.0.1", port, error);
  REQUIRE((host && guest));
  REQUIRE(Pump({host.get(), guest.get()},
               [&](std::size_t, const auto &) { return host->Players().size() == 2; }));

  guest->Leave();
  REQUIRE(Pump({host.get()},
               [&](std::size_t, const auto &events) { return Has<events::PlayerLeft>(events); }));
  CHECK(host->Players().size() == 1);
}

TEST_CASE("looks travel on join, and a late joiner gets everyone's") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host", proto::Body::Male}, port, error);
  REQUIRE_MESSAGE(host, error);
  host->SetLocalAppearance({.body = proto::Body::Male, .look = {{1, 2}}, .items = {7}});

  // Set before joining: sent as soon as the guest is in.
  auto first = Endpoint::Join({"first"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(first, error);
  first->SetLocalAppearance({.body = proto::Body::Female, .look = {{3, 4}, {5, 6}}});

  std::optional<proto::Appearance> hostHeard, firstHeard;
  REQUIRE(Pump({host.get(), first.get()}, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *a = std::get_if<events::RemoteAppearance>(&event))
        (i == 0 ? hostHeard : firstHeard) = a->appearance;
    return hostHeard && firstHeard;
  }));
  CHECK(hostHeard->player == first->You());
  CHECK(hostHeard->look.size() == 2);
  CHECK(firstHeard->player == 1);
  CHECK(firstHeard->items == std::vector<std::uint64_t>{7});

  // A later guest receives the host's and the first guest's looks.
  auto late = Endpoint::Join({"late"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(late, error);
  std::map<proto::PlayerId, proto::Appearance> lateHeard;
  REQUIRE(Pump({host.get(), first.get(), late.get()}, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *a = std::get_if<events::RemoteAppearance>(&event); a && i == 2)
        lateHeard[a->appearance.player] = a->appearance;
    return lateHeard.size() == 2;
  }));
  CHECK(lateHeard.at(1).body == proto::Body::Male);
  CHECK(lateHeard.at(first->You()).look.size() == 2);
}

TEST_CASE("what a guest holds and its shots reach the host, stamped") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  REQUIRE_MESSAGE(host, error);
  auto guest = Endpoint::Join({"guest"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(guest, error);
  guest->SetLocalHeld({.right = 42}); // before joining: sent once in
  std::optional<proto::Held> held;
  std::optional<proto::Shot> shot;
  bool shotSent = false;
  REQUIRE(Pump({host.get(), guest.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1 && guest->IsJoined() && !shotSent) {
      guest->SendLocalShot({.player = 99, .hand = proto::Hand::Right, .effect = 16});
      shotSent = true;
    }
    for (const auto &event : events) {
      if (const auto *h = std::get_if<events::RemoteHeld>(&event); h && i == 0)
        held = h->held;
      if (const auto *s = std::get_if<events::RemoteShot>(&event); s && i == 0)
        shot = s->shot;
    }
    return held && shot;
  }));
  CHECK(held->right == 42);
  CHECK(held->player == guest->You());
  CHECK(shot->player == guest->You());
  CHECK(shot->effect == 16);
}

TEST_CASE("a guest's hit reaches the host, stamped; health reaches guests, late ones too") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  REQUIRE_MESSAGE(host, error);
  auto guest = Endpoint::Join({"guest"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(guest, error);
  host->SendNpcSpawn({.npc = 7, .record = 1});
  host->SendNpcHealth({.npc = 7, .health = 0.5f}); // before anyone joined: kept

  std::optional<proto::NpcHit> hit;
  std::optional<proto::NpcHealth> health;
  bool hitSent = false;
  REQUIRE(Pump({host.get(), guest.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1 && guest->IsJoined() && !hitSent) {
      guest->SendNpcHit({.player = 99, .npc = 7, .damage = 30.f, .flags = proto::kHitWeakSpot});
      hitSent = true;
    }
    for (const auto &event : events) {
      if (const auto *h = std::get_if<events::NpcHitBy>(&event); h && i == 0)
        hit = h->hit;
      if (const auto *h = std::get_if<events::HostNpcHealth>(&event); h && i == 1)
        health = h->health;
    }
    return hit && health;
  }));
  CHECK(hit->player == guest->You()); // a guest can't hit in someone else's name
  CHECK(hit->npc == 7);
  CHECK(hit->damage == 30.f);
  CHECK(health->health == 0.5f);

  // The host's own "hits" are its game's: nothing is sent.
  host->SendNpcHit({.npc = 7, .damage = 1.f});
  // Gone: no health kept for the next joiner.
  host->SendNpcGone({.npc = 7, .reason = 1});
  auto late = Endpoint::Join({"late"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(late, error);
  bool lateJoined = false, lateGotHealth = false;
  Pump({host.get(), guest.get(), late.get()}, [&](std::size_t i, const auto &events) {
    if (i == 2) {
      lateJoined |= Has<events::Joined>(events);
      lateGotHealth |= Has<events::HostNpcHealth>(events);
    }
    return false; // run the full two seconds: nothing more may arrive
  });
  CHECK(lateJoined);
  CHECK_FALSE(lateGotHealth);
}

TEST_CASE("quickhacks: uploads to the host, states to all, NPC hacks and car hacks to one V") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  REQUIRE_MESSAGE(host, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(a, error);
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(b, error);
  REQUIRE(Pump({host.get(), a.get(), b.get()},
               [&](std::size_t, const auto &) { return a->IsJoined() && b->IsJoined(); }));

  // a uploads on NPC 7: the host hears it in a's name, nobody else does.
  a->SendNpcHack({.player = 99, .npc = 7, .action = 0x55, .uploadMs = 1200});
  std::optional<proto::NpcHack> upload;
  int othersHeard = 0;
  PumpFor({host.get(), a.get(), b.get()}, 300ms, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *h = std::get_if<events::NpcHackBy>(&event)) {
        if (i == 0)
          upload = h->hack;
        else
          ++othersHeard;
      }
  });
  REQUIRE(upload);
  CHECK(upload->player == a->You());
  CHECK(upload->uploadMs == 1200);
  CHECK(othersHeard == 0);

  // The host tells everyone what runs on it; the effect reaches all but
  // the V whose hack it is.
  host->SendNpcHackState({.npc = 7, .player = a->You(), .action = 0x55});
  host->SendNpcStatus({.npc = 7, .player = a->You(), .effect = 0x66, .applied = true});
  // A host netrunner hacks b's V: only b hears it.
  host->SendPlayerHack({.player = b->You(), .npc = 7, .action = 0x77, .flags = 1});
  int states = 0, statusA = 0, statusB = 0, hackA = 0, hackB = 0;
  PumpFor({host.get(), a.get(), b.get()}, 300ms, [&](std::size_t i, const auto &events) {
    for (const auto &event : events) {
      if (std::holds_alternative<events::HostNpcHackState>(event))
        ++states;
      if (std::holds_alternative<events::HostNpcStatus>(event))
        ++(i == 1 ? statusA : statusB);
      if (const auto *h = std::get_if<events::HostPlayerHack>(&event)) {
        ++(i == 1 ? hackA : hackB);
        CHECK(h->hack.action == 0x77);
      }
    }
  });
  CHECK(states == 2);
  CHECK(statusA == 0);
  CHECK(statusB == 1);
  CHECK(hackA == 0);
  CHECK(hackB == 1);

  // a hacks the car b drives: it goes through the host to b, in a's name;
  // and one the host drives stays on the host.
  a->SendVehicleHack({.player = 99, .driver = b->You(), .hack = proto::CarHack::ForceBrakes});
  a->SendVehicleHack({.driver = host->You(), .hack = proto::CarHack::Explode, .seconds = 2.f});
  std::optional<proto::VehicleHack> onB, onHost;
  PumpFor({host.get(), a.get(), b.get()}, 300ms, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *h = std::get_if<events::VehicleHackOn>(&event)) {
        CHECK(i != 1);
        (i == 2 ? onB : onHost) = h->hack;
      }
  });
  REQUIRE(onB);
  CHECK(onB->player == a->You());
  CHECK(onB->hack == proto::CarHack::ForceBrakes);
  REQUIRE(onHost);
  CHECK(onHost->player == a->You());
  CHECK(onHost->seconds == 2.f);
}

TEST_CASE("an NPC's hit on a guest reaches that guest only, whole") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error);
  REQUIRE((host && a && b));
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t, const auto &) {
    return a->Players().size() == 3 && b->Players().size() == 3;
  }));

  const proto::PlayerHit sent{.player = a->You(),
                              .npc = 7,
                              .physical = 10.f,
                              .thermal = 2.f,
                              .attackType = 10,
                              .flags = proto::kHitWeakSpot,
                              .pierce = 0.25f,
                              .position = {1, 2, 3},
                              .direction = {0, 1, 0},
                              .attack = 0x1234,
                              .weapon = 0x5678};
  CHECK(host->SendPlayerHit(sent));
  // Nobody to send to: the host's own V, a player not in the session.
  CHECK_FALSE(host->SendPlayerHit({.player = host->You(), .physical = 1.f}));
  CHECK_FALSE(host->SendPlayerHit({.player = 42, .physical = 1.f}));
  // A guest never sends one: the NPCs are the host's.
  CHECK_FALSE(b->SendPlayerHit({.player = a->You(), .physical = 99.f}));

  std::vector<proto::PlayerHit> atA, atB, atHost;
  PumpFor({host.get(), a.get(), b.get()}, 500ms, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *hit = std::get_if<events::HitByNpc>(&event))
        (i == 0 ? atHost : i == 1 ? atA : atB).push_back(hit->hit);
  });
  REQUIRE(atA.size() == 1);
  CHECK(atA[0] == sent);
  CHECK(atB.empty());
  CHECK(atHost.empty());
}

TEST_CASE("NPC animation states reach guests and late joiners; shots too; guests can't send them") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  REQUIRE_MESSAGE(host, error);
  host->SendNpcSpawn({.npc = 7, .record = 1});
  host->SendNpcSpawn({.npc = 8, .record = 1});
  // Kept before anyone joined; an NPC never announced (9) is dropped.
  host->SendNpcAnims({.npcs = {{.npc = 7, .highLevel = 2, .upperBody = 0, .weapon = 42},
                               {.npc = 9, .highLevel = 2}}});
  host->SendNpcAnims({.npcs = {{.npc = 8, .flags = proto::kNpcAnimInWorkspot, .workspot = 5}}});
  auto guest = Endpoint::Join({"guest"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(guest, error);

  std::map<proto::NpcId, proto::NpcAnim> heard;
  std::optional<proto::NpcShot> shot;
  bool sent = false, hostHeardAnims = false;
  REQUIRE(Pump({host.get(), guest.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1 && guest->IsJoined() && !sent) {
      // A guest's NPC messages are refused: it owns no NPC.
      guest->SendNpcAnims({.npcs = {{.npc = 7, .highLevel = 4}}});
      sent = true;
    }
    if (i == 0 && heard.size() == 2 && !shot)
      host->SendNpcShot({.npc = 7, .effect = 16});
    for (const auto &event : events) {
      if (const auto *a = std::get_if<events::HostNpcAnims>(&event); a && i == 1)
        for (const auto &anim : a->anims.npcs)
          heard[anim.npc] = anim;
      if (const auto *s = std::get_if<events::HostNpcShot>(&event); s && i == 1)
        shot = s->shot;
      hostHeardAnims |= i == 0 && Has<events::HostNpcAnims>(events);
    }
    return heard.size() == 2 && shot;
  }));
  CHECK_FALSE(heard.contains(9));
  CHECK(heard.at(7).weapon == 42);
  CHECK(heard.at(7).highLevel == 2);
  CHECK(heard.at(8).workspot == 5);
  CHECK(shot->effect == 16);
  CHECK_FALSE(hostHeardAnims);

  // Gone: its state is not handed to the next joiner.
  host->SendNpcGone({.npc = 8});
  auto late = Endpoint::Join({"late"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(late, error);
  std::map<proto::NpcId, proto::NpcAnim> lateHeard;
  Pump({host.get(), guest.get(), late.get()}, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *a = std::get_if<events::HostNpcAnims>(&event); a && i == 2)
        for (const auto &anim : a->anims.npcs)
          lateHeard[anim.npc] = anim;
    return false; // the full two seconds: nothing more may arrive
  });
  CHECK(lateHeard.size() == 1);
  CHECK(lateHeard.contains(7));
}

namespace {
// The pause changes an endpoint reported, in order.
void CollectPauses(const std::vector<SessionEvent> &events, std::vector<proto::Pause> &into) {
  for (const auto &event : events)
    if (const auto *p = std::get_if<events::PauseChanged>(&event))
      into.push_back(p->pause);
}
} // namespace

TEST_CASE("a guest's pause menu reaches the host and the other guest, stamped") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error);
  REQUIRE((host && a && b));
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t, const auto &) {
    return a->Players().size() == 3 && b->Players().size() == 3;
  }));

  a->SetLocalPaused(true);
  std::vector<proto::Pause> atHost, atA, atB;
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    CollectPauses(events, i == 0 ? atHost : i == 1 ? atA : atB);
    return !atHost.empty() && !atB.empty();
  }));
  CHECK(atHost.front() == proto::Pause{a->You(), true});
  CHECK(atB.front() == proto::Pause{a->You(), true});
  CHECK(atA.empty()); // never told about its own
  CHECK(host->Paused() == std::set<proto::PlayerId>{a->You()});
  CHECK(b->Paused() == std::set<proto::PlayerId>{a->You()});

  // The host's own pause reaches both guests; a resumes.
  host->SetLocalPaused(true);
  a->SetLocalPaused(false);
  atHost.clear(), atA.clear(), atB.clear();
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    CollectPauses(events, i == 0 ? atHost : i == 1 ? atA : atB);
    return !atHost.empty() && atB.size() == 2 && !atA.empty();
  }));
  CHECK(atHost.front() == proto::Pause{a->You(), false});
  CHECK(atA.front() == proto::Pause{1, true});
  CHECK(b->Paused() == std::set<proto::PlayerId>{1});
  CHECK(host->Paused() == std::set<proto::PlayerId>{1});
}

TEST_CASE("a guest cannot pause or resume in someone else's name") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  REQUIRE_MESSAGE(host, error);
  host->SetLocalPaused(true);
  // A raw connection, to send what the Endpoint API never would.
  auto raw = net::Session::Connect("127.0.0.1", port, error);
  REQUIRE_MESSAGE(raw, error);

  proto::PlayerId rawId = proto::kNoPlayer;
  std::vector<proto::Pause> atHost;
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  bool sent = false;
  while (atHost.empty() && std::chrono::steady_clock::now() < deadline) {
    for (const auto &event : raw->Poll(1ms)) {
      if (event.kind == net::Event::Kind::Connected)
        raw->Send(event.peer, proto::Hello{.name = "raw"});
      if (event.kind != net::Event::Kind::Received)
        continue;
      if (const auto *welcome = std::get_if<proto::Welcome>(&*event.message); welcome && !sent) {
        rawId = welcome->you;
        raw->Send(event.peer, proto::Pause{.player = 99, .paused = true});
        raw->Send(event.peer, proto::Pause{.player = 1, .paused = false});
        sent = true;
      }
    }
    CollectPauses(host->Update(1ms), atHost);
  }
  // Both landed on the sender: paused (in its own name), the host untouched.
  // (Its second message, "1 resumes", became "rawId resumes".)
  Pump({host.get()}, [&](std::size_t, const auto &events) {
    CollectPauses(events, atHost);
    return atHost.size() >= 2;
  });
  REQUIRE(atHost.size() == 2);
  CHECK(rawId != proto::kNoPlayer);
  CHECK(atHost[0] == proto::Pause{rawId, true});
  CHECK(atHost[1] == proto::Pause{rawId, false});
  CHECK(host->Paused() == std::set<proto::PlayerId>{1});
}

TEST_CASE("a late joiner learns who is paused; a guest that leaves paused no longer is") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  REQUIRE((host && a));
  a->SetLocalPaused(true); // before joining: sent once in
  REQUIRE(Pump({host.get(), a.get()},
               [&](std::size_t, const auto &) { return host->Paused().size() == 1; }));

  auto late = Endpoint::Join({"late"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(late, error);
  std::vector<proto::Pause> atLate;
  REQUIRE(Pump({host.get(), a.get(), late.get()}, [&](std::size_t i, const auto &events) {
    if (i == 2)
      CollectPauses(events, atLate);
    return !atLate.empty();
  }));
  CHECK(atLate.front() == proto::Pause{a->You(), true});

  // a leaves while paused: the host and the late joiner both drop it.
  const auto aId = a->You();
  a->Leave();
  std::vector<proto::Pause> atHost;
  atLate.clear();
  REQUIRE(Pump({host.get(), late.get()}, [&](std::size_t i, const auto &events) {
    CollectPauses(events, i == 0 ? atHost : atLate);
    return !atHost.empty() && !atLate.empty() && late->Players().size() == 2;
  }));
  CHECK(atHost.front() == proto::Pause{aId, false});
  CHECK(atLate.front() == proto::Pause{aId, false});
  CHECK(host->Paused().empty());
  CHECK(late->Paused().empty());
}

TEST_CASE("a driver's car radio reaches everyone, stamped, and late joiners") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  REQUIRE((host && a));
  REQUIRE(Pump({host.get(), a.get()},
               [&](std::size_t, const auto &) { return a->Players().size() == 2; }));

  // a drives with its radio on: the host hears it in a's name.
  a->SetLocalVehicleRadio({.player = 99, .on = true, .station = 4, .track = 0xabc});
  std::optional<proto::VehicleRadio> atHost;
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *radio = std::get_if<events::RemoteVehicleRadio>(&event); radio && i == 0)
        atHost = radio->radio;
    return atHost.has_value();
  }));
  CHECK(atHost->player == a->You());
  CHECK(atHost->on);
  CHECK(atHost->station == 4);
  CHECK(atHost->track == 0xabc);

  // Sent again unchanged (a passenger got in): it goes out again.
  a->SetLocalVehicleRadio({.on = true, .station = 4, .track = 0xabc});
  atHost.reset();
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *radio = std::get_if<events::RemoteVehicleRadio>(&event); radio && i == 0)
        atHost = radio->radio;
    return atHost.has_value();
  }));
  CHECK(atHost->revision == 2);

  // A player joining later learns it from the host, and the host's own.
  host->SetLocalVehicleRadio({.on = false, .station = -1});
  auto late = Endpoint::Join({"late"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(late, error);
  std::map<proto::PlayerId, proto::VehicleRadio> atLate;
  REQUIRE(Pump({host.get(), a.get(), late.get()}, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *radio = std::get_if<events::RemoteVehicleRadio>(&event); radio && i == 2)
        atLate[radio->radio.player] = radio->radio;
    return atLate.size() == 2;
  }));
  CHECK(atLate[a->You()].station == 4);
  CHECK_FALSE(atLate[1].on);
}

namespace {
// The world items and takes an endpoint reported.
struct LootSeen {
  std::vector<proto::WorldItem> items;
  std::vector<proto::WorldItemTaken> taken;
  void Add(const std::vector<SessionEvent> &events) {
    for (const auto &event : events) {
      if (const auto *e = std::get_if<events::WorldItemAppeared>(&event))
        items.push_back(e->item);
      if (const auto *e = std::get_if<events::WorldItemTakenBy>(&event))
        taken.push_back(e->taken);
    }
  }
};
} // namespace

TEST_CASE("a dropped bag is numbered by the host and lies in every game, late joiners' too") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error);
  REQUIRE((host && a && b));
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t, const auto &) {
    return a->Players().size() == 3 && b->Players().size() == 3;
  }));

  // a drops two stacks (one empty one is dropped on the way) in someone
  // else's name: the host stamps it a's, numbers it, and everyone hears it.
  a->DropWorldItem({.id = 42,
                    .dropper = 99,
                    .token = 7,
                    .position = {1.f, 2.f, 3.f},
                    .stacks = {{.record = 0xaa, .seed = 5, .quantity = 1},
                               {.record = 0xbb, .quantity = 0},
                               {.record = 0xcc, .quantity = 30}}});
  LootSeen seen[3];
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    seen[i].Add(events);
    return !seen[0].items.empty() && !seen[1].items.empty() && !seen[2].items.empty();
  }));
  for (const auto &s : seen) {
    REQUIRE(s.items.size() == 1);
    CHECK(s.items[0].id != 42);
    CHECK(s.items[0].dropper == a->You());
    CHECK(s.items[0].token == 7); // a knows its bag again by it
    CHECK(s.items[0].position == proto::Vec3{1.f, 2.f, 3.f});
    REQUIRE(s.items[0].stacks.size() == 2);
    CHECK(s.items[0].stacks[1].record == 0xcc);
  }
  CHECK(host->WorldItems().size() == 1);

  // The host drops one too: its own game hears it, numbered.
  host->DropWorldItem({.token = 1, .stacks = {{.record = 0xdd, .quantity = 2}}});
  LootSeen hostOwn;
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    if (i == 0)
      hostOwn.Add(events);
    return !hostOwn.items.empty();
  }));
  CHECK(hostOwn.items[0].dropper == 1);
  CHECK(hostOwn.items[0].id != seen[0].items[0].id);

  // A player joining later finds both lying there.
  auto late = Endpoint::Join({"late"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(late, error);
  LootSeen atLate;
  REQUIRE(Pump({host.get(), a.get(), b.get(), late.get()}, [&](std::size_t i, const auto &events) {
    if (i == 3)
      atLate.Add(events);
    return atLate.items.size() == 2;
  }));
}

TEST_CASE("the first to pick a stack up keeps it; a later claim hears who has it") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error);
  REQUIRE((host && a && b));
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t, const auto &) {
    return a->Players().size() == 3 && b->Players().size() == 3;
  }));
  host->DropWorldItem(
      {.token = 1,
       .stacks = {{.record = 0xaa, .seed = 3, .quantity = 1}, {.record = 0xbb, .quantity = 10}}});
  LootSeen seen[3];
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    seen[i].Add(events);
    return !seen[1].items.empty() && !seen[2].items.empty();
  }));
  const auto id = seen[1].items[0].id;

  // a and b both pick up stack 0; a's claim reaches the host first.
  a->TakeWorldItem(id, 0);
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    seen[i].Add(events);
    return !seen[0].taken.empty();
  }));
  b->TakeWorldItem(id, 0);
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    seen[i].Add(events);
    return seen[2].taken.size() == 2 && !seen[1].taken.empty();
  }));
  // Everyone hears a has it; b, who claimed second, hears it twice (the
  // broadcast, then the answer to its own claim), never itself.
  CHECK(seen[0].taken.size() == 1);
  CHECK(seen[0].taken[0] == proto::WorldItemTaken{id, 0, a->You()});
  CHECK(seen[1].taken[0].taker == a->You());
  for (const auto &taken : seen[2].taken)
    CHECK(taken.taker == a->You());
  // A claim in someone else's name is the claimer's.
  REQUIRE(host->WorldItems().contains(id));
  CHECK(host->WorldItems().at(id).stacks[0].quantity == 0);

  // The host takes the last stack: the bag is gone from the host's record,
  // and a later claim on it is told nobody has it for the claimer.
  host->TakeWorldItem(id, 1);
  LootSeen hostOwn;
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    if (i == 0)
      hostOwn.Add(events);
    else
      seen[i].Add(events);
    return !hostOwn.taken.empty() && seen[1].taken.size() == 2 && seen[2].taken.size() == 3;
  }));
  CHECK(hostOwn.taken[0] == proto::WorldItemTaken{id, 1, 1});
  CHECK(host->WorldItems().empty());
  b->TakeWorldItem(id, 1);
  const auto before = seen[2].taken.size();
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    seen[i].Add(events);
    return seen[2].taken.size() == before + 1;
  }));
  CHECK(seen[2].taken.back() == proto::WorldItemTaken{id, 1, proto::kNoPlayer});
}

TEST_CASE("a quest item one player got reaches every other player, stamped") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error);
  REQUIRE((host && a && b));
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t, const auto &) {
    return a->Players().size() == 3 && b->Players().size() == 3;
  }));
  std::vector<proto::SharedItem> seen[3];
  const auto collect = [&](std::size_t i, const std::vector<SessionEvent> &events) {
    for (const auto &event : events)
      if (const auto *e = std::get_if<events::SharedItemFrom>(&event))
        seen[i].push_back(e->item);
  };

  // a loots one (claiming to be someone, on a corpse): the host and b get it
  // as a's loot; a hears nothing back.
  a->SendSharedItem({.player = 99,
                     .kind = proto::kSharedOnCorpse,
                     .npc = 4,
                     .item = {.record = 0xabc, .quantity = 1}});
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    collect(i, events);
    return !seen[0].empty() && !seen[2].empty();
  }));
  for (const auto i : {0, 2}) {
    CHECK(seen[i][0].player == a->You());
    CHECK(seen[i][0].kind == proto::kSharedLooted);
    CHECK(seen[i][0].npc == 0);
    CHECK(seen[i][0].item.record == 0xabc);
  }

  // The host's corpse holds one: both guests hear it, on npc 7.
  host->SendSharedItem({.kind = proto::kSharedOnCorpse, .npc = 7, .item = {.record = 0xdef}});
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    collect(i, events);
    return !seen[1].empty() && seen[2].size() == 2;
  }));
  CHECK(seen[1][0].npc == 7);
  CHECK(seen[1][0].player == 1);
  CHECK(seen[0].size() == 1); // the host's own is never reported back
}

TEST_CASE("a guest that crashes is reported gone within the timeout") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast);
  auto guest = Endpoint::Join({"guest"}, "127.0.0.1", port, error, kFast);
  REQUIRE((host && guest));
  REQUIRE(Pump({host.get(), guest.get()},
               [&](std::size_t, const auto &) { return host->Players().size() == 2; }));

  // No goodbye: the process is gone, its socket with it.
  guest.reset();
  const auto start = std::chrono::steady_clock::now();
  REQUIRE(Pump(
      {host.get()},
      [&](std::size_t, const auto &events) { return Has<events::PlayerLeft>(events); }, 1ms, 3s));
  const auto took = SecondsSince(start);
  MESSAGE("crashed guest reported gone after " << took << " s (timeout 1 s)");
  CHECK(took > 0.8);
  CHECK(took < 1.4);
  CHECK(host->Players().size() == 1);
}

TEST_CASE("a hung guest is reported gone, and finds the session over when it wakes") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast);
  auto guest = Endpoint::Join({"guest"}, "127.0.0.1", port, error, kFast);
  REQUIRE((host && guest));
  REQUIRE(Pump({host.get(), guest.get()},
               [&](std::size_t, const auto &) { return guest->IsJoined(); }));

  // Its socket stays open, nothing answers.
  const auto start = std::chrono::steady_clock::now();
  REQUIRE(Pump(
      {host.get()},
      [&](std::size_t, const auto &events) { return Has<events::PlayerLeft>(events); }, 1ms, 3s));
  const auto took = SecondsSince(start);
  MESSAGE("hung guest reported gone after " << took << " s (timeout 1 s)");
  CHECK(took < 1.4);
  CHECK(EndOf({host.get(), guest.get()}, 1) == "the host is gone");
}

TEST_CASE("a guest notices a host that vanished within the timeout") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast);
  auto guest = Endpoint::Join({"guest"}, "127.0.0.1", port, error, kFast);
  REQUIRE((host && guest));
  REQUIRE(Pump({host.get(), guest.get()},
               [&](std::size_t, const auto &) { return guest->IsJoined(); }));

  host.reset(); // the host's game crashed
  const auto start = std::chrono::steady_clock::now();
  const auto reason = EndOf({guest.get()}, 0);
  const auto took = SecondsSince(start);
  MESSAGE("vanished host noticed after " << took << " s (timeout 1 s)");
  CHECK(reason == "the host is gone");
  CHECK(took > 0.8);
  CHECK(took < 1.4);
}

TEST_CASE("a host that never answers is given up within the timeout") {
  std::string error;
  auto guest = Endpoint::Join({"guest"}, "127.0.0.1", NextPort(), error, kFast);
  REQUIRE_MESSAGE(guest, error);
  const auto start = std::chrono::steady_clock::now();
  CHECK(EndOf({guest.get()}, 0) == "could not reach the host");
  CHECK(SecondsSince(start) < 1.4);
}

TEST_CASE("a hiccup shorter than the timeout disconnects nobody") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast);
  auto guest = Endpoint::Join({"guest"}, "127.0.0.1", port, error, kFast);
  REQUIRE((host && guest));
  REQUIRE(Pump({host.get(), guest.get()},
               [&](std::size_t, const auto &) { return guest->IsJoined(); }));

  // 0.7 of the timeout without a word from the guest (3.5 s in play), then
  // both run on.
  PumpFor({host.get()}, 700ms);
  bool left = false, ended = false;
  PumpFor({host.get(), guest.get()}, 1500ms, [&](std::size_t i, const auto &events) {
    if (i == 0)
      left |= Has<events::PlayerLeft>(events);
    else
      ended |= Has<events::Ended>(events);
  });
  CHECK_FALSE(left);
  CHECK_FALSE(ended);
  CHECK(host->Players().size() == 2);
  CHECK(guest->IsJoined());
}

TEST_CASE("a guest back from a crash gets in at once, under its old id, even in a full session") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error, kFast);
  auto c = Endpoint::Join({"c"}, "127.0.0.1", port, error, kFast);
  REQUIRE((host && a && b && c));
  REQUIRE(Pump({host.get(), a.get(), b.get(), c.get()}, [&](std::size_t, const auto &) {
    return host->Players().size() == 4 && a->IsJoined() && b->IsJoined() && c->IsJoined();
  }));
  const auto id = a->You();

  // The session is full: someone new is refused.
  auto newcomer = Endpoint::Join({"d"}, "127.0.0.1", port, error, kFast);
  REQUIRE(newcomer);
  const auto refused = EndOf({host.get(), a.get(), b.get(), c.get(), newcomer.get()}, 4);
  CHECK(refused.value_or("(still waiting)") == "refused: the session is full");

  // a's latest state reaches b; then a's game crashes.
  a->SendLocalState(proto::PlayerState{.position = {1.f, 0.f, 0.f}});
  std::optional<proto::PlayerState> before;
  REQUIRE(Pump({host.get(), a.get(), b.get(), c.get()}, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *remote = std::get_if<events::RemoteState>(&event); remote && i == 2)
        before = remote->state;
    return before.has_value();
  }));
  a.reset();
  const auto crashed = std::chrono::steady_clock::now();
  bool left = false;
  const Seen watchLeft = [&](std::size_t, const auto &events) {
    left |= Has<events::PlayerLeft>(events);
  };
  PumpFor({host.get(), b.get(), c.get()}, kFast.takeover + 100ms, watchLeft);

  // It comes back before its old connection times out.
  auto back = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
  REQUIRE(back);
  bool returned = false;
  REQUIRE(Pump({host.get(), b.get(), c.get(), back.get()}, [&](std::size_t i, const auto &events) {
    watchLeft(i, events);
    returned |= i == 0 && Has<events::PlayerReturned>(events);
    return back->IsJoined() && returned;
  }));
  const auto took = SecondsSince(crashed);
  MESSAGE("back in the session " << took << " s after the crash (timeout 1 s)");
  CHECK(took < 1.0);
  CHECK(back->You() == id);
  CHECK(host->Players().size() == 4);

  // Its states drive its puppet again: newer than anything it sent before.
  back->SendLocalState(proto::PlayerState{.position = {2.f, 0.f, 0.f}});
  std::optional<proto::PlayerState> after;
  REQUIRE(Pump({host.get(), b.get(), c.get(), back.get()}, [&](std::size_t i, const auto &events) {
    watchLeft(i, events);
    for (const auto &event : events)
      if (const auto *remote = std::get_if<events::RemoteState>(&event); remote && i == 1)
        after = remote->state;
    return after.has_value();
  }));
  CHECK(after->player == id);
  CHECK(static_cast<std::int32_t>(after->seq - before->seq) > 0);

  // Past the old connection's timeout: nobody ever saw it leave.
  PumpFor({host.get(), b.get(), c.get(), back.get()}, kFast.timeout, watchLeft);
  CHECK_FALSE(left);
  CHECK(host->Players().size() == 4);
  CHECK(back->IsJoined());
}

TEST_CASE("a live player cannot be replaced by a namesake") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
  REQUIRE((host && a));
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t, const auto &) { return a->IsJoined(); }));
  PumpFor({host.get(), a.get()}, kFast.takeover + 100ms); // in for longer than the takeover delay

  auto other = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
  REQUIRE(other);
  bool returned = false, aEnded = false;
  const Seen watch = [&](std::size_t i, const auto &events) {
    returned |= i == 0 && Has<events::PlayerReturned>(events);
    aEnded |= i == 1 && Has<events::Ended>(events);
  };
  REQUIRE(Pump({host.get(), a.get(), other.get()}, [&](std::size_t i, const auto &events) {
    watch(i, events);
    return other->IsJoined();
  }));
  CHECK(other->You() != a->You()); // someone else, under the same name
  PumpFor({host.get(), a.get(), other.get()}, kFast.timeout + 200ms, watch);
  CHECK_FALSE(returned);
  CHECK_FALSE(aEnded);
  CHECK(a->IsJoined());
  CHECK(host->Players().size() == 3);
}

TEST_CASE(
    "a guest's stealth reaches everyone, stamped, late joiners too; its noise only the host") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  REQUIRE_MESSAGE(host, error);
  auto guest = Endpoint::Join({"guest"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(guest, error);
  // Set before joining: sent once in.
  guest->SetLocalStealth(
      {.player = 99, .flags = proto::kStealthCamo, .visibility = 0.5f, .camo = 0x77});
  std::optional<proto::Stealth> atHost;
  std::optional<proto::Noise> noise;
  bool noiseSent = false;
  REQUIRE(Pump({host.get(), guest.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1 && guest->IsJoined() && !noiseSent) {
      guest->SendNoise({.player = 99, .stim = 23, .radius = 25.f});
      noiseSent = true;
    }
    for (const auto &event : events) {
      if (const auto *s = std::get_if<events::RemoteStealth>(&event); s && i == 0)
        atHost = s->stealth;
      if (const auto *n = std::get_if<events::NoiseBy>(&event); n && i == 0)
        noise = n->noise;
    }
    return atHost && noise;
  }));
  CHECK(atHost->player == guest->You()); // a guest can't hide someone else
  CHECK(atHost->flags == proto::kStealthCamo);
  CHECK(atHost->visibility == 0.5f);
  CHECK(atHost->camo == 0x77);
  CHECK(noise->player == guest->You());
  CHECK(noise->stim == 23);

  // A later guest gets the first one's stealth; the noise was never relayed.
  auto late = Endpoint::Join({"late"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(late, error);
  std::optional<proto::Stealth> atLate;
  bool lateHeardNoise = false;
  Pump({host.get(), guest.get(), late.get()}, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (i == 2) {
        if (const auto *s = std::get_if<events::RemoteStealth>(&event))
          atLate = s->stealth;
        lateHeardNoise |= std::holds_alternative<events::NoiseBy>(event);
      }
    return atLate.has_value();
  });
  REQUIRE(atLate);
  CHECK(atLate->player == guest->You());
  CHECK(atLate->flags == proto::kStealthCamo);
  CHECK_FALSE(lateHeardNoise);

  // The host's NPCs' awareness reaches the guests; a guest cannot send any.
  guest->SendNpcAwareness({.entries = {{.npc = 1, .player = 1, .level = 9}}});
  host->SendNpcAwareness({.entries = {{.npc = 7, .player = guest->You(), .level = 200}}});
  std::optional<proto::NpcAwareness> awareness;
  bool hostHeardAwareness = false;
  Pump({host.get(), guest.get(), late.get()}, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *a = std::get_if<events::HostNpcAwareness>(&event)) {
        if (i == 1)
          awareness = a->awareness;
        hostHeardAwareness |= i == 0;
      }
    return awareness.has_value();
  });
  REQUIRE(awareness);
  REQUIRE(awareness->entries.size() == 1);
  CHECK(awareness->entries[0].npc == 7);
  CHECK(awareness->entries[0].level == 200);
  CHECK_FALSE(hostHeardAwareness);
}

TEST_CASE("a guest's takedown request reaches the host stamped; the answer only the asker") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  REQUIRE_MESSAGE(host, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error);
  REQUIRE((a && b));
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t, const auto &) {
    return a->Players().size() == 3 && b->Players().size() == 3;
  }));

  a->SendNpcAction({.player = 99, .npc = 7, .kind = proto::NpcActionKind::Grab, .name = 5});
  std::optional<proto::NpcAction> asked;
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *e = std::get_if<events::NpcActionBy>(&event); e && i == 0)
        asked = e->action;
    return asked.has_value();
  }));
  CHECK(asked->player == a->You()); // a guest acts only in its own name
  CHECK(asked->kind == proto::NpcActionKind::Grab);
  CHECK(asked->name == 5);

  host->SendNpcActionReply(asked->player,
                           {.npc = 7, .kind = proto::NpcActionKind::Grab, .accepted = true});
  host->SendNpcHold({.npc = 7, .player = a->You(), .how = proto::NpcHoldKind::Grappled});
  std::optional<proto::NpcActionReply> answered;
  bool bAnswered = false;
  std::optional<proto::NpcHold> bHold;
  Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    for (const auto &event : events) {
      if (const auto *e = std::get_if<events::NpcActionAnswered>(&event)) {
        if (i == 1)
          answered = e->reply;
        else
          bAnswered = true;
      }
      if (const auto *e = std::get_if<events::HostNpcHold>(&event); e && i == 2)
        bHold = e->hold;
    }
    return false; // the full time: b must hear no answer
  });
  REQUIRE(answered);
  CHECK(answered->accepted);
  CHECK_FALSE(bAnswered);
  REQUIRE(bHold);
  CHECK(bHold->player == a->You());
  CHECK(host->NpcHolds().size() == 1);
}

TEST_CASE("holds reach late joiners and end with the NPC or the holder") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  REQUIRE_MESSAGE(host, error);
  auto holder = Endpoint::Join({"holder"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(holder, error);
  REQUIRE(Pump({host.get(), holder.get()},
               [&](std::size_t, const auto &) { return host->Players().size() == 2; }));
  host->SendNpcHold({.npc = 7, .player = 2, .how = proto::NpcHoldKind::Carried});
  host->SendNpcHold({.npc = 8, .player = 1, .how = proto::NpcHoldKind::Grappled});
  host->SendNpcHold({.npc = 9, .player = 1, .how = proto::NpcHoldKind::Grappled});
  host->SendNpcGone({.npc = 9, .reason = proto::kGoneDead}); // gone: no longer held

  auto late = Endpoint::Join({"late"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(late, error);
  std::map<proto::NpcId, proto::NpcHold> lateHeard;
  REQUIRE(Pump({host.get(), holder.get(), late.get()}, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *e = std::get_if<events::HostNpcHold>(&event); e && i == 2)
        lateHeard[e->hold.npc] = e->hold;
    return lateHeard.size() == 2;
  }));
  CHECK(lateHeard.at(7).how == proto::NpcHoldKind::Carried);
  CHECK(lateHeard.at(8).player == 1);
  CHECK_FALSE(lateHeard.contains(9));

  // The holder leaves: what it carried is released for everyone.
  holder->Leave();
  bool released = false;
  REQUIRE(Pump({host.get(), late.get()}, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *e = std::get_if<events::HostNpcHold>(&event);
          e && i == 1 && e->hold.npc == 7 && e->hold.player == proto::kNoPlayer)
        released = true;
    return released;
  }));
  CHECK(host->NpcHolds().size() == 1);
  CHECK(late->NpcHolds().size() == 1);
}

TEST_CASE("an NPC cue reaches only the player it is for") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error);
  REQUIRE((host && a && b));
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t, const auto &) {
    return a->Players().size() == 3 && b->Players().size() == 3;
  }));
  host->SendNpcCue({.npc = 7, .player = b->You(), .cue = proto::NpcCueKind::CombatStart});
  bool aHeard = false;
  std::optional<proto::NpcCue> bHeard;
  Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *e = std::get_if<events::HostNpcCue>(&event)) {
        if (i == 2)
          bHeard = e->cue;
        else
          aHeard = true;
      }
    return false;
  });
  REQUIRE(bHeard);
  CHECK(bHeard->cue == proto::NpcCueKind::CombatStart);
  CHECK_FALSE(aHeard);
  // A guest cannot answer or announce for the host.
  a->SendNpcActionReply(b->You(), {.npc = 7, .kind = proto::NpcActionKind::Grab});
  a->SendNpcHold({.npc = 7, .player = 2});
  CHECK(a->NpcHolds().empty());
}

namespace {
void CollectTimes(const std::vector<SessionEvent> &events, std::vector<proto::TimeEffect> &into) {
  for (const auto &event : events)
    if (const auto *t = std::get_if<events::RemoteTimeEffect>(&event))
      into.push_back(t->effect);
}
} // namespace

TEST_CASE("time abilities: stamped, made safe, kept for late joiners, ended when a player leaves") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  REQUIRE((host && a));
  // Before joining: sent once in. A wild scale is clamped (a bubble never
  // stops NPCs); the player field is ignored.
  a->SetLocalTimeEffect({.player = 9, .abilities = proto::kTimeSandevistan, .scale = -3.f});
  std::vector<proto::TimeEffect> atHost;
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    if (i == 0)
      CollectTimes(events, atHost);
    return !atHost.empty();
  }));
  const proto::TimeEffect sandevistan{
      .player = a->You(), .abilities = proto::kTimeSandevistan, .scale = 0.05f};
  CHECK(atHost.front() == sandevistan);
  CHECK(host->TimeEffects().at(a->You()) == sandevistan);

  // The host's own too: two Vs in a Sandevistan at once.
  host->SetLocalTimeEffect({.abilities = proto::kTimeSandevistan, .scale = 0.3f});
  auto late = Endpoint::Join({"late"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(late, error);
  std::vector<proto::TimeEffect> atLate, atA;
  REQUIRE(Pump({host.get(), a.get(), late.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1)
      CollectTimes(events, atA);
    if (i == 2)
      CollectTimes(events, atLate);
    return atLate.size() == 2 && !atA.empty();
  }));
  CHECK(atA.front() == proto::TimeEffect{1, proto::kTimeSandevistan, 0.3f});
  CHECK(late->TimeEffects().size() == 2);
  CHECK(late->TimeEffects().at(a->You()) == sandevistan);

  // Scanning alone makes no bubble: its scale is 1 whatever is sent.
  late->SetLocalTimeEffect({.abilities = proto::kTimeScanning, .scale = 0.1f});
  atHost.clear();
  REQUIRE(Pump({host.get(), a.get(), late.get()}, [&](std::size_t i, const auto &events) {
    if (i == 0)
      CollectTimes(events, atHost);
    return !atHost.empty();
  }));
  CHECK(atHost.front() == proto::TimeEffect{late->You(), proto::kTimeScanning, 1.f});

  // a leaves in its Sandevistan: its bubble ends on every game.
  const auto aId = a->You();
  a->Leave();
  atHost.clear(), atLate.clear();
  REQUIRE(Pump({host.get(), late.get()}, [&](std::size_t i, const auto &events) {
    CollectTimes(events, i == 0 ? atHost : atLate);
    return !atHost.empty() && !atLate.empty();
  }));
  CHECK(atHost.front() == proto::TimeEffect{aId, 0, 1.f});
  CHECK(atLate.front() == proto::TimeEffect{aId, 0, 1.f});
  CHECK_FALSE(host->TimeEffects().contains(aId));
  CHECK_FALSE(late->TimeEffects().contains(aId));
}

namespace {
struct DownAndOut {
  std::vector<proto::Vitals> vitals;
  std::vector<proto::Revive> revives;
  std::vector<proto::GameOver> lost;
  void Collect(const std::vector<SessionEvent> &events) {
    for (const auto &event : events) {
      if (const auto *v = std::get_if<events::RemoteVitals>(&event))
        vitals.push_back(v->vitals);
      else if (const auto *r = std::get_if<events::ReviveBy>(&event))
        revives.push_back(r->revive);
      else if (const auto *l = std::get_if<events::SessionLost>(&event))
        lost.push_back(l->over);
    }
  }
};
} // namespace

TEST_CASE("down and out: a guest's life reaches everyone, stamped, and late joiners") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error);
  REQUIRE((host && a && b));
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t, const auto &) {
    return a->Players().size() == 3 && b->Players().size() == 3;
  }));

  a->SetLocalVitals({.player = 99, .life = proto::Life::Down}); // never in someone else's name
  DownAndOut atHost, atA, atB;
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    (i == 0 ? atHost : i == 1 ? atA : atB).Collect(events);
    return !atHost.vitals.empty() && !atB.vitals.empty();
  }));
  CHECK(atHost.vitals.front().player == a->You());
  CHECK(atHost.vitals.front().life == proto::Life::Down);
  CHECK(atB.vitals.front().player == a->You());
  CHECK(atA.vitals.empty()); // never told about its own
  CHECK(host->LifeOf(a->You()) == proto::Life::Down);
  CHECK(b->LifeOf(a->You()) == proto::Life::Down);
  CHECK(atHost.lost.empty()); // two Vs are still up

  auto late = Endpoint::Join({"late"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(late, error);
  DownAndOut atLate;
  REQUIRE(Pump({host.get(), a.get(), b.get(), late.get()}, [&](std::size_t i, const auto &events) {
    if (i == 3)
      atLate.Collect(events);
    return !atLate.vitals.empty();
  }));
  CHECK(atLate.vitals.front().player == a->You());
  CHECK(late->LifeOf(a->You()) == proto::Life::Down);

  // A player who leaves is forgotten.
  const auto aId = a->You();
  a->Leave();
  REQUIRE(Pump({host.get(), b.get(), late.get()},
               [&](std::size_t, const auto &) { return late->Players().size() == 3; }));
  CHECK(host->LifeOf(aId) == proto::Life::Up);
  CHECK(late->LifeOf(aId) == proto::Life::Up);
}

TEST_CASE("down and out: a revive reaches the downed V's game only, and only while it is down") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error);
  REQUIRE((host && a && b));
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t, const auto &) {
    return a->Players().size() == 3 && b->Players().size() == 3;
  }));

  // Not down: the host drops it.
  b->SendRevive({.target = a->You(), .step = proto::ReviveStep::Start});
  DownAndOut atHost, atA, atB;
  PumpFor({host.get(), a.get(), b.get()}, 200ms, [&](std::size_t i, const auto &events) {
    (i == 0 ? atHost : i == 1 ? atA : atB).Collect(events);
  });
  CHECK(atA.revives.empty());

  a->SetLocalVitals({.life = proto::Life::Down});
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t, const auto &) {
    return host->LifeOf(a->You()) == proto::Life::Down;
  }));
  b->SendRevive({.player = 1, .target = a->You(), .step = proto::ReviveStep::Start});
  b->SendRevive({.target = a->You(), .step = proto::ReviveStep::Done});
  atA = {};
  atB = {};
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    (i == 0 ? atHost : i == 1 ? atA : atB).Collect(events);
    return atA.revives.size() == 2;
  }));
  CHECK(atA.revives[0] == proto::Revive{b->You(), a->You(), proto::ReviveStep::Start});
  CHECK(atA.revives[1] == proto::Revive{b->You(), a->You(), proto::ReviveStep::Done});
  CHECK(atHost.revives.empty());
  CHECK(atB.revives.empty());

  // The host revives a guest directly; a guest revives the host.
  host->SendRevive({.target = a->You(), .step = proto::ReviveStep::Cancel});
  host->SetLocalVitals({.life = proto::Life::Down});
  atA = {};
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    (i == 0 ? atHost : i == 1 ? atA : atB).Collect(events);
    return a->LifeOf(1) == proto::Life::Down;
  }));
  a->SendRevive({.target = 1, .step = proto::ReviveStep::Done});
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    (i == 0 ? atHost : i == 1 ? atA : atB).Collect(events);
    return !atHost.revives.empty() && !atA.revives.empty();
  }));
  CHECK(atA.revives.front() == proto::Revive{1, a->You(), proto::ReviveStep::Cancel});
  CHECK(atHost.revives.front() == proto::Revive{a->You(), 1, proto::ReviveStep::Done});
}

TEST_CASE("down and out: the session is lost when no V is up, once, and again after a revive") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  REQUIRE((host && a));
  REQUIRE(Pump({host.get(), a.get()},
               [&](std::size_t, const auto &) { return a->Players().size() == 2; }));

  DownAndOut atHost, atA;
  const auto collect = [&](std::size_t i, const auto &events) {
    (i == 0 ? atHost : atA).Collect(events);
  };
  host->SetLocalVitals({.life = proto::Life::Down});
  PumpFor({host.get(), a.get()}, 200ms, collect);
  CHECK(atHost.lost.empty());
  CHECK(atA.lost.empty());

  a->SetLocalVitals({.life = proto::Life::Out});
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    collect(i, events);
    return !atHost.lost.empty() && !atA.lost.empty();
  }));
  CHECK(atHost.lost.front().round == 1);
  CHECK(atA.lost.front().round == 1);
  // Nothing more while it stays lost.
  a->SetLocalVitals({.life = proto::Life::Down});
  PumpFor({host.get(), a.get()}, 200ms, collect);
  CHECK(atHost.lost.size() == 1);

  // Up again (the reload), then everyone down again: a second loss.
  host->SetLocalVitals({.life = proto::Life::Up});
  PumpFor({host.get(), a.get()}, 100ms, collect);
  host->SetLocalVitals({.life = proto::Life::Out});
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    collect(i, events);
    return atA.lost.size() == 2;
  }));
  CHECK(atA.lost.back().round == 2);
}

TEST_CASE("down and out: the last V up leaving loses the session for the one left down") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  REQUIRE((host && a));
  REQUIRE(Pump({host.get(), a.get()},
               [&](std::size_t, const auto &) { return a->Players().size() == 2; }));
  host->SetLocalVitals({.life = proto::Life::Down});
  DownAndOut atHost;
  PumpFor({host.get(), a.get()}, 200ms,
          [&](std::size_t i, const auto &events) { i == 0 ? atHost.Collect(events) : void(); });
  CHECK(atHost.lost.empty());
  a->Leave();
  REQUIRE(Pump({host.get()}, [&](std::size_t, const auto &events) {
    atHost.Collect(events);
    return !atHost.lost.empty();
  }));
}
