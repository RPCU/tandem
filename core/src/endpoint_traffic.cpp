// World vehicles in the session (docs/architecture/traffic.md): traffic,
// parked cars, police cars and wrecks, each run by one game, as an NPC is
// (docs/architecture/regions.md). The area owner that announced a vehicle
// runs it until the host hands it over; whoever runs it says what it is,
// where it goes and how it stands; the players linked with that owner show
// a proxy; everyone else's hits and claims go to the owner. The host relays a
// guest's word, keeps the record for late joiners and the digests, arbitrates
// hand-overs and hands an owner's vehicles on when it goes.

#include "tandem/endpoint.hpp"
#include "tandem/traffic.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace tandem {
namespace {
template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};

// As for NPCs (endpoint_regions.cpp): a step that got no answer is sent
// again a few times, a vehicle its owner would not give is left alone a
// while, and only so many are asked for per pass.
constexpr std::uint64_t kHandoverRetryUs = 1'000'000;
constexpr int kHandoverTries = 3;
constexpr std::uint64_t kRefusedUs = 5'000'000;
constexpr std::size_t kHandoversPerPass = 8;

bool Finite(const proto::Vec3 &v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool Finite(const proto::Quat &q) {
  return std::isfinite(q.i) && std::isfinite(q.j) && std::isfinite(q.k) && std::isfinite(q.r);
}

// A hand-over's state as safe to keep and act on.
proto::TrafficHandoverState Sanitized(proto::TrafficHandoverState state) {
  if (!Finite(state.velocity))
    state.velocity = {};
  if (!Finite(state.orientation))
    state.orientation = {};
  if (!std::isfinite(state.health))
    state.health = 1.f;
  state.health = std::clamp(state.health, 0.f, 1.f);
  return state;
}
} // namespace

// ---- who runs what ----

proto::PlayerId Endpoint::TrafficOwnerOf(proto::TrafficId id) const {
  if (const auto it = m_trafficOwner.find(id); it != m_trafficOwner.end())
    return it->second;
  return m_traffic.contains(id) ? proto::TrafficOwner(id) : proto::kNoPlayer;
}

bool Endpoint::OwnsTraffic(proto::TrafficId id) const {
  return m_you != proto::kNoPlayer && m_traffic.contains(id) && TrafficOwnerOf(id) == m_you;
}

std::map<proto::TrafficId, proto::TrafficSpawn> Endpoint::ProxiedTraffic() const {
  std::map<proto::TrafficId, proto::TrafficSpawn> out;
  for (const auto &[id, spawn] : m_traffic) {
    const auto owner = TrafficOwnerOf(id);
    // A guest's record holds only what the host chose to show it, and its own.
    if (owner != m_you && (!IsHost() || m_links.Linked(m_you, owner)))
      out.emplace(id, spawn);
  }
  return out;
}

proto::PlayerId Endpoint::VehicleOwner(proto::VehicleRef vehicle) const {
  if (!proto::IsStreetCar(vehicle))
    return vehicle;
  const auto id = proto::TrafficOfSeat(vehicle);
  const auto owner = TrafficOwnerOf(id);
  return owner != proto::kNoPlayer ? owner : proto::TrafficOwner(id);
}

void Endpoint::ShareTraffic(const proto::Message &message, proto::PlayerId owner,
                            net::PeerId except) {
  for (const auto &[peer, player] : m_playerOfPeer)
    if (peer != except && Sees(player, owner))
      Send(peer, message);
}

void Endpoint::SayTraffic(const proto::Message &message) {
  if (IsHost())
    ShareTraffic(message, m_you);
  else
    Send(m_hostPeer, message);
}

// ---- what our game says ----

void Endpoint::SendTrafficSpawn(const proto::TrafficSpawn &spawn) {
  Record(spawn);
  if (!IsJoined() || m_ended || proto::TrafficOwner(spawn.id) != m_you)
    return;
  // Said once: what a vehicle is does not change (its place follows the
  // states, how it stands the statuses). A new one only where our game runs
  // the area around our V: deep in a higher-ranked linked player's region,
  // its game announces the street.
  if (m_traffic.contains(spawn.id) || !OwnsArea())
    return;
  if (!Finite(spawn.position) || !Finite(spawn.orientation))
    return;
  m_traffic[spawn.id] = spawn;
  SayTraffic(spawn);
}

