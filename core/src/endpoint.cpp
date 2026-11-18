#include "tandem/endpoint.hpp"

#include <algorithm>
#include <cmath>

namespace tandem {
namespace {
constexpr std::uint64_t kPingIntervalUs = 500'000;
// The clock offset follows the lowest-RTT ping, but forgets it slowly so a
// route change or a drifting clock is picked up.
constexpr std::uint64_t kRttForgetUsPerPing = 200;

// What a game says of its own time ability, made safe to act on: a speed in
// 0.05..1 (a bubble never stops NPCs, nor speeds them up), 1 without a
// bubble ability.
proto::TimeEffect Sanitized(proto::TimeEffect effect) {
  if (!std::isfinite(effect.scale) || (effect.abilities & proto::kTimeBubbleAbilities) == 0)
    effect.scale = 1.f;
  effect.scale = std::clamp(effect.scale, 0.05f, 1.f);
  return effect;
}

template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
} // namespace

Endpoint::Endpoint(std::unique_ptr<net::Session> session, Identity identity, Timing timing)
    : m_session(std::move(session)), m_identity(std::move(identity)), m_timing(timing) {}

std::unique_ptr<Endpoint> Endpoint::Host(Identity identity, std::uint16_t port, std::string &error,
                                         Timing timing) {
  auto session = net::Session::Listen(port, kMaxGuests + kSpareConnections, error, timing.timeout);
  if (!session)
    return nullptr;
  auto endpoint =
      std::unique_ptr<Endpoint>(new Endpoint(std::move(session), std::move(identity), timing));
  endpoint->m_you = endpoint->m_host = 1;
  endpoint->m_players[1] = {1, endpoint->m_identity.name, endpoint->m_identity.body};
  return endpoint;
}

std::unique_ptr<Endpoint> Endpoint::Join(Identity identity, const std::string &host,
                                         std::uint16_t port, std::string &error, Timing timing) {
  auto session = net::Session::Connect(host, port, error, timing.timeout);
  if (!session)
    return nullptr;
  return std::unique_ptr<Endpoint>(new Endpoint(std::move(session), std::move(identity), timing));
}

std::vector<SessionEvent> Endpoint::Update(std::chrono::milliseconds wait) {
  std::vector<SessionEvent> out;
  if (m_ended)
    return out;

  // The host is in its session from the start: report it once.
  if (IsHost() && m_seq == 0 && m_players.size() == 1 && m_playerOfPeer.empty()) {
    out.push_back(events::Joined{m_you, m_host});
    m_seq = 1;
  }

  for (const auto &event : m_session->Poll(wait)) {
    if (IsHost())
      OnHostEvent(event, out);
    else
      OnGuestEvent(event, out);
  }

  if (!IsHost() && IsJoined()) {
    const auto now = net::NowUs();
    if (now - m_lastPingUs >= kPingIntervalUs) {
      m_lastPingUs = now;
      m_session->Send(m_hostPeer, proto::Ping{now});
    }
  }
  return out;
}

void Endpoint::OnHostEvent(const net::Event &event, std::vector<SessionEvent> &out) {
  using Kind = net::Event::Kind;
  switch (event.kind) {
  case Kind::Connected:
    break; // nothing until a valid Hello
  case Kind::Malformed:
    m_session->Disconnect(event.peer);
    break;
  case Kind::Disconnected:
    RemovePlayer(event.peer, out);
    break;
  case Kind::Received: {
    const auto &message = *event.message;
    const auto known = m_playerOfPeer.find(event.peer);

    if (const auto *hello = std::get_if<proto::Hello>(&message)) {
      if (known == m_playerOfPeer.end())
        OnHello(event.peer, *hello, out);
      return;
    }

    // Everything else needs a welcomed player.
    if (known == m_playerOfPeer.end())
      return;
    const auto player = known->second;
    if (proto::IsStory(message)) {
      OnGuestStory(event.peer, player, message, out);
      return;
    }

    std::visit(Overloaded{
                   [&](const proto::Ping &ping) {
                     m_session->Send(event.peer, proto::Pong{ping.sentUs, HostNowUs()});
                   },
                   [&](const proto::PlayerState &state) {
                     // A guest only speaks for its own V.
                     auto relayed = state;
                     relayed.player = player;
                     m_session->Broadcast(relayed, event.peer);
                     out.push_back(events::RemoteState{relayed});
                   },
                   [&](const proto::PoseState &pose) {
                     auto relayed = pose;
                     relayed.player = player;
                     m_session->Broadcast(relayed, event.peer);
                     out.push_back(events::RemotePose{relayed});
                   },
                   [&](const proto::Appearance &appearance) {
                     auto relayed = appearance;
                     relayed.player = player;
                     m_appearances[player] = relayed;
                     m_session->Broadcast(relayed, event.peer);
                     out.push_back(events::RemoteAppearance{relayed});
                   },
                   [&](const proto::Held &held) {
                     auto relayed = held;
                     relayed.player = player;
                     m_held[player] = relayed;
                     m_session->Broadcast(relayed, event.peer);
                     out.push_back(events::RemoteHeld{relayed});
                   },
                   [&](const proto::Seat &seat) {
                     auto relayed = seat;
                     relayed.player = player;
                     m_seats[player] = relayed;
                     m_session->Broadcast(relayed, event.peer);
                     out.push_back(events::RemoteSeat{relayed});
                   },
                   [&](const proto::VehicleState &vehicle) {
                     auto relayed = vehicle;
                     relayed.player = player;
                     m_session->Broadcast(relayed, event.peer);
                     out.push_back(events::RemoteVehicle{relayed});
                   },
                   [&](const proto::VehicleRadio &radio) {
                     // A guest only speaks for the car it drives.
                     auto relayed = radio;
                     relayed.player = player;
                     m_radios[player] = relayed;
                     m_session->Broadcast(relayed, event.peer);
                     out.push_back(events::RemoteVehicleRadio{relayed});
                   },
                   [&](const proto::Shot &shot) {
                     auto relayed = shot;
                     relayed.player = player;
                     m_session->Broadcast(relayed, event.peer);
                     out.push_back(events::RemoteShot{relayed});
                   },
                   [&](const proto::NpcHit &hit) {
                     // For the host's game only: it owns the NPC.
                     auto mine = hit;
                     mine.player = player;
                     out.push_back(events::NpcHitBy{mine});
                   },
                   [&](const proto::NpcEffect &effect) {
                     auto mine = effect;
                     mine.player = player;
                     out.push_back(events::NpcEffectBy{mine});
                   },
                   [&](const proto::NpcHack &hack) {
                     // For the host's game only: it owns the NPC.
                     auto mine = hack;
                     mine.player = player;
                     out.push_back(events::NpcHackBy{mine});
                   },
                   [&](const proto::VehicleHack &hack) {
                     // The driver's game owns the car: ours, or pass it on.
                     auto mine = hack;
                     mine.player = player;
                     if (mine.driver == m_you)
                       out.push_back(events::VehicleHackOn{mine});
                     else if (const auto peer = PeerOf(mine.driver); peer && mine.driver != player)
                       m_session->Send(*peer, mine);
                   },
                   [&](const proto::Stealth &stealth) {
                     auto relayed = stealth;
                     relayed.player = player;
                     m_stealth[player] = relayed;
                     m_session->Broadcast(relayed, event.peer);
                     out.push_back(events::RemoteStealth{relayed});
                   },
                   [&](const proto::Noise &noise) {
                     // For the host's game only: its NPCs hear it.
                     auto mine = noise;
                     mine.player = player;
                     out.push_back(events::NoiseBy{mine});
                   },
                   [&](const proto::Pause &pause) {
                     // A guest only pauses in its own name.
                     const proto::Pause relayed{player, pause.paused};
                     if (!NotePaused(player, pause.paused))
                       return;
                     m_session->Broadcast(relayed, event.peer);
                     out.push_back(events::PauseChanged{relayed});
                   },
                   [&](const proto::TimeEffect &time) {
                     // A guest only bends time in its own name.
                     auto relayed = Sanitized(time);
                     relayed.player = player;
                     if (!NoteTime(relayed))
                       return;
                     m_session->Broadcast(relayed, event.peer);
                     out.push_back(events::RemoteTimeEffect{relayed});
                   },
                   [&](const proto::Bye &) {
                     RemovePlayer(event.peer, out); // gone now, not when ENet is done
                     m_session->Disconnect(event.peer);
                   },
                   [](const auto &) {},
               },
               message);
    break;
  }
  }
}

void Endpoint::RemovePlayer(net::PeerId peer, std::vector<SessionEvent> &out) {
  const auto it = m_playerOfPeer.find(peer);
  if (it == m_playerOfPeer.end())
    return;
  const auto player = it->second;
  m_playerOfPeer.erase(it);
  m_players.erase(player);
  m_appearances.erase(player);
  m_held.erase(player);
  m_seats.erase(player);
  m_radios.erase(player);
  m_stealth.erase(player);
  m_story.Forget(player);
  // Gone while in the pause menu: no longer pausing anyone.
  if (NotePaused(player, false)) {
    m_session->Broadcast(proto::Pause{player, false});
    out.push_back(events::PauseChanged{{player, false}});
  }
  // Gone in a Sandevistan: its bubble ends for everyone.
  EndTimeOf(player, out, true);
  out.push_back(events::PlayerLeft{player});
  BroadcastRoster();
}

void Endpoint::OnHello(net::PeerId peer, const proto::Hello &hello,
                       std::vector<SessionEvent> &out) {
  if (hello.magic != proto::kMagic || hello.version != proto::kProtocolVersion) {
    m_session->Send(peer,
                    proto::Refused{"protocol version " + std::to_string(hello.version) +
                                   ", the host runs " + std::to_string(proto::kProtocolVersion)});
    m_session->Disconnect(peer);
    return;
  }

  // A player back on a new connection (its game crashed and restarted, or
  // its link dropped) while the old one has not timed out yet: if the old one
  // has gone silent, the new one takes its place, with its id, puppet and
  // seat. A live player keeps its place: a namesake joins as someone else.
  const auto silent = std::ranges::find_if(m_playerOfPeer, [&](const auto &entry) {
    if (m_players[entry.second].name != hello.name)
      return false;
    const auto link = m_session->StatsOf(entry.first);
    return !link || std::chrono::milliseconds{link->quietMs} >= m_timing.takeover;
  });
  if (silent != m_playerOfPeer.end()) {
    const auto [oldPeer, player] = *silent;
    m_playerOfPeer.erase(silent);
    m_session->Drop(oldPeer);
    m_playerOfPeer[peer] = player;
    m_players[player].body = hello.body;
    m_session->Send(peer, proto::Welcome{player, m_host, HostNowUs()});
    out.push_back(events::PlayerReturned{m_players[player]});
    // Back from a crash: a fresh game, not in its pause menu any more.
    if (NotePaused(player, false)) {
      m_session->Broadcast(proto::Pause{player, false});
      out.push_back(events::PauseChanged{{player, false}});
    }
    EndTimeOf(player, out, true);
    BroadcastRoster();
    SendSessionTo(peer);
    return;
  }

  if (m_playerOfPeer.size() >= kMaxGuests) {
    m_session->Send(peer, proto::Refused{"the session is full"});
    m_session->Disconnect(peer);
    return;
  }

  const auto player = m_nextPlayer++;
  m_playerOfPeer[peer] = player;
  m_players[player] = {player, hello.name, hello.body};
  m_session->Send(peer, proto::Welcome{player, m_host, HostNowUs()});
  out.push_back(events::PlayerJoined{m_players[player]});
  BroadcastRoster();
  SendSessionTo(peer);
}

// Everyone already here, as they look now, and the world: what a player
// joining (or coming back) needs.
void Endpoint::SendSessionTo(net::PeerId peer) {
  for (const auto &[id, appearance] : m_appearances)
    m_session->Send(peer, appearance);
  for (const auto &[id, held] : m_held)
    m_session->Send(peer, held);
  for (const auto &[id, seat] : m_seats)
    m_session->Send(peer, seat);
  for (const auto &[id, radio] : m_radios)
    m_session->Send(peer, radio);
  for (const auto &[id, stealth] : m_stealth)
    m_session->Send(peer, stealth);
  if (m_world)
    m_session->Send(peer, *m_world);
  for (const auto &[npc, spawn] : m_npcs)
    m_session->Send(peer, spawn);
  for (const auto &[npc, health] : m_npcHealth)
    m_session->Send(peer, health);
  // The story so far: journal, tracked entry, call, choice hub, story states.
  for (const auto &message : m_story.ForJoiner())
    m_session->Send(peer, message);
  // Who is in their pause menu (the newcomer itself is not).
  const auto self = m_playerOfPeer.find(peer);
  for (const auto paused : m_paused)
    if (self == m_playerOfPeer.end() || paused != self->second)
      m_session->Send(peer, proto::Pause{paused, true});
  // Who is in a Sandevistan (or another time ability) now.
  for (const auto &[id, time] : m_timeEffects)
    if (self == m_playerOfPeer.end() || id != self->second)
      m_session->Send(peer, time);
}

void Endpoint::OnGuestEvent(const net::Event &event, std::vector<SessionEvent> &out) {
  using Kind = net::Event::Kind;
  switch (event.kind) {
  case Kind::Connected:
    m_hostPeer = event.peer;
    m_session->Send(
        m_hostPeer,
        proto::Hello{.name = m_identity.name, .gameVersion = {}, .body = m_identity.body});
    break;
  case Kind::Malformed:
    break; // the host may be newer; Refused will say so
  case Kind::Disconnected:
    m_ended = true;
    out.push_back(events::Ended{IsJoined() ? "the host is gone" : "could not reach the host"});
    break;
  case Kind::Received:
    if (proto::IsStory(*event.message)) {
      // Only the host speaks for the story; our own relayed states are ours.
      if (const auto *state = std::get_if<proto::StoryState>(&*event.message);
          state && state->player == m_you)
        break;
      if (const auto *cursor = std::get_if<proto::ChoiceCursor>(&*event.message);
          cursor && cursor->player == m_you)
        break;
      if (IsJoined())
        out.push_back(events::Story{*event.message});
      break;
    }
    std::visit(
        Overloaded{
            [&](const proto::Welcome &welcome) {
              m_you = welcome.you;
              m_host = welcome.host;
              m_clockOffsetUs = static_cast<std::int64_t>(welcome.hostTimeUs) -
                                static_cast<std::int64_t>(net::NowUs());
              // Sequences start at the host clock's millisecond: a player
              // back under its old id (after a crash) is newer than
              // everything it sent before, which receivers already hold.
              m_seq = m_poseSeq = m_vehicleSeq = m_shotSeq =
                  static_cast<std::uint32_t>(welcome.hostTimeUs / 1000);
              out.push_back(events::Joined{m_you, m_host});
              SendLocalAppearanceNow();
              SendLocalHeldNow();
              SendLocalSeatNow();
              SendLocalRadioNow();
              SendLocalPausedNow();
              SendLocalStealthNow();
              SendLocalTimeNow();
            },
            [&](const proto::Refused &refused) {
              m_ended = true;
              out.push_back(events::Ended{"refused: " + refused.reason});
            },
            [&](const proto::Pong &pong) {
              const auto now = net::NowUs();
              const auto rtt = now - pong.pingSentUs;
              m_bestRttUs += kRttForgetUsPerPing;
              if (rtt <= m_bestRttUs) {
                m_bestRttUs = rtt;
                const auto midpoint = pong.pingSentUs + rtt / 2;
                m_clockOffsetUs = static_cast<std::int64_t>(pong.hostTimeUs) -
                                  static_cast<std::int64_t>(midpoint);
              }
            },
            [&](const proto::Roster &roster) {
              std::map<proto::PlayerId, proto::PlayerInfo> next;
              for (const auto &player : roster.players)
                next[player.id] = player;
              for (const auto &[id, info] : next)
                if (id != m_you && !m_players.contains(id))
                  out.push_back(events::PlayerJoined{info});
              for (const auto &[id, info] : m_players)
                if (!next.contains(id)) {
                  if (NotePaused(id, false))
                    out.push_back(events::PauseChanged{{id, false}});
                  EndTimeOf(id, out, false);
                  out.push_back(events::PlayerLeft{id});
                }
              m_players = std::move(next);
            },
            [&](const proto::PlayerState &state) {
              if (state.player != m_you)
                out.push_back(events::RemoteState{state});
            },
            [&](const proto::PoseState &pose) {
              if (pose.player != m_you)
                out.push_back(events::RemotePose{pose});
            },
            [&](const proto::Appearance &appearance) {
              if (appearance.player != m_you)
                out.push_back(events::RemoteAppearance{appearance});
            },
            [&](const proto::Held &held) {
              if (held.player != m_you)
                out.push_back(events::RemoteHeld{held});
            },
            [&](const proto::Shot &shot) {
              if (shot.player != m_you)
                out.push_back(events::RemoteShot{shot});
            },
            [&](const proto::Seat &seat) {
              if (seat.player != m_you)
                out.push_back(events::RemoteSeat{seat});
            },
            [&](const proto::VehicleState &vehicle) {
              if (vehicle.player != m_you)
                out.push_back(events::RemoteVehicle{vehicle});
            },
            [&](const proto::VehicleRadio &radio) {
              if (radio.player != m_you)
                out.push_back(events::RemoteVehicleRadio{radio});
            },
            [&](const proto::World &world) { out.push_back(events::HostWorld{world}); },
            [&](const proto::NpcSpawn &spawn) { out.push_back(events::HostNpcSpawn{spawn}); },
            [&](const proto::NpcStates &states) { out.push_back(events::HostNpcStates{states}); },
            [&](const proto::NpcGone &gone) { out.push_back(events::HostNpcGone{gone}); },
            [&](const proto::NpcHealth &health) { out.push_back(events::HostNpcHealth{health}); },
            [&](const proto::NpcHackState &state) {
              out.push_back(events::HostNpcHackState{state});
            },
            [&](const proto::NpcStatus &status) {
              if (status.player != m_you)
                out.push_back(events::HostNpcStatus{status});
            },
            [&](const proto::PlayerHack &hack) {
              if (hack.player == m_you)
                out.push_back(events::HostPlayerHack{hack});
            },
            [&](const proto::VehicleHack &hack) {
              if (hack.driver == m_you && hack.player != m_you)
                out.push_back(events::VehicleHackOn{hack});
            },
            [&](const proto::PlayerHit &hit) {
              // Only ever meant for us: anything else is not ours to apply.
              if (hit.player == m_you)
                out.push_back(events::HitByNpc{hit});
            },
            [&](const proto::NpcAwareness &awareness) {
              out.push_back(events::HostNpcAwareness{awareness});
            },
            [&](const proto::Stealth &stealth) {
              if (stealth.player != m_you)
                out.push_back(events::RemoteStealth{stealth});
            },
            [&](const proto::Pause &pause) {
              if (pause.player != m_you && NotePaused(pause.player, pause.paused))
                out.push_back(events::PauseChanged{pause});
            },
            [&](const proto::TimeEffect &time) {
              const auto safe = Sanitized(time);
              if (safe.player != m_you && NoteTime(safe))
                out.push_back(events::RemoteTimeEffect{safe});
            },
            [&](const proto::Bye &bye) {
              m_ended = true;
              out.push_back(events::Ended{bye.reason});
            },
            [](const auto &) {},
        },
        *event.message);
    break;
  }
}

void Endpoint::BroadcastRoster() {
  proto::Roster roster;
  for (const auto &[id, info] : m_players)
    roster.players.push_back(info);
  m_session->Broadcast(roster);
}

void Endpoint::SendLocalState(proto::PlayerState state) {
  if (!IsJoined() || m_ended)
    return;
  state.player = m_you;
  state.seq = ++m_seq;
  state.timeUs = HostNowUs();
  if (IsHost())
    m_session->Broadcast(state);
  else
    m_session->Send(m_hostPeer, state);
}

void Endpoint::SendLocalPose(proto::PoseState pose) {
  if (!IsJoined() || m_ended)
    return;
  pose.player = m_you;
  pose.seq = ++m_poseSeq;
  pose.timeUs = HostNowUs();
  if (IsHost())
    m_session->Broadcast(pose);
  else
    m_session->Send(m_hostPeer, pose);
}

void Endpoint::SetLocalAppearance(proto::Appearance appearance) {
  appearance.revision = ++m_appearanceRevision;
  m_localAppearance = std::move(appearance);
  SendLocalAppearanceNow();
}

void Endpoint::SendLocalAppearanceNow() {
  if (!m_localAppearance || !IsJoined() || m_ended)
    return;
  m_localAppearance->player = m_you;
  if (IsHost()) {
    m_appearances[m_you] = *m_localAppearance;
    m_session->Broadcast(*m_localAppearance);
  } else {
    m_session->Send(m_hostPeer, *m_localAppearance);
  }
}

void Endpoint::SetLocalHeld(proto::Held held) {
  held.revision = ++m_heldRevision;
  m_localHeld = held;
  SendLocalHeldNow();
}

void Endpoint::SendLocalHeldNow() {
  if (!m_localHeld || !IsJoined() || m_ended)
    return;
  m_localHeld->player = m_you;
  if (IsHost()) {
    m_held[m_you] = *m_localHeld;
    m_session->Broadcast(*m_localHeld);
  } else {
    m_session->Send(m_hostPeer, *m_localHeld);
  }
}

void Endpoint::SetLocalSeat(proto::Seat seat) {
  seat.revision = ++m_seatRevision;
  m_localSeat = seat;
  SendLocalSeatNow();
}

void Endpoint::SendLocalSeatNow() {
  if (!m_localSeat || !IsJoined() || m_ended)
    return;
  m_localSeat->player = m_you;
  if (m_localSeat->owner == proto::kNoPlayer && m_localSeat->vehicle != 0)
    m_localSeat->owner = m_you;
  if (IsHost()) {
    m_seats[m_you] = *m_localSeat;
    m_session->Broadcast(*m_localSeat);
  } else {
    m_session->Send(m_hostPeer, *m_localSeat);
  }
}

void Endpoint::SetLocalVehicleRadio(proto::VehicleRadio radio) {
  radio.revision = ++m_radioRevision;
  m_localRadio = radio;
  SendLocalRadioNow();
}

void Endpoint::SendLocalRadioNow() {
  if (!m_localRadio || !IsJoined() || m_ended)
    return;
  m_localRadio->player = m_you;
  if (IsHost()) {
    m_radios[m_you] = *m_localRadio;
    m_session->Broadcast(*m_localRadio);
  } else {
    m_session->Send(m_hostPeer, *m_localRadio);
  }
}

void Endpoint::SetWorld(proto::World world) {
  if (!IsHost() || m_ended)
    return; // a guest's world is the host's
  world.revision = ++m_worldRevision;
  m_world = world;
  m_session->Broadcast(world);
}

void Endpoint::SendNpcSpawn(const proto::NpcSpawn &spawn) {
  if (!IsHost() || m_ended)
    return;
  m_npcs[spawn.npc] = spawn;
  m_session->Broadcast(spawn);
}

void Endpoint::SendNpcStates(proto::NpcStates states) {
  if (!IsHost() || m_ended)
    return;
  states.seq = ++m_npcSeq;
  states.timeUs = HostNowUs();
  // The latest position also goes to the kept spawn, for late joiners.
  for (const auto &entry : states.npcs)
    if (const auto it = m_npcs.find(entry.npc); it != m_npcs.end()) {
      it->second.position = entry.position;
      it->second.yaw = entry.yaw;
    }
  m_session->Broadcast(states);
}

void Endpoint::SendNpcGone(const proto::NpcGone &gone) {
  if (!IsHost() || m_ended)
    return;
  m_npcs.erase(gone.npc);
  m_npcHealth.erase(gone.npc);
  m_session->Broadcast(gone);
}

void Endpoint::SendNpcHealth(const proto::NpcHealth &health) {
  if (!IsHost() || m_ended)
    return;
  m_npcHealth[health.npc] = health;
  m_session->Broadcast(health);
}

void Endpoint::SendNpcEffect(const proto::NpcEffect &effect) {
  if (IsHost() || !IsJoined() || m_ended)
    return;
  auto mine = effect;
  mine.player = m_you;
  m_session->Send(m_hostPeer, mine);
}

bool Endpoint::SendPlayerHit(const proto::PlayerHit &hit) {
  if (!IsHost() || m_ended || hit.player == m_you)
    return false; // our own V's hits are our game's
  const auto target = std::ranges::find_if(
      m_playerOfPeer, [&](const auto &entry) { return entry.second == hit.player; });
  if (target == m_playerOfPeer.end())
    return false;
  m_session->Send(target->first, hit); // that player's game only
  return true;
}

void Endpoint::SendNpcHit(const proto::NpcHit &hit) {
  if (IsHost() || !IsJoined() || m_ended)
    return; // the host's own hits are its game's
  auto mine = hit;
  mine.player = m_you;
  m_session->Send(m_hostPeer, mine);
}

void Endpoint::SendNpcHack(const proto::NpcHack &hack) {
  if (IsHost() || !IsJoined() || m_ended)
    return; // the host's own uploads are its game's
  auto mine = hack;
  mine.player = m_you;
  m_session->Send(m_hostPeer, mine);
}

void Endpoint::SendNpcHackState(const proto::NpcHackState &state) {
  if (!IsHost() || m_ended)
    return;
  m_session->Broadcast(state);
}

void Endpoint::SendNpcStatus(const proto::NpcStatus &status) {
  if (!IsHost() || m_ended)
    return;
  m_session->Broadcast(status);
}

void Endpoint::SendPlayerHack(const proto::PlayerHack &hack) {
  if (!IsHost() || m_ended)
    return;
  if (const auto peer = PeerOf(hack.player))
    m_session->Send(*peer, hack);
}

void Endpoint::SendVehicleHack(proto::VehicleHack hack) {
  if (!IsJoined() || m_ended || hack.driver == m_you)
    return; // our own car is not a copy: nothing to send
  hack.player = m_you;
  if (!IsHost())
    m_session->Send(m_hostPeer, hack);
  else if (const auto peer = PeerOf(hack.driver))
    m_session->Send(*peer, hack);
}

std::optional<net::PeerId> Endpoint::PeerOf(proto::PlayerId player) const {
  for (const auto &[peer, id] : m_playerOfPeer)
    if (id == player)
      return peer;
  return std::nullopt;
}

void Endpoint::SendLocalVehicle(proto::VehicleState vehicle) {
  if (!IsJoined() || m_ended)
    return;
  vehicle.player = m_you;
  vehicle.seq = ++m_vehicleSeq;
  vehicle.timeUs = HostNowUs();
  if (IsHost())
    m_session->Broadcast(vehicle);
  else
    m_session->Send(m_hostPeer, vehicle);
}

void Endpoint::SendLocalShot(proto::Shot shot) {
  if (!IsJoined() || m_ended)
    return;
  shot.player = m_you;
  shot.seq = ++m_shotSeq;
  if (IsHost())
    m_session->Broadcast(shot);
  else
    m_session->Send(m_hostPeer, shot);
}

void Endpoint::SetLocalPaused(bool paused) {
  if (paused == m_localPaused)
    return;
  m_localPaused = paused;
  SendLocalPausedNow();
}

void Endpoint::SendLocalPausedNow() {
  if (!IsJoined() || m_ended)
    return;
  // A guest that just joined has something to say only if it is paused.
  if (!NotePaused(m_you, m_localPaused))
    return;
  if (IsHost())
    m_session->Broadcast(proto::Pause{m_you, m_localPaused});
  else
    m_session->Send(m_hostPeer, proto::Pause{m_you, m_localPaused});
}

void Endpoint::SetLocalStealth(proto::Stealth stealth) {
  stealth.revision = ++m_stealthRevision;
  m_localStealth = stealth;
  SendLocalStealthNow();
}

void Endpoint::SendLocalStealthNow() {
  if (!m_localStealth || !IsJoined() || m_ended)
    return;
  m_localStealth->player = m_you;
  if (IsHost()) {
    m_stealth[m_you] = *m_localStealth;
    m_session->Broadcast(*m_localStealth);
  } else {
    m_session->Send(m_hostPeer, *m_localStealth);
  }
}

void Endpoint::SendNpcAwareness(proto::NpcAwareness awareness) {
  if (!IsHost() || m_ended)
    return;
  awareness.seq = ++m_awarenessSeq;
  awareness.timeUs = HostNowUs();
  if (awareness.entries.size() > proto::kMaxNpcAwareness)
    awareness.entries.resize(proto::kMaxNpcAwareness);
  m_session->Broadcast(awareness);
}

void Endpoint::SendNoise(const proto::Noise &noise) {
  if (IsHost() || !IsJoined() || m_ended)
    return; // the host's own noises are heard by its NPCs already
  auto mine = noise;
  mine.player = m_you;
  m_session->Send(m_hostPeer, mine);
}

bool Endpoint::NotePaused(proto::PlayerId player, bool paused) {
  return paused ? m_paused.insert(player).second : m_paused.erase(player) > 0;
}

void Endpoint::OnGuestStory(net::PeerId peer, proto::PlayerId player, const proto::Message &message,
                            std::vector<SessionEvent> &out) {
  if (const auto *action = std::get_if<proto::PhoneAction>(&message)) {
    auto mine = *action;
    mine.player = player;
    if (mine.action == proto::kPhoneDial) {
      out.push_back(events::Story{mine});
      return;
    }
    // The first answer wins; the call's new state goes to everyone.
    if (!m_calls.TryAnswer(player, mine.action, mine.contact))
      return;
    out.push_back(events::Story{mine});
    const auto call = *m_calls.Call();
    m_story.Note(call);
    m_session->Broadcast(call);
  } else if (const auto *reply = std::get_if<proto::PhoneReply>(&message)) {
    auto mine = *reply;
    mine.player = player;
    out.push_back(events::Story{mine});
  } else if (const auto *cursor = std::get_if<proto::ChoiceCursor>(&message)) {
    auto relayed = *cursor;
    relayed.player = player;
    m_session->Broadcast(relayed, peer);
    out.push_back(events::Story{relayed});
  } else if (const auto *state = std::get_if<proto::StoryState>(&message)) {
    auto relayed = *state;
    relayed.player = player;
    m_story.Note(relayed);
    m_session->Broadcast(relayed, peer);
    out.push_back(events::Story{relayed});
  }
  // The rest (journal, calls, lines, hubs, rewards) is the host's to say.
}

void Endpoint::SendStory(proto::Message message, proto::PlayerId to) {
  if (!IsJoined() || m_ended || !proto::IsStory(message))
    return;
  if (!IsHost()) {
    // A guest speaks for itself, and only about what it may ask.
    if (auto *action = std::get_if<proto::PhoneAction>(&message))
      action->player = m_you;
    else if (auto *reply = std::get_if<proto::PhoneReply>(&message))
      reply->player = m_you;
    else if (auto *cursor = std::get_if<proto::ChoiceCursor>(&message))
      cursor->player = m_you;
    else if (auto *state = std::get_if<proto::StoryState>(&message))
      state->player = m_you;
    else
      return;
    m_session->Send(m_hostPeer, message);
    return;
  }
  if (std::holds_alternative<proto::PhoneAction>(message) ||
      std::holds_alternative<proto::PhoneReply>(message))
    return; // the host applies its own answers itself
  if (auto *cursor = std::get_if<proto::ChoiceCursor>(&message))
    cursor->player = m_you;
  if (auto *state = std::get_if<proto::StoryState>(&message))
    state->player = m_you;
  if (const auto *call = std::get_if<proto::PhoneCall>(&message)) {
    m_calls.Update(*call);
    message = *m_calls.Call();
  }
  if (to != proto::kNoPlayer) {
    // One guest (a reward): not part of the story a joiner gets.
    for (const auto &[peer, player] : m_playerOfPeer)
      if (player == to)
        m_session->Send(peer, message);
    return;
  }
  if (!std::holds_alternative<proto::QuestReward>(message) &&
      !std::holds_alternative<proto::Subtitle>(message))
    m_story.Note(message);
  m_session->Broadcast(message);
}
void Endpoint::SetLocalTimeEffect(proto::TimeEffect effect) {
  effect = Sanitized(effect);
  effect.player = proto::kNoPlayer;
  if (effect == m_localTime)
    return;
  m_localTime = effect;
  SendLocalTimeNow();
}

void Endpoint::SendLocalTimeNow() {
  if (!IsJoined() || m_ended)
    return;
  auto mine = m_localTime;
  mine.player = m_you;
  // A guest that just joined has something to say only if one is on.
  if (!NoteTime(mine))
    return;
  if (IsHost())
    m_session->Broadcast(mine);
  else
    m_session->Send(m_hostPeer, mine);
}

bool Endpoint::NoteTime(const proto::TimeEffect &effect) {
  const auto it = m_timeEffects.find(effect.player);
  if (effect.abilities == 0) {
    if (it == m_timeEffects.end())
      return false;
    m_timeEffects.erase(it);
    return true;
  }
  if (it != m_timeEffects.end() && it->second == effect)
    return false;
  m_timeEffects[effect.player] = effect;
  return true;
}

void Endpoint::EndTimeOf(proto::PlayerId player, std::vector<SessionEvent> &out, bool broadcast) {
  const proto::TimeEffect none{.player = player};
  if (!NoteTime(none))
    return;
  if (broadcast)
    m_session->Broadcast(none);
  out.push_back(events::RemoteTimeEffect{none});
}

std::optional<net::Stats> Endpoint::HostLinkStats() const {
  return IsHost() ? std::nullopt : m_session->StatsOf(m_hostPeer);
}

std::optional<net::Stats> Endpoint::LinkStatsOf(proto::PlayerId player) const {
  if (player == proto::kNoPlayer || player == m_you)
    return std::nullopt;
  if (!IsHost())
    return player == m_host ? m_session->StatsOf(m_hostPeer) : std::nullopt;
  for (const auto &[peer, id] : m_playerOfPeer)
    if (id == player)
      return m_session->StatsOf(peer);
  return std::nullopt;
}

void Endpoint::Leave(const std::string &reason) {
  if (m_ended)
    return;
  if (IsHost())
    m_session->Broadcast(proto::Bye{reason});
  else if (m_hostPeer)
    m_session->Send(m_hostPeer, proto::Bye{reason});
  m_session->Flush();
  for (auto peer : m_session->Peers())
    m_session->Disconnect(peer);
  m_session->Poll(std::chrono::milliseconds{50});
  m_ended = true;
}
} // namespace tandem
