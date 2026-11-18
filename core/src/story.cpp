#include "tandem/story.hpp"

#include "tandem/consistency.hpp"

#include <algorithm>

namespace tandem::story {
bool Record::Note(const proto::Message &message) {
  if (const auto *journal = std::get_if<proto::Journal>(&message)) {
    auto next = journal->snapshot ? std::map<std::uint32_t, proto::JournalChange>{} : m_journal;
    for (const auto &entry : journal->entries) {
      // Inactive is what a joiner gets for anything not in the snapshot.
      if (entry.state <= 1)
        next.erase(entry.hash);
      else
        next[entry.hash] = {entry.hash, entry.state, 0};
    }
    const bool changed = next != m_journal || (journal->snapshot && !m_haveJournal);
    m_journal = std::move(next);
    m_haveJournal = m_haveJournal || journal->snapshot;
    return changed;
  }
  if (const auto *tracked = std::get_if<proto::Tracked>(&message)) {
    const bool changed = m_tracked != *tracked;
    m_tracked = *tracked;
    return changed;
  }
  if (const auto *call = std::get_if<proto::PhoneCall>(&message)) {
    // A call that ended is nothing for a joiner.
    std::optional<proto::PhoneCall> next;
    if (call->phase != 3 && call->phase != 0)
      next = *call;
    // An ended call always says so (the guest's phone hangs up).
    const bool changed = next != m_call || !next;
    m_call = next;
    return changed;
  }
  if (const auto *hub = std::get_if<proto::ChoiceHub>(&message)) {
    std::optional<proto::ChoiceHub> next;
    if (!hub->options.empty())
      next = *hub;
    const bool changed = next != m_hub || !next;
    m_hub = next;
    return changed;
  }
  if (const auto *state = std::get_if<proto::StoryState>(&message)) {
    const auto it = m_states.find(state->player);
    const bool changed = it == m_states.end() || it->second != *state;
    m_states[state->player] = *state;
    return changed;
  }
  if (const auto *effect = std::get_if<proto::StoryEffect>(&message)) {
    if (effect->applied) {
      const auto it = m_effects.find(effect->effect);
      const bool changed = it == m_effects.end() || it->second != *effect;
      m_effects[effect->effect] = *effect;
      return changed;
    }
    return m_effects.erase(effect->effect) > 0;
  }
  return true; // nothing kept: every one counts
}

std::uint32_t Record::Digest() const {
  consistency::Hasher hash;
  hash.Add(std::uint64_t{m_haveJournal});
  for (const auto &[key, entry] : m_journal)
    hash.Add(std::uint64_t{key}).Add(std::uint64_t{entry.state});
  hash.Add(std::uint64_t{m_tracked ? m_tracked->hash + 1ull : 0ull});
  return hash.Value();
}

void Record::Forget(proto::PlayerId player) { m_states.erase(player); }

std::vector<proto::Message> Record::ForJoiner() const {
  std::vector<proto::Message> out;
  // No snapshot from the host's game yet: nothing to say (an empty one
  // would empty the joiner's journal).
  if (m_haveJournal) {
    proto::Journal snapshot{.snapshot = true};
    snapshot.entries.reserve(m_journal.size());
    for (const auto &[hash, entry] : m_journal)
      snapshot.entries.push_back(entry);
    out.push_back(std::move(snapshot));
  }
  if (m_tracked)
    out.push_back(*m_tracked);
  for (const auto &[player, state] : m_states)
    out.push_back(state);
  if (m_call)
    out.push_back(*m_call);
  if (m_hub)
    out.push_back(*m_hub);
  for (const auto &[id, effect] : m_effects)
    out.push_back(effect);
  return out;
}

void Participation::Note(std::uint32_t quest, const std::vector<proto::PlayerId> &near,
                         std::uint64_t nowUs) {
  if (quest == 0)
    return;
  auto &players = m_players[quest];
  players.insert(near.begin(), near.end());
  m_lastQuest = quest;
  m_lastUs = nowUs;
}

Participation::Award Participation::Attribute(std::uint64_t nowUs,
                                              const std::vector<proto::PlayerId> &nearNow) const {
  Award award;
  if (m_lastQuest != 0 && nowUs >= m_lastUs && nowUs - m_lastUs <= kRewardWindowUs) {
    award.quest = m_lastQuest;
    // Near now counts too: the completion is usually the change that just
    // happened, noted with the same positions.
    std::set<proto::PlayerId> players = Of(m_lastQuest);
    players.insert(nearNow.begin(), nearNow.end());
    award.players.assign(players.begin(), players.end());
    return award;
  }
  award.players = nearNow;
  std::ranges::sort(award.players);
  return award;
}

std::set<proto::PlayerId> Participation::Of(std::uint32_t quest) const {
  const auto it = m_players.find(quest);
  return it == m_players.end() ? std::set<proto::PlayerId>{} : it->second;
}

void Participation::Forget(proto::PlayerId player) {
  for (auto &[quest, players] : m_players)
    players.erase(player);
}

void CallArbiter::Update(const proto::PhoneCall &call) {
  if (m_call && m_call->contact == call.contact && call.phase != 1)
    m_call = {call.contact, call.phase, call.mode, call.visuals, call.flags, m_call->answeredBy};
  else
    m_call = call;
  if (call.answeredBy != proto::kNoPlayer)
    m_call->answeredBy = call.answeredBy;
}

bool CallArbiter::TryAnswer(proto::PlayerId player, std::uint8_t action, std::uint64_t contact) {
  if (!m_call || m_call->phase != 1 || m_call->contact != contact ||
      m_call->answeredBy != proto::kNoPlayer || player == proto::kNoPlayer)
    return false;
  if (action != proto::kPhoneAnswer && action != proto::kPhoneReject)
    return false;
  m_call->answeredBy = player;
  if (action == proto::kPhoneReject)
    m_call->flags |= proto::kCallRejected;
  return true;
}
} // namespace tandem::story
