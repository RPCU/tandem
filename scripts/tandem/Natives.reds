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
  // 1 spawn, 2 gone, 3 hit (host), 4 health, 5 effect (host), 6 seat
  // (guest: it sits in player's vehicle in slot `appearance`; player 0: out)
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

// The co-op menu's view of the session (Coop.reds).
public native struct TandemStatus {
  // the chosen session: 0 off, 1 host, 2 join
  native let role: Int32;
  // 0 off, 1 starting, 2 connecting, 3 in the session, 4 retrying, 5 failed
  native let state: Int32;
  // join: the host's address
  native let address: String;
  native let port: Int32;
  native let you: Uint32;
  native let host: Uint32;
  native let players: Int32;
  // guest: to the host; -1 unknown
  native let pingMs: Int32;
  // why the last session ended, or failed
  native let detail: String;
  // chosen in the co-op menu (else tandem.ini)
  native let fromMenu: Bool;
  // join: the code ("K7Q-2M9"), "" by address
  native let joinCode: String;
}

public native struct TandemPlayer {
  native let id: Uint32;
  native let name: String;
  // our link to them; -1: unknown, or ourselves
  native let pingMs: Int32;
  native let host: Bool;
  native let you: Bool;
}

// A host this game joined (recent.txt next to the plugin).
public native struct TandemHost {
  native let address: String;
  native let port: Int32;
  native let name: String;
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
// Guest: one of the host's NPCs fires at our V (Shots.reds): fired from
// its proxy here, at `factor`, `boost` times faster for `boostSeconds`
// first (it catches up with the host's timeline).
public native struct TandemShotAt {
  native let npc: Uint32;
  // the ranged attack asked for (none: the weapon's own), the NPC's weapon
  native let attack: TweakDBID;
  native let weapon: TweakDBID;
  // the AI's time-between-hits coefficient, its gamedataTriggerMode
  native let tbh: Float;
  native let trigger: Int32;
  native let factor: Float;
  native let boost: Float;
  native let boostSeconds: Float;
}

// Host: a guest's game says a shot it simulated hit its V (Shots.reds).
public native struct TandemShotHit {
  native let player: Uint32;
  native let npc: Uint32;
  // where it landed, and its direction
  native let x: Float;
  native let y: Float;
  native let z: Float;
  native let dx: Float;
  native let dy: Float;
  native let dz: Float;
}

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

// A host NPC's animation state (NpcAnims.reds).
public native struct TandemNpcAnim {
  native let npc: Uint32;
  // gamedataNPCHighLevelState, gamedataNPCUpperBodyState, gamedataNPCStanceState
  native let highLevel: Int32;
  native let upperBody: Int32;
  native let stance: Int32;
  // 1 weapon drawn, 2 in a workspot
  native let flags: Int32;
  native let weapon: TweakDBID;
  // a player id, or 0x10000 + an NPC id (0: nobody)
  native let target: Uint32;
  // the workspot resource's path hash, its tree entry, the animation playing
  native let workspot: Uint64;
  native let workspotEntry: Int32;
  native let workspotAnim: CName;
}

public native struct TandemNpcShot {
  native let npc: Uint32;
  // gamedataFxAction: Shoot 16, SilencedShoot 17, EnterReload 5, ExitReload 11
  native let effect: Int32;
  // where it went (zero: unknown)
  native let x: Float;
  native let y: Float;
  native let z: Float;
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

public native struct TandemRegroup {
  // 1 an offer to come to `player`, 2 their answer to our offer
  native let kind: Int32;
  native let player: Uint32;
  native let offer: Uint32;
  // 0 fast travel, 1 asked, 2 story, 3 teleport
  native let reason: Int32;
  // The fast travel point `player` is at (invalid: none), its marker node's
  // hash (TandemNative.NodeRefOf; 0: none).
  native let point: TweakDBID;
  native let marker: Uint64;
  // Where `player` stands (plain floats: see TandemNpcEvent); yaw in radians.
  native let x: Float;
  native let y: Float;
  native let z: Float;
  native let yaw: Float;
  native let label: String;
  // 2: the answer
  native let accepted: Bool;
}

public native struct TandemDeviceEvent {
  // 1 (guest) the host's word, 2 (host) a guest asks what it is, 3 (host) a
  // guest made it this, 4 (guest) the host sends its record again
  native let kind: Int32;
  native let entity: EntityID;
  // its persistent state's class
  native let cls: CName;
  // EDeviceStatus
  native let status: Int32;
  // the kDevice* state bits (Devices.reds)
  native let flags: Int32;
  // floor, station, security state (-1: none)
  native let value: Int32;
  // 1: who changed it last (0: the world); 2, 3: who asks
  native let player: Uint32;
  // 1: a record to set, not a change to play
  native let quiet: Bool;
}

public native struct TandemLootEvent {
  // 1 a stack of a shared world item, 2 a stack taken, 3 an important item
  // another player got, 4 (guest) one on the host's corpse of NPC `id`
  native let kind: Int32;
  native let id: Uint32;
  // 1: the dropper; 2: who has it (0: nobody, it was gone)
  native let player: Uint32;
  // 1: the dropper's own number for it
  native let token: Uint32;
  native let stack: Int32;
  // 1: how many stacks the item has
  native let stacks: Int32;
  native let record: TweakDBID;
  native let seed: Uint32;
  // 1: 0 when already taken
  native let quantity: Int32;
  // 1: where it lies
  native let x: Float;
  native let y: Float;
  native let z: Float;
}

// Down and out: someone reviving our V (Revive.reds).
public native struct TandemReviveEvent {
  // the reviver
  native let player: Uint32;
  // 0 start, 1 cancel, 2 done
  native let step: Int32;
}

public native class TandemNative extends IScriptable {
  public static native func IsConfigured() -> Bool
  public static native func IsStarted() -> Bool
  public static native func Start(body: Int32)
  public static native func Stop()
  public static native func StartHost(port: Int32) -> Bool
  public static native func Join(address: String, port: Int32) -> Bool
  public static native func ParseAddress(text: String) -> TandemHost
  public static native func Leave()
  public static native func ResetChoice()
  public static native func SessionStatus() -> TandemStatus
  public static native func SessionPlayers() -> array<TandemPlayer>
  public static native func RecentHosts() -> array<TandemHost>
  public static native func LocalAddresses() -> array<String>
  public static native func DefaultPort() -> Int32
  // Phantom Liberty as the game says (TandemCoop.ReportExpansion): 1 enabled,
  // 2 owned but not installed or not enabled, 3 not owned.
  public static native func SetExpansion(state: Int32)
  // Why co-op is off (the game's version, a missing or old mod; empty: it
  // is not), what differs from the other players' games, and the bug report
  // zip (its path from the game's folder; "": not written, see the log).
  public static native func CompatProblems() -> array<String>
  public static native func CompatWarnings() -> array<String>
  public static native func WriteReport() -> String
  // The recorder (tandem.ini recorder = on): independent of the session.
  public static native func RecorderOn() -> Bool
  public static native func Recording() -> Bool
  public static native func RecorderPose() -> Bool
  public static native func RecorderStart(body: Int32)
  public static native func RecorderLeft()
  public static native func RecorderTick(begin: Bool)
  public static native func RecorderCut()
  public static native func RecorderSet(on: Bool)
  public static native func Setting(key: String) -> String
  public static native func SetSetting(key: String, value: String) -> Bool
  public static native func RecorderStatus() -> String
  public static native func SendLocalState(position: Vector4, orientation: Quaternion, velocity: Vector4, locomotion: Int32, flags: Int32)
  public static native func TakeEvents() -> array<TandemEvent>
  public static native func RegisterPuppet(player: Uint32, id: EntityID)
  public static native func UnregisterPuppet(player: Uint32)
  public static native func WantsController(player: Uint32) -> Bool
  public static native func AttachController(player: Uint32, entity: ref<Entity>) -> Bool
  public static native func RemotePosition(player: Uint32) -> Vector4
  public static native func RemoteYaw(player: Uint32) -> Float
  public static native func RemoteStance(player: Uint32) -> Int32
  public static native func TakeSnap(player: Uint32) -> Bool
  public static native func RemotePlacement(player: Uint32) -> Int32
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
  public static native func SendShotAt(player: Uint32, npc: Uint32, attack: TweakDBID, weapon: TweakDBID, tbh: Float, trigger: Int32, factor: Float)
  public static native func TakeShotsAt() -> array<TandemShotAt>
  public static native func SendShotHit(npc: Uint32, position: Vector4, direction: Vector4)
  public static native func TakeShotHits() -> array<TandemShotHit>
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
  public static native func SendWorldItem(token: Uint32, position: Vector4, records: array<TweakDBID>, seeds: array<Uint32>, quantities: array<Int32>)
  public static native func TakeWorldItem(id: Uint32, stack: Int32)
  public static native func TakeLootEvents() -> array<TandemLootEvent>
  public static native func SendSharedItem(kind: Int32, npc: Uint32, record: TweakDBID, seed: Uint32, quantity: Int32)
  // Devices (Devices.reds): what one of ours is now (host; by: the player
  // whose request made it so), what our V made one (guest), one that came
  // into our world (guest), and what the session says of them.
  public static native func EntityHex(entity: EntityID) -> String
  public static native func ReportDevice(entity: EntityID, cls: CName, status: Int32, flags: Int32, value: Int32, by: Uint32)
  public static native func UseDevice(entity: EntityID, cls: CName, status: Int32, flags: Int32, value: Int32)
  public static native func AskDevice(entity: EntityID, cls: CName, status: Int32, flags: Int32, value: Int32)
  public static native func TakeDeviceEvents() -> array<TandemDeviceEvent>
  public static native func You() -> Uint32
  public static native func SendVitals(life: Int32, flags: Int32)
  public static native func OtherPlayers() -> array<Uint32>
  public static native func LivesChanges() -> Int32
  public static native func RemoteLife(player: Uint32) -> Int32
  public static native func RemoteVitalsFlags(player: Uint32) -> Int32
  public static native func SendRevive(target: Uint32, step: Int32)
  public static native func TakeRevives() -> array<TandemReviveEvent>
  public static native func SessionLosses() -> Int32
  public static native func SendNpcAnim(npc: Uint32, highLevel: Int32, upperBody: Int32, stance: Int32, flags: Int32, weapon: TweakDBID, target: Uint32, workspot: Uint64, workspotEntry: Int32, workspotAnim: CName)
  public static native func SendNpcShot(npc: Uint32, effect: Int32, target: Vector4)
  public static native func TakeNpcAnims() -> array<TandemNpcAnim>
  public static native func TakeNpcShots() -> array<TandemNpcShot>
  public static native func SetResourcePath(object: ref<IScriptable>, property: CName, path: Uint64) -> Bool
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
  public static native func TimeBubbleScale(abilities: Int32, scale: Float) -> Float
  public static native func TimePlayerFactor(player: Uint32, own: Float, position: Vector4, owners: array<Uint32>, scales: array<Float>, ownerPositions: array<Vector4>, inside: Bool) -> Float
  public static native func IsTimeHostFull() -> Bool
  public static native func SendSeat(vehicle: TweakDBID, seat: CName, owner: Uint32)
  public static native func SendVehicle(position: Vector4, orientation: Quaternion, velocity: Vector4)
  public static native func RemoteSeatChanges(player: Uint32) -> Int32
  public static native func RemoteSeatVehicle(player: Uint32) -> TweakDBID
  public static native func RemoteSeatSlot(player: Uint32) -> CName
  public static native func RemoteSeatOwner(player: Uint32) -> Uint32
  public static native func RemoteVehiclePosition(player: Uint32) -> Vector4
  public static native func RemoteVehicleOrientation(player: Uint32) -> Quaternion
  // Rides (Transit.reds): kind 0 none, 1 metro, 2 ride, 3 scene, 4 along; flags 1
  // skipping, 2 seats for partners; stations are ENcartStations (0 unknown).
  public static native func SendTransit(kind: Int32, flags: Int32, at: Int32, next: Int32, vehicle: EntityID, seat: CName, carrier: Uint32)
  public static native func RemoteTransitChanges(player: Uint32) -> Int32
  public static native func RemoteTransitKind(player: Uint32) -> Int32
  public static native func RemoteTransitFlags(player: Uint32) -> Int32
  public static native func RemoteTransitAt(player: Uint32) -> Int32
  public static native func RemoteTransitNext(player: Uint32) -> Int32
  public static native func RemoteTransitVehicle(player: Uint32) -> EntityID
  public static native func RemoteTransitSeat(player: Uint32) -> CName
  public static native func RemoteTransitCarrier(player: Uint32) -> Uint32
  // Host: our NPC sits in a player's vehicle (owner 0: got out).
  public static native func SendNpcSeat(npc: Uint32, owner: Uint32, seat: CName)
  public static native func SendHeld(right: TweakDBID, left: TweakDBID)
  public static native func SendShot(left: Bool, effect: Int32)
  public static native func RemoteHeldChanges(player: Uint32) -> Int32
  public static native func RemoteHeld(player: Uint32, left: Bool) -> TweakDBID
  public static native func TakeShots() -> array<TandemShot>
  public static native func SubmitAppearance(state: ref<gameuiICharacterCustomizationState>, male: Bool, items: array<TweakDBID>) -> Int32
  public static native func RemoteAppearanceChanges(player: Uint32) -> Int32
  public static native func RemoteItems(player: Uint32) -> array<TweakDBID>
  public static native func ApplyRemoteLook(entity: ref<Entity>, player: Uint32) -> Int32
  // A stand-in look of that body (1 female, 2 male) when a player's own is
  // missing or failed: the last real one seen (-1: none yet).
  public static native func ApplyStandInLook(entity: ref<Entity>, body: Int32) -> Int32
  // How many body keys the player's latest look has (0: none).
  public static native func RemoteLookKeys(player: Uint32) -> Int32
  public static native func PoseSource(entity: ref<Entity>)
  public static native func PoseBind(player: Uint32, entity: ref<Entity>)
  public static native func PoseLoop(player: Uint32)
  public static native func PoseStreamReport()
  public static native func SendRegroup(to: Uint32, reason: Int32, point: TweakDBID, marker: NodeRef, position: Vector4, orientation: Quaternion, label: String) -> Uint32
  public static native func SendRegroupAnswer(to: Uint32, offer: Uint32, accepted: Bool)
  public static native func TakeRegroups() -> array<TandemRegroup>
  public static native func NodeRefHash(node: NodeRef) -> Uint64
  public static native func NodeRefOf(hash: Uint64) -> NodeRef
  public static native func Log(text: String)
  // Invites through tandem-relay: the host's join code ("K7Q-2M9", "" none
  // yet or no relay), the relay link's state ("" no relay, "connecting",
  // "ready", else the problem), and joining by a typed code (false: not a
  // code, or no relay in tandem.ini; starts now, or with the next game).
  public static native func RelayCode() -> String
  public static native func RelayStatus() -> String
  public static native func JoinByCode(code: String) -> Bool
  public static native func RecordStart(name: String) -> Bool
  public static native func RecordStop()
  public static native func IsJoinCode(text: String) -> Bool
}
