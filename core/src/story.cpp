#include "tandem/story.hpp"

#include "tandem/consistency.hpp"
#include "tandem/scene.hpp"

#include <algorithm>
#include <cmath>

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
  if (const auto *restrictions = std::get_if<proto::StoryRestrictions>(&message)) {
    // The whole set each time; a repeat (a resync of what never changed)
    // changes nothing, the explicit "none" of a resync included.
    const bool changed = restrictions->effects != m_restrictions.effects;
    m_restrictions = *restrictions;
    return changed;
  }
  if (const auto *prefabs = std::get_if<proto::WorldPrefabs>(&message)) {
    // The whole set each time, as the restrictions: a repeat changes nothing.
    const bool changed = prefabs->prefabs != m_prefabs.prefabs;
    m_prefabs = *prefabs;
    return changed;
  }
  if (const auto *gates = std::get_if<proto::StoryGates>(&message)) {
    const auto it = m_gates.find(gates->player);
    const bool changed = it == m_gates.end() || it->second != *gates;
    m_gates[gates->player] = *gates;
    return changed;
  }
  if (const auto *scene = std::get_if<proto::SceneState>(&message)) {
    std::optional<proto::SceneState> next;
    if (scene->scene != 0)
      next = *scene;
    // Another play, or none: the old one's cast and lines went with it.
    if (!next || !m_scene || m_scene->play != next->play) {
      m_cast.reset();
      m_props.clear();
      m_placed.clear();
      std::erase_if(m_lines, [](const proto::SceneLine &line) { return line.play != 0; });
    }
    // An end always says so (the guest lets its proxies go).
    const bool changed = next != m_scene || !next;
    m_scene = next;
    return changed;
  }
  if (const auto *cast = std::get_if<proto::SceneCast>(&message)) {
    if (!m_scene || m_scene->play != cast->play)
      return false; // of a play that is over
    const bool changed = m_cast != *cast;
    m_cast = *cast;
    return changed;
  }
  if (const auto *prop = std::get_if<proto::SceneProp>(&message)) {
    if (!m_scene || m_scene->play != prop->play)
      return false; // of a play that is over
    if (prop->flags & proto::kPropGone)
      return m_props.erase(prop->prop) > 0;
    const auto it = m_props.find(prop->prop);
    if (it != m_props.end() && it->second == *prop)
      return false;
    if (it == m_props.end() && m_props.size() >= proto::kMaxSceneProps)
      return false;
    m_props[prop->prop] = *prop;
    return true;
  }
  if (const auto *place = std::get_if<proto::ScenePlace>(&message)) {
    if (!m_scene || m_scene->play != place->play)
      return false; // of a play that is over
    if (place->flags & proto::kPropGone)
      return m_placed.erase(place->prop) > 0;
    const auto it = m_placed.find(place->prop);
    if (it != m_placed.end() && it->second == *place)
      return false;
    if (it == m_placed.end() && m_placed.size() >= proto::kMaxSceneProps)
      return false;
    m_placed[place->prop] = *place;
    return true;
  }
  if (const auto *line = std::get_if<proto::SceneLine>(&message)) {
    const auto same = [&](const proto::SceneLine &kept) { return kept.line == line->line; };
    if (line->flags & proto::kVoiceStop)
      return std::erase_if(m_lines, same) > 0;
    if (line->play != 0 && (!m_scene || m_scene->play != line->play))
      return false;
    if (std::ranges::find(m_lines, *line) != m_lines.end())
      return false;
    std::erase_if(m_lines, same);
    m_lines.push_back(*line);
    if (m_lines.size() > kKeptLines)
      m_lines.erase(m_lines.begin());
    return true;
  }
  return true; // nothing kept: every one counts
}

