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
  // Both own Phantom Liberty (required); only the host's story has begun it.
  CHECK(NoteFor({.mainDone = 6, .ep1Started = true}, {.mainDone = 5}) == JoinNote::HostAhead);
  CHECK(NoteFor({.mainDone = 6}, {.mainDone = 5, .ep1Started = true}) == JoinNote::None);
  CHECK(NoteFor({.mainDone = 6, .ep1Started = true}, {.mainDone = 5, .ep1Started = true}) ==
        JoinNote::None);
}

TEST_CASE("a vehicle earned with a host is credited once, and survives the save") {
  Ledger ledger;
  CHECK(ledger.NoteVehicle(0xCA7, 100, false));
  CHECK_FALSE(ledger.NoteVehicle(0xCA7, 100, false)); // said again, a rejoin
  CHECK_FALSE(
      ledger.NoteVehicle(0xCA7, 200, true)); // another host, another quest: the first stands
  CHECK_FALSE(ledger.NoteVehicle(0, 100, false));
  REQUIRE(ledger.FindVehicle(0xCA7));
  CHECK(ledger.FindVehicle(0xCA7)->quest == 100);
  CHECK_FALSE(ledger.FindVehicle(0xCA7)->owned);
  CHECK_FALSE(ledger.FindVehicle(0xCA7)->kept);
  // One the guest's own garage had already: credited, nothing to keep.
  CHECK(ledger.NoteVehicle(0xB1CE, 0, true));
  CHECK(ledger.FindVehicle(0xB1CE)->owned);
  CHECK(ledger.Vehicles().size() == 2);

  // Kept in the guest's own garage (vehicle_rewards = keep): remembered.
  ledger.KeepVehicle(0xCA7);
  ledger.KeepVehicle(0xDEAD); // not one of ours: nothing
  CHECK(ledger.FindVehicle(0xCA7)->kept);

  // Through the save and back; twice over; with quests and rewards beside.
  ledger.NoteQuest(100, QuestKind::SideQuest, OwnState::NotStarted, 0xAAAA);
  CHECK(ledger.PayReward(100, 7) == Pay::Yes);
  const auto words = ledger.Pack();
  Ledger loaded;
  loaded.Unpack(words);
  loaded.Unpack(words);
  CHECK(loaded == ledger);
  CHECK(loaded.Vehicles() == ledger.Vehicles());
  CHECK_FALSE(loaded.NoteVehicle(0xCA7, 100, false));
  // A save from before it was kept, merged with one after: kept.
  Ledger before;
  before.NoteVehicle(0xCA7, 100, false);
  before.Unpack(words);
  CHECK(before.FindVehicle(0xCA7)->kept);
  // A save from before the session forgets it: it can be earned again.
  Ledger older;
  CHECK(older.NoteVehicle(0xCA7, 100, false));
}

TEST_CASE("a helper's own copy of a gig stays playable at home and pays nothing again") {
  // The guest helps the host's gig (decided: grouped at its start, it is
  // the host's). Its own copy has not started.
  constexpr std::uint32_t gig = 0x616;
  constexpr std::uint64_t money = 0xED, item = 0x17E;
  Ledger ledger;
  const auto host = HostHash("Judy");
  ledger.NoteQuest(gig, QuestKind::StreetStory, OwnState::NotStarted, host);
  CHECK(ledger.PayReward(gig, money) == Pay::Yes);
  CHECK(ledger.PayReward(gig, item) == Pay::Yes);
  ledger.NoteCompleted(gig);
  // Credited, never applied: its own world still offers the gig.
  CHECK(ledger.Pending() == std::vector<std::uint32_t>{gig});
  REQUIRE(ledger.Find(gig));
  CHECK(ledger.Find(gig)->host == host);
  CHECK(ledger.Find(gig)->rewards == 2);
  // Played at home later (after a save and a load): the game's own rewards
  // for it are held back, a reward co-op never paid is not.
  Ledger home;
  home.Unpack(ledger.Pack());
  CHECK_FALSE(home.PayInOwnStory(gig, money));
  CHECK_FALSE(home.PayInOwnStory(gig, item));
  CHECK(home.PayInOwnStory(gig, 0xB0B));
  CHECK(home.PayInOwnStory(0, money)); // no quest known: the game pays
  // Helping another host through the same gig pays nothing either.
  home.NoteQuest(gig, QuestKind::StreetStory, OwnState::Active, HostHash("Panam"));
  CHECK(home.PayReward(gig, money) == Pay::AlreadyPaid);
  CHECK(home.Find(gig)->host == host); // the first host stays
}

TEST_CASE("the host's name is kept as a stable hash; old rewards move into the ledger once") {
  CHECK(HostHash("Judy") == HostHash("Judy"));
  CHECK(HostHash("Judy") != HostHash("judy"));
  CHECK(HostHash("") == 2166136261u);
  CHECK(HostHash("a") == 0xE40C292Cu); // FNV-1a, as any other implementation
  // A save from before the ledger kept (reward, quest) pairs: each goes in
  // as paid, and stays paid.
  Ledger ledger;
  CHECK(ledger.PayReward(0x51DE, 0xAAA) == Pay::Yes);
  CHECK(ledger.PayReward(0x51DE, 0xAAA) == Pay::AlreadyPaid);
  CHECK_FALSE(ledger.PayInOwnStory(0x51DE, 0xAAA));
  CHECK(ledger.Entries().empty()); // paid, but no quest taken part in
}