void Endpoint::SendTrafficStates(proto::TrafficStates states) {
  Record(states);
  if (!IsJoined() || m_ended)
    return;
  // Only the ones we run, and no more than a packet holds.
  std::erase_if(states.cars, [&](const proto::TrafficEntry &entry) {
    return !OwnsTraffic(entry.id) || !Finite(entry.position);
  });
  if (states.cars.size() > proto::kMaxTrafficPerState)
    states.cars.resize(proto::kMaxTrafficPerState);
  if (states.cars.empty())
    return;
  // The latest transform also goes to the kept spawn, for late joiners.
  for (const auto &entry : states.cars) {
    auto &spawn = m_traffic[entry.id];
    spawn.position = entry.position;
    spawn.orientation = traffic::UnpackOrientation(entry.orientation);
  }
  // Nobody sees our street: its reliable record is the host's all the same,
  // and where our vehicles are once a second (a link shows them from it).
  if (Linked().empty()) {
    const auto now = net::NowUs();
    if (now - m_apartTrafficStatesUs < regions::kApartStatesUs)
      return;
    m_apartTrafficStatesUs = now;
  }
  // One numbering with the NPC states: an epoch's firstNpcSeq fences both
  // (the host numbers a guest's again).
  states.seq = ++m_npcSeq;
  if (states.timeUs == 0) // when our game read them (Runtime), else now
    states.timeUs = HostNowUs();
  SayTraffic(states);
}

void Endpoint::SendTrafficGone(const proto::TrafficGone &gone) {
  Record(gone);
  if (!IsJoined() || m_ended || !OwnsTraffic(gone.id))
    return;
  // Taken is the owner's answer to a claim (TakeTraffic), never said here.
  if (gone.reason == proto::kTrafficTaken)
    return;
  ForgetTraffic(gone.id);
  SayTraffic(gone);
}

void Endpoint::SendTrafficStatus(const proto::TrafficStatus &status) {
  Record(status);
  if (!IsJoined() || m_ended || !OwnsTraffic(status.id) || !std::isfinite(status.health))
    return;
  auto kept = status;
  kept.flags &= proto::kTrafficStatusMask;
  kept.health = std::clamp(kept.health, 0.f, 1.f);
  if (const auto it = m_trafficStatus.find(kept.id);
      it != m_trafficStatus.end() && it->second == kept)
    return;
  m_trafficStatus[kept.id] = kept;
  SayTraffic(kept);
}

void Endpoint::SendTrafficHit(const proto::TrafficHit &hit) {
  Record(hit);
  if (!IsJoined() || m_ended || !m_traffic.contains(hit.id) || OwnsTraffic(hit.id))
    return; // our own vehicles' hits are our game's
  auto mine = hit;
  mine.player = m_you;
  if (!IsHost()) {
    Send(m_hostPeer, mine);
  } else if (const auto peer = PeerOf(TrafficOwnerOf(hit.id))) {
    Send(*peer, mine);
  }
}

void Endpoint::SendTrafficHack(proto::TrafficHack hack) {
  Record(hack);
  if (!IsJoined() || m_ended || !m_traffic.contains(hack.id) || !std::isfinite(hack.seconds))
    return;
  hack.player = m_you;
  if (!IsHost())
    Send(m_hostPeer, hack);
  else
    Broadcast(hack);
}

void Endpoint::ClaimTraffic(proto::TrafficId id, std::uint64_t seat) {
  const proto::TrafficClaim claim{.player = m_you, .id = id, .seat = seat};
  Record(claim);
  if (!IsJoined() || m_ended || !m_traffic.contains(id))
    return;
  if (OwnsTraffic(id))
    TakeTraffic(m_you, id, m_pending); // ours: our V is first
  else if (!IsHost())
    Send(m_hostPeer, claim);
  else if (const auto peer = PeerOf(TrafficOwnerOf(id)))
    Send(*peer, claim);
}

void Endpoint::ForgetTraffic(proto::TrafficId id) {
  m_traffic.erase(id);
  m_trafficStatus.erase(id);
  m_trafficOwner.erase(id);
  m_trafficHandoffs.erase(id);
  m_trafficRefusedUntilUs.erase(id);
  m_trafficGiven.erase(id);
  m_trafficTook.erase(id);
  ForgetStreetVehicle(id);
  // Whoever sat in it is out. The host says so for everyone but the NPC's
  // owner (whose game takes its real NPCs out itself).
  const auto seat = proto::TrafficSeatOwner(id);
  for (auto it = m_npcSeats.begin(); it != m_npcSeats.end();) {
    if (it->second.owner != seat) {
      ++it;
      continue;
    }
    if (IsHost()) {
      const auto owner = NpcOwnerOrAnnouncer(it->first);
      const proto::NpcSeat out{.npc = it->first};
      const auto ownerPeer = PeerOf(owner);
      ShareNpc(out, owner, ownerPeer ? *ownerPeer : 0);
      if (owner != m_you && Sees(m_you, owner))
        m_pending.push_back(events::HostNpcSeat{out});
    }
    it = m_npcSeats.erase(it);
  }
}

void Endpoint::TakeTraffic(proto::PlayerId player, proto::TrafficId id,
                           std::vector<SessionEvent> &out) {
  // Gone or taken already: the claimer has heard, or is about to (the
  // reliable stream said so before this claim could be answered).
  if (!OwnsTraffic(id))
    return;
  ForgetTraffic(id);
  const proto::TrafficGone taken{.id = id, .reason = proto::kTrafficTaken, .player = player};
  // Every game hands the body it has to that player: everyone hears it.
  if (IsHost())
    Broadcast(taken);
  else
    Send(m_hostPeer, taken);
  out.push_back(events::TrafficLeft{taken});
}

