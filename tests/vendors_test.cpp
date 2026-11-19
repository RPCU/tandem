// Vendors in co-op (core/include/tandem/vendors.hpp, docs/architecture/
// progression.md "Vendors"): whether a shop opens for our V and why not, the
// book a game keeps of the session's vendors, the message on the wire, and
// over a real session (loopback UDP): an owner's vendor reaches the players
// who see its NPC, late joiners too; a vendor that dies is every game's.

#include "ports.hpp"
#include "tandem/endpoint.hpp"
#include "tandem/protocol.hpp"
#include "tandem/regions.hpp"
#include "tandem/vendors.hpp"

#include <doctest/doctest.h>

#include <functional>
#include <limits>
#include <map>
#include <string>

using namespace std::chrono_literals;
using namespace tandem;
using vendors::Ask;
using vendors::Decide;
using vendors::Shop;

namespace {
// A vendor's proxy beside our V, our own world's copy of it there and open.
Ask Beside() {
  return {.vendorRecord = true,
          .entity = 0xABCDEF0123ull,
          .distance = 1.5f,
          .ownFound = true,
          .ownVendor = true,
          .ownAlive = true,
          .accessible = true};
}
} // namespace

TEST_CASE("a shop opens on our own world's vendor, and says why when it does not") {
  CHECK(Decide(Beside()) == Shop::Open);
  // No prompt at all: not a vendor, no placed entity named, out of reach.
  auto ask = Beside();
  ask.vendorRecord = false;
  CHECK(Decide(ask) == Shop::NotVendor);
  ask = Beside();
  ask.entity = 0;
  CHECK(Decide(ask) == Shop::Unknown);
  ask = Beside();
  ask.distance = vendors::kReach + 0.1f;
  CHECK(Decide(ask) == Shop::Far);
  ask.distance = std::numeric_limits<float>::quiet_NaN();
  CHECK(Decide(ask) == Shop::Far);
  ask.distance = vendors::kReach;
  CHECK(Decide(ask) == Shop::Open);
  for (const auto shop : {Shop::NotVendor, Shop::Unknown, Shop::Far})
    CHECK_FALSE(vendors::Prompts(shop));
  for (const auto shop : {Shop::Open, Shop::Dead, Shop::Busy, Shop::NotHere, Shop::Locked})
    CHECK(vendors::Prompts(shop));

  // Dead: the owner's NPC, the session's word, or our own world's copy.
  ask = Beside();
  ask.proxyDead = true;
  CHECK(Decide(ask) == Shop::Dead);
  ask = Beside();
  ask.sessionDead = true;
  CHECK(Decide(ask) == Shop::Dead);
  ask = Beside();
  ask.ownAlive = false;
  CHECK(Decide(ask) == Shop::Dead);
  // A dead vendor is dead even when our V is busy or our world lacks it.
  ask = Beside();
  ask.proxyDead = true;
  ask.free = false;
  ask.ownFound = false;
  CHECK(Decide(ask) == Shop::Dead);

  // Our V cannot now (combat, down, a scene).
  ask = Beside();
  ask.free = false;
  CHECK(Decide(ask) == Shop::Busy);

  // Our own save decides: no such vendor in our world (our story has not
  // put it there), or not a vendor there, or its access rules refuse us.
  ask = Beside();
  ask.ownFound = false;
  CHECK(Decide(ask) == Shop::NotHere);
  ask = Beside();
  ask.ownVendor = false;
  CHECK(Decide(ask) == Shop::NotHere);
  ask = Beside();
  ask.accessible = false;
  CHECK(Decide(ask) == Shop::Locked);
  // Locked comes after "not here": an absent vendor has no rules to read.
  ask.ownFound = false;
  CHECK(Decide(ask) == Shop::NotHere);

  CHECK(std::string(vendors::NameOf(Shop::Open)) == "open");
  CHECK(std::string(vendors::NameOf(Shop::Locked)) == "locked for us");
  CHECK(std::string(vendors::NameOf(Shop::NotHere)) == "not in our world");
}

