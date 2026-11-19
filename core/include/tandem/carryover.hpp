#pragma once

// What a guest brings home from a host's world
// (docs/architecture/quest-carryover.md): the rules, and the guest's ledger
// of what it did and earned in co-op. No game needed: tested on Linux.
//
// The ledger's durable form is a list of 64-bit words, kept by the guest's
// own game in a persistent script field: it is written with the guest's
// save and comes back with it, so the ledger and the inventory it describes
// can never disagree (a save loaded from before a session forgets both).

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace tandem::carry {
// gameJournalQuestType (final.reds:7102).
enum class QuestKind : std::uint8_t {
  MainQuest = 0,
  SideQuest = 1,
  MinorQuest = 2,
  StreetStory = 3, // gigs
  CyberPsycho = 4,
  Contract = 5,
  VehicleQuest = 6,
  ApartmentQuest = 7,
  CourierQuest = 8,
  CourierSideQuest = 9,
  Unknown = 255, // no journal quest (an NCPD hustle, an unattributed reward)
};

// What happens to it in the guest's own world.
enum class Tier : std::uint8_t {
  Kept = 1,       // the V's own: XP, money, loot (always the guest's)
  OpenWorld = 2,  // self-contained content: its completion may carry
  StoryQuest = 3, // main and side quests: recorded, never applied
};
// Gigs, cyberpsycho sightings and courier jobs are self-contained; a quest
// that unlocks a vehicle or an apartment changes what the guest owns in its
// own world, so it stays a story quest, with the main and side quests.
Tier TierOf(QuestKind kind);

// The guest's own state of a quest when it joined (its journal).
enum class OwnState : std::uint8_t {
  Unknown = 0,
  NotStarted = 1,
  Active = 2,
  Done = 3,
  Failed = 4
};

// One quest played in co-op.
struct Entry {
  std::uint32_t quest{}; // journal hash of the gameJournalQuest
  QuestKind kind{QuestKind::Unknown};
  OwnState own{};         // the guest's own state when it first took part
  bool completed{};       // seen to its end with the host
  std::uint32_t host{};   // a hash of the host's name (for "done with NAME")
  std::uint8_t rewards{}; // reward records paid for it (up to 255)
  friend bool operator==(const Entry &, const Entry &) = default;
};

// Why a reward is, or is not, paid to the guest.
enum class Pay : std::uint8_t {
  Yes,
  AlreadyPaid, // this ledger has it: a resend, a rejoin, another host
  DoneAlone,   // the guest finished that quest in its own story already
};

class Ledger {
public:
  // The guest took part in a quest's step (near the host as it moved).
  // The first note of a quest fixes its kind, the guest's own state and the
  // host; later ones only add.
  void NoteQuest(std::uint32_t quest, QuestKind kind, OwnState own, std::uint32_t host);
  // The host's quest reached its end with the guest taking part.
  void NoteCompleted(std::uint32_t quest);

  // The host pays `reward` for `quest` (0: no quest known). Decides, and on
  // Yes records it: asking twice pays once, whoever the host is.
  Pay PayReward(std::uint32_t quest, std::uint64_t reward);
  // The guest's own story is about to pay `reward` for `quest`: false when
  // co-op already paid it.
  bool PayInOwnStory(std::uint32_t quest, std::uint64_t reward) const;

  // Items that are a quest's props (tag Quest, keycards) come into the
  // guest's inventory in a session so nothing breaks there; they are not
  // the guest's to keep: its own quests check for them
  // (questInventory_ConditionType). Counted per record; taken back out when
  // the guest leaves, before any save.
  void NoteSessionItem(std::uint64_t record, std::int32_t quantity);
  // What to remove now (and forget): record, quantity.
  std::vector<std::pair<std::uint64_t, std::int32_t>> TakeSessionItems();
  bool HasSessionItems() const { return !m_sessionItems.empty(); }

  const Entry *Find(std::uint32_t quest) const;
  std::vector<Entry> Entries() const;
  // Open-world content finished in co-op that the guest's own world has not
  // done: what a later stage may apply there (carryover stage B).
  std::vector<std::uint32_t> Pending() const;

  // The durable form. Unpack accepts what Pack wrote, in any order, twice
  // over (merging is idempotent), and ignores words it does not know (a
  // newer tandem's). Session items are never packed: they do not outlive a
  // session.
  std::vector<std::uint64_t> Pack() const;
  void Unpack(std::span<const std::uint64_t> words);
  friend bool operator==(const Ledger &a, const Ledger &b) {
    return a.m_entries == b.m_entries && a.m_paid == b.m_paid;
  }

private:
  std::map<std::uint32_t, Entry> m_entries;
  std::set<std::pair<std::uint32_t, std::uint64_t>> m_paid; // quest, reward
  std::map<std::uint64_t, std::int32_t> m_sessionItems;
};

// Joining a host whose story is elsewhere (design "edge cases"): what the
// guest is told before it joins. Main quests done on each side, and whether
// each story has begun Phantom Liberty (every player owns it: the session
// requires it at join).
struct StoryMark {
  std::uint16_t mainDone{};
  bool pointOfNoReturn{}; // past it (an ending is being played)
  bool ep1Started{};      // its story has begun Phantom Liberty
  friend bool operator==(const StoryMark &, const StoryMark &) = default;
};
enum class JoinNote : std::uint8_t {
  None,
  HostAhead,  // spoilers: the host is further in the story
  HostEnding, // the host is past the point of no return
  HostBehind, // quests the guest has done pay nothing again
};
JoinNote NoteFor(const StoryMark &host, const StoryMark &guest);
inline constexpr std::uint16_t kAheadBy = 3; // main quests of difference worth a word
} // namespace tandem::carry
