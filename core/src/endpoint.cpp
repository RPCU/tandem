#include "tandem/endpoint.hpp"

#include "tandem/activity.hpp"
#include "tandem/compat.hpp"
#include "tandem/elsewhere.hpp"
#include "tandem/names.hpp"
#include "tandem/play.hpp"
#include "tandem/presence.hpp"
#include "tandem/progress.hpp"
#include "tandem/traffic.hpp"

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
// An NpcShotAt and the cosmetic NpcShot of the same shot go out within
// this of each other (one AIWeapon.Fire, one net-thread pass).
constexpr std::uint64_t kShotAtPairUs = 250'000;

// What a game says of its own time ability, made safe to act on: a speed in
// 0.05..1 (a bubble never stops NPCs, nor speeds them up), 1 without a
// bubble ability.
proto::TimeEffect Sanitized(proto::TimeEffect effect) {
  if (!std::isfinite(effect.scale) || (effect.abilities & proto::kTimeBubbleAbilities) == 0)
    effect.scale = 1.f;
  effect.scale = std::clamp(effect.scale, 0.05f, 1.f);
  return effect;
}

// Tests: told of every endpoint made (Endpoint::SetCreationHook).
std::function<void(Endpoint &)> &CreationHook() {
  static std::function<void(Endpoint &)> hook;
  return hook;
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

bool IsTradeRequest(const proto::Message &message) {
  const auto *trade = std::get_if<proto::Trade>(&message);
  return trade && trade->verb == proto::TradeVerb::Request;
}

// Guest requests that aim at the host's world as it was: void once it
// moved on to another epoch (proto::Epoch).
bool AimsAtWorld(const proto::Message &message) {
  return std::holds_alternative<proto::NpcHit>(message) ||
         std::holds_alternative<proto::NpcEffect>(message) ||
         std::holds_alternative<proto::NpcHack>(message) ||
         std::holds_alternative<proto::NpcStrike>(message) ||
         std::holds_alternative<proto::ShotHit>(message) ||
         std::holds_alternative<proto::NpcAction>(message) ||
         std::holds_alternative<proto::TrafficHit>(message) ||
         std::holds_alternative<proto::TrafficClaim>(message) ||
         std::holds_alternative<proto::Defended>(message) ||
         std::holds_alternative<proto::Noise>(message) ||
         std::holds_alternative<proto::VehicleHit>(message) ||
         std::holds_alternative<proto::NpcRam>(message) ||
         // A request to trade with a player beside us, as our world was.
         IsTradeRequest(message);
}
} // namespace

std::int64_t Endpoint::OffsetAt(std::uint64_t nowUs) const {
  const auto gap = m_offsetToUs - m_offsetFromUs;
  if (gap == 0)
    return m_offsetToUs;
  const auto moved =
      static_cast<std::int64_t>(static_cast<double>(nowUs - m_offsetSinceUs) * kClockSlew);
  if (moved >= std::abs(gap))
    return m_offsetToUs;
  return m_offsetFromUs + (gap > 0 ? moved : -moved);
}

std::uint64_t Endpoint::HostNowUs() const {
  const auto now = net::NowUs();
  return static_cast<std::uint64_t>(static_cast<std::int64_t>(now) + OffsetAt(now));
}

void Endpoint::SetClockOffset(std::int64_t offsetUs, bool jump) {
  const auto now = net::NowUs();
  const auto current = OffsetAt(now);
  if (jump || std::abs(offsetUs - current) > kClockJumpUs)
    m_offsetFromUs = offsetUs;
  else
    m_offsetFromUs = current;
  m_offsetToUs = offsetUs;
  m_offsetSinceUs = now;
}

float Endpoint::NearestOther() const {
  if (!m_ownPosition)
    return netcode::kNearest;
  float nearest = -1.f;
  for (const auto &[player, at] : m_positions) {
    if (player == m_you)
      continue;
    const float d =
        std::hypot(at.x - m_ownPosition->x, at.y - m_ownPosition->y, at.z - m_ownPosition->z);
    if (nearest < 0.f || d < nearest)
      nearest = d;
  }
  return nearest < 0.f ? netcode::kNearest : nearest;
}

void Endpoint::NotePosition(const proto::PlayerState &state) {
  if (state.player != proto::kNoPlayer && state.player != m_you)
    m_positions[state.player] = state.position;
}

void Endpoint::ForgetStreams(proto::PlayerId player) {
  m_positions.erase(player);
  m_poseDecoders.erase(player);
  m_relayEncoders.erase(player);
}

Endpoint::NetCounters Endpoint::Counters() const {
  NetCounters counters{
      .poseKeys = m_poseEncoder.Keys(),
      .poseFrames = m_poseEncoder.Frames(),
      .poseSkipped = m_poseSkipped,
      .statesHeld = m_stateGate.Held() + m_vehicleGate.Held(),
      .statesSkipped = m_statesSkipped,
      .npcMotions = m_npcMotions,
      .npcWhole = m_npcWhole,
  };
  for (const auto &[player, decoder] : m_poseDecoders) {
    counters.poseOtherKey += decoder.OtherKey();
    counters.poseDropped += decoder.Dropped();
  }
  return counters;
}

// A pose frame from `from` (host: a guest's, stamped; guest: anyone's, from
// the host): back to a whole pose for our game, and on to the others.
void Endpoint::OnRemotePoseFrame(proto::PoseFrame frame, net::PeerId from,
                                 std::vector<SessionEvent> &out) {
  if (frame.player == m_you || frame.player == proto::kNoPlayer)
    return;
  const auto pose = m_poseDecoders[frame.player].Decode(frame);
  if (!pose)
    return;
  if (!IsHost() && m_receivedTap)
    m_receivedTap(*pose);
  if (IsHost() && m_playerOfPeer.size() > 1)
    Broadcast(m_relayEncoders[frame.player].Encode(*pose), from);
  out.push_back(events::RemotePose{*pose});
}

// Guest: the host's NPC states, whole (from NpcStates, or NpcMotion filled
// in by our book).
void Endpoint::OnHostNpcStates(proto::NpcStates states, std::vector<SessionEvent> &out) {
  if (m_receivedTap)
    m_receivedTap(states);
  // Only NPCs we know: a state that overtook its NpcGone on the other
  // channel must not bring a track back.
  std::erase_if(states.npcs,
                [&](const proto::NpcEntry &entry) { return !m_npcs.contains(entry.npc); });
  out.push_back(events::HostNpcStates{std::move(states)});
}

Endpoint::Endpoint(std::unique_ptr<net::Session> session, Identity identity, Timing timing)
    : m_session(std::move(session)), m_identity(std::move(identity)), m_timing(timing),
      m_checker(Us(timing.resyncCooldown)), m_deviceCheck(Us(timing.resyncCooldown)) {
  // Every game says at least which build it is.
  if (m_identity.install.build.empty())
    m_identity.install.build = compat::Build();
  m_skips.SetAnswerTime(Us(timing.skipAnswer));
}

std::unique_ptr<Endpoint> Endpoint::Host(Identity identity, std::uint16_t port, std::string &error,
                                         Timing timing, std::optional<net::RelayOptions> relay) {
  auto session = net::Session::Listen(port, kMaxGuests + kSpareConnections, error, timing.timeout,
                                      relay ? &*relay : nullptr);
  if (!session)
    return nullptr;
  auto endpoint =
      std::unique_ptr<Endpoint>(new Endpoint(std::move(session), std::move(identity), timing));
  endpoint->m_you = endpoint->m_host = 1;
  endpoint->m_identity.name = names::Sanitize(endpoint->m_identity.name);
  endpoint->m_askedNames[1] = endpoint->m_identity.name;
  endpoint->m_players[1] = {1, endpoint->m_identity.name, endpoint->m_identity.body};
  if (CreationHook())
    CreationHook()(*endpoint);
  return endpoint;
}

void Endpoint::SetCreationHook(std::function<void(Endpoint &)> hook) {
  CreationHook() = std::move(hook);
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
  if (CreationHook())
    CreationHook()(*endpoint);
  return endpoint;
}

template <class M>
bool Endpoint::Deliver(const M &message, proto::PlayerId to, net::PeerId except) {
  if (to == proto::kNoPlayer) {
    Broadcast(message, except);
    return true;
  }
  if (to == m_you)
    return true;
  for (const auto &[peer, player] : m_playerOfPeer)
    if (player == to && peer != except)
      Send(peer, message);
  return false;
}

std::vector<SessionEvent> Endpoint::Update(std::chrono::milliseconds wait) {
  std::vector<SessionEvent> out = std::exchange(m_pending, {});
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

  // Host: a time skip request that something now blocks, or that nobody
  // answered in time.
  if (IsHost() && m_skips.Open()) {
    if (const auto ended = m_skips.Block(SkipBlocker()))
      EndTimeSkip(*ended, true, out);
    else if (const auto lapsed = m_skips.Tick(net::NowUs()))
      EndTimeSkip(*lapsed, true, out);
  }

  if (!IsHost() && IsJoined()) {
    const auto now = net::NowUs();
    if (now - m_lastPingUs >= kPingIntervalUs) {
      m_lastPingUs = now;
      Send(m_hostPeer, proto::Ping{now});
    }
  }
  if (IsHost() && !m_ended) {
    SendDigests();
    Arbitrate(out);
    // Requests that lapsed, commits to say again.
    RouteTrade(m_trades.Tick(net::NowUs()), out);
  }
  if (IsJoined() && !m_ended)
    UpdateAreas();
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
  const auto devices = DeviceDigestNow();
  for (const auto &[peer, player] : m_playerOfPeer)
    if (round || m_digestSoon.contains(player)) {
      Send(peer, DigestFor(player));
      Send(peer, devices);
      // ...and the NPCs that guest runs, as our record has them.
      proto::OwnedDigest owned{.seq = m_digestSeq};
      owned.npcs = OwnedHash(player, owned.count);
      Send(peer, owned);
    }
  m_digestSoon.clear();
  // A request our game never answered with a report (it has no such
  // device): forgotten, the next change of the device speaks for itself.
  std::erase_if(m_deviceUses, [&](const auto &entry) {
    return now - entry.second.atUs > 2 * Us(m_timing.resyncCooldown);
  });
}

proto::DeviceDigest Endpoint::DeviceDigestNow() const {
  return {.count = static_cast<std::uint32_t>(m_devices.Size()), .hash = m_devices.Digest()};
}

std::uint32_t Endpoint::WorldDigestNow() const {
  const auto world = consistency::WorldDigest(m_world, m_difficulty, m_rules);
  if (m_deaths.Empty() && m_fractureRecord.Empty())
    return world;
  consistency::Hasher hash;
  hash.Add(std::uint64_t{world});
  if (!m_deaths.Empty())
    hash.Add(std::uint64_t{m_deaths.Digest()});
  if (!m_fractureRecord.Empty())
    hash.Add(std::uint64_t{m_fractureRecord.Digest()});
  return hash.Value();
}

proto::Digest Endpoint::DigestFor(proto::PlayerId self) const {
  // The NPCs `self` shows as proxies: those of the owners it is linked with
  // (a guest's record holds only those, and its own).
  const bool host = IsHost();
  std::uint16_t count = 0;
  const auto npcs = consistency::NpcDigest(
      m_npcs, m_npcHealth, m_npcHolds, m_npcOwner,
      [&](proto::NpcId, proto::PlayerId owner) {
        return owner != self && (!host || m_links.Linked(owner, self));
      },
      &count, &m_npcScale);
  return {
      .epoch = host ? m_epoch : m_hostEpoch,
      .seq = m_digestSeq,
      // The world, the dead of the session (the ledger, regions.md §14) and
      // what broke in it (devices.md, "Glass and breakable scenery").
      .world = WorldDigestNow(),
      // One domain: the shared vehicles are resynced with the NPCs.
      .npcs = consistency::Hasher{}
                  .Add(std::uint64_t{npcs})
                  .Add(std::uint64_t{TrafficDigestFor(self)})
                  .Add(std::uint64_t{VehicleStreetDigest(self)})
                  .Value(),
      .story = m_story.Digest(),
      .players = consistency::Hasher{}
                     .Add(std::uint64_t{consistency::PlayersDigest(
                         {m_players, m_appearances, m_held, m_seats, m_radios, m_stealth, m_paused,
                          m_timeEffects, m_lives, m_elsewheres, m_heat, m_activities, &m_commsLinks,
                          &m_waypoints, &m_regionsSent},
                         self)})
                     .Add(std::uint64_t{VehiclePlayersDigest(self)})
                     .Value(),
      .npcCount = count,
  };
}

bool Endpoint::Send(net::PeerId peer, const proto::Message &message) {
  if (m_wireTap) {
    proto::PlayerId to = m_host;
    if (IsHost()) {
      const auto known = m_playerOfPeer.find(peer);
      to = known == m_playerOfPeer.end() ? proto::kNoPlayer : known->second;
    }
    m_wireTap(Wire::Out, to, message);
  }
  return Transmit(peer, message);
}

