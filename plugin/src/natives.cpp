// What redscript can call: `TandemNative` static functions and the
// `TandemEvent` struct. Declared on the script side in scripts/tandem/Natives.reds:
// keep the two in sync. All of these run on the game thread.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "engine/animation.hpp"
#include "engine/hooks.hpp"
#include "engine/look.hpp"
#include "engine/pose.hpp"
#include "engine/posescan.hpp"
#include "engine/posestream.hpp"
#include "engine/trace.hpp"
#include "log.hpp"
#include "runtime.hpp"

#include <RedLib.hpp>

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace tandem::plugin {
namespace {
proto::Body BodyOf(std::int32_t body) {
  switch (body) {
  case 1:
    return proto::Body::Female;
  case 2:
    return proto::Body::Male;
  default:
    return proto::Body::Unknown;
  }
}
} // namespace

struct TandemEvent {
  std::int32_t kind{};
  std::uint32_t player{};
  Red::CString name;
  std::int32_t body{};
};

struct TandemShot {
  std::uint32_t player{};
  bool left{};
  std::int32_t effect{};
};

// Plain fields only: Vector4 and Quaternion are 16-byte aligned, the script
// VM aligns struct locals to 8, and the zeroing movaps then faults (a crash
// that Proton's crash handler turns into a frozen game, 2026-11-18).
struct TandemNpcEvent {
  std::int32_t kind{}; // 1 spawn, 2 gone
  std::uint32_t npc{};
  Red::TweakDBID record;
  Red::CName appearance;
  float x{}, y{}, z{};
  float i{}, j{}, k{}, r{1.f}; // orientation quaternion
  std::int32_t reason{};
  std::uint32_t player{}; // 3 hit: who
  float amount{};         // 3 hit: damage; 4 health: fraction 0..1
  std::int32_t flags{};   // 3 hit: proto::kHit*
  float dx{}, dy{}, dz{}; // 3 hit: its direction (x, y, z: where it landed)
};
static_assert(alignof(TandemNpcEvent) <= 8, "the script VM aligns struct locals to 8");

// Guest: one of the host's NPCs hit our V (proto::PlayerHit). Plain fields,
// as above.
struct TandemPlayerHit {
  std::uint32_t npc{}; // the attacker as the host shares it (0: not shared)
  float physical{}, thermal{}, electric{}, chemical{};
  std::int32_t attackType{}; // gamedataAttackType
  std::int32_t flags{};      // proto::kHit*
  float pierce{};            // the attacker's armour penetration
  float x{}, y{}, z{};       // where it landed
  float dx{}, dy{}, dz{};    // its direction
  Red::TweakDBID attack;     // attack record
  Red::TweakDBID weapon;     // the NPC's weapon item record
};
static_assert(alignof(TandemPlayerHit) <= 8, "the script VM aligns struct locals to 8");
// Host: a guest's V made a noise (proto::Noise), for our NPCs to hear.
struct TandemNoise {
  std::uint32_t player{};
  std::int32_t stim{}; // gamedataStimType
  float radius{};      // 0: the stimulus's own
  bool propagation{};
};
static_assert(alignof(TandemNoise) <= 8, "the script VM aligns struct locals to 8");
// Quickhacks (scripts/tandem/Hacks.reds); plain fields, as above.
struct TandemHackEvent {
  std::int32_t kind{}; // HackEvent::Kind
  std::uint32_t npc{};
  std::uint32_t player{};
  Red::TweakDBID action; // ObjectAction record; 3 status: the status effect record
  std::int32_t state{};  // 2: proto::HackStep; 3: applied; 5: proto::CarHack
  std::int32_t flags{};  // 1: proto::kUpload*; 4: proto::kPlayerHack*
  float seconds{};
};
static_assert(alignof(TandemHackEvent) <= 8, "the script VM aligns struct locals to 8");
// An NPC's new speed in the time bubbles (tandem/timebubble.hpp): its
// factor, 1 when it is out of every bubble.
struct TandemTimeChange {
  std::uint32_t npc{};
  float factor{};
};

Red::DynArray<TandemTimeChange> ToScript(const std::vector<sync::TimeBubbles::Change> &changes) {
  Red::DynArray<TandemTimeChange> out;
  for (const auto &change : changes)
    out.EmplaceBack(TandemTimeChange{change.npc, change.factor});
  return out;
}

Red::Quaternion ToQuaternion(const proto::Quat &q) { return {q.i, q.j, q.k, q.r}; }
float YawOfQuaternion(const Red::Quaternion &q) { return sync::YawOf({q.i, q.j, q.k, q.r}); }

struct TandemNative : Red::IScriptable {
  static bool IsConfigured() { return Runtime::Get().config.role != Config::Role::Off; }

  static bool IsStarted() { return Runtime::Get().Started(); }

  // The local V is in a loaded game: start the session (once).
  static void Start(std::int32_t body) { Runtime::Get().Start(BodyOf(body)); }

  static void Stop() { Runtime::Get().Stop(); }

  static void SendLocalState(Red::Vector4 position, Red::Quaternion orientation,
                             Red::Vector4 velocity, std::int32_t locomotion, std::int32_t flags) {
    proto::PlayerState state;
    state.position = {position.X, position.Y, position.Z};
    state.orientation = {orientation.i, orientation.j, orientation.k, orientation.r};
    state.velocity = {velocity.X, velocity.Y, velocity.Z};
    state.stance = static_cast<std::uint8_t>(locomotion);
    state.flags = static_cast<std::uint16_t>(flags);
    Runtime::Get().SubmitLocal(state);
  }

  static Red::DynArray<TandemEvent> TakeEvents() {
    Runtime::Get().NoteGameFrame();
    Red::DynArray<TandemEvent> out;
    for (auto &event : Runtime::Get().TakeEvents())
      out.EmplaceBack(TandemEvent{static_cast<std::int32_t>(event.kind), event.player,
                                  Red::CString(event.name.c_str()),
                                  static_cast<std::int32_t>(event.body)});
    return out;
  }

  static void RegisterPuppet(std::uint32_t player, Red::EntityID id) {
    Runtime::Get().RegisterPuppet(player, id.hash);
    log::Info("puppet for player {} is entity {:x}", player, id.hash);
  }

  static void UnregisterPuppet(std::uint32_t player) { Runtime::Get().UnregisterPuppet(player); }

  // The engine offered this puppet a movement controller since the last ask.
  static bool WantsController(std::uint32_t player) {
    return Runtime::Get().TakeControllerRequest(player);
  }

  static bool AttachController(std::uint32_t player, Red::Handle<Red::ent::Entity> entity) {
    return engine::AttachController(player, entity);
  }

  // Where a remote player is drawn now (spawn position, debug).
  static Red::Vector4 RemotePosition(std::uint32_t player) {
    const auto view = Runtime::Get().ViewOf(player);
    return {view.position.x, view.position.y, view.position.z, view.valid ? 1.f : 0.f};
  }

  static float RemoteYaw(std::uint32_t player) { return Runtime::Get().ViewOf(player).yaw; }

  // The remote player's gamePSMLocomotionStates value, as of the drawn moment.
  static std::int32_t RemoteStance(std::uint32_t player) {
    return Runtime::Get().ViewOf(player).stance;
  }

  static Red::CString PuppetRecord(std::int32_t body) {
    const auto &config = Runtime::Get().config;
    return Red::CString(BodyOf(body) == proto::Body::Male ? config.puppetMale.c_str()
                                                          : config.puppetFemale.c_str());
  }

  static bool IsProbe() { return Runtime::Get().config.probe; }

  // Locomotion clip lengths of an entity's rig, for the probe: -1 = no clip.
  static Red::CString ClipLengths(Red::Handle<Red::ent::Entity> entity) {
    if (!entity)
      return "no entity";
    engine::AnimationDriver driver;
    driver.Attach(entity.GetPtr());
    std::string out;
    for (const char *clip : {"walk_0", "jog_0", "sprint_0"})
      out += std::format("{} {:.2f} ", clip, driver.ClipLength(clip));
    driver.Detach();
    return Red::CString(out.c_str());
  }

  // Dress a puppet in a customization state's look (V's templates have no
  // meshes of their own). Answers the number of appearance keys, -1 = failed.
  static std::int32_t ApplyLook(Red::Handle<Red::ent::Entity> entity,
                                Red::Handle<Red::game::ui::ICharacterCustomizationState> state,
                                bool male) {
    return engine::ApplyLook(entity, state, male);
  }

  // A command for the running game (tools/game.sh cmd): the first line of
  // tandem.cmd, which is then removed. Empty when there is none.
  static Red::CString TakeCommand() {
    const auto &path = Runtime::Get().config.commandFile;
    std::error_code error;
    if (path.empty() || !std::filesystem::exists(path, error))
      return {};
    std::string line;
    {
      std::ifstream file(path);
      std::getline(file, line);
    }
    std::filesystem::remove(path, error);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
      line.pop_back();
    log::Info("command: {}", line);
    return Red::CString(line.c_str());
  }

  // A puppet built from V's template takes items as first-person ones (no
  // visible garment) unless its PuppetPS says third person: the byte NCO
  // found at +0x72, set before anything is equipped. Checked against the
  // runtime class first (type and size measured on 2.5).
  static bool MarkThirdPerson(Red::Handle<Red::ent::Entity> entity) {
    // gameObject.persistentState by its RTTI offset, gamePuppetPS by name:
    // no hand offsets but the one byte.
    static auto *objectClass = Red::GetClass("gameObject");
    static auto *stateClass = Red::GetClass("gamePuppetPS");
    static const auto *stateProperty =
        objectClass ? objectClass->GetProperty("persistentState") : nullptr;
    if (!entity || !stateProperty || !stateClass || !entity->GetType()->IsA(objectClass))
      return false;
    const auto &state = *reinterpret_cast<const Red::Handle<Red::ISerializable> *>(
        reinterpret_cast<const std::uint8_t *>(entity.GetPtr()) + stateProperty->valueOffset);
    if (!state || !state->GetType()->IsA(stateClass) || stateClass->GetSize() != 0x78) {
      log::Warn("third person: not a PuppetPS of the measured layout, not marked");
      return false;
    }
    reinterpret_cast<std::uint8_t *>(state.GetPtr())[0x72] = 1;
    return true;
  }

  static bool IsMirror() { return Runtime::Get().config.mirror; }
  static void MirrorPlayerTo(Red::Handle<Red::ent::Entity> entity) {
    engine::MirrorPlayerTo(entity);
  }
  static void ClearMirrors() { engine::ClearMirrors(); }

  // (probe, docs/research/pose-streaming.md step 1) the unmapped memory of
  // an entity's root animated component, to find its pose buffers: every
  // 8-byte slot of 0x130..0x2B0 that holds a readable pointer, with the first
  // floats it points at.
  static void PoseProbe(Red::Handle<Red::ent::Entity> entity) {
    static auto *animatedClass = Red::GetClass("entAnimatedComponent");
    if (!entity)
      return;
    for (const auto &component : entity->components) {
      if (!component || !component->GetType()->IsA(animatedClass) ||
          component->name != Red::CName("root"))
        continue;
      const auto *base = reinterpret_cast<const std::uint8_t *>(component.GetPtr());
      log::Info("pose probe: entity {:x} root component {} ({})", entity->entityID.hash,
                static_cast<const void *>(base), component->GetType()->GetName().ToString());
      for (std::size_t offset = 0x130; offset < 0x2B0; offset += 8) {
        const auto value = *reinterpret_cast<const std::uintptr_t *>(base + offset);
        MEMORY_BASIC_INFORMATION info{};
        const bool pointer =
            value > 0x10000 && value < 0x00007FFFFFFFFFFF && (value & 7) == 0 &&
            VirtualQuery(reinterpret_cast<const void *>(value), &info, sizeof(info)) != 0 &&
            info.State == MEM_COMMIT &&
            (info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ)) != 0 &&
            (info.Protect & PAGE_GUARD) == 0;
        if (!pointer) {
          if (value != 0)
            log::Info("pose probe: +0x{:03X} = {:#x}", offset, value);
          continue;
        }
        const auto *target = reinterpret_cast<const float *>(value);
        const auto *words = reinterpret_cast<const std::uintptr_t *>(value);
        log::Info("pose probe: +0x{:03X} -> {:#x}: floats {:.3f} {:.3f} {:.3f} {:.3f} {:.3f} "
                  "{:.3f} {:.3f} {:.3f}, words {:#x} {:#x} {:#x}",
                  offset, value, target[0], target[1], target[2], target[3], target[4], target[5],
                  target[6], target[7], words[0], words[1], words[2]);
      }
      return;
    }
    log::Info("pose probe: entity {:x} has no root animated component", entity->entityID.hash);
  }

  static void PoseMark(Red::Handle<Red::ent::Entity> entity, bool diff) {
    engine::PoseMark(entity, diff);
  }
  static void PoseBones(Red::Handle<Red::ent::Entity> entity) { engine::PoseBones(entity); }
  static void PoseTree(Red::Handle<Red::ent::Entity> entity) { engine::PoseTree(entity); }
  static void PoseScan(Red::Handle<Red::ent::Entity> entity) { engine::PoseScan(entity); }

  // The pose hook (engine/pose.cpp): install, measure, and the copy experiment.
  static bool PoseHook() { return engine::InstallPoseHook(); }
  static void PoseWatch(Red::Handle<Red::ent::Entity> entity, Red::CString label,
                        std::int32_t depth) {
    engine::PoseWatch(entity, label.c_str(), depth);
  }
  static void PoseCopy(Red::Handle<Red::ent::Entity> source, Red::Handle<Red::ent::Entity> target) {
    engine::PoseCopy(source, target);
  }
  static void PoseReport() { engine::PoseReport(); }

  // Body pose streaming (engine/posestream.cpp): the local V to read, the
  // puppet that draws a player. Cheap; scripts call them twice a second.
  static void PoseSource(Red::Handle<Red::ent::Entity> entity) { engine::SetPoseSource(entity); }
  static void PoseBind(std::uint32_t player, Red::Handle<Red::ent::Entity> entity) {
    engine::BindPosePuppet(player, entity);
  }
  // (test) Our own pose drawn on this player's puppet, with no network; 0: off.
  static void PoseLoop(std::uint32_t player) {
    Runtime::Get().SetPoseLoopback(player);
    log::Info("pose stream: loopback {}", player ? std::format("to player {}", player) : "off");
  }
  static void PoseStreamReport() { engine::PoseStreamReport(); }

  // Our V's look (customization keys + the clothes shown), sent when it
  // changes. Answers the number of look keys (0: the state was not usable).
  static std::int32_t
  SubmitAppearance(Red::Handle<Red::game::ui::ICharacterCustomizationState> state, bool male,
                   Red::DynArray<Red::TweakDBID> items) {
    proto::Appearance appearance;
    appearance.body = male ? proto::Body::Male : proto::Body::Female;
    appearance.look = engine::CollectLook(state, male);
    for (const auto &item : items)
      if (appearance.items.size() < proto::kMaxAppearanceItems)
        appearance.items.push_back(item.value);
    if (appearance.look.size() > proto::kMaxAppearanceKeys)
      appearance.look.resize(proto::kMaxAppearanceKeys);
    const auto keys = static_cast<std::int32_t>(appearance.look.size());
    Runtime::Get().SubmitLocalAppearance(std::move(appearance));
    return keys;
  }

  // How many looks arrived for this player so far (0: none): a change means
  // the puppet should be dressed again.
  static std::int32_t RemoteAppearanceChanges(std::uint32_t player) {
    std::uint32_t changes = 0;
    Runtime::Get().RemoteAppearance(player, changes);
    return static_cast<std::int32_t>(changes);
  }

  // The clothes the player shows (TweakDB ids), to wear as preview items.
  static Red::DynArray<Red::TweakDBID> RemoteItems(std::uint32_t player) {
    Red::DynArray<Red::TweakDBID> out;
    std::uint32_t changes = 0;
    if (const auto appearance = Runtime::Get().RemoteAppearance(player, changes))
      for (const auto item : appearance->items)
        out.EmplaceBack(Red::TweakDBID(item));
    return out;
  }

  // The player's face and body on their puppet (skipped when unchanged).
  static std::int32_t ApplyRemoteLook(Red::Handle<Red::ent::Entity> entity, std::uint32_t player) {
    std::uint32_t changes = 0;
    const auto appearance = Runtime::Get().RemoteAppearance(player, changes);
    if (!appearance || appearance->look.empty())
      return -1;
    return engine::ScheduleLook(entity, appearance->look);
  }

  // What our V holds, when it changes (0 / empty id: nothing).
  static void SendHeld(Red::TweakDBID right, Red::TweakDBID left) {
    Runtime::Get().SubmitLocalHeld({.right = right.value, .left = left.value});
  }

  // One shot of our V's weapon: the effect (gamedataFxAction) it played.
  static void SendShot(bool left, std::int32_t effect) {
    Runtime::Get().SubmitLocalShot({.hand = left ? proto::Hand::Left : proto::Hand::Right,
                                    .effect = static_cast<std::uint8_t>(effect)});
  }

  static std::int32_t RemoteHeldChanges(std::uint32_t player) {
    std::uint32_t changes = 0;
    Runtime::Get().RemoteHeld(player, changes);
    return static_cast<std::int32_t>(changes);
  }

  static Red::TweakDBID RemoteHeld(std::uint32_t player, bool left) {
    std::uint32_t changes = 0;
    const auto held = Runtime::Get().RemoteHeld(player, changes);
    return held ? Red::TweakDBID(left ? held->left : held->right) : Red::TweakDBID();
  }

  static Red::DynArray<TandemShot> TakeShots() {
    Red::DynArray<TandemShot> out;
    for (const auto &shot : Runtime::Get().TakeRemoteShots())
      out.EmplaceBack(TandemShot{shot.player, shot.hand == proto::Hand::Left, shot.effect});
    return out;
  }

  // Where our V sits: the vehicle's record (none: on foot), the seat, and
  // whose car (0: our own, or a world car).
  static void SendSeat(Red::TweakDBID vehicle, Red::CName seat, std::uint32_t owner) {
    Runtime::Get().SubmitLocalSeat({.vehicle = vehicle.value, .seat = seat.hash, .owner = owner});
  }

  // The car our V drives, 30 times a second while driving.
  static void SendVehicle(Red::Vector4 position, Red::Quaternion orientation,
                          Red::Vector4 velocity) {
    Runtime::Get().SubmitLocalVehicle(
        {.position = {position.X, position.Y, position.Z},
         .orientation = {orientation.i, orientation.j, orientation.k, orientation.r},
         .velocity = {velocity.X, velocity.Y, velocity.Z}});
  }

  static std::int32_t RemoteSeatChanges(std::uint32_t player) {
    std::uint32_t changes = 0;
    Runtime::Get().RemoteSeat(player, changes);
    return static_cast<std::int32_t>(changes);
  }

  static Red::TweakDBID RemoteSeatVehicle(std::uint32_t player) {
    std::uint32_t changes = 0;
    const auto seat = Runtime::Get().RemoteSeat(player, changes);
    return seat ? Red::TweakDBID(seat->vehicle) : Red::TweakDBID();
  }

  static Red::CName RemoteSeatSlot(std::uint32_t player) {
    std::uint32_t changes = 0;
    const auto seat = Runtime::Get().RemoteSeat(player, changes);
    return seat ? Red::CName(seat->seat) : Red::CName();
  }

  static std::uint32_t RemoteSeatOwner(std::uint32_t player) {
    std::uint32_t changes = 0;
    const auto seat = Runtime::Get().RemoteSeat(player, changes);
    return seat ? seat->owner : 0;
  }

  // Where to put the player's car now; W = 0 when there is none.
  static Red::Vector4 RemoteVehiclePosition(std::uint32_t player) {
    const auto view = Runtime::Get().VehicleOf(player);
    return {view.position.x, view.position.y, view.position.z, view.valid ? 1.f : 0.f};
  }

  static Red::Quaternion RemoteVehicleOrientation(std::uint32_t player) {
    const auto q = Runtime::Get().VehicleOf(player).orientation;
    return {q.i, q.j, q.k, q.r};
  }

  // The radio of the car our V drives (scripts/tandem/Radio.reds): on, the
  // station (ERadioStationList index, -1 none), the song; `trackStarted` when
  // the song just changed on the same station (its start is now).
  static void SendVehicleRadio(bool on, std::int32_t station, Red::CName track, bool trackStarted) {
    Runtime::Get().SubmitLocalRadio({.on = on,
                                     .station = static_cast<std::int8_t>(on ? station : -1),
                                     .track = on ? track.hash : 0},
                                    trackStarted);
  }

  // The radio of the car a remote player drives: how many arrived (0: none).
  static std::int32_t RemoteRadioChanges(std::uint32_t player) {
    std::uint32_t changes = 0;
    Runtime::Get().RemoteRadio(player, changes);
    return static_cast<std::int32_t>(changes);
  }
  static bool RemoteRadioOn(std::uint32_t player) {
    std::uint32_t changes = 0;
    const auto radio = Runtime::Get().RemoteRadio(player, changes);
    return radio && radio->on;
  }
  static std::int32_t RemoteRadioStation(std::uint32_t player) {
    std::uint32_t changes = 0;
    const auto radio = Runtime::Get().RemoteRadio(player, changes);
    return radio ? radio->station : -1;
  }
  static Red::CName RemoteRadioTrack(std::uint32_t player) {
    std::uint32_t changes = 0;
    const auto radio = Runtime::Get().RemoteRadio(player, changes);
    return radio ? Red::CName(radio->track) : Red::CName();
  }
  // Seconds since the driver's song began (host clock); -1: unknown.
  static float RemoteRadioTrackAge(std::uint32_t player) {
    std::uint32_t changes = 0;
    const auto radio = Runtime::Get().RemoteRadio(player, changes);
    const auto now = Runtime::Get().HostNowUs();
    if (!radio || radio->trackStartUs == 0 || radio->trackStartUs > now)
      return -1.f;
    return static_cast<float>(static_cast<double>(now - radio->trackStartUs) / 1e6);
  }

  static bool IsHost() { return Runtime::Get().config.role == Config::Role::Host; }

  // Host: the world as our game has it (sent when it changes, and every few
  // seconds by the script).
  static void SendWorld(std::int32_t gameTimeSeconds, Red::CName weather, bool paused) {
    Runtime::Get().SubmitWorld(
        {.gameTimeSeconds = gameTimeSeconds, .weather = weather.hash, .timePaused = paused});
  }

  // Guest: the host's world; changes counts the updates (0: none yet).
  static std::int32_t HostWorldChanges() {
    std::uint32_t changes = 0;
    Runtime::Get().HostWorld(changes);
    return static_cast<std::int32_t>(changes);
  }
  static std::int32_t HostGameTime() {
    std::uint32_t changes = 0;
    const auto world = Runtime::Get().HostWorld(changes);
    return world ? world->gameTimeSeconds : -1;
  }
  static Red::CName HostWeather() {
    std::uint32_t changes = 0;
    const auto world = Runtime::Get().HostWorld(changes);
    return world ? Red::CName(world->weather) : Red::CName();
  }

  // The pause menu pauses everyone: ours opened (true) or closed.
  static void SendPaused(bool paused) { Runtime::Get().SubmitLocalPaused(paused); }
  // The other players in their pause menu, and how many times that changed
  // (0: never).
  static Red::DynArray<std::uint32_t> PausedPlayers() {
    std::uint32_t changes = 0;
    Red::DynArray<std::uint32_t> out;
    for (const auto player : Runtime::Get().RemotePaused(changes))
      out.EmplaceBack(player);
    return out;
  }
  static std::int32_t PausedChanges() {
    std::uint32_t changes = 0;
    Runtime::Get().RemotePaused(changes);
    return static_cast<std::int32_t>(changes);
  }
  // A player's name in the session (empty: unknown).
  static Red::CString PlayerName(std::uint32_t player) {
    return Red::CString(Runtime::Get().PlayerName(player).c_str());
  }

  // Time abilities (scripts/tandem/Time.reds): ours, when they change
  // (proto::kTime* bits; scale: the world speed they ask for, 1: none).
  static void SendTimeEffect(std::int32_t abilities, float scale) {
    Runtime::Get().SubmitLocalTime(
        {.abilities = static_cast<std::uint8_t>(abilities), .scale = scale});
  }
  // The other players' active ones, and how many times they changed (0: never).
  static std::int32_t TimeChanges() {
    std::uint32_t changes = 0;
    Runtime::Get().RemoteTimes(changes);
    return static_cast<std::int32_t>(changes);
  }
  static Red::DynArray<std::uint32_t> TimePlayers() {
    std::uint32_t changes = 0;
    Red::DynArray<std::uint32_t> out;
    for (const auto &[player, effect] : Runtime::Get().RemoteTimes(changes))
      out.EmplaceBack(player);
    return out;
  }
  static std::int32_t TimeAbilities(std::uint32_t player) {
    std::uint32_t changes = 0;
    const auto times = Runtime::Get().RemoteTimes(changes);
    const auto it = times.find(player);
    return it == times.end() ? 0 : it->second.abilities;
  }
  static float TimeScale(std::uint32_t player) {
    std::uint32_t changes = 0;
    const auto times = Runtime::Get().RemoteTimes(changes);
    const auto it = times.find(player);
    return it == times.end() ? 1.f : it->second.scale;
  }
  // The time bubbles: the Vs with a bubble ability on (player, world speed,
  // where), every NPC there is (host: the ones it shares; guest: its
  // proxies). Answers the NPCs whose speed changed.
  static Red::DynArray<TandemTimeChange>
  UpdateTimeBubbles(Red::DynArray<std::uint32_t> owners, Red::DynArray<float> scales,
                    Red::DynArray<Red::Vector4> ownerPositions, Red::DynArray<std::uint32_t> npcs,
                    Red::DynArray<Red::Vector4> npcPositions) {
    std::vector<sync::TimeBubbles::Owner> who;
    for (std::uint32_t i = 0; i < owners.Size() && i < scales.Size() && i < ownerPositions.Size();
         ++i)
      who.push_back({.player = owners[i],
                     .scale = scales[i],
                     .position = {ownerPositions[i].X, ownerPositions[i].Y, ownerPositions[i].Z}});
    std::vector<sync::TimeBubbles::Npc> what;
    for (std::uint32_t i = 0; i < npcs.Size() && i < npcPositions.Size(); ++i)
      what.push_back({npcs[i], {npcPositions[i].X, npcPositions[i].Y, npcPositions[i].Z}});
    return ToScript(Runtime::Get().Bubbles().Update(who, what));
  }
  // Every bubble ends (the session is over): the NPCs to give back.
  static Red::DynArray<TandemTimeChange> ClearTimeBubbles() {
    return ToScript(Runtime::Get().Bubbles().Clear());
  }
  static float TimeFactorOf(std::uint32_t npc) { return Runtime::Get().Bubbles().FactorOf(npc); }
  static std::int32_t TimeBubbleMembers(std::uint32_t player) {
    return static_cast<std::int32_t>(Runtime::Get().Bubbles().MembersOf(player));
  }
  // tandem.ini time_host = full: the host keeps its own slow motion whole.
  static bool IsTimeHostFull() { return Runtime::Get().config.timeHostFull; }

  // Host: its NPCs.
  static void SendNpcSpawn(std::uint32_t npc, Red::TweakDBID record, Red::CName appearance,
                           Red::Vector4 position, Red::Quaternion orientation) {
    Runtime::Get().SubmitNpcSpawn({.npc = npc,
                                   .record = record.value,
                                   .appearance = appearance.hash,
                                   .position = {position.X, position.Y, position.Z},
                                   .yaw = YawOfQuaternion(orientation)});
  }
  static void SendNpcStates(Red::DynArray<std::uint32_t> npcs,
                            Red::DynArray<Red::Vector4> positions,
                            Red::DynArray<Red::Quaternion> orientations,
                            Red::DynArray<std::int32_t> detected) {
    proto::NpcStates states;
    for (std::uint32_t i = 0; i < npcs.Size() && i < positions.Size() && i < orientations.Size() &&
                              i < detected.Size() && states.npcs.size() < proto::kMaxNpcsPerState;
         ++i)
      states.npcs.push_back({.npc = npcs[i],
                             .position = {positions[i].X, positions[i].Y, positions[i].Z},
                             .yaw = YawOfQuaternion(orientations[i]),
                             .detected = static_cast<std::uint8_t>(detected[i])});
    Runtime::Get().SubmitNpcStates(states);
  }

  // Host: an NPC's health (fraction 0..1) changed.
  static void SendNpcHealth(std::uint32_t npc, float health) {
    Runtime::Get().SubmitNpcHealth({.npc = npc, .health = health});
  }
  // Guest: we hit the host's NPC (its proxy) for `damage` health points.
  static void SendNpcHit(std::uint32_t npc, float damage, std::int32_t flags, Red::Vector4 position,
                         Red::Vector4 direction) {
    Runtime::Get().SubmitNpcHit({.npc = npc,
                                 .damage = damage,
                                 .flags = static_cast<std::uint8_t>(flags),
                                 .position = {position.X, position.Y, position.Z},
                                 .direction = {direction.X, direction.Y, direction.Z}});
  }
  // Guest: status effects our hit put on the host's NPC (its proxy).
  static void SendNpcEffect(std::uint32_t npc, Red::DynArray<Red::TweakDBID> effects) {
    proto::NpcEffect message{.npc = npc};
    for (std::uint32_t i = 0; i < effects.Size() && i < proto::kMaxNpcEffects; ++i)
      message.effects.push_back(effects[i].value);
    if (!message.effects.empty())
      Runtime::Get().SubmitNpcEffect(message);
  }
  // Host: our NPC `npc` (0: not shared) hit `player`'s puppet. The damage
  // is the attacker's side only; that player's game applies the target's.
  static void SendPlayerHit(std::uint32_t player, std::uint32_t npc, float physical, float thermal,
                            float electric, float chemical, std::int32_t attackType,
                            std::int32_t flags, float pierce, Red::Vector4 position,
                            Red::Vector4 direction, Red::TweakDBID attack, Red::TweakDBID weapon) {
    Runtime::Get().SubmitPlayerHit({.player = player,
                                    .npc = npc,
                                    .physical = physical,
                                    .thermal = thermal,
                                    .electric = electric,
                                    .chemical = chemical,
                                    .attackType = static_cast<std::uint8_t>(attackType),
                                    .flags = static_cast<std::uint8_t>(flags),
                                    .pierce = pierce,
                                    .position = {position.X, position.Y, position.Z},
                                    .direction = {direction.X, direction.Y, direction.Z},
                                    .attack = attack.value,
                                    .weapon = weapon.value});
  }
  // Guest: the host's NPCs' hits on our V since the last call.
  static Red::DynArray<TandemPlayerHit> TakePlayerHits() {
    Red::DynArray<TandemPlayerHit> out;
    for (const auto &hit : Runtime::Get().TakePlayerHits())
      out.EmplaceBack(TandemPlayerHit{hit.npc, hit.physical, hit.thermal, hit.electric,
                                      hit.chemical, hit.attackType, hit.flags, hit.pierce,
                                      hit.position.x, hit.position.y, hit.position.z,
                                      hit.direction.x, hit.direction.y, hit.direction.z,
                                      Red::TweakDBID(hit.attack), Red::TweakDBID(hit.weapon)});
    return out;
  }
  // Guest: which players the host's NPC has detected (bit player - 1).
  static std::int32_t NpcDetected(std::uint32_t npc) { return Runtime::Get().NpcDetected(npc); }
  // Guest: how many of the host's NPCs have detected our V (0: none, or the
  // host's states stopped coming).
  static std::int32_t NpcsDetectingUs() { return Runtime::Get().NpcsDetectingUs(); }
  // Guest: how far the host's NPC is in noticing our V, 0..1.
  static float NpcAwareness(std::uint32_t npc) { return Runtime::Get().NpcAwarenessOfUs(npc); }
  // Host: how far each of its NPCs is in noticing each guest (levels 0..1).
  static void SendNpcAwareness(Red::DynArray<std::uint32_t> npcs,
                               Red::DynArray<std::uint32_t> players, Red::DynArray<float> levels) {
    proto::NpcAwareness awareness;
    for (std::uint32_t i = 0; i < npcs.Size() && i < players.Size() && i < levels.Size() &&
                              awareness.entries.size() < proto::kMaxNpcAwareness;
         ++i) {
      const auto level = std::clamp(levels[i], 0.f, 1.f);
      awareness.entries.push_back({.npc = npcs[i],
                                   .player = players[i],
                                   .level = static_cast<std::uint8_t>(std::lround(level * 255.f))});
    }
    Runtime::Get().SubmitNpcAwareness(std::move(awareness));
  }

  // How hard our V is to see (proto::kStealth* flags, its Visibility stat,
  // the camo status effect on it), when it changes.
  static void SendStealth(std::int32_t flags, float visibility, Red::TweakDBID camo) {
    Runtime::Get().SubmitLocalStealth(
        {.flags = static_cast<std::uint8_t>(flags), .visibility = visibility, .camo = camo.value});
  }
  static std::int32_t RemoteStealthChanges(std::uint32_t player) {
    std::uint32_t changes = 0;
    Runtime::Get().RemoteStealth(player, changes);
    return static_cast<std::int32_t>(changes);
  }
  static std::int32_t RemoteStealthFlags(std::uint32_t player) {
    std::uint32_t changes = 0;
    const auto stealth = Runtime::Get().RemoteStealth(player, changes);
    return stealth ? stealth->flags : 0;
  }
  static float RemoteStealthVisibility(std::uint32_t player) {
    std::uint32_t changes = 0;
    const auto stealth = Runtime::Get().RemoteStealth(player, changes);
    return stealth ? stealth->visibility : 1.f;
  }
  static Red::TweakDBID RemoteStealthCamo(std::uint32_t player) {
    std::uint32_t changes = 0;
    const auto stealth = Runtime::Get().RemoteStealth(player, changes);
    return stealth ? Red::TweakDBID(stealth->camo) : Red::TweakDBID();
  }

  // Guest: a stimulus our V gave out, for the host's NPCs.
  static void SendNoise(std::int32_t stim, float radius, bool propagation) {
    Runtime::Get().SubmitLocalNoise(
        {.stim = static_cast<std::uint8_t>(stim), .radius = radius, .propagation = propagation});
  }
  // Host: the guests' noises since the last call.
  static Red::DynArray<TandemNoise> TakeNoises() {
    Red::DynArray<TandemNoise> out;
    for (const auto &noise : Runtime::Get().TakeRemoteNoises())
      out.EmplaceBack(TandemNoise{noise.player, noise.stim, noise.radius, noise.propagation});
    return out;
  }

  static void SendNpcGone(std::uint32_t npc, std::int32_t reason, std::uint32_t killer) {
    Runtime::Get().SubmitNpcGone(
        {.npc = npc, .reason = static_cast<std::uint8_t>(reason), .killer = killer});
  }

  // Guest: the host's NPCs to spawn or remove.
  static Red::DynArray<TandemNpcEvent> TakeNpcEvents() {
    Red::DynArray<TandemNpcEvent> out;
    for (const auto &event : Runtime::Get().TakeNpcEvents()) {
      const auto &s = event.spawn;
      const auto turn = sync::FromYaw(s.yaw);
      out.EmplaceBack(
          TandemNpcEvent{static_cast<std::int32_t>(event.kind), s.npc, Red::TweakDBID(s.record),
                         Red::CName(s.appearance), s.position.x, s.position.y, s.position.z, turn.i,
                         turn.j, turn.k, turn.r, event.reason, event.player, event.amount,
                         event.flags, event.direction.x, event.direction.y, event.direction.z});
    }
    return out;
  }

  // Quickhacks. Guest: we started uploading `action` on the host's NPC.
  static void SendNpcHack(std::uint32_t npc, Red::TweakDBID action, float seconds,
                          std::int32_t flags) {
    Runtime::Get().SubmitHack(proto::NpcHack{
        .npc = npc,
        .action = action.value,
        .uploadMs = static_cast<std::uint32_t>(std::clamp(seconds, 0.f, 600.f) * 1000.f),
        .flags = static_cast<std::uint8_t>(flags)});
  }
  // Host: an upload on one of our NPCs started (0), was done (1) or failed (2).
  static void SendNpcHackState(std::uint32_t npc, std::uint32_t player, Red::TweakDBID action,
                               std::int32_t step, float seconds) {
    if (step < 0 || step > static_cast<std::int32_t>(proto::HackStep::Failed))
      return;
    Runtime::Get().SubmitHack(proto::NpcHackState{
        .npc = npc,
        .player = player,
        .action = action.value,
        .step = static_cast<proto::HackStep>(step),
        .uploadMs = static_cast<std::uint32_t>(std::clamp(seconds, 0.f, 600.f) * 1000.f)});
  }
  // Host: a quickhack effect came onto (applied) or left one of our NPCs.
  static void SendNpcStatus(std::uint32_t npc, std::uint32_t player, Red::TweakDBID effect,
                            bool applied) {
    Runtime::Get().SubmitHack(
        proto::NpcStatus{.npc = npc, .player = player, .effect = effect.value, .applied = applied});
  }
  // Host: our NPC netrunner `npc` hacks player `player`'s V.
  static void SendPlayerHack(std::uint32_t player, std::uint32_t npc, Red::TweakDBID action,
                             std::int32_t flags) {
    Runtime::Get().SubmitHack(proto::PlayerHack{.player = player,
                                                .npc = npc,
                                                .action = action.value,
                                                .flags = static_cast<std::uint8_t>(flags)});
  }
  // We hacked the car player `driver` drives (proto::CarHack).
  static void SendVehicleHack(std::uint32_t driver, std::int32_t hack, float seconds) {
    if (hack < 0 || hack > static_cast<std::int32_t>(proto::CarHack::Accelerate))
      return;
    Runtime::Get().SubmitHack(proto::VehicleHack{
        .driver = driver, .hack = static_cast<proto::CarHack>(hack), .seconds = seconds});
  }
  static Red::DynArray<TandemHackEvent> TakeHackEvents() {
    Red::DynArray<TandemHackEvent> out;
    for (const auto &event : Runtime::Get().TakeHackEvents())
      out.EmplaceBack(TandemHackEvent{static_cast<std::int32_t>(event.kind), event.npc,
                                      event.player, Red::TweakDBID(event.action), event.state,
                                      event.flags, event.seconds});
    return out;
  }

  // The puppet id the controller and tracks know an NPC proxy by.
  static std::uint32_t NpcPuppetId(std::uint32_t npc) { return kNpcPuppetBase + npc; }

  static void Log(Red::CString text) { log::Info("[script] {}", text.c_str()); }

  RTTI_IMPL_TYPEINFO(TandemNative);
  RTTI_IMPL_ALLOCATOR();
};
} // namespace tandem::plugin

