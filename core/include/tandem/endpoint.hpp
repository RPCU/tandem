#pragma once

// The co-op session on top of the transport: handshake, roster, clock sync and
// player state relay. The same code runs in the game plugin and in the mock
// player, as host or as guest.
//
// Topology: one host, up to kMaxGuests guests, star-shaped. Guests only talk to
// the host; the host relays what one guest sends to the others. Each player's
// game owns its own V (PlayerState); the host's game owns the shared world.

#include "tandem/protocol.hpp"
#include "tandem/session.hpp"
#include "tandem/story.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace tandem {
inline constexpr std::size_t kMaxGuests = 3;
// Connections beyond kMaxGuests: a player coming back while its old
// connection is still open, or one to tell that the session is full.
inline constexpr std::size_t kSpareConnections = 2;

// How soon a silent connection is given up. The defaults suit play; tests
// shorten them.
struct Timing {
  // Nothing heard for this long: the player, or the host, is gone
  // (net::kDefaultTimeout). A hiccup of a second or two passes.
  std::chrono::milliseconds timeout{net::kDefaultTimeout};
  // Host: a Hello under the name of a player whose connection has been
  // silent this long replaces that connection at once, keeping the player's
  // id, puppet and seat (a game back from a crash or a dropped link). A live
  // player is heard from several times a second, so nobody takes its place.
  std::chrono::milliseconds takeover{2'000};
};

namespace events {
// We are in a session (host: listening; guest: welcomed by the host).
struct Joined {
  proto::PlayerId you{};
  proto::PlayerId host{};
};
struct PlayerJoined {
  proto::PlayerInfo player;
};
struct PlayerLeft {
  proto::PlayerId player{};
};
// Host only: a player came back on a new connection before its old one timed
// out; same id, so its puppet and seat stay (no PlayerLeft, no PlayerJoined).
struct PlayerReturned {
  proto::PlayerInfo player;
};
struct RemoteState {
  proto::PlayerState state;
};
struct RemotePose {
  proto::PoseState pose;
};
struct RemoteAppearance {
  proto::Appearance appearance;
};
struct RemoteHeld {
  proto::Held held;
};
struct RemoteShot {
  proto::Shot shot;
};
struct RemoteSeat {
  proto::Seat seat;
};
struct RemoteVehicle {
  proto::VehicleState vehicle;
};
// The host's NPCs (guests only).
struct HostNpcSpawn {
  proto::NpcSpawn spawn;
};
struct HostNpcStates {
  proto::NpcStates states;
};
struct HostNpcGone {
  proto::NpcGone gone;
};
struct HostNpcHealth {
  proto::NpcHealth health;
};
// A guest hit one of our NPCs (host only).
struct NpcHitBy {
  proto::NpcHit hit;
};
struct NpcEffectBy {
  proto::NpcEffect effect;
};
// One of the host's NPCs hit our V (guests only; never another player's hit).
struct HitByNpc {
  proto::PlayerHit hit;
};
// Quickhacks. Host: a guest started an upload on one of our NPCs.
struct NpcHackBy {
  proto::NpcHack hack;
};
// Guests: an upload on a host NPC started or ended (anyone's, ours too).
struct HostNpcHackState {
  proto::NpcHackState state;
};
// Guests: a quickhack effect came onto or left a host NPC (never our own).
struct HostNpcStatus {
  proto::NpcStatus status;
};
// Guests: a host NPC netrunner hacks our V.
struct HostPlayerHack {
  proto::PlayerHack hack;
};
// The driver's game (host or guest): someone hacked the car we drive.
struct VehicleHackOn {
  proto::VehicleHack hack;
};
// The host's world changed (guests only).
struct HostWorld {
  proto::World world;
};
// Another player opened (paused) or closed their pause menu, or left while
// in it (paused false). Never reported for our own.
struct PauseChanged {
  proto::Pause pause;
};
// A story message (proto::IsStory) for this game. Host: from a guest,
// stamped with its player (PhoneAction only when it is the answer that
// counts, story::CallArbiter). Guest: from the host.
struct Story {
  proto::Message message;
};
// Another player's time ability started, changed or ended (abilities 0),
// or they left while it was on. Never reported for our own.
struct RemoteTimeEffect {
  proto::TimeEffect effect;
};
// The session is over (refused, host gone, timed out).
struct Ended {
  std::string reason;
};
// The radio of the car another player drives changed (or was sent again
// for a passenger getting in). Never reported for our own.
struct RemoteVehicleRadio {
  proto::VehicleRadio radio;
};
// Another player's stealth changed (camo, visibility, crouch).
struct RemoteStealth {
  proto::Stealth stealth;
};
// How far the host's NPCs are in noticing each guest (guests only).
struct HostNpcAwareness {
  proto::NpcAwareness awareness;
};
// A guest's V made a noise for our NPCs to hear (host only).
struct NoiseBy {
  proto::Noise noise;
};
} // namespace events

// Who this game is in the session.
struct Identity {
  std::string name;
  proto::Body body{};
};

using SessionEvent = std::variant<
    events::Joined, events::PlayerJoined, events::PlayerLeft, events::RemoteState,
    events::RemotePose, events::RemoteAppearance, events::RemoteHeld, events::RemoteShot,
    events::RemoteSeat, events::RemoteVehicle, events::HostWorld, events::HostNpcSpawn,
    events::HostNpcStates, events::HostNpcGone, events::HostNpcHealth, events::NpcHitBy,
    events::NpcEffectBy, events::PauseChanged, events::Ended, events::PlayerReturned,
    events::RemoteVehicleRadio, events::HitByNpc, events::RemoteStealth, events::HostNpcAwareness,
    events::NoiseBy, events::NpcHackBy, events::HostNpcHackState, events::HostNpcStatus,
    events::HostPlayerHack, events::VehicleHackOn, events::Story, events::RemoteTimeEffect>;

class Endpoint {
public:
  static std::unique_ptr<Endpoint> Host(Identity identity, std::uint16_t port, std::string &error,
                                        Timing timing = {});
  static std::unique_ptr<Endpoint> Join(Identity identity, const std::string &host,
                                        std::uint16_t port, std::string &error, Timing timing = {});

