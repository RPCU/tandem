// A real host and real guests over loopback UDP, in one process.

#include "tandem/endpoint.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <functional>
#include <map>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
std::uint16_t NextPort() {
  static std::atomic<std::uint16_t> port{41000};
  return port++;
}

template <class E> bool Has(const std::vector<SessionEvent> &events) {
  for (const auto &event : events)
    if (std::holds_alternative<E>(event))
      return true;
  return false;
}

// Runs every endpoint until `done` holds or two seconds pass. `wait` is each
// endpoint's turn; 0 busy-loops, so no endpoint waits on another's turn.
bool Pump(std::vector<Endpoint *> endpoints,
          const std::function<bool(std::size_t, const std::vector<SessionEvent> &)> &done,
          std::chrono::milliseconds wait = 1ms) {
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  bool finished = false;
  while (!finished && std::chrono::steady_clock::now() < deadline) {
    for (std::size_t i = 0; i < endpoints.size(); ++i)
      finished = done(i, endpoints[i]->Update(wait)) || finished;
  }
  return finished;
}
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