void Endpoint::GiveTraffic(proto::TrafficHandoverState state) {
  if (!IsJoined() || m_ended || !OwnsTraffic(state.id) || state.to == m_you)
    return;
  state.from = m_you;
  state.handover = 0;
  state = Sanitized(std::move(state));
  if (IsHost()) {
    OnTrafficGiven(m_you, std::move(state), m_pending);
    return;
  }
  // Not ours to speak for from here on; kept until the host names the new
  // owner (its request may come again: the answer was lost).
  m_trafficOwner[state.id] = state.to;
  m_trafficTook.erase(state.id);
  m_trafficGiven[state.id] = state;
  Send(m_hostPeer, state);
}

void Endpoint::AnswerTraffic(proto::TrafficId id, bool accepted) {
  if (!IsJoined() || m_ended)
    return;
  const proto::TrafficHandoverDone done{.id = id, .accepted = accepted};
  if (IsHost()) {
    OnTrafficDone(m_you, done, m_pending);
    return;
  }
  const bool took = m_trafficTook.contains(id);
  Send(m_hostPeer, done);
  // Given to us and we cannot run it: the host tells everyone it is gone.
  if (took && !accepted)
    ForgetTraffic(id);
}

// ---- host: the record, the arbitration ----

std::vector<proto::Message> Endpoint::TrafficRecords(proto::TrafficId id) const {
  std::vector<proto::Message> out;
  const auto spawn = m_traffic.find(id);
  if (spawn == m_traffic.end())
    return out;
  // The owner first: the spawn that follows is of a vehicle it already runs.
  if (const auto owner = TrafficOwnerOf(id); owner != proto::TrafficOwner(id))
    out.emplace_back(proto::TrafficOwnership{.id = id, .owner = owner});
  out.emplace_back(spawn->second);
  if (const auto it = m_trafficStatus.find(id); it != m_trafficStatus.end())
    out.emplace_back(it->second);
  if (const auto it = m_vehicleDamage.find(proto::StreetCar(id)); it != m_vehicleDamage.end())
    out.emplace_back(it->second);
  if (const auto it = m_chases.find(id); it != m_chases.end())
    out.emplace_back(it->second);
  return out;
}

void Endpoint::ShowTrafficTo(net::PeerId peer, proto::TrafficId id) {
  for (const auto &message : TrafficRecords(id))
    Send(peer, message);
}

void Endpoint::ShowTrafficHere(proto::TrafficId id, std::vector<SessionEvent> &out) {
  for (const auto &message : TrafficRecords(id))
    std::visit(
        Overloaded{
            [&](const proto::TrafficSpawn &m) { out.push_back(events::TrafficSpawned{m}); },
            [&](const proto::TrafficStatus &m) { out.push_back(events::TrafficChanged{m}); },
            [&](const proto::VehicleDamage &m) { out.push_back(events::RemoteVehicleDamage{m}); },
            [&](const proto::Chase &m) { out.push_back(events::ChaseChanged{m}); },
            [](const auto &) {},
        },
        message);
}

void Endpoint::SendTrafficTo(net::PeerId peer, proto::PlayerId them, bool resync) {
  std::vector<proto::TrafficId> shown;
  for (const auto &[id, spawn] : m_traffic)
    if (Sees(them, TrafficOwnerOf(id)))
      shown.push_back(id);
  if (resync) {
    proto::TrafficSet set;
    for (const auto id : shown)
      if (set.ids.size() < proto::kMaxTrafficSet)
        set.ids.push_back(id);
    Send(peer, set);
  }
  // Spawns and statuses here; their damage and chases come with the vehicle
  // combat state (SendVehiclesTo), after the seats.
  for (const auto id : shown) {
    if (const auto owner = TrafficOwnerOf(id); owner != proto::TrafficOwner(id))
      Send(peer, proto::TrafficOwnership{.id = id, .owner = owner});
    Send(peer, m_traffic.at(id));
    if (const auto it = m_trafficStatus.find(id); it != m_trafficStatus.end())
      Send(peer, it->second);
  }
}

void Endpoint::OnTrafficLinksChanged(proto::PlayerId other, bool sees,
                                     std::vector<SessionEvent> &out) {
  std::vector<proto::TrafficId> theirs;
  for (const auto &[id, spawn] : m_traffic)
    if (TrafficOwnerOf(id) == other)
      theirs.push_back(id);
  for (const auto id : theirs) {
    if (sees)
      ShowTrafficHere(id, out);
    else
      out.push_back(events::TrafficLeft{{.id = id, .reason = proto::kTrafficAway}});
  }
}

