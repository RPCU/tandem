// What a guest brings home (docs/architecture/quest-carryover.md): the
// ledger's rules and its durable form.

#include "tandem/carryover.hpp"

#include <doctest/doctest.h>

using namespace tandem::carry;

TEST_CASE("quest kinds fall into the carry-over tiers") {
  CHECK(TierOf(QuestKind::MainQuest) == Tier::StoryQuest);
  CHECK(TierOf(QuestKind::SideQuest) == Tier::StoryQuest);
  CHECK(TierOf(QuestKind::MinorQuest) == Tier::StoryQuest);
  CHECK(TierOf(QuestKind::VehicleQuest) == Tier::StoryQuest);
  CHECK(TierOf(QuestKind::ApartmentQuest) == Tier::StoryQuest);
  CHECK(TierOf(QuestKind::StreetStory) == Tier::OpenWorld);
  CHECK(TierOf(QuestKind::CyberPsycho) == Tier::OpenWorld);
  CHECK(TierOf(QuestKind::Contract) == Tier::OpenWorld);
  CHECK(TierOf(QuestKind::CourierQuest) == Tier::OpenWorld);
  CHECK(TierOf(QuestKind::Unknown) == Tier::OpenWorld);
}

TEST_CASE("a reward is paid once per quest, whoever hosts, however often it is sent") {
  Ledger ledger;
  ledger.NoteQuest(100, QuestKind::StreetStory, OwnState::NotStarted, 0xAAAA);
  CHECK(ledger.PayReward(100, 7) == Pay::Yes);
  CHECK(ledger.PayReward(100, 7) == Pay::AlreadyPaid); // a resend, a rejoin
  CHECK(ledger.PayReward(100, 8) == Pay::Yes);         // another record of the same gig
  // The same gig with another host: the first note stands, nothing is paid again.
  ledger.NoteQuest(100, QuestKind::StreetStory, OwnState::NotStarted, 0xBBBB);
  CHECK(ledger.PayReward(100, 7) == Pay::AlreadyPaid);
  CHECK(ledger.Find(100)->host == 0xAAAA);
  CHECK(ledger.Find(100)->rewards == 2);
  // The guest's own story later: the same pair pays nothing, others do.
  CHECK_FALSE(ledger.PayInOwnStory(100, 7));
  CHECK(ledger.PayInOwnStory(100, 9));
  CHECK(ledger.PayInOwnStory(0, 7)); // no quest known: the game pays
}

TEST_CASE("a quest the guest finished on its own pays nothing with a host that is behind") {
  Ledger ledger;
  ledger.NoteQuest(200, QuestKind::SideQuest, OwnState::Done, 1);
  CHECK(ledger.PayReward(200, 5) == Pay::DoneAlone);
  CHECK(ledger.PayInOwnStory(200, 5)); // nothing was recorded as paid
  // A reward with no quest known is paid, once.
  CHECK(ledger.PayReward(0, 5) == Pay::Yes);
  CHECK(ledger.PayReward(0, 5) == Pay::AlreadyPaid);
}

TEST_CASE("open-world content finished in co-op is pending for the guest's own world") {
  Ledger ledger;
  ledger.NoteQuest(1, QuestKind::StreetStory, OwnState::NotStarted, 1);
  ledger.NoteQuest(2, QuestKind::StreetStory, OwnState::Done, 1);     // done at home already
  ledger.NoteQuest(3, QuestKind::MainQuest, OwnState::NotStarted, 1); // never applied
  ledger.NoteQuest(4, QuestKind::CyberPsycho, OwnState::Active, 1);
  ledger.NoteQuest(5, QuestKind::Contract, OwnState::NotStarted, 1); // left before its end
  for (const auto quest : {1u, 2u, 3u, 4u})
    ledger.NoteCompleted(quest);
  ledger.NoteCompleted(99); // never noted: ignored
  CHECK(ledger.Pending() == std::vector<std::uint32_t>{1, 4});
  CHECK(ledger.Find(3)->completed);
  CHECK_FALSE(ledger.Find(5)->completed);
  CHECK(ledger.Find(99) == nullptr);
}

