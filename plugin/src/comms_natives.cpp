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
#include <format>

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
  Red::TweakDBID record; // hacks: the program (ObjectAction)
  Red::TweakDBID effect; // hacks: the status effect
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
          .seconds = event.seconds,
          .record = Red::TweakDBID(event.record),
          .effect = Red::TweakDBID(event.effect)});
    return out;
  }

  // Another player's quickhack on a shared NPC, as Hacks.reds learns it: an
  // upload (step 0 started, for `seconds`; 1 done, 2 failed), an effect
  // applied (with what is left of it) or removed. Shown while linked.
  static void PartnerUpload(std::uint32_t npc, std::uint32_t player, Red::TweakDBID action,
                            std::int32_t step, float seconds) {
    Comms::Get().PartnerUpload(npc, player, action.value, step, seconds, net::NowUs());
  }
  static void PartnerEffect(std::uint32_t npc, std::uint32_t player, Red::TweakDBID effect,
                            bool applied, float seconds) {
    Comms::Get().PartnerEffect(npc, player, effect.value, applied, seconds, net::NowUs());
  }
  // A device another player changed, as Devices.reds learns it (host: a
  // guest's request; guest: the host's live word): its state bits before
  // and after. A quickhack on it shows while linked.
  static void PartnerDevice(Red::EntityID entity, std::uint32_t player, std::int32_t before,
                            std::int32_t after) {
    // No program crosses games for a device: the icons of what the bits say
    // (the distraction is the one the game's own malfunction uses).
    static const comms::DevicePrograms kPrograms{
        .glitch = Red::TweakDBID("DeviceAction.MalfunctionClassHack").value};
    const auto bits = [](std::int32_t flags) {
      return static_cast<std::uint16_t>(flags & proto::kDeviceStateBits);
    };
    Comms::Get().PartnerDevice(entity.hash, player, bits(before), bits(after), kPrograms,
                               net::NowUs());
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

  // Our team target (the "Mark target" quickhack) at a shared NPC, a player
  // or a static entity: 0 not sent, 1 marked, 3 cleared. ClearTarget: ours
  // died or was destroyed (false: we had none).
  static std::int32_t MarkTarget(std::uint32_t npc, std::uint32_t player, Red::EntityID entity,
                                 Red::Vector4 position) {
    return Comms::Get().Target(TargetOf(npc, player, entity), {position.X, position.Y, position.Z},
                               net::NowUs());
  }
  static bool ClearTarget() { return Comms::Get().ClearTarget(net::NowUs()); }

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
  // Their link request not taken (reason: proto::kLinkDecline*): they are
  // told why; false: not in a session, or not a player.
  static bool DeclineLink(std::uint32_t player, std::int32_t reason) {
    return Comms::Get().Decline(player, static_cast<std::uint8_t>(std::clamp(reason, 0, 255)));
  }
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

  // Our waypoint (kind 0 none, 1 a map pin, 2 a tracked map point, 3 the
  // tracked quest objective).
  static void SetWaypoint(std::int32_t kind, Red::Vector4 position) {
    Comms::Get().SetWaypoint(static_cast<proto::WaypointKind>(std::clamp(kind, 0, 3)),
                             {position.X, position.Y, position.Z});
  }

  // (tools/game.sh cmd comms report) one line for the log.
  static Red::CString Report() { return Red::CString(Comms::Get().Report().c_str()); }

  // ---- a partner's call (Holocall.reds; comms.md, "The call on screen") ----
  // A journal contact of the partner's own, for the game's call UI to find
  // by its id (tandem_partner_PLAYER): a gameJournalContact made here, never
  // in the journal, with the partner's name and the unknown avatar (research
  // engine/comms.md §13.163: the call UI reads the name's text at +0xA8 and
  // `avatarID` from the entry itself). Null when the class is not laid out
  // as on 2.50 (logged once).
  static Red::Handle<Red::IScriptable> PartnerContact(std::uint32_t player, Red::CString name) {
    auto *cls = ContactClass();
    if (!cls)
      return {};
    auto *instance = static_cast<Red::IScriptable *>(cls->CreateInstance(true));
    if (!instance)
      return {};
    Red::Handle<Red::IScriptable> contact(instance);
    const auto id = std::format("tandem_partner_{}", player);
    cls->GetProperty("id")->SetValue<Red::CString>(instance, Red::CString(id.c_str()));
    cls->GetProperty("avatarID")
        ->SetValue<Red::TweakDBID>(instance, Red::TweakDBID("PhoneAvatars.Avatar_Unknown"));
    cls->GetProperty("type")->SetValue<std::int32_t>(instance, 0); // gameContactType.Caller
    cls->GetProperty("isCallableDefault")->SetValue<bool>(instance, true);
    RenameContact(contact, name);
    return contact;
  }
  // The partner's name on their contact (a name changed in the session).
  static void RenameContact(Red::Handle<Red::IScriptable> contact, Red::CString name) {
    auto *cls = ContactClass();
    if (!cls || !contact || !contact->GetType()->IsA(cls))
      return;
    cls->GetProperty("name")->GetValuePtr<RED4ext::LocalizationString>(contact.GetPtr())->unk08 =
        name;
  }
  // gameJournalContact as measured on 2.50 (0xD8 bytes; `name` a
  // LocalizationString at +0xA0, its text at +0xA8; `type` +0xC8, `avatarID`
  // +0xCC, `isCallableDefault` +0xD5; the entry's `id` +0x40), else null.
  static Red::CClass *ContactClass() {
    static Red::CClass *s_class = []() -> Red::CClass * {
      auto *cls = Red::CRTTISystem::Get()->GetClass("gameJournalContact");
      const auto at = [&](const char *property, std::uint32_t offset) {
        const auto *field = cls ? cls->GetProperty(property) : nullptr;
        return field && field->valueOffset == offset;
      };
      if (cls && cls->GetSize() == 0xD8 && at("id", 0x40) && at("name", 0xA0) && at("type", 0xC8) &&
          at("avatarID", 0xCC) && at("isCallableDefault", 0xD5))
        return cls;
      log::Error("partner call: gameJournalContact is not laid out as on 2.50: no contact for a "
                 "partner's call");
      return nullptr;
    }();
    return s_class;
  }

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
  RTTI_PROPERTY(record);
  RTTI_PROPERTY(effect);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemCommsNative, "TandemCommsNative", {
  RTTI_METHOD(Update);
  RTTI_METHOD(Ping);
  RTTI_METHOD(MarkTarget);
  RTTI_METHOD(ClearTarget);
  RTTI_METHOD(PartnerUpload);
  RTTI_METHOD(PartnerEffect);
  RTTI_METHOD(PartnerDevice);
  RTTI_METHOD(Tag);
  RTTI_METHOD(SetLink);
  RTTI_METHOD(DeclineLink);
  RTTI_METHOD(LinkState);
  RTTI_METHOD(AnyLinked);
  RTTI_METHOD(LinkSetting);
  RTTI_METHOD(PingsOn);
  RTTI_METHOD(SetWaypoint);
  RTTI_METHOD(Report);
  RTTI_METHOD(PartnerContact);
  RTTI_METHOD(RenameContact);
});