void Endpoint::ArbitrateTraffic(std::uint64_t now, std::vector<SessionEvent> &out) {
  // Steps that got no answer: again, a few times, then given up (the
  // vehicle is still its last owner's; the digests put right what a lost
  // message left).
  for (auto it = m_trafficHandoffs.begin(); it != m_trafficHandoffs.end();) {
    auto &[id, handoff] = *it;
    if (now - handoff.sentUs < kHandoverRetryUs) {
      ++it;
      continue;
    }
    if (++handoff.tries > kHandoverTries) {
      it = m_trafficHandoffs.erase(it);
      continue;
    }
    handoff.sentUs = now;
    const auto target = handoff.state ? handoff.to : handoff.from;
    if (const auto peer = PeerOf(target)) {
      if (handoff.state)
        Send(*peer, *handoff.state);
      else
        Send(*peer, proto::TrafficHandover{.id = id, .to = handoff.to});
    }
    ++it;
  }

  // Shared vehicles driven remotely (the take control quickhack: a game
  // drives one from where its V stands, regions.md "Remote views"): run by
  // the driver's game, and handed to nobody else while it drives. A driver
  // that does not run it yet is given it at once, whatever the distances;
  // one its owner drives itself stays its owner's (two drivers: the owner,
  // else the lowest id; the second's game refuses its own takeover).
  std::map<proto::TrafficId, std::set<proto::PlayerId>> remote;
  for (const auto &[player, busy] : m_activities)
    if (const auto id = activity::RemoteCar(busy); id != 0 && m_traffic.contains(id))
      remote[id].insert(player);
  for (const auto &[id, drivers] : remote) {
    const auto owner = TrafficOwnerOf(id);
    if (drivers.contains(owner) || m_trafficHandoffs.contains(id))
      continue;
    if (const auto refused = m_trafficRefusedUntilUs.find(id);
        refused != m_trafficRefusedUntilUs.end() && now < refused->second)
      continue;
    const auto driver = *drivers.begin();
    if (!m_players.contains(driver))
      continue;
    m_trafficHandoffs[id] = {.from = owner, .to = driver, .state = std::nullopt, .sentUs = now};
    if (owner == m_you)
      out.push_back(events::TrafficGive{.id = id, .to = driver});
    else if (const auto peer = PeerOf(owner))
      Send(*peer, proto::TrafficHandover{.id = id, .to = driver});
  }

  // Vehicles whose owner's V is leaving them with another player near: the
  // NPC rule (regions::Taker), on the vehicle's place.
  std::size_t asked = 0;
  for (const auto &[id, spawn] : m_traffic) {
    if (asked >= kHandoversPerPass)
      break;
    // Driven remotely: its driver's (above).
    if (remote.contains(id))
      continue;
    // A gunner on its gun keeps its seat with the game that gave it.
    if (m_trafficHandoffs.contains(id) || m_gunners.contains(proto::StreetCar(id)))
      continue;
    if (const auto refused = m_trafficRefusedUntilUs.find(id);
        refused != m_trafficRefusedUntilUs.end()) {
      if (now < refused->second)
        continue;
      m_trafficRefusedUntilUs.erase(refused);
    }
    const auto owner = TrafficOwnerOf(id);
    const auto taker = regions::Taker(spawn.position, owner, m_places, m_links, m_host, m_holders);
    // Not while it is on top of the taker's V (regions::ClearOf): asked again
    // on a later pass, once it has driven clear.
    if (taker == proto::kNoPlayer || !regions::ClearOf(spawn.position, taker, m_places))
      continue;
    m_trafficHandoffs[id] = {.from = owner, .to = taker, .state = std::nullopt, .sentUs = now};
    ++asked;
    if (owner == m_you)
      out.push_back(events::TrafficGive{.id = id, .to = taker});
    else if (const auto peer = PeerOf(owner))
      Send(*peer, proto::TrafficHandover{.id = id, .to = taker});
  }
}

void Endpoint::OnTrafficGiven(proto::PlayerId from, proto::TrafficHandoverState state,
                              std::vector<SessionEvent> &out) {
  const auto id = state.id;
  if (!m_traffic.contains(id) || TrafficOwnerOf(id) != from)
    return; // moved already (a repeat), or never its own
  const auto pending = m_trafficHandoffs.find(id);
  const auto to =
      pending != m_trafficHandoffs.end() && !pending->second.state ? pending->second.to : state.to;
  m_trafficHandoffs.erase(id);
  if (to == from || !m_players.contains(to)) {
    // Whoever it was for has left: the giver's body is a proxy now, so the
    // vehicle goes rather than stay with nobody running it.
    DropTraffic(id, out);
    return;
  }
  state.from = from;
  state.handover = 0;
  ++m_trafficHandoversDone;
  MoveTraffic(id, to, Sanitized(std::move(state)), out);
}

void Endpoint::OnTrafficDone(proto::PlayerId from, const proto::TrafficHandoverDone &done,
                             std::vector<SessionEvent> &out) {
  const auto it = m_trafficHandoffs.find(done.id);
  if (it == m_trafficHandoffs.end())
    return;
  const auto &handoff = it->second;
  if (!handoff.state) {
    // Asked, not given: only a refusal ends it.
    if (from == handoff.from && !done.accepted) {
      m_trafficRefusedUntilUs[done.id] = net::NowUs() + kRefusedUs;
      m_trafficHandoffs.erase(it);
    }
    return;
  }
  if (from != handoff.to)
    return;
  m_trafficHandoffs.erase(it);
  // The new owner cannot run it: it goes, never left with nobody.
  if (!done.accepted)
    DropTraffic(done.id, out);
}