TEST_CASE("quest props of a session are taken back out, once") {
  Ledger ledger;
  ledger.NoteSessionItem(0x11, 1);
  ledger.NoteSessionItem(0x11, 2);
  ledger.NoteSessionItem(0x22, 1);
  ledger.NoteSessionItem(0, 1);
  ledger.NoteSessionItem(0x33, 0);
  CHECK(ledger.HasSessionItems());
  const auto items = ledger.TakeSessionItems();
  REQUIRE(items.size() == 2);
  CHECK(items[0] == std::pair<std::uint64_t, std::int32_t>{0x11, 3});
  CHECK(items[1] == std::pair<std::uint64_t, std::int32_t>{0x22, 1});
  CHECK_FALSE(ledger.HasSessionItems());
  CHECK(ledger.TakeSessionItems().empty());
}

TEST_CASE("the ledger survives its durable form; loading twice changes nothing") {
  Ledger ledger;
  ledger.NoteQuest(0xDEADBEEF, QuestKind::StreetStory, OwnState::Active, 0x12345678);
  ledger.NoteQuest(7, QuestKind::MainQuest, OwnState::NotStarted, 9);
  ledger.NoteCompleted(0xDEADBEEF);
  CHECK(ledger.PayReward(0xDEADBEEF, 0xFFFFFFFFFFULL) == Pay::Yes);
  CHECK(ledger.PayReward(0, 3) == Pay::Yes);
  ledger.NoteSessionItem(5, 1); // never packed

  const auto words = ledger.Pack();
  CHECK(words.size() == 8);
  Ledger back;
  back.Unpack(words);
  CHECK(back == ledger);
  CHECK_FALSE(back.HasSessionItems());
  back.Unpack(words); // idempotent
  CHECK(back == ledger);
  CHECK(back.Find(0xDEADBEEF)->kind == QuestKind::StreetStory);
  CHECK(back.Find(0xDEADBEEF)->own == OwnState::Active);
  CHECK(back.Find(0xDEADBEEF)->host == 0x12345678);
  CHECK(back.PayReward(0xDEADBEEF, 0xFFFFFFFFFFULL) == Pay::AlreadyPaid);

  // A newer tandem's words and a torn tail are skipped, not misread.
  auto odd = words;
  odd.insert(odd.begin(), {std::uint64_t{0x7F} << 56 | 42, 43});
  odd.push_back(std::uint64_t{1} << 56 | 5); // half a record
  Ledger tolerant;
  tolerant.Unpack(odd);
  CHECK(tolerant == ledger);

  // Two ledgers of one character (an old save's and a newer one's) merge.
  Ledger other;
  other.NoteQuest(7, QuestKind::MainQuest, OwnState::NotStarted, 9);
  other.NoteCompleted(7);
  back.Unpack(other.Pack());
  CHECK(back.Find(7)->completed);
}

TEST_CASE("a guest is told when the host's story is elsewhere") {
  CHECK(NoteFor({.mainDone = 20}, {.mainDone = 5}) == JoinNote::HostAhead);
  CHECK(NoteFor({.mainDone = 5}, {.mainDone = 20}) == JoinNote::HostBehind);
  CHECK(NoteFor({.mainDone = 6}, {.mainDone = 5}) == JoinNote::None);
  CHECK(NoteFor({.mainDone = 30, .pointOfNoReturn = true}, {.mainDone = 30}) ==
        JoinNote::HostEnding);
  CHECK(NoteFor({.mainDone = 30, .pointOfNoReturn = true},
                {.mainDone = 30, .pointOfNoReturn = true}) == JoinNote::None);
  CHECK(Ep1Missing({.ep1 = true}, {.ep1 = false}));
  CHECK_FALSE(Ep1Missing({.ep1 = false}, {.ep1 = true}));
  CHECK_FALSE(Ep1Missing({.ep1 = true}, {.ep1 = true}));
}