TEST_CASE("the book keeps each vendor's entity and the session's dead, once each") {
  vendors::Book book;
  CHECK(book.EntityOf(7) == 0);
  CHECK(book.Note({.npc = 7, .entity = 0x77}));
  CHECK_FALSE(book.Note({.npc = 7, .entity = 0x77})); // said again: no news
  CHECK(book.EntityOf(7) == 0x77);
  CHECK(book.Note({.npc = 7, .entity = 0x78})); // another word for it
  CHECK(book.EntityOf(7) == 0x78);
  // Nothing to keep: no entity, or a live vendor with no NPC.
  CHECK_FALSE(book.Note({.npc = 8, .entity = 0}));
  CHECK_FALSE(book.Note({.npc = 0, .entity = 0x99}));
  CHECK(book.Live() == 1);

  // A death: news once, taken once.
  CHECK_FALSE(book.Dead(0x78));
  CHECK(book.Note({.entity = 0x78, .flags = proto::kVendorDead}));
  CHECK_FALSE(book.Note({.entity = 0x78, .flags = proto::kVendorDead}));
  CHECK(book.Dead(0x78));
  CHECK(book.TakeDead() == std::vector<std::uint64_t>{0x78});
  CHECK(book.TakeDead().empty());
  CHECK(book.DeadCount() == 1);

  // Bounded: a flood of names never grows it past its caps.
  for (std::uint32_t i = 1; i <= vendors::Book::kMaxLive + 50; ++i)
    book.Note({.npc = 1000 + i, .entity = i});
  CHECK(book.Live() == vendors::Book::kMaxLive);
  CHECK(book.EntityOf(1000 + vendors::Book::kMaxLive + 50) == vendors::Book::kMaxLive + 50);
  for (std::uint64_t i = 1; i <= vendors::Book::kMaxDead + 50; ++i)
    book.Note({.entity = 0x1000 + i, .flags = proto::kVendorDead});
  CHECK(book.DeadCount() == vendors::Book::kMaxDead);

  book.Clear();
  CHECK(book.Live() == 0);
  CHECK(book.DeadCount() == 0);
  CHECK(book.TakeDead().empty());
  CHECK_FALSE(book.Dead(0x78));
}

TEST_CASE("a vendor survives encoding, is small reliable state, and is refused malformed") {
  using namespace proto;
  for (const NpcVendor vendor : {NpcVendor{.npc = 7, .entity = 0x1234'5678'9ABC'DEF0ull},
                                 NpcVendor{.entity = 0x42, .flags = kVendorDead}}) {
    const auto decoded = Decode(Encode(vendor));
    REQUIRE(decoded.has_value());
    CHECK(std::get<NpcVendor>(*decoded) == vendor);
    CHECK(Encode(vendor).size() < 24);
    // The hit outcomes' path (Endpoint::SendCombat), reliable.
    CHECK(IsCombat(vendor));
    CHECK_FALSE(IsStory(vendor));
    CHECK(ChannelOf(vendor) == Channel::Event);
  }
  // Appended after every older message: no index moved.
  CHECK(Message(NpcVendor{}).index() == Message(VehicleMotion{}).index() + 1);
  CHECK(Message(NpcVendor{}).index() + 1 == std::variant_size_v<Message>);
  CHECK(std::string(NameOf(NpcVendor{})) == "NpcVendor");
  // No entity; unknown flags; a death that names an NPC; a live one that
  // names none.
  CHECK_FALSE(Decode(Encode(NpcVendor{.npc = 7})).has_value());
  CHECK_FALSE(Decode(Encode(NpcVendor{.npc = 7, .entity = 1, .flags = 2})).has_value());
  CHECK_FALSE(Decode(Encode(NpcVendor{.npc = 7, .entity = 1, .flags = kVendorDead})).has_value());
  CHECK_FALSE(Decode(Encode(NpcVendor{.entity = 1})).has_value());
}

// ---- over a session ----

namespace {
using test::NextPort;

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

void PumpFor(std::vector<Endpoint *> endpoints, std::chrono::milliseconds time,
             const std::function<void(std::size_t, const std::vector<SessionEvent> &)> &each) {
  const auto deadline = std::chrono::steady_clock::now() + time;
  while (std::chrono::steady_clock::now() < deadline)
    for (std::size_t i = 0; i < endpoints.size(); ++i)
      each(i, endpoints[i]->Update(1ms));
}

// What each game's scripts hear of vendors, and its book.
struct Heard {
  std::map<std::size_t, std::vector<proto::NpcVendor>> at;
  std::map<std::size_t, vendors::Book> books;
  void operator()(std::size_t i, const std::vector<SessionEvent> &events) {
    for (const auto &event : events)
      if (const auto *combat = std::get_if<events::Combat>(&event))
        if (const auto *vendor = std::get_if<proto::NpcVendor>(&combat->message)) {
          at[i].push_back(*vendor);
          books[i].Note(*vendor);
        }
  }
  std::size_t Count(std::size_t i) const {
    const auto it = at.find(i);
    return it == at.end() ? 0 : it->second.size();
  }
};

constexpr std::uint64_t kStall = 0x0123'4567'89AB'CDEFull;
constexpr std::uint64_t kClinic = 0x0FED'CBA9'8765'4321ull;
} // namespace

TEST_CASE("the host's vendor reaches every guest once, late joiners too; its death is everyone's") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  REQUIRE((host && a));
  Heard heard;
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return a->IsJoined() && host->Players().size() == 2;
  }));
  // Said before its spawn (the game's two queues): kept, said right after it.
  CHECK_FALSE(host->SendCombat(proto::NpcVendor{.npc = 7, .entity = kStall}));
  host->SendNpcSpawn({.npc = 7, .record = 1});
  host->SendNpcSpawn({.npc = 8, .record = 2});
  CHECK(host->NpcVendors().contains(7));
  CHECK(host->SendCombat(proto::NpcVendor{.npc = 8, .entity = kClinic}));
  CHECK(host->SendCombat(proto::NpcVendor{.npc = 8, .entity = kClinic})); // said once
  // Not ours to say: a guest on the host's NPC, a death from anyone, an NPC
  // nobody shares.
  CHECK_FALSE(a->SendCombat(proto::NpcVendor{.npc = 7, .entity = kStall}));
  CHECK_FALSE(host->SendCombat(proto::NpcVendor{.entity = kStall, .flags = proto::kVendorDead}));
  CHECK_FALSE(host->SendCombat(proto::NpcVendor{.npc = 99, .entity = 5}));
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return a->NpcVendors().size() == 2;
  }));
  PumpFor({host.get(), a.get()}, 100ms, std::ref(heard));
  CHECK(heard.Count(1) == 2);
  CHECK(heard.books[1].EntityOf(7) == kStall);
  CHECK(heard.books[1].EntityOf(8) == kClinic);
  CHECK(heard.Count(0) == 0); // the host's own

  // The stall's vendor dies on the host: every guest hears its entity, once.
  host->SendNpcGone({.npc = 7, .reason = proto::kGoneDead, .killer = host->You()});
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return heard.books[1].Dead(kStall);
  }));
  CHECK(host->DeadVendors().contains(kStall));
  CHECK(a->DeadVendors().contains(kStall));
  CHECK_FALSE(a->NpcVendors().contains(7)); // its NPC is gone
  // The clinic's leaves range (not a death): nothing to remember.
  host->SendNpcGone({.npc = 8, .reason = proto::kGoneAway});
  PumpFor({host.get(), a.get()}, 100ms, std::ref(heard));
  CHECK_FALSE(a->DeadVendors().contains(kClinic));
  CHECK(heard.books[1].TakeDead() == std::vector<std::uint64_t>{kStall});

  // A late joiner: the dead stall, not the gone clinic; a vendor still shared.
  host->SendNpcSpawn({.npc = 9, .record = 3});
  CHECK(host->SendCombat(proto::NpcVendor{.npc = 9, .entity = kClinic}));
  auto late = Endpoint::Join({"late"}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(late, error);
  REQUIRE(Pump({host.get(), a.get(), late.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return late->IsJoined() && late->NpcVendors().contains(9) &&
           late->DeadVendors().contains(kStall);
  }));
  PumpFor({host.get(), a.get(), late.get()}, 200ms, std::ref(heard));
  CHECK(heard.books[2].Dead(kStall));
  CHECK(heard.books[2].EntityOf(9) == kClinic);
  CHECK(late->DeadVendors().size() == 1);
  // a heard nothing twice.
  std::size_t deaths = 0;
  for (const auto &vendor : heard.at[1])
    deaths += (vendor.flags & proto::kVendorDead) != 0;
  CHECK(deaths == 1);
}