void Endpoint::MoveTraffic(proto::TrafficId id, proto::PlayerId now,
                           proto::TrafficHandoverState state, std::vector<SessionEvent> &out) {
  const auto was = TrafficOwnerOf(id);
  m_trafficOwner[id] = now;
  // The record follows the giver's word: where it is, how it stands.
  auto &spawn = m_traffic.at(id);
  if ((state.handover & proto::kHandoverInherited) == 0 && Finite(state.position)) {
    spawn.position = state.position;
    spawn.orientation = state.orientation;
  }
  const proto::TrafficStatus status{
      .id = id,
      .health = state.health,
      .flags = static_cast<std::uint16_t>(state.flags & proto::kTrafficStatusMask)};
  const auto kept = m_trafficStatus.find(id);
  const bool restated =
      (state.handover & proto::kHandoverInherited) == 0 &&
      (kept == m_trafficStatus.end()
           ? status.flags != (spawn.flags & proto::kTrafficStatusMask) || status.health != 1.f
           : !(kept->second == status));
  if (restated)
    m_trafficStatus[id] = status;
  const proto::TrafficOwnership word{.id = id, .owner = now};
  const proto::TrafficGone away{.id = id, .reason = proto::kTrafficAway};
  for (const auto &[peer, player] : m_playerOfPeer) {
    if (player == now)
      continue; // it gets the state
    const bool saw = player == was || Sees(player, was);
    const bool sees = Sees(player, now);
    if (sees && saw) {
      Send(peer, word);
      if (restated && player != was)
        Send(peer, status);
    } else if (sees) {
      ShowTrafficTo(peer, id);
    } else if (saw) {
      // The giver hears who has it even when it will not see it.
      if (player == was)
        Send(peer, word);
      Send(peer, away);
    }
  }
  if (now != m_you) {
    const bool saw = was == m_you || Sees(m_you, was);
    const bool sees = Sees(m_you, now);
    if (sees && saw) {
      out.push_back(events::TrafficOwnerChanged{.id = id, .owner = now, .was = was});
      if (restated && was != m_you)
        out.push_back(events::TrafficChanged{status});
    } else if (sees) {
      ShowTrafficHere(id, out);
    } else if (saw) {
      if (was == m_you)
        out.push_back(events::TrafficOwnerChanged{.id = id, .owner = now, .was = was});
      else
        out.push_back(events::TrafficLeft{away});
    }
  }
  state.id = id;
  state.to = now;
  m_trafficHandoffs[id] = {
      .from = was, .to = now, .state = state, .sentUs = net::NowUs(), .tries = 0};
  if (now == m_you)
    out.push_back(events::TrafficTake{state});
  else if (const auto peer = PeerOf(now))
    Send(*peer, state);
  // Whoever rides in it goes with it: the same owner for a car and its
  // driver (their seat is kept: no hand-over asks for a seated NPC).
  std::vector<proto::NpcId> riders;
  for (const auto &[npc, seat] : m_npcSeats)
    if (seat.owner == proto::TrafficSeatOwner(id) && m_npcs.contains(npc) && NpcOwner(npc) == was &&
        !m_pinned.contains(npc) && !m_handovers.contains(npc))
      riders.push_back(npc);
  for (const auto npc : riders) {
    const auto &record = m_npcs.at(npc);
    proto::NpcHandoverState rider{.npc = npc,
                                  .from = was,
                                  .to = now,
                                  .position = record.position,
                                  .yaw = record.yaw,
                                  .flags = proto::kHandoverInherited};
    if (const auto it = m_npcHealth.find(npc); it != m_npcHealth.end())
      rider.health = it->second.health;
    if (const auto it = m_npcPoise.find(npc); it != m_npcPoise.end())
      rider.poise = it->second.poise;
    // Its chase goes on: the V it chases is its target.
    if (const auto chase = m_chases.find(id); chase != m_chases.end()) {
      rider.target = chase->second.target;
      rider.flags |= proto::kHandoverCombat;
    }
    MoveNpc(npc, now, std::move(rider), out);
  }
}

void Endpoint::DropTraffic(proto::TrafficId id, std::vector<SessionEvent> &out) {
  const auto owner = TrafficOwnerOf(id);
  if (owner == proto::kNoPlayer)
    return;
  const proto::TrafficGone away{.id = id, .reason = proto::kTrafficAway};
  ShareTraffic(away, owner);
  if (owner == m_you || Sees(m_you, owner))
    out.push_back(events::TrafficLeft{away});
  if (owner != m_you)
    if (const auto peer = PeerOf(owner))
      Send(*peer, away); // its endpoint forgets it too
  ForgetTraffic(id);
}

