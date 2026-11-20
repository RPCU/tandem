#include "tandem/consistency.hpp"

#include "tandem/regions.hpp"

#include <algorithm>
#include <bit>

namespace tandem::consistency {
void Hasher::Byte(std::uint8_t byte) {
  m_hash ^= byte;
  m_hash *= 16777619u;
}

Hasher &Hasher::Add(std::uint64_t value) {
  for (int i = 0; i < 8; ++i)
    Byte(static_cast<std::uint8_t>(value >> (8 * i)));
  return *this;
}

Hasher &Hasher::Add(float value) { return Add(std::uint64_t{std::bit_cast<std::uint32_t>(value)}); }

Hasher &Hasher::Add(std::string_view text) {
  Add(std::uint64_t{text.size()});
  for (const char c : text)
    Byte(static_cast<std::uint8_t>(c));
  return *this;
}

std::uint32_t WorldDigest(const std::optional<proto::World> &world,
                          const std::optional<proto::Difficulty> &difficulty,
                          const std::optional<proto::Rules> &rules) {
  Hasher hash;
  if (world)
    hash.Add(std::uint64_t{world->revision})
        .Add(static_cast<std::uint64_t>(world->gameTimeSeconds))
        .Add(world->weather)
        .Add(std::uint64_t{world->timePaused});
  if (difficulty)
    hash.Add(std::uint64_t{0xD0}).Add(std::uint64_t{difficulty->level});
  if (rules)
    hash.Add(std::uint64_t{0xD1})
        .Add(std::uint64_t{rules->flags})
        .Add(std::uint64_t{rules->enemyHealth});
  return hash.Value();
}

std::uint32_t NpcDigest(const std::map<proto::NpcId, proto::NpcSpawn> &npcs,
                        const std::map<proto::NpcId, proto::NpcHealth> &health,
                        const std::map<proto::NpcId, proto::NpcHold> &holds,
                        const std::map<proto::NpcId, proto::PlayerId> &owners,
                        const NpcFilter &only, std::uint16_t *count,
                        const std::map<proto::NpcId, proto::NpcScale> *scales) {
  Hasher hash;
  std::size_t counted = 0;
  for (const auto &[id, spawn] : npcs) {
    const auto announcer = regions::Announcer(id);
    const auto owned = owners.find(id);
    const auto owner = owned == owners.end() ? announcer : owned->second;
    if (only && !only(id, owner))
      continue;
    ++counted;
    hash.Add(std::uint64_t{id}).Add(spawn.record).Add(spawn.appearance);
    // Health of the NPCs shared now only: what the host said of one that
    // died or went is no one's business.
    if (const auto it = health.find(id); it != health.end())
      hash.Add(it->second.health);
    if (const auto it = holds.find(id); it != holds.end())
      hash.Add(std::uint64_t{0xE0})
          .Add(std::uint64_t{it->second.player})
          .Add(std::uint64_t{static_cast<std::uint8_t>(it->second.how)});
    // Handed over: whoever runs it now.
    if (owner != announcer)
      hash.Add(std::uint64_t{0xE1}).Add(std::uint64_t{owner});
    // Scaled for more than one player (alone, said or not, is the same).
    if (scales)
      if (const auto it = scales->find(id); it != scales->end() && it->second.players > 1)
        hash.Add(std::uint64_t{0xE2}).Add(std::uint64_t{it->second.players});
  }
  if (count)
    *count = static_cast<std::uint16_t>(std::min<std::size_t>(counted, 0xFFFF));
  return hash.Value();
}

std::uint32_t TrafficDigest(const std::map<proto::TrafficId, proto::TrafficSpawn> &traffic,
                            const std::map<proto::TrafficId, proto::TrafficStatus> &status,
                            const std::map<proto::TrafficId, proto::PlayerId> &owners,
                            const TrafficFilter &only, std::uint16_t *count) {
  Hasher hash;
  std::size_t counted = 0;
  for (const auto &[id, spawn] : traffic) {
    const auto announcer = proto::TrafficOwner(id);
    const auto owned = owners.find(id);
    const auto owner = owned == owners.end() ? announcer : owned->second;
    if (only && !only(id, owner))
      continue;
    ++counted;
    hash.Add(std::uint64_t{id}).Add(spawn.record).Add(spawn.appearance).Add(spawn.entity);
    // Handed over: whoever runs it now.
    if (owner != announcer)
      hash.Add(std::uint64_t{0xE2}).Add(std::uint64_t{owner});
    // Without a status it stands as it was announced.
    if (const auto it = status.find(id); it != status.end())
      hash.Add(it->second.health).Add(std::uint64_t{it->second.flags});
    else
      hash.Add(std::uint64_t{0xE1}).Add(std::uint64_t{spawn.flags});
  }
  if (count)
    *count = static_cast<std::uint16_t>(std::min<std::size_t>(counted, 0xFFFF));
  return hash.Value();
}

std::uint32_t PlayersDigest(const PlayersView &players, proto::PlayerId self) {
  Hasher hash;
  for (const auto &[id, info] : players.roster)
    hash.Add(std::uint64_t{id})
        .Add(info.name)
        .Add(std::uint64_t{static_cast<std::uint8_t>(info.body)});
  hash.Add(std::uint64_t{0xA0});
  for (const auto &[id, a] : players.appearances)
    if (id != self) {
      hash.Add(std::uint64_t{id}).Add(std::uint64_t{a.revision}).Add(std::uint64_t{a.look.size()});
      for (const auto &key : a.look)
        hash.Add(key.resource).Add(key.definition);
      for (const auto item : a.items)
        hash.Add(item);
    }
  hash.Add(std::uint64_t{0xA1});
  for (const auto &[id, h] : players.held)
    if (id != self)
      hash.Add(std::uint64_t{id}).Add(std::uint64_t{h.revision}).Add(h.right).Add(h.left);
  hash.Add(std::uint64_t{0xA2});
  for (const auto &[id, s] : players.seats)
    if (id != self)
      hash.Add(std::uint64_t{id})
          .Add(std::uint64_t{s.revision})
          .Add(s.vehicle)
          .Add(s.seat)
          .Add(std::uint64_t{s.owner});
  hash.Add(std::uint64_t{0xA3});
  for (const auto &[id, r] : players.radios)
    if (id != self)
      hash.Add(std::uint64_t{id})
          .Add(std::uint64_t{r.revision})
          .Add(std::uint64_t{r.on})
          .Add(static_cast<std::uint64_t>(r.station))
          .Add(r.track);
  hash.Add(std::uint64_t{0xA4});
  for (const auto &[id, s] : players.stealth)
    if (id != self)
      hash.Add(std::uint64_t{id})
          .Add(std::uint64_t{s.revision})
          .Add(std::uint64_t{s.flags})
          .Add(s.visibility)
          .Add(s.camo);
  hash.Add(std::uint64_t{0xA5});
  for (const auto id : players.paused)
    if (id != self)
      hash.Add(std::uint64_t{id});
  hash.Add(std::uint64_t{0xA6});
  for (const auto &[id, t] : players.time)
    if (id != self)
      hash.Add(std::uint64_t{id}).Add(std::uint64_t{t.abilities}).Add(t.scale);
  hash.Add(std::uint64_t{0xA7});
  for (const auto &[id, v] : players.lives)
    if (id != self && v.life != proto::Life::Up)
      hash.Add(std::uint64_t{id}).Add(std::uint64_t{static_cast<std::uint8_t>(v.life)});
  hash.Add(std::uint64_t{0xA8});
  for (const auto &[id, e] : players.elsewhere)
    if (id != self && e.place != proto::Place::City)
      hash.Add(std::uint64_t{id})
          .Add(std::uint64_t{static_cast<std::uint8_t>(e.place)})
          .Add(std::uint64_t{e.flags})
          .Add(e.anchor.x)
          .Add(e.anchor.y)
          .Add(e.anchor.z);
  hash.Add(std::uint64_t{0xA9});
  for (const auto &[id, h] : players.heat)
    if (id != self && h.stage != 0)
      hash.Add(std::uint64_t{id})
          .Add(std::uint64_t{h.stage})
          .Add(std::uint64_t{static_cast<std::uint8_t>(h.state)});
  hash.Add(std::uint64_t{0xAA});
  for (const auto &[id, a] : players.activities)
    if (id != self && a.kind != proto::ActivityKind::None)
      hash.Add(std::uint64_t{id})
          .Add(std::uint64_t{static_cast<std::uint8_t>(a.kind)})
          .Add(std::uint64_t{a.flags})
          .Add(a.device)
          .Add(a.workspot)
          .Add(std::uint64_t{a.entry})
          .Add(a.anchor.x)
          .Add(a.anchor.y)
          .Add(a.anchor.z);
  // Comms: links that share with someone, waypoints that are set (an off
  // link and no waypoint count like one never heard of).
  if (players.links) {
    hash.Add(std::uint64_t{0xAB});
    for (const auto &[id, l] : *players.links)
      if (id != self && (l.mode != proto::LinkMode::Off || !l.players.empty())) {
        hash.Add(std::uint64_t{id}).Add(std::uint64_t{static_cast<std::uint8_t>(l.mode)});
        for (const auto other : l.players)
          hash.Add(std::uint64_t{other});
      }
  }
  if (players.waypoints) {
    hash.Add(std::uint64_t{0xAC});
    for (const auto &[id, w] : *players.waypoints)
      if (id != self && w.kind != proto::WaypointKind::None)
        hash.Add(std::uint64_t{id})
            .Add(std::uint64_t{static_cast<std::uint8_t>(w.kind)})
            .Add(w.position.x)
            .Add(w.position.y)
            .Add(w.position.z);
  }
  if (players.regionPairs) {
    hash.Add(std::uint64_t{0xAD});
    for (const auto id : *players.regionPairs)
      hash.Add(std::uint64_t{id});
  }
  return hash.Value();
}

std::uint8_t Differences(const proto::Digest &a, const proto::Digest &b) {
  std::uint8_t out = 0;
  if (a.world != b.world)
    out |= proto::kDomainWorld;
  if (a.npcs != b.npcs || a.npcCount != b.npcCount)
    out |= proto::kDomainNpcs;
  if (a.story != b.story)
    out |= proto::kDomainStory;
  if (a.players != b.players)
    out |= proto::kDomainPlayers;
  return out;
}

std::uint8_t Checker::Compare(const proto::Digest &host, const proto::Digest &ours,
                              std::uint64_t nowUs) {
  const auto differ = Differences(host, ours);
  if (differ)
    ++m_mismatches;
  // Twice in a row: the first may have raced a transfer still on its way.
  const auto twice = static_cast<std::uint8_t>(differ & m_last);
  m_last = differ;
  std::uint8_t ask = 0;
  for (std::size_t bit = 0; bit < m_askedUs.size(); ++bit) {
    const auto domain = static_cast<std::uint8_t>(1u << bit);
    if ((twice & domain) == 0)
      continue;
    if (m_askedUs[bit] != 0 && nowUs - m_askedUs[bit] < m_cooldownUs)
      continue;
    m_askedUs[bit] = nowUs;
    ask |= domain;
  }
  if (ask) {
    ++m_requests;
    // Asked: what differed is on its way; count afresh from the answer.
    m_last &= static_cast<std::uint8_t>(~ask);
  }
  return ask;
}

bool NoticeGate::Allow(std::string_view key, std::uint64_t nowUs, std::uint64_t gapUs) {
  const auto it = m_lastUs.find(key);
  if (it != m_lastUs.end() && nowUs - it->second < gapUs)
    return false;
  m_lastUs.insert_or_assign(std::string(key), nowUs);
  return true;
}

bool IsOneShot(const proto::Message &message) {
  using namespace proto;
  if (const auto *journal = std::get_if<Journal>(&message))
    return !journal->snapshot; // a change notifies (toast, sound); a snapshot is state
  // A device's shot is fired once; what it aims at, and who it sees, is
  // state, said again (devices.md, "A device's fire").
  if (const auto *fire = std::get_if<DeviceFire>(&message))
    return fire->kind == DeviceFireKind::Shot;
  return std::holds_alternative<Shot>(message) || std::holds_alternative<NpcHit>(message) ||
         std::holds_alternative<NpcEffect>(message) || std::holds_alternative<PlayerHit>(message) ||
         std::holds_alternative<Noise>(message) || std::holds_alternative<NpcHack>(message) ||
         std::holds_alternative<NpcHackState>(message) ||
         std::holds_alternative<PlayerHack>(message) ||
         std::holds_alternative<VehicleHack>(message) ||
         std::holds_alternative<PhoneAction>(message) ||
         std::holds_alternative<PhoneReply>(message) || std::holds_alternative<Subtitle>(message) ||
         std::holds_alternative<ChoiceCursor>(message) ||
         std::holds_alternative<QuestReward>(message) ||
         std::holds_alternative<VehicleUnlock>(message) ||
         // Takedowns, regroup, loot, revive, hit outcomes: each counts.
         std::holds_alternative<NpcAction>(message) ||
         std::holds_alternative<NpcActionReply>(message) ||
         std::holds_alternative<NpcCue>(message) || std::holds_alternative<Regroup>(message) ||
         std::holds_alternative<RegroupAnswer>(message) ||
         std::holds_alternative<WorldItem>(message) ||
         std::holds_alternative<WorldItemTaken>(message) ||
         std::holds_alternative<SharedItem>(message) || std::holds_alternative<Revive>(message) ||
         // A shot at a V, and its hit: each one counts.
         std::holds_alternative<NpcShotAt>(message) || std::holds_alternative<ShotHit>(message) ||
         std::holds_alternative<GameOver>(message) || std::holds_alternative<NpcShot>(message) ||
         // A request to operate a device: asked once (its answer is state).
         std::holds_alternative<DeviceUse>(message) || std::holds_alternative<NpcStrike>(message) ||
         // A breach and its answer: the daemons run once (devices.md, "Breach Protocol").
         std::holds_alternative<Breach>(message) || std::holds_alternative<BreachResult>(message) ||
         // A quest interaction: asked once, answered once (story.md).
         std::holds_alternative<QuestInteract>(message) ||
         std::holds_alternative<NpcWound>(message) || std::holds_alternative<AreaHit>(message) ||
         // A hand-over's steps: each is acted on once (docs/architecture/regions.md).
         std::holds_alternative<NpcHandover>(message) ||
         std::holds_alternative<NpcHandoverState>(message) ||
         std::holds_alternative<NpcHandoverDone>(message) ||
         // ...and a shared vehicle's (traffic.md, "Who owns an area").
         std::holds_alternative<TrafficHandover>(message) ||
         std::holds_alternative<TrafficHandoverState>(message) ||
         std::holds_alternative<TrafficHandoverDone>(message) ||
         // A hit on a shared vehicle, a claim on one, a crime: each counts.
         std::holds_alternative<TrafficHit>(message) ||
         std::holds_alternative<TrafficClaim>(message) || std::holds_alternative<Crime>(message) ||
         // Presence: a cue is of the moment, a reaction is played once.
         std::holds_alternative<Cue>(message) || std::holds_alternative<NpcReaction>(message) ||
         std::holds_alternative<Throw>(message) || std::holds_alternative<ThrowEnd>(message) ||
         std::holds_alternative<Defended>(message) || std::holds_alternative<TimeSkip>(message) ||
         std::holds_alternative<TimeSkipVote>(message) ||
         std::holds_alternative<TimeSkipResult>(message) || IsTrade(message) ||
         std::holds_alternative<Mark>(message) || std::holds_alternative<Tag>(message) ||
         // Vehicle combat: a hit, an exit, a mounted shot, a seat asked
         // for, a push, a ram: each counts.
         std::holds_alternative<VehicleHit>(message) ||
         std::holds_alternative<VehicleExit>(message) ||
         std::holds_alternative<MountedFire>(message) ||
         std::holds_alternative<GunnerRequest>(message) ||
         std::holds_alternative<VehicleBump>(message) || std::holds_alternative<NpcRam>(message);
}

void FaultyLink::Set(const Faults &faults) {
  m_faults = faults;
  m_random.seed(faults.seed);
}

FaultyLink::Action FaultyLink::Decide(const proto::Message &message) {
  using namespace proto;
  if (!Active() || std::holds_alternative<Hello>(message) ||
      std::holds_alternative<Welcome>(message) || std::holds_alternative<Refused>(message) ||
      std::holds_alternative<Bye>(message))
    return Action::Send;
  std::uniform_real_distribution<double> chance(0.0, 1.0);
  if (chance(m_random) < m_faults.drop) {
    ++m_dropped;
    return Action::Drop;
  }
  if (!IsOneShot(message) && chance(m_random) < m_faults.dup) {
    ++m_duplicated;
    return Action::Twice;
  }
  return Action::Send;
}
} // namespace tandem::consistency
