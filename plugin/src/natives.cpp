// What redscript can call: `TandemNative` static functions and the
// `TandemEvent` struct. Declared on the script side in scripts/tandem/Natives.reds:
// keep the two in sync. All of these run on the game thread.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "addresses.hpp"
#include "engine/animation.hpp"
#include "engine/hooks.hpp"
#include "engine/look.hpp"
#include "engine/pose.hpp"
#include "engine/posescan.hpp"
#include "engine/posestream.hpp"
#include "engine/trace.hpp"
#include "log.hpp"
#include "runtime.hpp"

#include "tandem/movement.hpp"

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

// The co-op menu's view of the session (Runtime::SessionStatus).
struct TandemStatus {
  std::int32_t role{};  // the chosen session: 0 off, 1 host, 2 join
  std::int32_t state{}; // SessionStatus::State (0 off .. 5 failed)
  Red::CString address; // join: the host's address
  std::int32_t port{};
  std::uint32_t you{};
  std::uint32_t host{};
  std::int32_t players{};
  std::int32_t pingMs{}; // guest: to the host; -1 unknown
  Red::CString detail;   // why the last session ended, or failed
  bool fromMenu{};       // chosen in the co-op menu (else tandem.ini)
  Red::CString joinCode; // join: the code ("K7Q-2M9"), "" by address
};

struct TandemPlayer {
  std::uint32_t id{};
  Red::CString name;
  std::int32_t pingMs{}; // our link to them; -1: unknown, or ourselves
  bool host{};
  bool you{};
};

// A host this game joined (recent.txt).
struct TandemHost {
  Red::CString address;
  std::int32_t port{};
  Red::CString name;
};
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
// Guest: one of the host's NPCs fires at our V (proto::NpcShotAt); our game
// fires it from the proxy. `boost` x `factor` for `boostSeconds`, then
// `factor`: the projectile's speed, so it catches up with the host's
// timeline (TimeBubbles::ShotCatchUp). Plain fields, as above.
struct TandemShotAt {
  std::uint32_t npc{};
  Red::TweakDBID attack; // 0: the weapon's own
  Red::TweakDBID weapon;
  float tbh{};
  std::int32_t trigger{}; // gamedataTriggerMode
  float factor{};
  float boost{};
  float boostSeconds{};
};
static_assert(alignof(TandemShotAt) <= 8, "the script VM aligns struct locals to 8");
// Host: a guest's game says a shot it simulated hit its V (proto::ShotHit).
struct TandemShotHit {
  std::uint32_t player{};
  std::uint32_t npc{};
  float x{}, y{}, z{};    // where it landed
  float dx{}, dy{}, dz{}; // its direction
};
static_assert(alignof(TandemShotHit) <= 8, "the script VM aligns struct locals to 8");
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
// Fast travel together (scripts/tandem/Travel.reds). Plain fields, as above.
struct TandemRegroup {
  std::int32_t kind{}; // 1 an offer to come to `player`, 2 their answer to our offer
  std::uint32_t player{};
  std::uint32_t offer{};
  std::int32_t reason{};  // proto::RegroupReason
  Red::TweakDBID point;   // the fast travel point `player` is at (invalid: none)
  std::uint64_t marker{}; // its marker node's hash (NodeRefOf; 0: none)
  float x{}, y{}, z{};    // where `player` stands
  float yaw{};            // radians, 0 = facing +Y
  Red::CString label;     // shown with the prompt; may be empty
  bool accepted{};        // 2: the answer
};
static_assert(alignof(TandemRegroup) <= 8, "the script VM aligns struct locals to 8");

// Grapples, takedowns, finishers and carried bodies (Takedowns.reds). Plain
// fields only, like TandemNpcEvent.
struct TandemNpcAction {
  std::int32_t kind{}; // 1 a guest's request (host), 2 the host's answer, 3 who holds, 4 cue
  std::uint32_t npc{};
  std::uint32_t player{}; // 1: who asks; 3: the holder (0: released); 4: us
  std::int32_t action{};  // 1, 2: proto::NpcActionKind; 3: proto::NpcHoldKind; 4: proto::NpcCueKind
  Red::CName name;
  Red::TweakDBID record; // the item, as a TweakDB id (finisher: weapon)
  Red::EntityID entity;  // the item, as an entity id (dispose: the container)
  float amount{};
  float dx{}, dy{}, dz{}; // throw: the impulse
  bool accepted{};        // 2
  std::int32_t reason{};  // 2: proto::kRefused*
};
static_assert(alignof(TandemNpcAction) <= 8, "the script VM aligns struct locals to 8");
// Loot (scripts/tandem/Loot.reds): one stack of a shared world item lying
// in the world (1), or who picked up a stack (2). Plain fields only, as above.
struct TandemLootEvent {
  // 1 a stack of a world item, 2 a stack taken, 3 an important item another
  // player got (id 0), 4 (guest) one on the host's corpse of NPC `id`
  std::int32_t kind{};
  std::uint32_t id{};
  std::uint32_t player{}; // 1: the dropper; 2: who has it (0: nobody, it was gone)
  std::uint32_t token{};  // 1: the dropper's own number for it
  std::int32_t stack{};
  std::int32_t stacks{}; // 1: how many stacks the item has
  Red::TweakDBID record;
  std::uint32_t seed{};
  std::int32_t quantity{}; // 1: 0 when already taken
  float x{}, y{}, z{};     // 1: where it lies
};
static_assert(alignof(TandemLootEvent) <= 8, "the script VM aligns struct locals to 8");

