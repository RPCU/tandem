// Vehicle combat in the session (docs/architecture/vehicle-combat.md): who
// says what about a vehicle, who hears it, what the host keeps for late
// joiners and the digests, and what goes when a player or a vehicle does.
// The rule is the authority table's: the game that simulates a vehicle owns
// it (VehicleOwner); everyone else shows a copy and sends the owner
// what they saw happen to it.

#include "tandem/endpoint.hpp"

#include <algorithm>
#include <cmath>

namespace tandem {
namespace {
template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};

bool Finite(const proto::Vec3 &v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// A damage state as safe to keep and act on: health in 0..1, a wreck with
// no health left, never more parts than a message holds.
proto::VehicleDamage Sanitized(proto::VehicleDamage damage) {
  if (!std::isfinite(damage.health))
    damage.health = 1.f;
  damage.health = std::clamp(damage.health, 0.f, 1.f);
  if (damage.flags & proto::kDamageDestroyed)
    damage.health = 0.f;
  if (damage.parts.size() > proto::kMaxDetachedParts)
    damage.parts.resize(proto::kMaxDetachedParts);
  return damage;
}
} // namespace

// ---- what this game says ----

void Endpoint::SetVehicleDamage(proto::VehicleDamage damage) {
  if (!IsJoined() || m_ended || VehicleOwner(damage.vehicle) != m_you)
    return;
  // A shared vehicle's damage is said while it is shared, by its owner.
  if (proto::IsStreetCar(damage.vehicle) &&
      !m_traffic.contains(proto::TrafficOfSeat(damage.vehicle)))
    return;
  damage = Sanitized(std::move(damage));
  if (const auto it = m_vehicleDamage.find(damage.vehicle); it != m_vehicleDamage.end()) {
    auto same = damage;
    same.revision = it->second.revision;
    if (same == it->second)
      return; // as it was
  }
  // Numbered from the host clock's millisecond at the first one: a game
  // back under its old id (after a crash) is newer than what it said before.
  if (m_damageRevision == 0)
    m_damageRevision = static_cast<std::uint32_t>(HostNowUs() / 1000);
  damage.revision = ++m_damageRevision;
  m_vehicleDamage[damage.vehicle] = damage;
  if (!IsHost())
    Send(m_hostPeer, damage);
  else if (proto::IsStreetCar(damage.vehicle))
    ShareTraffic(damage, m_you); // to whoever shows our street
  else
    Broadcast(damage);
}

void Endpoint::SendVehicleHit(proto::VehicleHit hit) {
  if (!IsJoined() || m_ended)
    return;
  const auto owner = VehicleOwner(hit.vehicle);
  if (owner == m_you || owner == proto::kNoPlayer)
    return; // our own vehicles' hits are our game's
  hit.player = m_you;
  if (!std::isfinite(hit.damage) || hit.damage < 0.f)
    hit.damage = 0.f;
  if (IsHost()) {
    if (const auto peer = PeerOf(owner))
      Send(*peer, hit);
  } else {
    Send(m_hostPeer, hit);
  }
}

void Endpoint::SendVehicleExit(proto::VehicleExit exit) {
  if (!IsJoined() || m_ended)
    return;
  exit.player = m_you;
  if (!Finite(exit.velocity))
    exit.velocity = {};
  if (IsHost())
    Broadcast(exit);
  else
    Send(m_hostPeer, exit);
}

void Endpoint::SetLocalVehicleCombat(proto::VehicleCombat combat) {
  combat.player = proto::kNoPlayer;
  combat.revision = 0;
  if (combat.mode == proto::SeatCombat::None)
    combat = {};
  if (m_localSeatCombat) {
    auto same = *m_localSeatCombat;
    same.player = proto::kNoPlayer;
    same.revision = 0;
    if (same == combat)
      return;
  } else if (combat.mode == proto::SeatCombat::None) {
    return; // none, as before
  }
  combat.revision = ++m_seatCombatRevision;
  m_localSeatCombat = combat;
  SendLocalVehicleCombatNow();
}

void Endpoint::SendLocalVehicleCombatNow() {
  if (!m_localSeatCombat || !IsJoined() || m_ended)
    return;
  m_localSeatCombat->player = m_you;
  if (m_localSeatCombat->mode == proto::SeatCombat::None)
    m_seatCombat.erase(m_you);
  else
    m_seatCombat[m_you] = *m_localSeatCombat;
  if (IsHost())
    Broadcast(*m_localSeatCombat);
  else
    Send(m_hostPeer, *m_localSeatCombat);
}

void Endpoint::SendMountedFire(proto::MountedFire fire) {
  if (!IsJoined() || m_ended)
    return;
  fire.player = m_you;
  fire.seq = ++m_mountedSeq;
  if (IsHost())
    Broadcast(fire);
  else
    Send(m_hostPeer, fire);
}

void Endpoint::AskGunner(proto::VehicleRef vehicle, std::uint64_t seat, bool leave) {
  if (!IsJoined() || m_ended)
    return;
  const proto::GunnerRequest request{
      .player = m_you, .vehicle = vehicle, .seat = seat, .leave = leave};
  const auto owner = VehicleOwner(vehicle);
  if (owner == m_you)
    return; // our own vehicle: our V sits where it likes
  if (IsHost()) {
    if (const auto peer = PeerOf(owner))
      Send(*peer, request);
  } else {
    Send(m_hostPeer, request);
  }
}

void Endpoint::FreeGunner(proto::VehicleRef vehicle) {
  if (!IsJoined() || m_ended || VehicleOwner(vehicle) != m_you)
    return;
  if (const auto freed = m_turrets.Free(vehicle))
    SayGunner(*freed, m_pending);
}

void Endpoint::SendGunnerAim(proto::GunnerAim aim) {
  if (!IsJoined() || m_ended)
    return;
  const auto it = m_gunners.find(aim.vehicle);
  if (it == m_gunners.end() || it->second.gunner != m_you)
    return; // not our seat
  if (!std::isfinite(aim.yaw) || !std::isfinite(aim.pitch))
    return;
  aim.player = m_you;
  aim.seq = ++m_aimSeq;
  aim.timeUs = HostNowUs();
  if (IsHost())
    Broadcast(aim);
  else
    Send(m_hostPeer, aim);
}

void Endpoint::SendVehicleBump(proto::VehicleBump bump) {
  if (!IsJoined() || m_ended || bump.other == m_you || !m_players.contains(bump.other))
    return;
  if (!Finite(bump.impulse) || !Finite(bump.position))
    return;
  bump.player = m_you;
  if (bump.atUs == 0)
    bump.atUs = HostNowUs();
  if (IsHost()) {
    if (const auto peer = PeerOf(bump.other))
      Send(*peer, bump);
  } else {
    Send(m_hostPeer, bump);
  }
}

void Endpoint::SendNpcRam(proto::NpcRam ram) {
  if (!IsJoined() || m_ended || !m_npcs.contains(ram.npc) || OwnsNpc(ram.npc))
    return; // our own NPCs are hit by our own game
  if (!Finite(ram.velocity) || !Finite(ram.position))
    return;
  ram.player = m_you;
  // To the NPC's owner (regions: whoever runs it now), through the host.
  if (IsHost())
    RouteToOwner(ram.npc, ram, m_you);
  else
    Send(m_hostPeer, ram);
}

void Endpoint::SendChase(const proto::Chase &chase) {
  // The vehicle's owner says whom it chases: the game that runs it.
  if (!IsJoined() || m_ended || !OwnsTraffic(chase.vehicle))
    return;
  const auto it = m_chases.find(chase.vehicle);
  if (chase.target == proto::kNoPlayer) {
    if (it == m_chases.end())
      return;
    m_chases.erase(it);
  } else {
    if (!m_players.contains(chase.target) || (it != m_chases.end() && it->second == chase))
      return;
    m_chases[chase.vehicle] = chase;
  }
  SayTraffic(chase);
}

// ---- records ----

bool Endpoint::NoteDamage(const proto::VehicleDamage &damage) {
  const auto it = m_vehicleDamage.find(damage.vehicle);
  if (it != m_vehicleDamage.end() &&
      (it->second == damage || !vehiclecombat::Supersedes(it->second, damage)))
    return false;
  m_vehicleDamage[damage.vehicle] = damage;
  return true;
}

bool Endpoint::NoteGunner(const proto::Gunner &gunner) {
  const auto it = m_gunners.find(gunner.vehicle);
  if (gunner.gunner == proto::kNoPlayer) {
    if (it == m_gunners.end())
      return false;
    m_gunners.erase(it);
    return true;
  }
  if (it != m_gunners.end() && it->second.gunner == gunner.gunner && it->second.seat == gunner.seat)
    return false;
  m_gunners[gunner.vehicle] = gunner;
  return true;
}

void Endpoint::SayGunner(const proto::Gunner &gunner, std::vector<SessionEvent> &out) {
  if (!NoteGunner(gunner))
    return;
  if (IsHost())
    Broadcast(gunner);
  else
    Send(m_hostPeer, gunner);
  out.push_back(events::GunnerChanged{gunner});
}

void Endpoint::GiveGunner(const proto::GunnerRequest &request, std::vector<SessionEvent> &out) {
  // Only a player in the session sits in our turret.
  if (!request.leave && !m_players.contains(request.player))
    return;
  const auto changed = m_turrets.Ask(request);
  for (const auto &gunner : changed)
    SayGunner(gunner, out);
  // Refused (taken by someone else): the asker hears who has it.
  if (changed.empty() && !request.leave)
    if (const auto it = m_gunners.find(request.vehicle);
        it != m_gunners.end() && it->second.gunner != request.player) {
      if (IsHost()) {
        if (const auto peer = PeerOf(request.player))
          Send(*peer, it->second);
      } else {
        Send(m_hostPeer, it->second);
      }
    }
}

void Endpoint::ForgetVehiclesOf(proto::PlayerId player, std::vector<SessionEvent> &out,
                                bool broadcast, bool returning) {
  m_vehicleDamage.erase(proto::PlayerCar(player));
  // Its weapons in a vehicle: none, for everyone.
  if (m_seatCombat.erase(player) > 0)
    out.push_back(events::RemoteVehicleCombat{{.player = player}});
  // The seats it held, and the seats of its vehicle: free.
  std::vector<proto::VehicleRef> freed;
  for (const auto &[vehicle, gunner] : m_gunners)
    if (gunner.gunner == player || VehicleOwner(vehicle) == player)
      freed.push_back(vehicle);
  for (const auto &gunner : m_turrets.Release(player))
    if (!std::ranges::contains(freed, gunner.vehicle))
      freed.push_back(gunner.vehicle);
  for (const auto vehicle : freed) {
    const auto it = m_gunners.find(vehicle);
    const proto::Gunner free{.vehicle = vehicle,
                             .revision = it == m_gunners.end() ? 0 : it->second.revision + 1};
    m_gunners.erase(vehicle);
    m_turrets.Set(free);
    if (broadcast)
      Broadcast(free);
    out.push_back(events::GunnerChanged{free});
  }
  if (returning)
    return;
  // Nobody to chase any more.
  for (auto it = m_chases.begin(); it != m_chases.end();) {
    if (it->second.target != player) {
      ++it;
      continue;
    }
    const proto::Chase over{.vehicle = it->first};
    if (broadcast)
      Broadcast(over);
    out.push_back(events::ChaseChanged{over});
    it = m_chases.erase(it);
  }
}

void Endpoint::ForgetStreetVehicle(proto::TrafficId id) {
  const auto vehicle = proto::StreetCar(id);
  m_vehicleDamage.erase(vehicle);
  m_chases.erase(id);
  m_gunners.erase(vehicle);
  m_turrets.Free(vehicle);
}

// ---- what the others say ----

void Endpoint::OnGuestVehicles(net::PeerId peer, proto::PlayerId player,
                               const proto::Message &message, std::vector<SessionEvent> &out) {
  std::visit(Overloaded{
                 [&](const proto::VehicleDamage &damage) {
                   // A guest only speaks for the vehicles it owns (its car,
                   // the shared ones its game runs).
                   if (VehicleOwner(damage.vehicle) != player)
                     return;
                   const bool street = proto::IsStreetCar(damage.vehicle);
                   if (street && !m_traffic.contains(proto::TrafficOfSeat(damage.vehicle)))
                     return;
                   const auto kept = Sanitized(damage);
                   if (!NoteDamage(kept))
                     return; // a repeat, or older than what we have
                   if (!street) {
                     Broadcast(kept, peer);
                     out.push_back(events::RemoteVehicleDamage{kept});
                   } else {
                     ShareTraffic(kept, player, peer);
                     if (Sees(m_you, player))
                       out.push_back(events::RemoteVehicleDamage{kept});
                   }
                 },
                 [&](const proto::VehicleHit &hit) {
                   auto mine = hit;
                   mine.player = player;
                   const auto owner = VehicleOwner(mine.vehicle);
                   if (owner == player || owner == proto::kNoPlayer)
                     return;
                   if (owner == m_you) {
                     if (!proto::IsStreetCar(mine.vehicle) ||
                         m_traffic.contains(proto::TrafficOfSeat(mine.vehicle)))
                       out.push_back(events::VehicleHitBy{mine});
                   } else if (const auto to = PeerOf(owner)) {
                     Send(*to, mine);
                   }
                 },
                 [&](const proto::VehicleExit &exit) {
                   auto relayed = exit;
                   relayed.player = player;
                   if (!Finite(relayed.velocity))
                     relayed.velocity = {};
                   Broadcast(relayed, peer);
                   out.push_back(events::RemoteVehicleExit{relayed});
                 },
                 [&](const proto::VehicleCombat &combat) {
                   auto relayed = combat;
                   relayed.player = player;
                   const auto it = m_seatCombat.find(player);
                   if (relayed.mode == proto::SeatCombat::None) {
                     if (it == m_seatCombat.end())
                       return;
                     m_seatCombat.erase(it);
                   } else {
                     if (it != m_seatCombat.end() && it->second == relayed)
                       return; // a repeat
                     m_seatCombat[player] = relayed;
                   }
                   Broadcast(relayed, peer);
                   out.push_back(events::RemoteVehicleCombat{relayed});
                 },
                 [&](const proto::MountedFire &fire) {
                   auto relayed = fire;
                   relayed.player = player;
                   Broadcast(relayed, peer);
                   out.push_back(events::RemoteMountedFire{relayed});
                 },
                 [&](const proto::GunnerRequest &request) {
                   auto mine = request;
                   mine.player = player;
                   const auto owner = VehicleOwner(mine.vehicle);
                   if (owner == player)
                     return;
                   if (owner == m_you)
                     GiveGunner(mine, out);
                   else if (const auto to = PeerOf(owner))
                     Send(*to, mine);
                 },
                 [&](const proto::Gunner &gunner) {
                   // Only the vehicle's owner gives its seats.
                   if (VehicleOwner(gunner.vehicle) != player)
                     return;
                   if (gunner.gunner != proto::kNoPlayer && !m_players.contains(gunner.gunner))
                     return; // given to someone who has gone meanwhile
                   if (!NoteGunner(gunner))
                     return;
                   Broadcast(gunner, peer);
                   out.push_back(events::GunnerChanged{gunner});
                 },
                 [&](const proto::GunnerAim &aim) {
                   const auto it = m_gunners.find(aim.vehicle);
                   if (it == m_gunners.end() || it->second.gunner != player)
                     return; // not its seat
                   auto relayed = aim;
                   relayed.player = player;
                   Broadcast(relayed, peer);
                   out.push_back(events::RemoteGunnerAim{relayed});
                 },
                 [&](const proto::VehicleBump &bump) {
                   auto relayed = bump;
                   relayed.player = player;
                   if (relayed.other == player || !Finite(relayed.impulse))
                     return;
                   if (relayed.other == m_you)
                     out.push_back(events::VehicleBumpBy{relayed});
                   else if (const auto to = PeerOf(relayed.other))
                     Send(*to, relayed);
                 },
                 [&](const proto::NpcRam &ram) {
                   // The NPC's owner applies it: us, or the guest that runs
                   // it (regions), passed on.
                   auto mine = ram;
                   mine.player = player;
                   if (Finite(mine.velocity) && RouteToOwner(mine.npc, mine, player))
                     out.push_back(events::NpcRamBy{mine});
                 },
                 [&](const proto::Chase &chase) {
                   // The vehicle's owner says whom it chases (the game that
                   // runs it, a guest's since regions).
                   if (!m_traffic.contains(chase.vehicle) ||
                       TrafficOwnerOf(chase.vehicle) != player)
                     return;
                   const auto it = m_chases.find(chase.vehicle);
                   if (chase.target == proto::kNoPlayer) {
                     if (it == m_chases.end())
                       return;
                     m_chases.erase(it);
                   } else {
                     if (!m_players.contains(chase.target) ||
                         (it != m_chases.end() && it->second == chase))
                       return;
                     m_chases[chase.vehicle] = chase;
                   }
                   ShareTraffic(chase, player, peer);
                   if (Sees(m_you, player))
                     out.push_back(events::ChaseChanged{chase});
                 },
                 [](const auto &) {},
             },
             message);
}

void Endpoint::OnHostVehicles(const proto::Message &message, std::vector<SessionEvent> &out) {
  std::visit(Overloaded{
                 [&](const proto::VehicleDamage &damage) {
                   // Ours is our game's: never taken from the network.
                   if (VehicleOwner(damage.vehicle) == m_you)
                     return;
                   const auto kept = Sanitized(damage);
                   if (proto::IsStreetCar(kept.vehicle) &&
                       !m_traffic.contains(proto::TrafficOfSeat(kept.vehicle)))
                     return; // a vehicle we do not know (its spawn comes first)
                   if (NoteDamage(kept))
                     out.push_back(events::RemoteVehicleDamage{kept});
                 },
                 [&](const proto::VehicleHit &hit) {
                   if (VehicleOwner(hit.vehicle) == m_you && hit.player != m_you)
                     out.push_back(events::VehicleHitBy{hit});
                 },
                 [&](const proto::VehicleExit &exit) {
                   if (exit.player != m_you)
                     out.push_back(events::RemoteVehicleExit{exit});
                 },
                 [&](const proto::VehicleCombat &combat) {
                   if (combat.player == m_you || combat.player == proto::kNoPlayer)
                     return;
                   const auto it = m_seatCombat.find(combat.player);
                   if (combat.mode == proto::SeatCombat::None) {
                     if (it == m_seatCombat.end())
                       return;
                     m_seatCombat.erase(it);
                   } else {
                     if (it != m_seatCombat.end() && it->second == combat)
                       return;
                     m_seatCombat[combat.player] = combat;
                   }
                   out.push_back(events::RemoteVehicleCombat{combat});
                 },
                 [&](const proto::MountedFire &fire) {
                   if (fire.player != m_you)
                     out.push_back(events::RemoteMountedFire{fire});
                 },
                 [&](const proto::GunnerRequest &request) {
                   // Relayed to us: we own the vehicle and give its seat.
                   if (VehicleOwner(request.vehicle) == m_you && request.player != m_you)
                     GiveGunner(request, out);
                 },
                 [&](const proto::Gunner &gunner) {
                   // The host's word on our own vehicle's seat (it freed it when
                   // the gunner went) is our arbitration's too.
                   if (VehicleOwner(gunner.vehicle) == m_you)
                     m_turrets.Set(gunner);
                   if (NoteGunner(gunner))
                     out.push_back(events::GunnerChanged{gunner});
                 },
                 [&](const proto::GunnerAim &aim) {
                   if (aim.player != m_you)
                     out.push_back(events::RemoteGunnerAim{aim});
                 },
                 [&](const proto::VehicleBump &bump) {
                   if (bump.other == m_you && bump.player != m_you)
                     out.push_back(events::VehicleBumpBy{bump});
                 },
                 [&](const proto::NpcRam &ram) {
                   // Passed on to us: we run that NPC (regions).
                   if (ram.player != m_you && OwnsNpc(ram.npc))
                     out.push_back(events::NpcRamBy{ram});
                 },
                 [&](const proto::Chase &chase) {
                   // Ours is our game's to say.
                   if (OwnsTraffic(chase.vehicle))
                     return;
                   const auto it = m_chases.find(chase.vehicle);
                   if (chase.target == proto::kNoPlayer) {
                     if (it == m_chases.end())
                       return;
                     m_chases.erase(it);
                   } else {
                     if (!m_traffic.contains(chase.vehicle) ||
                         (it != m_chases.end() && it->second == chase))
                       return;
                     m_chases[chase.vehicle] = chase;
                   }
                   out.push_back(events::ChaseChanged{chase});
                 },
                 [](const auto &) {},
             },
             message);
}

// ---- late joiners, resyncs, digests ----

void Endpoint::SendVehiclesTo(net::PeerId peer, proto::PlayerId them, std::uint8_t domains,
                              bool resync) {
  if (domains & proto::kDomainPlayers) {
    // The players' cars, never the newcomer's own back.
    for (const auto &[vehicle, damage] : m_vehicleDamage)
      if (!proto::IsStreetCar(vehicle) && vehicle != proto::PlayerCar(them))
        Send(peer, damage);
    for (const auto &[vehicle, gunner] : m_gunners)
      Send(peer, gunner);
    for (const auto &[player, combat] : m_seatCombat)
      if (player != them)
        Send(peer, combat);
    if (resync)
      for (const auto &[id, info] : m_players) {
        if (id == them)
          continue;
        if (!m_seatCombat.contains(id))
          Send(peer, proto::VehicleCombat{.player = id});
        // Their vehicle's seat, free unless someone holds it.
        if (!m_gunners.contains(proto::PlayerCar(id)))
          Send(peer, proto::Gunner{.vehicle = proto::PlayerCar(id)});
      }
  }
  if (domains & proto::kDomainNpcs) {
    // After the spawns (SendDomainsTo): how each shared vehicle it sees is
    // damaged and whom it chases; in a resync, which ones chase nobody.
    const auto shown = [&](proto::TrafficId id) {
      return m_traffic.contains(id) && Sees(them, TrafficOwnerOf(id));
    };
    for (const auto &[vehicle, damage] : m_vehicleDamage)
      if (proto::IsStreetCar(vehicle) && shown(proto::TrafficOfSeat(vehicle)))
        Send(peer, damage);
    for (const auto &[id, chase] : m_chases)
      if (shown(id))
        Send(peer, chase);
    if (resync)
      for (const auto &[id, spawn] : m_traffic)
        if (!m_chases.contains(id) && shown(id))
          Send(peer, proto::Chase{.vehicle = id});
  }
}

std::uint32_t Endpoint::VehiclePlayersDigest(proto::PlayerId self) const {
  consistency::Hasher hash;
  for (const auto &[vehicle, damage] : m_vehicleDamage)
    if (!proto::IsStreetCar(vehicle) && vehicle != proto::PlayerCar(self))
      hash.Add(std::uint64_t{vehicle})
          .Add(damage.health)
          .Add(std::uint64_t{damage.flags})
          .Add(std::uint64_t{damage.tyres})
          .Add(std::uint64_t{damage.parts.size()});
  hash.Add(std::uint64_t{0xB0});
  for (const auto &[vehicle, gunner] : m_gunners)
    hash.Add(std::uint64_t{vehicle}).Add(std::uint64_t{gunner.gunner}).Add(gunner.seat);
  hash.Add(std::uint64_t{0xB1});
  for (const auto &[player, combat] : m_seatCombat)
    if (player != self)
      hash.Add(std::uint64_t{player})
          .Add(std::uint64_t{static_cast<std::uint8_t>(combat.mode)})
          .Add(std::uint64_t{combat.vehicle})
          .Add(combat.weapon);
  return hash.Value();
}

std::uint32_t Endpoint::VehicleStreetDigest(proto::PlayerId self) const {
  // The shared vehicles `self` shows (as TrafficDigestFor).
  const auto shown = [&](proto::TrafficId id) {
    const auto owner = TrafficOwnerOf(id);
    return m_traffic.contains(id) && owner != self && (!IsHost() || m_links.Linked(owner, self));
  };
  consistency::Hasher hash;
  for (const auto &[vehicle, damage] : m_vehicleDamage)
    if (proto::IsStreetCar(vehicle) && shown(proto::TrafficOfSeat(vehicle)))
      hash.Add(std::uint64_t{vehicle})
          .Add(std::uint64_t{damage.flags})
          .Add(std::uint64_t{damage.tyres})
          .Add(std::uint64_t{damage.parts.size()});
  hash.Add(std::uint64_t{0xB2});
  for (const auto &[id, chase] : m_chases)
    if (shown(id))
      hash.Add(std::uint64_t{id}).Add(std::uint64_t{chase.target}).Add(std::uint64_t{chase.flags});
  return hash.Value();
}
} // namespace tandem
