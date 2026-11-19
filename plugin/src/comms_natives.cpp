// What scripts/tandem/Comms.reds can call: `TandemCommsNative` static
// functions and the `TandemCommsEvent` struct (docs/architecture/comms.md).
// Declared on the script side in scripts/tandem/CommsNatives.reds: keep the
// two in sync (tools/check-natives.py reads every pair). All of these run on
// the game thread.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "comms.hpp"
#include "log.hpp"

#include "tandem/session.hpp"

#include <RED4ext/Scripting/Natives/Generated/ent/EntityID.hpp>
#include <RedLib.hpp>

#include <algorithm>

namespace tandem::plugin {
// One change for the script (CommsEvent). Plain fields only: the script VM
// aligns struct locals to 8.
struct TandemCommsEvent {
  std::int32_t kind{}; // CommsEvent::Kind
  std::uint32_t player{};
  std::uint32_t id{};
  std::int32_t type{};
  std::int32_t before{};
  std::int32_t targetKind{}; // proto::TargetKind
  std::uint32_t npc{};       // Npc: its NpcId; Player: the player
  Red::EntityID entity;      // Entity: the placed entity
  float x{}, y{}, z{};
  float seconds{};
};
static_assert(alignof(TandemCommsEvent) <= 8, "the script VM aligns struct locals to 8");

namespace {
// A shared NPC first, then a player, then a placed entity (its hash: the
// script passes static entities only), else none.
proto::TargetRef TargetOf(std::uint32_t npc, std::uint32_t player, Red::EntityID entity) {
  if (npc != 0)
    return {proto::TargetKind::Npc, npc};
  if (player != 0)
    return {proto::TargetKind::Player, player};
  if (entity.hash != 0)
    return {proto::TargetKind::Entity, entity.hash};
  return {};
}
} // namespace

struct TandemCommsNative : Red::IScriptable {
  // Every tick: what to change on screen since the last call.
  static Red::DynArray<TandemCommsEvent> Update() {
    Red::DynArray<TandemCommsEvent> out;
    for (const auto &event : Comms::Get().Update(net::NowUs()))
      out.EmplaceBack(TandemCommsEvent{
          .kind = static_cast<std::int32_t>(event.kind),
          .player = event.player,
          .id = event.id,
          .type = event.type,
          .before = event.before,
          .targetKind = static_cast<std::int32_t>(event.target.kind),
          .npc = event.target.kind == proto::TargetKind::Entity
                     ? 0u
                     : static_cast<std::uint32_t>(event.target.id),
          .entity =
              Red::EntityID(event.target.kind == proto::TargetKind::Entity ? event.target.id : 0),
          .x = event.position.x,
          .y = event.position.y,
          .z = event.position.z,
          .seconds = event.seconds});
    return out;
  }

  // Our ping (kind: proto::MarkKind 1..6) at a target (a shared NPC, a
  // player, a placed entity; all 0: a place): 0 not sent, 1 marked, 2 made a
  // danger, 3 taken back.
  static std::int32_t Ping(std::int32_t kind, std::uint32_t npc, std::uint32_t player,
                           Red::EntityID entity, Red::Vector4 position) {
    if (kind < 1 || kind > 6)
      return 0;
    return Comms::Get().Ping(static_cast<proto::MarkKind>(kind), TargetOf(npc, player, entity),
                             {position.X, position.Y, position.Z}, net::NowUs());
  }

  // Our scanner tag (reason 0) or a reveal (1, for `seconds`) of one target,
  // on or off: to the players linked with us.
  static void Tag(std::int32_t reason, bool on, std::int32_t seconds, std::uint32_t npc,
                  Red::EntityID entity) {
    Comms::Get().Tag(reason == 1 ? proto::TagReason::Reveal : proto::TagReason::Scan, on,
                     static_cast<std::uint8_t>(std::clamp(seconds, 0, 255)),
                     TargetOf(npc, 0, entity));
  }

  // Link with a player (on) or break it; false: link = off, or not a player.
  static bool SetLink(std::uint32_t player, bool on) { return Comms::Get().SetConsent(player, on); }
  // 0 none, 1 we asked, 2 they ask us, 3 linked.
  static std::int32_t LinkState(std::uint32_t player) {
    return static_cast<std::int32_t>(Comms::Get().StateOf(player));
  }
  static bool AnyLinked() { return Comms::Get().AnyLinked(); }
  // tandem.ini: link (0 auto, 1 ask, 2 off), pings.
  static std::int32_t LinkSetting() {
    return static_cast<std::int32_t>(Comms::Get().LinkSetting());
  }
  static bool PingsOn() { return Comms::Get().PingsOn(); }

  // Our waypoint (kind 0 none, 1 a map pin, 2 a tracked map point).
  static void SetWaypoint(std::int32_t kind, Red::Vector4 position) {
    Comms::Get().SetWaypoint(static_cast<proto::WaypointKind>(std::clamp(kind, 0, 2)),
                             {position.X, position.Y, position.Z});
  }

  // (tools/game.sh cmd comms report) one line for the log.
  static Red::CString Report() { return Red::CString(Comms::Get().Report().c_str()); }

  RTTI_IMPL_TYPEINFO(TandemCommsNative);
  RTTI_IMPL_ALLOCATOR();
};
} // namespace tandem::plugin

RTTI_DEFINE_CLASS(tandem::plugin::TandemCommsEvent, "TandemCommsEvent", {
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(id);
  RTTI_PROPERTY(type);
  RTTI_PROPERTY(before);
  RTTI_PROPERTY(targetKind);
  RTTI_PROPERTY(npc);
  RTTI_PROPERTY(entity);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
  RTTI_PROPERTY(seconds);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemCommsNative, "TandemCommsNative", {
  RTTI_METHOD(Update);
  RTTI_METHOD(Ping);
  RTTI_METHOD(Tag);
  RTTI_METHOD(SetLink);
  RTTI_METHOD(LinkState);
  RTTI_METHOD(AnyLinked);
  RTTI_METHOD(LinkSetting);
  RTTI_METHOD(PingsOn);
  RTTI_METHOD(SetWaypoint);
  RTTI_METHOD(Report);
});
