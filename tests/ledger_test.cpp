// The death ledger and twins, the rules alone (core/include/tandem/ledger.hpp,
// twins.hpp; docs/architecture/regions.md §3, §14): what the ledger keeps and
// in which order, its bound, its digest, whose body a game shows and for how
// long; which of our NPCs is the twin of a proxy. Over a real session:
// tests/regions_endpoint_test.cpp.

#include "tandem/ledger.hpp"
#include "tandem/protocol.hpp"
#include "tandem/twins.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace tandem;

namespace {
constexpr std::uint64_t kSecond = 1'000'000;

proto::NpcDeath Report(std::uint64_t entity, proto::NpcId npc = 5, proto::Vec3 at = {}) {
  return {.npc = npc, .entity = entity, .record = 9, .appearance = 3, .position = at, .yaw = 1.f};
}
} // namespace

TEST_CASE("ledger: the host stamps each death once; a guest keeps what the host said") {
  ledger::Deaths host, guest;
  const auto first = host.Record(Report(0xA), 2, 10 * kSecond);
  REQUIRE(first.has_value());
  CHECK(first->npc == 0);
  CHECK(first->by == 2u);
  CHECK(first->order == 1u);
  CHECK(first->timeUs == 10 * kSecond);
  CHECK(first->record == 9u);
  // The same entity again (knocked out, then dead; its twin in another
  // game): once.
  CHECK_FALSE(host.Record(Report(0xA, 77), 3, 11 * kSecond).has_value());
  CHECK_FALSE(host.Record(Report(0), 3, 11 * kSecond).has_value());
  const auto second = host.Record(Report(0xB), 1, 12 * kSecond);
  REQUIRE(second.has_value());
  CHECK(second->order == 2u);
  CHECK(host.Size() == 2);

  // A guest: news once, never an unstamped report, in any order.
  CHECK(guest.Note(*second));
  CHECK_FALSE(guest.Note(*second));
  CHECK_FALSE(guest.Note(Report(0xC)));
  CHECK(guest.Digest() != host.Digest());
  CHECK(guest.Note(*first));
  CHECK(guest.Digest() == host.Digest());
  CHECK(guest.Has(0xA));
  REQUIRE(guest.Find(0xB) != nullptr);
  CHECK(guest.Find(0xB)->by == 1u);
  CHECK(guest.Find(0xC) == nullptr);
  const auto order = guest.InOrder();
  REQUIRE(order.size() == 2);
  CHECK(order[0].entity == 0xAu);
  CHECK(order[1].entity == 0xBu);

  guest.Clear();
  CHECK(guest.Empty());
  CHECK(guest.Digest() == ledger::Deaths{}.Digest());
}

TEST_CASE("ledger: bounded; the oldest goes, the same on the host and on every guest") {
  ledger::Deaths host, guest, late;
  std::vector<proto::NpcDeath> said;
  for (std::uint64_t i = 1; i <= proto::kMaxDeaths + 10; ++i) {
    const auto death = host.Record(Report(0x1000 + i), 1, i * kSecond);
    REQUIRE(death.has_value());
    said.push_back(*death);
    guest.Note(*death);
  }
  CHECK(host.Size() == proto::kMaxDeaths);
  CHECK(guest.Size() == proto::kMaxDeaths);
  CHECK_FALSE(host.Has(0x1001));
  CHECK_FALSE(host.Has(0x100A));
  CHECK(host.Has(0x100B));
  CHECK(host.Has(0x1000 + proto::kMaxDeaths + 10));
  CHECK(guest.Digest() == host.Digest());
  // One that was dropped does not come back (a repeat, a resync of old news).
  CHECK_FALSE(guest.Note(said.front()));
  CHECK(guest.Digest() == host.Digest());
  // A late joiner hears the ledger as it stands, oldest first.
  for (const auto &death : host.InOrder())
    CHECK(late.Note(death));
  CHECK(late.Digest() == host.Digest());
  // The next death is number kMaxDeaths + 11, wherever the count resumes.
  const auto next = host.Record(Report(0x9999), 2, kSecond);
  REQUIRE(next.has_value());
  CHECK(next->order == proto::kMaxDeaths + 11);
  CHECK(host.Size() == proto::kMaxDeaths);
}