void Endpoint::InheritTrafficOf(proto::PlayerId owner, bool gone, std::vector<SessionEvent> &out) {
  std::set<proto::PlayerId> candidates;
  for (const auto &[id, info] : m_players)
    if (id != owner && m_links.Linked(id, owner))
      candidates.insert(id);
  std::vector<proto::TrafficId> theirs;
  for (const auto &[id, spawn] : m_traffic)
    if (TrafficOwnerOf(id) == owner)
      theirs.push_back(id);
  for (const auto id : theirs) {
    const auto &spawn = m_traffic.at(id);
    const auto heir = regions::Heir(spawn.position, m_places, candidates, m_host, m_holders);
    if (heir == proto::kNoPlayer) {
      // Nobody near it. Its owner is gone: so is the vehicle. Its owner's
      // world was only rebuilt: its own game says what became of it.
      if (gone)
        DropTraffic(id, out);
      continue;
    }
    proto::TrafficHandoverState state{.id = id,
                                      .from = owner,
                                      .to = heir,
                                      .position = spawn.position,
                                      .orientation = spawn.orientation,
                                      .flags = spawn.flags,
                                      .handover = proto::kHandoverInherited};
    if (const auto it = m_trafficStatus.find(id); it != m_trafficStatus.end()) {
      state.health = it->second.health;
      state.flags =
          static_cast<std::uint16_t>((spawn.flags & ~proto::kTrafficStatusMask) | it->second.flags);
    }
    m_trafficHandoffs.erase(id);
    ++m_trafficInherited;
    MoveTraffic(id, heir, std::move(state), out);
  }
}

// ---- host: what a guest says ----

bool Endpoint::OnGuestTraffic(net::PeerId peer, proto::PlayerId player,
                              const proto::Message &message, std::vector<SessionEvent> &out) {
  const auto owns = [&](proto::TrafficId id) {
    return m_traffic.contains(id) && TrafficOwnerOf(id) == player;
  };
  // Our own game shows this player's vehicles.
  const bool here = Sees(m_you, player);
  return std::visit(
      Overloaded{
          // ---- the owner's word on its vehicles ----
          [&](const proto::TrafficSpawn &spawn) {
            // A new one of its own range only; one we have is not said twice.
            if (m_traffic.contains(spawn.id) || proto::TrafficOwner(spawn.id) != player ||
                !Finite(spawn.position) || !Finite(spawn.orientation))
              return true;
            m_traffic[spawn.id] = spawn;
            ShareTraffic(spawn, player, peer);
            if (here)
              out.push_back(events::TrafficSpawned{spawn});
            return true;
          },
          [&](const proto::TrafficStates &states) {
            auto live = states;
            std::erase_if(live.cars, [&](const proto::TrafficEntry &entry) {
              return !owns(entry.id) || !Finite(entry.position);
            });
            if (live.cars.size() > proto::kMaxTrafficPerState)
              live.cars.resize(proto::kMaxTrafficPerState);
            if (live.cars.empty())
              return true;
            for (const auto &entry : live.cars) {
              auto &kept = m_traffic.at(entry.id);
              kept.position = entry.position;
              kept.orientation = traffic::UnpackOrientation(entry.orientation);
            }
            // One sequence for every receiver, whoever the states are from
            // (the epoch fence and the tracks count on it).
            live.seq = ++m_npcSeq;
            ShareTraffic(live, player, peer);
            if (here)
              out.push_back(events::TrafficMoved{std::move(live)});
            return true;
          },
          [&](const proto::TrafficGone &gone) {
            if (!owns(gone.id))
              return true;
            ForgetTraffic(gone.id);
            if (gone.reason == proto::kTrafficTaken) {
              // A claim it granted: every game hands its body to the taker.
              if (!m_players.contains(gone.player))
                return true;
              Broadcast(gone, peer);
              out.push_back(events::TrafficLeft{gone});
              return true;
            }
            ShareTraffic(gone, player, peer);
            if (here)
              out.push_back(events::TrafficLeft{gone});
            return true;
          },
          [&](const proto::TrafficStatus &status) {
            if (!owns(status.id) || !std::isfinite(status.health))
              return true;
            auto kept = status;
            kept.flags &= proto::kTrafficStatusMask;
            kept.health = std::clamp(kept.health, 0.f, 1.f);
            if (const auto it = m_trafficStatus.find(kept.id);
                it != m_trafficStatus.end() && it->second == kept)
              return true;
            m_trafficStatus[kept.id] = kept;
            ShareTraffic(kept, player, peer);
            if (here)
              out.push_back(events::TrafficChanged{kept});
            return true;
          },
          [&](const proto::TrafficSet &set) {
            // Its whole set again (our record of it differed): what it no
            // longer names is gone. Not one being handed to or from it: its
            // set may have crossed the hand-over's state on the way.
            const std::set<proto::TrafficId> named(set.ids.begin(), set.ids.end());
            std::vector<proto::TrafficId> gone;
            for (const auto &[id, spawn] : m_traffic)
              if (TrafficOwnerOf(id) == player && !named.contains(id) &&
                  !TrafficInHandover(id, player))
                gone.push_back(id);
            for (const auto id : gone) {
              const proto::TrafficGone away{.id = id, .reason = proto::kTrafficAway};
              ShareTraffic(away, player, peer);
              if (here)
                out.push_back(events::TrafficLeft{away});
              ForgetTraffic(id);
            }
            return true;
          },
          [&](const proto::TrafficHandoverState &state) {
            OnTrafficGiven(player, state, out);
            return true;
          },
          [&](const proto::TrafficHandoverDone &done) {
            OnTrafficDone(player, done, out);
            return true;
          },
          // The host's alone to say.
          [](const proto::TrafficOwnership &) { return true; },
          [](const proto::TrafficHandover &) { return true; },
          // ---- anyone's request to a vehicle's owner ----
          [&](const proto::TrafficHit &hit) {
            auto mine = hit;
            mine.player = player;
            const auto owner = TrafficOwnerOf(mine.id);
            if (owner == proto::kNoPlayer || owner == player || !std::isfinite(mine.damage))
              return true;
            if (owner == m_you)
              out.push_back(events::TrafficHitBy{mine});
            else if (const auto to = PeerOf(owner))
              Send(*to, mine);
            return true;
          },
          // A guest's quickhack on a shared vehicle: to every other player
          // (its owner applies it; everyone marks it), and to our game.
          [&](const proto::TrafficHack &hack) {
            auto mine = hack;
            mine.player = player;
            if (!m_traffic.contains(mine.id) || !std::isfinite(mine.seconds) || mine.seconds < 0.f)
              return true;
            Broadcast(mine, peer);
            out.push_back(events::TrafficHacked{mine, OwnsTraffic(mine.id)});
            return true;
          },
          [&](const proto::TrafficClaim &claim) {
            auto mine = claim;
            mine.player = player;
            const auto owner = TrafficOwnerOf(mine.id);
            if (owner == proto::kNoPlayer || owner == player)
              return true;
            if (owner == m_you)
              TakeTraffic(player, mine.id, out);
            else if (const auto to = PeerOf(owner))
              Send(*to, mine);
            return true;
          },
          [](const auto &) { return false; },
      },
      message);
}

