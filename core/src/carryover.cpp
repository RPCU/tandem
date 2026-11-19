#include "tandem/carryover.hpp"

#include <algorithm>

namespace tandem::carry {
namespace {
// Every record is two words; the first word's top byte says which:
//   1  entry: [tag | kind 8 | own 7 | completed 1 | rewards 8 | quest 32],
//             then [host 32]
//   2  paid:  [tag | quest 32], then the reward record
constexpr std::uint64_t kEntry = 1, kPaid = 2;
constexpr int kTagShift = 56;
} // namespace

Tier TierOf(QuestKind kind) {
  switch (kind) {
  case QuestKind::StreetStory:
  case QuestKind::CyberPsycho:
  case QuestKind::Contract:
  case QuestKind::CourierQuest:
  case QuestKind::CourierSideQuest:
  case QuestKind::Unknown:
    return Tier::OpenWorld;
  case QuestKind::MainQuest:
  case QuestKind::SideQuest:
  case QuestKind::MinorQuest:
  case QuestKind::VehicleQuest:
  case QuestKind::ApartmentQuest:
    return Tier::StoryQuest;
  }
  return Tier::StoryQuest;
}

void Ledger::NoteQuest(std::uint32_t quest, QuestKind kind, OwnState own, std::uint32_t host) {
  if (quest == 0)
    return;
  const auto [it, fresh] = m_entries.try_emplace(quest);
  if (fresh)
    it->second = {.quest = quest, .kind = kind, .own = own, .host = host};
}

void Ledger::NoteCompleted(std::uint32_t quest) {
  if (const auto it = m_entries.find(quest); it != m_entries.end())
    it->second.completed = true;
}

Pay Ledger::PayReward(std::uint32_t quest, std::uint64_t reward) {
  if (m_paid.contains({quest, reward}))
    return Pay::AlreadyPaid;
  if (const auto *entry = Find(quest); entry && entry->own == OwnState::Done)
    return Pay::DoneAlone;
  m_paid.insert({quest, reward});
  if (const auto it = m_entries.find(quest); it != m_entries.end() && it->second.rewards < 0xFF)
    ++it->second.rewards;
  return Pay::Yes;
}

bool Ledger::PayInOwnStory(std::uint32_t quest, std::uint64_t reward) const {
  // Without a quest nothing is known: the game pays.
  return quest == 0 || !m_paid.contains({quest, reward});
}

void Ledger::NoteSessionItem(std::uint64_t record, std::int32_t quantity) {
  if (record != 0 && quantity > 0)
    m_sessionItems[record] += quantity;
}

std::vector<std::pair<std::uint64_t, std::int32_t>> Ledger::TakeSessionItems() {
  std::vector<std::pair<std::uint64_t, std::int32_t>> out(m_sessionItems.begin(),
                                                          m_sessionItems.end());
  m_sessionItems.clear();
  return out;
}

const Entry *Ledger::Find(std::uint32_t quest) const {
  const auto it = m_entries.find(quest);
  return it == m_entries.end() ? nullptr : &it->second;
}

std::vector<Entry> Ledger::Entries() const {
  std::vector<Entry> out;
  for (const auto &[quest, entry] : m_entries)
    out.push_back(entry);
  return out;
}

std::vector<std::uint32_t> Ledger::Pending() const {
  std::vector<std::uint32_t> out;
  for (const auto &[quest, entry] : m_entries)
    if (entry.completed && TierOf(entry.kind) == Tier::OpenWorld &&
        entry.kind != QuestKind::Unknown &&
        (entry.own == OwnState::NotStarted || entry.own == OwnState::Active))
      out.push_back(quest);
  return out;
}

std::vector<std::uint64_t> Ledger::Pack() const {
  std::vector<std::uint64_t> words;
  for (const auto &[quest, e] : m_entries) {
    words.push_back(kEntry << kTagShift | std::uint64_t{static_cast<std::uint8_t>(e.kind)} << 48 |
                    std::uint64_t{static_cast<std::uint8_t>(e.own) & 0x7Fu} << 41 |
                    std::uint64_t{e.completed} << 40 | std::uint64_t{e.rewards} << 32 | e.quest);
    words.push_back(e.host);
  }
  for (const auto &[quest, reward] : m_paid) {
    words.push_back(kPaid << kTagShift | quest);
    words.push_back(reward);
  }
  return words;
}

void Ledger::Unpack(std::span<const std::uint64_t> words) {
  // Every record is two words; an unknown tag skips its two.
  for (std::size_t i = 0; i + 1 < words.size(); i += 2) {
    const auto head = words[i], tail = words[i + 1];
    const auto quest = static_cast<std::uint32_t>(head);
    switch (head >> kTagShift) {
    case kEntry: {
      Entry entry{.quest = quest,
                  .kind = static_cast<QuestKind>((head >> 48) & 0xFF),
                  .own = static_cast<OwnState>((head >> 41) & 0x7F),
                  .completed = ((head >> 40) & 1) != 0,
                  .host = static_cast<std::uint32_t>(tail),
                  .rewards = static_cast<std::uint8_t>((head >> 32) & 0xFF)};
      if (quest == 0)
        break;
      const auto [it, fresh] = m_entries.try_emplace(quest, entry);
      if (!fresh) { // merged: what either side knows
        it->second.completed |= entry.completed;
        it->second.rewards = std::max(it->second.rewards, entry.rewards);
      }
      break;
    }
    case kPaid:
      m_paid.insert({quest, tail});
      break;
    default:
      break;
    }
  }
}

JoinNote NoteFor(const StoryMark &host, const StoryMark &guest) {
  if (host.pointOfNoReturn && !guest.pointOfNoReturn)
    return JoinNote::HostEnding;
  // Dogtown before the guest's own story got there is a spoiler too.
  if (host.mainDone >= guest.mainDone + kAheadBy || (host.ep1Started && !guest.ep1Started))
    return JoinNote::HostAhead;
  if (guest.mainDone >= host.mainDone + kAheadBy)
    return JoinNote::HostBehind;
  return JoinNote::None;
}

} // namespace tandem::carry
