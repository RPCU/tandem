#include "tandem/story.hpp"

#include <algorithm>

namespace tandem::story {
void Record::Note(const proto::Message &message) {
  if (const auto *journal = std::get_if<proto::Journal>(&message)) {
    if (journal->snapshot) {
      m_journal.clear();
      m_haveJournal = true;
    }
    for (const auto &entry : journal->entries) {
      // Inactive is what a joiner gets for anything not in the snapshot.
      if (entry.state <= 1)
        m_journal.erase(entry.hash);
      else
        m_journal[entry.hash] = {entry.hash, entry.state, 0};
    }
  } else if (const auto *tracked = std::get_if<proto::Tracked>(&message)) {
    m_tracked = *tracked;
  } else if (const auto *call = std::get_if<proto::PhoneCall>(&message)) {
    // A call that ended is nothing for a joiner.
    if (call->phase == 3 || call->phase == 0)
      m_call.reset();
    else
      m_call = *call;
  } else if (const auto *hub = std::get_if<proto::ChoiceHub>(&message)) {
    if (hub->options.empty())
      m_hub.reset();
    else
      m_hub = *hub;
  } else if (const auto *state = std::get_if<proto::StoryState>(&message)) {
    m_states[state->player] = *state;
  }
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
