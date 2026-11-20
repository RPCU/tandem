// The story's natives (plugin/src/story_natives.cpp). Keep the two in sync:
// tools/check-natives.py checks them with Natives.reds.

public native struct TandemStoryEvent {
  // 1 in the session (player: us, hash: the host), 2 session ended,
  // 3 player joined, 4 player left;
  // 10 journal snapshot begins (index: entries), 11 journal entry (hash,
  // state, flags 1: notify, extra 1: part of a snapshot), 12 snapshot ends;
  // 14 tracked (hash; 0 none);
  // 20 the host's call (contact, state: phase, index: mode, extra: visuals,
  // flags: 1 rejectable 2 avatar 4 player calling 8 player triggered
  // 16 rejected, player: who answered);
  // 21 (host) a guest's phone action (player, index: 1 answer 2 reject
  // 3 dial, contact), 22 (host) a guest's text reply (player, hash);
  // 30 a dialogue line (line, index: type, flags: 1 hide 2 persistent,
  // duration, player: a V speaking, npc, name, text);
  // 31 the host's choice hub (extra: hub id, flags: 1 phone lock, index:
  // options, duration: timed seconds, name: title; no options: closed),
  // 32 one option (extra: hub, index, flags: type bits, text);
  // 33 a cursor (player, extra: hub, index, state: 0 hover 1 suggest 2 picked);
  // 40 a player's story state (player, state: tier, flags: 1 braindance
  // 2 replacer 4 in a call);
  // 41 a quest reward for our V (reward, hash: its quest);
  // 43 a vehicle the host's quest gave while we took part (reward: the
  // vehicle record, hash: its quest);
  // 42 a story effect on the host's V (reward: the status effect, flags 1:
  // on, 0: gone);
  // 44 the host's story restrictions changed (player: the host, index: how
  // many; the set itself: TandemStory.RestrictHostSet);
  // scene replay (a guest; docs/architecture/scene-replay.md):
  // 50 the host's scene begins (line: the scene, hash: its play, state: the
  // host's tier, flags: 1 skipping 2 held 8 staged, duration: seconds it has
  // played), 51 it changed (the same), 52 it is over (flags 4: interrupted);
  // 53 an NPC acts in it (npc, flags: 1 a ghost, Johnny; 2 not shown now;
  // 4 its body is streamed), 54 it no longer does (npc);
  // 55 a voiced line begins (line, npc or player: the speaker, duration:
  // seconds into it, index: its length in ms, flags: 2 holocall 4 a bark,
  // hash: its play), 56 it stops (line, npc, player);
  // 57 an actor holds a prop (hash: the prop's number, reward: the item,
  // npc or player: who holds it, flags: 2 in its left hand), 58 the prop is
  // gone (hash);
  // quest interactions (docs/architecture/story.md, "Quest interactions"):
  // 60 (host) a guest's question (player, hash: its number, entity: the
  // object (placed: the same id in every game), choice, detail, layer, flags: 1 its checks passed
  // 2 it casts there 4 loot, x y z: where its V stood), 61 (guest) the
  // host's verdict on ours (hash: its number, state: 1 performed 2 host
  // only 3 local 4 taken 5 too far 6 missing 7 busy, entity, choice, detail)
  native let kind: Int32;
  native let player: Uint32;
  native let hash: Uint32;
  native let state: Int32;
  native let flags: Int32;
  native let index: Int32;
  native let extra: Int32;
  native let contact: CName;
  native let line: Uint64;
  native let duration: Float;
  native let npc: Uint32;
  native let reward: TweakDBID;
  native let name: String;
  native let text: String;
  native let entity: EntityID;
  native let choice: CName;
  native let detail: Uint64;
  native let layer: CName;
  native let x: Float;
  native let y: Float;
  native let z: Float;
}