TEST_CASE("a guest's own vendor reaches the host and the others; a knock-out then a death counts") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error);
  REQUIRE((host && a && b));
  Heard heard;
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return a->IsJoined() && b->IsJoined() && host->Players().size() == 3;
  }));
  // a runs a vendor of its own (regions: an NPC of a's range of ids).
  const auto npc = regions::MakeNpcId(a->You(), 1);
  a->SendNpcSpawn({.npc = npc, .record = 1});
  CHECK(a->SendCombat(proto::NpcVendor{.npc = npc, .entity = kStall}));
  // Not b's to say.
  CHECK_FALSE(b->SendCombat(proto::NpcVendor{.npc = npc, .entity = kClinic}));
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return heard.books[0].EntityOf(npc) == kStall && heard.books[2].EntityOf(npc) == kStall;
  }));
  CHECK(host->NpcVendors().at(npc).entity == kStall);
  CHECK(heard.Count(1) == 0); // a's own word never comes back to it

  // Knocked out, then killed: the death still counts, for everyone.
  a->SendNpcGone({.npc = npc, .reason = proto::kGoneKnockedOut, .killer = b->You()});
  PumpFor({host.get(), a.get(), b.get()}, 100ms, std::ref(heard));
  CHECK(host->DeadVendors().empty());
  a->SendNpcGone({.npc = npc, .reason = proto::kGoneDead, .killer = b->You()});
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    heard(i, events);
    return heard.books[0].Dead(kStall) && heard.books[1].Dead(kStall) &&
           heard.books[2].Dead(kStall);
  }));
  CHECK(host->DeadVendors().contains(kStall));
}