// ---- guest: what the host says ----

bool Endpoint::OnHostTraffic(const proto::Message &message, std::vector<SessionEvent> &out) {
  return std::visit(
      Overloaded{
          [&](const proto::TrafficSpawn &spawn) {
            // Our copy of what is shared: one we have already (a resync)
            // changes nothing in the game; ours is our game's.
            const bool known = m_traffic.contains(spawn.id);
            if (known && OwnsTraffic(spawn.id))
              return true;
            m_traffic[spawn.id] = spawn;
            if (!known)
              out.push_back(events::TrafficSpawned{spawn});
            return true;
          },
          [&](const proto::TrafficStates &states) {
            // The old world's states are void (they share the NPC states'
            // numbering: Epoch::firstNpcSeq fences both).
            if (m_firstNpcSeq != 0 && static_cast<std::int32_t>(states.seq - m_firstNpcSeq) < 0)
              return true;
            // Only vehicles we know and do not run: a state that overtook
            // its TrafficGone (or a hand-over) must not bring a track back.
            auto live = states;
            std::erase_if(live.cars, [&](const proto::TrafficEntry &entry) {
              return !m_traffic.contains(entry.id) || OwnsTraffic(entry.id);
            });
            for (const auto &entry : live.cars)
              m_traffic[entry.id].position = entry.position;
            if (!live.cars.empty())
              out.push_back(events::TrafficMoved{std::move(live)});
            return true;
          },
          [&](const proto::TrafficGone &gone) {
            if (!m_traffic.contains(gone.id))
              return true; // never shared with us, or said already
            // Ours too: the host let go of it (whoever it was given to left).
            ForgetTraffic(gone.id);
            out.push_back(events::TrafficLeft{gone});
            return true;
          },
          [&](const proto::TrafficStatus &status) {
            if (!m_traffic.contains(status.id) || OwnsTraffic(status.id))
              return true;
            if (const auto it = m_trafficStatus.find(status.id);
                it != m_trafficStatus.end() && it->second == status)
              return true;
            m_trafficStatus[status.id] = status;
            out.push_back(events::TrafficChanged{status});
            return true;
          },
          [&](const proto::TrafficSet &set) {
            // A resync: every vehicle shown to us now. Others beyond it are
            // gone (never ours: the host does not list what we run).
            const std::set<proto::TrafficId> shared(set.ids.begin(), set.ids.end());
            std::vector<proto::TrafficId> stale;
            for (const auto &[id, spawn] : m_traffic)
              if (!shared.contains(id) && !OwnsTraffic(id))
                stale.push_back(id);
            for (const auto id : stale) {
              ForgetTraffic(id);
              out.push_back(events::TrafficLeft{{.id = id, .reason = proto::kTrafficAway}});
            }
            return true;
          },
          [&](const proto::TrafficOwnership &owner) {
            // The host has placed it: what we gave need not be sent again.
            m_trafficGiven.erase(owner.id);
            if (!m_traffic.contains(owner.id)) {
              // Its spawn comes next (a vehicle we are shown for the first time).
              m_trafficOwner[owner.id] = owner.owner;
              return true;
            }
            const auto was = TrafficOwnerOf(owner.id);
            if (was == owner.owner)
              return true; // as we have it
            m_trafficOwner[owner.id] = owner.owner;
            if (owner.owner != m_you) {
              m_trafficTook.erase(owner.id);
              out.push_back(
                  events::TrafficOwnerChanged{.id = owner.id, .owner = owner.owner, .was = was});
            }
            return true;
          },
          [&](const proto::TrafficHandover &ask) {
            // Asked again: our answer was lost.
            if (const auto given = m_trafficGiven.find(ask.id); given != m_trafficGiven.end()) {
              Send(m_hostPeer, given->second);
              return true;
            }
            if (OwnsTraffic(ask.id))
              out.push_back(events::TrafficGive{.id = ask.id, .to = ask.to});
            else if (m_traffic.contains(ask.id))
              Send(m_hostPeer, proto::TrafficHandoverDone{.id = ask.id, .accepted = false});
            return true;
          },
          [&](const proto::TrafficHandoverState &state) {
            if (state.to != m_you)
              return true;
            // Sent again: our answer was lost.
            if (m_trafficTook.contains(state.id) && OwnsTraffic(state.id)) {
              Send(m_hostPeer, proto::TrafficHandoverDone{.id = state.id, .accepted = true});
              return true;
            }
            if (!m_traffic.contains(state.id)) {
              // Nothing of it here to wake up, and no record to spawn it from.
              Send(m_hostPeer, proto::TrafficHandoverDone{.id = state.id, .accepted = false});
              return true;
            }
            m_trafficOwner[state.id] = m_you;
            m_trafficGiven.erase(state.id);
            m_trafficTook.insert(state.id);
            // The record follows the state: ours to say from now on.
            auto &spawn = m_traffic[state.id];
            if ((state.handover & proto::kHandoverInherited) == 0 && Finite(state.position)) {
              spawn.position = state.position;
              spawn.orientation = state.orientation;
            }
            m_trafficStatus[state.id] = {
                .id = state.id,
                .health = std::isfinite(state.health) ? std::clamp(state.health, 0.f, 1.f) : 1.f,
                .flags = static_cast<std::uint16_t>(state.flags & proto::kTrafficStatusMask)};
            out.push_back(events::TrafficTake{Sanitized(state)});
            return true;
          },
          // ---- requests for the vehicles we run ----
          [&](const proto::TrafficHit &hit) {
            if (OwnsTraffic(hit.id) && hit.player != m_you)
              out.push_back(events::TrafficHitBy{hit});
            return true;
          },
          // Another player's quickhack on a vehicle we know of.
          [&](const proto::TrafficHack &hack) {
            if (hack.player != m_you && hack.player != proto::kNoPlayer &&
                m_traffic.contains(hack.id))
              out.push_back(events::TrafficHacked{hack, OwnsTraffic(hack.id)});
            return true;
          },
          [&](const proto::TrafficClaim &claim) {
            if (claim.player != m_you && claim.player != proto::kNoPlayer)
              TakeTraffic(claim.player, claim.id, out);
            return true;
          },
          [](const auto &) { return false; },
      },
      message);
}