TEST_CASE("ledger: a body lies where a placed NPC fell, for a while, in the other games") {
  ledger::Deaths host;
  const auto near = *host.Record(Report(0xA, 5, {10.f, 0.f, 0.f}), 2, 100 * kSecond);
  const auto far = *host.Record(Report(0xB, 6, {500.f, 0.f, 0.f}), 2, 100 * kSecond);
  const auto own = *host.Record(Report(0xC, 7, {12.f, 0.f, 0.f}), 1, 100 * kSecond);
  const auto now = 160 * kSecond;
  // Not its own kill's (its world has the real body); recent only.
  CHECK(ledger::ShowsBody(near, 1, now));
  CHECK_FALSE(ledger::ShowsBody(own, 1, now));
  CHECK(ledger::ShowsBody(own, 2, now));
  CHECK_FALSE(ledger::ShowsBody(near, 1, 100 * kSecond + ledger::kBodyForUs));
  CHECK(ledger::ShowsBody(near, 1, 100 * kSecond + ledger::kBodyForUs - 1));
  CHECK(ledger::ShowsBody(near, 1, 99 * kSecond));     // our reading of the host clock lags
  CHECK_FALSE(ledger::ShowsBody(Report(0xD), 1, now)); // never stamped

  const auto all = host.InOrder();
  auto shown = ledger::Bodies(all, 1, now, {0.f, 0.f, 0.f}, {});
  REQUIRE(shown.size() == 1);
  CHECK(shown[0].entity == 0xAu);
  // Hysteresis: a body shown stays until its V is beyond kBodyOut.
  CHECK(ledger::Bodies(all, 1, now, {-65.f, 0.f, 0.f}, {}).empty());
  CHECK(ledger::Bodies(all, 1, now, {-65.f, 0.f, 0.f}, {0xA}).size() == 1);
  CHECK(ledger::Bodies(all, 1, now, {-95.f, 0.f, 0.f}, {0xA}).empty());
  CHECK(ledger::Bodies(all, 1, now + ledger::kBodyForUs, {0.f, 0.f, 0.f}, {0xA}).empty());

  // Never more than a few: the nearest.
  ledger::Deaths many;
  for (std::uint64_t i = 1; i <= 20; ++i)
    many.Record(Report(0x100 + i, 5, {static_cast<float>(i), 0.f, 0.f}), 2, 100 * kSecond);
  shown = ledger::Bodies(many.InOrder(), 1, now, {0.f, 0.f, 0.f}, {});
  REQUIRE(shown.size() == ledger::kMaxBodies);
  CHECK(shown.front().entity == 0x101u);
  CHECK(shown.back().entity == 0x100u + ledger::kMaxBodies);
}

