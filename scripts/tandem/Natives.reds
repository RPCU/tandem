// The plugin's natives (plugin/src/natives.cpp). Keep the two in sync: a
// signature that differs from the C++ side fails at load.

public native struct TandemEvent {
  // 1 joined, 2 left, 3 session ended, 4 we are in the session
  native let kind: Int32;
  native let player: Uint32;
  native let name: String;
  // 0 unknown, 1 female, 2 male
  native let body: Int32;
}

public native struct TandemShot {
  native let player: Uint32;
  native let left: Bool;
  // gamedataFxAction: Shoot 16, SilencedShoot 17, MuzzleBrakeShoot 15
  native let effect: Int32;
}

public native struct TandemNpcEvent {
  // 1 spawn, 2 gone
  native let kind: Int32;
  native let npc: Uint32;
  native let record: TweakDBID;
  native let appearance: CName;
  // Where (plain floats: a 16-byte aligned field crashes the script VM).
  native let x: Float;
  native let y: Float;
  native let z: Float;
  // orientation quaternion
  native let i: Float;
  native let j: Float;
  native let k: Float;
  native let r: Float;
  // gone: 0 out of range, 1 dead
  native let reason: Int32;
  // 3 hit (host): who hit
  native let player: Uint32;
  // 3 hit: damage in health points; 4 health (guest): fraction 0..1
  native let amount: Float;
  // 3 hit: 1 weak spot, 2 critical, 4 melee, 8 lethal
  native let flags: Int32;
  // 3 hit: its direction (x, y, z: where it landed)
  native let dx: Float;
  native let dy: Float;
  native let dz: Float;
}

public native struct TandemHackEvent {
  // 1 upload (host: a guest started one on our NPC), 2 upload state (guest),
  // 3 quickhack effect on a host NPC (guest), 4 an NPC hacks our V (guest),
  // 5 someone hacked the car we drive
  native let kind: Int32;
  native let npc: Uint32;
  // the uploader, the effect's V, the hacker
  native let player: Uint32;
  // ObjectAction record; 3: the status effect record
  native let action: TweakDBID;
  // 2: 0 started, 1 done, 2 failed; 3: 1 applied, 0 removed; 5: 0 brakes, 1 explode, 2 accelerate
  native let state: Int32;
  // 1: 1 traceable; 4: 1 reveal, 2 directional, 4 skip being hacked
  native let flags: Int32;
  // upload time; 5: how long it lasts
  native let seconds: Float;
}

// Guest: one of the host's NPCs hit our V (Combat.reds).
public native struct TandemPlayerHit {
  // the attacker as the host shares it (0: not shared)
  native let npc: Uint32;
  // damage per type, the attacker's side only
  native let physical: Float;
  native let thermal: Float;
  native let electric: Float;
  native let chemical: Float;
  // gamedataAttackType
  native let attackType: Int32;
  // 1 weak spot, 2 critical, 4 melee, 8 lethal
  native let flags: Int32;
  // the attacker's armour penetration, 0..1
  native let pierce: Float;
  // where it landed, and its direction (plain floats, as above)
  native let x: Float;
  native let y: Float;
  native let z: Float;
  native let dx: Float;
  native let dy: Float;
  native let dz: Float;
  // the attack record, and the NPC's weapon item record
  native let attack: TweakDBID;
  native let weapon: TweakDBID;
}
// Host: a guest's V made a noise, for our NPCs to hear (Stealth.reds).
public native struct TandemNoise {
  native let player: Uint32;
  // gamedataStimType
  native let stim: Int32;
  // 0: the stimulus's own radius
  native let radius: Float;
  native let propagation: Bool;
}

// An NPC's new speed in the time bubbles (Time.reds): 1 = normal again.
public native struct TandemTimeChange {
  native let npc: Uint32;
  native let factor: Float;
}

// Grapples, takedowns, finishers and carried bodies (Takedowns.reds).
public native struct TandemNpcAction {
  // 1 a guest's request (host), 2 the host's answer (guest), 3 who holds an NPC, 4 a cue for us
  native let kind: Int32;
  native let npc: Uint32;
  // 1: who asks; 3: the holder (0: released); 4: us
  native let player: Uint32;
  // 1, 2: NpcActionKind (1 grab ... 10 dispose); 3: NpcHoldKind; 4: NpcCueKind
  native let action: Int32;
  native let name: CName;
  // the item: a TweakDB id (finisher: weapon) or an entity (dispose: container)
  native let record: TweakDBID;
  native let entity: EntityID;
  native let amount: Float;
  // throw: the impulse
  native let dx: Float;
  native let dy: Float;
  native let dz: Float;
  // 2: accepted, else why not (reason)
  native let accepted: Bool;
  native let reason: Int32;
}