// ---- consistency ----

std::uint32_t Endpoint::TrafficDigestFor(proto::PlayerId self) const {
  // The vehicles `self` shows as proxies: those of the owners it is linked
  // with (a guest's record holds only those, and its own).
  const bool host = IsHost();
  return consistency::TrafficDigest(
      m_traffic, m_trafficStatus, m_trafficOwner, [&](proto::TrafficId, proto::PlayerId owner) {
        return owner != self && (!host || m_links.Linked(owner, self));
      });
}

std::uint32_t Endpoint::OwnedTrafficHash(proto::PlayerId owner, std::uint16_t &count) const {
  // Which vehicles, and what they are: statuses change too often to
  // compare across the link without racing what is in flight, nor one being
  // handed to or from it (as for NPCs, OwnedHash).
  return consistency::TrafficDigest(
      m_traffic, {}, m_trafficOwner,
      [&](proto::TrafficId id, proto::PlayerId theirs) {
        return theirs == owner && !TrafficInHandover(id, owner);
      },
      &count);
}

void Endpoint::ResendOwnedTraffic() {
  proto::TrafficSet set;
  for (const auto &[id, spawn] : m_traffic)
    if (OwnsTraffic(id) && set.ids.size() < proto::kMaxTrafficSet)
      set.ids.push_back(id);
  Send(m_hostPeer, set);
  for (const auto id : set.ids) {
    // A vehicle handed to us keeps the id of the game that announced it:
    // the host knows it already (it moved it), only its state goes again.
    if (proto::TrafficOwner(id) == m_you)
      Send(m_hostPeer, m_traffic.at(id));
    if (const auto it = m_trafficStatus.find(id); it != m_trafficStatus.end())
      Send(m_hostPeer, it->second);
  }
}
} // namespace tandem
