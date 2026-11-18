#include "tandem/endpoint.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace tandem {
namespace {
constexpr std::uint64_t kPingIntervalUs = 500'000;
// The clock offset follows the lowest-RTT ping, but forgets it slowly so a
// route change or a drifting clock is picked up.
constexpr std::uint64_t kRttForgetUsPerPing = 200;
// How long after our world is rebuilt a guest that has not said it saw it
// has its world requests dropped (proto::Epoch): far longer than a round trip.
constexpr std::uint64_t kFenceUs = 2'000'000;

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

std::uint64_t Us(std::chrono::milliseconds ms) {
  return static_cast<std::uint64_t>(ms.count()) * 1000;
}

// A player's state as last received: false when `value` is what we have
// already (a repeat, a resync of what never changed), so nothing is applied
// twice.
template <class T> bool Keep(std::map<proto::PlayerId, T> &kept, const T &value) {
  const auto it = kept.find(value.player);
  if (it != kept.end() && it->second == value)
    return false;
  kept.insert_or_assign(value.player, value);
  return true;
}

// Guest requests that aim at the host's world as it was: void once it
// moved on to another epoch (proto::Epoch).
bool AimsAtWorld(const proto::Message &message) {
  return std::holds_alternative<proto::NpcHit>(message) ||
         std::holds_alternative<proto::NpcEffect>(message) ||
         std::holds_alternative<proto::NpcHack>(message) ||
         std::holds_alternative<proto::Noise>(message);
}
} // namespace

Endpoint::Endpoint(std::unique_ptr<net::Session> session, Identity identity, Timing timing)
    : m_session(std::move(session)), m_identity(std::move(identity)), m_timing(timing),
      m_checker(Us(timing.resyncCooldown)) {}

std::unique_ptr<Endpoint> Endpoint::Host(Identity identity, std::uint16_t port, std::string &error,
                                         Timing timing, std::optional<net::RelayOptions> relay) {
  auto session = net::Session::Listen(port, kMaxGuests + kSpareConnections, error, timing.timeout,
                                      relay ? &*relay : nullptr);
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
  return Join(std::move(identity), JoinTarget{.address = host, .port = port}, error, timing);
}

std::unique_ptr<Endpoint> Endpoint::Join(Identity identity, const JoinTarget &target,
                                         std::string &error, Timing timing) {
  std::unique_ptr<net::Session> session;
  if (!target.address.empty())
    session =
        net::Session::Connect(target.address, target.port, error, timing.timeout,
                              target.relay ? timing.timeout / 2 : std::chrono::milliseconds{});
  const bool throughRelay = !session && target.relay;
  if (throughRelay)
    session = net::Session::ConnectRelay(*target.relay, error, timing.timeout);
  if (!session) {
    if (error.empty())
      error = "no host address and no relay to join through";
    return nullptr;
  }
  auto endpoint =
      std::unique_ptr<Endpoint>(new Endpoint(std::move(session), std::move(identity), timing));
  endpoint->m_throughRelay = throughRelay;
  if (!throughRelay)
    endpoint->m_fallback = target.relay;
  return endpoint;
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
    if (m_tryRelay)
      break; // the rest is the direct attempt's
  }

  // The host did not answer directly: through the relay, as if nothing
  // happened (the guest's game sees one join that took longer).
  if (m_tryRelay) {
    m_tryRelay = false;
    const auto relay = std::move(*m_fallback);
    m_fallback.reset();
    std::string error;
    if (auto session = net::Session::ConnectRelay(relay, error, m_timing.timeout)) {
      m_session = std::move(session);
      m_hostPeer = 0;
      m_throughRelay = true;
    } else {
      m_ended = true;
      out.push_back(events::Ended{error});
    }
    return out;
  }

  if (!IsHost() && IsJoined()) {
    const auto now = net::NowUs();
    if (now - m_lastPingUs >= kPingIntervalUs) {
      m_lastPingUs = now;
      Send(m_hostPeer, proto::Ping{now});
    }
  }
  if (IsHost() && !m_ended)
    SendDigests();
  return out;
}

