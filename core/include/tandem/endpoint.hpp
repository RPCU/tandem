#pragma once

// The co-op session on top of the transport: handshake, roster, clock sync and
// player state relay. The same code runs in the game plugin and in the mock
// player, as host or as guest.
//
// Topology: one host, up to kMaxGuests guests, star-shaped. Guests only talk to
// the host; the host relays what one guest sends to the others. Each player's
// game owns its own V (PlayerState); the host's game owns the shared world.

#include "tandem/consistency.hpp"
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
  // Host: how often each guest gets a digest of the shared state
  // (docs/ARCHITECTURE.md, Consistency). Guest: how soon the same domain may
  // be asked for again after a resync.
  std::chrono::milliseconds digest{3'000};
  std::chrono::milliseconds resyncCooldown{5'000};
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
struct HostNpcAnims {
  proto::NpcAnims anims;
};
struct HostNpcShot {
  proto::NpcShot shot;
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
// A guest's grapple, takedown, finisher or carry request (host only), and
// the host's answer to ours (guests only).
struct NpcActionBy {
  proto::NpcAction action;
};
struct NpcActionAnswered {
  proto::NpcActionReply reply;
};
// Who holds or animates one of the host's NPCs (guests only).
struct HostNpcHold {
  proto::NpcHold hold;
};
// Our half of an NPC's reaction to our V (guests only).
struct HostNpcCue {
  proto::NpcCue cue;
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
// A hit outcome message (proto::IsCombat) for this game. Host: a guest's
// NpcStrike or AreaHit on our V, stamped. Guest: the host's NpcWound,
// NpcPoise, Difficulty, an AreaHit on our V.
struct Combat {
  proto::Message message;
};
// Another player went into another world, changed it or came back (place
// City), or left while away. Never reported for our own.
struct RemoteElsewhere {
  proto::Elsewhere state;
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
// Another player offers us to come to them (Regroup), or answers ours. Only
// ever what is meant for us (to us, or to everyone).
struct RegroupOffered {
  proto::Regroup offer;
};
struct RegroupAnswered {
  proto::RegroupAnswer answer;
};
// Loot: a shared world item lies in the world (numbered by the host; ours
// too, with our token), and who picked up one of its stacks (our own
// claims included: taker is us when the host granted it).
struct WorldItemAppeared {
  proto::WorldItem item;
};
struct WorldItemTakenBy {
  proto::WorldItemTaken taken;
};
// Another player got a quest or important item (or, guests: one lies on a
// host NPC's corpse). Never reported for our own.
struct SharedItemFrom {
  proto::SharedItem item;
};
// Down and out. Another player's V went up, down or out (never our own; a
// player who leaves is forgotten without one).
struct RemoteVitals {
  proto::Vitals vitals;
};
// Someone is reviving our downed V (target is us), stamped by the host.
struct ReviveBy {
  proto::Revive revive;
};
// No V is up any more: the session is lost (host: our own arbitration).
struct SessionLost {
  proto::GameOver over;
};
// Consistency. Guest: our copy of these domains (kDomain* bits) differed
// from the host's digest twice in a row; we asked for them again (player:
// us). Host: `player` asked, and was sent them.
struct Resync {
  proto::PlayerId player{};
  std::uint8_t domains{};
};
// Another game's world was rebuilt around its V (loading screen, fast
// travel): its epoch moved on. Never reported for our own, nor for a join.
struct EpochChanged {
  proto::Epoch epoch;
};
// Another player's ride (metro, a ride, a scene vehicle) started, changed
// or ended (kind None). Never reported for our own.
struct RemoteTransit {
  proto::Transit transit;
};
// One of the host's NPCs got into, or out of, a player's vehicle (guests only).
struct HostNpcSeat {
  proto::NpcSeat seat;
};
} // namespace events

// Who this game is in the session.
struct Identity {
  std::string name;
  proto::Body body{};
};

// Where a guest finds its host: directly, through a relay, or directly
// first and through the relay when that fails.
struct JoinTarget {
  // The host's address ("": none, through the relay only).
  std::string address;
  std::uint16_t port{proto::kDefaultPort};
  // A relay and the session's join code or secret (tandem/relay.hpp). With
  // an address too, the relay is tried when the host has not answered
  // directly (NAT) within half the timeout; the switch is not reported.
  std::optional<net::RelayOptions> relay;
};

using SessionEvent = std::variant<
    events::Joined, events::PlayerJoined, events::PlayerLeft, events::RemoteState,
    events::RemotePose, events::RemoteAppearance, events::RemoteHeld, events::RemoteShot,
    events::RemoteSeat, events::RemoteVehicle, events::HostWorld, events::HostNpcSpawn,
    events::HostNpcStates, events::HostNpcGone, events::HostNpcHealth, events::NpcHitBy,
    events::NpcEffectBy, events::PauseChanged, events::Ended, events::PlayerReturned,
    events::RemoteVehicleRadio, events::HitByNpc, events::RemoteStealth, events::HostNpcAwareness,
    events::NoiseBy, events::NpcHackBy, events::HostNpcHackState, events::HostNpcStatus,
    events::HostPlayerHack, events::VehicleHackOn, events::Story, events::RemoteTimeEffect,
    events::NpcActionBy, events::NpcActionAnswered, events::HostNpcHold, events::HostNpcCue,
    events::RegroupOffered, events::RegroupAnswered, events::WorldItemAppeared,
    events::WorldItemTakenBy, events::SharedItemFrom, events::RemoteVitals, events::ReviveBy,
    events::SessionLost, events::HostNpcAnims, events::HostNpcShot, events::Combat, events::Resync,
    events::EpochChanged, events::RemoteElsewhere, events::RemoteTransit, events::HostNpcSeat>;

class Endpoint {
public:
  // With `relay`, guests can also join through it, by the join code the
  // relay gives (Relay()->code).
  static std::unique_ptr<Endpoint> Host(Identity identity, std::uint16_t port, std::string &error,
                                        Timing timing = {},
                                        std::optional<net::RelayOptions> relay = std::nullopt);
  static std::unique_ptr<Endpoint> Join(Identity identity, const JoinTarget &target,
                                        std::string &error, Timing timing = {});
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
  // Host only: NPCs whose animation state changed (each NPC's latest kept
  // for late joiners until gone), and one of their shots.
  void SendNpcAnims(const proto::NpcAnims &anims);
  void SendNpcShot(const proto::NpcShot &shot);
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
  // Guest only: we grab, take down, finish or carry one of the host's NPCs.
  void SendNpcAction(const proto::NpcAction &action);
  // Host only: the answer to `player`'s NpcAction, to that player alone.
  void SendNpcActionReply(proto::PlayerId player, const proto::NpcActionReply &reply);
  // Host only: who holds an NPC now (kept for late joiners until released,
  // gone, or the holder leaves).
  void SendNpcHold(const proto::NpcHold &hold);
  // Host only: an NPC's reaction for one player's game (cue.player).
  void SendNpcCue(const proto::NpcCue &cue);
  // The NPCs held now, as this game knows them (host: the session's record).
  const std::map<proto::NpcId, proto::NpcHold> &NpcHolds() const { return m_npcHolds; }
  // Our pause menu is open (paused) or closed. Kept: sent now if we are in
  // the session, else as soon as we are.
  void SetLocalPaused(bool paused);
  // Story (milestone 6, proto::IsStory). Host: to every guest (to: one
  // guest), kept for late joiners (story::Record). Guest: to the host, which
  // relays ChoiceCursor and StoryState. Anything else is dropped.
  void SendStory(proto::Message message, proto::PlayerId to = proto::kNoPlayer);
  // Hit outcomes (proto::IsCombat). Guest: NpcStrike to the host. Host:
  // NpcWound, NpcPoise, Difficulty to every guest (the last two kept for
  // late joiners). Either: AreaHit to its victim's game, through the host.
  // False when nothing was sent (not ours to say, nobody to send to).
  bool SendCombat(proto::Message message);
  // The players in their pause menu, as this game knows them (us included).
  const std::set<proto::PlayerId> &Paused() const { return m_paused; }
  // Our V's time ability (Sandevistan...). Kept: sent now if we are in the
  // session, else as soon as we are; the host hands everyone's to late joiners.
  void SetLocalTimeEffect(proto::TimeEffect effect);
  // Every active time ability this game knows of (us included), by player.
  const std::map<proto::PlayerId, proto::TimeEffect> &TimeEffects() const { return m_timeEffects; }
  // Our V in another world (tandem/elsewhere.hpp). Kept: sent now if we are
  // in the session, else as soon as we are; the host hands everyone's to
  // late joiners.
  void SetLocalElsewhere(proto::Elsewhere state);
  // Every player not in the city, as this game knows them (us included).
  const std::map<proto::PlayerId, proto::Elsewhere> &Elsewheres() const { return m_elsewheres; }
  // How hard our V is to see (kept: sent now if we are in the session, else
  // as soon as we are; the host hands everyone's to late joiners).
  void SetLocalStealth(proto::Stealth stealth);
  // Host only: how far its NPCs are in noticing each guest.
  void SendNpcAwareness(proto::NpcAwareness awareness);
  // Guest only: a noise our V made, for the host's NPCs.
  void SendNoise(const proto::Noise &noise);
  // Our V's ride (kind None: none). Kept: sent now if we are in the session,
  // else as soon as we are; the host hands everyone's to late joiners.
  void SetLocalTransit(proto::Transit transit);
  // Every ride this game knows of, by player (host: the session's record).
  const std::map<proto::PlayerId, proto::Transit> &Transits() const { return m_transits; }
  // Host only: one of our NPCs got into (owner set) or out of a player's
  // vehicle; kept while it sits, for late joiners.
  void SendNpcSeat(const proto::NpcSeat &seat);
  // Offer `offer.to` (kNoPlayer: everyone else) to come to us, or answer an
  // offer. `from` is filled in; nothing is kept.
  void SendRegroup(proto::Regroup offer);
  void SendRegroupAnswer(proto::RegroupAnswer answer);
  // Loot: our V dropped a bag of stacks (id ignored: the host numbers it;
  // `token` is ours, to know it again), or picked up a stack of a world item
  // (a claim: the host grants the first one). Each comes back as an event.
  void DropWorldItem(proto::WorldItem item);
  void TakeWorldItem(proto::WorldItemId id, std::uint8_t stack);
  // Loot: our V got a quest or important item, or (host) one lies on our
  // NPC's corpse: every other player gets it.
  void SendSharedItem(proto::SharedItem item);
  // Host: the world items still lying somewhere (taken stacks at quantity 0).
  const std::map<proto::WorldItemId, proto::WorldItem> &WorldItems() const { return m_worldItems; }
  // Down and out (docs/ARCHITECTURE.md). Our V's life: kept, sent now if we
  // are in the session, else as soon as we are; the host hands everyone's
  // to late joiners and decides when the session is lost (no V up).
  void SetLocalVitals(proto::Vitals vitals);
  // We revive (or stop reviving) `revive.target`'s downed V: to that
  // player's game, through the host.
  void SendRevive(proto::Revive revive);
  // Every player's life as this game knows it (us included; a player with
  // none sent yet is up).
  proto::Life LifeOf(proto::PlayerId player) const;
  const std::map<proto::PlayerId, proto::Vitals> &Lives() const { return m_lives; }

  // Consistency (docs/ARCHITECTURE.md). Our world was rebuilt around our V
  // (proto::kEpoch* reason): what was in flight about the old one is void.
  // Host: every guest learns it, and their requests aimed at the old world
  // are dropped until they have. Guest: the host learns it.
  void BumpEpoch(std::uint8_t reason);
  std::uint32_t WorldEpoch() const { return m_epoch; }
  // Guest: the host's world epoch as last heard.
  std::uint32_t HostEpoch() const { return m_hostEpoch; }
  // The NPCs the host shares: host, its record; guest, its copy of it (what
  // its game must show as proxies, no more, no less).
  const std::map<proto::NpcId, proto::NpcSpawn> &SharedNpcs() const { return m_npcs; }
  // Guest: its digest checks so far. Host: the guests' requests it dropped
  // because they aimed at an older epoch of its world.
  const consistency::Checker &Checks() const { return m_checker; }
  std::uint64_t Fenced() const { return m_fenced; }
  // Faults on what this endpoint sends (tests, tandem-peer --drop/--dup).
  void SetFaults(const consistency::Faults &faults) { m_faults.Set(faults); }
  const consistency::FaultyLink &FaultStats() const { return m_faults; }

  // Time on the host's clock (the session's shared timeline).
  std::uint64_t HostNowUs() const { return net::NowUs() + m_clockOffsetUs; }

  bool IsHost() const { return m_session->IsHost(); }
  bool IsJoined() const { return m_you != proto::kNoPlayer; }
  proto::PlayerId You() const { return m_you; }
  proto::PlayerId HostPlayer() const { return m_host; } // kNoPlayer until welcomed
  const std::map<proto::PlayerId, proto::PlayerInfo> &Players() const { return m_players; }
  std::optional<net::Stats> HostLinkStats() const;
  // The UDP port we listen on (host) or send from (guest).
  std::uint16_t LocalPort() const { return m_session->LocalPort(); }
  // The link to a player: host, to each guest; guest, to the host only
  // (the others are reached through it). nullopt for ourselves and strangers.
  std::optional<net::Stats> LinkStatsOf(proto::PlayerId player) const;

  // The relay, when the session uses one: host, its join code; guest, how
  // far it got (nullopt while still trying directly).
  std::optional<net::RelayStatus> Relay() const { return m_session->Relay(); }
  // Guest through a relay: the session's secret, to join again without the
  // code (JoinTarget::relay->secret).
  std::optional<net::RelaySecret> RelaySessionSecret() const {
    return m_session->RelaySessionSecret();
  }
  bool ThroughRelay() const { return m_throughRelay; }

  void Leave(const std::string &reason = "left");

private:
  Endpoint(std::unique_ptr<net::Session> session, Identity identity, Timing timing);

  void OnHostEvent(const net::Event &event, std::vector<SessionEvent> &out);
  void OnGuestEvent(const net::Event &event, std::vector<SessionEvent> &out);
  void OnHello(net::PeerId peer, const proto::Hello &hello, std::vector<SessionEvent> &out);
  void RemovePlayer(net::PeerId peer, std::vector<SessionEvent> &out);
  void SendSessionTo(net::PeerId peer);
  // Host: these domains (kDomain*) as they are now, to one guest. `resync`:
  // its copy may hold what we no longer have (NpcSet first).
  void SendDomainsTo(net::PeerId peer, std::uint8_t domains, bool resync);
  // Every send goes through these (faults injected here).
  bool Send(net::PeerId peer, const proto::Message &message);
  void Broadcast(const proto::Message &message, net::PeerId except = 0);
  void OnGuestMessage(const proto::Message &message, std::vector<SessionEvent> &out);
  // Each domain's hash as `self` sees it (consistency.hpp).
  proto::Digest DigestFor(proto::PlayerId self) const;
  void SendDigests();
  void BroadcastRoster();
  // Host: the connection of a welcomed player (nullopt: none, or us).
  std::optional<net::PeerId> PeerOf(proto::PlayerId player) const;
  // Host: delivers a message meant for `to` (kNoPlayer: everyone but
  // `except`) and tells whether it is for us too.
  template <class M> bool Deliver(const M &message, proto::PlayerId to, net::PeerId except);

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
  std::map<proto::NpcId, proto::NpcHold> m_npcHolds;
  std::map<proto::NpcId, proto::NpcAnim> m_npcAnims;
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
  // Players in another world (host: the session's record, for late joiners).
  proto::Elsewhere m_localElsewhere;
  std::map<proto::PlayerId, proto::Elsewhere> m_elsewheres;
  void SendLocalElsewhereNow();
  // Records a player's place; false when it did not change.
  bool NoteElsewhere(const proto::Elsewhere &state);
  // A player is gone (or back from a crash): back in the city for everyone.
  void EndElsewhereOf(proto::PlayerId player, std::vector<SessionEvent> &out, bool broadcast);
  // Our stealth, and (host) everyone's latest, for late joiners.
  std::optional<proto::Stealth> m_localStealth;
  std::uint32_t m_stealthRevision{};
  std::map<proto::PlayerId, proto::Stealth> m_stealth;
  void SendLocalStealthNow();
  std::uint32_t m_awarenessSeq{};
  // Our ride, and everyone's (host: the session's record, for late joiners;
  // only rides that are on).
  std::optional<proto::Transit> m_localTransit;
  std::uint32_t m_transitRevision{};
  std::map<proto::PlayerId, proto::Transit> m_transits;
  void SendLocalTransitNow();
  void NoteTransit(const proto::Transit &transit);
  // Host: our NPCs seated in a player's vehicle.
  std::map<proto::NpcId, proto::NpcSeat> m_npcSeats;
  // Host: the story for late joiners, and who answered the call first.
  story::Record m_story;
  story::CallArbiter m_calls;
  void OnGuestStory(net::PeerId peer, proto::PlayerId player, const proto::Message &message,
                    std::vector<SessionEvent> &out);
  // Host: the shared world items and who took each stack; events for our
  // own game from calls made between updates (our drops and claims).
  std::map<proto::WorldItemId, proto::WorldItem> m_worldItems;
  std::map<proto::WorldItemId, std::vector<proto::PlayerId>> m_worldTakers;
  proto::WorldItemId m_nextWorldItem{1};
  std::vector<SessionEvent> m_pending;
  void AddWorldItem(proto::PlayerId dropper, proto::WorldItem item, std::vector<SessionEvent> &out);
  void ClaimWorldItem(proto::PlayerId player, proto::WorldItemId id, std::uint8_t stack,
                      std::optional<net::PeerId> peer, std::vector<SessionEvent> &out);
  // Down and out: our V's life, and everyone's (host: the record for late
  // joiners). Host: whether the session is lost now, and how many times.
  std::optional<proto::Vitals> m_localVitals;
  std::uint32_t m_vitalsRevision{};
  std::map<proto::PlayerId, proto::Vitals> m_lives;
  bool m_lost{};
  std::uint32_t m_losses{};
  void SendLocalVitalsNow();
  // Host: every player is down or out: GameOver to all, once per loss.
  void CheckLost(std::vector<SessionEvent> &out);
  // Host: the session's difficulty and its NPCs' poise, for late joiners.
  std::optional<proto::Difficulty> m_difficulty;
  std::map<proto::NpcId, proto::NpcPoise> m_npcPoise;
  void OnGuestCombat(proto::PlayerId player, const proto::Message &message,
                     std::vector<SessionEvent> &out);
  // Consistency: our world epoch; guest, the host's and the first NpcStates
  // of it; host, the host epoch each guest has seen and its own epoch.
  std::uint32_t m_epoch{1};
  std::uint32_t m_hostEpoch{};
  std::uint32_t m_firstNpcSeq{};
  std::map<proto::PlayerId, std::uint32_t> m_seenHost, m_guestEpoch;
  std::uint64_t m_fenced{};
  std::uint64_t m_epochAtUs{};
  // Host: digests (when the next round is due, guests owed one sooner) and
  // each guest's last resync, for its cooldown.
  std::uint32_t m_digestSeq{};
  std::uint64_t m_nextDigestUs{};
  std::set<proto::PlayerId> m_digestSoon;
  std::map<proto::PlayerId, std::uint64_t> m_resyncAtUs;
  // Guest: its checks, the host's NPCs that died (corpses until the host's
  // goes), and what came before the Welcome (the bulk channel can overtake it).
  consistency::Checker m_checker;
  std::set<proto::NpcId> m_corpses;
  std::vector<proto::Message> m_early;
  consistency::FaultyLink m_faults;
  bool m_ended{};
  // Guest: the relay to try when the host does not answer directly.
  std::optional<net::RelayOptions> m_fallback;
  bool m_tryRelay{};
  bool m_throughRelay{};
};
} // namespace tandem
