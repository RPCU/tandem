// Per-player regions in the session (docs/architecture/regions.md): who owns
// each NPC, who sees it, the host's arbitration of links and hand-overs, and
// what happens to the NPCs of an owner that is gone. The rules themselves
// are in regions.hpp; this is their plumbing through the endpoint.

#include "tandem/endpoint.hpp"

#include <algorithm>
#include <utility>

namespace tandem {
namespace {
constexpr std::uint64_t kArbitrationUs = 250'000;
// A step of a hand-over that got no answer is sent again, a few times.
constexpr std::uint64_t kHandoverRetryUs = 1'000'000;
constexpr int kHandoverTries = 3;
// An NPC its owner would not give (held, seated, in a scene) is left alone
// this long.
constexpr std::uint64_t kRefusedUs = 5'000'000;
constexpr std::size_t kHandoversPerPass = 8;
// As in endpoint.cpp: a shot at a V and its cosmetic twin go out together.
constexpr std::uint64_t kShotAtPairUs = 250'000;

template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
} // namespace

proto::PlayerId Endpoint::NpcOwner(proto::NpcId npc) const {
  if (const auto it = m_npcOwner.find(npc); it != m_npcOwner.end())
    return it->second;
  return m_npcs.contains(npc) ? regions::Announcer(npc) : proto::kNoPlayer;
}

bool Endpoint::OwnsNpc(proto::NpcId npc) const {
  return m_you != proto::kNoPlayer && m_npcs.contains(npc) && NpcOwner(npc) == m_you;
}

std::map<proto::NpcId, proto::NpcSpawn> Endpoint::ProxiedNpcs() const {
  std::map<proto::NpcId, proto::NpcSpawn> out;
  for (const auto &[npc, spawn] : m_npcs) {
    const auto owner = NpcOwner(npc);
    // A guest's record holds only what the host chose to show it, and its own.
    if (owner != m_you && (!IsHost() || m_links.Linked(m_you, owner)))
      out.emplace(npc, spawn);
  }
  return out;
}

bool Endpoint::LinkedWith(proto::PlayerId player) const {
  return m_players.contains(player) && m_links.Linked(m_you, player);
}

std::vector<proto::PlayerId> Endpoint::Linked() const {
  std::vector<proto::PlayerId> out;
  for (const auto &[id, info] : m_players)
    if (id != m_you && m_links.Linked(m_you, id))
      out.push_back(id);
  return out;
}

void Endpoint::ShareNpc(const proto::Message &message, proto::PlayerId owner, net::PeerId except) {
  for (const auto &[peer, player] : m_playerOfPeer)
    if (peer != except && Sees(player, owner))
      Send(peer, message);
}

void Endpoint::SayNpc(const proto::Message &message) {
  if (IsHost())
    ShareNpc(message, m_you);
  else
    Send(m_hostPeer, message);
}

bool Endpoint::RouteToOwner(proto::NpcId npc, const proto::Message &stamped, proto::PlayerId from) {
  // An NPC the record does not hold is its announcer's to answer for.
  auto owner = NpcOwner(npc);
  if (owner == proto::kNoPlayer)
    owner = regions::Announcer(npc);
  if (owner == from)
    return false; // the sender's own
  if (owner == m_you)
    return true;
  if (const auto peer = PeerOf(owner))
    Send(*peer, stamped);
  return false;
}

bool Endpoint::RouteToPlayer(proto::PlayerId to, const proto::Message &message,
                             proto::PlayerId from) {
  if (to == from || to == proto::kNoPlayer)
    return false;
  if (to == m_you)
    return true;
  if (const auto peer = PeerOf(to))
    Send(*peer, message);
  return false;
}

proto::PlayerId Endpoint::RegionLeader() const {
  if (m_you == proto::kNoPlayer)
    return proto::kNoPlayer;
  std::set<proto::PlayerId> players;
  for (const auto &[id, info] : m_players)
    players.insert(id);
  return regions::Leader(regions::GroupOf(m_you, players, m_links), m_host, m_holders);
}

proto::PlayerId Endpoint::AreaOwner(proto::PlayerId player) const {
  if (m_you == proto::kNoPlayer)
    return m_host;
  if (const auto it = m_areaOwners.find(player); it != m_areaOwners.end())
    return it->second;
  std::set<proto::PlayerId> players;
  for (const auto &[id, info] : m_players)
    players.insert(id);
  return regions::AreaOwner(player, players, m_places, m_links, m_host, m_holders);
}

void Endpoint::UpdateAreas() {
  const auto now = net::NowUs();
  if (now < m_nextAreasUs)
    return;
  m_nextAreasUs = now + kArbitrationUs;
  std::set<proto::PlayerId> players;
  for (const auto &[id, info] : m_players)
    players.insert(id);
  std::map<proto::PlayerId, proto::PlayerId> next;
  for (const auto player : players) {
    const auto was = m_areaOwners.find(player);
    next[player] = regions::AreaOwner(player, players, m_places, m_links, m_host, m_holders,
                                      was == m_areaOwners.end() ? proto::kNoPlayer : was->second);
  }
  m_areaOwners = std::move(next);
}

void Endpoint::SetRegionHold(bool held) {
  if (held == m_localHold)
    return;
  m_localHold = held;
  SendRegionHoldNow();
}

void Endpoint::SendRegionHoldNow() {
  if (!IsJoined() || m_ended)
    return;
  // A guest that just joined has something to say only if it holds.
  if (m_localHold ? !m_holders.insert(m_you).second : m_holders.erase(m_you) == 0)
    return;
  const proto::RegionHold hold{.player = m_you, .held = m_localHold};
  if (IsHost())
    Broadcast(hold);
  else
    Send(m_hostPeer, hold);
  NoteGroup(m_pending);
}

void Endpoint::NoteGroup(std::vector<SessionEvent> &out) {
  auto linked = Linked();
  const auto leader = RegionLeader();
  if (linked == m_linkedShown && leader == m_leaderShown)
    return;
  m_linkedShown = linked;
  m_leaderShown = leader;
  out.push_back(events::RegionsChanged{.linked = std::move(linked), .leader = leader});
}

void Endpoint::PinNpc(proto::NpcId npc) {
  if (!IsJoined() || m_ended || !OwnsNpc(npc) || !m_pinned.insert(npc).second)
    return;
  if (!IsHost())
    Send(m_hostPeer, proto::NpcOwner{.npc = npc, .owner = m_you, .flags = proto::kNpcPinned});
}

void Endpoint::GiveNpc(proto::NpcHandoverState state) {
  if (!IsJoined() || m_ended || !OwnsNpc(state.npc) || state.to == m_you)
    return;
  state.from = m_you;
  if (state.effects.size() > proto::kMaxNpcEffects)
    state.effects.resize(proto::kMaxNpcEffects);
  if (IsHost()) {
    OnGiven(m_you, std::move(state), m_pending);
    return;
  }
  // Not ours to speak for from here on; kept until the host names the new
  // owner (its request may come again: the answer was lost).
  m_npcOwner[state.npc] = state.to;
  m_took.erase(state.npc);
  m_given[state.npc] = state;
  Send(m_hostPeer, state);
}

void Endpoint::RefuseNpc(proto::NpcId npc) {
  if (!IsJoined() || m_ended)
    return;
  const proto::NpcHandoverDone no{.npc = npc, .accepted = false};
  if (IsHost())
    OnHandoverDone(m_you, no, m_pending);
  else
    Send(m_hostPeer, no);
}

void Endpoint::AdoptNpc(proto::NpcId npc, bool accepted) {
  if (!IsJoined() || m_ended)
    return;
  const proto::NpcHandoverDone done{.npc = npc, .accepted = accepted};
  if (IsHost()) {
    OnHandoverDone(m_you, done, m_pending);
    return;
  }
  Send(m_hostPeer, done);
  if (!accepted)
    ForgetNpc(npc); // the host tells everyone it is gone
}

void Endpoint::ForgetNpc(proto::NpcId npc) {
  m_npcs.erase(npc);
  m_actorEncoders.erase(npc);
  m_rigStreams.erase(scene::StreamOf(npc, proto::kRigBody));
  m_rigStreams.erase(scene::StreamOf(npc, proto::kRigFace));
  m_trackStreams.erase(scene::TracksOf(npc, proto::kRigBody));
  m_trackStreams.erase(scene::TracksOf(npc, proto::kRigFace));
  m_actorDecoders.erase(npc);
  m_npcHealth.erase(npc);
  m_npcHolds.erase(npc);
  m_npcSeats.erase(npc);
  m_npcAnims.erase(npc);
  m_npcPoise.erase(npc);
  m_npcScale.erase(npc);
  m_npcScaleEarly.erase(npc);
  m_bossStates.erase(npc);
  m_npcPoiseEarly.erase(npc);
  m_npcVendors.erase(npc);
  m_npcPlaced.erase(npc);
  m_npcOwner.erase(npc);
  m_pinned.erase(npc);
  m_refusedUntilUs.erase(npc);
  m_handovers.erase(npc);
  m_askers.erase(npc);
  m_shotAt.erase(npc);
  m_given.erase(npc);
  m_took.erase(npc);
}

void Endpoint::NoteVendorGone(const proto::NpcGone &gone, std::vector<SessionEvent> *out) {
  if (!IsHost())
    return;
  if (gone.reason == proto::kGoneKnockedOut) {
    // Its records go now; it may still die.
    if (const auto it = m_npcVendors.find(gone.npc);
        it != m_npcVendors.end() && m_downVendors.size() < 256)
      m_downVendors[gone.npc] = it->second;
    return;
  }
  auto vendor = m_downVendors.extract(gone.npc);
  if (gone.reason != proto::kGoneDead)
    return;
  std::uint64_t entity = vendor ? vendor.mapped().entity : 0;
  if (const auto it = m_npcVendors.find(gone.npc); it != m_npcVendors.end())
    entity = it->second.entity;
  if (entity == 0 || m_deadVendors.size() >= 256)
    return;
  const proto::NpcVendor dead{.entity = entity, .flags = proto::kVendorDead};
  if (!m_deadVendors.emplace(entity, dead).second)
    return;
  // Every game hides its own copy, linked or not: the same dead stay dead.
  Broadcast(dead);
  if (out)
    out->push_back(events::Combat{dead});
}

void Endpoint::NoteDeath(const proto::NpcGone &gone) {
  if (gone.reason != proto::kGoneDead && gone.reason != proto::kGoneKnockedOut)
    return;
  const auto placed = m_npcPlaced.find(gone.npc);
  const auto spawn = m_npcs.find(gone.npc);
  if (placed == m_npcPlaced.end() || spawn == m_npcs.end() || m_deaths.Has(placed->second.entity))
    return;
  // Where our game last said it stood (SendNpcStates keeps the spawn there).
  const proto::NpcDeath death{
      .npc = gone.npc,
      .entity = placed->second.entity,
      .record = spawn->second.record,
      .appearance = spawn->second.appearance,
      .position = spawn->second.position,
      .yaw = spawn->second.yaw,
      .flags = static_cast<std::uint8_t>(
          gone.reason == proto::kGoneKnockedOut ? proto::kDeathKnockedOut : 0)};
  if (IsHost())
    RecordDeath(death, m_you, m_pending);
  else
    Send(m_hostPeer, death);
}

void Endpoint::RecordDeath(const proto::NpcDeath &death, proto::PlayerId by,
                           std::vector<SessionEvent> &out) {
  const auto stamped = m_deaths.Record(death, by, HostNowUs());
  if (!stamped)
    return;
  // Every game keeps its own copy out of its world, linked or not.
  Broadcast(*stamped);
  out.push_back(events::Combat{*stamped});
}

std::uint32_t Endpoint::OwnedHash(proto::PlayerId owner, std::uint16_t &count) const {
  // Which NPCs, and what they are: health and holds change too often to
  // compare across the link without racing what is in flight. Neither does
  // an NPC being handed to or from it (host: the guest learns it owns one
  // from the giver's state, which may still be on its way or lost and sent
  // again; a guest's m_handovers is empty).
  // ...and the same of the shared vehicles it runs (traffic.md).
  std::uint16_t npcs = 0, cars = 0;
  const auto hash = consistency::Hasher{}
                        .Add(std::uint64_t{consistency::NpcDigest(
                            m_npcs, {}, {}, m_npcOwner,
                            [&](proto::NpcId npc, proto::PlayerId theirs) {
                              return theirs == owner && !InHandover(npc, owner);
                            },
                            &npcs)})
                        .Add(std::uint64_t{OwnedTrafficHash(owner, cars)})
                        .Value();
  count = static_cast<std::uint16_t>(std::min<unsigned>(0xFFFFu, unsigned{npcs} + cars));
  return hash;
}

void Endpoint::ResendOwned() {
  proto::NpcSet set;
  for (const auto &[npc, spawn] : m_npcs)
    if (OwnsNpc(npc) && set.npcs.size() < proto::kMaxNpcSet)
      set.npcs.push_back(npc);
  Send(m_hostPeer, set);
  for (const auto npc : set.npcs) {
    Send(m_hostPeer, m_npcs.at(npc));
    if (const auto it = m_npcHealth.find(npc); it != m_npcHealth.end())
      Send(m_hostPeer, it->second);
    if (const auto it = m_npcHolds.find(npc); it != m_npcHolds.end())
      Send(m_hostPeer, it->second);
    if (const auto it = m_npcVendors.find(npc); it != m_npcVendors.end())
      Send(m_hostPeer, it->second);
    if (const auto it = m_npcPlaced.find(npc); it != m_npcPlaced.end())
      Send(m_hostPeer, it->second);
    if (m_pinned.contains(npc))
      Send(m_hostPeer, proto::NpcOwner{.npc = npc, .owner = m_you, .flags = proto::kNpcPinned});
  }
  ResendOwnedTraffic();
}

// ---- host ----

std::vector<proto::Message> Endpoint::NpcRecords(proto::NpcId npc) const {
  std::vector<proto::Message> out;
  const auto spawn = m_npcs.find(npc);
  if (spawn == m_npcs.end())
    return out;
  // The owner first: the spawn that follows is of an NPC it already places.
  if (const auto owner = NpcOwner(npc); owner != regions::Announcer(npc))
    out.emplace_back(proto::NpcOwner{.npc = npc, .owner = owner});
  out.emplace_back(spawn->second);
  if (const auto it = m_npcVendors.find(npc); it != m_npcVendors.end())
    out.emplace_back(it->second);
  if (const auto it = m_npcPlaced.find(npc); it != m_npcPlaced.end())
    out.emplace_back(it->second);
  if (const auto it = m_npcHealth.find(npc); it != m_npcHealth.end())
    out.emplace_back(it->second);
  if (const auto it = m_npcPoise.find(npc); it != m_npcPoise.end())
    out.emplace_back(it->second);
  if (const auto it = m_npcScale.find(npc); it != m_npcScale.end())
    out.emplace_back(it->second);
  if (const auto it = m_bossStates.find(npc); it != m_bossStates.end())
    out.emplace_back(it->second);
  if (const auto it = m_npcHolds.find(npc); it != m_npcHolds.end())
    out.emplace_back(it->second);
  if (const auto it = m_npcAnims.find(npc); it != m_npcAnims.end())
    out.emplace_back(proto::NpcAnims{.npcs = {it->second}});
  if (const auto it = m_npcSeats.find(npc); it != m_npcSeats.end())
    out.emplace_back(it->second);
  return out;
}

void Endpoint::ShowNpcTo(net::PeerId peer, proto::NpcId npc) {
  for (const auto &message : NpcRecords(npc))
    Send(peer, message);
}

void Endpoint::ShowNpcHere(proto::NpcId npc, std::vector<SessionEvent> &out) {
  for (const auto &message : NpcRecords(npc))
    std::visit(Overloaded{
                   [&](const proto::NpcSpawn &m) { out.push_back(events::HostNpcSpawn{m}); },
                   [&](const proto::NpcHealth &m) { out.push_back(events::HostNpcHealth{m}); },
                   [&](const proto::NpcPoise &m) { out.push_back(events::Combat{m}); },
                   [&](const proto::NpcScale &m) { out.push_back(events::Combat{m}); },
                   [&](const proto::BossState &m) { out.push_back(events::Combat{m}); },
                   [&](const proto::NpcVendor &m) { out.push_back(events::Combat{m}); },
                   [&](const proto::NpcPlaced &m) { out.push_back(events::Combat{m}); },
                   [&](const proto::NpcHold &m) { out.push_back(events::HostNpcHold{m}); },
                   [&](const proto::NpcAnims &m) { out.push_back(events::HostNpcAnims{m}); },
                   [&](const proto::NpcSeat &m) { out.push_back(events::HostNpcSeat{m}); },
                   [](const auto &) {},
               },
               message);
}

void Endpoint::SendRegionsTo(std::optional<net::PeerId> peer) {
  const proto::Regions regions{.revision = m_regionsRevision, .pairs = m_regionsSent};
  if (peer)
    Send(*peer, regions);
  else
    Broadcast(regions);
}

void Endpoint::SendRegionsNow() {
  std::set<proto::PlayerId> players;
  for (const auto &[id, info] : m_players)
    players.insert(id);
  if (auto pairs = m_links.Pairs(players); pairs != m_regionsSent) {
    m_regionsSent = std::move(pairs);
    ++m_regionsRevision;
    SendRegionsTo(std::nullopt);
  }
}

void Endpoint::OnLinksChanged(const regions::Links &before, std::vector<SessionEvent> &out) {
  // A guest whose links changed sees other NPCs: the set it sees now, whole
  // (it drops the rest and learns the new ones, as after a resync).
  for (const auto &[peer, player] : m_playerOfPeer) {
    const bool changed = std::ranges::any_of(m_players, [&](const auto &other) {
      return other.first != player &&
             before.Linked(player, other.first) != m_links.Linked(player, other.first);
    });
    if (changed)
      SendDomainsTo(peer, proto::kDomainNpcs, true);
  }
  // Our own game: the same, as events.
  for (const auto &[id, info] : m_players) {
    if (id == m_you || before.Linked(m_you, id) == m_links.Linked(m_you, id))
      continue;
    const bool sees = m_links.Linked(m_you, id);
    for (const auto &[npc, spawn] : m_npcs) {
      if (NpcOwner(npc) != id)
        continue;
      if (sees)
        ShowNpcHere(npc, out);
      else
        out.push_back(events::HostNpcGone{{.npc = npc, .reason = proto::kGoneAway}});
    }
    if (!sees)
      for (const auto &[npc, owner] : m_corpseOwner)
        if (owner == id)
          out.push_back(events::HostNpcGone{{.npc = npc, .reason = proto::kGoneAway}});
    // Its vehicles too (traffic.md).
    OnTrafficLinksChanged(id, sees, out);
  }
}

void Endpoint::MoveNpc(proto::NpcId npc, proto::PlayerId now, proto::NpcHandoverState state,
                       std::vector<SessionEvent> &out) {
  const auto was = NpcOwner(npc);
  m_npcOwner[npc] = now;
  if (const auto it = m_npcs.find(npc);
      it != m_npcs.end() && (state.flags & proto::kHandoverBlind) == 0) {
    it->second.position = state.position;
    it->second.yaw = state.yaw;
  }
  const proto::NpcOwner word{.npc = npc, .owner = now};
  const proto::NpcGone away{.npc = npc, .reason = proto::kGoneAway};
  const bool acts = InCast(npc);
  for (const auto &[peer, player] : m_playerOfPeer) {
    const bool saw = player == was || Sees(player, was);
    const bool sees = Sees(player, now);
    if (player == now) {
      // It gets the state; a scene's actor is its own now, no proxy to pose.
      if (acts)
        SendCastTo(peer);
      continue;
    }
    if (sees && saw)
      Send(peer, word);
    else if (sees)
      ShowNpcTo(peer, npc);
    else if (saw) {
      // The giver hears who has it even when it will not see it.
      if (player == was)
        Send(peer, word);
      Send(peer, away);
    }
    // A scene's actor it sees now, or no more (SceneFor), after its spawn.
    if (acts && (player == was || saw != sees))
      SendCastTo(peer);
  }
  if (now != m_you) {
    const bool saw = was == m_you || Sees(m_you, was);
    const bool sees = Sees(m_you, now);
    if (sees && saw)
      out.push_back(events::NpcOwnerChanged{.npc = npc, .owner = now, .was = was});
    else if (sees)
      ShowNpcHere(npc, out);
    else if (saw) {
      if (was == m_you)
        out.push_back(events::NpcOwnerChanged{.npc = npc, .owner = now, .was = was});
      else
        out.push_back(events::HostNpcGone{away});
    }
  }
  state.npc = npc;
  state.to = now;
  m_handovers[npc] = {.from = was, .to = now, .state = state, .sentUs = net::NowUs(), .tries = 0};
  if (now == m_you)
    out.push_back(events::NpcTake{std::move(state)});
  else if (const auto peer = PeerOf(now))
    Send(*peer, state);
}

void Endpoint::DropNpc(proto::NpcId npc, std::vector<SessionEvent> &out) {
  const auto owner = NpcOwner(npc);
  if (owner == proto::kNoPlayer)
    return;
  const proto::NpcGone away{.npc = npc, .reason = proto::kGoneAway};
  ShareNpc(away, owner);
  if (owner == m_you) {
    out.push_back(events::NpcOwnerChanged{.npc = npc, .owner = proto::kNoPlayer, .was = m_you});
  } else {
    if (const auto peer = PeerOf(owner))
      Send(*peer, away); // its endpoint forgets it too
    if (Sees(m_you, owner))
      out.push_back(events::HostNpcGone{away});
  }
  const bool acts = InCast(npc);
  ForgetNpc(npc);
  // A scene's actor gone: the casts name it no more (SceneFor).
  if (acts)
    for (const auto &[peer, player] : m_playerOfPeer)
      SendCastTo(peer);
}

void Endpoint::InheritNpcsOf(proto::PlayerId owner, bool gone, std::vector<SessionEvent> &out) {
  std::set<proto::PlayerId> candidates;
  for (const auto &[id, info] : m_players)
    if (id != owner && m_links.Linked(id, owner))
      candidates.insert(id);
  std::vector<proto::NpcId> theirs;
  for (const auto &[npc, spawn] : m_npcs)
    if (NpcOwner(npc) == owner)
      theirs.push_back(npc);
  for (const auto npc : theirs) {
    const auto &spawn = m_npcs.at(npc);
    // A quest NPC, or one riding in its owner's vehicle, goes with its owner.
    const bool stays = !m_pinned.contains(npc) && !m_npcSeats.contains(npc);
    const auto heir = stays ? regions::Heir(spawn.position, m_places, candidates, m_host, m_holders)
                            : proto::kNoPlayer;
    if (heir == proto::kNoPlayer) {
      // Nobody near it. Its owner is gone: so is the NPC. Its owner's world
      // was only rebuilt: its own game says what became of it (a companion
      // travels with it, the rest it reports gone).
      if (gone)
        DropNpc(npc, out);
      continue;
    }
    proto::NpcHandoverState state{.npc = npc,
                                  .from = owner,
                                  .to = heir,
                                  .position = spawn.position,
                                  .yaw = spawn.yaw,
                                  .flags = proto::kHandoverBlind | proto::kHandoverInherited};
    if (const auto it = m_npcHealth.find(npc); it != m_npcHealth.end())
      state.health = it->second.health;
    if (const auto it = m_npcPoise.find(npc); it != m_npcPoise.end())
      state.poise = it->second.poise;
    ++m_inherited;
    MoveNpc(npc, heir, std::move(state), out);
  }
  // Its corpses: nobody is left to say when they go.
  for (auto it = m_corpseOwner.begin(); gone && it != m_corpseOwner.end();) {
    if (it->second != owner) {
      ++it;
      continue;
    }
    const proto::NpcGone away{.npc = it->first, .reason = proto::kGoneAway};
    ShareNpc(away, owner);
    if (Sees(m_you, owner))
      out.push_back(events::HostNpcGone{away});
    it = m_corpseOwner.erase(it);
  }
}

void Endpoint::OnGiven(proto::PlayerId from, proto::NpcHandoverState state,
                       std::vector<SessionEvent> &out) {
  const auto npc = state.npc;
  if (!m_npcs.contains(npc) || NpcOwner(npc) != from)
    return; // moved already (a repeat), or never its own
  const auto pending = m_handovers.find(npc);
  const auto to =
      pending != m_handovers.end() && !pending->second.state ? pending->second.to : state.to;
  m_handovers.erase(npc);
  if (to == from || !m_players.contains(to)) {
    // Whoever it was for has left: the giver's body is a proxy now, so the
    // NPC goes rather than stay with nobody running it.
    DropNpc(npc, out);
    return;
  }
  state.from = from;
  if (state.effects.size() > proto::kMaxNpcEffects)
    state.effects.resize(proto::kMaxNpcEffects);
  ++m_handoversDone;
  MoveNpc(npc, to, std::move(state), out);
}

void Endpoint::OnHandoverDone(proto::PlayerId from, const proto::NpcHandoverDone &done,
                              std::vector<SessionEvent> &out) {
  const auto it = m_handovers.find(done.npc);
  if (it == m_handovers.end())
    return;
  const auto &handover = it->second;
  if (!handover.state) {
    // Asked, not given: only a refusal ends it.
    if (from == handover.from && !done.accepted) {
      m_refusedUntilUs[done.npc] = net::NowUs() + kRefusedUs;
      m_handovers.erase(it);
    }
    return;
  }
  if (from != handover.to)
    return;
  m_handovers.erase(it);
  // The new owner cannot run it: it goes, never left with nobody.
  if (!done.accepted)
    DropNpc(done.npc, out);
}

void Endpoint::Arbitrate(std::vector<SessionEvent> &out) {
  const auto now = net::NowUs();
  if (now < m_nextArbitrationUs)
    return;
  m_nextArbitrationUs = now + kArbitrationUs;

  // Everyone in the session has a place (unknown until its first state).
  std::set<proto::PlayerId> players;
  for (const auto &[id, info] : m_players) {
    players.insert(id);
    m_places.try_emplace(id);
  }
  std::erase_if(m_places, [&](const auto &place) { return !players.contains(place.first); });

  const auto before = m_links;
  // A joiner that said nothing of where it is all this while: linked as
  // any player whose place is unknown (a mock player standing nowhere).
  bool released = false;
  for (auto it = m_placeWaits.begin(); it != m_placeWaits.end();) {
    if (!m_links.Waiting(it->first) || !m_players.contains(it->first)) {
      it = m_placeWaits.erase(it);
    } else if (now >= it->second && !m_places[it->first].known) {
      m_links.Release(it->first);
      released = true;
      it = m_placeWaits.erase(it);
    } else
      ++it;
  }
  if (m_links.Update(m_places) || released)
    OnLinksChanged(before, out);
  SendRegionsNow();
  NoteGroup(out);

  // Steps that got no answer: again, a few times, then given up (the NPC is
  // still its last owner's; the digests put right what a lost message left).
  for (auto it = m_handovers.begin(); it != m_handovers.end();) {
    auto &[npc, handover] = *it;
    if (now - handover.sentUs < kHandoverRetryUs) {
      ++it;
      continue;
    }
    if (++handover.tries > kHandoverTries) {
      it = m_handovers.erase(it);
      continue;
    }
    handover.sentUs = now;
    const auto target = handover.state ? handover.to : handover.from;
    if (const auto peer = PeerOf(target)) {
      if (handover.state)
        Send(*peer, *handover.state);
      else
        Send(*peer, proto::NpcHandover{.npc = npc, .to = handover.to});
    }
    ++it;
  }

  // NPCs whose owner is leaving them with another player near.
  std::size_t asked = 0;
  for (const auto &[npc, spawn] : m_npcs) {
    if (asked >= kHandoversPerPass)
      break;
    if (m_pinned.contains(npc) || m_handovers.contains(npc) || m_npcHolds.contains(npc) ||
        m_npcSeats.contains(npc))
      continue;
    if (const auto refused = m_refusedUntilUs.find(npc); refused != m_refusedUntilUs.end()) {
      if (now < refused->second)
        continue;
      m_refusedUntilUs.erase(refused);
    }
    // (Never from an owner that holds its region: no hand-over mid-gig.)
    const auto owner = NpcOwner(npc);
    const auto taker = regions::Taker(spawn.position, owner, m_places, m_links, m_host, m_holders);
    if (taker == proto::kNoPlayer)
      continue;
    m_handovers[npc] = {.from = owner, .to = taker, .state = std::nullopt, .sentUs = now};
    ++asked;
    if (owner == m_you)
      out.push_back(events::NpcGive{.npc = npc, .to = taker});
    else if (const auto peer = PeerOf(owner))
      Send(*peer, proto::NpcHandover{.npc = npc, .to = taker});
  }

  // The shared vehicles, by the same rule (endpoint_traffic.cpp).
  ArbitrateTraffic(now, out);
}

bool Endpoint::OnOwnerMessage(net::PeerId peer, proto::PlayerId player,
                              const proto::Message &message, std::vector<SessionEvent> &out) {
  const auto owns = [&](proto::NpcId npc) {
    return m_npcs.contains(npc) && NpcOwner(npc) == player;
  };
  const auto ownsBody = [&](proto::NpcId npc) {
    const auto corpse = m_corpseOwner.find(npc);
    return owns(npc) || (corpse != m_corpseOwner.end() && corpse->second == player);
  };
  // Our own game shows this player's NPCs.
  const bool here = Sees(m_you, player);
  return std::visit(
      Overloaded{
          // ---- the owner's word on its NPCs ----
          [&](const proto::NpcSpawn &spawn) {
            // A new NPC of its own range only; one we have is not said twice.
            if (m_npcs.contains(spawn.npc) || regions::Announcer(spawn.npc) != player)
              return true;
            m_corpseOwner.erase(spawn.npc);
            m_npcs[spawn.npc] = spawn;
            ShareNpc(spawn, player, peer);
            if (here)
              out.push_back(events::HostNpcSpawn{spawn});
            return true;
          },
          [&](const proto::NpcStates &states) {
            auto live = states;
            std::erase_if(live.npcs,
                          [&](const proto::NpcEntry &entry) { return !owns(entry.npc); });
            if (live.npcs.empty())
              return true;
            for (const auto &entry : live.npcs) {
              auto &kept = m_npcs.at(entry.npc);
              kept.position = entry.position;
              kept.yaw = entry.yaw;
            }
            // One sequence for every receiver, whoever the states are from.
            live.seq = ++m_npcSeq;
            ShareNpc(live, player, peer);
            if (here)
              out.push_back(events::HostNpcStates{std::move(live)});
            return true;
          },
          [&](const proto::NpcMotion &motion) {
            // As its states, compact: relayed as they came, renumbered.
            auto live = motion;
            std::erase_if(live.npcs, [&](const proto::NpcMove &move) { return !owns(move.npc); });
            if (live.npcs.empty())
              return true;
            for (const auto &move : live.npcs) {
              const auto entry = netcode::Dequantize(live, move);
              auto &kept = m_npcs.at(move.npc);
              kept.position = entry.position;
              kept.yaw = entry.yaw;
            }
            live.seq = ++m_npcSeq;
            ShareNpc(live, player, peer);
            if (here)
              out.push_back(events::HostNpcStates{netcode::Read(live)});
            return true;
          },
          [&](const proto::NpcGone &gone) {
            if (!ownsBody(gone.npc))
              return true;
            NoteVendorGone(gone, &out);
            ForgetNpc(gone.npc);
            if (gone.reason == proto::kGoneAway)
              m_corpseOwner.erase(gone.npc);
            else
              m_corpseOwner[gone.npc] = player; // its body stays, still its owner's
            ShareNpc(gone, player, peer);
            if (here)
              out.push_back(events::HostNpcGone{gone});
            return true;
          },
          [&](const proto::NpcHealth &health) {
            if (!owns(health.npc))
              return true;
            m_npcHealth[health.npc] = health;
            ShareNpc(health, player, peer);
            if (here)
              out.push_back(events::HostNpcHealth{health});
            return true;
          },
          [&](const proto::NpcAnims &anims) {
            proto::NpcAnims known;
            for (const auto &anim : anims.npcs)
              if (owns(anim.npc) && known.npcs.size() < proto::kMaxNpcAnims) {
                m_npcAnims[anim.npc] = anim;
                known.npcs.push_back(anim);
              }
            if (known.npcs.empty())
              return true;
            ShareNpc(known, player, peer);
            if (here)
              out.push_back(events::HostNpcAnims{std::move(known)});
            return true;
          },
          [&](const proto::NpcReaction &reaction) {
            if (!owns(reaction.npc))
              return true;
            ShareNpc(reaction, player, peer);
            if (here)
              out.push_back(events::HostNpcReaction{reaction});
            return true;
          },
          [&](const proto::NpcVoice &voice) {
            if (!owns(voice.npc))
              return true;
            ShareNpc(voice, player, peer);
            if (here)
              out.push_back(events::HostNpcVoice{voice});
            return true;
          },
          [&](const proto::NpcShot &shot) {
            if (!owns(shot.npc))
              return true;
            // Whoever this NPC shoots at fires the shot itself (NpcShotAt).
            proto::PlayerId target = proto::kNoPlayer;
            if (const auto at = m_shotAt.find(shot.npc);
                at != m_shotAt.end() && net::NowUs() - at->second.second < kShotAtPairUs)
              target = at->second.first;
            const auto skip = target != proto::kNoPlayer ? PeerOf(target) : std::nullopt;
            for (const auto &[to, viewer] : m_playerOfPeer)
              if (to != peer && Sees(viewer, player) && !(skip && to == *skip))
                Send(to, shot);
            if (here && target != m_you)
              out.push_back(events::HostNpcShot{shot});
            return true;
          },
          [&](const proto::NpcHold &hold) {
            if (!owns(hold.npc))
              return true;
            if (hold.player == proto::kNoPlayer)
              m_npcHolds.erase(hold.npc);
            else
              m_npcHolds[hold.npc] = hold;
            ShareNpc(hold, player, peer);
            if (here)
              out.push_back(events::HostNpcHold{hold});
            return true;
          },
          [&](const proto::NpcSeat &seat) {
            if (!owns(seat.npc))
              return true;
            if (seat.owner == proto::kNoPlayer) {
              if (m_npcSeats.erase(seat.npc) == 0)
                return true;
            } else {
              if (const auto it = m_npcSeats.find(seat.npc);
                  it != m_npcSeats.end() && it->second == seat)
                return true;
              m_npcSeats[seat.npc] = seat;
            }
            ShareNpc(seat, player, peer);
            if (here)
              out.push_back(events::HostNpcSeat{seat});
            return true;
          },
          [&](const proto::NpcHackState &state) {
            if (!owns(state.npc))
              return true;
            ShareNpc(state, player, peer);
            if (here)
              out.push_back(events::HostNpcHackState{state});
            return true;
          },
          [&](const proto::NpcStatus &status) {
            if (!owns(status.npc))
              return true;
            ShareNpc(status, player, peer);
            if (here && status.player != m_you)
              out.push_back(events::HostNpcStatus{status});
            return true;
          },
          [&](const proto::NpcAwareness &awareness) {
            auto theirs = awareness;
            std::erase_if(theirs.entries,
                          [&](const proto::NpcAwarenessEntry &entry) { return !owns(entry.npc); });
            ShareNpc(theirs, player, peer);
            if (here)
              out.push_back(events::HostNpcAwareness{std::move(theirs)});
            return true;
          },
          // ---- the owner's word to one player: only if it shows the NPC ----
          [&](const proto::NpcCue &cue) {
            if (owns(cue.npc) && SharedWith(cue.npc, cue.player) &&
                RouteToPlayer(cue.player, cue, player))
              out.push_back(events::HostNpcCue{cue});
            return true;
          },
          [&](const proto::PlayerHit &hit) {
            if (hit.npc != 0 && owns(hit.npc) && SharedWith(hit.npc, hit.player) &&
                RouteToPlayer(hit.player, hit, player))
              out.push_back(events::HitByNpc{hit});
            return true;
          },
          [&](const proto::PlayerHack &hack) {
            if (owns(hack.npc) && SharedWith(hack.npc, hack.player) &&
                RouteToPlayer(hack.player, hack, player))
              out.push_back(events::HostPlayerHack{hack});
            return true;
          },
          [&](const proto::NpcShotAt &shot) {
            if (!owns(shot.npc) || !SharedWith(shot.npc, shot.player))
              return true;
            m_shotAt[shot.npc] = {shot.player, net::NowUs()};
            if (RouteToPlayer(shot.player, shot, player))
              out.push_back(events::ShotAtUs{shot});
            return true;
          },
          [&](const proto::NpcActionReply &reply) {
            // To whoever asked: the oldest request passed on for this NPC.
            const auto askers = m_askers.find(reply.npc);
            if (askers == m_askers.end() || askers->second.empty())
              return true;
            const auto asker = askers->second.front();
            askers->second.pop_front();
            if (askers->second.empty())
              m_askers.erase(askers);
            if (RouteToPlayer(asker, reply, player))
              out.push_back(events::NpcActionAnswered{reply});
            return true;
          },
          [&](const proto::NpcOwner &owner) {
            if (owns(owner.npc) && (owner.flags & proto::kNpcPinned))
              m_pinned.insert(owner.npc);
            return true;
          },
          [&](const proto::RegionHold &hold) {
            // A guest only holds in its own name.
            if (hold.held ? !m_holders.insert(player).second : m_holders.erase(player) == 0)
              return true;
            Broadcast(proto::RegionHold{.player = player, .held = hold.held}, peer);
            NoteGroup(out);
            return true;
          },
          [&](const proto::NpcHandoverState &state) {
            OnGiven(player, state, out);
            return true;
          },
          [&](const proto::NpcHandoverDone &done) {
            OnHandoverDone(player, done, out);
            return true;
          },
          [&](const proto::NpcSet &set) {
            // Its whole set again (our record of it differed): what it no
            // longer names is gone. Not one being handed to or from it: its
            // set may have crossed the hand-over's state on the way.
            const std::set<proto::NpcId> named(set.npcs.begin(), set.npcs.end());
            std::vector<proto::NpcId> gone;
            for (const auto &[npc, spawn] : m_npcs)
              if (NpcOwner(npc) == player && !named.contains(npc) && !InHandover(npc, player))
                gone.push_back(npc);
            // What it names and does not run by our record: it is told, or
            // it would run it for good beside its real owner (a lost
            // NpcOwner), or one we let go of (its spawn of another's id is
            // never taken). One of its own range comes with its spawn next.
            for (const auto npc : named) {
              if (InHandover(npc, player))
                continue;
              if (!m_npcs.contains(npc)) {
                if (regions::Announcer(npc) != player)
                  Send(peer, proto::NpcGone{.npc = npc, .reason = proto::kGoneAway});
              } else if (const auto owner = NpcOwner(npc); owner != player) {
                Send(peer, proto::NpcOwner{.npc = npc, .owner = owner});
              }
            }
            for (const auto npc : gone) {
              const proto::NpcGone away{.npc = npc, .reason = proto::kGoneAway};
              ShareNpc(away, player, peer);
              if (here)
                out.push_back(events::HostNpcGone{away});
              ForgetNpc(npc);
            }
            return true;
          },
          // ---- anyone's request to an NPC's owner ----
          [&](const proto::NpcHit &hit) {
            auto mine = hit;
            mine.player = player;
            if (RouteToOwner(mine.npc, mine, player))
              out.push_back(events::NpcHitBy{mine});
            return true;
          },
          [&](const proto::NpcEffect &effect) {
            auto mine = effect;
            mine.player = player;
            if (RouteToOwner(mine.npc, mine, player))
              out.push_back(events::NpcEffectBy{mine});
            return true;
          },
          [&](const proto::NpcHack &hack) {
            auto mine = hack;
            mine.player = player;
            if (RouteToOwner(mine.npc, mine, player))
              out.push_back(events::NpcHackBy{mine});
            return true;
          },
          [&](const proto::NpcAction &action) {
            auto mine = action;
            mine.player = player;
            // Passed on to a guest: its answer comes back to this player.
            if (const auto owner = NpcOwner(mine.npc);
                owner != proto::kNoPlayer && owner != m_you && owner != player)
              m_askers[mine.npc].push_back(player);
            if (RouteToOwner(mine.npc, mine, player))
              out.push_back(events::NpcActionBy{mine});
            return true;
          },
          [&](const proto::ShotHit &hit) {
            // A game only reports hits on its own V, to the shooter's owner.
            auto mine = hit;
            mine.player = player;
            if (RouteToOwner(mine.npc, mine, player))
              out.push_back(events::ShotHitBy{mine});
            return true;
          },
          [&](const proto::Noise &noise) {
            // Every game near it hears it: its NPCs do, from the puppet.
            auto mine = noise;
            mine.player = player;
            for (const auto &[to, other] : m_playerOfPeer)
              if (to != peer && m_links.Linked(other, player))
                Send(to, mine);
            if (here)
              out.push_back(events::NoiseBy{mine});
            return true;
          },
          [&](const proto::SharedItem &shared) {
            auto relayed = shared;
            relayed.player = player;
            if (relayed.kind == proto::kSharedOnCorpse && ownsBody(relayed.npc)) {
              // On the corpse of one of its NPCs: for who sees it.
              ShareNpc(relayed, player, peer);
              if (here)
                out.push_back(events::SharedItemFrom{relayed});
              return true;
            }
            // What its own V got: every other player gets it.
            relayed.kind = proto::kSharedLooted;
            relayed.npc = 0;
            Broadcast(relayed, peer);
            out.push_back(events::SharedItemFrom{relayed});
            return true;
          },
          [](const auto &) { return false; },
      },
      message);
}

// ---- guest ----

bool Endpoint::OnRegionMessage(const proto::Message &message, std::vector<SessionEvent> &out) {
  return std::visit(
      Overloaded{
          [&](const proto::Regions &regions) {
            if (m_regionsRevision == regions.revision && m_regionsSent == regions.pairs)
              return true; // a repeat
            m_regionsRevision = regions.revision;
            m_regionsSent = regions.pairs;
            m_links.Set(regions);
            NoteGroup(out);
            return true;
          },
          [&](const proto::RegionHold &hold) {
            if (hold.player == m_you)
              return true; // ours is our game's
            if (hold.held)
              m_holders.insert(hold.player);
            else
              m_holders.erase(hold.player);
            NoteGroup(out);
            return true;
          },
          [&](const proto::NpcOwner &owner) {
            // The host has placed it: what we gave need not be sent again.
            m_given.erase(owner.npc);
            const auto was = NpcOwner(owner.npc);
            if (was == owner.owner)
              return true; // as we have it
            m_npcOwner[owner.npc] = owner.owner;
            if (owner.owner != m_you) {
              m_took.erase(owner.npc);
              if (was != proto::kNoPlayer)
                out.push_back(
                    events::NpcOwnerChanged{.npc = owner.npc, .owner = owner.owner, .was = was});
            }
            return true;
          },
          [&](const proto::NpcHandover &ask) {
            // Asked again: our answer was lost.
            if (const auto given = m_given.find(ask.npc); given != m_given.end()) {
              Send(m_hostPeer, given->second);
              return true;
            }
            if (OwnsNpc(ask.npc))
              out.push_back(events::NpcGive{.npc = ask.npc, .to = ask.to});
            return true;
          },
          [&](const proto::NpcHandoverState &state) {
            if (state.to != m_you)
              return true;
            // Sent again: our answer was lost.
            if (m_took.contains(state.npc) && OwnsNpc(state.npc)) {
              Send(m_hostPeer, proto::NpcHandoverDone{.npc = state.npc, .accepted = true});
              return true;
            }
            if (!m_npcs.contains(state.npc)) {
              // Nothing of it here to wake up, and no record to spawn it from.
              Send(m_hostPeer, proto::NpcHandoverDone{.npc = state.npc, .accepted = false});
              return true;
            }
            m_npcOwner[state.npc] = m_you;
            m_given.erase(state.npc);
            m_took.insert(state.npc);
            out.push_back(events::NpcTake{state});
            return true;
          },
          [&](const proto::OwnedDigest &digest) {
            std::uint16_t count = 0;
            const auto hash = OwnedHash(m_you, count);
            const bool differs = hash != digest.npcs || count != digest.count;
            const auto now = net::NowUs();
            const auto cooldown =
                static_cast<std::uint64_t>(m_timing.resyncCooldown.count()) * 1000 / 2;
            // Twice in a row: the first may have raced what was in flight.
            if (differs && m_ownedDiffered &&
                (m_ownedResentUs == 0 || now - m_ownedResentUs >= cooldown)) {
              m_ownedResentUs = now;
              m_ownedDiffered = false;
              ResendOwned();
            } else {
              m_ownedDiffered = differs;
            }
            return true;
          },
          // ---- requests for the NPCs we run ----
          [&](const proto::NpcHit &hit) {
            if (OwnsNpc(hit.npc) && hit.player != m_you)
              out.push_back(events::NpcHitBy{hit});
            return true;
          },
          [&](const proto::NpcEffect &effect) {
            if (OwnsNpc(effect.npc) && effect.player != m_you)
              out.push_back(events::NpcEffectBy{effect});
            return true;
          },
          [&](const proto::NpcHack &hack) {
            if (OwnsNpc(hack.npc) && hack.player != m_you)
              out.push_back(events::NpcHackBy{hack});
            return true;
          },
          [&](const proto::NpcAction &action) {
            if (OwnsNpc(action.npc) && action.player != m_you)
              out.push_back(events::NpcActionBy{action});
            return true;
          },
          [&](const proto::ShotHit &hit) {
            if (OwnsNpc(hit.npc) && hit.player != m_you)
              out.push_back(events::ShotHitBy{hit});
            return true;
          },
          [&](const proto::Noise &noise) {
            if (noise.player != m_you)
              out.push_back(events::NoiseBy{noise});
            return true;
          },
          [](const auto &) { return false; },
      },
      message);
}
} // namespace tandem