TEST_CASE("ledger: its messages survive encoding, are reliable state, and are refused malformed") {
  using namespace proto;
  const NpcPlaced placed{.npc = 7, .entity = 0x1234'5678'9ABC'DEF0ull};
  const NpcDeath report{.npc = 7,
                        .entity = 0x42,
                        .record = 5,
                        .appearance = 6,
                        .position = {1.f, 2.f, 3.f},
                        .yaw = 0.5f,
                        .flags = kDeathKnockedOut};
  const NpcDeath stamped{.entity = 0x42,
                         .record = 5,
                         .appearance = 6,
                         .position = {1.f, 2.f, 3.f},
                         .yaw = 0.5f,
                         .by = 2,
                         .order = 9,
                         .timeUs = 123'456'789'012ull};
  for (const Message &message : {Message(placed), Message(report), Message(stamped)}) {
    const auto decoded = Decode(Encode(message));
    REQUIRE(decoded.has_value());
    CHECK(*decoded == message);
    CHECK(Encode(message).size() < 72);
    // The hit outcomes' path (Endpoint::SendCombat), reliable, ordered with
    // the digests; state, not a one-shot.
    CHECK(IsCombat(message));
    CHECK_FALSE(IsStory(message));
    CHECK(ChannelOf(message) == Channel::Event);
    CHECK(IsReliable(message));
  }
  // Appended after every older message: no index moved.
  CHECK(Message(NpcPlaced{}).index() > Message(QuestInteract{}).index());
  CHECK(Message(NpcDeath{}).index() == Message(NpcPlaced{}).index() + 1);
  CHECK(std::string(NameOf(NpcPlaced{})) == "NpcPlaced");
  CHECK(std::string(NameOf(NpcDeath{})) == "NpcDeath");
  // No NPC or no entity; a death with no entity, unknown flags, both an NPC
  // and a stamp, neither, or a report that names its reporter.
  CHECK_FALSE(Decode(Encode(NpcPlaced{.entity = 1})).has_value());
  CHECK_FALSE(Decode(Encode(NpcPlaced{.npc = 1})).has_value());
  CHECK_FALSE(Decode(Encode(NpcDeath{.npc = 1})).has_value());
  CHECK_FALSE(Decode(Encode(NpcDeath{.npc = 1, .entity = 1, .flags = 2})).has_value());
  CHECK_FALSE(Decode(Encode(NpcDeath{.npc = 1, .entity = 1, .order = 1})).has_value());
  CHECK_FALSE(Decode(Encode(NpcDeath{.entity = 1})).has_value());
  CHECK_FALSE(Decode(Encode(NpcDeath{.npc = 1, .entity = 1, .by = 2})).has_value());
}

// ---- twins ----

namespace {
using twins::Body;
using twins::Match;
using twins::Pair;

constexpr std::uint64_t kGuardRecord = 0x77, kClerkRecord = 0x78;
Body At(float x, float y = 0.f, std::uint64_t record = kGuardRecord, std::uint64_t entity = 0) {
  return {.record = record, .position = {x, y, 0.f}, .entity = entity};
}
} // namespace

TEST_CASE("twins: a placed NPC's twin is ours of the same id, wherever each stands") {
  // Theirs walked 12 m from its post; ours stands at it, next to another of
  // the same record.
  const std::vector<Body> theirs{At(12.f, 0.f, kGuardRecord, 0xA)};
  const std::vector<Body> ours{At(11.5f, 0.f, kGuardRecord, 0xB), At(0.f, 0.f, kGuardRecord, 0xA)};
  CHECK(Match(theirs, ours) == std::vector<Pair>{{.theirs = 0, .ours = 1, .identity = true}});
  // Named, and we have no such NPC (our story has not put it there): none,
  // never the neighbour of its record.
  CHECK(Match(theirs, std::vector<Body>{At(12.f, 0.f, kGuardRecord, 0xB), At(12.f)}).empty());
  // Ours is in a fight: never given up.
  auto busy = ours;
  busy[1].busy = true;
  CHECK(Match(theirs, busy).empty());
}

TEST_CASE("twins: two identical guards side by side are two pairs, each its own") {
  // Theirs: A at 0 and B at 2. Ours: B' at 1.9 listed first, A' at 0.1.
  const std::vector<Body> theirs{At(0.f), At(2.f)};
  const std::vector<Body> ours{At(1.9f), At(0.1f)};
  CHECK(Match(theirs, ours) ==
        std::vector<Pair>{{.theirs = 0, .ours = 1}, {.theirs = 1, .ours = 0}});
  // One proxy only (the other guard is still on its way): it takes the
  // nearer of ours, and the other one of ours stays.
  CHECK(Match(std::vector<Body>{At(2.f)}, ours) == std::vector<Pair>{{.theirs = 0, .ours = 0}});
  // Two proxies, one of ours: one pair, the nearer proxy's; ours is never
  // taken twice.
  CHECK(Match(theirs, std::vector<Body>{At(1.5f)}) == std::vector<Pair>{{.theirs = 1, .ours = 0}});
  // Three in a row, 1 m apart, ours shifted by 0.4 m: each takes its own,
  // not the first in reach.
  const std::vector<Body> row{At(0.f), At(1.f), At(2.f)};
  const std::vector<Body> shifted{At(2.4f), At(0.4f), At(1.4f)};
  CHECK(Match(row, shifted) == std::vector<Pair>{{.theirs = 0, .ours = 1},
                                                 {.theirs = 1, .ours = 2},
                                                 {.theirs = 2, .ours = 0}});
}

TEST_CASE("twins: by place only within reach, of the same record, and never one of ours placed") {
  const std::vector<Body> theirs{At(0.f)};
  // Too far; another record; ours has a static id (its twin would be named).
  CHECK(Match(theirs, std::vector<Body>{At(twins::kReach + 0.1f)}).empty());
  CHECK(Match(theirs, std::vector<Body>{At(0.5f, 0.f, kClerkRecord)}).empty());
  CHECK(Match(theirs, std::vector<Body>{At(0.5f, 0.f, kGuardRecord, 0xA)}).empty());
  CHECK(Match(theirs, std::vector<Body>{At(twins::kReach - 0.1f)}).size() == 1);
  // A wider reach when asked.
  CHECK(Match(theirs, std::vector<Body>{At(4.f)}, 5.f).size() == 1);
  // Identity and place together: the named one first, the rest by place.
  const std::vector<Body> mixed{At(0.f, 0.f, kGuardRecord, 0xA), At(1.f)};
  const std::vector<Body> mine{At(1.2f), At(30.f, 0.f, kGuardRecord, 0xA)};
  CHECK(Match(mixed, mine) ==
        std::vector<Pair>{{.theirs = 0, .ours = 1, .identity = true}, {.theirs = 1, .ours = 0}});
  CHECK(Match({}, mine).empty());
  CHECK(Match(mixed, {}).empty());
}