// Host: each guest's digest, every `digest` interval, and sooner to a guest
// whose world (or ours) was just rebuilt.
void Endpoint::SendDigests() {
  const auto now = net::NowUs();
  const bool round = now >= m_nextDigestUs;
  if (!round && m_digestSoon.empty())
    return;
  if (round)
    m_nextDigestUs = now + Us(m_timing.digest);
  ++m_digestSeq;
  for (const auto &[peer, player] : m_playerOfPeer)
    if (round || m_digestSoon.contains(player))
      Send(peer, DigestFor(player));
  m_digestSoon.clear();
}

proto::Digest Endpoint::DigestFor(proto::PlayerId self) const {
  return {
      .epoch = IsHost() ? m_epoch : m_hostEpoch,
      .seq = m_digestSeq,
      .world = consistency::WorldDigest(m_world),
      .npcs = consistency::NpcDigest(m_npcs, m_npcHealth),
      .story = m_story.Digest(),
      .players = consistency::PlayersDigest(
          {m_players, m_appearances, m_held, m_seats, m_radios, m_stealth, m_paused, m_timeEffects},
          self),
      .npcCount = static_cast<std::uint16_t>(std::min<std::size_t>(m_npcs.size(), 0xFFFF)),
  };
}

bool Endpoint::Send(net::PeerId peer, const proto::Message &message) {
  switch (m_faults.Decide(message)) {
  case consistency::FaultyLink::Action::Drop:
    return true; // as far as anyone here knows, it went
  case consistency::FaultyLink::Action::Twice:
    m_session->Send(peer, message);
    return m_session->Send(peer, message);
  case consistency::FaultyLink::Action::Send:
    break;
  }
  return m_session->Send(peer, message);
}

void Endpoint::Broadcast(const proto::Message &message, net::PeerId except) {
  if (!m_faults.Active()) {
    m_session->Broadcast(message, except);
    return;
  }
  // Each recipient's own fate.
  for (const auto peer : m_session->Peers())
    if (peer != except)
      Send(peer, message);
}