public native class TandemNative extends IScriptable {
  public static native func IsConfigured() -> Bool
  public static native func IsStarted() -> Bool
  public static native func Start(body: Int32)
  public static native func Stop()
  public static native func SendLocalState(position: Vector4, orientation: Quaternion, velocity: Vector4, locomotion: Int32, flags: Int32)
  public static native func TakeEvents() -> array<TandemEvent>
  public static native func RegisterPuppet(player: Uint32, id: EntityID)
  public static native func UnregisterPuppet(player: Uint32)
  public static native func WantsController(player: Uint32) -> Bool
  public static native func AttachController(player: Uint32, entity: ref<Entity>) -> Bool
  public static native func RemotePosition(player: Uint32) -> Vector4
  public static native func RemoteYaw(player: Uint32) -> Float
  public static native func RemoteStance(player: Uint32) -> Int32
  public static native func PuppetRecord(body: Int32) -> String
  public static native func IsProbe() -> Bool
  public static native func ClipLengths(entity: ref<Entity>) -> String
  public static native func ApplyLook(entity: ref<Entity>, state: ref<gameuiICharacterCustomizationState>, male: Bool) -> Int32
  public static native func TakeCommand() -> String
  public static native func MarkThirdPerson(entity: ref<Entity>) -> Bool
  public static native func IsMirror() -> Bool
  public static native func MirrorPlayerTo(entity: ref<Entity>)
  public static native func ClearMirrors()
  public static native func PoseProbe(entity: ref<Entity>)
  public static native func PoseScan(entity: ref<Entity>)
  public static native func PoseTree(entity: ref<Entity>)
  public static native func PoseBones(entity: ref<Entity>)
  public static native func PoseMark(entity: ref<Entity>, diff: Bool)
  public static native func PoseHook() -> Bool
  public static native func PoseWatch(entity: ref<Entity>, label: String, depth: Int32)
  public static native func PoseCopy(source: ref<Entity>, target: ref<Entity>)
  public static native func PoseReport()
  public static native func SendNpcSpawn(npc: Uint32, record: TweakDBID, appearance: CName, position: Vector4, orientation: Quaternion)
  public static native func SendNpcStates(npcs: array<Uint32>, positions: array<Vector4>, orientations: array<Quaternion>, detected: array<Int32>)
  public static native func SendNpcGone(npc: Uint32, reason: Int32, killer: Uint32)
  public static native func SendNpcHealth(npc: Uint32, health: Float)
  public static native func SendNpcHit(npc: Uint32, damage: Float, flags: Int32, position: Vector4, direction: Vector4)
  public static native func SendNpcEffect(npc: Uint32, effects: array<TweakDBID>)
  public static native func SendPlayerHit(player: Uint32, npc: Uint32, physical: Float, thermal: Float, electric: Float, chemical: Float, attackType: Int32, flags: Int32, pierce: Float, position: Vector4, direction: Vector4, attack: TweakDBID, weapon: TweakDBID)
  public static native func TakePlayerHits() -> array<TandemPlayerHit>
  public static native func NpcDetected(npc: Uint32) -> Int32
  public static native func NpcsDetectingUs() -> Int32
  public static native func NpcAwareness(npc: Uint32) -> Float
  public static native func SendNpcAwareness(npcs: array<Uint32>, players: array<Uint32>, levels: array<Float>)
  public static native func SendStealth(flags: Int32, visibility: Float, camo: TweakDBID)
  public static native func RemoteStealthChanges(player: Uint32) -> Int32
  public static native func RemoteStealthFlags(player: Uint32) -> Int32
  public static native func RemoteStealthVisibility(player: Uint32) -> Float
  public static native func RemoteStealthCamo(player: Uint32) -> TweakDBID
  public static native func SendNoise(stim: Int32, radius: Float, propagation: Bool)
  public static native func TakeNoises() -> array<TandemNoise>
  public static native func TakeNpcEvents() -> array<TandemNpcEvent>
  public static native func SendNpcHack(npc: Uint32, action: TweakDBID, seconds: Float, flags: Int32)
  public static native func SendNpcHackState(npc: Uint32, player: Uint32, action: TweakDBID, step: Int32, seconds: Float)
  public static native func SendNpcStatus(npc: Uint32, player: Uint32, effect: TweakDBID, applied: Bool)
  public static native func SendPlayerHack(player: Uint32, npc: Uint32, action: TweakDBID, flags: Int32)
  public static native func SendVehicleHack(driver: Uint32, hack: Int32, seconds: Float)
  public static native func TakeHackEvents() -> array<TandemHackEvent>
  public static native func NpcPuppetId(npc: Uint32) -> Uint32
  public static native func SendVehicleRadio(on: Bool, station: Int32, track: CName, trackStarted: Bool)
  public static native func RemoteRadioChanges(player: Uint32) -> Int32
  public static native func RemoteRadioOn(player: Uint32) -> Bool
  public static native func RemoteRadioStation(player: Uint32) -> Int32
  public static native func RemoteRadioTrack(player: Uint32) -> CName
  public static native func RemoteRadioTrackAge(player: Uint32) -> Float
  public static native func SendNpcAction(npc: Uint32, kind: Int32, name: CName, record: TweakDBID, entity: EntityID, amount: Float, direction: Vector4)
  public static native func SendNpcActionReply(player: Uint32, npc: Uint32, kind: Int32, accepted: Bool, reason: Int32)
  public static native func SendNpcHold(npc: Uint32, player: Uint32, how: Int32, name: CName)
  public static native func SendNpcCue(npc: Uint32, player: Uint32, cue: Int32)
  public static native func TakeNpcActions() -> array<TandemNpcAction>
  public static native func HoldPuppet(player: Uint32, hold: Bool)
  public static native func IsHost() -> Bool
  public static native func SendWorld(gameTimeSeconds: Int32, weather: CName, paused: Bool)
  public static native func HostWorldChanges() -> Int32
  public static native func HostGameTime() -> Int32
  public static native func HostWeather() -> CName
  public static native func SendPaused(paused: Bool)
  public static native func PausedPlayers() -> array<Uint32>
  public static native func PausedChanges() -> Int32
  public static native func PlayerName(player: Uint32) -> String
  public static native func SendTimeEffect(abilities: Int32, scale: Float)
  public static native func TimeChanges() -> Int32
  public static native func TimePlayers() -> array<Uint32>
  public static native func TimeAbilities(player: Uint32) -> Int32
  public static native func TimeScale(player: Uint32) -> Float
  public static native func UpdateTimeBubbles(owners: array<Uint32>, scales: array<Float>, ownerPositions: array<Vector4>, npcs: array<Uint32>, npcPositions: array<Vector4>) -> array<TandemTimeChange>
  public static native func ClearTimeBubbles() -> array<TandemTimeChange>
  public static native func TimeFactorOf(npc: Uint32) -> Float
  public static native func TimeBubbleMembers(player: Uint32) -> Int32
  public static native func IsTimeHostFull() -> Bool
  public static native func SendSeat(vehicle: TweakDBID, seat: CName, owner: Uint32)
  public static native func SendVehicle(position: Vector4, orientation: Quaternion, velocity: Vector4)
  public static native func RemoteSeatChanges(player: Uint32) -> Int32
  public static native func RemoteSeatVehicle(player: Uint32) -> TweakDBID
  public static native func RemoteSeatSlot(player: Uint32) -> CName
  public static native func RemoteSeatOwner(player: Uint32) -> Uint32
  public static native func RemoteVehiclePosition(player: Uint32) -> Vector4
  public static native func RemoteVehicleOrientation(player: Uint32) -> Quaternion
  public static native func SendHeld(right: TweakDBID, left: TweakDBID)
  public static native func SendShot(left: Bool, effect: Int32)
  public static native func RemoteHeldChanges(player: Uint32) -> Int32
  public static native func RemoteHeld(player: Uint32, left: Bool) -> TweakDBID
  public static native func TakeShots() -> array<TandemShot>
  public static native func SubmitAppearance(state: ref<gameuiICharacterCustomizationState>, male: Bool, items: array<TweakDBID>) -> Int32
  public static native func RemoteAppearanceChanges(player: Uint32) -> Int32
  public static native func RemoteItems(player: Uint32) -> array<TweakDBID>
  public static native func ApplyRemoteLook(entity: ref<Entity>, player: Uint32) -> Int32
  public static native func PoseSource(entity: ref<Entity>)
  public static native func PoseBind(player: Uint32, entity: ref<Entity>)
  public static native func PoseLoop(player: Uint32)
  public static native func PoseStreamReport()
  public static native func Log(text: String)
  // Invites through tandem-relay: the host's join code ("K7Q-2M9", "" none
  // yet or no relay), the relay link's state ("" no relay, "connecting",
  // "ready", else the problem), and joining by a typed code (false: not a
  // code, or no relay in tandem.ini; starts now, or with the next game).
  public static native func RelayCode() -> String
  public static native func RelayStatus() -> String
  public static native func JoinByCode(code: String) -> Bool
}