std::uint32_t Record::Digest() const {
  consistency::Hasher hash;
  hash.Add(std::uint64_t{m_haveJournal});
  for (const auto &[key, entry] : m_journal)
    hash.Add(std::uint64_t{key}).Add(std::uint64_t{entry.state});
  hash.Add(std::uint64_t{m_tracked ? m_tracked->hash + 1ull : 0ull});
  // The scene: which one and who is in it (its staging moves too often).
  hash.Add(std::uint64_t{m_scene ? m_scene->scene : 0u})
      .Add(std::uint64_t{m_scene ? m_scene->play : 0u});
  if (m_cast)
    for (const auto &actor : m_cast->actors)
      hash.Add(std::uint64_t{actor.npc}).Add(std::uint64_t{actor.flags});
  // Its props: one lost would stay in a proxy's hand, or never get there.
  for (const auto &[id, prop] : m_props)
    hash.Add(std::uint64_t{id})
        .Add(prop.record)
        .Add(std::uint64_t{prop.holder})
        .Add(std::uint64_t{prop.holderPlayer})
        .Add(std::uint64_t{prop.flags});
  // The props set down: which ones (where they are moves too often).
  for (const auto &[id, place] : m_placed)
    hash.Add(static_cast<std::uint64_t>(id) | std::uint64_t{1} << 32).Add(place.entity);
  // The host's restrictions: a lift lost would hold a guest for good.
  hash.Add(std::uint64_t{m_restrictions.effects.size()});
  for (const auto effect : m_restrictions.effects)
    hash.Add(effect);
  // The host's world prefabs: a change lost would keep an interior loaded
  // in a guest's world, or missing, for good.
  hash.Add(std::uint64_t{m_prefabs.prefabs.size()} | std::uint64_t{1} << 40);
  for (const auto prefab : m_prefabs.prefabs)
    hash.Add(prefab);
  return hash.Value();
}

void Record::Forget(proto::PlayerId player) {
  m_states.erase(player);
  m_gates.erase(player);
}

std::optional<proto::StoryGates> Record::GatesOf(proto::PlayerId player) const {
  const auto it = m_gates.find(player);
  if (it == m_gates.end())
    return std::nullopt;
  return it->second;
}