// Down and out (scripts/tandem/Revive.reds): someone reviving our V.
struct TandemReviveEvent {
  std::uint32_t player{}; // the reviver
  std::int32_t step{};    // proto::ReviveStep: 0 start, 1 cancel, 2 done
};
static_assert(alignof(TandemReviveEvent) <= 8, "the script VM aligns struct locals to 8");

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
  // A session is chosen (tandem.ini's role, or the co-op menu's choice).
  static bool IsConfigured() { return Runtime::Get().ChosenRole() != Config::Role::Off; }

  static bool IsStarted() { return Runtime::Get().Started(); }

  // The local V is in a loaded game: start the chosen session (once).
  static void Start(std::int32_t body) { Runtime::Get().Start(BodyOf(body)); }

  static void Stop() { Runtime::Get().Stop(); }

  // ---- the co-op menu (scripts/tandem/Coop.reds) ----
  // Each picks the next session and stops a running one; TandemSystem starts
  // the new one when a game is loaded (at once in game, else at the load).

  // Host on this UDP port (0: tandem.ini's).
  static bool StartHost(std::int32_t port) {
    auto &runtime = Runtime::Get();
    const auto chosen =
        port > 0 && port < 65536 ? static_cast<std::uint16_t>(port) : runtime.config.port;
    runtime.Choose({.role = Config::Role::Host, .port = chosen, .fromMenu = true});
    return true;
  }

  // Join "address" or "address:port" (port 0: tandem.ini's). False: not an address.
  static bool Join(Red::CString address, std::int32_t port) {
    auto &runtime = Runtime::Get();
    const auto fallback =
        port > 0 && port < 65536 ? static_cast<std::uint16_t>(port) : runtime.config.port;
    const auto target = hosts::Parse(address.c_str(), fallback);
    if (!target) {
      log::Warn("co-op: \"{}\" is not an address to join", address.c_str());
      return false;
    }
    runtime.Choose({.role = Config::Role::Join,
                    .address = target->address,
                    .port = target->port,
                    .fromMenu = true});
    return true;
  }

  // "address" or "address:port" as the menu would join it (port 0: tandem.ini's);
  // an empty address: not one.
  static TandemHost ParseAddress(Red::CString text) {
    const auto target = hosts::Parse(text.c_str(), Runtime::Get().config.port);
    if (!target)
      return {};
    return {Red::CString(target->address.c_str()), target->port, {}};
  }

  // No session (until the next choice, or tandem.ini's at the main menu).
  static void Leave() { Runtime::Get().Choose({.role = Config::Role::Off, .fromMenu = true}); }

  // tandem.ini's choice again (the main menu: a menu choice was for the game left).
  static void ResetChoice() {
    if (Runtime::Get().Choice().fromMenu)
      Runtime::Get().ResetChoice();
  }

  static TandemStatus SessionStatus() {
    auto &runtime = Runtime::Get();
    const auto choice = runtime.Choice();
    const auto status = runtime.Status();
    TandemStatus out;
    out.role = static_cast<std::int32_t>(choice.role);
    out.state = static_cast<std::int32_t>(status.state);
    out.address = Red::CString(choice.address.c_str());
    out.port = choice.port;
    out.you = status.you;
    out.host = status.host;
    out.players = static_cast<std::int32_t>(status.players.size());
    out.pingMs = -1;
    for (const auto &player : status.players)
      if (player.id == status.host && status.host != status.you)
        out.pingMs = player.pingMs;
    out.detail = Red::CString(status.detail.c_str());
    out.fromMenu = choice.fromMenu;
    out.joinCode =
        Red::CString(choice.joinCode.empty() ? "" : net::DisplayJoinCode(choice.joinCode).c_str());
    return out;
  }

  static Red::DynArray<TandemPlayer> SessionPlayers() {
    const auto status = Runtime::Get().Status();
    Red::DynArray<TandemPlayer> out;
    for (const auto &player : status.players)
      out.EmplaceBack(TandemPlayer{player.id, Red::CString(player.name.c_str()), player.pingMs,
                                   player.id == status.host, player.id == status.you});
    return out;
  }

  static Red::DynArray<TandemHost> RecentHosts() {
    Red::DynArray<TandemHost> out;
    for (const auto &recent : Runtime::Get().RecentHosts())
      out.EmplaceBack(TandemHost{Red::CString(recent.target.address.c_str()), recent.target.port,
                                 Red::CString(recent.name.c_str())});
    return out;
  }

  // Where friends reach this game: its IPv4 addresses, the main one first.
  static Red::DynArray<Red::CString> LocalAddresses() {
    Red::DynArray<Red::CString> out;
    for (const auto &address : plugin::LocalAddresses())
      out.EmplaceBack(Red::CString(address.c_str()));
    return out;
  }

  static std::int32_t DefaultPort() { return Runtime::Get().config.port; }

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

  // The controller asked for this puppet's body to be teleported to where
  // the player is drawn (it could not take it there): once per request.
  static bool TakeSnap(std::uint32_t player) { return Runtime::Get().TakeSnap(player); }

  // How the remote player moves as of the drawn moment (tandem/movement.hpp
  // Placement: 0 ground, 1 air, 2 climb, 3 ladder, 4 swim, 5 platform, 6 down).
  static std::int32_t RemotePlacement(std::uint32_t player) {
    return static_cast<std::int32_t>(movement::PlacementOf(Runtime::Get().ViewOf(player).flags));
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

  // Our V's ride (scripts/tandem/Transit.reds): proto::TransitKind (0 none,
  // 1 metro, 2 ride, 3 scene), proto::kTransit* flags, the metro's station
  // and next station (ENcartStations), the vehicle, the seat.
  static void SendTransit(std::int32_t kind, std::int32_t flags, std::int32_t at, std::int32_t next,
                          Red::EntityID vehicle, Red::CName seat, std::uint32_t carrier) {
    if (kind < 0 || kind > static_cast<std::int32_t>(proto::TransitKind::Along))
      return;
    Runtime::Get().SubmitLocalTransit({.kind = static_cast<proto::TransitKind>(kind),
                                       .flags = static_cast<std::uint8_t>(flags),
                                       .at = static_cast<std::uint8_t>(std::clamp(at, 0, 255)),
                                       .next = static_cast<std::uint8_t>(std::clamp(next, 0, 255)),
                                       .vehicle = vehicle.hash,
                                       .seat = seat.hash,
                                       .carrier = carrier});
  }

  // A remote player's ride: how many arrived (0: none), and the latest.
  static std::int32_t RemoteTransitChanges(std::uint32_t player) {
    std::uint32_t changes = 0;
    Runtime::Get().RemoteTransit(player, changes);
    return static_cast<std::int32_t>(changes);
  }
  static std::int32_t RemoteTransitKind(std::uint32_t player) {
    std::uint32_t changes = 0;
    const auto ride = Runtime::Get().RemoteTransit(player, changes);
    return ride ? static_cast<std::int32_t>(ride->kind) : 0;
  }
  static std::int32_t RemoteTransitFlags(std::uint32_t player) {
    std::uint32_t changes = 0;
    const auto ride = Runtime::Get().RemoteTransit(player, changes);
    return ride ? ride->flags : 0;
  }
  static std::int32_t RemoteTransitAt(std::uint32_t player) {
    std::uint32_t changes = 0;
    const auto ride = Runtime::Get().RemoteTransit(player, changes);
    return ride ? ride->at : 0;
  }
  static std::int32_t RemoteTransitNext(std::uint32_t player) {
    std::uint32_t changes = 0;
    const auto ride = Runtime::Get().RemoteTransit(player, changes);
    return ride ? ride->next : 0;
  }
  // The entity the rider rides, as named in its world (the same in ours if
  // it is a placed or quest entity streamed here too: check what it is).
  static Red::EntityID RemoteTransitVehicle(std::uint32_t player) {
    std::uint32_t changes = 0;
    const auto ride = Runtime::Get().RemoteTransit(player, changes);
    return Red::EntityID(ride ? ride->vehicle : 0);
  }
  // Along: whose vehicle carries the player (0: none).
  static std::uint32_t RemoteTransitCarrier(std::uint32_t player) {
    std::uint32_t changes = 0;
    const auto ride = Runtime::Get().RemoteTransit(player, changes);
    return ride ? ride->carrier : 0;
  }
  static Red::CName RemoteTransitSeat(std::uint32_t player) {
    std::uint32_t changes = 0;
    const auto ride = Runtime::Get().RemoteTransit(player, changes);
    return ride ? Red::CName(ride->seat) : Red::CName();
  }

  // Host: our NPC `npc` sits in `owner`'s vehicle in `seat` (owner 0: out).
  static void SendNpcSeat(std::uint32_t npc, std::uint32_t owner, Red::CName seat) {
    Runtime::Get().SubmitNpcSeat({.npc = npc, .owner = owner, .seat = owner ? seat.hash : 0});
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

  static bool IsHost() { return Runtime::Get().ChosenRole() == Config::Role::Host; }

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
  // How much our V is slowed by the other Vs' bubbles (1: not at all): our
  // own ability's world speed (1: none), where we are, and the Vs with a
  // bubble as for UpdateTimeBubbles.
  static float TimePlayerFactor(std::uint32_t player, float own, Red::Vector4 position,
                                Red::DynArray<std::uint32_t> owners, Red::DynArray<float> scales,
                                Red::DynArray<Red::Vector4> ownerPositions, bool inside) {
    std::vector<sync::TimeBubbles::Owner> who;
    for (std::uint32_t i = 0; i < owners.Size() && i < scales.Size() && i < ownerPositions.Size();
         ++i)
      who.push_back({.player = owners[i],
                     .scale = scales[i],
                     .position = {ownerPositions[i].X, ownerPositions[i].Y, ownerPositions[i].Z}});
    return sync::TimeBubbles::PlayerFactor(player, own, {position.X, position.Y, position.Z}, who,
                                           inside);
  }
  static std::int32_t TimeBubbleMembers(std::uint32_t player) {
    return static_cast<std::int32_t>(Runtime::Get().Bubbles().MembersOf(player));
  }
  // tandem.ini time_host = full (the default): the host keeps its own slow
  // motion whole; bubble: its world is put back to normal speed.
  static bool IsTimeHostFull() { return Runtime::Get().config.timeHostFull; }
  // Invites through tandem-relay (docs/ARCHITECTURE.md, "Invites"): the
  // host's join code to show ("K7Q-2M9"; "" until the relay gave one, or
  // with no relay), what the relay link is doing ("" no relay,
  // "connecting", "ready", else the problem, in words), and joining the
  // session a typed code names (false: not a code, or no relay set).
  static Red::CString RelayCode() { return Red::CString(Runtime::Get().RelayCode().c_str()); }

  static Red::CString RelayStatus() { return Red::CString(Runtime::Get().RelayStatus().c_str()); }

  static bool JoinByCode(Red::CString code) { return Runtime::Get().JoinByCode(code.c_str()); }
  // The text is a join code (6 characters of base32, a dash optional).
  static bool IsJoinCode(Red::CString text) {
    return net::NormalizeJoinCode(text.c_str()).has_value();
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
  // Host: our NPC `npc` fires at `player`'s V. That player's game fires it
  // from its proxy and decides the hit (proto::NpcShotAt).
  static void SendShotAt(std::uint32_t player, std::uint32_t npc, Red::TweakDBID attack,
                         Red::TweakDBID weapon, float tbh, std::int32_t trigger, float factor) {
    Runtime::Get().SubmitShotAt({.player = player,
                                 .npc = npc,
                                 .timeUs = Runtime::Get().HostNowUs(),
                                 .attack = attack.value,
                                 .weapon = weapon.value,
                                 .tbh = tbh,
                                 .trigger = static_cast<std::uint8_t>(trigger),
                                 .factor = factor});
  }
  // Guest: the host's NPCs' shots at our V since the last call.
  static Red::DynArray<TandemShotAt> TakeShotsAt() {
    Red::DynArray<TandemShotAt> out;
    const auto now = Runtime::Get().HostNowUs();
    for (const auto &shot : Runtime::Get().TakeShotsAt()) {
      const auto catchUp = sync::TimeBubbles::ShotCatchUp(shot.timeUs, now);
      out.EmplaceBack(TandemShotAt{shot.npc, Red::TweakDBID(shot.attack),
                                   Red::TweakDBID(shot.weapon), shot.tbh, shot.trigger, shot.factor,
                                   catchUp.boost, catchUp.seconds});
    }
    return out;
  }
  // Guest: a shot our game simulated (from a proxy) hit our V.
  static void SendShotHit(std::uint32_t npc, Red::Vector4 position, Red::Vector4 direction) {
    Runtime::Get().SubmitShotHit({.npc = npc,
                                  .position = {position.X, position.Y, position.Z},
                                  .direction = {direction.X, direction.Y, direction.Z}});
  }
  // Host: the guests' reports of shots that hit their V since the last call.
  static Red::DynArray<TandemShotHit> TakeShotHits() {
    Red::DynArray<TandemShotHit> out;
    for (const auto &hit : Runtime::Get().TakeShotHits())
      out.EmplaceBack(TandemShotHit{hit.player, hit.npc, hit.position.x, hit.position.y,
                                    hit.position.z, hit.direction.x, hit.direction.y,
                                    hit.direction.z});
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
  // Grapples, takedowns, finishers, carried bodies (Takedowns.reds).
  // Guest: our V acts on the host's NPC; `record` or else `entity` is the item.
  static void SendNpcAction(std::uint32_t npc, std::int32_t kind, Red::CName name,
                            Red::TweakDBID record, Red::EntityID entity, float amount,
                            Red::Vector4 direction) {
    Runtime::Get().SubmitNpcAction({.npc = npc,
                                    .kind = static_cast<proto::NpcActionKind>(kind),
                                    .name = name.hash,
                                    .item = record.value ? record.value : entity.hash,
                                    .amount = amount,
                                    .direction = {direction.X, direction.Y, direction.Z}});
  }
  // Host: the answer to a guest's request, to that guest only.
  static void SendNpcActionReply(std::uint32_t player, std::uint32_t npc, std::int32_t kind,
                                 bool accepted, std::int32_t reason) {
    Runtime::Get().SubmitNpcActionReply(player, {.npc = npc,
                                                 .kind = static_cast<proto::NpcActionKind>(kind),
                                                 .accepted = accepted,
                                                 .reason = static_cast<std::uint8_t>(reason)});
  }
  // Host: who holds an NPC now (player 0: released).
  static void SendNpcHold(std::uint32_t npc, std::uint32_t player, std::int32_t how,
                          Red::CName name) {
    Runtime::Get().SubmitNpcHold({.npc = npc,
                                  .player = player,
                                  .how = static_cast<proto::NpcHoldKind>(how),
                                  .name = name.hash});
  }
  // Host: an NPC's reaction for one player's game.
  static void SendNpcCue(std::uint32_t npc, std::uint32_t player, std::int32_t cue) {
    Runtime::Get().SubmitNpcCue(
        {.npc = npc, .player = player, .cue = static_cast<proto::NpcCueKind>(cue)});
  }
  static Red::DynArray<TandemNpcAction> TakeNpcActions() {
    Red::DynArray<TandemNpcAction> out;
    for (const auto &e : Runtime::Get().TakeActionEvents()) {
      TandemNpcAction action{.kind = static_cast<std::int32_t>(e.kind),
                             .npc = e.npc,
                             .player = e.player,
                             .action = e.action,
                             .name = Red::CName(e.name),
                             .record = Red::TweakDBID(e.item),
                             .amount = e.amount,
                             .dx = e.direction.x,
                             .dy = e.direction.y,
                             .dz = e.direction.z,
                             .accepted = e.accepted,
                             .reason = e.reason};
      action.entity.hash = e.item;
      out.EmplaceBack(action);
    }
    return out;
  }
  // The controller leaves this puppet (player or NpcPuppetId) where it is.
  static void HoldPuppet(std::uint32_t player, bool hold) {
    Runtime::Get().HoldPuppet(player, hold);
  }
  // Loot: our V dropped a bag of stacks from its inventory (a shared world
  // item: the host numbers it, `token` is ours), or picked up a stack of one
  // (a claim: the first one to reach the host keeps it).
  static void SendWorldItem(std::uint32_t token, Red::Vector4 position,
                            Red::DynArray<Red::TweakDBID> records,
                            Red::DynArray<std::uint32_t> seeds,
                            Red::DynArray<std::int32_t> quantities) {
    proto::WorldItem item{.token = token, .position = {position.X, position.Y, position.Z}};
    for (std::uint32_t i = 0; i < records.Size() && i < seeds.Size() && i < quantities.Size() &&
                              item.stacks.size() < proto::kMaxWorldItemStacks;
         ++i)
      if (quantities[i] > 0)
        item.stacks.push_back({.record = records[i].value,
                               .seed = seeds[i],
                               .quantity = static_cast<std::uint32_t>(quantities[i])});
    if (!item.stacks.empty())
      Runtime::Get().SubmitLoot(std::move(item));
  }
  static void TakeWorldItem(std::uint32_t id, std::int32_t stack) {
    if (stack >= 0 && stack < static_cast<std::int32_t>(proto::kMaxWorldItemStacks))
      Runtime::Get().SubmitLoot(proto::WorldItemTaken{id, static_cast<std::uint8_t>(stack)});
  }
  // World items appearing (one event per stack) and stacks taken.
  static Red::DynArray<TandemLootEvent> TakeLootEvents() {
    Red::DynArray<TandemLootEvent> out;
    for (const auto &message : Runtime::Get().TakeLoot()) {
      if (const auto *item = std::get_if<proto::WorldItem>(&message)) {
        for (std::size_t i = 0; i < item->stacks.size(); ++i) {
          const auto &stack = item->stacks[i];
          out.EmplaceBack(TandemLootEvent{
              .kind = 1,
              .id = item->id,
              .player = item->dropper,
              .token = item->token,
              .stack = static_cast<std::int32_t>(i),
              .stacks = static_cast<std::int32_t>(item->stacks.size()),
              .record = Red::TweakDBID(stack.record),
              .seed = stack.seed,
              .quantity = static_cast<std::int32_t>(stack.quantity),
              .x = item->position.x,
              .y = item->position.y,
              .z = item->position.z,
          });
        }
      } else if (const auto *shared = std::get_if<proto::SharedItem>(&message)) {
        out.EmplaceBack(TandemLootEvent{
            .kind = shared->kind == proto::kSharedOnCorpse ? 4 : 3,
            .id = shared->npc,
            .player = shared->player,
            .record = Red::TweakDBID(shared->item.record),
            .seed = shared->item.seed,
            .quantity = static_cast<std::int32_t>(shared->item.quantity),
        });
      } else if (const auto *taken = std::get_if<proto::WorldItemTaken>(&message)) {
        out.EmplaceBack(TandemLootEvent{
            .kind = 2, .id = taken->id, .player = taken->taker, .stack = taken->stack});
      }
    }
    return out;
  }
  // A quest or important item our V got (kind 0), or (host) one on our NPC
  // `npc`'s corpse (kind 1): every other player gets it.
  static void SendSharedItem(std::int32_t kind, std::uint32_t npc, Red::TweakDBID record,
                             std::uint32_t seed, std::int32_t quantity) {
    if (kind != proto::kSharedLooted && kind != proto::kSharedOnCorpse)
      return;
    Runtime::Get().SubmitLoot(
        proto::SharedItem{.kind = static_cast<std::uint8_t>(kind),
                          .npc = npc,
                          .item = {.record = record.value,
                                   .seed = seed,
                                   .quantity = static_cast<std::uint32_t>(std::max(quantity, 1))}});
  }
  // Our player id in the session (0: not in one).
  static std::uint32_t You() { return Runtime::Get().You(); }

  // Down and out (Revive.reds). Our V's life (proto::Life: 0 up, 1 down,
  // 2 out) and proto::kVitals* flags, when they change.
  static void SendVitals(std::int32_t life, std::int32_t flags) {
    if (life < 0 || life > static_cast<std::int32_t>(proto::Life::Out))
      return;
    Runtime::Get().SubmitLocalVitals(
        {.life = static_cast<proto::Life>(life), .flags = static_cast<std::uint8_t>(flags)});
  }
  // The other players in the session (whatever their life), and how many
  // times that or their lives changed (0: never).
  static Red::DynArray<std::uint32_t> OtherPlayers() {
    std::uint32_t changes = 0;
    Red::DynArray<std::uint32_t> out;
    for (const auto &vitals : Runtime::Get().OthersLives(changes))
      out.EmplaceBack(vitals.player);
    return out;
  }
  static std::int32_t LivesChanges() {
    std::uint32_t changes = 0;
    Runtime::Get().OthersLives(changes);
    return static_cast<std::int32_t>(changes);
  }
  // Another player's life (0 up, also when unknown) and its flags.
  static std::int32_t RemoteLife(std::uint32_t player) {
    std::uint32_t changes = 0;
    for (const auto &vitals : Runtime::Get().OthersLives(changes))
      if (vitals.player == player)
        return static_cast<std::int32_t>(vitals.life);
    return 0;
  }
  static std::int32_t RemoteVitalsFlags(std::uint32_t player) {
    std::uint32_t changes = 0;
    for (const auto &vitals : Runtime::Get().OthersLives(changes))
      if (vitals.player == player)
        return vitals.flags;
    return 0;
  }
  // We revive `target`'s downed V: 0 start, 1 cancel, 2 done.
  static void SendRevive(std::uint32_t target, std::int32_t step) {
    if (step < 0 || step > static_cast<std::int32_t>(proto::ReviveStep::Done))
      return;
    Runtime::Get().SubmitRevive({.target = target, .step = static_cast<proto::ReviveStep>(step)});
  }
  // Revives of our V since the last call.
  static Red::DynArray<TandemReviveEvent> TakeRevives() {
    Red::DynArray<TandemReviveEvent> out;
    for (const auto &revive : Runtime::Get().TakeRevives())
      out.EmplaceBack(TandemReviveEvent{revive.player, static_cast<std::int32_t>(revive.step)});
    return out;
  }
  // How many times a session was lost (no V up) since the game started.
  static std::int32_t SessionLosses() {
    return static_cast<std::int32_t>(Runtime::Get().SessionLosses());
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

  // Fast travel together: offer `to` (0: everyone) to come to us, where we
  // stand (and at which fast travel point, if any). Answers our offer's id.
  static std::uint32_t SendRegroup(std::uint32_t to, std::int32_t reason, Red::TweakDBID point,
                                   Red::NodeRef marker, Red::Vector4 position,
                                   Red::Quaternion orientation, Red::CString label) {
    return Runtime::Get().SubmitRegroup(
        {.to = to,
         .reason = static_cast<proto::RegroupReason>(std::clamp(reason, 0, 3)),
         .point = point.value,
         .marker = marker.hash,
         .position = {position.X, position.Y, position.Z},
         .yaw = YawOfQuaternion(orientation),
         .label = label.c_str()});
  }
  // Our answer to player `to`'s offer (offer 0: we come on our own).
  static void SendRegroupAnswer(std::uint32_t to, std::uint32_t offer, bool accepted) {
    Runtime::Get().SubmitRegroupAnswer({.to = to, .offer = offer, .accepted = accepted});
  }
  // The others' offers and answers since the last call.
  static Red::DynArray<TandemRegroup> TakeRegroups() {
    Red::DynArray<TandemRegroup> out;
    for (const auto &event : Runtime::Get().TakeRegroupEvents()) {
      TandemRegroup item;
      item.kind = static_cast<std::int32_t>(event.kind);
      if (event.kind == RegroupEvent::Kind::Offer) {
        const auto &o = event.offer;
        item.player = o.from;
        item.offer = o.offer;
        item.reason = static_cast<std::int32_t>(o.reason);
        item.point = Red::TweakDBID(o.point);
        item.marker = o.marker;
        item.x = o.position.x;
        item.y = o.position.y;
        item.z = o.position.z;
        item.yaw = o.yaw;
        item.label = Red::CString(o.label.c_str());
      } else {
        item.player = event.answer.from;
        item.offer = event.answer.offer;
        item.accepted = event.answer.accepted;
      }
      out.EmplaceBack(std::move(item));
    }
    return out;
  }
  // A node reference as the number that names it in every game, and back.
  static std::uint64_t NodeRefHash(Red::NodeRef node) { return node.hash; }
  static Red::NodeRef NodeRefOf(std::uint64_t hash) { return Red::NodeRef(hash); }

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

RTTI_DEFINE_CLASS(tandem::plugin::TandemHackEvent, "TandemHackEvent", {
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(npc);
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(action);
  RTTI_PROPERTY(state);
  RTTI_PROPERTY(flags);
  RTTI_PROPERTY(seconds);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemShotAt, "TandemShotAt", {
  RTTI_PROPERTY(npc);
  RTTI_PROPERTY(attack);
  RTTI_PROPERTY(weapon);
  RTTI_PROPERTY(tbh);
  RTTI_PROPERTY(trigger);
  RTTI_PROPERTY(factor);
  RTTI_PROPERTY(boost);
  RTTI_PROPERTY(boostSeconds);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemShotHit, "TandemShotHit", {
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(npc);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
  RTTI_PROPERTY(dx);
  RTTI_PROPERTY(dy);
  RTTI_PROPERTY(dz);
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

RTTI_DEFINE_CLASS(tandem::plugin::TandemNpcAction, "TandemNpcAction", {
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(npc);
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(action);
  RTTI_PROPERTY(name);
  RTTI_PROPERTY(record);
  RTTI_PROPERTY(entity);
  RTTI_PROPERTY(amount);
  RTTI_PROPERTY(dx);
  RTTI_PROPERTY(dy);
  RTTI_PROPERTY(dz);
  RTTI_PROPERTY(accepted);
  RTTI_PROPERTY(reason);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemRegroup, "TandemRegroup", {
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(offer);
  RTTI_PROPERTY(reason);
  RTTI_PROPERTY(point);
  RTTI_PROPERTY(marker);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
  RTTI_PROPERTY(yaw);
  RTTI_PROPERTY(label);
  RTTI_PROPERTY(accepted);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemLootEvent, "TandemLootEvent", {
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(id);
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(token);
  RTTI_PROPERTY(stack);
  RTTI_PROPERTY(stacks);
  RTTI_PROPERTY(record);
  RTTI_PROPERTY(seed);
  RTTI_PROPERTY(quantity);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemReviveEvent, "TandemReviveEvent", {
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(step);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemShot, "TandemShot", {
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(left);
  RTTI_PROPERTY(effect);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemStatus, "TandemStatus", {
  RTTI_PROPERTY(role);
  RTTI_PROPERTY(state);
  RTTI_PROPERTY(address);
  RTTI_PROPERTY(port);
  RTTI_PROPERTY(you);
  RTTI_PROPERTY(host);
  RTTI_PROPERTY(players);
  RTTI_PROPERTY(pingMs);
  RTTI_PROPERTY(detail);
  RTTI_PROPERTY(fromMenu);
  RTTI_PROPERTY(joinCode);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemPlayer, "TandemPlayer", {
  RTTI_PROPERTY(id);
  RTTI_PROPERTY(name);
  RTTI_PROPERTY(pingMs);
  RTTI_PROPERTY(host);
  RTTI_PROPERTY(you);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemHost, "TandemHost", {
  RTTI_PROPERTY(address);
  RTTI_PROPERTY(port);
  RTTI_PROPERTY(name);
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
  RTTI_METHOD(StartHost);
  RTTI_METHOD(Join);
  RTTI_METHOD(ParseAddress);
  RTTI_METHOD(Leave);
  RTTI_METHOD(ResetChoice);
  RTTI_METHOD(SessionStatus);
  RTTI_METHOD(SessionPlayers);
  RTTI_METHOD(RecentHosts);
  RTTI_METHOD(LocalAddresses);
  RTTI_METHOD(DefaultPort);
  RTTI_METHOD(SendLocalState);
  RTTI_METHOD(TakeEvents);
  RTTI_METHOD(RegisterPuppet);
  RTTI_METHOD(UnregisterPuppet);
  RTTI_METHOD(WantsController);
  RTTI_METHOD(AttachController);
  RTTI_METHOD(RemotePosition);
  RTTI_METHOD(RemoteYaw);
  RTTI_METHOD(RemoteStance);
  RTTI_METHOD(TakeSnap);
  RTTI_METHOD(RemotePlacement);
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
  RTTI_METHOD(SendShotAt);
  RTTI_METHOD(TakeShotsAt);
  RTTI_METHOD(SendShotHit);
  RTTI_METHOD(TakeShotHits);
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
  RTTI_METHOD(SendNpcAction);
  RTTI_METHOD(SendNpcActionReply);
  RTTI_METHOD(SendNpcHold);
  RTTI_METHOD(SendNpcCue);
  RTTI_METHOD(TakeNpcActions);
  RTTI_METHOD(SendWorldItem);
  RTTI_METHOD(TakeWorldItem);
  RTTI_METHOD(TakeLootEvents);
  RTTI_METHOD(SendSharedItem);
  RTTI_METHOD(You);
  RTTI_METHOD(SendVitals);
  RTTI_METHOD(OtherPlayers);
  RTTI_METHOD(LivesChanges);
  RTTI_METHOD(RemoteLife);
  RTTI_METHOD(RemoteVitalsFlags);
  RTTI_METHOD(SendRevive);
  RTTI_METHOD(TakeRevives);
  RTTI_METHOD(SessionLosses);
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
  RTTI_METHOD(SendTimeEffect);
  RTTI_METHOD(TimeChanges);
  RTTI_METHOD(TimePlayers);
  RTTI_METHOD(TimeAbilities);
  RTTI_METHOD(TimeScale);
  RTTI_METHOD(UpdateTimeBubbles);
  RTTI_METHOD(ClearTimeBubbles);
  RTTI_METHOD(TimeFactorOf);
  RTTI_METHOD(TimeBubbleMembers);
  RTTI_METHOD(TimePlayerFactor);
  RTTI_METHOD(IsTimeHostFull);
  RTTI_METHOD(RelayCode);
  RTTI_METHOD(RelayStatus);
  RTTI_METHOD(JoinByCode);
  RTTI_METHOD(IsJoinCode);
  RTTI_METHOD(SendSeat);
  RTTI_METHOD(SendVehicle);
  RTTI_METHOD(RemoteSeatChanges);
  RTTI_METHOD(RemoteSeatVehicle);
  RTTI_METHOD(RemoteSeatSlot);
  RTTI_METHOD(RemoteSeatOwner);
  RTTI_METHOD(RemoteVehiclePosition);
  RTTI_METHOD(RemoteVehicleOrientation);
  RTTI_METHOD(SendTransit);
  RTTI_METHOD(RemoteTransitChanges);
  RTTI_METHOD(RemoteTransitKind);
  RTTI_METHOD(RemoteTransitFlags);
  RTTI_METHOD(RemoteTransitAt);
  RTTI_METHOD(RemoteTransitNext);
  RTTI_METHOD(RemoteTransitVehicle);
  RTTI_METHOD(RemoteTransitSeat);
  RTTI_METHOD(RemoteTransitCarrier);
  RTTI_METHOD(SendNpcSeat);
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
  RTTI_METHOD(SendRegroup);
  RTTI_METHOD(SendRegroupAnswer);
  RTTI_METHOD(TakeRegroups);
  RTTI_METHOD(NodeRefHash);
  RTTI_METHOD(NodeRefOf);
  RTTI_METHOD(Log);
});
