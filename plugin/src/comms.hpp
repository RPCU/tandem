#pragma once

// Comms between the net thread and the game thread (docs/architecture/comms.md;
// scripts/tandem/Comms.reds through plugin/src/comms_natives.cpp): pings, the
// link and what it shares, waypoints. The decisions are core's (tandem/comms.hpp):
// this holds them for our game and turns them into changes for the script.

#include "tandem/comms.hpp"
#include "tandem/protocol.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace tandem::plugin {
// What the net thread tells us.
struct CommsInbound {
  enum class Kind { InSession, Ended, Joined, Left, Message };
  Kind kind{};
  proto::PlayerId player{}; // InSession: us; Joined, Left: who
  proto::Message message;   // Message: Mark, Tag, Link, Waypoint, LinkDecline
};

// One change for the script.
struct CommsEvent {
  enum class Kind : std::int32_t {
    ShowMark = 1, // a ping to show (ours too: player is us)
    HideMark = 2,
    TagOn = 3, // a linked player's tag or reveal to outline
    TagOff = 4,
    Link = 5,     // our link with `player` changed (type: now, before: was; comms::LinkState)
    Waypoint = 6, // `player`'s waypoint (type: proto::WaypointKind, 0 none)
    // A linked partner's quickhack on a shared NPC or a device (target): its
    // upload, the effect it left (record: the program; effect: the status
    // effect, or a device's comms::HackBoard::kDevice*Effect).
    ShowUpload = 7,
    HideUpload = 8,
    ShowEffect = 9,
    HideEffect = 10,
    // `player` did not take our link request (type: proto::kLinkDecline*).
    LinkDeclined = 11,
  };
  Kind kind{};
  proto::PlayerId player{};
  std::uint32_t id{};    // marks: the sender's id
  std::int32_t type{};   // MarkKind, TagReason, LinkState, WaypointKind
  std::int32_t before{}; // Link: the state before
  proto::TargetRef target;
  proto::Vec3 position;
  float seconds{}; // marks: how long; tags: a reveal's length (0: until off); hacks: left
  std::uint64_t record{}, effect{}; // hacks
};

class Comms {
public:
  static Comms &Get();

  // From tandem.ini (link, pings): at load, and from the settings page. A
  // new link default replaces our consents, sent at once in a session.
  enum class LinkDefault { Auto, Ask, Off };
  void Configure(LinkDefault link, bool pings);

  // Net thread.
  void PushInbound(CommsInbound inbound);
  std::vector<proto::Message> TakeOutbound();
  // What a new session's endpoint must say of us from the start.
  proto::Link OwnLink();
  std::optional<proto::Waypoint> OwnWaypoint();

  // Game thread. Every tick: what the script must change on screen.
  std::vector<CommsEvent> Update(std::uint64_t nowUs);
  // Our ping: 0 not sent (too fast, pings off, no session), 1 marked, 2
  // made a danger, 3 taken back.
  std::int32_t Ping(proto::MarkKind kind, const proto::TargetRef &target,
                    const proto::Vec3 &position, std::uint64_t nowUs);
  // Our team target (the "Mark target" quickhack): 0 not sent (too fast, no
  // session), 1 marked, 3 cleared (the same target again). ClearTarget: it
  // died or was destroyed (false: none).
  std::int32_t Target(const proto::TargetRef &target, const proto::Vec3 &position,
                      std::uint64_t nowUs);
  bool ClearTarget(std::uint64_t nowUs);
  // Another player's quickhack on a shared NPC, as this game learns it
  // (step 0 started, 1 done, 2 failed; an effect applied or removed, with
  // what is left of it): shown while we are linked with them.
  void PartnerUpload(proto::NpcId npc, proto::PlayerId player, std::uint64_t action,
                     std::int32_t step, float seconds, std::uint64_t nowUs);
  void PartnerEffect(proto::NpcId npc, proto::PlayerId player, std::uint64_t effect, bool applied,
                     float seconds, std::uint64_t nowUs);
  // A device (its entity's static id) went from `before` to `after` (kDevice*
  // bits) by `player`'s doing, as Devices.reds learns it (host: a guest's
  // DeviceUse; guest: the host's live DeviceState): a partner's quickhack
  // on it shows while we are linked (comms::HackBoard::Device).
  void PartnerDevice(std::uint64_t entity, proto::PlayerId player, std::uint16_t before,
                     std::uint16_t after, const comms::DevicePrograms &programs,
                     std::uint64_t nowUs);
  // Our tag or reveal of one target (sent with the others of this tick).
  void Tag(proto::TagReason reason, bool on, std::uint8_t seconds, const proto::TargetRef &target);
  // We link with `player` (on) or break it.
  bool SetConsent(proto::PlayerId player, bool on);
  // We did not take `player`'s link request (proto::kLinkDecline*): they
  // are told why. False: not in a session, or not a player.
  bool Decline(proto::PlayerId player, std::uint8_t reason);
  comms::LinkState StateOf(proto::PlayerId player);
  bool AnyLinked();
  // Our waypoint (kind None: none); sent when it changes (comms::Moved).
  void SetWaypoint(proto::WaypointKind kind, const proto::Vec3 &position);
  LinkDefault LinkSetting() const { return m_linkDefault; }
  bool PingsOn() const { return m_pings; }
  std::string Report();

private:
  void Queue(const std::vector<comms::Change> &changes);
  void Queue(const std::vector<comms::TagChange> &changes);
  void Queue(const std::vector<comms::LinkChange> &changes);
  void Queue(const std::vector<comms::HackChange> &changes);
  void Apply(const proto::Message &message, std::uint64_t nowUs);
  void FlushTags();

  std::mutex m_lock; // everything below
  LinkDefault m_linkDefault{LinkDefault::Auto};
  bool m_pings{true};
  std::vector<CommsInbound> m_inbound;
  std::vector<proto::Message> m_outbound;
  proto::Link m_own;
  std::optional<proto::Waypoint> m_waypoint;
  proto::PlayerId m_you{};
  comms::Pinger m_pinger;
  comms::Board m_board;
  comms::TagLedger m_tags;
  comms::Links m_links;
  comms::HackBoard m_hacks;
  std::uint64_t m_nowUs{};
  std::map<proto::PlayerId, proto::WaypointKind> m_waypointsShown;
  std::vector<proto::Tag> m_pendingTags;
  std::vector<CommsEvent> m_events;
  std::uint64_t m_sentMarks{}, m_heardMarks{}, m_sentTags{}, m_heardTags{};
};

struct Config;
// Comms::Configure with tandem.ini's link and pings.
void ConfigureComms(const Config &config);
} // namespace tandem::plugin
