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

// A host NPC's animation state (proto::NpcAnim), for NpcAnims.reds.
struct TandemNpcAnim {
  std::uint32_t npc{};
  std::int32_t highLevel{}; // gamedataNPCHighLevelState
  std::int32_t upperBody{}; // gamedataNPCUpperBodyState
  std::int32_t stance{};    // gamedataNPCStanceState
  std::int32_t flags{};     // proto::kNpcAnim*
  Red::TweakDBID weapon;
  std::uint32_t target{}; // a player id, or 0x10000 + an NPC id (0: nobody)
  std::uint64_t workspot{};
  std::int32_t workspotEntry{};
  Red::CName workspotAnim;
};
static_assert(alignof(TandemNpcAnim) <= 8, "the script VM aligns struct locals to 8");

// One shot of a host NPC (proto::NpcShot).
struct TandemNpcShot {
  std::uint32_t npc{};
  std::int32_t effect{}; // gamedataFxAction
  float x{}, y{}, z{};   // where it went (zero: unknown)
};

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
  // Guest: which players the host's NPC has detected (bit player - 1).
  static std::int32_t NpcDetected(std::uint32_t npc) { return Runtime::Get().NpcDetected(npc); }

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

  // Host: an NPC's animation state changed (NpcAnims.reds samples them).
  static void SendNpcAnim(std::uint32_t npc, std::int32_t highLevel, std::int32_t upperBody,
                          std::int32_t stance, std::int32_t flags, Red::TweakDBID weapon,
                          std::uint32_t target, std::uint64_t workspot, std::int32_t workspotEntry,
                          Red::CName workspotAnim) {
    Runtime::Get().SubmitNpcAnim({.npc = npc,
                                  .highLevel = static_cast<std::uint8_t>(highLevel),
                                  .upperBody = static_cast<std::uint8_t>(upperBody),
                                  .stance = static_cast<std::uint8_t>(stance),
                                  .flags = static_cast<std::uint8_t>(flags),
                                  .weapon = weapon.value,
                                  .target = target,
                                  .workspot = workspot,
                                  .workspotEntry = static_cast<std::uint32_t>(workspotEntry),
                                  .workspotAnim = workspotAnim.hash});
  }
  // Host: one of its NPCs fired (or reloaded).
  static void SendNpcShot(std::uint32_t npc, std::int32_t effect, Red::Vector4 target) {
    Runtime::Get().SubmitNpcShot({.npc = npc,
                                  .effect = static_cast<std::uint8_t>(effect),
                                  .target = {target.X, target.Y, target.Z}});
  }
  // Guest: the host's NPCs' animation states and shots since the last ask.
  static Red::DynArray<TandemNpcAnim> TakeNpcAnims() {
    Red::DynArray<TandemNpcAnim> out;
    for (const auto &a : Runtime::Get().TakeNpcAnims())
      out.EmplaceBack(TandemNpcAnim{
          a.npc, a.highLevel, a.upperBody, a.stance, a.flags, Red::TweakDBID(a.weapon), a.target,
          a.workspot, static_cast<std::int32_t>(a.workspotEntry), Red::CName(a.workspotAnim)});
    return out;
  }
  static Red::DynArray<TandemNpcShot> TakeNpcShots() {
    Red::DynArray<TandemNpcShot> out;
    for (const auto &shot : Runtime::Get().TakeNpcShots())
      out.EmplaceBack(
          TandemNpcShot{shot.npc, shot.effect, shot.target.x, shot.target.y, shot.target.z});
    return out;
  }
  // A puppet the game places for now (a proxy in a workspot): its movement
  // controller keeps still until released.
  static void HoldPuppet(std::uint32_t player, bool held) {
    Runtime::Get().SetPuppetHeld(player, held);
  }
  // Writes a resource path (a depot path hash, the same in every copy of the
  // game) into an async resource reference property, found by its RTTI name
  // and type: redscript cannot make a ResourceAsyncRef from a hash.
  static bool SetResourcePath(Red::Handle<Red::IScriptable> object, Red::CName property,
                              std::uint64_t path) {
    if (!object)
      return false;
    const auto *field = object->GetType()->GetProperty(property);
    if (!field || !field->type ||
        field->type->GetType() != Red::rtti::ERTTIType::ResourceAsyncReference) {
      log::Warn("resource path: {} has no async resource property {}",
                object->GetType()->GetName().ToString(), property.ToString());
      return false;
    }
    auto *reference = reinterpret_cast<Red::ResourceAsyncReference<> *>(
        reinterpret_cast<std::uint8_t *>(object.GetPtr()) + field->valueOffset);
    reference->path = Red::ResourcePath(path);
    return true;
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

RTTI_DEFINE_CLASS(tandem::plugin::TandemNpcAnim, "TandemNpcAnim", {
  RTTI_PROPERTY(npc);
  RTTI_PROPERTY(highLevel);
  RTTI_PROPERTY(upperBody);
  RTTI_PROPERTY(stance);
  RTTI_PROPERTY(flags);
  RTTI_PROPERTY(weapon);
  RTTI_PROPERTY(target);
  RTTI_PROPERTY(workspot);
  RTTI_PROPERTY(workspotEntry);
  RTTI_PROPERTY(workspotAnim);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemNpcShot, "TandemNpcShot", {
  RTTI_PROPERTY(npc);
  RTTI_PROPERTY(effect);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemShot, "TandemShot", {
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(left);
  RTTI_PROPERTY(effect);
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
  RTTI_METHOD(NpcDetected);
  RTTI_METHOD(TakeNpcEvents);
  RTTI_METHOD(NpcPuppetId);
  RTTI_METHOD(SendNpcAnim);
  RTTI_METHOD(SendNpcShot);
  RTTI_METHOD(TakeNpcAnims);
  RTTI_METHOD(TakeNpcShots);
  RTTI_METHOD(HoldPuppet);
  RTTI_METHOD(SetResourcePath);
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