std::vector<proto::Message> Record::ForJoiner(std::uint64_t hostNowUs) const {
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
  if (RestrictionsOn())
    out.push_back(m_restrictions);
  if (PrefabsOn())
    out.push_back(m_prefabs);
  for (const auto &[player, gates] : m_gates)
    out.push_back(gates);
  if (m_scene) {
    out.push_back(*m_scene);
    if (m_cast)
      out.push_back(*m_cast);
    for (const auto &[id, prop] : m_props)
      out.push_back(prop);
    for (const auto &[id, place] : m_placed)
      out.push_back(place);
  }
  for (const auto &line : m_lines) {
    const float seconds = line.duration > 0.f ? line.duration : scene::kUnknownLineSeconds;
    const auto endUs =
        line.startUs + static_cast<std::uint64_t>(static_cast<double>(seconds) * 1e6);
    if (hostNowUs == 0 || endUs > hostNowUs)
      out.push_back(line);
  }
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

namespace {
float Distance(const proto::Vec3 &a, const proto::Vec3 &b) {
  const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}
} // namespace

bool InMoment(const MomentInput &in, bool was) {
  if (in.tier < 2 && !in.restricted)
    return false; // no moment on the host's side
  if (!in.grouped || in.away || !in.hostKnown)
    return false;
  const float near = was ? kMomentNearLeave : kMomentNear;
  if (Distance(in.guest, in.host) <= near)
    return true;
  if (!in.staged || !(in.radius >= 0.f))
    return false;
  const float area =
      std::min(in.radius, kMomentStageCap) + (was ? kMomentStageLeave : kMomentStageMargin);
  return Distance(in.guest, in.anchor) <= area;
}

std::uint8_t MomentReasons(const MomentSignals &signals) {
  if (signals.isPrivate)
    return 0;
  std::uint8_t reasons = 0;
  if (signals.tier >= 2)
    reasons |= kMomentScene;
  if (signals.tier >= 5)
    reasons |= kMomentCutscene;
  if (signals.restricted)
    reasons |= kMomentRestricted;
  if (signals.questMount)
    reasons |= kMomentMounted;
  if (signals.mode)
    reasons |= kMomentMode;
  if (signals.camera)
    reasons |= kMomentCamera;
  if (signals.elsewhere)
    reasons |= kMomentElsewhere;
  if (signals.test)
    reasons |= kMomentTest;
  return reasons;
}

namespace {
bool Held(bool &on, std::uint64_t &lastUs, bool now, std::uint64_t nowUs) {
  if (now) {
    on = true;
    lastUs = nowUs;
  } else if (on && (nowUs < lastUs || nowUs - lastUs >= kMomentHoldUs)) {
    on = false;
  }
  return on;
}
} // namespace

bool HostMoment::Update(std::uint8_t reasons, std::uint64_t nowUs) {
  Held(m_watch, m_watchLastUs, (reasons & kMomentWatch) != 0, nowUs);
  return Held(m_on, m_lastUs, reasons != 0, nowUs);
}

void HostMoment::Clear() {
  m_on = m_watch = false;
  m_lastUs = m_watchLastUs = 0;
}

Spectator::Change Spectator::Update(const SpectateInput &in) {
  const bool moment = in.hostMoment && !in.hostPrivate;
  if (!moment)
    m_shared = false;
  const bool able = in.on && moment && in.grouped && !in.ourAway;
  if (able && !m_shared)
    m_shared = InMoment({.grouped = true,
                         .away = false,
                         .hostKnown = in.hostKnown,
                         .host = in.host,
                         .guest = in.guest,
                         .restricted = true},
                        false);
  Why why = Why::None;
  if (able && m_shared) {
    if (in.hostWatch) {
      why = in.hostElsewhere ? Why::Elsewhere : in.hostTier >= 5 ? Why::Cutscene : Why::View;
    } else if (!in.inVehicle) {
      const bool taken = m_watching && m_why == Why::Taken;
      const float distance = Distance(in.guest, in.host);
      if (m_watching && in.hostRiding)
        why = Why::Taken; // carried in its copy: until its ride ends
      else if (taken && !in.hostKnown)
        why = Why::Taken; // its puppet streaming in
      else if (in.hostKnown && distance > (taken ? kSpectateReachBack : kSpectateReach))
        why = Why::Taken;
    }
  }
  if (why == Why::None)
    m_declined = false; // the next reason is offered again
  if (m_declined)
    why = Why::None;
  const bool want = why != Why::None;
  m_why = why;
  if (want == m_watching)
    return Change::None;
  m_watching = want;
  return want ? Change::Start : Change::Stop;
}

Spectator::Change Spectator::Leave() {
  if (!m_watching)
    return Change::None;
  m_watching = false;
  m_declined = true;
  m_why = Why::None;
  return Change::Stop;
}

Spectator::Change Spectator::Lift() {
  m_declined = false;
  m_shared = false;
  m_why = Why::None;
  if (!m_watching)
    return Change::None;
  m_watching = false;
  return Change::Stop;
}

Restrictions::Change Restrictions::Update(bool inMoment, const std::vector<std::uint64_t> &holds) {
  std::set<std::uint64_t> wanted(holds.begin(), holds.end());
  if (inMoment)
    wanted.insert(m_host.begin(), m_host.end());
  Change change;
  for (auto it = m_held.begin(); it != m_held.end();) {
    if (wanted.contains(it->first)) {
      ++it;
      continue;
    }
    // Theirs is forgotten (ours to decide again next time), never removed.
    if (it->second)
      change.remove.push_back(it->first);
    it = m_held.erase(it);
  }
  for (const auto effect : wanted)
    if (m_held.emplace(effect, true).second)
      change.apply.push_back(effect);
  return change;
}

void Restrictions::Theirs(std::uint64_t effect) {
  if (const auto it = m_held.find(effect); it != m_held.end())
    it->second = false;
}

void Restrictions::Gone(std::uint64_t effect) { m_held.erase(effect); }

Restrictions::Change Restrictions::Lift() {
  Change change;
  for (const auto &[effect, ours] : m_held)
    if (ours)
      change.remove.push_back(effect);
  m_held.clear();
  m_host.clear();
  return change;
}

bool Restrictions::Ours(std::uint64_t effect) const {
  const auto it = m_held.find(effect);
  return it != m_held.end() && it->second;
}

std::size_t Restrictions::OursCount() const {
  return static_cast<std::size_t>(
      std::ranges::count_if(m_held, [](const auto &held) { return held.second; }));
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

bool IsSceneActor(const SceneActor &npc) {
  return npc.cast || ((npc.quest || npc.kept) && npc.inScene);
}

Backstop::Verdict Backstop::Judge(bool moment, const SceneActor &npc, GuestAct act,
                                  std::uint64_t nowUs) {
  if (!moment || !IsSceneActor(npc))
    return {};
  const auto i = Index(act);
  ++m_refused[i];
  auto &logged = m_loggedUs[i];
  const bool log = !logged || nowUs - *logged >= kBackstopLogGapUs;
  if (log)
    logged = nowUs;
  return {.refuse = true, .log = log};
}
} // namespace tandem::story