  // Services the network; call every frame or tick. Returns what happened.
  std::vector<SessionEvent> Update(std::chrono::milliseconds wait = std::chrono::milliseconds{0});

  // Our own V. `timeUs` is filled with the host clock.
  void SendLocalState(proto::PlayerState state);
  // Our own V's body pose (tandem/pose.hpp). `timeUs` is filled with the host clock.
  void SendLocalPose(proto::PoseState pose);
  // Our own V's look. Kept: sent now if we are in the session, else as soon
  // as we are; the host also hands it to players who join later.
  void SetLocalAppearance(proto::Appearance appearance);
  // What we hold (kept and handed to late joiners like the look), and shots.
  void SetLocalHeld(proto::Held held);
  void SendLocalShot(proto::Shot shot);
  // Where we sit (kept and handed to late joiners), and the car we drive.
  void SetLocalSeat(proto::Seat seat);
  void SendLocalVehicle(proto::VehicleState vehicle);
  // The radio of the car we drive: sent now (each call, even unchanged: a
  // passenger getting in is sent it again), kept and handed to late joiners.
  void SetLocalVehicleRadio(proto::VehicleRadio radio);
  // Host only: the world every guest follows (kept for late joiners).
  void SetWorld(proto::World world);
  // Host only: its NPCs (spawns kept for late joiners until gone).
  void SendNpcSpawn(const proto::NpcSpawn &spawn);
  void SendNpcStates(proto::NpcStates states);
  void SendNpcGone(const proto::NpcGone &gone);
  // Host only: an NPC's health changed (kept for late joiners until gone).
  void SendNpcHealth(const proto::NpcHealth &health);
  // Guest only: we hit one of the host's NPCs.
  void SendNpcHit(const proto::NpcHit &hit);
  void SendNpcEffect(const proto::NpcEffect &effect);
  // Host only: one of our NPCs hit `hit.player`'s V (its puppet here). Sent
  // to that player alone; false when it is not a guest in the session.
  bool SendPlayerHit(const proto::PlayerHit &hit);
  // Quickhacks (docs/ARCHITECTURE.md). Guest only: we started uploading on a
  // host NPC. Host only: uploads and quickhack effects on our NPCs, to
  // everyone; an NPC netrunner's hack on a guest's V, to that guest only.
  void SendNpcHack(const proto::NpcHack &hack);
  void SendNpcHackState(const proto::NpcHackState &state);
  void SendNpcStatus(const proto::NpcStatus &status);
  void SendPlayerHack(const proto::PlayerHack &hack);
  // We hacked the car `hack.driver` drives: to its driver's game, through
  // the host (which stamps the hacker).
  void SendVehicleHack(proto::VehicleHack hack);
  // Our pause menu is open (paused) or closed. Kept: sent now if we are in
  // the session, else as soon as we are.
  void SetLocalPaused(bool paused);
  // Story (milestone 6, proto::IsStory). Host: to every guest (to: one
  // guest), kept for late joiners (story::Record). Guest: to the host, which
  // relays ChoiceCursor and StoryState. Anything else is dropped.
  void SendStory(proto::Message message, proto::PlayerId to = proto::kNoPlayer);
  // The players in their pause menu, as this game knows them (us included).
  const std::set<proto::PlayerId> &Paused() const { return m_paused; }
  // Our V's time ability (Sandevistan...). Kept: sent now if we are in the
  // session, else as soon as we are; the host hands everyone's to late joiners.
  void SetLocalTimeEffect(proto::TimeEffect effect);
  // Every active time ability this game knows of (us included), by player.
  const std::map<proto::PlayerId, proto::TimeEffect> &TimeEffects() const { return m_timeEffects; }
  // How hard our V is to see (kept: sent now if we are in the session, else
  // as soon as we are; the host hands everyone's to late joiners).
  void SetLocalStealth(proto::Stealth stealth);
  // Host only: how far its NPCs are in noticing each guest.
  void SendNpcAwareness(proto::NpcAwareness awareness);
  // Guest only: a noise our V made, for the host's NPCs.
  void SendNoise(const proto::Noise &noise);

