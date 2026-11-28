// Activities' natives (plugin/src/activity_natives.cpp). Keep the two in
// sync: tools/check-natives.py checks them with Natives.reds.

public native struct TandemBusyState {
  native let player: Uint32;
  // 0 nothing, 1 a workspot, 2 a bed, 3 a screen, 4 a minigame, 5 a shop,
  // 6 a menu, 7 photo mode, 8 a contest, 9 a remote view (entry: its sort,
  // RemoteView.reds; device: the object)
  native let kind: Int32;
  // 1 anchored: hold the body at x, y, z, yaw; 2 lying; 4 its game stands still
  native let flags: Int32;
  // the device used (its EntityID hash), the workspot resource's path hash,
  // its tree entry, the animation playing
  native let device: Uint64;
  native let workspot: Uint64;
  native let entry: Int32;
  native let anim: CName;
  native let x: Float;
  native let y: Float;
  native let z: Float;
  native let yaw: Float;
  // a remote view: where it is, the shared vehicle it drives (0: none)
  native let viewX: Float;
  native let viewY: Float;
  native let viewZ: Float;
  native let traffic: Uint32;
}

// The workspot an actor plays in, read from the game's workspot system
// (Codeware's GetWorkspotInstance finds none on 2.50).
public native struct TandemWorkspot {
  // 0 none, 1 found, 2 an instance with no resource, 3 not laid out as measured
  native let state: Int32;
  // the workspot resource's path hash, its tree entry, the animations
  native let path: Uint64;
  native let entry: Int32;
  native let anim: CName;
  native let idle: CName;
}

public native struct TandemSkipEvent {
  // 1 asked of us, 2 a vote on ours, 3 ended
  native let kind: Int32;
  native let asker: Uint32;
  native let request: Uint32;
  // 0 the wheel, 1 a bed, 2 the host's story
  native let reason: Int32;
  // ended: 0 applied, 1 declined, 2 lapsed, 3 withdrawn, 4 refused
  native let outcome: Int32;
  native let by: Uint32;
  // 0 no reason, 1 another one is open, 2 cannot skip now, 3 a V is down,
  // 4 a time ability, 5 another world, 6 a pause menu, 7 not the host
  native let why: Int32;
  native let accepted: Bool;
  native let fromSeconds: Int32;
  native let toSeconds: Int32;
}

// A street race's note (Races.reds): 1 heard from the session (a head,
// part 0, then part 1 per checkpoint point, part 2 per standings entry),
// 2 our race HUD's mode (arg: vehicleRaceUI; checkpoint: its maxPosition,
// checkpoints: its maxCheckpoints), 3 a reward our V got (record). `ago`:
// seconds before now on the host clock (-1: none).
public native struct TandemRaceEvent {
  native let kind: Int32;
  native let part: Int32;
  native let player: Uint32;
  native let starter: Uint32;
  native let to: Uint32;
  native let race: Uint32;
  native let raceKind: Int32;
  native let arg: Int32;
  native let checkpoint: Int32;
  native let checkpoints: Int32;
  native let metres: Float;
  native let heading: Float;
  native let ago: Float;
  native let record: TweakDBID;
  native let questHash: Uint32;
  native let x: Float;
  native let y: Float;
  native let z: Float;
  native let who: Uint32;
  native let place: Int32;
  native let flags: Int32;
}

public native class TandemActivity extends IScriptable {
  public static native func SetOurs(kind: Int32, flags: Int32, device: Uint64, workspot: Uint64, entry: Int32, anim: CName, position: Vector4, orientation: Quaternion) -> Void;
  // Our V's remote view (kind 9): its sort (RemoteView.reds), the object's
  // EntityID hash, where the view is, the shared vehicle it drives (0: none).
  public static native func SetOursRemote(sort: Int32, device: Uint64, view: Vector4, traffic: Uint32) -> Void;
  public static native func Others() -> array<TandemBusyState>;
  public static native func Changes() -> Uint32;
  public static native func AskSkip(reason: Int32, fromSeconds: Int32, toSeconds: Int32) -> Void;
  public static native func AnswerSkip(asker: Uint32, request: Uint32, accepted: Bool, why: Int32) -> Void;
  public static native func WithdrawSkip() -> Void;
  public static native func TakeSkips() -> array<TandemSkipEvent>;
  // Street races: our word (proto::Race), what was said and seen.
  // (the lists first, staged for the next SendRace)
  public static native func StageRacePoints(points: array<Vector4>) -> Void;
  public static native func StageRaceStandings(who: array<Uint32>, passed: array<Int32>, places: array<Int32>, flags: array<Int32>, agos: array<Float>) -> Void;
  public static native func SendRace(kind: Int32, starter: Uint32, to: Uint32, race: Uint32, arg: Int32, checkpoint: Int32, checkpoints: Int32, metres: Float, heading: Float, ago: Float, record: TweakDBID, questHash: Uint32, at: Vector4) -> Void;
  public static native func TakeRaces() -> array<TandemRaceEvent>;
  public static native func QueueRaceUi(mode: Int32, maxPosition: Int32, maxCheckpoints: Int32) -> Void;
  public static native func QueueRaceReward(reward: TweakDBID) -> Void;
  // The scene our game plays now (its resource path hash; 0: none), and
  // whether it is one of V's homes' interactions (the shower, the bed...).
  public static native func OurScene() -> Uint64;
  public static native func Workspot(actor: EntityID) -> TandemWorkspot;
  public static native func ApartmentScene(scene: Uint64) -> Bool;
}