public native class TandemStory extends IScriptable {
  public static native func StoryOn() -> Bool;
  public static native func FreezeAvailable() -> Bool;
  public static native func Freeze(on: Bool) -> Bool;
  public static native func Frozen() -> Bool;
  public static native func FreezeHeld() -> Uint32;
  public static native func FreezeLost() -> Bool;
  public static native func FreezeReport() -> String;
  public static native func Grouped(grouped: Bool) -> Void;
  public static native func IsGrouped() -> Bool;
  public static native func QuestTrace(on: Bool) -> Void;
  public static native func TakeEvents() -> array<TandemStoryEvent>;
  public static native func You() -> Uint32;
  public static native func HostId() -> Uint32;
  public static native func SendJournal(snapshot: Bool, hashes: array<Uint32>, states: array<Int32>, notify: array<Bool>) -> Void;
  public static native func SendTracked(hash: Uint32) -> Void;
  public static native func SendCall(contact: CName, phase: Int32, mode: Int32, visuals: Int32, flags: Int32, answeredBy: Uint32) -> Void;
  public static native func SendPhoneAction(action: Int32, contact: CName) -> Void;
  public static native func SendReply(entry: Uint32) -> Void;
  public static native func SendLine(line: Uint64, type: Int32, flags: Int32, duration: Float, speakerPlayer: Uint32, speakerName: String, text: String) -> Void;
  public static native func SendHub(hub: Int32, flags: Int32, title: String, texts: array<String>, types: array<Int32>, timed: Float) -> Void;
  public static native func SendCursor(hub: Int32, index: Int32, kind: Int32) -> Void;
  public static native func SendState(tier: Int32, flags: Int32) -> Void;
  public static native func QuestMoved(questHash: Uint32, x: Float, y: Float, z: Float) -> Void;
  public static native func Reward(reward: TweakDBID, x: Float, y: Float, z: Float) -> Int32;
  // Host: a vehicle came into our garage: the guests that took part are told
  // (event 43 there: reward: the vehicle record, hash: its quest).
  public static native func VehicleUnlocked(vehicle: TweakDBID, x: Float, y: Float, z: Float) -> Int32;
  // tandem.ini vehicle_rewards = keep.
  public static native func VehicleRewardsKept() -> Bool;
  // The ledger of what our V earned in co-op: the save's words in, the
  // words to save out; a vehicle earned (true the first time), kept.
  public static native func CarryLoad(words: array<Uint64>);
  public static native func CarryWords() -> array<Uint64>;
  public static native func CarryVehicle(vehicle: TweakDBID, questHash: Uint32, owned: Bool) -> Bool;
  public static native func CarryVehicleKept(vehicle: TweakDBID);
  public static native func CarryVehicles() -> array<TweakDBID>;
  public static native func CarryVehicleIsKept(vehicle: TweakDBID) -> Bool;
  // Guest: a step of the host's quest with us (kind: gameJournalQuestType;
  // own: our own state of it, gameJournalEntryState), its end, a reward (0
  // pay, 1 paid already, 2 we did that quest alone), our own story's reward
  // (false: co-op paid it), "done with NAME", a line for the logs.
  public static native func CarryQuest(questHash: Uint32, kind: Int32, own: Int32, host: String);
  public static native func CarryCompleted(questHash: Uint32);
  public static native func CarryPay(questHash: Uint32, reward: TweakDBID) -> Int32;
  public static native func CarryOwnPays(questHash: Uint32, reward: TweakDBID) -> Bool;
  public static native func CarryWith(questHash: Uint32, host: String) -> Bool;
  public static native func CarryReport() -> String;
  public static native func SendEffect(effect: TweakDBID, applied: Bool) -> Void;
  public static native func SendRestrictions(effects: array<TweakDBID>) -> Bool;
  // Quest interactions: tandem.ini quest_interact; a CName as a number;
  // (guest) ask the host about our V's choice on its object (the question's
  // number), what it answered this session (-1 never asked); (host) answer
  // a guest's question, whether its V could reach our object then (lag
  // compensated), the tandem.ini override for a record (1 host only,
  // 2 allow, 0 none), whether an action casts its doer (1, 0, -1 unknown).
  public static native func InteractOn() -> Bool;
  public static native func NameKey(name: CName) -> Uint64;
  public static native func SendInteract(entity: EntityID, choice: CName, detail: Uint64, layer: CName, flags: Int32, x: Float, y: Float, z: Float) -> Uint32;
  public static native func InteractKnown(entity: EntityID, choice: CName, detail: Uint64) -> Int32;
  public static native func AnswerInteract(player: Uint32, seq: Uint32, verdict: Int32) -> Void;
  public static native func InteractInReach(player: Uint32, seq: Uint32, x: Float, y: Float, z: Float) -> Bool;
  public static native func InteractOverride(record: TweakDBID) -> Int32;
  public static native func ActionCasts(entity: CName, state: CName, action: CName) -> Int32;
  public static native func SceneCasts(npc: Uint32) -> Bool;
  // Host: 0 let through, 1 refused, 2 refused and to say so (act: 0 strike,
  // 1 takedown, 2 quickhack).
  public static native func BackstopRefuses(moment: Bool, cast: Bool, questNpc: Bool, kept: Bool, inScene: Bool, act: Int32) -> Int32;
  public static native func RestrictUpdate(grouped: Bool, away: Bool, hostKnown: Bool, hostX: Float, hostY: Float, hostZ: Float, x: Float, y: Float, z: Float, tier: Int32, holds: array<TweakDBID>) -> array<TweakDBID>;
  public static native func RestrictRemovals() -> array<TweakDBID>;
  public static native func RestrictLift() -> Void;
  public static native func RestrictTheirs(effect: TweakDBID) -> Void;
  public static native func RestrictGone(effect: TweakDBID) -> Void;
  public static native func RestrictHolds(effect: TweakDBID) -> Bool;
  public static native func RestrictOurs(effect: TweakDBID) -> Bool;
  public static native func RestrictInMoment() -> Bool;
  public static native func RestrictHostSet() -> array<TweakDBID>;
  public static native func SceneClass() -> Int32;
  public static native func SetPrivate(on: Bool) -> Void;
  public static native func PausePose(player: Uint32, paused: Bool) -> Void;
  public static native func SceneReplay() -> Int32;
  public static native func SceneActor(npc: Uint32, x: Float, y: Float, z: Float, flags: Int32) -> Void;
  public static native func SceneProp(key: Uint64, record: TweakDBID, holderNpc: Uint32, holderPlayer: Uint32, leftHand: Bool) -> Void;
  public static native func SceneHolds(npc: Uint32, player: Uint32) -> Bool;
  public static native func SceneSample(playing: Bool, isPrivate: Bool, tier: Int32, rate: Float, held: Bool, x: Float, y: Float, z: Float, yaw: Float) -> Int32;
  public static native func SceneVoice(line: Uint64, speakerNpc: Uint32, speakerPlayer: Uint32, duration: Float, flags: Int32) -> Bool;
  public static native func SceneVoiceHide(line: Uint64) -> Bool;
  public static native func ScenePlaying() -> Uint64;
  public static native func SceneAnchor() -> Vector4;
  public static native func SceneYaw() -> Float;
  public static native func SceneSeconds() -> Float;
  public static native func SceneActs(npc: Uint32) -> Bool;
  public static native func SceneBodies() -> Void;
  public static native func SceneBody(x: Float, y: Float, z: Float) -> Void;
  public static native func WatchSpot(hostX: Float, hostY: Float, hostZ: Float, x: Float, y: Float, z: Float) -> Vector4;
  public static native func KeepOut(x: Float, y: Float, z: Float, radius: Float, maxStep: Float) -> Vector4;
  public static native func ActorSource(npc: Uint32, entity: ref<Entity>) -> Void;
  public static native func ActorBind(npc: Uint32, entity: ref<Entity>) -> Void;
  public static native func ActorsClear() -> Void;
  public static native func SceneReport() -> String;
  public static native func LineId(line: Uint64) -> CRUID;
  public static native func LineValue(id: CRUID) -> Uint64;
  public static native func StandInLine(line: Uint64) -> Uint64;
}