  // Time on the host's clock (the session's shared timeline).
  std::uint64_t HostNowUs() const { return net::NowUs() + m_clockOffsetUs; }

  bool IsHost() const { return m_session->IsHost(); }
  bool IsJoined() const { return m_you != proto::kNoPlayer; }
  proto::PlayerId You() const { return m_you; }
  proto::PlayerId HostPlayer() const { return m_host; } // kNoPlayer until welcomed
  const std::map<proto::PlayerId, proto::PlayerInfo> &Players() const { return m_players; }
  std::optional<net::Stats> HostLinkStats() const;
  // The link to a player: host, to each guest; guest, to the host only
  // (the others are reached through it). nullopt for ourselves and strangers.
  std::optional<net::Stats> LinkStatsOf(proto::PlayerId player) const;

  void Leave(const std::string &reason = "left");

private:
  Endpoint(std::unique_ptr<net::Session> session, Identity identity, Timing timing);

  void OnHostEvent(const net::Event &event, std::vector<SessionEvent> &out);
  void OnGuestEvent(const net::Event &event, std::vector<SessionEvent> &out);
  void OnHello(net::PeerId peer, const proto::Hello &hello, std::vector<SessionEvent> &out);
  void RemovePlayer(net::PeerId peer, std::vector<SessionEvent> &out);
  void SendSessionTo(net::PeerId peer);
  void BroadcastRoster();
  // Host: the connection of a welcomed player (nullopt: none, or us).
  std::optional<net::PeerId> PeerOf(proto::PlayerId player) const;

  std::unique_ptr<net::Session> m_session;
  Identity m_identity;
  Timing m_timing;

  proto::PlayerId m_you{proto::kNoPlayer};
  proto::PlayerId m_host{proto::kNoPlayer};
  std::map<proto::PlayerId, proto::PlayerInfo> m_players;

  // Host: which connection is which player (only after a valid Hello).
  std::map<net::PeerId, proto::PlayerId> m_playerOfPeer;
  proto::PlayerId m_nextPlayer{2};

  // Guest: the host connection, and the clock offset from the best ping.
  net::PeerId m_hostPeer{};
  std::int64_t m_clockOffsetUs{};
  std::uint64_t m_bestRttUs{UINT64_MAX};
  std::uint64_t m_lastPingUs{};
  std::uint32_t m_seq{};
  std::uint32_t m_poseSeq{};

  // Our look, and (host) everyone's latest, for late joiners.
  std::optional<proto::Appearance> m_localAppearance;
  std::uint32_t m_appearanceRevision{};
  std::map<proto::PlayerId, proto::Appearance> m_appearances;
  void SendLocalAppearanceNow();
  std::optional<proto::Held> m_localHeld;
  std::uint32_t m_heldRevision{};
  std::map<proto::PlayerId, proto::Held> m_held;
  std::uint32_t m_shotSeq{};
  void SendLocalHeldNow();
  std::optional<proto::Seat> m_localSeat;
  std::uint32_t m_seatRevision{};
  std::map<proto::PlayerId, proto::Seat> m_seats;
  std::uint32_t m_vehicleSeq{};
  void SendLocalSeatNow();
  std::optional<proto::VehicleRadio> m_localRadio;
  std::uint32_t m_radioRevision{};
  std::map<proto::PlayerId, proto::VehicleRadio> m_radios;
  void SendLocalRadioNow();
  std::optional<proto::World> m_world;
  std::uint32_t m_worldRevision{};
  std::map<proto::NpcId, proto::NpcSpawn> m_npcs;
  std::map<proto::NpcId, proto::NpcHealth> m_npcHealth;
  std::uint32_t m_npcSeq{};
  // Who is in the pause menu (host: the session's record, for late joiners).
  bool m_localPaused{};
  std::set<proto::PlayerId> m_paused;
  void SendLocalPausedNow();
  // Records a player's pause state; false when it did not change.
  bool NotePaused(proto::PlayerId player, bool paused);
  // Active time abilities (host: the session's record, for late joiners).
  proto::TimeEffect m_localTime;
  std::map<proto::PlayerId, proto::TimeEffect> m_timeEffects;
  void SendLocalTimeNow();
  // Records a player's time ability; false when it did not change.
  bool NoteTime(const proto::TimeEffect &effect);
  // A player is gone (or back from a crash): its ability ends for everyone.
  void EndTimeOf(proto::PlayerId player, std::vector<SessionEvent> &out, bool broadcast);
  // Our stealth, and (host) everyone's latest, for late joiners.
  std::optional<proto::Stealth> m_localStealth;
  std::uint32_t m_stealthRevision{};
  std::map<proto::PlayerId, proto::Stealth> m_stealth;
  void SendLocalStealthNow();
  std::uint32_t m_awarenessSeq{};
  // Host: the story for late joiners, and who answered the call first.
  story::Record m_story;
  story::CallArbiter m_calls;
  void OnGuestStory(net::PeerId peer, proto::PlayerId player, const proto::Message &message,
                    std::vector<SessionEvent> &out);
  bool m_ended{};
};
} // namespace tandem