bool Endpoint::Transmit(net::PeerId peer, const proto::Message &message) {
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
  if (m_wireTap)
    m_wireTap(Wire::Out, proto::kNoPlayer, message);
  // Only to the players in the session: a connection that has not said
  // Hello yet hears nothing until its Welcome (the referee caught a late
  // joiner hearing of player 2's epoch before the roster that brings it).
  if (IsHost()) {
    for (const auto &[peer, player] : m_playerOfPeer)
      if (peer != except)
        Transmit(peer, message);
    return;
  }
  if (!m_faults.Active()) {
    m_session->Broadcast(message, except);
    return;
  }
  // Each recipient's own fate.
  for (const auto peer : m_session->Peers())
    if (peer != except)
      Transmit(peer, message);
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
    // Our NPCs are not in our world any more: the players we leave behind
    // inherit the ones near them (regions), the rest go.
    if (reason != proto::kEpochJoin)
      InheritTrafficOf(m_you, false, m_pending);
    if (reason != proto::kEpochJoin)
      InheritNpcsOf(m_you, false, m_pending);
    // Our V is no longer beside whoever it traded with.
    if (reason != proto::kEpochJoin)
      RouteTrade(m_trades.Drop(m_you, proto::TradeReason::Away), m_pending);
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
    // A Hello of another protocol version whose layout we cannot read: its
    // version is all we know. Refused in words, like one we can read.
    if (event.helloVersion && *event.helloVersion != proto::kProtocolVersion &&
        !m_playerOfPeer.contains(event.peer)) {
      Send(event.peer, proto::Refused{compat::RefusalForGuest(*event.helloVersion)});
      out.push_back(events::JoinRefused{{}, compat::RefusalForHost({}, *event.helloVersion, {})});
    }
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
    if (m_wireTap)
      m_wireTap(Wire::In, player, message);
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
    if (proto::IsCombat(message)) {
      OnGuestCombat(player, message, out);
      return;
    }
    if (proto::IsTrade(message)) {
      // Its seat's word, in its own name; a commit is never a guest's.
      if (const auto *trade = std::get_if<proto::Trade>(&message))
        RouteTrade(m_trades.On(player, *trade, net::NowUs(), Present()), out);
      else if (const auto *offer = std::get_if<proto::TradeOffer>(&message))
        RouteTrade(m_trades.On(player, *offer, net::NowUs()), out);
      return;
    }

    // Its vehicles and its requests to other owners (traffic.md).
    if (OnGuestTraffic(event.peer, player, message, out))
      return;
    // Its NPCs, and its requests to other owners (regions).
    if (OnOwnerMessage(event.peer, player, message, out))
      return;
    if (proto::IsVehicleCombat(message)) {
      OnGuestVehicles(event.peer, player, message, out);
      return;
    }

    std::visit(
        Overloaded{
            [&](const proto::Ping &ping) {
              Send(event.peer, proto::Pong{ping.sentUs, HostNowUs()});
            },
            [&](const proto::PlayerState &state) {
              // A guest only speaks for its own V.
              auto relayed = state;
              relayed.player = player;
              m_places[player] = {.position = relayed.position, .known = true};
              NotePosition(relayed);
              Broadcast(m_compact ? proto::Message(netcode::Compact(relayed))
                                  : proto::Message(relayed),
                        event.peer);
              out.push_back(events::RemoteState{relayed});
            },
            [&](const proto::PlayerMotion &motion) {
              auto relayed = netcode::Whole(motion);
              relayed.player = player;
              m_places[player] = {.position = relayed.position, .known = true};
              NotePosition(relayed);
              Broadcast(netcode::Compact(relayed), event.peer);
              out.push_back(events::RemoteState{relayed});
            },
            [&](const proto::PoseState &pose) {
              auto relayed = pose;
              relayed.player = player;
              Broadcast(relayed, event.peer);
              out.push_back(events::RemotePose{relayed});
            },
            [&](const proto::PoseFrame &frame) {
              auto stamped = frame;
              stamped.player = player;
              OnRemotePoseFrame(std::move(stamped), event.peer, out);
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
              Broadcast(m_compact ? proto::Message(netcode::Compact(relayed))
                                  : proto::Message(relayed),
                        event.peer);
              out.push_back(events::RemoteVehicle{relayed});
            },
            [&](const proto::VehicleMotion &motion) {
              auto relayed = netcode::Whole(motion);
              relayed.player = player;
              Broadcast(netcode::Compact(relayed), event.peer);
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
            [&](const proto::Presence &presence) {
              // A guest only speaks for its own V.
              auto relayed = presence;
              relayed.player = player;
              if (!Keep(m_presences, relayed))
                return;
              Broadcast(relayed, event.peer);
              out.push_back(events::RemotePresence{relayed});
            },
            [&](const proto::Cue &cue) {
              auto relayed = cue;
              relayed.player = player;
              Broadcast(relayed, event.peer);
              out.push_back(events::RemoteCue{relayed});
            },
            [&](const proto::StatusVisuals &visuals) {
              // A guest only speaks for its own V.
              auto relayed = visuals;
              relayed.player = player;
              if (!Keep(m_statusVisuals, relayed))
                return;
              Broadcast(relayed, event.peer);
              out.push_back(events::RemoteStatusVisuals{relayed});
            },
            [&](const proto::Activity &busy) {
              // A guest is only busy in its own name.
              auto relayed = activity::Sanitized(busy);
              relayed.player = player;
              if (!NoteActivity(relayed))
                return;
              Broadcast(relayed, event.peer);
              out.push_back(events::RemoteActivity{relayed});
            },
            [&](const proto::TimeSkip &skip) {
              // A guest only asks in its own name.
              auto asked = skip;
              asked.from = player;
              OnTimeSkip(asked, out);
            },
            [&](const proto::TimeSkipVote &vote) {
              auto mine = vote;
              mine.from = player;
              OnTimeSkipVote(mine, out);
            },
            [&](const proto::Install &install) {
              // The guest's game against ours: said, never refused.
              const auto &name = m_players[player].name;
              out.push_back(events::RemoteInstall{
                  player, name, install, compat::Differences(m_identity.install, install, name)});
            },
            [&](const proto::ModList &list) { OnModList(player, list, out); },
            [&](const proto::Substitutes &substitutes) { OnSubstitutes(player, substitutes, out); },
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
            [&](const proto::Elsewhere &away) {
              // A guest only goes elsewhere in its own name.
              auto relayed = elsewhere::Sanitized(away);
              relayed.player = player;
              if (!NoteElsewhere(relayed))
                return;
              Broadcast(relayed, event.peer);
              out.push_back(events::RemoteElsewhere{relayed});
            },
            [&](const proto::Regroup &offer) {
              // A guest only offers to come to itself.
              auto relayed = offer;
              relayed.from = player;
              if (relayed.to != player && Deliver(relayed, relayed.to, event.peer))
                out.push_back(events::RegroupOffered{relayed});
            },
            [&](const proto::RegroupAnswer &answer) {
              auto relayed = answer;
              relayed.from = player;
              if (relayed.to != player && relayed.to != proto::kNoPlayer &&
                  Deliver(relayed, relayed.to, event.peer))
                out.push_back(events::RegroupAnswered{relayed});
            },
            [&](const proto::WorldItem &item) { AddWorldItem(player, item, out); },
            [&](const proto::WorldItemTaken &taken) {
              // A claim, in the guest's own name only.
              ClaimWorldItem(player, taken.id, taken.stack, event.peer, out);
            },
            [&](const proto::Vitals &vitals) {
              // A guest only speaks for its own V.
              auto relayed = vitals;
              relayed.player = player;
              m_lives[player] = relayed;
              Broadcast(relayed, event.peer);
              out.push_back(events::RemoteVitals{relayed});
              // Down or out: not at a trade table any more.
              if (relayed.life != proto::Life::Up)
                RouteTrade(m_trades.Drop(player, proto::TradeReason::Down), out);
              CheckLost(out);
            },
            [&](const proto::Revive &revive) {
              // The downed V's game decides; only a V that is down
              // can be revived (a cancel always passes).
              auto mine = revive;
              mine.player = player;
              if (mine.target == player || (mine.step != proto::ReviveStep::Cancel &&
                                            LifeOf(mine.target) != proto::Life::Down))
                return;
              if (mine.target == m_you)
                out.push_back(events::ReviveBy{mine});
              else if (const auto peer = PeerOf(mine.target))
                Send(*peer, mine);
            },
            [&](const proto::Transit &transit) {
              // A guest only rides in its own name.
              auto relayed = transit;
              relayed.player = player;
              NoteTransit(relayed);
              Broadcast(relayed, event.peer);
              out.push_back(events::RemoteTransit{relayed});
            },
            [&](const proto::Heat &heat) {
              // A guest is only wanted in its own name.
              auto relayed = heat;
              relayed.player = player;
              if (!NoteHeat(relayed))
                return; // a repeat: nothing new for anyone
              Broadcast(relayed, event.peer);
              out.push_back(events::RemoteHeat{relayed});
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
              const auto domains = static_cast<std::uint8_t>(request.domains & proto::kAllDomains);
              SendDomainsTo(event.peer, domains, true);
              out.push_back(events::Resync{player, domains});
            },
            [&](const proto::DeviceState &ask) {
              // A guest asks what a device is (it came into its
              // world). Nothing else of a guest's is a device's state.
              if (ask.how != proto::kDeviceAsk)
                return;
              if (const auto *record = m_devices.Find(devices::KeyOf(ask))) {
                if (devices::WhatOf(*record) != devices::WhatOf(ask)) {
                  auto answer = *record;
                  answer.how = proto::kDeviceQuiet;
                  Send(event.peer, answer);
                }
                return;
              }
              auto mine = ask;
              mine.by = player;
              out.push_back(events::DeviceAsked{mine});
            },
            [&](const proto::DeviceUse &use) { OnDeviceUse(event.peer, player, use, out); },
            [&](const proto::Breach &breach) { OnBreach(event.peer, player, breach, out); },
            [&](const proto::Fracture &fracture) {
              // A guest only speaks for what broke in its own game.
              auto relayed = fracture;
              relayed.player = player;
              auto &rate = m_fractureRate
                               .try_emplace(player, devices::kHostFractureBurst,
                                            devices::kHostFracturesPerSecond)
                               .first->second;
              if (!rate.Allow(net::NowUs())) {
                ++m_fracturesDropped;
                return;
              }
              RelayFracture(relayed, event.peer, &out);
            },
            [&](const proto::DeviceFire &fire) { OnDeviceSeen(player, fire, out); },
            [&](const proto::DeviceSet &) {
              // At most one per cooldown and guest, as for ResyncRequest.
              const auto now = net::NowUs();
              auto &last = m_deviceSetAtUs[player];
              if (last != 0 && now - last < Us(m_timing.resyncCooldown) / 2)
                return;
              last = now;
              SendDevicesTo(event.peer, true);
              out.push_back(events::Resync{player, proto::kDomainDevices});
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
                // Its world was rebuilt: its NPCs are not in it any
                // more (regions: heirs, or gone).
                InheritTrafficOf(player, false, out);
                InheritNpcsOf(player, false, out);
                out.push_back(events::EpochChanged{relayed});
                // Its world was rebuilt: check it soon.
                m_digestSoon.insert(player);
                // ...and it is no longer beside whoever it traded with.
                RouteTrade(m_trades.Drop(player, proto::TradeReason::Away), out);
              }
            },
            [&](const proto::Mark &) { OnGuestComms(event.peer, player, message, out); },
            [&](const proto::Tag &) { OnGuestComms(event.peer, player, message, out); },
            [&](const proto::Link &) { OnGuestComms(event.peer, player, message, out); },
            [&](const proto::Waypoint &) { OnGuestComms(event.peer, player, message, out); },
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
  // The NPCs it ran: to the players near them, or gone (regions).
  InheritTrafficOf(player, true, out);
  InheritNpcsOf(player, true, out);
  // It holds no region any more.
  if (m_holders.erase(player) > 0)
    Broadcast(proto::RegionHold{.player = player, .held = false});
  m_links.Forget(player);
  m_places.erase(player);
  m_players.erase(player);
  m_askedNames.erase(player);
  m_appearances.erase(player);
  m_held.erase(player);
  m_seats.erase(player);
  // What it held is free again for the others (the host's game lets go of
  // the real NPC itself when the puppet goes).
  for (auto it = m_npcHolds.begin(); it != m_npcHolds.end();) {
    if (it->second.player == player) {
      Broadcast(proto::NpcHold{.npc = it->first});
      it = m_npcHolds.erase(it);
    } else {
      ++it;
    }
  }
  m_radios.erase(player);
  m_stealth.erase(player);
  m_presences.erase(player);
  m_statusVisuals.erase(player);
  m_transits.erase(player);
  // Our NPCs seated in its vehicle: out (its copy stays parked elsewhere).
  for (auto it = m_npcSeats.begin(); it != m_npcSeats.end();) {
    if (it->second.owner != player) {
      ++it;
      continue;
    }
    Broadcast(proto::NpcSeat{.npc = it->first});
    it = m_npcSeats.erase(it);
  }
  m_story.Forget(player);
  // Its quest interactions with our game are void (what our game already
  // did stays done, as for a device); the objects are free for the others.
  m_interactReferee.Forget(player);
  ForgetStreams(player);
  m_seenHost.erase(player);
  m_guestEpoch.erase(player);
  m_resyncAtUs.erase(player);
  // What it changed stays changed (a device is the world's, not a
  // player's); only its requests still waiting for our game go.
  m_deviceSetAtUs.erase(player);
  std::erase_if(m_deviceUses, [&](const auto &entry) { return entry.second.player == player; });
  // The turrets that had its V have nobody there any more.
  ForgetDevicesSeeing(player, out);
  m_fractureRate.erase(player);
  // Gone while in the pause menu: no longer pausing anyone.
  if (NotePaused(player, false)) {
    Broadcast(proto::Pause{player, false});
    out.push_back(events::PauseChanged{{player, false}});
  }
  // Gone in a Sandevistan: its bubble ends for everyone.
  EndTimeOf(player, out, true);
  // Gone from another world: back in the city for everyone (nothing of it
  // stays hidden anywhere).
  EndElsewhereOf(player, out, true);
  // Gone while wanted: the police have nobody to chase.
  EndHeatOf(player, out, true);
  // Gone while busy: its puppet is free again everywhere; its time skip
  // request is withdrawn, its say in another's is gone.
  EndActivityOf(player, out, true);
  if (const auto ended = m_skips.Left(player))
    EndTimeSkip(*ended, true, out);
  m_lives.erase(player);
  // Its cars are no one's to lend any more.
  m_lends.erase(player);
  // The session's level is the mean of those who are left.
  m_levels.erase(player);
  UpdateSessionLevel(out);
  // Its trades close for the other side (one already decided stands).
  RouteTrade(m_trades.Drop(player, proto::TradeReason::Gone), out);
  // Its link and waypoint go with it (the others' games drop its marks,
  // tags and waypoint on its PlayerLeft).
  ForgetComms(player);
  // Its car, its gunner seats, its weapons in a vehicle, the chases after it.
  ForgetVehiclesOf(player, out, true);
  // It may have been reviving someone (it cannot say it stopped: a player
  // can vanish at any moment): every downed V hears its revive is off.
  for (const auto &[id, vitals] : m_lives) {
    if (vitals.life != proto::Life::Down)
      continue;
    const proto::Revive off{.player = player, .target = id, .step = proto::ReviveStep::Cancel};
    if (id == m_you)
      out.push_back(events::ReviveBy{off});
    else if (const auto to = PeerOf(id))
      Send(*to, off);
  }
  out.push_back(events::PlayerLeft{player});
  BroadcastRoster();
  // The last V up left: whoever is left down has nobody to revive them.
  CheckLost(out);
}

void Endpoint::OnHello(net::PeerId peer, const proto::Hello &hello,
                       std::vector<SessionEvent> &out) {
  if (hello.magic != proto::kMagic) {
    Send(peer, proto::Refused{"not a tandem game"});
    m_session->Disconnect(peer);
    return;
  }
  if (hello.version != proto::kProtocolVersion) {
    // Both players read why, in words: the guest with the refusal, ours
    // from the event (the guest's name and build came with its Hello).
    Send(peer, proto::Refused{compat::RefusalForGuest(hello.version)});
    m_session->Disconnect(peer);
    out.push_back(events::JoinRefused{
        hello.name, compat::RefusalForHost(hello.name, hello.version, hello.gameVersion)});
    return;
  }
  // Phantom Liberty on every game: the records and the Relic tree a V
  // brings must exist where it is shown. Ours first: we take nobody.
  if (!m_identity.expansion || !(hello.flags & proto::kHelloExpansion)) {
    const bool ours = !m_identity.expansion;
    Send(peer, proto::Refused{
                   std::string(ours ? compat::kExpansionHostLacks : compat::kExpansionGuestLacks)});
    m_session->Disconnect(peer);
    out.push_back(
        events::JoinRefused{hello.name, compat::ExpansionRefusalForHost(hello.name, ours)});
    return;
  }

  // A player back on a new connection (its game crashed and restarted, or
  // its link dropped) while the old one has not timed out yet: if the old one
  // has gone silent, the new one takes its place, with its id, puppet and
  // seat. A live player keeps its place: a namesake joins as someone else.
  // Matched by the name it asked for: the session may show it as "V (2)".
  const auto asked = names::Sanitize(hello.name);
  const auto silent = std::ranges::find_if(m_playerOfPeer, [&](const auto &entry) {
    if (const auto it = m_askedNames.find(entry.second);
        it == m_askedNames.end() || it->second != asked)
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
    // A fresh game, born into our world as it is now: the NPCs its old one
    // ran are not in it, and where it is we do not know yet.
    InheritTrafficOf(player, true, out);
    InheritNpcsOf(player, true, out);
    m_places[player] = {};
    ForgetStreams(player);
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
    // ...in its body, in the city, until it says otherwise...
    EndElsewhereOf(player, out, true);
    // ...not wanted (its game says so again if it still is)...
    EndHeatOf(player, out, true);
    // ...doing nothing, with no time skip asked or owed...
    EndActivityOf(player, out, true);
    if (const auto ended = m_skips.Left(player))
      EndTimeSkip(*ended, true, out);
    // ...with no waypoint and no link until its new game says so...
    EndCommsOf(player, peer, out);
    // ...seen by no turret (its copies of them start over)...
    ForgetDevicesSeeing(player, out);
    // ...in no gunner seat, its car a new one...
    ForgetVehiclesOf(player, out, true, true);
    // ...and its V is up until it says otherwise.
    if (LifeOf(player) != proto::Life::Up) {
      const proto::Vitals up{.player = player, .revision = m_lives[player].revision + 1};
      m_lives.erase(player);
      Broadcast(up, peer);
      out.push_back(events::RemoteVitals{up});
      CheckLost(out);
    }
    BroadcastRoster();
    Send(peer, m_identity.install);
    SendModsTo(peer);
    SendSessionTo(peer);
    return;
  }

  const auto player = m_playerOfPeer.size() < kMaxGuests ? NextFreePlayer() : proto::kNoPlayer;
  if (player == proto::kNoPlayer) {
    Send(peer, proto::Refused{std::string(kSessionFull)});
    m_session->Disconnect(peer);
    return;
  }

  m_playerOfPeer[peer] = player;
  m_seenHost[player] = m_epoch;
  // One name per player: a namesake is shown as "V (2)", "V (3)"...
  std::vector<std::string> taken;
  for (const auto &[id, info] : m_players)
    taken.push_back(info.name);
  m_askedNames[player] = asked;
  m_players[player] = {player, names::Unique(asked, taken), hello.body};
  Send(peer, proto::Welcome{player, m_host, HostNowUs()});
  out.push_back(events::PlayerJoined{m_players[player]});
  BroadcastRoster();
  // After the roster: the guest names us when it says what differs.
  Send(peer, m_identity.install);
  SendModsTo(peer);
  SendSessionTo(peer);
  CheckLost(out); // a V up again
}

proto::PlayerId Endpoint::NextFreePlayer() {
  for (proto::PlayerId tries = 1; tries < proto::kMaxPlayerId; ++tries) {
    const auto id = m_nextPlayer;
    m_nextPlayer = m_nextPlayer >= proto::kMaxPlayerId ? 2 : m_nextPlayer + 1;
    if (id != m_you && !m_players.contains(id))
      return id;
  }
  return proto::kNoPlayer;
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
    // Who is linked with whom (regions), and who holds its region (in a
    // resync, who does not).
    SendRegionsTo(peer);
    for (const auto &[id, info] : m_players)
      if (id != them && (resync || m_holders.contains(id)))
        Send(peer, proto::RegionHold{.player = id, .held = m_holders.contains(id)});
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
    others(m_presences);
    others(m_statusVisuals); // what shows on each V (an empty set too)
    others(m_transits);      // who rides what (only rides that are on)
    others(m_heat);          // who is wanted (only levels above 0)
    others(m_lends);         // who may drive whose cars
    others(m_commsLinks);    // whom each shares tags with
    others(m_waypoints);
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
    // Who is in another world now; in a resync, who is in the city.
    for (const auto &[id, away] : m_elsewheres)
      if (id != them)
        Send(peer, away);
    // What everyone is busy with; in a resync, who is not.
    for (const auto &[id, busy] : m_activities)
      if (id != them)
        Send(peer, busy);
    if (resync)
      for (const auto &[id, info] : m_players) {
        if (id == them)
          continue;
        if (!m_paused.contains(id))
          Send(peer, proto::Pause{id, false});
        if (!m_timeEffects.contains(id))
          Send(peer, proto::TimeEffect{.player = id});
        if (!m_elsewheres.contains(id))
          Send(peer, proto::Elsewhere{.player = id});
        if (!m_activities.contains(id))
          Send(peer, proto::Activity{.player = id});
        if (LifeOf(id) == proto::Life::Up)
          Send(peer, proto::Vitals{.player = id});
        if (!m_transits.contains(id))
          Send(peer, proto::Transit{.player = id});
        if (!m_heat.contains(id))
          Send(peer, proto::Heat{.player = id});
        if (!m_commsLinks.contains(id))
          Send(peer, proto::Link{.player = id});
        if (!m_waypoints.contains(id))
          Send(peer, proto::Waypoint{.player = id});
      }
    // Who is down or out (the newcomer's own is its game's).
    for (const auto &[id, vitals] : m_lives)
      if (id != them && vitals.life != proto::Life::Up)
        Send(peer, vitals);
  }
  if (domains & proto::kDomainWorld) {
    if (m_world)
      Send(peer, *m_world);
    if (m_difficulty)
      Send(peer, *m_difficulty);
    if (m_rules)
      Send(peer, *m_rules);
    if (m_sessionLevel)
      Send(peer, *m_sessionLevel);
    // The placed NPCs that died in the session, wherever they were: the
    // same dead stay dead for a late joiner, and a resync brings back one
    // that was lost.
    for (const auto &death : m_deaths.InOrder())
      Send(peer, death);
    // What broke in the session: broken for who was not there.
    for (const auto &fracture : m_fractureRecord.InOrder())
      Send(peer, fracture);
    // What lies in the world (its taken stacks at quantity 0). Loot is not
    // in the digests: the host arbitrates each claim (ClaimWorldItem).
    if (!resync)
      for (const auto &[id, item] : m_worldItems)
        Send(peer, item);
  }
  // The devices as the session has them (a resync of them is asked for
  // apart, DeviceSet).
  if ((domains & proto::kDomainWorld) && !resync)
    SendDevicesTo(peer, false);
  if (domains & proto::kDomainNpcs) {
    // The NPCs this guest shows: those of the owners it is linked with.
    std::vector<proto::NpcId> shown;
    for (const auto &[npc, spawn] : m_npcs)
      if (Sees(them, NpcOwner(npc)))
        shown.push_back(npc);
    if (resync) {
      proto::NpcSet set;
      for (const auto npc : shown)
        if (set.npcs.size() < proto::kMaxNpcSet)
          set.npcs.push_back(npc);
      // ...and the ones being handed to or from it: its body there is the
      // NPC's, whatever its copy says of the owner until the hand-over ends.
      for (const auto &[npc, handover] : m_handovers)
        if (InHandover(npc, them) && m_npcs.contains(npc) && !Sees(them, NpcOwner(npc)) &&
            set.npcs.size() < proto::kMaxNpcSet)
          set.npcs.push_back(npc);
      Send(peer, set);
    }
    // The world vehicles of the owners it sees, before who sits in them.
    SendTrafficTo(peer, them, resync);
    // Each with its owner (when handed over), health, poise, hold,
    // animation state and seat.
    for (const auto npc : shown)
      ShowNpcTo(peer, npc);
    // Poise and holds of NPCs the record never listed (said by their owner
    // all the same): from the owners this guest sees.
    const auto unlisted = [&](proto::NpcId npc) {
      return !m_npcs.contains(npc) && Sees(them, NpcOwnerOrAnnouncer(npc));
    };
    for (const auto &[npc, poise] : m_npcPoise)
      if (unlisted(npc))
        Send(peer, poise);
    for (const auto &[npc, level] : m_npcLevels)
      if (Sees(them, NpcOwnerOrAnnouncer(npc)))
        Send(peer, level);
    // The vendors that died in the session, wherever they were.
    for (const auto &[entity, dead] : m_deadVendors)
      Send(peer, dead);
    for (const auto &[npc, hold] : m_npcHolds)
      if (unlisted(npc))
        Send(peer, hold);
    // In a resync, who is not held or seated (its copy may say otherwise).
    if (resync)
      for (const auto npc : shown) {
        if (!m_npcHolds.contains(npc))
          Send(peer, proto::NpcHold{.npc = npc});
        if (!m_npcSeats.contains(npc))
          Send(peer, proto::NpcSeat{.npc = npc});
      }
  }
  // Vehicle combat: damage, gunner seats, weapons in vehicles, chases.
  SendVehiclesTo(peer, them, domains, resync);
  // The story so far: journal, tracked entry, call, choice hub, story states.
  if (domains & proto::kDomainStory) {
    for (const auto &message : m_story.ForJoiner())
      Send(peer, message);
    // A resync also says what is not: no scene plays (a guest that missed
    // its end lets its proxies go).
    if (resync && !m_story.SceneOn())
      Send(peer, proto::SceneState{});
    // And no restriction is on (a guest that missed the lift frees its V).
    if (resync && !m_story.RestrictionsOn())
      Send(peer, proto::StoryRestrictions{.player = m_you});
  }
}

void Endpoint::OnGuestEvent(const net::Event &event, std::vector<SessionEvent> &out) {
  using Kind = net::Event::Kind;
  switch (event.kind) {
  case Kind::Connected:
    m_hostPeer = event.peer;
    Send(m_hostPeer, proto::Hello{.name = m_identity.name,
                                  .gameVersion = m_identity.install.build,
                                  .body = m_identity.body,
                                  .flags = static_cast<std::uint8_t>(
                                      m_identity.expansion ? proto::kHelloExpansion : 0)});
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
  // A session recording keeps what the host said in full: compact forms are
  // tapped once decoded (PoseState, NpcStates).
  if (m_receivedTap && !std::holds_alternative<proto::PoseFrame>(message) &&
      !std::holds_alternative<proto::ActorFrame>(message) &&
      !std::holds_alternative<proto::NpcMotion>(message) &&
      !std::holds_alternative<proto::NpcStates>(message) &&
      !std::holds_alternative<proto::PlayerMotion>(message) &&
      !std::holds_alternative<proto::VehicleMotion>(message))
    m_receivedTap(message);
  // Compact states read back whole.
  if (const auto *motion = std::get_if<proto::PlayerMotion>(&message)) {
    OnGuestMessage(netcode::Whole(*motion), out);
    return;
  }
  if (const auto *motion = std::get_if<proto::VehicleMotion>(&message)) {
    OnGuestMessage(netcode::Whole(*motion), out);
    return;
  }
  if (m_wireTap)
    m_wireTap(Wire::In, m_host, message);
  if (proto::IsStory(message)) {
    // Only the host speaks for the story; our own relayed states are ours.
    if (const auto *state = std::get_if<proto::StoryState>(&message);
        state && state->player == m_you)
      return;
    if (const auto *cursor = std::get_if<proto::ChoiceCursor>(&message);
        cursor && cursor->player == m_you)
      return;
    // The host's verdict on one of our questions (never an ask, never
    // another's): kept for the session, then our game hears it.
    if (const auto *answer = std::get_if<proto::QuestInteract>(&message)) {
      if (answer->verdict == proto::kInteractAsk || answer->player != m_you)
        return;
      m_interactKnown.Note(interact::KeyOf(*answer), answer->verdict);
      out.push_back(events::Story{message});
      return;
    }
    // Our copy of the host's story (digests), and what it already has is
    // not applied again (a resync's snapshot of an unchanged journal).
    // Journal changes always pass: each one may notify.
    const auto *journal = std::get_if<proto::Journal>(&message);
    if (!m_story.Note(message) && !(journal && !journal->snapshot))
      return;
    out.push_back(events::Story{message});
    return;
  }
  if (proto::IsTrade(message)) {
    HearTrade(message, out);
    return;
  }
  if (proto::IsCombat(message)) {
    // An owner's word on its NPCs, the host's rules; a strike only on an NPC
    // we run, an area hit only when on us and while the rules allow it;
    // another player's throws.
    if (const auto *strike = std::get_if<proto::NpcStrike>(&message)) {
      if (OwnsNpc(strike->npc) && strike->player != m_you)
        out.push_back(events::Combat{message});
      return;
    }
    if (const auto *defended = std::get_if<proto::Defended>(&message)) {
      if (OwnsNpc(defended->npc) && defended->player != m_you)
        out.push_back(events::Combat{message});
      return;
    }
    if (const auto *area = std::get_if<proto::AreaHit>(&message);
        area && (area->player != m_you || !FriendlyFire()))
      return;
    if (const auto *thrown = std::get_if<proto::Throw>(&message); thrown && thrown->player == m_you)
      return;
    if (const auto *end = std::get_if<proto::ThrowEnd>(&message); end && end->player == m_you)
      return;
    if (const auto *path = std::get_if<proto::ThrowPath>(&message); path && path->player == m_you)
      return;
    // The difficulty and the rules are state: kept for the digests, said once.
    if (const auto *difficulty = std::get_if<proto::Difficulty>(&message)) {
      if (m_difficulty == *difficulty)
        return;
      m_difficulty = *difficulty;
    }
    if (const auto *rules = std::get_if<proto::Rules>(&message)) {
      if (m_rules == *rules)
        return;
      m_rules = *rules;
    }
    // Levels are state too: a repeat (a resync) is applied nowhere. A
    // player's own level is never relayed.
    if (std::holds_alternative<proto::PlayerLevel>(message))
      return;
    if (const auto *lend = std::get_if<proto::CarLend>(&message)) {
      if (lend->player != m_you && lend->player != proto::kNoPlayer)
        NoteLend(*lend, out);
      return;
    }
    if (const auto *session = std::get_if<proto::SessionLevel>(&message)) {
      if (m_sessionLevel == *session)
        return;
      m_sessionLevel = *session;
    }
    if (const auto *scale = std::get_if<proto::NpcScale>(&message)) {
      // Of an NPC we know and do not run (our own word is ours), once.
      if (!m_npcs.contains(scale->npc) || OwnsNpc(scale->npc))
        return;
      if (const auto it = m_npcScale.find(scale->npc);
          it != m_npcScale.end() && it->second == *scale)
        return;
      m_npcScale[scale->npc] = *scale;
    }
    if (const auto *level = std::get_if<proto::NpcLevel>(&message)) {
      if (!m_npcs.contains(level->npc))
        return; // of an NPC that is gone
      if (const auto it = m_npcLevels.find(level->npc);
          it != m_npcLevels.end() && it->second == *level)
        return;
      m_npcLevels[level->npc] = *level;
    }
    // A placed NPC we show: said once. A death of the ledger: news once.
    if (const auto *placed = std::get_if<proto::NpcPlaced>(&message)) {
      if (!m_npcs.contains(placed->npc) || OwnsNpc(placed->npc))
        return; // gone, or ours (our own word, back)
      if (const auto it = m_npcPlaced.find(placed->npc);
          it != m_npcPlaced.end() && it->second == *placed)
        return;
      m_npcPlaced[placed->npc] = *placed;
    }
    if (const auto *death = std::get_if<proto::NpcDeath>(&message)) {
      if (!m_deaths.Note(*death))
        return;
    }
    // A vendor of an NPC we show, or one that died: said once.
    if (const auto *vendor = std::get_if<proto::NpcVendor>(&message)) {
      if ((vendor->flags & proto::kVendorDead) != 0) {
        if (!m_deadVendors.emplace(vendor->entity, *vendor).second)
          return;
      } else {
        if (!m_npcs.contains(vendor->npc) || OwnsNpc(vendor->npc))
          return; // gone, or ours (our own word, back)
        if (const auto it = m_npcVendors.find(vendor->npc);
            it != m_npcVendors.end() && it->second == *vendor)
          return;
        m_npcVendors[vendor->npc] = *vendor;
      }
    }
    out.push_back(events::Combat{message});
    return;
  }
  // Links, owners, hand-overs, and requests for the NPCs we run (regions).
  if (OnRegionMessage(message, out))
    return;
  // Shared vehicles: their owners, hand-overs, requests for ours.
  if (OnHostTraffic(message, out))
    return;
  if (proto::IsVehicleCombat(message)) {
    OnHostVehicles(message, out);
    return;
  }
  std::visit(
      Overloaded{
          [&](const proto::Welcome &welcome) {
            m_you = welcome.you;
            m_host = welcome.host;
            SetClockOffset(static_cast<std::int64_t>(welcome.hostTimeUs) -
                               static_cast<std::int64_t>(net::NowUs()),
                           true);
            // Sequences start at the host clock's millisecond: a player
            // back under its old id (after a crash) is newer than
            // everything it sent before, which receivers already hold.
            m_seq = m_poseSeq = m_vehicleSeq = m_shotSeq =
                static_cast<std::uint32_t>(welcome.hostTimeUs / 1000);
            out.push_back(events::Joined{m_you, m_host});
            Send(m_hostPeer, m_identity.install);
            SendModsTo(m_hostPeer);
            SendLocalAppearanceNow();
            SendLocalHeldNow();
            SendLocalSeatNow();
            SendLocalRadioNow();
            SendLocalPausedNow();
            SendLocalStealthNow();
            SendLocalPresenceNow();
            SendLocalStatusVisualsNow();
            SendLocalTimeNow();
            SendLocalElsewhereNow();
            SendLocalActivityNow();
            SendLocalVitalsNow();
            SendLocalTransitNow();
            SendRegionHoldNow();
            SendLocalHeatNow();
            SendLocalLevelNow();
            SendLocalLendNow();
            SendLocalLinkNow();
            SendLocalWaypointNow();
            SendLocalVehicleCombatNow();
            // What overtook the Welcome, in the order it came.
            for (const auto &early : std::exchange(m_early, {}))
              OnGuestMessage(early, out);
          },
          [&](const proto::Refused &refused) {
            m_ended = true;
            out.push_back(events::Ended{"refused: " + refused.reason});
          },
          [&](const proto::Install &install) {
            // The host's game against ours: said, never a reason to leave.
            const auto host = m_players.find(m_host);
            const std::string name = host != m_players.end() && !host->second.name.empty()
                                         ? host->second.name
                                         : "The host";
            out.push_back(events::RemoteInstall{
                m_host, name, install, compat::Differences(m_identity.install, install, name)});
          },
          [&](const proto::ModList &list) { OnModList(m_host, list, out); },
          [&](const proto::Substitutes &substitutes) { OnSubstitutes(m_host, substitutes, out); },
          [&](const proto::Pong &pong) {
            const auto now = net::NowUs();
            const auto rtt = now - pong.pingSentUs;
            // Saturating: from UINT64_MAX (no ping yet) it wrapped to 199 us,
            // and no pong over a real link (rtt > 0.2 ms) ever counted, so
            // the offset stayed the Welcome's, off by the one-way latency.
            if (m_bestRttUs <= UINT64_MAX - kRttForgetUsPerPing)
              m_bestRttUs += kRttForgetUsPerPing;
            if (rtt <= m_bestRttUs) {
              m_bestRttUs = rtt;
              const auto midpoint = pong.pingSentUs + rtt / 2;
              SetClockOffset(static_cast<std::int64_t>(pong.hostTimeUs) -
                                 static_cast<std::int64_t>(midpoint),
                             false);
            }
          },
          [&](const proto::Roster &roster) {
            std::map<proto::PlayerId, proto::PlayerInfo> next;
            for (const auto &player : roster.players)
              next[player.id] = player;
            // How the session shows us: our name, or the host made it unique.
            if (const auto self = next.find(m_you); self != next.end() &&
                                                    !self->second.name.empty() &&
                                                    self->second.name != m_shownName) {
              m_shownName = self->second.name;
              if (m_shownName != m_identity.name)
                out.push_back(events::NamedAs{m_identity.name, m_shownName});
            }
            for (const auto &[id, info] : next)
              if (id != m_you && !m_players.contains(id))
                out.push_back(events::PlayerJoined{info});
            for (const auto &[id, info] : m_players)
              if (!next.contains(id)) {
                if (NotePaused(id, false))
                  out.push_back(events::PauseChanged{{id, false}});
                EndTimeOf(id, out, false);
                EndElsewhereOf(id, out, false);
                EndActivityOf(id, out, false);
                // Its state goes with it: a namesake joining later starts afresh.
                m_appearances.erase(id);
                m_held.erase(id);
                m_seats.erase(id);
                m_radios.erase(id);
                m_stealth.erase(id);
                m_presences.erase(id);
                m_statusVisuals.erase(id);
                m_transits.erase(id);
                EndHeatOf(id, out, false);
                m_lends.erase(id);
                ForgetVehiclesOf(id, out, false);
                m_lives.erase(id);
                m_holders.erase(id);
                m_places.erase(id);
                m_areaOwners.erase(id);
                m_story.Forget(id);
                m_commsLinks.erase(id);
                m_waypoints.erase(id);
                ForgetStreams(id);
                out.push_back(events::PlayerLeft{id});
              }
            m_players = std::move(next);
          },
          [&](const proto::PlayerState &state) {
            if (state.player == m_you)
              return;
            m_places[state.player] = {.position = state.position, .known = true};
            NotePosition(state);
            out.push_back(events::RemoteState{state});
          },
          [&](const proto::PoseState &pose) {
            if (pose.player != m_you)
              out.push_back(events::RemotePose{pose});
          },
          [&](const proto::PoseFrame &frame) { OnRemotePoseFrame(frame, m_hostPeer, out); },
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
            if (known && OwnsNpc(spawn.npc))
              return; // ours: our game is its source
            m_npcs[spawn.npc] = spawn;
            if (known)
              return;
            m_corpses.erase(spawn.npc);
            m_npcBook.Seed(spawn);
            out.push_back(events::HostNpcSpawn{spawn});
          },
          [&](const proto::NpcStates &states) {
            // The old world's states (before the host's last epoch) are void.
            if (m_firstNpcSeq != 0 && static_cast<std::int32_t>(states.seq - m_firstNpcSeq) < 0)
              return;
            m_npcBook.Note(states);
            OnHostNpcStates(states, out);
          },
          [&](const proto::NpcMotion &motion) {
            if (m_firstNpcSeq != 0 && static_cast<std::int32_t>(motion.seq - m_firstNpcSeq) < 0)
              return;
            // The host compacts only its own NPCs' states (regions: another
            // owner's come whole): the book fills in the host's alone.
            std::map<proto::NpcId, proto::NpcSpawn> hosts;
            for (const auto &[npc, spawn] : m_npcs)
              if (NpcOwner(npc) == m_host)
                hosts.emplace(npc, spawn);
            OnHostNpcStates(m_npcBook.Apply(motion, hosts), out);
          },
          [&](const proto::ActorPose &pose) {
            // As NPC states: none of the host's old world, none for an NPC
            // we do not know (a pose that overtook its NpcGone).
            if (m_firstNpcSeq != 0 && static_cast<std::int32_t>(pose.seq - m_firstNpcSeq) < 0)
              return;
            if (m_npcs.contains(pose.npc))
              out.push_back(events::HostActorPose{pose});
          },
          [&](const proto::ActorFrame &actor) {
            // An actor's pose, delta coded: fenced and filtered as a whole
            // one, then whole again for our game.
            if (m_firstNpcSeq != 0 &&
                static_cast<std::int32_t>(actor.frame.seq - m_firstNpcSeq) < 0)
              return;
            if (!m_npcs.contains(actor.npc))
              return;
            if (const auto decoded = m_actorDecoders[actor.npc].Decode(actor.frame)) {
              const auto whole = pose::ForActor(actor.npc, *decoded);
              if (m_receivedTap)
                m_receivedTap(whole);
              out.push_back(events::HostActorPose{whole});
            }
          },
          [&](const proto::RigPose &pose) {
            // A face, or a body on another rig: fenced and filtered as an
            // actor's pose, for the story's side of our game.
            if (m_firstNpcSeq != 0 && static_cast<std::int32_t>(pose.seq - m_firstNpcSeq) < 0)
              return;
            if (m_npcs.contains(pose.npc))
              out.push_back(events::Story{pose});
          },
          [&](const proto::RigTracks &tracks) {
            // Its float tracks: the same.
            if (m_firstNpcSeq != 0 && static_cast<std::int32_t>(tracks.seq - m_firstNpcSeq) < 0)
              return;
            if (m_npcs.contains(tracks.npc))
              out.push_back(events::Story{tracks});
          },
          [&](const proto::SceneCamera &camera) {
            // The host's view of its cutscene: for the story's side of our game.
            out.push_back(events::Story{camera});
          },
          [&](const proto::NpcGone &gone) {
            if (OwnsNpc(gone.npc)) {
              // The host's record let go of one of ours (our world was
              // rebuilt, nobody could take it over): no longer shared.
              ForgetNpc(gone.npc);
              out.push_back(events::NpcOwnerChanged{
                  .npc = gone.npc, .owner = proto::kNoPlayer, .was = m_you});
              return;
            }
            m_npcHolds.erase(gone.npc);
            m_npcOwner.erase(gone.npc);
            m_given.erase(gone.npc);
            m_npcBook.Forget(gone.npc);
            m_npcVendors.erase(gone.npc);
            m_npcPlaced.erase(gone.npc);
            if (m_npcs.erase(gone.npc) > 0) {
              m_npcHealth.erase(gone.npc);
              m_npcLevels.erase(gone.npc);
              m_npcScale.erase(gone.npc);
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
              // (Never ours: the host does not list what we run.)
              if (shared.contains(it->first) || NpcOwner(it->first) == m_you) {
                ++it;
                continue;
              }
              m_npcHealth.erase(it->first);
              m_npcLevels.erase(it->first);
              m_npcScale.erase(it->first);
              m_npcVendors.erase(it->first);
              m_npcPlaced.erase(it->first);
              m_npcHolds.erase(it->first);
              m_npcOwner.erase(it->first);
              m_npcBook.Forget(it->first);
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
          [&](const proto::Presence &presence) {
            if (presence.player != m_you && Keep(m_presences, presence))
              out.push_back(events::RemotePresence{presence});
          },
          [&](const proto::Cue &cue) {
            if (cue.player != m_you)
              out.push_back(events::RemoteCue{cue});
          },
          [&](const proto::StatusVisuals &visuals) {
            if (visuals.player != m_you && Keep(m_statusVisuals, visuals))
              out.push_back(events::RemoteStatusVisuals{visuals});
          },
          [&](const proto::NpcActionReply &reply) {
            out.push_back(events::NpcActionAnswered{reply});
          },
          [&](const proto::NpcHold &hold) {
            // Held or free as we have it already (a resync): nothing to do.
            const auto it = m_npcHolds.find(hold.npc);
            if (hold.player == proto::kNoPlayer) {
              if (it == m_npcHolds.end())
                return;
              m_npcHolds.erase(it);
            } else {
              if (it != m_npcHolds.end() && it->second == hold)
                return;
              m_npcHolds[hold.npc] = hold;
            }
            out.push_back(events::HostNpcHold{hold});
          },
          [&](const proto::NpcCue &cue) {
            if (cue.player == m_you)
              out.push_back(events::HostNpcCue{cue});
          },
          [&](const proto::NpcShotAt &shot) {
            if (shot.player == m_you)
              out.push_back(events::ShotAtUs{shot});
          },
          [&](const proto::NpcAnims &anims) { out.push_back(events::HostNpcAnims{anims}); },
          [&](const proto::NpcShot &shot) { out.push_back(events::HostNpcShot{shot}); },
          [&](const proto::NpcReaction &reaction) {
            out.push_back(events::HostNpcReaction{reaction});
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
          [&](const proto::Transit &transit) {
            if (transit.player == m_you)
              return;
            NoteTransit(transit);
            out.push_back(events::RemoteTransit{transit});
          },
          [&](const proto::NpcSeat &seat) { out.push_back(events::HostNpcSeat{seat}); },
          [&](const proto::Heat &heat) {
            if (heat.player != m_you && NoteHeat(heat))
              out.push_back(events::RemoteHeat{heat});
          },
          [&](const proto::Crime &crime) {
            if (crime.player == m_you)
              out.push_back(events::CrimeReported{crime});
          },
          [&](const proto::Elsewhere &away) {
            const auto safe = elsewhere::Sanitized(away);
            if (safe.player != m_you && NoteElsewhere(safe))
              out.push_back(events::RemoteElsewhere{safe});
          },
          [&](const proto::Activity &busy) {
            const auto safe = activity::Sanitized(busy);
            if (safe.player != m_you && NoteActivity(safe))
              out.push_back(events::RemoteActivity{safe});
          },
          [&](const proto::TimeSkip &skip) {
            if (skip.from != m_you)
              out.push_back(events::TimeSkipAsked{skip});
          },
          [&](const proto::TimeSkipVote &vote) {
            if (vote.asker == m_you)
              out.push_back(events::TimeSkipVoted{vote});
          },
          [&](const proto::TimeSkipResult &result) {
            if (m_ourSkip && result.asker == m_you && result.request == m_ourSkip->request)
              m_ourSkip.reset();
            out.push_back(events::TimeSkipEnded{result});
          },
          [&](const proto::Regroup &offer) {
            if (offer.from != m_you && (offer.to == m_you || offer.to == proto::kNoPlayer))
              out.push_back(events::RegroupOffered{offer});
          },
          [&](const proto::RegroupAnswer &answer) {
            if (answer.from != m_you && answer.to == m_you)
              out.push_back(events::RegroupAnswered{answer});
          },
          [&](const proto::WorldItem &item) { out.push_back(events::WorldItemAppeared{item}); },
          [&](const proto::WorldItemTaken &taken) {
            out.push_back(events::WorldItemTakenBy{taken});
          },
          [&](const proto::SharedItem &shared) {
            if (shared.player != m_you)
              out.push_back(events::SharedItemFrom{shared});
          },
          [&](const proto::Vitals &vitals) {
            // Ours is our game's: never taken from the network.
            if (vitals.player == m_you || vitals.player == proto::kNoPlayer)
              return;
            // As we have it already (a resync, a repeat): said once. A V we
            // know nothing of is up.
            const auto it = m_lives.find(vitals.player);
            if (it == m_lives.end()
                    ? vitals.life == proto::Life::Up
                    : it->second.life == vitals.life && it->second.flags == vitals.flags)
              return;
            m_lives[vitals.player] = vitals;
            out.push_back(events::RemoteVitals{vitals});
          },
          [&](const proto::Revive &revive) {
            if (revive.target == m_you && revive.player != m_you)
              out.push_back(events::ReviveBy{revive});
          },
          [&](const proto::GameOver &over) { out.push_back(events::SessionLost{over}); },
          [&](const proto::Digest &digest) {
            // On the same ordered channel as the host's Epoch, a digest of
            // another epoch means that Epoch never came (lost): it says
            // which one the host is in, and the host hears we have seen it.
            if (digest.epoch != m_hostEpoch) {
              m_hostEpoch = digest.epoch;
              Send(m_hostPeer, proto::Epoch{.player = m_you,
                                            .epoch = m_epoch,
                                            .seenHost = m_hostEpoch,
                                            .reason = proto::kEpochJoin});
            }
            const auto ask = m_checker.Compare(digest, DigestFor(m_you), net::NowUs());
            if (ask == 0)
              return;
            Send(m_hostPeer, proto::ResyncRequest{ask});
            out.push_back(events::Resync{m_you, ask});
          },
          [&](const proto::DeviceState &state) {
            // The host's word only (an ask is a guest's, never relayed).
            if (state.how == proto::kDeviceAsk)
              return;
            const bool news = m_devices.Accept(state);
            // The answer to our own request, even when it changes nothing
            // of the record: the host's device did not become what we asked.
            const bool answer = m_deviceAsked.erase(devices::KeyOf(state)) > 0;
            if (news || answer)
              out.push_back(events::HostDevice{state});
          },
          [&](const proto::Breach &breach) {
            // Another player's breach, granted: our NPCs of its network,
            // or the NPC it breached (ours). Never our own back.
            if (breach.player != m_you)
              out.push_back(events::BreachBy{breach});
          },
          [&](const proto::BreachResult &result) { out.push_back(events::BreachAnswered{result}); },
          [&](const proto::Fracture &fracture) {
            // Kept as the host sent it (our own back too: the record is
            // the session's); played unless it is our own.
            m_fractureRecord.Note(fracture);
            if (fracture.player != m_you)
              out.push_back(events::Fractured{fracture});
          },
          [&](const proto::DeviceFire &fire) {
            // The host's turret's doing only (what a copy sees is a guest's
            // word to the host, never passed on).
            if (fire.entity != 0 && fire.kind <= proto::DeviceFireKind::Shot)
              out.push_back(events::DeviceFired{fire});
          },
          [&](const proto::DeviceSet &) {
            m_devices.Clear();
            m_deviceAsked.clear();
            out.push_back(events::DevicesReset{});
          },
          [&](const proto::DeviceDigest &digest) {
            if (!m_deviceCheck.Compare(digest, DeviceDigestNow(), net::NowUs()))
              return;
            Send(m_hostPeer, proto::DeviceSet{});
            out.push_back(events::Resync{m_you, proto::kDomainDevices});
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
          [&](const proto::Mark &mark) {
            if (mark.from != m_you && mark.from != proto::kNoPlayer)
              out.push_back(events::MarkFrom{mark});
          },
          [&](const proto::Tag &tag) {
            // The host delivers only what is for us; a link that broke on
            // the way is checked again here.
            if (tag.from != m_you && LinkedPair(tag.from, m_you))
              out.push_back(events::TagFrom{tag});
          },
          [&](const proto::Link &link) {
            if (link.player != m_you && Keep(m_commsLinks, link))
              out.push_back(events::RemoteLink{link});
          },
          [&](const proto::Waypoint &waypoint) {
            if (waypoint.player != m_you && Keep(m_waypoints, waypoint))
              out.push_back(events::RemoteWaypoint{waypoint});
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
  Record(state);
  if (m_tap)
    m_tap(state);
  if (!IsJoined() || m_ended)
    return;
  m_places[m_you] = {.position = state.position, .known = true};
  state.player = m_you;
  state.timeUs = HostNowUs();
  m_ownPosition = state.position;
  // A still V 5 times a second, a far one less often (netcode.md).
  const auto due = m_compact ? m_stateGate.Offer(state, netcode::StateIntervalUs(NearestOther()))
                             : std::vector<proto::PlayerState>{state};
  if (due.empty())
    ++m_statesSkipped;
  for (auto send : due) {
    send.seq = ++m_seq;
    const auto wire = m_compact ? proto::Message(netcode::Compact(send)) : proto::Message(send);
    if (IsHost())
      Broadcast(wire);
    else
      Send(m_hostPeer, wire);
  }
}

void Endpoint::SendLocalPose(proto::PoseState pose) {
  Record(pose);
  if (m_tap)
    m_tap(pose);
  if (!IsJoined() || m_ended)
    return;
  // Far from everyone, fewer poses (netcode.md); delta coded.
  if (m_compact && !m_poseGate.Due(net::NowUs(), netcode::PoseIntervalUs(NearestOther()))) {
    ++m_poseSkipped;
    return;
  }
  pose.player = m_you;
  pose.seq = ++m_poseSeq;
  pose.timeUs = HostNowUs();
  const proto::Message frame =
      m_compact ? proto::Message(m_poseEncoder.Encode(pose)) : proto::Message(pose);
  if (IsHost())
    Broadcast(frame);
  else
    Send(m_hostPeer, frame);
}

void Endpoint::SetLocalAppearance(proto::Appearance appearance) {
  Record(appearance);
  if (m_tap)
    m_tap(appearance);
  // The same look again (a mirror or a wardrobe left without a change): the
  // others would only redress the puppet.
  if (m_localAppearance && m_localAppearance->body == appearance.body &&
      m_localAppearance->look == appearance.look && m_localAppearance->items == appearance.items)
    return;
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
  Record(held);
  if (m_tap)
    m_tap(held);
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
  Record(seat);
  if (m_tap)
    m_tap(seat);
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
  Record(radio);
  if (m_tap)
    m_tap(radio);
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
  Record(world);
  if (!IsHost() || m_ended)
    return; // a guest's world is the host's
  world.revision = ++m_worldRevision;
  m_world = world;
  Broadcast(world);
}

bool Endpoint::SpeaksFor(proto::NpcId npc) const {
  if (m_you == proto::kNoPlayer)
    return false;
  if (OwnsNpc(npc))
    return true;
  if (const auto corpse = m_corpseOwner.find(npc); corpse != m_corpseOwner.end())
    return corpse->second == m_you;
  // One of our own range that the record no longer holds (knocked out, a
  // word that follows its removal), and that nobody else was given.
  return !m_npcs.contains(npc) && !m_npcOwner.contains(npc) && regions::Announcer(npc) == m_you;
}

void Endpoint::AskOwner(proto::NpcId npc, const proto::Message &stamped) {
  if (!IsHost())
    Send(m_hostPeer, stamped);
  else
    RouteToOwner(npc, stamped, m_you); // ours: our game's own business
}

void Endpoint::SendNpcSpawn(const proto::NpcSpawn &spawn) {
  Record(spawn);
  if (!IsJoined() || m_ended)
    return;
  // A new NPC of our own range, or one we run (its record again).
  if (m_npcs.contains(spawn.npc) ? !OwnsNpc(spawn.npc) : regions::Announcer(spawn.npc) != m_you)
    return;
  m_corpseOwner.erase(spawn.npc);
  m_npcs[spawn.npc] = spawn;
  SayNpc(spawn);
  // Its level, if the game said it first (its owner's to say: ours).
  if (const auto early = m_npcLevelsEarly.extract(spawn.npc)) {
    m_npcLevels[spawn.npc] = early.mapped();
    SayNpc(early.mapped());
  }
  // Its scale, the same way (ours to say: we run it).
  if (const auto early = m_npcScaleEarly.extract(spawn.npc)) {
    m_npcScale[spawn.npc] = early.mapped();
    SayNpc(early.mapped());
  }
  // That it is a vendor, likewise (any owner's to say).
  if (const auto early = m_npcVendorsEarly.extract(spawn.npc)) {
    m_npcVendors[spawn.npc] = early.mapped();
    SayNpc(early.mapped());
  }
  // ...and that it is placed in the world.
  if (const auto early = m_npcPlacedEarly.extract(spawn.npc)) {
    m_npcPlaced[spawn.npc] = early.mapped();
    SayNpc(early.mapped());
  }
}

void Endpoint::SendNpcStates(proto::NpcStates states) {
  Record(states);
  if (!IsJoined() || m_ended)
    return;
  std::erase_if(states.npcs, [&](const proto::NpcEntry &entry) { return !OwnsNpc(entry.npc); });
  states.seq = ++m_npcSeq;
  states.timeUs = HostNowUs();
  // The latest position also goes to the kept spawn, for late joiners.
  for (const auto &entry : states.npcs)
    if (const auto it = m_npcs.find(entry.npc); it != m_npcs.end()) {
      it->second.position = entry.position;
      it->second.yaw = entry.yaw;
    }
  // Linked with nobody: nobody shows them.
  if (!IsHost()) {
    if (!Linked().empty())
      SayNpc(states);
    return;
  }
  // Ours, as the host: only what changed, quantized (netcode.md); whole when
  // out of range. The owners' own (regions) are relayed whole.
  if (const auto motion = m_compact ? m_npcCoder.Encode(states) : std::nullopt) {
    ++m_npcMotions;
    ShareNpc(*motion, m_you);
  } else {
    ++m_npcWhole;
    m_npcCoder.Reset();
    ShareNpc(states, m_you);
  }
}

float Endpoint::ActorWeight(proto::NpcId npc) const {
  const auto it = m_npcs.find(npc);
  if (it == m_npcs.end())
    return 1.f;
  float nearest = -1.f;
  for (const auto &[player, at] : m_positions) {
    if (player == m_you)
      continue;
    const auto &actor = it->second.position;
    const float d = std::hypot(at.x - actor.x, at.y - actor.y, at.z - actor.z);
    if (nearest < 0.f || d < nearest)
      nearest = d;
  }
  // Nobody's place known: as if near.
  return nearest <= 15.f ? 1.f : nearest <= 40.f ? 0.5f : 0.25f;
}

void Endpoint::SendActorPose(proto::ActorPose pose) {
  if (!IsHost() || m_ended || !OwnsNpc(pose.npc))
    return;
  if (!m_compact) {
    pose.seq = ++m_npcSeq;
    pose.timeUs = HostNowUs();
    ShareNpc(pose, m_you); // the games that show the actor, as its states
    return;
  }
  // The scene's streams share one budget: this body's turn, or not yet.
  const auto now = net::NowUs();
  const auto stream = scene::StreamOf(pose.npc, proto::kRigBody);
  if (!m_scenePacer.Due(stream, ActorWeight(pose.npc), now))
    return;
  pose.seq = ++m_npcSeq;
  pose.timeUs = HostNowUs();
  const auto [encoder, fresh] = m_actorEncoders.try_emplace(pose.npc);
  if (fresh) // slower than a V's stream: a key every 2 s, bones within 0.006 rad kept
    encoder->second.Pace(64, 2'000'000, 0.006f);
  const proto::Message frame =
      proto::ActorFrame{.npc = pose.npc, .frame = encoder->second.Encode(pose::FromActor(pose))};
  m_scenePacer.Sent(stream, proto::Encode(frame).size(), now);
  ShareNpc(frame, m_you);
}

void Endpoint::SendRigPose(proto::NpcId npc, std::uint8_t part, std::uint32_t rigHash,
                           std::span<const rig::Bone> reference, std::span<const rig::Bone> pose) {
  if (!IsHost() || m_ended || !OwnsNpc(npc) || part > proto::kRigFace || reference.empty() ||
      reference.size() != pose.size() || pose.size() > proto::kMaxRigBones)
    return;
  const auto now = net::NowUs();
  const auto stream = scene::StreamOf(npc, part);
  if (!m_scenePacer.Due(stream, part == proto::kRigFace ? scene::kFaceWeight : ActorWeight(npc),
                        now))
    return;
  auto &state = m_rigStreams[stream];
  if (state.hash != rigHash || state.encoder.Bones() != reference.size()) {
    state.hash = rigHash;
    state.encoder.Reset(reference);
  }
  // Small frames often rather than large ones seldom: a tenth of a second
  // of the stream's share each.
  const auto cap =
      static_cast<std::size_t>(std::clamp(m_scenePacer.Share(stream) / 10.f, 40.f, 240.f));
  proto::RigPose said{.npc = npc,
                      .part = part,
                      .rig = rigHash,
                      .bones = static_cast<std::uint16_t>(reference.size()),
                      .data = state.encoder.Encode(pose, cap)};
  if (said.data.empty())
    return;
  said.seq = ++m_npcSeq;
  said.timeUs = HostNowUs();
  const proto::Message message = std::move(said);
  m_scenePacer.Sent(stream, proto::Encode(message).size(), now);
  ShareNpc(message, m_you);
}

void Endpoint::SendRigTracks(proto::NpcId npc, std::uint8_t part, std::uint32_t rigHash,
                             std::span<const float> reference, std::span<const float> values) {
  if (!IsHost() || m_ended || !OwnsNpc(npc) || part > proto::kRigFace || reference.empty() ||
      reference.size() != values.size() || values.size() > proto::kMaxRigTracks)
    return;
  const auto now = net::NowUs();
  const auto stream = scene::TracksOf(npc, part);
  if (!m_scenePacer.Due(stream, scene::kTracksWeight, now))
    return;
  auto &state = m_trackStreams[stream];
  if (state.hash != rigHash || state.encoder.Tracks() != reference.size()) {
    state.hash = rigHash;
    state.encoder.Reset(reference);
  }
  const auto cap =
      static_cast<std::size_t>(std::clamp(m_scenePacer.Share(stream) / 10.f, 40.f, 240.f));
  proto::RigTracks said{.npc = npc,
                        .part = part,
                        .rig = rigHash,
                        .tracks = static_cast<std::uint16_t>(reference.size()),
                        .data = state.encoder.Encode(values, cap)};
  if (said.data.empty())
    return;
  said.seq = ++m_npcSeq;
  said.timeUs = HostNowUs();
  const proto::Message message = std::move(said);
  m_scenePacer.Sent(stream, proto::Encode(message).size(), now);
  ShareNpc(message, m_you);
}

void Endpoint::SendSceneCamera(proto::SceneCamera camera) {
  if (!IsHost() || m_ended || camera.play == 0)
    return;
  const auto now = net::NowUs();
  if (!m_scenePacer.Due(scene::kCameraStream, scene::kCameraWeight, now)) {
    // A cut that is not sent is still a cut for the next view that is.
    m_cameraCut = m_cameraCut || (camera.flags & proto::kCameraCut) != 0;
    return;
  }
  if (std::exchange(m_cameraCut, false))
    camera.flags |= proto::kCameraCut;
  camera.seq = ++m_cameraSeq;
  camera.timeUs = HostNowUs();
  const proto::Message message = camera;
  m_scenePacer.Sent(scene::kCameraStream, proto::Encode(message).size(), now);
  Broadcast(message);
}

void Endpoint::SendNpcGone(const proto::NpcGone &gone) {
  Record(gone);
  if (!IsJoined() || m_ended || !SpeaksFor(gone.npc))
    return;
  NoteVendorGone(gone, nullptr);
  NoteDeath(gone);
  ForgetNpc(gone.npc);
  m_npcPlacedEarly.erase(gone.npc);
  m_npcCoder.Forget(gone.npc);
  m_npcLevels.erase(gone.npc);
  m_npcLevelsEarly.erase(gone.npc);
  m_npcScaleEarly.erase(gone.npc);
  m_npcVendorsEarly.erase(gone.npc);
  if (gone.reason == proto::kGoneAway)
    m_corpseOwner.erase(gone.npc);
  else
    m_corpseOwner[gone.npc] = m_you; // its body stays ours to speak for
  SayNpc(gone);
}

void Endpoint::SendNpcAction(const proto::NpcAction &action) {
  Record(action);
  if (m_tap)
    m_tap(action);
  if (!IsJoined() || m_ended)
    return;
  auto mine = action;
  mine.player = m_you;
  // The host's own action on a guest's NPC: its answer comes back to us.
  if (IsHost())
    if (const auto owner = NpcOwner(mine.npc); owner != proto::kNoPlayer && owner != m_you)
      m_askers[mine.npc].push_back(m_you);
  AskOwner(mine.npc, mine);
}

// ---- Other mods (docs/architecture/mod-compat.md) ----

void Endpoint::SendModsTo(net::PeerId peer) {
  if (!m_identity.tellsMods)
    return;
  // Hashes and categories: the names stay here until asked for.
  proto::ModList list{.kind = proto::kModListed};
  for (const auto &mod : m_identity.mods)
    if (list.mods.size() < proto::kMaxModEntries)
      list.mods.push_back({.hash = mod.hash, .categories = mod.categories});
  Send(peer, list);
}

void Endpoint::OnModList(proto::PlayerId player, const proto::ModList &list,
                         std::vector<SessionEvent> &out) {
  const auto found = m_players.find(player);
  const std::string name = found != m_players.end() && !found->second.name.empty()
                               ? found->second.name
                               : std::string(player == m_host ? "The host" : "A player");
  switch (list.kind) {
  case proto::kModListed: {
    // Theirs against ours; a game that does not say its own compares nothing.
    m_theirMods[player] = list.mods;
    if (m_identity.tellsMods)
      out.push_back(events::RemoteMods{player, name, mods::Compare(m_identity.mods, list.mods)});
    break;
  }
  case proto::kModNamesAsked: {
    // Only the names of our own mods, only those asked for.
    proto::ModList names{.kind = proto::kModNames};
    for (const auto &asked : list.mods)
      for (const auto &mod : m_identity.mods)
        if (mod.hash == asked.hash && names.mods.size() < proto::kMaxModEntries)
          names.mods.push_back(mod);
    if (const auto peer = IsHost() ? PeerOf(player) : std::optional(m_hostPeer))
      Send(*peer, names);
    break;
  }
  case proto::kModNames:
    out.push_back(events::ModNamesFrom{player, name, list.mods});
    break;
  default:
    break;
  }
}

void Endpoint::AskModNames(proto::PlayerId player) {
  if (!IsJoined() || m_ended)
    return;
  const auto theirs = m_theirMods.find(player);
  if (theirs == m_theirMods.end())
    return;
  proto::ModList ask{.kind = proto::kModNamesAsked};
  for (const auto hash : mods::Compare(m_identity.mods, theirs->second).theirsOnlyHashes)
    ask.mods.push_back({.hash = hash});
  if (ask.mods.empty())
    return;
  // Guests compare with the host only (like Install): a guest asks the host.
  if (!IsHost())
    Send(m_hostPeer, ask);
  else if (const auto peer = PeerOf(player))
    Send(*peer, ask);
}

void Endpoint::SendSubstitutes(proto::Substitutes substitutes) {
  if (!IsJoined() || m_ended || substitutes.owner == m_you)
    return;
  substitutes.seer = m_you;
  if (!IsHost())
    Send(m_hostPeer, substitutes);
  else if (const auto peer = PeerOf(substitutes.owner))
    Send(*peer, substitutes);
}

void Endpoint::OnSubstitutes(proto::PlayerId player, proto::Substitutes substitutes,
                             std::vector<SessionEvent> &out) {
  // The host stamps who sees them and passes them to their owner only.
  if (IsHost())
    substitutes.seer = player;
  if (substitutes.owner == m_you) {
    const auto seer = m_players.find(substitutes.seer);
    out.push_back(events::SubstitutesFrom{substitutes, seer != m_players.end() ? seer->second.name
                                                                               : std::string()});
  } else if (IsHost()) {
    if (const auto peer = PeerOf(substitutes.owner))
      Send(*peer, substitutes);
  }
}

void Endpoint::SendNpcActionReply(proto::PlayerId player, const proto::NpcActionReply &reply) {
  Record(reply);
  if (!IsJoined() || m_ended)
    return;
  // A guest's answer goes to whoever asked: the host knows who.
  if (!IsHost())
    Send(m_hostPeer, reply);
  else if (const auto peer = PeerOf(player))
    Send(*peer, reply);
}

void Endpoint::SendNpcHold(const proto::NpcHold &hold) {
  Record(hold);
  if (!IsJoined() || m_ended || !SpeaksFor(hold.npc))
    return;
  // Kept for late joiners, announced or not (a body held past the shared
  // list, a takedown on one it never listed).
  if (hold.player == proto::kNoPlayer)
    m_npcHolds.erase(hold.npc);
  else
    m_npcHolds[hold.npc] = hold;
  SayNpc(hold);
}

void Endpoint::SendNpcCue(const proto::NpcCue &cue) {
  Record(cue);
  if (!IsJoined() || m_ended || cue.player == m_you || !SpeaksFor(cue.npc))
    return;
  if (!IsHost())
    Send(m_hostPeer, cue); // the host passes it on if that player shows the NPC
  else if (const auto peer = PeerOf(cue.player); peer && SharedWith(cue.npc, cue.player))
    Send(*peer, cue);
}

void Endpoint::SendNpcHealth(const proto::NpcHealth &health) {
  Record(health);
  if (!IsJoined() || m_ended || !SpeaksFor(health.npc))
    return;
  if (m_npcs.contains(health.npc))
    m_npcHealth[health.npc] = health;
  SayNpc(health);
}

void Endpoint::SendNpcAnims(const proto::NpcAnims &anims) {
  Record(anims);
  if (!IsJoined() || m_ended || anims.npcs.empty())
    return;
  // Only NPCs we run, announced and not gone: a late joiner gets nothing it
  // can't place.
  proto::NpcAnims known;
  for (const auto &anim : anims.npcs)
    if (OwnsNpc(anim.npc) && known.npcs.size() < proto::kMaxNpcAnims) {
      m_npcAnims[anim.npc] = anim;
      known.npcs.push_back(anim);
    }
  if (!known.npcs.empty())
    SayNpc(known);
}

void Endpoint::SendNpcReaction(const proto::NpcReaction &reaction) {
  // The NPC's owner's word, for the games linked with it (regions.md).
  if (!IsJoined() || m_ended || !m_npcs.contains(reaction.npc) || !OwnsNpc(reaction.npc))
    return;
  SayNpc(reaction);
}

void Endpoint::SendNpcShot(const proto::NpcShot &shot) {
  Record(shot);
  if (!IsJoined() || m_ended || !OwnsNpc(shot.npc))
    return;
  if (!IsHost()) {
    if (!Linked().empty())
      Send(m_hostPeer, shot); // the host leaves out whoever it shoots at
    return;
  }
  // The guest this NPC is shooting at fires the shot itself from its proxy
  // (NpcShotAt, sent just before): its flash and report would be doubled.
  std::optional<net::PeerId> target;
  if (const auto at = m_shotAt.find(shot.npc);
      at != m_shotAt.end() && net::NowUs() - at->second.second < kShotAtPairUs)
    target = PeerOf(at->second.first);
  ShareNpc(shot, m_you, target.value_or(0));
}

void Endpoint::SendNpcEffect(const proto::NpcEffect &effect) {
  Record(effect);
  if (m_tap)
    m_tap(effect);
  if (!IsJoined() || m_ended)
    return;
  auto mine = effect;
  mine.player = m_you;
  AskOwner(mine.npc, mine);
}

bool Endpoint::SendPlayerHit(const proto::PlayerHit &hit) {
  Record(hit);
  if (!IsJoined() || m_ended || hit.player == m_you)
    return false; // our own V's hits are our game's
  // Not our NPC's hit to tell; one of no shared NPC only from the host.
  if (hit.npc == 0 ? !IsHost() : !SpeaksFor(hit.npc))
    return false;
  if (!IsHost()) {
    if (!m_players.contains(hit.player))
      return false;
    Send(m_hostPeer, hit); // the host passes it to that player's game
    return true;
  }
  const auto target = PeerOf(hit.player);
  // An NPC's hit only for a game that shows it (regions.md: linked with us).
  if (!target || (hit.npc != 0 && !SharedWith(hit.npc, hit.player)))
    return false;
  Send(*target, hit); // that player's game only
  return true;
}

void Endpoint::SendNpcHit(const proto::NpcHit &hit) {
  Record(hit);
  if (m_tap)
    m_tap(hit);
  if (!IsJoined() || m_ended)
    return;
  auto mine = hit;
  mine.player = m_you;
  AskOwner(mine.npc, mine);
}

void Endpoint::SendNpcHack(const proto::NpcHack &hack) {
  Record(hack);
  if (m_tap)
    m_tap(hack);
  if (!IsJoined() || m_ended)
    return;
  auto mine = hack;
  mine.player = m_you;
  AskOwner(mine.npc, mine);
}

void Endpoint::SendNpcHackState(const proto::NpcHackState &state) {
  Record(state);
  if (!IsJoined() || m_ended || !SpeaksFor(state.npc))
    return;
  SayNpc(state);
}

void Endpoint::SendNpcStatus(const proto::NpcStatus &status) {
  Record(status);
  if (!IsJoined() || m_ended || !SpeaksFor(status.npc))
    return;
  SayNpc(status);
}

void Endpoint::SendPlayerHack(const proto::PlayerHack &hack) {
  Record(hack);
  if (!IsJoined() || m_ended || hack.player == m_you || !SpeaksFor(hack.npc))
    return;
  if (!IsHost())
    Send(m_hostPeer, hack);
  else if (const auto peer = PeerOf(hack.player); peer && SharedWith(hack.npc, hack.player))
    Send(*peer, hack);
}

void Endpoint::SendVehicleHack(proto::VehicleHack hack) {
  Record(hack);
  if (m_tap)
    m_tap(hack);
  if (!IsJoined() || m_ended || hack.driver == m_you)
    return; // our own car is not a copy: nothing to send
  hack.player = m_you;
  if (!IsHost())
    Send(m_hostPeer, hack);
  else if (const auto peer = PeerOf(hack.driver))
    Send(*peer, hack);
}

bool Endpoint::SendNpcShotAt(proto::NpcShotAt shot) {
  Record(shot);
  if (!IsJoined() || m_ended || shot.player == m_you || !SpeaksFor(shot.npc))
    return false; // shots at our own V are our game's
  if (!IsHost()) {
    if (!m_players.contains(shot.player))
      return false;
    if (shot.timeUs == 0)
      shot.timeUs = HostNowUs();
    Send(m_hostPeer, shot); // the host passes it to that player's game
    return true;
  }
  const auto peer = PeerOf(shot.player);
  // Only at a V whose game shows the shooter (it fires the shot from its proxy).
  if (!peer || !SharedWith(shot.npc, shot.player))
    return false;
  // Behind the epoch fence: a guest that has not seen our world since it
  // was rebuilt is not in it to be shot at.
  if (const auto seen = m_seenHost.find(shot.player);
      seen != m_seenHost.end() && seen->second < m_epoch && net::NowUs() - m_epochAtUs < kFenceUs) {
    ++m_fenced;
    return false;
  }
  if (shot.timeUs == 0)
    shot.timeUs = HostNowUs();
  m_shotAt[shot.npc] = {shot.player, net::NowUs()};
  Send(*peer, shot);
  return true;
}

void Endpoint::SendShotHit(const proto::ShotHit &hit) {
  Record(hit);
  if (m_tap)
    m_tap(hit);
  if (!IsJoined() || m_ended)
    return;
  auto mine = hit;
  mine.player = m_you;
  AskOwner(mine.npc, mine);
}

std::optional<net::PeerId> Endpoint::PeerOf(proto::PlayerId player) const {
  for (const auto &[peer, id] : m_playerOfPeer)
    if (id == player)
      return peer;
  return std::nullopt;
}

void Endpoint::SendLocalVehicle(proto::VehicleState vehicle) {
  Record(vehicle);
  if (m_tap)
    m_tap(vehicle);
  if (!IsJoined() || m_ended)
    return;
  vehicle.player = m_you;
  vehicle.timeUs = HostNowUs();
  const auto due = m_compact
                       ? m_vehicleGate.Offer(vehicle, netcode::StateIntervalUs(NearestOther()))
                       : std::vector<proto::VehicleState>{vehicle};
  for (auto send : due) {
    send.seq = ++m_vehicleSeq;
    const auto wire = m_compact ? proto::Message(netcode::Compact(send)) : proto::Message(send);
    if (IsHost())
      Broadcast(wire);
    else
      Send(m_hostPeer, wire);
  }
}

void Endpoint::SendLocalShot(proto::Shot shot) {
  Record(shot);
  if (m_tap)
    m_tap(shot);
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
  Record(proto::Pause{m_you, paused});
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

void Endpoint::SetLocalPresence(proto::Presence presence) {
  if (m_tap)
    m_tap(presence);
  presence.revision = ++m_presenceRevision;
  m_localPresence = presence;
  SendLocalPresenceNow();
}

void Endpoint::SendLocalPresenceNow() {
  if (!m_localPresence || !IsJoined() || m_ended)
    return;
  m_localPresence->player = m_you;
  m_presences[m_you] = *m_localPresence;
  if (IsHost())
    Broadcast(*m_localPresence);
  else
    Send(m_hostPeer, *m_localPresence);
}

void Endpoint::SetLocalStatusVisuals(std::vector<std::uint64_t> records) {
  proto::StatusVisuals visuals{.records = presence::StatusSet(std::move(records))};
  if (m_localStatusVisuals && m_localStatusVisuals->records == visuals.records)
    return; // nothing new to show
  if (m_tap)
    m_tap(visuals);
  visuals.revision = ++m_statusVisualsRevision;
  m_localStatusVisuals = visuals;
  SendLocalStatusVisualsNow();
}

void Endpoint::SendLocalStatusVisualsNow() {
  if (!m_localStatusVisuals || !IsJoined() || m_ended)
    return;
  m_localStatusVisuals->player = m_you;
  m_statusVisuals[m_you] = *m_localStatusVisuals;
  if (IsHost())
    Broadcast(*m_localStatusVisuals);
  else
    Send(m_hostPeer, *m_localStatusVisuals);
}

void Endpoint::SendLocalCue(proto::Cue cue) {
  if (m_tap)
    m_tap(cue);
  if (!IsJoined() || m_ended)
    return;
  cue.player = m_you;
  cue.seq = ++m_cueSeq;
  if (IsHost())
    Broadcast(cue);
  else
    Send(m_hostPeer, cue);
}

void Endpoint::SetLocalStealth(proto::Stealth stealth) {
  Record(stealth);
  if (m_tap)
    m_tap(stealth);
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
  Record(awareness);
  if (!IsJoined() || m_ended)
    return;
  std::erase_if(awareness.entries,
                [&](const proto::NpcAwarenessEntry &entry) { return !SpeaksFor(entry.npc); });
  awareness.seq = ++m_awarenessSeq;
  awareness.timeUs = HostNowUs();
  if (awareness.entries.size() > proto::kMaxNpcAwareness)
    awareness.entries.resize(proto::kMaxNpcAwareness);
  if (IsHost() || !Linked().empty())
    SayNpc(awareness);
}

void Endpoint::SendNoise(const proto::Noise &noise) {
  Record(noise);
  if (m_tap)
    m_tap(noise);
  if (!IsJoined() || m_ended)
    return;
  // Our own NPCs hear it already; the games linked with us run others.
  auto mine = noise;
  mine.player = m_you;
  if (!IsHost()) {
    if (!Linked().empty())
      Send(m_hostPeer, mine);
    return;
  }
  for (const auto &[peer, player] : m_playerOfPeer)
    if (m_links.Linked(m_you, player))
      Send(peer, mine);
}

void Endpoint::SendRegroup(proto::Regroup offer) {
  Record(offer);
  if (!IsJoined() || m_ended)
    return;
  offer.from = m_you;
  if (offer.label.size() > proto::kMaxRegroupLabel)
    offer.label.resize(proto::kMaxRegroupLabel);
  if (offer.to == m_you)
    return;
  if (IsHost())
    Deliver(offer, offer.to, 0);
  else
    Send(m_hostPeer, offer);
}

void Endpoint::SendRegroupAnswer(proto::RegroupAnswer answer) {
  Record(answer);
  if (!IsJoined() || m_ended || answer.to == proto::kNoPlayer || answer.to == m_you)
    return;
  answer.from = m_you;
  if (IsHost())
    Deliver(answer, answer.to, 0);
  else
    Send(m_hostPeer, answer);
}

// Host: a guest's ping, tag, link consent or waypoint (comms.hpp), in its
// own name only.
void Endpoint::OnGuestComms(net::PeerId peer, proto::PlayerId player, const proto::Message &message,
                            std::vector<SessionEvent> &out) {
  const auto rate = [&](std::map<proto::PlayerId, comms::Limiter> &rates, double burst,
                        double perSecond) {
    auto &limiter = rates.try_emplace(player, burst, perSecond).first->second;
    if (limiter.Allow(net::NowUs()))
      return true;
    ++m_commsDropped;
    return false;
  };
  if (const auto *mark = std::get_if<proto::Mark>(&message)) {
    auto relayed = *mark;
    relayed.from = player;
    if (!rate(m_markRate, comms::kHostMarkBurst, comms::kHostMarksPerSecond))
      return;
    Broadcast(relayed, peer);
    out.push_back(events::MarkFrom{relayed});
  } else if (const auto *tag = std::get_if<proto::Tag>(&message)) {
    auto relayed = *tag;
    relayed.from = player;
    if (!rate(m_tagRate, comms::kHostTagBurst, comms::kHostTagsPerSecond))
      return;
    if (DeliverTag(relayed, peer))
      out.push_back(events::TagFrom{relayed});
  } else if (const auto *link = std::get_if<proto::Link>(&message)) {
    auto relayed = *link;
    relayed.player = player;
    if (!Keep(m_commsLinks, relayed))
      return; // a repeat: nothing new for anyone
    Broadcast(relayed, peer);
    out.push_back(events::RemoteLink{relayed});
  } else if (const auto *waypoint = std::get_if<proto::Waypoint>(&message)) {
    auto relayed = *waypoint;
    relayed.player = player;
    if (!Keep(m_waypoints, relayed))
      return;
    Broadcast(relayed, peer);
    out.push_back(events::RemoteWaypoint{relayed});
  }
}

// Host: a player back from a crash has no waypoint and no link until its
// new game says so (everyone hears it now; its new connection is spared).
void Endpoint::EndCommsOf(proto::PlayerId player, net::PeerId peer,
                          std::vector<SessionEvent> &out) {
  if (m_waypoints.contains(player)) {
    const proto::Waypoint none{.player = player};
    Broadcast(none, peer);
    out.push_back(events::RemoteWaypoint{none});
  }
  if (m_commsLinks.contains(player)) {
    const proto::Link none{.player = player};
    Broadcast(none, peer);
    out.push_back(events::RemoteLink{none});
  }
  ForgetComms(player);
}

bool Endpoint::SendMark(proto::Mark mark) {
  Record(mark);
  if (!IsJoined() || m_ended)
    return false;
  mark.from = m_you;
  if (IsHost())
    Broadcast(mark);
  else
    Send(m_hostPeer, mark);
  return true;
}

void Endpoint::SendTag(proto::Tag tag) {
  Record(tag);
  if (!IsJoined() || m_ended)
    return;
  tag.from = m_you;
  if (tag.targets.size() > proto::kMaxTagTargets)
    tag.targets.resize(proto::kMaxTagTargets);
  if (IsHost())
    DeliverTag(tag, 0);
  else
    Send(m_hostPeer, tag);
}

bool Endpoint::LinkedPair(proto::PlayerId from, proto::PlayerId to) const {
  if (from == to)
    return false;
  const auto consent = [&](proto::PlayerId who, proto::PlayerId other) {
    if (who == m_you && !IsHost())
      return comms::Consents(m_localLink ? &*m_localLink : nullptr, other);
    const auto it = m_commsLinks.find(who);
    return comms::Consents(it == m_commsLinks.end() ? nullptr : &it->second, other);
  };
  return consent(from, to) && consent(to, from);
}

bool Endpoint::DeliverTag(const proto::Tag &tag, net::PeerId except) {
  for (const auto &[peer, player] : m_playerOfPeer)
    if (peer != except && player != tag.from && LinkedPair(tag.from, player))
      Send(peer, tag);
  return tag.from != m_you && LinkedPair(tag.from, m_you);
}

void Endpoint::ForgetComms(proto::PlayerId player) {
  m_commsLinks.erase(player);
  m_waypoints.erase(player);
  m_markRate.erase(player);
  m_tagRate.erase(player);
}

void Endpoint::SetLocalLink(proto::Link link) {
  Record(link);
  link.revision = ++m_linkRevision;
  if (link.players.size() > proto::kMaxLinkPlayers)
    link.players.resize(proto::kMaxLinkPlayers);
  m_localLink = link;
  SendLocalLinkNow();
}

void Endpoint::SendLocalLinkNow() {
  if (!m_localLink || !IsJoined() || m_ended)
    return;
  m_localLink->player = m_you;
  if (IsHost()) {
    m_commsLinks[m_you] = *m_localLink;
    Broadcast(*m_localLink);
  } else {
    Send(m_hostPeer, *m_localLink);
  }
}

void Endpoint::SetLocalWaypoint(proto::Waypoint waypoint) {
  Record(waypoint);
  waypoint.revision = ++m_waypointRevision;
  m_localWaypoint = waypoint;
  SendLocalWaypointNow();
}

void Endpoint::SendLocalWaypointNow() {
  if (!m_localWaypoint || !IsJoined() || m_ended)
    return;
  m_localWaypoint->player = m_you;
  if (IsHost()) {
    m_waypoints[m_you] = *m_localWaypoint;
    Broadcast(*m_localWaypoint);
  } else {
    Send(m_hostPeer, *m_localWaypoint);
  }
}

void Endpoint::SetLocalVitals(proto::Vitals vitals) {
  Record(vitals);
  if (m_tap)
    m_tap(vitals);
  vitals.revision = ++m_vitalsRevision;
  m_localVitals = vitals;
  SendLocalVitalsNow();
}

void Endpoint::SendLocalVitalsNow() {
  if (!m_localVitals || !IsJoined() || m_ended)
    return;
  m_localVitals->player = m_you;
  m_lives[m_you] = *m_localVitals;
  if (IsHost()) {
    Broadcast(*m_localVitals);
    CheckLost(m_pending);
    if (m_localVitals->life != proto::Life::Up)
      RouteTrade(m_trades.Drop(m_you, proto::TradeReason::Down), m_pending);
  } else {
    Send(m_hostPeer, *m_localVitals);
  }
}

void Endpoint::SendRevive(proto::Revive revive) {
  Record(revive);
  if (m_tap)
    m_tap(revive);
  if (!IsJoined() || m_ended || revive.target == m_you)
    return;
  revive.player = m_you;
  if (!IsHost())
    Send(m_hostPeer, revive);
  else if (const auto peer = PeerOf(revive.target);
           peer &&
           (revive.step == proto::ReviveStep::Cancel || LifeOf(revive.target) == proto::Life::Down))
    Send(*peer, revive);
}

proto::Life Endpoint::LifeOf(proto::PlayerId player) const {
  const auto it = m_lives.find(player);
  return it == m_lives.end() ? proto::Life::Up : it->second.life;
}

void Endpoint::CheckLost(std::vector<SessionEvent> &out) {
  if (!IsHost() || m_ended || m_players.empty())
    return;
  const bool nobodyUp = std::ranges::none_of(
      m_players, [&](const auto &entry) { return LifeOf(entry.first) == proto::Life::Up; });
  if (!nobodyUp) {
    m_lost = false;
    return;
  }
  if (m_lost)
    return;
  m_lost = true;
  const proto::GameOver over{++m_losses};
  Broadcast(over);
  out.push_back(events::SessionLost{over});
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
  } else if (const auto *ask = std::get_if<proto::QuestInteract>(&message)) {
    OnGuestInteract(peer, player, *ask, out);
  }
  // The rest (journal, calls, lines, hubs, rewards) is the host's to say.
}

void Endpoint::OnGuestInteract(net::PeerId peer, proto::PlayerId player, proto::QuestInteract ask,
                               std::vector<SessionEvent> &out) {
  // A guest asks; only the host answers.
  if (ask.verdict != proto::kInteractAsk)
    return;
  ask.player = player;
  auto answer = ask;
  // Known for the session: the same answer, at once, without our game. (A
  // performed choice is done again by our game each time.)
  if (const auto known = m_interactKnown.Get(interact::KeyOf(ask));
      known && *known != proto::kInteractPerformed) {
    answer.verdict = *known;
    ++m_interactInstant;
    Send(peer, answer);
    return;
  }
  // First come, first served: another player's use of it is with our game,
  // or was answered a moment ago.
  switch (m_interactReferee.Ask(ask.target, player, ask.seq, net::NowUs())) {
  case interact::Referee::Claim::Granted:
    out.push_back(events::Story{ask});
    return;
  case interact::Referee::Claim::Taken:
    ++m_interactTaken;
    answer.verdict = proto::kInteractTaken;
    break;
  case interact::Referee::Claim::Full:
    answer.verdict = proto::kInteractBusy;
    break;
  }
  Send(peer, answer);
}

void Endpoint::SendStory(proto::Message message, proto::PlayerId to) {
  Record(message);
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
    else if (auto *ask = std::get_if<proto::QuestInteract>(&message)) {
      if (ask->verdict != proto::kInteractAsk)
        return;
      ask->player = m_you;
      // Known: host only or local is not asked again; our game hears the
      // known verdict at once (a performed one is asked each time).
      if (const auto known = m_interactKnown.Get(interact::KeyOf(*ask));
          known && *known != proto::kInteractPerformed) {
        auto answer = *ask;
        answer.verdict = *known;
        ++m_interactInstant;
        m_pending.push_back(events::Story{answer});
        return;
      }
    } else
      return;
    Send(m_hostPeer, message);
    return;
  }
  if (auto *answer = std::get_if<proto::QuestInteract>(&message)) {
    // Our game's verdict on a guest's question: to that guest alone, and
    // only while the question is still here (its asker did not vanish).
    if (answer->verdict == proto::kInteractAsk || to == proto::kNoPlayer || to == m_you)
      return;
    answer->player = to;
    if (!m_interactReferee.Answered(answer->target, to, answer->seq, answer->verdict, net::NowUs()))
      return;
    m_interactKnown.Note(interact::KeyOf(*answer), answer->verdict);
    if (const auto peer = PeerOf(to))
      Send(*peer, *answer);
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
  if (auto *restrictions = std::get_if<proto::StoryRestrictions>(&message)) {
    restrictions->player = m_you;
    // One set, said one way (Decode refuses any other on the guest's side).
    auto &effects = restrictions->effects;
    std::ranges::sort(effects);
    effects.erase(std::unique(effects.begin(), effects.end()), effects.end());
    if (effects.size() > proto::kMaxStoryRestrictions)
      effects.resize(proto::kMaxStoryRestrictions);
    // A repeat of the set the guests have is not sent again.
    if (!m_story.Note(message))
      return;
    Broadcast(message);
    return;
  }
  if (const auto *call = std::get_if<proto::PhoneCall>(&message)) {
    m_calls.Update(*call);
    message = *m_calls.Call();
  }
  // A vehicle is credited to the guests that took part, one by one.
  if (std::holds_alternative<proto::VehicleUnlock>(message) && to == proto::kNoPlayer)
    return;
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
  Record(effect);
  if (m_tap)
    m_tap(effect);
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

void Endpoint::SetLocalTransit(proto::Transit transit) {
  Record(transit);
  if (m_tap)
    m_tap(transit);
  transit.player = proto::kNoPlayer;
  transit.revision = 0;
  if (m_localTransit) {
    auto same = *m_localTransit;
    same.player = proto::kNoPlayer;
    same.revision = 0;
    if (same == transit)
      return;
  }
  transit.revision = ++m_transitRevision;
  m_localTransit = transit;
  SendLocalTransitNow();
}

void Endpoint::SendLocalTransitNow() {
  if (!m_localTransit || !IsJoined() || m_ended)
    return;
  m_localTransit->player = m_you;
  NoteTransit(*m_localTransit);
  if (IsHost())
    Broadcast(*m_localTransit);
  else
    Send(m_hostPeer, *m_localTransit);
}

void Endpoint::NoteTransit(const proto::Transit &transit) {
  if (transit.kind == proto::TransitKind::None)
    m_transits.erase(transit.player);
  else
    m_transits[transit.player] = transit;
}

void Endpoint::SetLocalHeat(proto::Heat heat) {
  Record(heat);
  if (m_tap)
    m_tap(heat);
  heat.stage = std::min(heat.stage, proto::kMaxHeat);
  if (heat.stage == 0)
    heat.state = proto::HeatState::Default;
  if (m_localHeat ? m_localHeat->stage == heat.stage && m_localHeat->state == heat.state
                  : heat.stage == 0)
    return; // as it was
  heat.revision = ++m_heatRevision;
  m_localHeat = heat;
  SendLocalHeatNow();
}

void Endpoint::SendLocalHeatNow() {
  if (!m_localHeat || !IsJoined() || m_ended)
    return;
  m_localHeat->player = m_you;
  NoteHeat(*m_localHeat);
  if (IsHost())
    Broadcast(*m_localHeat);
  else
    Send(m_hostPeer, *m_localHeat);
}

bool Endpoint::NoteHeat(const proto::Heat &heat) {
  const auto it = m_heat.find(heat.player);
  if (heat.stage == 0) {
    if (it == m_heat.end())
      return false;
    m_heat.erase(it);
    return true;
  }
  if (it != m_heat.end() && it->second.stage == heat.stage && it->second.state == heat.state)
    return false;
  m_heat[heat.player] = heat;
  return true;
}

void Endpoint::EndHeatOf(proto::PlayerId player, std::vector<SessionEvent> &out, bool broadcast) {
  const auto it = m_heat.find(player);
  if (it == m_heat.end())
    return;
  const proto::Heat off{.player = player, .revision = it->second.revision + 1};
  m_heat.erase(it);
  if (broadcast)
    Broadcast(off);
  out.push_back(events::RemoteHeat{off});
}

bool Endpoint::SendCrime(const proto::Crime &crime) {
  Record(crime);
  if (!OwnsArea() || !IsHost() || m_ended || crime.player == m_you)
    return false; // our own V's crimes are our game's
  const auto peer = PeerOf(crime.player);
  if (!peer)
    return false;
  Send(*peer, crime); // that player's game only
  return true;
}

void Endpoint::SendNpcSeat(const proto::NpcSeat &seat) {
  Record(seat);
  if (!IsJoined() || m_ended || !SpeaksFor(seat.npc))
    return;
  if (seat.owner == proto::kNoPlayer) {
    if (m_npcSeats.erase(seat.npc) == 0)
      return; // it was not seated: nothing to say
  } else {
    if (const auto it = m_npcSeats.find(seat.npc); it != m_npcSeats.end() && it->second == seat)
      return;
    m_npcSeats[seat.npc] = seat;
  }
  SayNpc(seat);
}

void Endpoint::DropWorldItem(proto::WorldItem item) {
  Record(item);
  if (!IsJoined() || m_ended)
    return;
  item.id = 0;
  item.dropper = m_you;
  if (IsHost())
    AddWorldItem(m_you, std::move(item), m_pending);
  else
    Send(m_hostPeer, item);
}

void Endpoint::TakeWorldItem(proto::WorldItemId id, std::uint8_t stack) {
  Record(proto::WorldItemTaken{.id = id, .stack = stack, .taker = m_you});
  if (!IsJoined() || m_ended)
    return;
  if (IsHost())
    ClaimWorldItem(m_you, id, stack, std::nullopt, m_pending);
  else
    Send(m_hostPeer, proto::WorldItemTaken{id, stack, m_you});
}

void Endpoint::SendSharedItem(proto::SharedItem item) {
  Record(item);
  if (!IsJoined() || m_ended || !item.item.record)
    return;
  item.player = m_you;
  // On the corpse of an NPC we ran: for whoever sees it. On another's
  // corpse, it is what our own V got.
  if (item.kind == proto::kSharedOnCorpse && SpeaksFor(item.npc)) {
    SayNpc(item);
    return;
  }
  item.kind = proto::kSharedLooted;
  item.npc = 0;
  if (IsHost())
    Broadcast(item);
  else
    Send(m_hostPeer, item);
}

void Endpoint::AddWorldItem(proto::PlayerId dropper, proto::WorldItem item,
                            std::vector<SessionEvent> &out) {
  // Bounded: a guest cannot fill the host's memory with bags.
  constexpr std::size_t kMaxWorldItems = 256;
  std::erase_if(item.stacks, [](const auto &stack) { return !stack.record || !stack.quantity; });
  if (item.stacks.empty() || item.stacks.size() > proto::kMaxWorldItemStacks ||
      m_worldItems.size() >= kMaxWorldItems)
    return;
  item.id = m_nextWorldItem++;
  item.dropper = dropper;
  m_worldItems[item.id] = item;
  m_worldTakers[item.id].assign(item.stacks.size(), proto::kNoPlayer);
  Broadcast(item); // the dropper too: it learns the id of its bag
  out.push_back(events::WorldItemAppeared{std::move(item)});
}

// First come, first served: the host's order of arrival decides.
void Endpoint::ClaimWorldItem(proto::PlayerId player, proto::WorldItemId id, std::uint8_t stack,
                              std::optional<net::PeerId> peer, std::vector<SessionEvent> &out) {
  const auto answer = [&](proto::PlayerId winner) {
    const proto::WorldItemTaken taken{id, stack, winner};
    if (peer)
      Send(*peer, taken);
    else
      out.push_back(events::WorldItemTakenBy{taken});
  };
  const auto item = m_worldItems.find(id);
  if (item == m_worldItems.end() || stack >= item->second.stacks.size()) {
    answer(proto::kNoPlayer); // gone (every stack taken), or never was
    return;
  }
  auto &takers = m_worldTakers[id];
  if (takers[stack] != proto::kNoPlayer) {
    answer(takers[stack]); // someone was first
    // ...and the bag as it really is: the claimer's copy may have missed
    // more than this stack (a lost WorldItemTaken: a ghost bag).
    if (peer)
      Send(*peer, item->second);
    return;
  }
  takers[stack] = player;
  item->second.stacks[stack].quantity = 0;
  const proto::WorldItemTaken taken{id, stack, player};
  Broadcast(taken);
  out.push_back(events::WorldItemTakenBy{taken});
  if (std::ranges::all_of(takers, [](auto taker) { return taker != proto::kNoPlayer; })) {
    m_worldItems.erase(item);
    m_worldTakers.erase(id);
  }
}

void Endpoint::ReportDevice(proto::DeviceState state) {
  if (IsHost() || !IsJoined())
    Record(state); // the host's word (a guest's game has none to say)
  if (!IsHost() || !IsJoined() || m_ended)
    return;
  const auto key = devices::KeyOf(state);
  if (const auto changed = m_devices.Report(state))
    Broadcast(*changed);
  // The guest whose request this answers: when our device did not become
  // what it asked (the game refused: sealed, unpowered), it gets ours back.
  const auto asked = m_deviceUses.find(key);
  if (asked == m_deviceUses.end())
    return;
  // ...and so does whoever asked for something else meanwhile.
  if (const auto *record = m_devices.Find(key)) {
    const auto answer = [&](proto::PlayerId player, const devices::What &what) {
      if (devices::WhatOf(*record) == what)
        return;
      if (const auto peer = PeerOf(player))
        Send(*peer, *record);
    };
    answer(asked->second.player, asked->second.what);
    for (const auto &[player, what] : asked->second.late)
      answer(player, what);
  }
  m_deviceUses.erase(asked);
}

void Endpoint::UseDevice(proto::DeviceUse use) {
  if (IsHost() || !IsJoined() || m_ended)
    return;
  const auto key = devices::KeyOf(use);
  const auto *record = m_devices.Find(key);
  if (record && devices::WhatOf(*record) == devices::WhatOf(use))
    return; // the session has it so already
  use.seen = record ? record->revision : 0;
  use.player = m_you;
  m_deviceAsked[key] = devices::WhatOf(use);
  Send(m_hostPeer, use);
}

void Endpoint::AskDevice(proto::DeviceState state) {
  if (IsHost() || !IsJoined() || m_ended)
    return;
  if (const auto *record = m_devices.Find(devices::KeyOf(state))) {
    if (devices::WhatOf(*record) != devices::WhatOf(state)) {
      auto again = *record;
      again.how = proto::kDeviceQuiet;
      m_pending.push_back(events::HostDevice{again});
    }
    return;
  }
  state.how = proto::kDeviceAsk;
  state.revision = 0;
  state.by = m_you;
  Send(m_hostPeer, state);
}

void Endpoint::SendFracture(proto::Fracture fracture) {
  Record(fracture);
  if (!IsJoined() || m_ended)
    return;
  fracture.player = m_you;
  if (IsHost())
    RelayFracture(fracture, 0, nullptr);
  else
    Send(m_hostPeer, fracture);
}

void Endpoint::RelayFracture(const proto::Fracture &fracture, net::PeerId from,
                             std::vector<SessionEvent> *out) {
  const bool ours = fracture.player == m_you;
  if (m_fractureRecord.Note(fracture)) {
    // A piece new to the session: every player's (its teller's too, for
    // its record), linked or not: one away from it breaks it on its way.
    Broadcast(fracture);
    if (out && !ours)
      out->push_back(events::Fractured{fracture});
    return;
  }
  if (!m_fractureRecord.Has(fracture)) {
    // The record is full: a guest's could not follow it.
    ++m_fracturesDropped;
    return;
  }
  // Another piece of one the session knows: where it broke is in the world
  // of the players linked with its teller.
  ShareNpc(fracture, fracture.player, from);
  if (out && !ours && Sees(m_you, fracture.player))
    out->push_back(events::Fractured{fracture});
}

void Endpoint::SendDeviceFire(proto::DeviceFire fire) {
  Record(fire);
  if (!IsJoined() || m_ended || fire.entity == 0)
    return;
  if (IsHost()) {
    // What the device does is ours to say; what a copy sees is a guest's.
    if (fire.kind > proto::DeviceFireKind::Shot)
      return;
    fire.player = proto::kNoPlayer;
    fire.timeUs = fire.kind == proto::DeviceFireKind::Shot ? HostNowUs() : 0;
    ShareNpc(fire, m_you);
    return;
  }
  if (fire.kind != proto::DeviceFireKind::Seen && fire.kind != proto::DeviceFireKind::Lost)
    return;
  fire.player = m_you;
  fire.target = {};
  fire.timeUs = 0;
  Send(m_hostPeer, fire);
}

// Host: a guest's copy of a turret locked that guest's V, or lost it. Kept
// (a repeat tells our game nothing), so a guest that vanishes is let go.
void Endpoint::OnDeviceSeen(proto::PlayerId player, proto::DeviceFire fire,
                            std::vector<SessionEvent> &out) {
  if (fire.entity == 0)
    return;
  auto &seen = m_deviceSeen[player];
  if (fire.kind == proto::DeviceFireKind::Seen) {
    if (seen.size() >= kMaxDevicesSeeing || !seen.insert(fire.entity).second)
      return;
  } else if (fire.kind != proto::DeviceFireKind::Lost || seen.erase(fire.entity) == 0) {
    return; // not a guest's to say, or a turret that never had it
  }
  fire.player = player;
  fire.target = {};
  fire.timeUs = 0;
  out.push_back(events::DeviceFired{fire});
}

void Endpoint::ForgetDevicesSeeing(proto::PlayerId player, std::vector<SessionEvent> &out) {
  const auto seen = m_deviceSeen.find(player);
  if (seen == m_deviceSeen.end())
    return;
  for (const auto entity : seen->second)
    out.push_back(events::DeviceFired{proto::DeviceFire{
        .entity = entity, .kind = proto::DeviceFireKind::Lost, .player = player}});
  m_deviceSeen.erase(seen);
}

void Endpoint::SendBreach(proto::Breach breach) {
  Record(breach);
  if (!IsJoined() || m_ended || (breach.entity == 0 && breach.npc == 0))
    return;
  if (breach.programs.size() > proto::kMaxBreachPrograms)
    breach.programs.resize(proto::kMaxBreachPrograms);
  if (breach.traps.size() > proto::kMaxBreachPrograms)
    breach.traps.resize(proto::kMaxBreachPrograms);
  breach.flags &= proto::kBreachRemote | proto::kBreachFailed;
  if (!IsHost()) {
    breach.player = m_you;
    Send(m_hostPeer, breach);
    return;
  }
  // Our game's word: ours, or (an NPC's network) the player who breached.
  const auto player = breach.player == proto::kNoPlayer ? m_you : breach.player;
  std::vector<SessionEvent> out;
  OnBreach(std::nullopt, player, std::move(breach), out);
  m_pending.insert(m_pending.end(), std::make_move_iterator(out.begin()),
                   std::make_move_iterator(out.end()));
}

// Host: a breach. The first of a network (or of an NPC) stands, the record
// of its access point says breached for everyone, every other game hears
// it for the NPCs of that network it runs; any later one is refused.
// `peer`: the breacher's connection (none: our own game said it).
void Endpoint::OnBreach(std::optional<net::PeerId> peer, proto::PlayerId player,
                        proto::Breach breach, std::vector<SessionEvent> &out) {
  breach.player = player;
  breach.flags &= proto::kBreachRemote | proto::kBreachFailed;
  if (breach.entity == 0 && breach.npc == 0)
    return;
  const devices::Key network{breach.entity, breach.cls};
  // Our own game ran this breach already (the minigame was ours): it is
  // not ours to apply again. Our game's word for another player is.
  const bool ranHere = !peer && player == m_you;
  const auto answer = [&](proto::BreachVerdict verdict, proto::PlayerId by) {
    const proto::BreachResult result{.entity = breach.entity,
                                     .cls = breach.cls,
                                     .npc = breach.npc,
                                     .verdict = verdict,
                                     .by = by};
    if (peer)
      Send(*peer, result);
    else if (player == m_you)
      out.push_back(events::BreachAnswered{result});
  };
  // The owner of the NPC breached through the quickhack, when not the
  // breacher's own game (which ran it).
  const auto npcOwner = breach.npc != 0 ? NpcOwnerOrAnnouncer(breach.npc) : proto::kNoPlayer;
  const bool npcElsewhere = breach.npc != 0 && npcOwner != player;
  auto applied = breach;
  applied.flags |= proto::kBreachApply;
  // Every game but the breacher's applies a network's breach to its own
  // NPCs of it; an NPC alone goes to its owner.
  const auto passOn = [&] {
    if (breach.entity != 0) {
      for (const auto &[to, other] : m_playerOfPeer)
        if (other != player)
          Send(to, applied);
      if (!ranHere)
        out.push_back(events::BreachBy{applied});
    } else if (npcElsewhere && RouteToOwner(breach.npc, applied, player) && !ranHere) {
      out.push_back(events::BreachBy{applied});
    }
  };
  if (breach.flags & proto::kBreachFailed) {
    // A lost minigame: nothing is breached; whose NPCs they are alert them.
    passOn();
    return;
  }
  auto verdict = proto::BreachVerdict::Granted;
  auto by = player;
  if (breach.entity != 0) {
    verdict = m_breaches.Judge(network, player, m_devices);
    by = m_breaches.By(network, m_devices);
  }
  if (verdict == proto::BreachVerdict::Granted && npcElsewhere) {
    verdict = m_breaches.JudgeNpc(breach.npc);
    by = m_breaches.ByNpc(breach.npc);
  }
  if (verdict != proto::BreachVerdict::Granted) {
    ++m_breachesRefused;
    answer(verdict, by);
    return;
  }
  if (breach.entity != 0) {
    m_breaches.Note(network, player);
    // Breached once, for everyone: the access point's record says so (its
    // late joiners and the device digest included), whatever our game
    // reports of it later.
    if (const auto changed =
            m_devices.Report(devices::BreachedState(network, player, m_devices.Find(network))))
      Broadcast(*changed);
  }
  if (npcElsewhere)
    m_breaches.NoteNpc(breach.npc, player);
  passOn();
  answer(proto::BreachVerdict::Granted, player);
}

// Host: a guest operated a device. First come, first served, by the
// revision it had seen (devices::Ledger::Judge).
void Endpoint::OnDeviceUse(net::PeerId peer, proto::PlayerId player, const proto::DeviceUse &use,
                           std::vector<SessionEvent> &out) {
  auto mine = use;
  mine.player = player;
  const auto key = devices::KeyOf(mine);
  // Someone else's request is with our game right now (both arrived within
  // a frame): that one was first. This one hears what the device became.
  if (const auto first = m_deviceUses.find(key);
      first != m_deviceUses.end() && first->second.player != player) {
    ++m_deviceStale;
    if (first->second.late.size() < kMaxGuests)
      first->second.late.emplace_back(player, devices::WhatOf(mine));
    return;
  }
  switch (m_devices.Judge(mine)) {
  case devices::Ledger::Verdict::Grant:
    m_deviceUses[key] = {player, devices::WhatOf(mine), net::NowUs(), {}};
    out.push_back(events::DeviceRequested{mine});
    break;
  case devices::Ledger::Verdict::Stale:
    ++m_deviceStale;
    Send(peer, *m_devices.Find(key)); // someone was first: the device as it is
    break;
  case devices::Ledger::Verdict::Same:
  case devices::Ledger::Verdict::Full:
    break;
  }
}

void Endpoint::SendDevicesTo(net::PeerId peer, bool resync) {
  if (resync)
    Send(peer, proto::DeviceSet{static_cast<std::uint32_t>(m_devices.Size())});
  for (const auto &[key, state] : m_devices.All()) {
    auto quiet = state;
    quiet.how = proto::kDeviceQuiet;
    Send(peer, quiet);
  }
}

void Endpoint::OnGuestCombat(proto::PlayerId player, const proto::Message &message,
                             std::vector<SessionEvent> &out) {
  if (const auto *strike = std::get_if<proto::NpcStrike>(&message)) {
    // For the NPC's owner: our game, or passed on.
    auto mine = *strike;
    mine.player = player;
    if (RouteToOwner(mine.npc, mine, player))
      out.push_back(events::Combat{mine});
  } else if (const auto *wound = std::get_if<proto::NpcWound>(&message)) {
    // An owner's word on its NPC (wounds, poise): for who sees it.
    const auto corpse = m_corpseOwner.find(wound->npc);
    if (NpcOwner(wound->npc) != player &&
        !(corpse != m_corpseOwner.end() && corpse->second == player))
      return;
    if (const auto peer = PeerOf(player))
      ShareNpc(message, player, *peer);
    if (Sees(m_you, player))
      out.push_back(events::Combat{message});
  } else if (const auto *poise = std::get_if<proto::NpcPoise>(&message)) {
    if (!m_npcs.contains(poise->npc) || NpcOwner(poise->npc) != player)
      return;
    m_npcPoise[poise->npc] = *poise;
    if (const auto peer = PeerOf(player))
      ShareNpc(message, player, *peer);
    if (Sees(m_you, player))
      out.push_back(events::Combat{message});
  } else if (const auto *scale = std::get_if<proto::NpcScale>(&message)) {
    // The scale of an NPC it runs: for who sees it, once.
    if (!m_npcs.contains(scale->npc) || NpcOwner(scale->npc) != player)
      return;
    if (const auto it = m_npcScale.find(scale->npc); it != m_npcScale.end() && it->second == *scale)
      return;
    m_npcScale[scale->npc] = *scale;
    if (const auto peer = PeerOf(player))
      ShareNpc(message, player, *peer);
    if (Sees(m_you, player))
      out.push_back(events::Combat{message});
  } else if (const auto *area = std::get_if<proto::AreaHit>(&message)) {
    // To the victim's game: ours, or pass it on (never back to the
    // attacker), while the session's rules allow friendly fire.
    if (!FriendlyFire())
      return;
    auto mine = *area;
    mine.from = player;
    if (mine.player == m_you)
      out.push_back(events::Combat{mine});
    else if (const auto peer = PeerOf(mine.player); peer && mine.player != player)
      Send(*peer, mine);
  } else if (const auto *thrown = std::get_if<proto::Throw>(&message)) {
    // Its copy on every other game: ours, and the other guests'.
    auto mine = *thrown;
    mine.player = player;
    out.push_back(events::Combat{mine});
    if (const auto peer = PeerOf(player))
      Broadcast(mine, *peer);
  } else if (const auto *end = std::get_if<proto::ThrowEnd>(&message)) {
    auto mine = *end;
    mine.player = player;
    out.push_back(events::Combat{mine});
    if (const auto peer = PeerOf(player))
      Broadcast(mine, *peer);
  } else if (const auto *path = std::get_if<proto::ThrowPath>(&message)) {
    auto mine = *path;
    mine.player = player;
    out.push_back(events::Combat{mine});
    if (const auto peer = PeerOf(player))
      Broadcast(mine, *peer);
  } else if (const auto *defended = std::get_if<proto::Defended>(&message)) {
    // For the attacker's owner: our game, or passed on.
    auto mine = *defended;
    mine.player = player;
    if (RouteToOwner(mine.npc, mine, player))
      out.push_back(events::Combat{mine});
  } else if (const auto *npcLevel = std::get_if<proto::NpcLevel>(&message)) {
    // A guest owner's word on its NPC's level: kept (late joiners), for who
    // sees it.
    if (!m_npcs.contains(npcLevel->npc) || NpcOwner(npcLevel->npc) != player)
      return;
    if (const auto it = m_npcLevels.find(npcLevel->npc);
        it != m_npcLevels.end() && it->second == *npcLevel)
      return;
    m_npcLevels[npcLevel->npc] = *npcLevel;
    if (const auto peer = PeerOf(player))
      ShareNpc(message, player, *peer);
    if (Sees(m_you, player))
      out.push_back(events::Combat{message});
  } else if (const auto *level = std::get_if<proto::PlayerLevel>(&message)) {
    // Its V's level: the session's is the mean of everyone's.
    m_levels[player] = level->level;
    UpdateSessionLevel(out);
  } else if (const auto *lend = std::get_if<proto::CarLend>(&message)) {
    // Its own cars: who says is the connection, never the message.
    auto mine = *lend;
    mine.player = player;
    NoteLend(mine, out);
  } else if (const auto *placed = std::get_if<proto::NpcPlaced>(&message)) {
    // An owner's word on its NPC, for who sees it.
    if (!m_npcs.contains(placed->npc) || NpcOwner(placed->npc) != player)
      return;
    if (const auto it = m_npcPlaced.find(placed->npc);
        it != m_npcPlaced.end() && it->second == *placed)
      return;
    m_npcPlaced[placed->npc] = *placed;
    if (const auto peer = PeerOf(player))
      ShareNpc(message, player, *peer);
    if (Sees(m_you, player))
      out.push_back(events::Combat{message});
  } else if (const auto *death = std::get_if<proto::NpcDeath>(&message)) {
    // A placed NPC of its own died: the ledger's, and everyone's to know.
    // Only its owner's word (its NpcGone follows), never one stamped.
    if (death->npc == 0 || death->order != 0 || !m_npcs.contains(death->npc) ||
        NpcOwner(death->npc) != player)
      return;
    RecordDeath(*death, player, out);
  } else if (const auto *vendor = std::get_if<proto::NpcVendor>(&message)) {
    // An owner's word on its NPC, for who sees it; a death is never a
    // guest's to say (the host derives it from the owner's NpcGone).
    if (vendor->flags != 0 || !m_npcs.contains(vendor->npc) || NpcOwner(vendor->npc) != player)
      return;
    if (const auto it = m_npcVendors.find(vendor->npc);
        it != m_npcVendors.end() && it->second == *vendor)
      return;
    m_npcVendors[vendor->npc] = *vendor;
    if (const auto peer = PeerOf(player))
      ShareNpc(message, player, *peer);
    if (Sees(m_you, player))
      out.push_back(events::Combat{message});
  }
  // The difficulty and the rules are the host's to say; wounds, poise and
  // levels their NPC's owner's (above).
}

std::set<proto::PlayerId> Endpoint::Present() const {
  std::set<proto::PlayerId> present;
  for (const auto &[player, info] : m_players)
    present.insert(player);
  return present;
}

// Host: the table's word, to each player it is for.
void Endpoint::RouteTrade(const std::vector<trade::Out> &outs, std::vector<SessionEvent> &out) {
  for (const auto &[to, message] : outs) {
    if (to == m_you)
      HearTrade(message, out);
    else if (const auto peer = PeerOf(to))
      Send(*peer, message);
  }
}

// The host's word for our seat. A commit is acknowledged every time it
// comes and reported to the game once: a trade is never applied twice.
void Endpoint::HearTrade(const proto::Message &message, std::vector<SessionEvent> &out) {
  if (const auto *commit = std::get_if<proto::TradeCommit>(&message)) {
    const bool fresh = m_tradesApplied.insert(commit->id).second;
    if (fresh)
      out.push_back(events::Trade{message});
    const proto::Trade applied{.id = commit->id, .from = m_you, .verb = proto::TradeVerb::Applied};
    if (IsHost())
      RouteTrade(m_trades.On(m_you, applied, net::NowUs(), Present()), out);
    else
      Send(m_hostPeer, applied);
    return;
  }
  out.push_back(events::Trade{message});
}

bool Endpoint::SendTrade(proto::Message message) {
  if (!IsJoined() || m_ended)
    return false;
  // Who says it is who we are.
  if (auto *trade = std::get_if<proto::Trade>(&message)) {
    trade->from = m_you;
    if (IsHost())
      RouteTrade(m_trades.On(m_you, *trade, net::NowUs(), Present()), m_pending);
    else
      Send(m_hostPeer, message);
    return true;
  }
  if (auto *offer = std::get_if<proto::TradeOffer>(&message)) {
    offer->player = m_you;
    if (IsHost())
      RouteTrade(m_trades.On(m_you, *offer, net::NowUs()), m_pending);
    else
      Send(m_hostPeer, message);
    return true;
  }
  return false; // a commit is the table's to send; anything else is not a trade
}

void Endpoint::SendLocalLevelNow() {
  if (!m_localLevel || !IsJoined() || m_ended)
    return;
  if (IsHost())
    UpdateSessionLevel(m_pending);
  else
    Send(m_hostPeer, proto::PlayerLevel{.player = m_you, .level = *m_localLevel});
}

void Endpoint::SendLocalLendNow() {
  if (!m_localLend || !IsJoined() || m_ended)
    return;
  const proto::CarLend lend{.player = m_you, .lend = *m_localLend};
  if (IsHost()) {
    if (const auto it = m_lends.find(m_you); it != m_lends.end() && it->second == lend)
      return;
    m_lends[m_you] = lend;
    Broadcast(lend);
  } else {
    Send(m_hostPeer, lend);
  }
}

bool Endpoint::NoteLend(const proto::CarLend &lend, std::vector<SessionEvent> &out) {
  if (const auto it = m_lends.find(lend.player); it != m_lends.end() && it->second == lend)
    return false;
  m_lends[lend.player] = lend;
  if (IsHost())
    Broadcast(lend);
  out.push_back(events::Combat{lend});
  return true;
}

void Endpoint::UpdateSessionLevel(std::vector<SessionEvent> &out) {
  if (!IsHost() || m_ended)
    return;
  if (m_localLevel)
    m_levels[m_you] = *m_localLevel;
  // A player that has not said its level yet is left out of the mean.
  std::vector<std::uint8_t> levels;
  for (const auto &[player, level] : m_levels)
    levels.push_back(level);
  if (levels.empty())
    return;
  const proto::SessionLevel session{.level = progress::MeanLevel(levels),
                                    .players = static_cast<std::uint8_t>(levels.size())};
  if (m_sessionLevel == session)
    return;
  m_sessionLevel = session;
  Broadcast(session);
  out.push_back(events::Combat{session});
}

bool Endpoint::SendCombat(proto::Message message) {
  Record(message);
  if (m_tap && play::Keeps(message))
    m_tap(message);
  if (const auto *level = std::get_if<proto::PlayerLevel>(&message)) {
    // Our V's: kept, and said as soon as we are in the session.
    if (level->level == 0 || level->level > proto::kMaxLevel)
      return false;
    m_localLevel = level->level;
    SendLocalLevelNow();
    return IsJoined() && !m_ended;
  }
  if (const auto *lend = std::get_if<proto::CarLend>(&message)) {
    if (lend->lend > proto::Lend::Never)
      return false;
    m_localLend = lend->lend;
    SendLocalLendNow();
    return IsJoined() && !m_ended;
  }
  if (!IsJoined() || m_ended || !proto::IsCombat(message))
    return false;
  // The session's level is the host's endpoint's to work out.
  if (std::holds_alternative<proto::SessionLevel>(message))
    return false;
  if (auto *area = std::get_if<proto::AreaHit>(&message)) {
    if (area->player == m_you)
      return false; // our own V's hits are our game's
    if (!FriendlyFire())
      return false; // the session's rules: a V never hurts another
    area->from = m_you;
    if (!IsHost()) {
      Send(m_hostPeer, message);
      return true;
    }
    const auto peer = PeerOf(area->player);
    if (peer)
      Send(*peer, message);
    return peer.has_value();
  }
  if (auto *strike = std::get_if<proto::NpcStrike>(&message)) {
    strike->player = m_you;
    if (!IsHost()) {
      Send(m_hostPeer, message);
      return true;
    }
    // The host's hit on a guest's NPC; on its own, its game's business.
    const auto owner = NpcOwner(strike->npc);
    if (owner == proto::kNoPlayer || owner == m_you)
      return false;
    RouteToOwner(strike->npc, message, m_you);
    return true;
  }
  // An owner's word on its NPC: wounds and poise.
  if (const auto *wound = std::get_if<proto::NpcWound>(&message)) {
    if (!SpeaksFor(wound->npc))
      return false;
    SayNpc(message);
    return true;
  }
  if (const auto *poise = std::get_if<proto::NpcPoise>(&message)) {
    if (!SpeaksFor(poise->npc))
      return false;
    m_npcPoise[poise->npc] = *poise;
    SayNpc(message);
    return true;
  }
  // An owner's word on its NPC's scale: once per change. One of our range
  // whose spawn is still on its way from the game (another queue): kept, and
  // said right after the spawn.
  if (const auto *scale = std::get_if<proto::NpcScale>(&message)) {
    if (!m_npcs.contains(scale->npc)) {
      if (regions::Announcer(scale->npc) != m_you)
        return false;
      if (m_npcScaleEarly.size() >= 256)
        m_npcScaleEarly.clear();
      m_npcScaleEarly[scale->npc] = *scale;
      return false;
    }
    if (!OwnsNpc(scale->npc))
      return false;
    if (const auto it = m_npcScale.find(scale->npc); it != m_npcScale.end() && it->second == *scale)
      return true; // said already
    m_npcScale[scale->npc] = *scale;
    SayNpc(message);
    return true;
  }
  if (auto *defended = std::get_if<proto::Defended>(&message)) {
    // To the attacker's owner (the host, or a guest for an NPC of its own,
    // regions.md); our own NPCs' attacks on our V are our game's.
    if (OwnsNpc(defended->npc) || NpcOwnerOrAnnouncer(defended->npc) == m_you)
      return false;
    defended->player = m_you;
    if (!IsHost()) {
      Send(m_hostPeer, message);
      return true;
    }
    RouteToOwner(defended->npc, message, m_you);
    return true;
  }
  if (const auto *level = std::get_if<proto::NpcLevel>(&message)) {
    // An owner's word on its NPC (the host's, or a guest's own: regions.md),
    // only when it changes. One we are about to share (its spawn is on its
    // way from the game by another queue): kept, and said right after the
    // spawn.
    if (m_you == proto::kNoPlayer)
      return false;
    if (!m_npcs.contains(level->npc)) {
      if (regions::Announcer(level->npc) != m_you || m_npcOwner.contains(level->npc))
        return false;
      if (m_npcLevelsEarly.size() >= 256)
        m_npcLevelsEarly.clear();
      m_npcLevelsEarly[level->npc] = *level;
      return false;
    }
    if (!OwnsNpc(level->npc))
      return false;
    if (const auto it = m_npcLevels.find(level->npc);
        it != m_npcLevels.end() && it->second == *level)
      return true;
    m_npcLevels[level->npc] = *level;
    SayNpc(message);
    return true;
  }
  // Our throws: to every other game (a guest's through the host).
  if (auto *thrown = std::get_if<proto::Throw>(&message)) {
    thrown->player = m_you;
    if (IsHost())
      Broadcast(message);
    else
      Send(m_hostPeer, message);
    return true;
  }
  if (auto *end = std::get_if<proto::ThrowEnd>(&message)) {
    end->player = m_you;
    if (IsHost())
      Broadcast(message);
    else
      Send(m_hostPeer, message);
    return true;
  }
  if (auto *path = std::get_if<proto::ThrowPath>(&message)) {
    path->player = m_you;
    if (IsHost())
      Broadcast(message);
    else
      Send(m_hostPeer, message);
    return true;
  }
  // A death of the ledger is the endpoint's to say (NoteDeath), never the game's.
  if (std::holds_alternative<proto::NpcDeath>(message))
    return false;
  // One of our NPCs is placed in the world.
  if (const auto *placed = std::get_if<proto::NpcPlaced>(&message)) {
    if (placed->npc == 0 || placed->entity == 0)
      return false;
    if (!m_npcs.contains(placed->npc)) {
      // Its spawn is on its way from the game by another queue: kept, and
      // said right after it.
      if (regions::Announcer(placed->npc) != m_you)
        return false;
      if (m_npcPlacedEarly.size() >= 256)
        m_npcPlacedEarly.clear();
      m_npcPlacedEarly[placed->npc] = *placed;
      return false;
    }
    if (!OwnsNpc(placed->npc))
      return false;
    if (const auto it = m_npcPlaced.find(placed->npc);
        it != m_npcPlaced.end() && it->second == *placed)
      return true; // said already
    m_npcPlaced[placed->npc] = *placed;
    SayNpc(message);
    return true;
  }
  // One of our NPCs is a placed vendor (never a death: the host's to derive).
  if (const auto *vendor = std::get_if<proto::NpcVendor>(&message)) {
    if (vendor->npc == 0 || vendor->entity == 0 || vendor->flags != 0)
      return false;
    if (!m_npcs.contains(vendor->npc)) {
      // Its spawn is on its way from the game by another queue: kept, and
      // said right after it.
      if (regions::Announcer(vendor->npc) != m_you)
        return false;
      if (m_npcVendorsEarly.size() >= 256)
        m_npcVendorsEarly.clear();
      m_npcVendorsEarly[vendor->npc] = *vendor;
      return false;
    }
    if (!OwnsNpc(vendor->npc))
      return false;
    if (const auto it = m_npcVendors.find(vendor->npc);
        it != m_npcVendors.end() && it->second == *vendor)
      return true; // said already
    m_npcVendors[vendor->npc] = *vendor;
    SayNpc(message);
    return true;
  }
  if (!IsHost())
    return false; // the host's rules
  if (const auto *difficulty = std::get_if<proto::Difficulty>(&message))
    m_difficulty = *difficulty;
  if (const auto *rules = std::get_if<proto::Rules>(&message)) {
    if (m_rules == *rules)
      return true; // said already
    m_rules = *rules;
  }
  Broadcast(message);
  return true;
}

void Endpoint::SetLocalElsewhere(proto::Elsewhere state) {
  Record(state);
  if (m_tap)
    m_tap(state);
  state = elsewhere::Sanitized(state);
  state.player = proto::kNoPlayer;
  if (state == m_localElsewhere)
    return;
  m_localElsewhere = state;
  SendLocalElsewhereNow();
}

void Endpoint::SendLocalElsewhereNow() {
  if (!IsJoined() || m_ended)
    return;
  auto mine = m_localElsewhere;
  mine.player = m_you;
  // A guest that just joined has something to say only if it is away.
  if (!NoteElsewhere(mine))
    return;
  if (IsHost())
    Broadcast(mine);
  else
    Send(m_hostPeer, mine);
}

bool Endpoint::NoteElsewhere(const proto::Elsewhere &state) {
  const auto it = m_elsewheres.find(state.player);
  if (state.place == proto::Place::City) {
    if (it == m_elsewheres.end())
      return false;
    m_elsewheres.erase(it);
    return true;
  }
  if (it != m_elsewheres.end() && it->second == state)
    return false;
  m_elsewheres[state.player] = state;
  return true;
}

void Endpoint::EndElsewhereOf(proto::PlayerId player, std::vector<SessionEvent> &out,
                              bool broadcast) {
  const proto::Elsewhere city{.player = player};
  if (!NoteElsewhere(city))
    return;
  if (broadcast)
    Broadcast(city);
  out.push_back(events::RemoteElsewhere{city});
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
    Broadcast(proto::Bye{reason});
  else if (m_hostPeer)
    Send(m_hostPeer, proto::Bye{reason});
  m_session->Flush();
  for (auto peer : m_session->Peers())
    m_session->Disconnect(peer);
  m_session->Poll(std::chrono::milliseconds{50});
  m_ended = true;
}

std::vector<proto::Message> Endpoint::LocalSnapshot() const {
  std::vector<proto::Message> out;
  if (m_localAppearance)
    out.emplace_back(*m_localAppearance);
  if (m_localHeld)
    out.emplace_back(*m_localHeld);
  if (m_localSeat)
    out.emplace_back(*m_localSeat);
  if (m_localRadio)
    out.emplace_back(*m_localRadio);
  if (m_localStealth)
    out.emplace_back(*m_localStealth);
  if (m_localPresence)
    out.emplace_back(*m_localPresence);
  if (m_localStatusVisuals)
    out.emplace_back(*m_localStatusVisuals);
  if (m_localTime.abilities)
    out.emplace_back(m_localTime);
  if (m_localTransit && m_localTransit->kind != proto::TransitKind::None)
    out.emplace_back(*m_localTransit);
  if (m_localHeat && m_localHeat->stage != 0)
    out.emplace_back(*m_localHeat);
  if (m_localVitals)
    out.emplace_back(*m_localVitals);
  if (m_localElsewhere != proto::Elsewhere{})
    out.emplace_back(m_localElsewhere);
  if (m_localActivity.kind != proto::ActivityKind::None)
    out.emplace_back(m_localActivity);
  return out;
}

void Endpoint::SetLocalActivity(proto::Activity state) {
  Record(state);
  if (m_tap)
    m_tap(state);
  state = activity::Sanitized(state);
  state.player = proto::kNoPlayer;
  if (state == m_localActivity)
    return;
  m_localActivity = state;
  SendLocalActivityNow();
}

void Endpoint::SendLocalActivityNow() {
  if (!IsJoined() || m_ended)
    return;
  auto mine = m_localActivity;
  mine.player = m_you;
  if (!NoteActivity(mine))
    return;
  if (IsHost())
    Broadcast(mine);
  else
    Send(m_hostPeer, mine);
}

bool Endpoint::NoteActivity(const proto::Activity &state) {
  const auto it = m_activities.find(state.player);
  if (state.kind == proto::ActivityKind::None) {
    if (it == m_activities.end())
      return false;
    m_activities.erase(it);
    return true;
  }
  if (it != m_activities.end() && it->second == state)
    return false;
  m_activities[state.player] = state;
  return true;
}

void Endpoint::EndActivityOf(proto::PlayerId player, std::vector<SessionEvent> &out,
                             bool broadcast) {
  const proto::Activity idle{.player = player};
  if (!NoteActivity(idle))
    return;
  if (broadcast)
    Broadcast(idle);
  out.push_back(events::RemoteActivity{idle});
}

std::uint32_t Endpoint::AskTimeSkip(proto::SkipReason reason, std::int32_t fromSeconds,
                                    std::int32_t toSeconds) {
  if (!IsJoined() || m_ended)
    return 0;
  const proto::TimeSkip skip{.from = m_you,
                             .request = ++m_skipRequests,
                             .reason = reason,
                             .fromSeconds = fromSeconds,
                             .toSeconds = toSeconds};
  Record(skip);
  if (IsHost()) {
    OnTimeSkip(skip, m_pending);
  } else {
    m_ourSkip = skip;
    Send(m_hostPeer, skip);
  }
  return skip.request;
}

void Endpoint::AnswerTimeSkip(proto::PlayerId asker, std::uint32_t request, bool accepted,
                              std::uint8_t why) {
  if (!IsJoined() || m_ended)
    return;
  const proto::TimeSkipVote vote{.from = m_you,
                                 .asker = asker,
                                 .request = request,
                                 .accepted = accepted,
                                 .why = accepted ? proto::kSkipNoReason : why};
  if (IsHost())
    OnTimeSkipVote(vote, m_pending);
  else
    Send(m_hostPeer, vote);
}

void Endpoint::WithdrawTimeSkip() {
  if (IsHost()) {
    if (const auto &open = m_skips.Open(); open && open->from == m_you)
      AnswerTimeSkip(m_you, open->request, false);
  } else if (m_ourSkip) {
    AnswerTimeSkip(m_you, m_ourSkip->request, false);
  }
}

activity::Blocker Endpoint::SkipBlocker() const {
  // Everyone must be free to let the hours pass: nobody down, nobody in a
  // time ability (its bubble's NPCs would jump hours), in another world or
  // in the pause menu.
  for (const auto &[id, vitals] : m_lives)
    if (vitals.life != proto::Life::Up)
      return {proto::kSkipDown, id};
  for (const auto &[id, time] : m_timeEffects)
    if (time.abilities & proto::kTimeBubbleAbilities)
      return {proto::kSkipTimeAbility, id};
  for (const auto &[id, away] : m_elsewheres)
    if (away.place != proto::Place::City)
      return {proto::kSkipElsewhere, id};
  if (!m_paused.empty())
    return {proto::kSkipPaused, *m_paused.begin()};
  return {};
}

void Endpoint::OnTimeSkip(proto::TimeSkip skip, std::vector<SessionEvent> &out) {
  std::set<proto::PlayerId> voters;
  for (const auto &[id, info] : m_players)
    if (id != skip.from)
      voters.insert(id);
  const bool fromHost = skip.from == m_you;
  const auto blocker =
      skip.reason == proto::SkipReason::Quest ? activity::Blocker{} : SkipBlocker();
  // Two beds at once: the second sleeper's request is refused (one at a
  // time) and is its yes to the open one, voted after the refusal so its
  // game knows it joined before the hours pass.
  std::optional<proto::TimeSkipVote> joins;
  if (const auto &open = m_skips.Open(); open && m_skips.Joins(skip))
    joins = proto::TimeSkipVote{
        .from = skip.from, .asker = open->from, .request = open->request, .accepted = true};
  if (const auto ended = m_skips.Ask(skip, fromHost, std::move(voters), blocker, net::NowUs())) {
    EndTimeSkip(*ended, false, out);
    if (joins)
      OnTimeSkipVote(*joins, out);
    return;
  }
  // Open: everyone else is asked (the host's own game too).
  for (const auto &[peer, player] : m_playerOfPeer)
    if (player != skip.from)
      Send(peer, skip);
  if (!fromHost)
    out.push_back(events::TimeSkipAsked{skip});
}

void Endpoint::OnTimeSkipVote(const proto::TimeSkipVote &vote, std::vector<SessionEvent> &out) {
  const auto &open = m_skips.Open();
  if (!open || open->from != vote.asker || open->request != vote.request)
    return;
  // The asker hears each answer (not its own way out).
  if (vote.from != vote.asker) {
    if (vote.asker == m_you)
      out.push_back(events::TimeSkipVoted{vote});
    else if (const auto peer = PeerOf(vote.asker))
      Send(*peer, vote);
  }
  if (const auto ended = m_skips.Vote(vote))
    EndTimeSkip(*ended, true, out);
}

void Endpoint::EndTimeSkip(const proto::TimeSkipResult &result, bool asked,
                           std::vector<SessionEvent> &out) {
  if (asked || result.outcome == proto::SkipOutcome::Applied) {
    Broadcast(result);
    out.push_back(events::TimeSkipEnded{result});
    return;
  }
  // Refused at once: the others never heard of it.
  if (result.asker == m_you)
    out.push_back(events::TimeSkipEnded{result});
  else if (const auto peer = PeerOf(result.asker))
    Send(*peer, result);
}
} // namespace tandem