RTTI_DEFINE_CLASS(tandem::plugin::TandemEvent, "TandemEvent", {
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(name);
  RTTI_PROPERTY(body);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemNpcEvent, "TandemNpcEvent", {
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(npc);
  RTTI_PROPERTY(record);
  RTTI_PROPERTY(appearance);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
  RTTI_PROPERTY(i);
  RTTI_PROPERTY(j);
  RTTI_PROPERTY(k);
  RTTI_PROPERTY(r);
  RTTI_PROPERTY(reason);
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(amount);
  RTTI_PROPERTY(flags);
  RTTI_PROPERTY(dx);
  RTTI_PROPERTY(dy);
  RTTI_PROPERTY(dz);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemHackEvent, "TandemHackEvent", {
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(npc);
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(action);
  RTTI_PROPERTY(state);
  RTTI_PROPERTY(flags);
  RTTI_PROPERTY(seconds);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemPlayerHit, "TandemPlayerHit", {
  RTTI_PROPERTY(npc);
  RTTI_PROPERTY(physical);
  RTTI_PROPERTY(thermal);
  RTTI_PROPERTY(electric);
  RTTI_PROPERTY(chemical);
  RTTI_PROPERTY(attackType);
  RTTI_PROPERTY(flags);
  RTTI_PROPERTY(pierce);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
  RTTI_PROPERTY(dx);
  RTTI_PROPERTY(dy);
  RTTI_PROPERTY(dz);
  RTTI_PROPERTY(attack);
  RTTI_PROPERTY(weapon);
});
RTTI_DEFINE_CLASS(tandem::plugin::TandemNoise, "TandemNoise", {
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(stim);
  RTTI_PROPERTY(radius);
  RTTI_PROPERTY(propagation);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemShot, "TandemShot", {
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(left);
  RTTI_PROPERTY(effect);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemTimeChange, "TandemTimeChange", {
  RTTI_PROPERTY(npc);
  RTTI_PROPERTY(factor);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemNative, "TandemNative", {
  RTTI_METHOD(IsConfigured);
  RTTI_METHOD(IsStarted);
  RTTI_METHOD(Start);
  RTTI_METHOD(Stop);
  RTTI_METHOD(SendLocalState);
  RTTI_METHOD(TakeEvents);
  RTTI_METHOD(RegisterPuppet);
  RTTI_METHOD(UnregisterPuppet);
  RTTI_METHOD(WantsController);
  RTTI_METHOD(AttachController);
  RTTI_METHOD(RemotePosition);
  RTTI_METHOD(RemoteYaw);
  RTTI_METHOD(RemoteStance);
  RTTI_METHOD(PuppetRecord);
  RTTI_METHOD(IsProbe);
  RTTI_METHOD(ClipLengths);
  RTTI_METHOD(ApplyLook);
  RTTI_METHOD(TakeCommand);
  RTTI_METHOD(MarkThirdPerson);
  RTTI_METHOD(IsMirror);
  RTTI_METHOD(MirrorPlayerTo);
  RTTI_METHOD(ClearMirrors);
  RTTI_METHOD(PoseProbe);
  RTTI_METHOD(PoseScan);
  RTTI_METHOD(PoseTree);
  RTTI_METHOD(PoseBones);
  RTTI_METHOD(PoseMark);
  RTTI_METHOD(PoseHook);
  RTTI_METHOD(PoseWatch);
  RTTI_METHOD(PoseCopy);
  RTTI_METHOD(PoseReport);
  RTTI_METHOD(SendNpcSpawn);
  RTTI_METHOD(SendNpcStates);
  RTTI_METHOD(SendNpcGone);
  RTTI_METHOD(SendNpcHealth);
  RTTI_METHOD(SendNpcHit);
  RTTI_METHOD(SendNpcEffect);
  RTTI_METHOD(SendPlayerHit);
  RTTI_METHOD(TakePlayerHits);
  RTTI_METHOD(NpcDetected);
  RTTI_METHOD(NpcsDetectingUs);
  RTTI_METHOD(NpcAwareness);
  RTTI_METHOD(SendNpcAwareness);
  RTTI_METHOD(SendStealth);
  RTTI_METHOD(RemoteStealthChanges);
  RTTI_METHOD(RemoteStealthFlags);
  RTTI_METHOD(RemoteStealthVisibility);
  RTTI_METHOD(RemoteStealthCamo);
  RTTI_METHOD(SendNoise);
  RTTI_METHOD(TakeNoises);
  RTTI_METHOD(TakeNpcEvents);
  RTTI_METHOD(SendNpcHack);
  RTTI_METHOD(SendNpcHackState);
  RTTI_METHOD(SendNpcStatus);
  RTTI_METHOD(SendPlayerHack);
  RTTI_METHOD(SendVehicleHack);
  RTTI_METHOD(TakeHackEvents);
  RTTI_METHOD(NpcPuppetId);
  RTTI_METHOD(SendVehicleRadio);
  RTTI_METHOD(RemoteRadioChanges);
  RTTI_METHOD(RemoteRadioOn);
  RTTI_METHOD(RemoteRadioStation);
  RTTI_METHOD(RemoteRadioTrack);
  RTTI_METHOD(RemoteRadioTrackAge);
  RTTI_METHOD(IsHost);
  RTTI_METHOD(SendWorld);
  RTTI_METHOD(HostWorldChanges);
  RTTI_METHOD(HostGameTime);
  RTTI_METHOD(HostWeather);
  RTTI_METHOD(SendPaused);
  RTTI_METHOD(PausedPlayers);
  RTTI_METHOD(PausedChanges);
  RTTI_METHOD(PlayerName);
  RTTI_METHOD(SendTimeEffect);
  RTTI_METHOD(TimeChanges);
  RTTI_METHOD(TimePlayers);
  RTTI_METHOD(TimeAbilities);
  RTTI_METHOD(TimeScale);
  RTTI_METHOD(UpdateTimeBubbles);
  RTTI_METHOD(ClearTimeBubbles);
  RTTI_METHOD(TimeFactorOf);
  RTTI_METHOD(TimeBubbleMembers);
  RTTI_METHOD(IsTimeHostFull);
  RTTI_METHOD(SendSeat);
  RTTI_METHOD(SendVehicle);
  RTTI_METHOD(RemoteSeatChanges);
  RTTI_METHOD(RemoteSeatVehicle);
  RTTI_METHOD(RemoteSeatSlot);
  RTTI_METHOD(RemoteSeatOwner);
  RTTI_METHOD(RemoteVehiclePosition);
  RTTI_METHOD(RemoteVehicleOrientation);
  RTTI_METHOD(SendHeld);
  RTTI_METHOD(SendShot);
  RTTI_METHOD(RemoteHeldChanges);
  RTTI_METHOD(RemoteHeld);
  RTTI_METHOD(TakeShots);
  RTTI_METHOD(SubmitAppearance);
  RTTI_METHOD(RemoteAppearanceChanges);
  RTTI_METHOD(RemoteItems);
  RTTI_METHOD(ApplyRemoteLook);
  RTTI_METHOD(PoseSource);
  RTTI_METHOD(PoseBind);
  RTTI_METHOD(PoseLoop);
  RTTI_METHOD(PoseStreamReport);
  RTTI_METHOD(Log);
});