void Endpoint::BumpEpoch(std::uint8_t reason) {
  if (m_ended)
    return;
  ++m_epoch;
  m_epochAtUs = net::NowUs();
  if (!IsJoined())
    return;
  if (IsHost()) {
    // Every guest is behind until it says it has seen this one.
    Broadcast(proto::Epoch{.player = m_you,
                           .epoch = m_epoch,
                           .seenHost = m_epoch,
                           .reason = reason,
                           .firstNpcSeq = m_npcSeq + 1});
    // A check soon after the world settles.
    m_nextDigestUs = std::min(m_nextDigestUs, net::NowUs() + Us(m_timing.digest) / 2);
  } else {
    Send(
        m_hostPeer,
        proto::Epoch{.player = m_you, .epoch = m_epoch, .seenHost = m_hostEpoch, .reason = reason});
  }
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
    // Aimed at our world before the guest knew it was rebuilt: void. For a
    // moment only: whatever was in flight has arrived within it, and a
    // guest whose answer was lost is not shut out for good.
    if (AimsAtWorld(message) && m_seenHost[player] < m_epoch &&
        net::NowUs() - m_epochAtUs < kFenceUs) {
      ++m_fenced;
      return;
    }

    std::visit(Overloaded{
                   [&](const proto::Ping &ping) {
                     Send(event.peer, proto::Pong{ping.sentUs, HostNowUs()});
                   },
                   [&](const proto::PlayerState &state) {
                     // A guest only speaks for its own V.
                     auto relayed = state;
                     relayed.player = player;
                     Broadcast(relayed, event.peer);
                     out.push_back(events::RemoteState{relayed});
                   },
                   [&](const proto::PoseState &pose) {
                     auto relayed = pose;
                     relayed.player = player;
                     Broadcast(relayed, event.peer);
                     out.push_back(events::RemotePose{relayed});
                   },
                   [&](const proto::Appearance &appearance) {
                     auto relayed = appearance;
                     relayed.player = player;
                     if (!Keep(m_appearances, relayed))
                       return; // a repeat: nothing new for anyone
                     Broadcast(relayed, event.peer);
                     out.push_back(events::RemoteAppearance{relayed});
                   },
                   [&](const proto::Held &held) {
                     auto relayed = held;
                     relayed.player = player;
                     if (!Keep(m_held, relayed))
                       return; // a repeat: nothing new for anyone
                     Broadcast(relayed, event.peer);
                     out.push_back(events::RemoteHeld{relayed});
                   },
                   [&](const proto::Seat &seat) {
                     auto relayed = seat;
                     relayed.player = player;
                     if (!Keep(m_seats, relayed))
                       return; // a repeat: nothing new for anyone
                     Broadcast(relayed, event.peer);
                     out.push_back(events::RemoteSeat{relayed});
                   },
                   [&](const proto::VehicleState &vehicle) {
                     auto relayed = vehicle;
                     relayed.player = player;
                     Broadcast(relayed, event.peer);
                     out.push_back(events::RemoteVehicle{relayed});
                   },
                   [&](const proto::VehicleRadio &radio) {
                     // A guest only speaks for the car it drives.
                     auto relayed = radio;
                     relayed.player = player;
                     if (!Keep(m_radios, relayed))
                       return; // a repeat: nothing new for anyone
                     Broadcast(relayed, event.peer);
                     out.push_back(events::RemoteVehicleRadio{relayed});
                   },
                   [&](const proto::Shot &shot) {
                     auto relayed = shot;
                     relayed.player = player;
                     Broadcast(relayed, event.peer);
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
                       Send(*peer, mine);
                   },
                   [&](const proto::Stealth &stealth) {
                     auto relayed = stealth;
                     relayed.player = player;
                     if (!Keep(m_stealth, relayed))
                       return; // a repeat: nothing new for anyone
                     Broadcast(relayed, event.peer);
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
                     Broadcast(relayed, event.peer);
                     out.push_back(events::PauseChanged{relayed});
                   },
                   [&](const proto::TimeEffect &time) {
                     // A guest only bends time in its own name.
                     auto relayed = Sanitized(time);
                     relayed.player = player;
                     if (!NoteTime(relayed))
                       return;
                     Broadcast(relayed, event.peer);
                     out.push_back(events::RemoteTimeEffect{relayed});
                   },
                   [&](const proto::Bye &) {
                     RemovePlayer(event.peer, out); // gone now, not when ENet is done
                     m_session->Disconnect(event.peer);
                   },
                   [&](const proto::ResyncRequest &request) {
                     // At most one resync per cooldown and guest: a guest that
                     // keeps asking (a bug) cannot flood the link.
                     const auto now = net::NowUs();
                     auto &last = m_resyncAtUs[player];
                     if (last != 0 && now - last < Us(m_timing.resyncCooldown) / 2)
                       return;
                     last = now;
                     const auto domains =
                         static_cast<std::uint8_t>(request.domains & proto::kAllDomains);
                     SendDomainsTo(event.peer, domains, true);
                     out.push_back(events::Resync{player, domains});
                   },
                   [&](const proto::Epoch &epoch) {
                     auto relayed = epoch;
                     relayed.player = player;
                     relayed.firstNpcSeq = 0;
                     auto &seen = m_seenHost[player];
                     seen = std::max(seen, std::min(epoch.seenHost, m_epoch));
                     auto &theirs = m_guestEpoch[player];
                     if (relayed.epoch == theirs)
                       return;
                     theirs = relayed.epoch;
                     Broadcast(relayed, event.peer);
                     if (relayed.reason != proto::kEpochJoin) {
                       out.push_back(events::EpochChanged{relayed});
                       // Its world was rebuilt: check it soon.
                       m_digestSoon.insert(player);
                     }
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
  m_seenHost.erase(player);
  m_guestEpoch.erase(player);
  m_resyncAtUs.erase(player);
  // Gone while in the pause menu: no longer pausing anyone.
  if (NotePaused(player, false)) {
    Broadcast(proto::Pause{player, false});
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
    Send(peer, proto::Refused{"protocol version " + std::to_string(hello.version) +
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
    // A fresh game, born into our world as it is now.
    m_seenHost[player] = m_epoch;
    m_guestEpoch.erase(player);
    Send(peer, proto::Welcome{player, m_host, HostNowUs()});
    out.push_back(events::PlayerReturned{m_players[player]});
    // Back from a crash: a fresh game, not in its pause menu any more.
    if (NotePaused(player, false)) {
      Broadcast(proto::Pause{player, false});
      out.push_back(events::PauseChanged{{player, false}});
    }
    EndTimeOf(player, out, true);
    BroadcastRoster();
    SendSessionTo(peer);
    return;
  }

  if (m_playerOfPeer.size() >= kMaxGuests) {
    Send(peer, proto::Refused{"the session is full"});
    m_session->Disconnect(peer);
    return;
  }

  const auto player = m_nextPlayer++;
  m_playerOfPeer[peer] = player;
  m_seenHost[player] = m_epoch;
  m_players[player] = {player, hello.name, hello.body};
  Send(peer, proto::Welcome{player, m_host, HostNowUs()});
  out.push_back(events::PlayerJoined{m_players[player]});
  BroadcastRoster();
  SendSessionTo(peer);
}

// Everyone already here, as they look now, and the world: what a player
// joining (or coming back) needs.
void Endpoint::SendSessionTo(net::PeerId peer) { SendDomainsTo(peer, proto::kAllDomains, false); }

void Endpoint::SendDomainsTo(net::PeerId peer, std::uint8_t domains, bool resync) {
  const auto self = m_playerOfPeer.find(peer);
  const auto them = self == m_playerOfPeer.end() ? proto::kNoPlayer : self->second;
  // Our world's epoch first: what follows is of it.
  Send(peer, proto::Epoch{.player = m_you,
                          .epoch = m_epoch,
                          .seenHost = m_epoch,
                          .reason = proto::kEpochJoin,
                          .firstNpcSeq = m_npcSeq + 1});
  if (domains & proto::kDomainPlayers) {
    if (resync) {
      proto::Roster roster;
      for (const auto &[id, info] : m_players)
        roster.players.push_back(info);
      Send(peer, roster);
    }
    // Never a guest's own state back: its game is the owner.
    const auto others = [&](const auto &kept) {
      for (const auto &[id, value] : kept)
        if (id != them)
          Send(peer, value);
    };
    others(m_appearances);
    others(m_held);
    others(m_seats);
    others(m_radios);
    others(m_stealth);
    // Who is in their pause menu (the newcomer itself is not), and in a
    // resync who is not (its copy may say otherwise).
    for (const auto paused : m_paused)
      if (paused != them)
        Send(peer, proto::Pause{paused, true});
    // Who is in a Sandevistan (or another time ability) now; in a resync,
    // who is not.
    for (const auto &[id, time] : m_timeEffects)
      if (id != them)
        Send(peer, time);
    if (resync)
      for (const auto &[id, info] : m_players) {
        if (id == them)
          continue;
        if (!m_paused.contains(id))
          Send(peer, proto::Pause{id, false});
        if (!m_timeEffects.contains(id))
          Send(peer, proto::TimeEffect{.player = id});
      }
  }
  if ((domains & proto::kDomainWorld) && m_world)
    Send(peer, *m_world);
  if (domains & proto::kDomainNpcs) {
    if (resync) {
      proto::NpcSet set;
      for (const auto &[npc, spawn] : m_npcs)
        if (set.npcs.size() < proto::kMaxNpcSet)
          set.npcs.push_back(npc);
      Send(peer, set);
    }
    for (const auto &[npc, spawn] : m_npcs)
      Send(peer, spawn);
    for (const auto &[npc, health] : m_npcHealth)
      Send(peer, health);
  }
  // The story so far: journal, tracked entry, call, choice hub, story states.
  if (domains & proto::kDomainStory)
    for (const auto &message : m_story.ForJoiner())
      Send(peer, message);
}

void Endpoint::OnGuestEvent(const net::Event &event, std::vector<SessionEvent> &out) {
  using Kind = net::Event::Kind;
  switch (event.kind) {
  case Kind::Connected:
    m_hostPeer = event.peer;
    Send(m_hostPeer,
         proto::Hello{.name = m_identity.name, .gameVersion = {}, .body = m_identity.body});
    break;
  case Kind::Malformed:
    break; // the host may be newer; Refused will say so
  case Kind::Disconnected:
    if (!IsJoined() && m_fallback) {
      m_tryRelay = true;
      break;
    }
    m_ended = true;
    out.push_back(events::Ended{IsJoined()              ? "the host is gone"
                                : !event.reason.empty() ? event.reason
                                                        : "could not reach the host"});
    break;
  case Kind::Received: {
    const auto &message = *event.message;
    // The bulk channel (the journal snapshot) can overtake the Welcome on
    // the event channel when a packet is lost: kept until we are in, never
    // dropped. States before it are worth nothing.
    if (!IsJoined() && !std::holds_alternative<proto::Welcome>(message) &&
        !std::holds_alternative<proto::Refused>(message) &&
        !std::holds_alternative<proto::Bye>(message)) {
      if (proto::ChannelOf(message) != proto::Channel::State && m_early.size() < 4096)
        m_early.push_back(message);
      break;
    }
    OnGuestMessage(message, out);
    break;
  }
  }
}

void Endpoint::OnGuestMessage(const proto::Message &message, std::vector<SessionEvent> &out) {
  if (proto::IsStory(message)) {
    // Only the host speaks for the story; our own relayed states are ours.
    if (const auto *state = std::get_if<proto::StoryState>(&message);
        state && state->player == m_you)
      return;
    if (const auto *cursor = std::get_if<proto::ChoiceCursor>(&message);
        cursor && cursor->player == m_you)
      return;
    // Our copy of the host's story (digests), and what it already has is
    // not applied again (a resync's snapshot of an unchanged journal).
    // Journal changes always pass: each one may notify.
    const auto *journal = std::get_if<proto::Journal>(&message);
    if (!m_story.Note(message) && !(journal && !journal->snapshot))
      return;
    out.push_back(events::Story{message});
    return;
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
            // What overtook the Welcome, in the order it came.
            for (const auto &early : std::exchange(m_early, {}))
              OnGuestMessage(early, out);
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
              m_clockOffsetUs =
                  static_cast<std::int64_t>(pong.hostTimeUs) - static_cast<std::int64_t>(midpoint);
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
                // Its state goes with it: a namesake joining later starts afresh.
                m_appearances.erase(id);
                m_held.erase(id);
                m_seats.erase(id);
                m_radios.erase(id);
                m_stealth.erase(id);
                m_story.Forget(id);
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
            if (appearance.player != m_you && Keep(m_appearances, appearance))
              out.push_back(events::RemoteAppearance{appearance});
          },
          [&](const proto::Held &held) {
            if (held.player != m_you && Keep(m_held, held))
              out.push_back(events::RemoteHeld{held});
          },
          [&](const proto::Shot &shot) {
            if (shot.player != m_you)
              out.push_back(events::RemoteShot{shot});
          },
          [&](const proto::Seat &seat) {
            if (seat.player != m_you && Keep(m_seats, seat))
              out.push_back(events::RemoteSeat{seat});
          },
          [&](const proto::VehicleState &vehicle) {
            if (vehicle.player != m_you)
              out.push_back(events::RemoteVehicle{vehicle});
          },
          [&](const proto::VehicleRadio &radio) {
            if (radio.player != m_you && Keep(m_radios, radio))
              out.push_back(events::RemoteVehicleRadio{radio});
          },
          [&](const proto::World &world) {
            if (m_world == world)
              return; // a resync of what we have
            m_world = world;
            out.push_back(events::HostWorld{world});
          },
          [&](const proto::NpcSpawn &spawn) {
            // Our copy of what the host shares: a spawn we have already
            // (a resync) changes nothing in the game.
            const bool known = m_npcs.contains(spawn.npc);
            m_npcs[spawn.npc] = spawn;
            if (known)
              return;
            m_corpses.erase(spawn.npc);
            out.push_back(events::HostNpcSpawn{spawn});
          },
          [&](const proto::NpcStates &states) {
            // The old world's states (before the host's last epoch) are void.
            if (m_firstNpcSeq != 0 && static_cast<std::int32_t>(states.seq - m_firstNpcSeq) < 0)
              return;
            // Only NPCs we know: a state that overtook its NpcGone on the
            // other channel must not bring a track back.
            auto live = states;
            std::erase_if(live.npcs, [&](const proto::NpcEntry &entry) {
              return !m_npcs.contains(entry.npc);
            });
            out.push_back(events::HostNpcStates{std::move(live)});
          },
          [&](const proto::NpcGone &gone) {
            if (m_npcs.erase(gone.npc) > 0) {
              m_npcHealth.erase(gone.npc);
              if (gone.reason == 1)
                m_corpses.insert(gone.npc); // stays until the host's corpse goes
              out.push_back(events::HostNpcGone{gone});
            } else if (gone.reason == 0 && m_corpses.erase(gone.npc) > 0) {
              out.push_back(events::HostNpcGone{gone});
            }
            // Else: never shared with us, or said already.
          },
          [&](const proto::NpcHealth &health) {
            if (const auto it = m_npcHealth.find(health.npc);
                it != m_npcHealth.end() && it->second == health)
              return;
            m_npcHealth[health.npc] = health;
            out.push_back(events::HostNpcHealth{health});
          },
          [&](const proto::NpcSet &set) {
            // A resync: every NPC the host shares now. Ours beyond it are gone.
            const std::set<proto::NpcId> shared(set.npcs.begin(), set.npcs.end());
            for (auto it = m_npcs.begin(); it != m_npcs.end();) {
              if (shared.contains(it->first)) {
                ++it;
                continue;
              }
              m_npcHealth.erase(it->first);
              out.push_back(events::HostNpcGone{{.npc = it->first, .reason = 0}});
              it = m_npcs.erase(it);
            }
          },
          [&](const proto::NpcHackState &state) { out.push_back(events::HostNpcHackState{state}); },
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
            if (stealth.player != m_you && Keep(m_stealth, stealth))
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
          [&](const proto::Digest &digest) {
            if (digest.epoch != m_hostEpoch)
              return; // in flight across an epoch: the next one counts
            const auto ask = m_checker.Compare(digest, DigestFor(m_you), net::NowUs());
            if (ask == 0)
              return;
            Send(m_hostPeer, proto::ResyncRequest{ask});
            out.push_back(events::Resync{m_you, ask});
          },
          [&](const proto::Epoch &epoch) {
            if (epoch.player != m_host) {
              // Another guest's world was rebuilt.
              if (epoch.player != m_you && epoch.reason != proto::kEpochJoin)
                out.push_back(events::EpochChanged{epoch});
              return;
            }
            const bool moved = epoch.epoch != m_hostEpoch;
            m_hostEpoch = epoch.epoch;
            m_firstNpcSeq = epoch.firstNpcSeq;
            // Seen: the host takes our requests again.
            Send(m_hostPeer, proto::Epoch{.player = m_you,
                                          .epoch = m_epoch,
                                          .seenHost = m_hostEpoch,
                                          .reason = proto::kEpochJoin});
            if (moved && epoch.reason != proto::kEpochJoin)
              out.push_back(events::EpochChanged{epoch});
          },
          [&](const proto::Bye &bye) {
            m_ended = true;
            out.push_back(events::Ended{bye.reason});
          },
          [](const auto &) {},
      },
      message);
}

void Endpoint::BroadcastRoster() {
  proto::Roster roster;
  for (const auto &[id, info] : m_players)
    roster.players.push_back(info);
  Broadcast(roster);
}

void Endpoint::SendLocalState(proto::PlayerState state) {
  if (!IsJoined() || m_ended)
    return;
  state.player = m_you;
  state.seq = ++m_seq;
  state.timeUs = HostNowUs();
  if (IsHost())
    Broadcast(state);
  else
    Send(m_hostPeer, state);
}

void Endpoint::SendLocalPose(proto::PoseState pose) {
  if (!IsJoined() || m_ended)
    return;
  pose.player = m_you;
  pose.seq = ++m_poseSeq;
  pose.timeUs = HostNowUs();
  if (IsHost())
    Broadcast(pose);
  else
    Send(m_hostPeer, pose);
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
    Broadcast(*m_localAppearance);
  } else {
    Send(m_hostPeer, *m_localAppearance);
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
    Broadcast(*m_localHeld);
  } else {
    Send(m_hostPeer, *m_localHeld);
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
    Broadcast(*m_localSeat);
  } else {
    Send(m_hostPeer, *m_localSeat);
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
    Broadcast(*m_localRadio);
  } else {
    Send(m_hostPeer, *m_localRadio);
  }
}

void Endpoint::SetWorld(proto::World world) {
  if (!IsHost() || m_ended)
    return; // a guest's world is the host's
  world.revision = ++m_worldRevision;
  m_world = world;
  Broadcast(world);
}

void Endpoint::SendNpcSpawn(const proto::NpcSpawn &spawn) {
  if (!IsHost() || m_ended)
    return;
  m_npcs[spawn.npc] = spawn;
  Broadcast(spawn);
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
  Broadcast(states);
}

void Endpoint::SendNpcGone(const proto::NpcGone &gone) {
  if (!IsHost() || m_ended)
    return;
  m_npcs.erase(gone.npc);
  m_npcHealth.erase(gone.npc);
  Broadcast(gone);
}

void Endpoint::SendNpcHealth(const proto::NpcHealth &health) {
  if (!IsHost() || m_ended)
    return;
  m_npcHealth[health.npc] = health;
  Broadcast(health);
}

void Endpoint::SendNpcEffect(const proto::NpcEffect &effect) {
  if (IsHost() || !IsJoined() || m_ended)
    return;
  auto mine = effect;
  mine.player = m_you;
  Send(m_hostPeer, mine);
}

bool Endpoint::SendPlayerHit(const proto::PlayerHit &hit) {
  if (!IsHost() || m_ended || hit.player == m_you)
    return false; // our own V's hits are our game's
  const auto target = std::ranges::find_if(
      m_playerOfPeer, [&](const auto &entry) { return entry.second == hit.player; });
  if (target == m_playerOfPeer.end())
    return false;
  Send(target->first, hit); // that player's game only
  return true;
}

void Endpoint::SendNpcHit(const proto::NpcHit &hit) {
  if (IsHost() || !IsJoined() || m_ended)
    return; // the host's own hits are its game's
  auto mine = hit;
  mine.player = m_you;
  Send(m_hostPeer, mine);
}

void Endpoint::SendNpcHack(const proto::NpcHack &hack) {
  if (IsHost() || !IsJoined() || m_ended)
    return; // the host's own uploads are its game's
  auto mine = hack;
  mine.player = m_you;
  Send(m_hostPeer, mine);
}

void Endpoint::SendNpcHackState(const proto::NpcHackState &state) {
  if (!IsHost() || m_ended)
    return;
  Broadcast(state);
}

void Endpoint::SendNpcStatus(const proto::NpcStatus &status) {
  if (!IsHost() || m_ended)
    return;
  Broadcast(status);
}

void Endpoint::SendPlayerHack(const proto::PlayerHack &hack) {
  if (!IsHost() || m_ended)
    return;
  if (const auto peer = PeerOf(hack.player))
    Send(*peer, hack);
}

void Endpoint::SendVehicleHack(proto::VehicleHack hack) {
  if (!IsJoined() || m_ended || hack.driver == m_you)
    return; // our own car is not a copy: nothing to send
  hack.player = m_you;
  if (!IsHost())
    Send(m_hostPeer, hack);
  else if (const auto peer = PeerOf(hack.driver))
    Send(*peer, hack);
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
    Broadcast(vehicle);
  else
    Send(m_hostPeer, vehicle);
}

void Endpoint::SendLocalShot(proto::Shot shot) {
  if (!IsJoined() || m_ended)
    return;
  shot.player = m_you;
  shot.seq = ++m_shotSeq;
  if (IsHost())
    Broadcast(shot);
  else
    Send(m_hostPeer, shot);
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
    Broadcast(proto::Pause{m_you, m_localPaused});
  else
    Send(m_hostPeer, proto::Pause{m_you, m_localPaused});
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
    Broadcast(*m_localStealth);
  } else {
    Send(m_hostPeer, *m_localStealth);
  }
}

void Endpoint::SendNpcAwareness(proto::NpcAwareness awareness) {
  if (!IsHost() || m_ended)
    return;
  awareness.seq = ++m_awarenessSeq;
  awareness.timeUs = HostNowUs();
  if (awareness.entries.size() > proto::kMaxNpcAwareness)
    awareness.entries.resize(proto::kMaxNpcAwareness);
  Broadcast(awareness);
}

void Endpoint::SendNoise(const proto::Noise &noise) {
  if (IsHost() || !IsJoined() || m_ended)
    return; // the host's own noises are heard by its NPCs already
  auto mine = noise;
  mine.player = m_you;
  Send(m_hostPeer, mine);
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
    Broadcast(call);
  } else if (const auto *reply = std::get_if<proto::PhoneReply>(&message)) {
    auto mine = *reply;
    mine.player = player;
    out.push_back(events::Story{mine});
  } else if (const auto *cursor = std::get_if<proto::ChoiceCursor>(&message)) {
    auto relayed = *cursor;
    relayed.player = player;
    Broadcast(relayed, peer);
    out.push_back(events::Story{relayed});
  } else if (const auto *state = std::get_if<proto::StoryState>(&message)) {
    auto relayed = *state;
    relayed.player = player;
    m_story.Note(relayed);
    Broadcast(relayed, peer);
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
    Send(m_hostPeer, message);
    return;
  }
  if (std::holds_alternative<proto::PhoneAction>(message) ||
      std::holds_alternative<proto::PhoneReply>(message))
    return; // the host applies its own answers itself
  if (auto *cursor = std::get_if<proto::ChoiceCursor>(&message))
    cursor->player = m_you;
  if (auto *state = std::get_if<proto::StoryState>(&message))
    state->player = m_you;
  if (auto *effect = std::get_if<proto::StoryEffect>(&message))
    effect->player = m_you;
  if (const auto *call = std::get_if<proto::PhoneCall>(&message)) {
    m_calls.Update(*call);
    message = *m_calls.Call();
  }
  if (to != proto::kNoPlayer) {
    // One guest (a reward): not part of the story a joiner gets.
    for (const auto &[peer, player] : m_playerOfPeer)
      if (player == to)
        Send(peer, message);
    return;
  }
  if (!std::holds_alternative<proto::QuestReward>(message) &&
      !std::holds_alternative<proto::Subtitle>(message))
    m_story.Note(message);
  Broadcast(message);
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
    Broadcast(mine);
  else
    Send(m_hostPeer, mine);
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
    Broadcast(none);
  out.push_back(events::RemoteTimeEffect{none});
}

std::optional<net::Stats> Endpoint::HostLinkStats() const {
  return IsHost() ? std::nullopt : m_session->StatsOf(m_hostPeer);
}

void Endpoint::Leave(const std::string &reason) {
  if (m_ended)
    return;
  if (IsHost())
    Broadcast(proto::Bye{reason});
  else if (m_hostPeer)
    Send(m_hostPeer, proto::Bye{reason});
  m_session->Flush();
  for (auto peer : m_session->Peers())
    m_session->Disconnect(peer);
  m_session->Poll(std::chrono::milliseconds{50});
  m_ended = true;
}
} // namespace tandem
