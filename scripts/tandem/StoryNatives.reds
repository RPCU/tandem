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
  // 42 a story effect on the host's V (reward: the status effect, flags 1:
  // on, 0: gone);
  // scene replay (a guest; docs/architecture/scene-replay.md):
  // 50 the host's scene begins (line: the scene, hash: its play, state: the
  // host's tier, flags: 1 skipping 2 held 8 staged, duration: seconds it has
  // played), 51 it changed (the same), 52 it is over (flags 4: interrupted);
  // 53 an NPC acts in it (npc, flags: 1 a ghost, Johnny; 2 not shown now;
  // 4 its body is streamed), 54 it no longer does (npc);
  // 55 a voiced line begins (line, npc or player: the speaker, duration:
  // seconds into it, index: its length in ms, flags: 2 holocall 4 a bark,
  // hash: its play), 56 it stops (line, npc, player)
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
  public static native func SendEffect(effect: TweakDBID, applied: Bool) -> Void;
  public static native func SceneClass() -> Int32;
  public static native func SetPrivate(on: Bool) -> Void;
  public static native func PausePose(player: Uint32, paused: Bool) -> Void;
  public static native func SceneReplay() -> Int32;
  public static native func SceneActor(npc: Uint32, x: Float, y: Float, z: Float, flags: Int32) -> Void;
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
