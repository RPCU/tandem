// Devices and world objects in co-op (docs/architecture/devices.md;
// engine facts: docs/research/engine/devices.md).
//
// A door one player opens is open for everyone, a camera one player turns
// is turned for everyone, a barrel that blew up is gone in every game. The
// host's game owns each device: the session keeps a record of what every
// device anyone touched or asked about is (core tandem/devices.hpp), and
// every game brings its own copy of the device to it, the game's own way
// (the actions quests use, so it opens with its animation and sound).
//
// - A device is named by its entity's id (placed in the world: the same in
//   every game) and the class of its persistent state. Its state lives in
//   that persistent state, which exists whether the device is streamed in
//   or not: all of this works on devices nobody stands next to.
// - What a device is: its status (on, off, unpowered, disabled), a set of
//   bits (Bit* below) and one number (a lift's floor, a radio's station, a
//   security system's state).
// - Host: we look at the devices around our V a few dozen a frame, and at
//   those whose state just handled an action; what changed is reported and
//   the session tells everyone. A guest's question about a device (it came
//   into its world) is answered from ours.
// - Guest: our V operates a device and it changes here at once (no wait on
//   the network); we see the change the same way and tell the host, which
//   brings its own copy to that state and answers for everyone. When
//   someone else was first, or the host's game refuses, the host's word
//   comes back and our copy follows it. What the host says of any device is
//   applied here, and what our own save had is remembered for every device
//   the session changed (our own V's doing too): when the session ends, our
//   world is put back as it was (as the journal is, Story.reds).
// - Devices that sense and shoot (cameras, turrets, mines, lasers) run in
//   every game against that game's own V: a turret's hits on a V are its
//   own game's (the host forwards none of a device's, Combat.reds), its
//   state (on, off, friendly, destroyed) and the alarm are the host's.
// - An explosion is played in every game (ForceDetonate): it hurts each
//   game's own V there, and NPCs in the host's game only (a guest's blast
//   takes nothing from its proxies, and no game forwards a device's hit on
//   a puppet: Combat.reds).
//
// tools/game.sh cmd device | device near [KIND] | device goto [KIND [M]] |
// device tell [KIND] (the session hears what it is, as if our V just made
// it so: tandem-peer --device-answer then knows it) |
// device open|close|lock|unlock|on|off|explode|distract|friendly|hostile
// [KIND] | device floor N|other | device station N
// KIND: door autodoor (opens itself) lift camera turret sentry (not the
// police's) barrel (whole) mine (a placed trap, whole) ap radio any. near and
// goto look 200 m around our V (goto puts it beside the device, or M m from
// it on our side), the rest 40 m.

public class TandemDevice {
  public let entity: EntityID;
  public let cls: CName;
  // Its entity is in our world; when it came (we look a moment later).
  public let attached: Bool;
  public let since: Float;
  // What its persistent state was at our last look.
  public let seen: Bool;
  public let status: Int32;
  public let flags: Int32;
  public let value: Int32;
  // The session's word on it (guest), or what a guest made a device our
  // game has no state for yet (host): applied when it differs.
  public let known: Bool;
  public let wantStatus: Int32;
  public let wantFlags: Int32;
  public let wantValue: Int32;
  // Guest: what our own save had (at our first look), and whether the
  // session changed the device since: put back when it ends.
  public let kept: Bool;
  public let ownStatus: Int32;
  public let ownFlags: Int32;
  public let ownValue: Int32;
  // We are changing it ourselves until then: what it does meanwhile is not
  // our V's doing (guest), and is not reported half-way (host).
  public let settleUntil: Float;
  // Host: the player whose request we applied; the answer is due then.
  public let by: Uint32;
  public let answerAt: Float;
  // Guest: it would not become what the host says: tried again, a few times.
  public let tries: Int32;
  public let nextTry: Float;
  // Its state handled an action: look now.
  public let dirty: Bool;
  // Guest: named by the host before a resync and not again since.
  public let stale: Bool;
  // Guest: our V's change of it waits for the host's verdict on a quest
  // interaction (Interact.reds) until then: not said meanwhile.
  public let heldUntil: Float;
  public let heldChanged: Bool;
  // The player whose doing its state is (0: nobody's we know): a partner's
  // hack of an enemy's device is marked while it lasts (UpdateMarks).
  public let hackedBy: Uint32;
  public let mark: NewMappinID;
  public let markBy: Uint32;
  // A camera or turret we made friendly to every V here (KeepFriendly):
  // undone when the session's word clears it.
  public let befriended: Bool;
}

public class TandemDevices extends ScriptableSystem {
  private let m_devices: array<ref<TandemDevice>>;
  // The same devices by their entity's (32-bit) hash: a drive through the
  // city attaches hundreds a minute, each found without walking the list.
  private let m_index: ref<inkHashMap>;
  private let m_collisions: Int32;
  private let m_cursor: Int32;
  // In a session, as whom (0: none), and as its host.
  private let m_you: Uint32;
  private let m_host: Bool;
  private let m_dirty: Bool;
  // Guest: when the host began to send its record again (0: not).
  private let m_resetAt: Float;
  private let m_logged: Int32;
  private let m_loggedSince: Float;
  // When the partners' hacks were last marked (UpdateMarks).
  private let m_marksAt: Float;

  public static func Get(game: GameInstance) -> ref<TandemDevices> {
    return GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemDevices") as TandemDevices;
  }

  // ---- what a device is ----

  public static func BitOpen() -> Int32 { return 1; }
  public static func BitLocked() -> Int32 { return 2; }
  public static func BitSealed() -> Int32 { return 4; }
  public static func BitBroken() -> Int32 { return 8; }
  public static func BitExploded() -> Int32 { return 16; }
  public static func BitHacked() -> Int32 { return 32; }
  public static func BitFriendly() -> Int32 { return 64; }
  public static func BitExposed() -> Int32 { return 128; }
  public static func BitBreached() -> Int32 { return 256; }
  // 512 (a lift on its way) is not sent: its value says where it goes.
  public static func BitGlitching() -> Int32 { return 1024; }

  private static func Has(flags: Int32, bit: Int32) -> Bool {
    return (flags & bit) != 0;
  }

  private static func Differ(a: Int32, b: Int32, bit: Int32) -> Bool {
    return (a & bit) != (b & bit);
  }

  private static func With(flags: Int32, bit: Int32) -> Int32 {
    return TandemDevices.Has(flags, bit) ? flags : flags + bit;
  }

  private static func Without(flags: Int32, bit: Int32) -> Int32 {
    return TandemDevices.Has(flags, bit) ? flags - bit : flags;
  }

  // A device we follow: placed in the world (the same id in every game),
  // its state the device kind's (a vehicle's is the car sync's).
  private static func Follows(ps: ref<ScriptableDeviceComponentPS>) -> Bool {
    if !IsDefined(ps) || IsDefined(ps as VehicleComponentPS) {
      return false;
    }
    let id = ps.GetID();
    if !PersistentID.IsDefined(id) || PersistentID.IsDynamic(id) {
      return false;
    }
    return Equals(PersistentID.GetComponentName(id), n"controller");
  }

  private func StateOf(device: ref<TandemDevice>) -> ref<ScriptableDeviceComponentPS> {
    let id = CreatePersistentID(device.entity, n"controller");
    return GameInstance.GetPersistencySystem(this.GetGameInstance()).GetConstAccessToPSObject(id, device.cls) as ScriptableDeviceComponentPS;
  }

  private static func StatusOf(ps: ref<ScriptableDeviceComponentPS>) -> Int32 {
    let status = EnumInt(ps.GetDeviceState());
    if status < -2 {
      return -2;
    }
    // INVALID (2): a device that is nothing in particular counts as on.
    return status > 1 ? 1 : status;
  }

  private static func FlagsOf(ps: ref<ScriptableDeviceComponentPS>) -> Int32 {
    let flags = 0;
    if EnumInt(ps.GetDurabilityState()) >= 2 {
      flags += TandemDevices.BitBroken();
    }
    if ps.WasQuickHacked() {
      flags += TandemDevices.BitHacked();
    }
    // The device's own exposure (a breach), not the world's default: with
    // the ping preset's QuickHacksExposedByDefault every device reads
    // exposed and no revoke clears it (final.reds (2.50):28234, :132306),
    // so a host's word without the bit never matched (devices-barrel-record:
    // "will not become what the host has", 2026-11-28).
    if ps.IsQuickHacksExposed() && !NetworkSystem.QuickHacksExposedByDefault(ps.GetGameInstance()) {
      flags += TandemDevices.BitExposed();
    }
    if ps.IsGlitching() {
      flags += TandemDevices.BitGlitching();
    }
    let door = ps as DoorControllerPS;
    if IsDefined(door) {
      if door.IsOpen() {
        flags += TandemDevices.BitOpen();
      }
      if door.IsLocked() {
        flags += TandemDevices.BitLocked();
      }
      if door.IsSealed() {
        flags += TandemDevices.BitSealed();
      }
    }
    let explosive = ps as ExplosiveDeviceControllerPS;
    if IsDefined(explosive) && explosive.IsExploded() {
      flags += TandemDevices.BitExploded();
    }
    // A camera or turret turned (Friendly Mode, a breach's daemon, a quest),
    // or in a player's hands (the takeover): friendly to every V in every
    // other game while it lasts.
    let sensor = ps as SensorDeviceControllerPS;
    if IsDefined(sensor) && (sensor.IsAttitudeChanged() || sensor.IsControlledByPlayer()) {
      flags += TandemDevices.BitFriendly();
    }
    let point = ps as AccessPointControllerPS;
    if IsDefined(point) && point.IsNetworkBreached() {
      flags += TandemDevices.BitBreached();
    }
    return flags;
  }

  private static func ValueOf(ps: ref<ScriptableDeviceComponentPS>) -> Int32 {
    let lift = ps as LiftControllerPS;
    if IsDefined(lift) {
      // On its way: where it goes, so every game's lift leaves together.
      let target = lift.GetTargetFloor();
      return lift.IsMoving() && target >= 0 ? target : lift.GetActiveFloor();
    }
    let media = ps as MediaDeviceControllerPS;
    if IsDefined(media) {
      return media.GetActiveStationIndex();
    }
    let security = ps as SecuritySystemControllerPS;
    if IsDefined(security) {
      return EnumInt(security.GetSecurityState());
    }
    return -1;
  }

  // ---- the devices we follow ----

  private static func KeyOf(entity: EntityID) -> Uint64 {
    return Cast<Uint64>(EntityID.GetHash(entity));
  }

  private func Find(entity: EntityID, cls: CName) -> ref<TandemDevice> {
    if !IsDefined(this.m_index) {
      return null;
    }
    let first = this.m_index.Get(TandemDevices.KeyOf(entity)) as TandemDevice;
    if !IsDefined(first) {
      return null;
    }
    if first.entity == entity && Equals(first.cls, cls) {
      return first;
    }
    // Two entities with one short hash (or two states of one entity): the
    // second is only in the list.
    for device in this.m_devices {
      if device.entity == entity && Equals(device.cls, cls) {
        return device;
      }
    }
    return null;
  }

  private func Track(entity: EntityID, cls: CName) -> ref<TandemDevice> {
    let device = this.Find(entity, cls);
    if !IsDefined(device) {
      device = new TandemDevice();
      device.entity = entity;
      device.cls = cls;
      ArrayPush(this.m_devices, device);
      if !IsDefined(this.m_index) {
        this.m_index = new inkHashMap();
      }
      let key = TandemDevices.KeyOf(entity);
      if !this.m_index.KeyExist(key) {
        this.m_index.Insert(key, device);
      } else {
        this.m_collisions += 1;
      }
    }
    return device;
  }

  // Out of the list (the caller erases it there) and out of the index.
  private func Unindex(device: ref<TandemDevice>) -> Void {
    if !IsDefined(this.m_index) {
      return;
    }
    let key = TandemDevices.KeyOf(device.entity);
    if this.m_index.Get(key) == device {
      this.m_index.Remove(key);
      if this.m_collisions == 0 {
        return;
      }
      // Another one behind the same key takes its place.
      for other in this.m_devices {
        if other != device && TandemDevices.KeyOf(other.entity) == key {
          this.m_index.Insert(key, other);
          return;
        }
      }
    }
  }

  private func Now() -> Float {
    return EngineTime.ToFloat(GameInstance.GetEngineTime(this.GetGameInstance()));
  }

  // Every tick, session or not (TandemSystem.OnTick): the devices that
  // came, went or were touched since, as their wraps queued them (below).
  public func Drain() -> Void {
    let game = this.GetGameInstance();
    let streamed = TandemStreaming.TakeStreamed(TandemStreamKind.Device());
    for event in streamed {
      if event.attached {
        // Gone again since: its detach is further down the queue.
        this.Attached(GameInstance.FindEntityByID(game, event.entity) as Device);
      } else {
        this.Detached(event.entity);
      }
    }
    let touched = TandemStreaming.TakeStreamed(TandemStreamKind.DeviceTouched());
    for event in touched {
      this.Touched(event.entity, event.cls);
    }
  }

  // A device came into our world (Device.OnGameAttached), session or not.
  private func Attached(owner: ref<Device>) -> Void {
    if !IsDefined(owner) {
      return;
    }
    let ps = owner.GetDevicePS();
    if !TandemDevices.Follows(ps) {
      return;
    }
    let device = this.Track(owner.GetEntityID(), ps.GetClassName());
    // Streamed in again (a device the session keeps while it is away): its
    // state is our world's own again, read anew by its first look, not a
    // change of ours. Seen from before, a guest's doors that came back
    // locked, then unlocked, were each asked of the host as its V's doing,
    // and the host's doors followed (pair-devices-door, 2026-11-28).
    if !device.attached {
      device.seen = false;
    }
    device.attached = true;
    device.since = this.Now();
  }

  // ...and went (Device.OnDetach): every state we follow of that entity (its
  // own may be gone by now). Forgotten unless the session says something of
  // it, or it is ours to put back.
  private func Detached(entity: EntityID) -> Void {
    if !IsDefined(this.m_index) {
      return;
    }
    let first = this.m_index.Get(TandemDevices.KeyOf(entity)) as TandemDevice;
    if !IsDefined(first) {
      return;
    }
    if this.m_collisions == 0 {
      if first.entity == entity {
        first.attached = false;
        this.Unmark(first);
        if this.Idle(first) {
          this.Drop(first);
        }
      }
      return;
    }
    // Two entities with one short hash (or two states of one entity): the
    // others are only in the list.
    let gone: array<ref<TandemDevice>>;
    for device in this.m_devices {
      if device.entity == entity {
        ArrayPush(gone, device);
      }
    }
    for device in gone {
      device.attached = false;
      this.Unmark(device);
      if this.Idle(device) {
        this.Drop(device);
      }
    }
  }

  // A device's state handled an action (ScriptableDeviceComponentPS.Notify),
  // from any thread: queued with its entity and class if we follow it.
  public static func QueueTouched(ps: ref<ScriptableDeviceComponentPS>) -> Void {
    if TandemDevices.Follows(ps) {
      TandemStreaming.QueueStreamed(TandemStreamKind.DeviceTouched(), PersistentID.ExtractEntityID(ps.GetID()), ps.GetClassName(), true);
    }
  }

  // ...taken on the next tick: looked at then.
  private func Touched(entity: EntityID, cls: CName) -> Void {
    if this.m_you == 0u {
      return;
    }
    let device = this.Track(entity, cls);
    device.dirty = true;
    this.m_dirty = true;
  }

  // ---- quest interactions (Interact.reds) ----

  // Guest: our V is about to operate this device and the host's game is
  // asked whether the use is its own: what our copy becomes meanwhile is not
  // said (the host's own doing of it will answer for everyone).
  public func Hold(entity: EntityID, cls: CName, seconds: Float) -> Void {
    if this.m_you == 0u || this.m_host {
      return;
    }
    let device = this.Track(entity, cls);
    device.heldUntil = this.Now() + seconds;
    device.heldChanged = false;
  }

  // Guest: the verdict came. report: the use was ours after all (nothing of
  // the host's quest, someone else's came first, the host's game has no
  // such thing): what our copy became is asked of the host the usual way.
  // Else (the host's V did it, or it was not ours to do): the host is asked
  // what the device is, and our copy follows its word when they differ.
  public func Release(entity: EntityID, cls: CName, report: Bool) -> Void {
    let device = this.Find(entity, cls);
    if !IsDefined(device) {
      return;
    }
    device.heldUntil = 0.0;
    let changed = device.heldChanged;
    device.heldChanged = false;
    let ps = this.StateOf(device);
    if !changed || this.m_you == 0u || !IsDefined(ps) {
      return;
    }
    if report {
      TandemNative.UseDevice(device.entity, device.cls, TandemDevices.StatusOf(ps), TandemDevices.FlagsOf(ps), TandemDevices.ValueOf(ps));
      this.LogSome(s"devices: \(TandemDevices.Name(device)) changed here (a quest interaction that was ours): asked of the host");
    } else {
      TandemNative.AskDevice(device.entity, device.cls, TandemDevices.StatusOf(ps), TandemDevices.FlagsOf(ps), TandemDevices.ValueOf(ps));
    }
  }

  // Host: our V is doing guest `player`'s use of this device: what it
  // becomes is said in that guest's name, a moment later.
  public func PerformedFor(entity: EntityID, cls: CName, player: Uint32) -> Void {
    if this.m_you == 0u || !this.m_host {
      return;
    }
    let device = this.Track(entity, cls);
    let ps = this.StateOf(device);
    if IsDefined(ps) && !device.seen {
      device.seen = true;
      device.status = TandemDevices.StatusOf(ps);
      device.flags = TandemDevices.FlagsOf(ps);
      device.value = TandemDevices.ValueOf(ps);
    }
    device.by = player;
    device.answerAt = this.Now() + 0.25;
    device.settleUntil = device.answerAt;
  }

  // Not in our world and nothing of the session's: no need to remember it.
  private func Idle(device: ref<TandemDevice>) -> Bool {
    return !device.attached && !device.known && !device.kept && !device.stale && device.answerAt == 0.0 && !device.befriended;
  }

  // ---- the session ----

  public func Begin() -> Void {
    this.m_you = 0u;
    this.m_resetAt = 0.0;
    // Looked at afresh, as this session finds them.
    for device in this.m_devices {
      this.Forget(device);
    }
  }

  private func Forget(device: ref<TandemDevice>) -> Void {
    device.seen = false;
    device.known = false;
    device.kept = false;
    device.stale = false;
    device.dirty = false;
    device.by = 0u;
    device.answerAt = 0.0;
    device.settleUntil = 0.0;
    device.tries = 0;
    device.nextTry = 0.0;
    device.hackedBy = 0u;
    device.befriended = false;
    this.Unmark(device);
  }

  // The session is over. worldStays (we stay in this game) and we were a
  // guest: every device the host's word changed goes back to what our own
  // save had, quietly, loaded or not.
  public func End(worldStays: Bool) -> Void {
    let back = 0;
    if worldStays && this.m_you != 0u && !this.m_host {
      for device in this.m_devices {
        if device.kept && this.PutBack(device) {
          back += 1;
        }
      }
    }
    if this.m_you != 0u {
      TandemNative.Log(s"devices: session over, \(ArraySize(this.m_devices)) followed, \(back) put back as our own save had them");
    }
    this.m_you = 0u;
    this.m_host = false;
    this.m_resetAt = 0.0;
    let i = ArraySize(this.m_devices) - 1;
    while i >= 0 {
      let device = this.m_devices[i];
      this.Forget(device);
      if !device.attached {
        this.Unindex(device);
        ArrayErase(this.m_devices, i);
      }
      i -= 1;
    }
  }

  private func PutBack(device: ref<TandemDevice>) -> Bool {
    let ps = this.StateOf(device);
    if !IsDefined(ps) {
      return false;
    }
    if TandemDevices.StatusOf(ps) == device.ownStatus && TandemDevices.FlagsOf(ps) == device.ownFlags && TandemDevices.ValueOf(ps) == device.ownValue {
      return false;
    }
    this.Apply(device, ps, device.ownStatus, device.ownFlags, device.ownValue, true, 0u);
    return true;
  }

  // Every frame of a session (TandemSystem.OnTick). you: our player id,
  // 0 while the world loads.
  public func Tick(now: Float, you: Uint32) -> Void {
    if you == 0u {
      return;
    }
    if this.m_you != you {
      this.m_you = you;
      this.m_host = TandemNative.IsHost();
      let role = this.m_host ? "host" : "guest";
      TandemNative.Log(s"devices: following \(ArraySize(this.m_devices)) around us (\(role))");
    }
    this.HandleEvents(now);
    // What an action just touched, then a share of the ones around us.
    if this.m_dirty {
      this.m_dirty = false;
      let d = ArraySize(this.m_devices) - 1;
      while d >= 0 {
        let touched = this.m_devices[d];
        if touched.dirty {
          touched.dirty = false;
          this.Look(touched, now, true);
          if this.Idle(touched) {
            this.Unindex(touched);
            ArrayErase(this.m_devices, d);
          }
        }
        d -= 1;
      }
    }
    let count = ArraySize(this.m_devices);
    let budget = Min(count, 32);
    if this.m_cursor >= count {
      this.m_cursor = 0;
    }
    while budget > 0 {
      budget -= 1;
      this.m_cursor += 1;
      if this.m_cursor >= count {
        this.m_cursor = 0;
      }
      let device = this.m_devices[this.m_cursor];
      if device.attached || device.answerAt > 0.0 {
        this.Look(device, now, false);
      }
    }
    if now - this.m_marksAt >= 0.5 || now < this.m_marksAt {
      this.m_marksAt = now;
      this.UpdateMarks();
    }
    // Guest: the host's record came again; what it no longer names goes
    // back to what our own save had.
    if this.m_resetAt > 0.0 && now - this.m_resetAt > 3.0 {
      this.m_resetAt = 0.0;
      for device in this.m_devices {
        if device.stale {
          device.stale = false;
          if device.kept && this.PutBack(device) {
            TandemNative.Log(s"devices: \(TandemDevices.Name(device)) is no longer the session's: back as our own save had it");
          }
          device.kept = false;
        }
      }
    }
  }

  private static func Name(device: ref<TandemDevice>) -> String {
    return s"\(TandemNative.EntityHex(device.entity)) \(NameToString(device.cls))";
  }

  // "on, open+hacked, value -1": what the logs (and the scenarios) read.
  private static func Describe(status: Int32, flags: Int32, value: Int32) -> String {
    let power = "on";
    if status == 0 {
      power = "off";
    } else if status == -1 {
      power = "unpowered";
    } else if status == -2 {
      power = "disabled";
    }
    let bits = "";
    bits = TandemDevices.Word(bits, flags, TandemDevices.BitOpen(), "open");
    bits = TandemDevices.Word(bits, flags, TandemDevices.BitLocked(), "locked");
    bits = TandemDevices.Word(bits, flags, TandemDevices.BitSealed(), "sealed");
    bits = TandemDevices.Word(bits, flags, TandemDevices.BitBroken(), "broken");
    bits = TandemDevices.Word(bits, flags, TandemDevices.BitExploded(), "exploded");
    bits = TandemDevices.Word(bits, flags, TandemDevices.BitHacked(), "hacked");
    bits = TandemDevices.Word(bits, flags, TandemDevices.BitFriendly(), "friendly");
    bits = TandemDevices.Word(bits, flags, TandemDevices.BitExposed(), "exposed");
    bits = TandemDevices.Word(bits, flags, TandemDevices.BitBreached(), "breached");
    bits = TandemDevices.Word(bits, flags, TandemDevices.BitGlitching(), "glitching");
    if Equals(bits, "") {
      bits = "plain";
    }
    return s"\(power), \(bits), value \(value)";
  }

  private static func Word(bits: String, flags: Int32, bit: Int32, word: String) -> String {
    if !TandemDevices.Has(flags, bit) {
      return bits;
    }
    return Equals(bits, "") ? word : s"\(bits)+\(word)";
  }

  // One look at a device's state: what changed since the last one, whose
  // doing it is, and what to say of it. touched: its state just handled an
  // action (else: its turn among the devices around us).
  private func Look(device: ref<TandemDevice>, now: Float, touched: Bool) -> Void {
    // Just streamed in: its state settles first.
    if device.attached && !device.seen && now - device.since < 0.5 {
      return;
    }
    let ps = this.StateOf(device);
    if !IsDefined(ps) {
      return;
    }
    this.KeepFriendly(device, ps, false);
    let status = TandemDevices.StatusOf(ps);
    let flags = TandemDevices.FlagsOf(ps);
    let value = TandemDevices.ValueOf(ps);
    if !device.seen {
      device.seen = true;
      device.status = status;
      device.flags = flags;
      device.value = value;
      if this.m_host {
        if device.known {
          // A guest made it something before our game had it: now it does.
          if !this.IsWanted(device, status, flags, value) {
            this.Apply(device, ps, device.wantStatus, device.wantFlags, device.wantValue, true, 0u);
            device.settleUntil = now + 1.0;
          }
          device.known = false;
        } else if touched {
          // An action on a device we never looked at (a quest's, far from
          // us): what it was before is unknown, so the session hears what
          // it is now, changed or not.
          TandemNative.ReportDevice(device.entity, device.cls, status, flags, value, this.m_you);
        }
        return;
      }
      device.ownStatus = status;
      device.ownFlags = flags;
      device.ownValue = value;
      if !device.known {
        // What is it in the session? (Answered only when it differs.)
        TandemNative.AskDevice(device.entity, device.cls, status, flags, value);
      } else if !this.IsWanted(device, status, flags, value) {
        this.Follow(device, ps, now, true);
      }
      return;
    }
    // Host: the answer to a guest's request is due (changed or not).
    if this.m_host && device.answerAt > 0.0 {
      if now < device.answerAt {
        return;
      }
      device.answerAt = 0.0;
      device.status = status;
      device.flags = flags;
      device.value = value;
      TandemNative.ReportDevice(device.entity, device.cls, status, flags, value, device.by);
      TandemNative.Log(s"devices: \(TandemDevices.Name(device)) after player \(device.by)'s request: \(TandemDevices.Describe(status, flags, value))");
      device.by = 0u;
      return;
    }
    let changed = status != device.status || flags != device.flags || value != device.value;
    if !changed {
      // Guest: still not what the host says (the game refused, an action
      // was lost): again, a few times, then it stays as it is.
      if !this.m_host && device.known && now >= device.settleUntil && now >= device.nextTry && device.tries < 3 && !this.IsWanted(device, status, flags, value) {
        device.tries += 1;
        device.nextTry = now + 2.0;
        if device.tries == 3 {
          TandemNative.Log(s"devices: \(TandemDevices.Name(device)) will not become what the host has (\(TandemDevices.Describe(device.wantStatus, device.wantFlags, device.wantValue))): ours stays \(TandemDevices.Describe(status, flags, value))");
          TandemDegraded.Fallback("device not matched", s"\(TandemDevices.Name(device)) will not become what the host has: ours stays as it is");
        }
        this.Follow(device, ps, now, true);
      }
      return;
    }
    device.status = status;
    device.flags = flags;
    device.value = value;
    if this.m_host {
      if now < device.settleUntil {
        return;
      }
      device.hackedBy = this.m_you;
      TandemNative.ReportDevice(device.entity, device.cls, status, flags, value, this.m_you);
      this.LogSome(s"devices: \(TandemDevices.Name(device)) is now \(TandemDevices.Describe(status, flags, value))");
      return;
    }
    if device.known && this.IsWanted(device, status, flags, value) {
      device.tries = 0;
      return; // it became what the host says
    }
    if now < device.settleUntil {
      return; // on its way there
    }
    // An alarm is raised by whoever is seen and called off by the host
    // only: ours going quiet on its own is put back by the next look.
    if device.known && value < device.wantValue && IsDefined(ps as SecuritySystemControllerPS) {
      device.tries = 0;
      return;
    }
    device.kept = true;
    // A quest interaction's: the host's game does it on its own copy, or
    // says it is ours (Release, below).
    if now < device.heldUntil {
      device.heldChanged = true;
      return;
    }
    // Our V's doing (or our game's own: a door closing itself): the host
    // hears what it is here now, and answers for everyone.
    device.hackedBy = this.m_you;
    TandemNative.UseDevice(device.entity, device.cls, status, flags, value);
    this.LogSome(s"devices: \(TandemDevices.Name(device)) changed here (\(TandemDevices.Describe(status, flags, value))): asked of the host");
  }

  // A line per change is too many in a firefight: a hundred a minute (a
  // hundred for the game's whole run left every later change unsaid, and a
  // partner game's scenarios waiting for theirs, 2026-11-28).
  private func LogSome(text: String) -> Void {
    let now = this.Now();
    if now - this.m_loggedSince >= 60.0 || now < this.m_loggedSince {
      this.m_loggedSince = now;
      this.m_logged = 0;
    }
    if this.m_logged < 100 {
      this.m_logged += 1;
      TandemNative.Log(text);
    }
  }

  private func IsWanted(device: ref<TandemDevice>, status: Int32, flags: Int32, value: Int32) -> Bool {
    return status == device.wantStatus && flags == device.wantFlags && value == device.wantValue;
  }

  // Guest: our copy to what the host says (and back to our own when the
  // session ends).
  private func Follow(device: ref<TandemDevice>, ps: ref<ScriptableDeviceComponentPS>, now: Float, quiet: Bool) -> Void {
    device.kept = true;
    this.Apply(device, ps, device.wantStatus, device.wantFlags, device.wantValue, quiet, 0u);
    device.settleUntil = now + 1.0;
  }

  private func HandleEvents(now: Float) -> Void {
    for event in TandemNative.TakeDeviceEvents() {
      if event.kind == 4 {
        // The host's record, again: whatever it names next is the session's.
        for known in this.m_devices {
          if known.known {
            known.known = false;
            known.stale = true;
          }
        }
        this.m_resetAt = now;
      } else if event.kind == 1 && !this.m_host {
        this.OnHostWord(event, now);
      } else if event.kind == 2 && this.m_host {
        this.OnAsked(event);
      } else if event.kind == 3 && this.m_host {
        this.OnRequested(event, now);
      }
    }
  }

  // Guest: the host's word on a device, loaded here or not.
  private func OnHostWord(event: TandemDeviceEvent, now: Float) -> Void {
    let device = this.Track(event.entity, event.cls);
    // A partner's quickhack on it (the bits it turned on since the host's
    // last word, or our last look): its indicator while we are linked
    // (Comms.reds). A record (a join, a resync) is no news.
    if !event.quiet {
      TandemCommsNative.PartnerDevice(event.entity, event.player, device.known ? device.wantFlags : device.flags, event.flags);
    }
    device.known = true;
    device.stale = false;
    device.wantStatus = event.status;
    device.wantFlags = event.flags;
    device.wantValue = event.value;
    device.tries = 0;
    device.hackedBy = event.player;
    let ps = this.StateOf(device);
    if !IsDefined(ps) {
      return; // no state for it here yet: applied when it comes (Look)
    }
    let status = TandemDevices.StatusOf(ps);
    let flags = TandemDevices.FlagsOf(ps);
    let value = TandemDevices.ValueOf(ps);
    if !device.seen {
      device.seen = true;
      device.ownStatus = status;
      device.ownFlags = flags;
      device.ownValue = value;
    }
    device.status = status;
    device.flags = flags;
    device.value = value;
    if this.IsWanted(device, status, flags, value) {
      return;
    }
    let how = event.quiet ? "set" : "played";
    this.LogSome(s"devices: \(TandemDevices.Name(device)) by player \(event.player): \(TandemDevices.Describe(event.status, event.flags, event.value)), \(how) (ours: \(TandemDevices.Describe(status, flags, value)))");
    this.Follow(device, ps, now, event.quiet);
  }

  // Host: a guest asks what a device is. Ours says, if we have it.
  private func OnAsked(event: TandemDeviceEvent) -> Void {
    let device = this.Track(event.entity, event.cls);
    let ps = this.StateOf(device);
    if !IsDefined(ps) {
      if !device.attached {
        this.Drop(device);
      }
      return;
    }
    device.seen = true;
    device.status = TandemDevices.StatusOf(ps);
    device.flags = TandemDevices.FlagsOf(ps);
    device.value = TandemDevices.ValueOf(ps);
    TandemNative.ReportDevice(device.entity, device.cls, device.status, device.flags, device.value, 0u);
    if !device.attached {
      this.Drop(device);
    }
  }

  private func Drop(device: ref<TandemDevice>) -> Void {
    this.Unmark(device);
    this.Unindex(device);
    ArrayRemove(this.m_devices, device);
  }

  // Host: a guest operated a device; ours follows, the game's way, and the
  // answer (what it became) goes out a moment later.
  private func OnRequested(event: TandemDeviceEvent, now: Float) -> Void {
    let device = this.Track(event.entity, event.cls);
    let ps = this.StateOf(device);
    if !IsDefined(ps) {
      // Our game has no state for it (never been there): the session takes
      // the guest's word, and ours follows when it comes into our world.
      device.known = true;
      device.wantStatus = event.status;
      device.wantFlags = event.flags;
      device.wantValue = event.value;
      TandemNative.ReportDevice(device.entity, device.cls, event.status, event.flags, event.value, event.player);
      TandemNative.Log(s"devices: \(TandemDevices.Name(device)) by player \(event.player), not in our world yet: taken as said");
      return;
    }
    if !device.seen {
      device.seen = true;
      device.status = TandemDevices.StatusOf(ps);
      device.flags = TandemDevices.FlagsOf(ps);
      device.value = TandemDevices.ValueOf(ps);
    }
    device.by = event.player;
    device.hackedBy = event.player;
    // A guest's quickhack on it: its indicator while we are linked (Comms.reds).
    TandemCommsNative.PartnerDevice(event.entity, event.player, device.flags, event.flags);
    // The game handles the actions on its next frames: answered then. An
    // explosion takes its time to go off (its fuse, its chain).
    let blast = TandemDevices.Has(event.flags, TandemDevices.BitExploded()) && !TandemDevices.Has(device.flags, TandemDevices.BitExploded());
    device.answerAt = now + (blast ? 1.5 : 0.25);
    device.settleUntil = device.answerAt;
    this.Apply(device, ps, event.status, event.flags, event.value, false, event.player);
  }

  // ---- making a device something, the game's way ----

  private func Quest(ps: ref<ScriptableDeviceComponentPS>, action: CName) -> Bool {
    let quest = ps.GetQuestActionByName(action) as ScriptableDeviceAction;
    if !IsDefined(quest) {
      return false;
    }
    GameInstance.GetPersistencySystem(this.GetGameInstance()).QueuePSDeviceEvent(quest);
    return true;
  }

  private func Send(ps: ref<ScriptableDeviceComponentPS>, evt: ref<Event>) -> Void {
    GameInstance.GetPersistencySystem(this.GetGameInstance()).QueuePSEvent(ps.GetID(), ps.GetClassName(), evt);
  }

  // quiet: a record to set (a join, a resync, our own save put back), not
  // a change to play. by: (host) the guest whose doing it is, else 0.
  private func Apply(device: ref<TandemDevice>, ps: ref<ScriptableDeviceComponentPS>, status: Int32, flags: Int32, value: Int32, quiet: Bool, by: Uint32) -> Void {
    let game = this.GetGameInstance();
    let loaded = IsDefined(GameInstance.FindEntityByID(game, device.entity));
    let has = TandemDevices.FlagsOf(ps);
    let was = TandemDevices.StatusOf(ps);
    if was != status {
      if status == -2 {
        this.Quest(ps, n"ForceDisabled");
      } else if status == -1 {
        this.Quest(ps, n"ForceUnpower");
      } else {
        if was == -2 {
          this.Quest(ps, n"ForceEnabled");
        } else if was == -1 {
          this.Quest(ps, n"ForcePower");
        }
        this.Quest(ps, status == 1 ? n"ForceON" : n"ForceOFF");
      }
    }
    if TandemDevices.Differ(flags, has, TandemDevices.BitBroken()) {
      // Broken by a hit somewhere else: no action does that, the state
      // says so (its look follows when it next resolves its state).
      ps.SetDurabilityState(TandemDevices.Has(flags, TandemDevices.BitBroken()) ? EDeviceDurabilityState.BROKEN : EDeviceDurabilityState.NOMINAL);
    }
    let door = ps as DoorControllerPS;
    if IsDefined(door) {
      this.ApplyDoor(door, has, flags);
    }
    let lift = ps as LiftControllerPS;
    if IsDefined(lift) {
      this.ApplyLift(lift, value, loaded && !quiet);
    }
    let explosive = ps as ExplosiveDeviceControllerPS;
    if IsDefined(explosive) && TandemDevices.Differ(flags, has, TandemDevices.BitExploded()) {
      if !TandemDevices.Has(flags, TandemDevices.BitExploded()) {
        // Whole again only as a record (our own save put back): what went
        // up in front of us stays up, whatever anyone says meanwhile.
        if quiet {
          explosive.SetExplodedState(false);
        }
      } else if loaded && !quiet {
        // Played here, as a quest would: the blast, its sound, its damage
        // (to our own V; to NPCs when ours is the host's game). A guest's
        // doing goes off from its puppet: its kills and its crime are that
        // V's (Npcs.reds KillerOf, Police.reds), as when a V sets one off.
        let puppet = by != 0u ? TandemNpcs.PuppetOf(game, by) : null;
        let barrel = GameInstance.FindEntityByID(game, device.entity) as ExplosiveDevice;
        if IsDefined(puppet) && IsDefined(barrel) {
          barrel.TandemDetonate(puppet);
          TandemNative.Log(s"devices: \(TandemDevices.Name(device)) set off by player \(by)'s puppet");
        } else {
          this.Quest(ps, n"ForceDetonate");
        }
      } else {
        explosive.SetExplodedState(true);
        // A record (a join, a resync) for one standing whole in front of
        // us: the wreck the engine gives one that streams in exploded
        // (ExplosiveDevice.ResolveGameplayState), with no blast: a record
        // hurts nobody. Not loaded: that look comes when it streams in.
        let wreck = loaded ? GameInstance.FindEntityByID(game, device.entity) as ExplosiveDevice : null;
        if IsDefined(wreck) && explosive.DoExplosiveResolveGameplayLogic() {
          if wreck.TandemShowExploded() {
            TandemNative.Log(s"devices: \(TandemDevices.Name(device)) set as the host's wreck, in front of us");
          } else {
            TandemDegraded.Fallback("device not matched", s"\(TandemDevices.Name(device)) stays whole here: the host's is exploded");
          }
        }
      }
    }
    // A camera or turret turned: friendly to every V here, the way the
    // Friendly Mode quickhack makes it (SetDeviceAttitude), or back. Not
    // streamed in: its state says so, and its entity is turned when it
    // comes (KeepFriendly). Not while our V holds it (the takeover's own).
    let sensor = ps as SensorDeviceControllerPS;
    if IsDefined(sensor) && !sensor.IsControlledByPlayer() && TandemDevices.Differ(flags, has, TandemDevices.BitFriendly()) {
      if TandemDevices.Has(flags, TandemDevices.BitFriendly()) {
        sensor.SetIsAttitudeChanged(true);
        if loaded {
          this.KeepFriendly(device, ps, true);
        }
      } else {
        sensor.SetIsAttitudeChanged(false);
        if loaded {
          this.Unbefriend(device);
        }
      }
    }
    let point = ps as AccessPointControllerPS;
    if IsDefined(point) && TandemDevices.Differ(flags, has, TandemDevices.BitBreached()) {
      if TandemDevices.Has(flags, TandemDevices.BitBreached()) {
        // Breached once, for everyone (Breach.reds): the network opens up,
        // and the game offers no jack-in into it again; what the breach
        // paid went to who did it, and its daemons run where their
        // targets are owned.
        this.Send(ps, new BreachAccessPointEvent());
        ps.TandemSetMinigameWon(true);
      } else if quiet {
        this.Quest(ps, n"ResetNetworkBreachState"); // our own save: not breached
        ps.TandemSetMinigameWon(false);
      }
    }
    // An access point is open when breached (SetIsBreached): never through
    // SetExposeQuickHacks, whose handler there runs RefreshSlaves, the
    // daemons of whatever our minigame blackboard last had and its rewards.
    if !IsDefined(point) && TandemDevices.Differ(flags, has, TandemDevices.BitExposed()) {
      if TandemDevices.Has(flags, TandemDevices.BitExposed()) {
        let expose = new SetExposeQuickHacks();
        expose.clearanceLevel = 2;
        expose.SetUp(ps);
        expose.SetProperties();
        ps.ExecutePSAction(expose, ps);
      } else if quiet {
        this.Send(ps, new RevokeQuickHackAccess());
      }
    }
    if TandemDevices.Differ(flags, has, TandemDevices.BitHacked()) {
      let hacked = new SetQuickHackEvent();
      hacked.wasQuickHacked = TandemDevices.Has(flags, TandemDevices.BitHacked());
      this.Send(ps, hacked);
    }
    if TandemDevices.Differ(flags, has, TandemDevices.BitGlitching()) {
      if !TandemDevices.Has(flags, TandemDevices.BitGlitching()) {
        this.Quest(ps, n"QuestStopGlitch");
      } else if by != 0u {
        // Host, a guest's distraction: the real one, so our NPCs come and look.
        let distract = new QuickHackDistraction();
        distract.clearanceLevel = 10;
        distract.SetUp(ps);
        distract.SetObjectActionID(t"DeviceAction.MalfunctionClassHack");
        distract.SetProperties();
        distract.SetDurationValue(ps.GetDistractionDuration(distract));
        ps.ExecutePSAction(distract, ps);
      } else {
        this.Quest(ps, n"QuestStartGlitch");
      }
    }
    let media = ps as MediaDeviceControllerPS;
    if IsDefined(media) && value >= 0 && media.GetActiveStationIndex() != value {
      let channel = new QuestSetChannel();
      channel.clearanceLevel = 99;
      channel.SetUp(ps);
      channel.SetProperties(value);
      GameInstance.GetPersistencySystem(game).QueuePSDeviceEvent(channel);
    }
    let security = ps as SecuritySystemControllerPS;
    if IsDefined(security) && value >= 1 && value <= 3 && EnumInt(security.GetSecurityState()) != value {
      this.ApplySecurity(security, value, by);
    }
  }

  // ForceOpen unlocks and unseals, ForceLock and ForceClose unseal,
  // ForceSeal closes: in the order that leaves the door as asked.
  private func ApplyDoor(door: ref<DoorControllerPS>, has: Int32, flags: Int32) -> Void {
    let open = TandemDevices.Has(flags, TandemDevices.BitOpen());
    let locked = TandemDevices.Has(flags, TandemDevices.BitLocked());
    let sealed = TandemDevices.Has(flags, TandemDevices.BitSealed());
    if open {
      if !TandemDevices.Has(has, TandemDevices.BitOpen()) {
        this.Quest(door, n"ForceOpen");
      }
      return;
    }
    if TandemDevices.Has(has, TandemDevices.BitOpen()) {
      this.Quest(door, n"ForceClose");
    }
    if TandemDevices.Differ(flags, has, TandemDevices.BitLocked()) {
      this.Quest(door, locked ? n"ForceLock" : n"ForceUnlock");
    }
    if sealed {
      if !TandemDevices.Has(has, TandemDevices.BitSealed()) {
        this.Quest(door, n"ForceSeal");
      }
    } else if TandemDevices.Has(has, TandemDevices.BitSealed()) && !locked {
      this.Quest(door, n"ForceUnseal");
    }
  }

  // A lift someone rides leaves in every game at once (GoToFloor: its own
  // doors, its own ride); set quietly, or streamed out, it is just there.
  private func ApplyLift(lift: ref<LiftControllerPS>, floor: Int32, ride: Bool) -> Void {
    if floor < 0 || lift.GetActiveFloor() == floor {
      return;
    }
    if lift.IsMoving() {
      return; // on its way somewhere: looked at again when it arrives
    }
    let game = this.GetGameInstance();
    if ride {
      let go = new QuestGoToFloor();
      go.clearanceLevel = 99;
      go.SetUp(lift);
      go.SetProperties(floor);
      GameInstance.GetPersistencySystem(game).QueuePSDeviceEvent(go);
    } else {
      let put = new QuestForceTeleportToFloor();
      put.clearanceLevel = 99;
      put.SetUp(lift);
      put.SetProperties(floor);
      GameInstance.GetPersistencySystem(game).QueuePSDeviceEvent(put);
    }
  }

  // The alarm. Host, a guest's doing (its copy of a camera saw its V): our
  // system hears of that V where its puppet stands, so our NPCs go there.
  // Anyone else: the system is put in the state the host has.
  private func ApplySecurity(security: ref<SecuritySystemControllerPS>, state: Int32, by: Uint32) -> Void {
    let game = this.GetGameInstance();
    if by != 0u && state >= 2 {
      let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
      let puppet: ref<GameObject>;
      if IsDefined(system) {
        puppet = system.PuppetOf(by);
      }
      if IsDefined(puppet) {
        let kind = state == 3 ? ESecurityNotificationType.COMBAT : ESecurityNotificationType.ALARM;
        security.ReportPotentialSituation(security.ActionSecurityBreachNotification(puppet.GetWorldPosition(), puppet, kind));
        return;
      }
    }
    let set = new SetSecuritySystemState();
    set.state = IntEnum<ESecuritySystemState>(state);
    this.Send(security, set);
  }

  // ---- a camera or turret turned: friendly to every V ----

  // Our V or a partner's puppet (from any thread: it only reads).
  public static func IsAnyV(game: GameInstance, target: wref<GameObject>) -> Bool {
    if !IsDefined(target) {
      return false;
    }
    if target.IsPlayer() {
      return true;
    }
    let system = TandemSystem.Of(game);
    return IsDefined(system) && system.PlayerOfPuppet(target.GetEntityID()) != 0u;
  }

  // The remote players' bodies here.
  private func Puppets() -> array<ref<GameObject>> {
    let bodies: array<ref<GameObject>>;
    let system = TandemSystem.Of(this.GetGameInstance());
    if IsDefined(system) {
      for player in system.PuppetPlayers() {
        let body = system.PuppetOf(player);
        if IsDefined(body) {
          ArrayPush(bodies, body);
        }
      }
    }
    return bodies;
  }

  // Whoever turned it (any player, in any game: the session's word is its
  // state's), a camera or turret in our world is friendly to our V the way
  // the Friendly Mode quickhack makes it (the game's own SetDeviceAttitude,
  // once its entity is here: its state alone left it hostile to our V, which
  // its alarm then called an intruder), and to every puppet as it is to our
  // V. Each look, so a puppet that comes later, and an entity that streams in
  // again, are turned too. fresh: the word just came (Apply): our V too
  // when it is not friendly yet; else only when it is hostile (a Friendly
  // Mode that ran out leaves it neutral, as in solo). On the tick (Look,
  // Apply): no hook does this.
  private func KeepFriendly(device: ref<TandemDevice>, ps: ref<ScriptableDeviceComponentPS>, fresh: Bool) -> Void {
    let sensor = ps as SensorDeviceControllerPS;
    if !IsDefined(sensor) || sensor.IsControlledByPlayer() {
      return;
    }
    let body = GameInstance.FindEntityByID(this.GetGameInstance(), device.entity) as SensorDevice;
    if !IsDefined(body) {
      return;
    }
    if !sensor.IsAttitudeChanged() {
      if device.befriended {
        this.Unbefriend(device);
      }
      return;
    }
    let did = body.TandemKeepFriendly(this.Puppets(), fresh);
    if did > 0 {
      let line = s"devices: \(TandemDevices.Name(device)) turned for every V here: \(did == 1 || did == 3 ? "our V (the game's own SetDeviceAttitude)" : "our V already") and \(did >= 2 ? "the puppets" : "no puppet to turn")";
      // Once a turn in the log; again (an action the game did not take yet,
      // a puppet that came) in the debug log.
      if fresh || !device.befriended {
        TandemNative.Log(line);
      } else {
        TandemNative.LogDebug(line);
      }
      device.befriended = true;
    }
  }

  // The session's word cleared it: hostile again as its own was, for our V
  // and every puppet.
  private func Unbefriend(device: ref<TandemDevice>) -> Void {
    device.befriended = false;
    let body = GameInstance.FindEntityByID(this.GetGameInstance(), device.entity) as SensorDevice;
    if IsDefined(body) && !body.GetDevicePS().IsControlledByPlayer() {
      body.TandemUnbefriend(this.Puppets());
      TandemNative.Log(s"devices: \(TandemDevices.Name(device)) back to its own attitude for every V here");
    }
  }

  // ---- a partner's hack of an enemy's device, marked ----

  // Turned, distracted, or switched off by a quickhack: what an ally's hack
  // leaves on a device while it lasts.
  private static func IsHackedState(status: Int32, flags: Int32) -> Bool {
    return TandemDevices.Has(flags, TandemDevices.BitFriendly()) || TandemDevices.Has(flags, TandemDevices.BitGlitching()) || (status == 0 && TandemDevices.Has(flags, TandemDevices.BitHacked()));
  }

  // An enemy's: on a security system that is not our V's friend, or a camera
  // or turret whose own side (before it was turned) is hostile to our V. A
  // device of ours or a neutral one shows nothing.
  private func WasEnemy(ps: ref<ScriptableDeviceComponentPS>, body: ref<GameObject>) -> Bool {
    let player = GetPlayer(this.GetGameInstance());
    if !IsDefined(player) || !IsDefined(player.GetAttitudeAgent()) {
      return false;
    }
    let ours = player.GetAttitudeAgent().GetAttitudeGroup();
    let security = ps.GetSecuritySystem();
    if IsDefined(security) && NotEquals(GameInstance.GetAttitudeSystem(this.GetGameInstance()).GetAttitudeRelation(security.GetSecuritySystemAttitudeGroupName(), ours), EAIAttitude.AIA_Friendly) {
      return true;
    }
    let sensor = body as SensorDevice;
    return IsDefined(sensor) && sensor.TandemOwnSideHostile(ours);
  }

  // Twice a second: a marker (world, minimap, map; the hacker's colour,
  // "Hacked by NAME") on each enemy's device in our world another player's
  // hack holds; gone when that ends, the device leaves our world, or the
  // markers are switched off.
  private func UpdateMarks() -> Void {
    let game = this.GetGameInstance();
    let shown = TandemSettings.Markers();
    for device in this.m_devices {
      if device.hackedBy != 0u || device.mark.value != 0ul {
        let want = false;
        let body: ref<GameObject>;
        let ps: ref<ScriptableDeviceComponentPS>;
        if shown && device.attached && device.hackedBy != 0u && device.hackedBy != this.m_you {
          body = GameInstance.FindEntityByID(game, device.entity) as GameObject;
          ps = this.StateOf(device);
          want = IsDefined(body) && IsDefined(ps) && TandemDevices.IsHackedState(TandemDevices.StatusOf(ps), TandemDevices.FlagsOf(ps)) && this.WasEnemy(ps, body);
        }
        if !want {
          this.Unmark(device);
        } else if device.mark.value == 0ul || device.markBy != device.hackedBy {
          this.Unmark(device);
          let comms = TandemComms.Of(game);
          if IsDefined(comms) {
            let name = TandemCoop.NameOf(device.hackedBy);
            device.mark = comms.MarkerOn(body, n"Use", TandemText.F("Devices-HackedBy", name), TandemText.F("Devices-HackedByDescription", name), device.hackedBy);
            device.markBy = device.hackedBy;
            TandemNative.Log(s"devices: \(TandemDevices.Name(device)) marked: hacked by player \(device.hackedBy), registered \(device.mark.value != 0ul)");
          }
        }
      }
    }
  }

  private func Unmark(device: ref<TandemDevice>) -> Void {
    if device.mark.value == 0ul {
      return;
    }
    GameInstance.GetMappinSystem(this.GetGameInstance()).UnregisterMappin(device.mark);
    TandemNative.Log(s"devices: \(TandemDevices.Name(device)) unmarked (player \(device.markBy)'s hack)");
    let none: NewMappinID;
    device.mark = none;
    device.markBy = 0u;
  }

  // (test) TestQuery.reds, "device KIND WHAT": the nearest device of a kind
  // within 40 m, read from the game. attitude: its attitude to our V;
  // puppet: to the first puppet; target: what it follows (none, V, puppet,
  // other); alarm: its security system's state (none: on none); mark: the
  // player whose hack is marked on it (0: none); look: an explosive's
  // entity, whole or wreck (none: not one, or not in our world). KIND may
  // be a device's entity (hex, as the commands name it) instead.
  public func Query(kind: String, what: String) -> String {
    let distance = 40.0;
    let device = this.ByHex(kind);
    if !IsDefined(device) {
      device = this.Nearest(kind, distance);
    }
    if !IsDefined(device) {
      return "none";
    }
    let game = this.GetGameInstance();
    let body = GameInstance.FindEntityByID(game, device.entity) as GameObject;
    let ps = this.StateOf(device);
    let player = GetPlayer(game);
    let puppets = this.Puppets();
    if Equals(what, "attitude") {
      return IsDefined(body) && IsDefined(player) ? TandemDevices.AttitudeWord(GameObject.GetAttitudeTowards(body, player)) : "none";
    }
    if Equals(what, "puppet") {
      return IsDefined(body) && ArraySize(puppets) > 0 ? TandemDevices.AttitudeWord(GameObject.GetAttitudeTowards(body, puppets[0])) : "none";
    }
    if Equals(what, "target") {
      let sensor = body as SensorDevice;
      let target = IsDefined(sensor) ? sensor.GetCurrentlyFollowedTarget() : null;
      if !IsDefined(target) {
        return "none";
      }
      if target == player {
        return "V";
      }
      for puppet in puppets {
        if target == puppet {
          return "puppet";
        }
      }
      return "other";
    }
    if Equals(what, "alarm") {
      let security = IsDefined(ps) ? ps.GetSecuritySystem() : null;
      return IsDefined(security) ? EnumValueToString("ESecuritySystemState", Cast<Int64>(EnumInt(security.GetSecurityState()))) : "none";
    }
    if Equals(what, "mark") {
      return IntToString(Cast<Int32>(device.markBy));
    }
    if Equals(what, "look") {
      let explosive = body as ExplosiveDevice;
      return IsDefined(explosive) ? explosive.TandemLook() : "none";
    }
    return "unknown";
  }

  // The followed device of that entity (hex, TandemNative.EntityHex).
  private func ByHex(hex: String) -> ref<TandemDevice> {
    for device in this.m_devices {
      if Equals(TandemNative.EntityHex(device.entity), hex) {
        return device;
      }
    }
    return null;
  }

  private static func AttitudeWord(attitude: EAIAttitude) -> String {
    switch attitude {
      case EAIAttitude.AIA_Friendly:
        return "friendly";
      case EAIAttitude.AIA_Neutral:
        return "neutral";
      default:
        return "hostile";
    }
  }

  // ---- hits ----

  // A hit dealt by a device (a turret, a mine, a barrel going up), whoever
  // set it off.
  public static func IsDeviceHit(hitEvent: ref<gameHitEvent>) -> Bool {
    if !IsDefined(hitEvent) || !IsDefined(hitEvent.attackData) {
      return false;
    }
    return IsDefined(hitEvent.attackData.GetSource() as Device) || IsDefined(hitEvent.attackData.GetInstigator() as Device);
  }

  // ---- commands (tools/game.sh cmd device ...) ----

  private static func IsKind(ps: ref<ScriptableDeviceComponentPS>, kind: String) -> Bool {
    if Equals(kind, "") || Equals(kind, "any") {
      return true;
    }
    if Equals(kind, "door") {
      return IsDefined(ps as DoorControllerPS);
    }
    // A door that opens itself for who comes near (EDoorType.AUTOMATIC).
    if Equals(kind, "autodoor") {
      let door = ps as DoorControllerPS;
      return IsDefined(door) && Equals(door.GetDoorType(), EDoorType.AUTOMATIC);
    }
    // A placed trap that goes off when a V walks into it, still whole.
    if Equals(kind, "mine") {
      let trap = ps as ExplosiveTriggerDeviceControllerPS;
      return IsDefined(trap) && !trap.IsExploded();
    }
    if Equals(kind, "lift") {
      return IsDefined(ps as LiftControllerPS);
    }
    if Equals(kind, "camera") {
      return IsDefined(ps as SurveillanceCameraControllerPS);
    }
    if Equals(kind, "turret") {
      return IsDefined(ps as SecurityTurretControllerPS);
    }
    // A turret the session follows (Turrets.reds): not the police's, which
    // stand at street corners (every fixture's nearest was one, 2026-11-21).
    if Equals(kind, "sentry") {
      let turret = ps as SecurityTurretControllerPS;
      return IsDefined(turret) && !turret.IsPartOfPrevention();
    }
    // One still whole: a barrel that went up (an earlier test, a fight)
    // stays exploded for the rest of the game.
    if Equals(kind, "barrel") {
      let explosive = ps as ExplosiveDeviceControllerPS;
      return IsDefined(explosive) && !IsDefined(ps as SensorDeviceControllerPS) && !explosive.IsExploded();
    }
    if Equals(kind, "ap") {
      return IsDefined(ps as AccessPointControllerPS);
    }
    if Equals(kind, "radio") {
      return IsDefined(ps as MediaDeviceControllerPS);
    }
    return false;
  }

  // The entity of the nearest device of a kind, within `distance` (Breach.reds,
  // Interact.reds).
  public func NearestOf(kind: String, distance: Float) -> ref<GameObject> {
    let device = this.Nearest(kind, distance);
    return IsDefined(device) ? GameInstance.FindEntityByID(this.GetGameInstance(), device.entity) as GameObject : null;
  }

  // The nearest device of a kind in our world, within `distance` of our V.
  private func Nearest(kind: String, out distance: Float) -> ref<TandemDevice> {
    let game = this.GetGameInstance();
    let player = GetPlayer(game);
    let best: ref<TandemDevice>;
    if !IsDefined(player) {
      return null;
    }
    let here = player.GetWorldPosition();
    for device in this.m_devices {
      if device.attached {
        let object = GameInstance.FindEntityByID(game, device.entity) as GameObject;
        let ps = this.StateOf(device);
        if IsDefined(object) && IsDefined(ps) && TandemDevices.IsKind(ps, kind) {
          let far = Vector4.Distance(here, object.GetWorldPosition());
          if far < distance {
            distance = far;
            best = device;
          }
        }
      }
    }
    return best;
  }

  // (test, Fixtures.reds: fixture check) The devices of our world within
  // `radius` m of our V by class, then the nearest of each kind the device
  // commands take, within 200 m, with where it stands: a scenario's spot
  // confirmed, or the way to a better one.
  public func Survey(radius: Float) -> Void {
    let game = this.GetGameInstance();
    let player = GetPlayer(game);
    if !IsDefined(player) {
      return;
    }
    let here = player.GetWorldPosition();
    let classes: array<CName>;
    let counts: array<Int32>;
    let total = 0;
    for device in this.m_devices {
      let object: ref<GameObject>;
      if device.attached {
        object = GameInstance.FindEntityByID(game, device.entity) as GameObject;
      }
      if IsDefined(object) && Vector4.Distance(here, object.GetWorldPosition()) <= radius {
        total += 1;
        let found = -1;
        let i = 0;
        while i < ArraySize(classes) {
          if Equals(classes[i], device.cls) {
            found = i;
          }
          i += 1;
        }
        if found < 0 {
          ArrayPush(classes, device.cls);
          ArrayPush(counts, 1);
        } else {
          counts[found] += 1;
        }
      }
    }
    let listed = "";
    let j = 0;
    while j < ArraySize(classes) {
      let separator = j > 0 ? ", " : "";
      listed += s"\(separator)\(NameToString(classes[j])) \(counts[j])";
      j += 1;
    }
    TandemNative.Log(s"fixture: devices within \(radius) m: \(total) (\(listed))");
    let kinds: array<String> = ["door", "autodoor", "lift", "camera", "turret", "sentry", "barrel", "mine", "ap", "radio"];
    for kind in kinds {
      let distance = 200.0;
      let device = this.Nearest(kind, distance);
      let object: ref<GameObject>;
      let ps: ref<ScriptableDeviceComponentPS>;
      if IsDefined(device) {
        object = GameInstance.FindEntityByID(game, device.entity) as GameObject;
        ps = this.StateOf(device);
      }
      if IsDefined(object) && IsDefined(ps) {
        let at = object.GetWorldPosition();
        TandemNative.Log(s"fixture: nearest \(kind) \(TandemDevices.Name(device)) at \(distance) m (\(at.X) \(at.Y) \(at.Z)): \(TandemDevices.Describe(TandemDevices.StatusOf(ps), TandemDevices.FlagsOf(ps), TandemDevices.ValueOf(ps)))");
      } else {
        TandemNative.Log(s"fixture: no \(kind) within 200 m");
      }
    }
  }

  // (test, Parity.reds) every device we follow within `radius` of one of
  // `centers` (the Vs drawn here), with its state as the session says it:
  // one "parity device:" line each. How many.
  public func ParityList(centers: array<ref<GameObject>>, radius: Float) -> Int32 {
    let game = this.GetGameInstance();
    let count = 0;
    for device in this.m_devices {
      let object: ref<GameObject>;
      if device.attached {
        object = GameInstance.FindEntityByID(game, device.entity) as GameObject;
      }
      let ps = IsDefined(object) ? this.StateOf(device) : null;
      if IsDefined(ps) && TandemParity.Within(object, centers, radius) {
        let at = object.GetWorldPosition();
        TandemNative.Log(s"parity device: \(TandemNative.EntityHex(device.entity)) \(NameToString(device.cls)) at \(at.X) \(at.Y) \(at.Z) status \(TandemDevices.StatusOf(ps)) flags \(TandemDevices.FlagsOf(ps)) value \(TandemDevices.ValueOf(ps))");
        count += 1;
      }
    }
    return count;
  }

  public func Command(command: String) -> Void {
    let words = StrSplit(command, " ");
    let verb = ArraySize(words) > 1 ? words[1] : "";
    let arg = ArraySize(words) > 2 ? words[2] : "";
    if Equals(verb, "") {
      let attached = 0;
      let known = 0;
      let kept = 0;
      for device in this.m_devices {
        attached += device.attached ? 1 : 0;
        known += device.known ? 1 : 0;
        kept += device.kept ? 1 : 0;
      }
      TandemNative.Log(s"devices: \(ArraySize(this.m_devices)) followed, \(attached) in our world, \(known) the session's, \(kept) with our own state kept");
      return;
    }
    let kind = arg;
    let number = 0;
    if Equals(verb, "floor") {
      kind = "lift";
      number = Equals(arg, "other") ? -1 : StringToInt(arg, 0);
    } else if Equals(verb, "station") {
      kind = "radio";
      number = StringToInt(arg, 0);
    } else if Equals(verb, "explode") && Equals(arg, "") {
      kind = "barrel";
    } else if (Equals(verb, "open") || Equals(verb, "close") || Equals(verb, "lock") || Equals(verb, "unlock")) && Equals(arg, "") {
      kind = "door";
    }
    let what = Equals(kind, "") ? "device" : kind;
    // tell names the same device near found (devices-barrel: near found one
    // 42 m away, tell then none within 40 m); a lift's floor too, from its
    // cabin (the partner rode 47.7 m down from the lift's controller, then
    // found none within 40 m, pair-devices-lift 2026-11-28).
    let distance = Equals(verb, "near") || Equals(verb, "goto") || Equals(verb, "tell") || Equals(verb, "floor") ? 200.0 : 40.0;
    let range = distance;
    let device = this.Nearest(kind, distance);
    if !IsDefined(device) {
      TandemNative.Log(s"devices: no \(what) within \(range) m");
      return;
    }
    let ps = this.StateOf(device);
    let status = TandemDevices.StatusOf(ps);
    let flags = TandemDevices.FlagsOf(ps);
    let value = TandemDevices.ValueOf(ps);
    if Equals(verb, "near") {
      TandemNative.Log(s"devices: nearest \(what) \(TandemDevices.Name(device)) at \(distance) m: \(TandemDevices.Describe(status, flags, value))");
      return;
    }
    if Equals(verb, "goto") {
      // Beside it (1.5 m, or the metres given: goto KIND M), on our side
      // of it, at its height.
      let game = this.GetGameInstance();
      let player = GetPlayer(game);
      let object = GameInstance.FindEntityByID(game, device.entity) as GameObject;
      let beside = ArraySize(words) > 3 ? StringToFloat(words[3]) : 1.5;
      if beside <= 0.0 {
        beside = 1.5;
      }
      let there = object.GetWorldPosition();
      let back = player.GetWorldPosition() - there;
      back.Z = 0.0;
      let far = Vector4.Length(back);
      let to = far > 0.1 ? there + back * (beside / far) : there + new Vector4(beside, 0.0, 0.0, 0.0);
      to.W = 1.0;
      TandemTravel.ExpectMove(game);
      GameInstance.GetTeleportationFacility(game).Teleport(player, to, player.GetWorldOrientation().ToEulerAngles());
      TandemNative.Log(s"devices: at \(what) \(TandemDevices.Name(device)): (\(to.X), \(to.Y), \(to.Z)), \(TandemDevices.Describe(status, flags, value))");
      return;
    }
    let want = flags;
    if Equals(verb, "open") {
      want = TandemDevices.With(TandemDevices.Without(TandemDevices.Without(flags, TandemDevices.BitLocked()), TandemDevices.BitSealed()), TandemDevices.BitOpen());
    } else if Equals(verb, "close") {
      want = TandemDevices.Without(flags, TandemDevices.BitOpen());
    } else if Equals(verb, "lock") {
      want = TandemDevices.With(TandemDevices.Without(flags, TandemDevices.BitOpen()), TandemDevices.BitLocked());
    } else if Equals(verb, "unlock") {
      want = TandemDevices.Without(flags, TandemDevices.BitLocked());
    } else if Equals(verb, "on") {
      status = 1;
    } else if Equals(verb, "off") {
      status = 0;
    } else if Equals(verb, "explode") {
      want = TandemDevices.With(flags, TandemDevices.BitExploded());
    } else if Equals(verb, "distract") {
      want = TandemDevices.With(flags, TandemDevices.BitGlitching());
    } else if Equals(verb, "friendly") {
      want = TandemDevices.With(flags, TandemDevices.BitFriendly());
    } else if Equals(verb, "hostile") {
      // Its Friendly Mode over: no longer turned, nor hacked (a camera that
      // was off stayed "switched off by a quickhack", marked, 2026-11-28).
      want = TandemDevices.Without(TandemDevices.Without(flags, TandemDevices.BitFriendly()), TandemDevices.BitHacked());
    } else if Equals(verb, "floor") || Equals(verb, "station") {
      // floor other: the ground floor, or the one above it when it is there.
      value = number >= 0 ? number : (value == 0 ? 1 : 0);
    } else if Equals(verb, "tell") {
      TandemNative.ReportDevice(device.entity, device.cls, status, flags, value, this.m_you);
      TandemNative.Log(s"devices: told the session of \(what) \(TandemDevices.Name(device)): \(TandemDevices.Describe(status, flags, value))");
      return;
    } else {
      TandemNative.Log(s"devices: unknown command: \(command)");
      return;
    }
    TandemNative.Log(s"devices: \(verb) \(TandemDevices.Name(device)) at \(distance) m: to \(TandemDevices.Describe(status, want, value))");
    // As our V would have: it changes here, and the next look tells the
    // session (a report on the host, a request from a guest).
    this.Apply(device, ps, status, want, value, false, 0u);
    device.dirty = true;
    this.m_dirty = true;
  }
}

// Every device that comes into the world, and goes. These run as the world
// streams in, on job threads during a load: queued only (no script state
// touched here), taken by TandemDevices.Drain on the game thread's tick.
@wrapMethod(Device)
protected cb func OnGameAttached() -> Bool {
  let result = wrappedMethod();
  TandemStreaming.QueueStreamed(TandemStreamKind.Device(), this.GetEntityID(), n"None", true);
  return result;
}

@wrapMethod(Device)
protected cb func OnDetach() -> Bool {
  TandemStreaming.QueueStreamed(TandemStreamKind.Device(), this.GetEntityID(), n"None", false);
  return wrappedMethod();
}

// Every action a device's state handles ends here (players, quickhacks,
// quests, other devices), streamed in or not: queued as above.
@wrapMethod(ScriptableDeviceComponentPS)
protected func Notify(notifier: ref<ActionNotifier>, action: ref<ScriptableDeviceAction>) -> Void {
  wrappedMethod(notifier, action);
  TandemDevices.QueueTouched(this);
}

// A guest's barrel, set off on the host's copy from its puppet: what
// ExplosiveDevice.OnForceDetonate does (final.reds:120801), with the V's
// puppet as the instigator of its death and its blast instead of the
// barrel itself.
@addMethod(ExplosiveDevice)
public final func TandemDetonate(instigator: ref<GameObject>) -> Void {
  GameInstance.GetStatPoolsSystem(this.GetGame()).RequestSettingStatPoolValue(Cast<StatsObjectID>(this.GetEntityID()), gamedataStatPoolType.Health, 0.0, instigator, false);
  this.StartExplosionPipeline(instigator);
}

// An explosive the host's record says went up, standing whole in front of
// us (a late joiner's or a resync's record): what a whole one has running
// stopped (as its own explosion stops them: Explode, HandleDeath), then the
// look the engine gives one that streams in exploded
// (ResolveGameplayState: ToggleVisibility(false): mesh and collider off,
// its exploded appearance, no gameplay role, disabled). No attack, no
// physics pulse, no VFX. True when its mesh is off (read back). On the
// tick only (TandemDevices.Apply).
@addMethod(ExplosiveDevice)
public final func TandemShowExploded() -> Bool {
  this.KillAllFxInstances();
  this.EffectsOnStartStop(true);
  if NotEquals(this.m_loopAudioEvent, n"None") {
    GameObject.StopSoundEvent(this, this.m_loopAudioEvent);
  }
  if IsDefined(this.m_distractionCollider) {
    this.m_distractionCollider.Toggle(false);
  }
  this.RemoveEnvironmentalHazardStimuli();
  let light = new ChangeLightEvent();
  light.settings.strength = 0.0;
  light.settings.color = new Color(Cast<Uint8>(0), Cast<Uint8>(0), Cast<Uint8>(0), Cast<Uint8>(0));
  this.QueueEvent(light);
  this.ToggleVisibility(false);
  return !IsDefined(this.m_mesh) || !this.m_mesh.IsEnabled();
}

// (test) Its look: wreck (mesh off) or whole.
@addMethod(ExplosiveDevice)
public final func TandemLook() -> String {
  return IsDefined(this.m_mesh) && this.m_mesh.IsEnabled() ? "whole" : "wreck";
}

// An explosive that went up says so on its state from its entity, with no
// action: caught here.
@wrapMethod(ExplosiveDeviceControllerPS)
public final func SetExplodedState(state: Bool) -> Void {
  wrappedMethod(state);
  TandemDevices.QueueTouched(this);
}

// ---- a camera or turret turned, on its entity (TandemDevices.KeepFriendly) ----

// When we last asked the game to turn it for our V (engine time).
@addField(SensorDevice)
public let m_tandemBefriendAt: Float;

// Turned (its state says so) yet hostile to our V: the game's own Friendly
// Mode on it (SetDeviceAttitude, as the quickhack's ForceIgnoreTargets
// becomes: SensorDevice.SetDeviceFriendly makes it friendly to the local
// player, its group the player's, its senses off our V); asked again 2 s
// later at most (fresh: not yet friendly to our V is enough). Each puppet
// it holds another attitude to then gets the one it has to our V, and its senses
// drop the puppet (SetDetectionOverwrite, as SetDeviceFriendly does for our
// V). 1: our V asked, 2: puppets turned, 3: both, 0: nothing to do. On the
// tick only.
@addMethod(SensorDevice)
public func TandemKeepFriendly(puppets: array<ref<GameObject>>, fresh: Bool) -> Int32 {
  let did = 0;
  let ps = this.GetDevicePS();
  let player = GetPlayer(this.GetGame());
  if !IsDefined(ps) || !IsDefined(player) {
    return 0;
  }
  let toV = GameObject.GetAttitudeTowards(this, player);
  if Equals(toV, EAIAttitude.AIA_Hostile) || (fresh && NotEquals(toV, EAIAttitude.AIA_Friendly)) {
    let now = EngineTime.ToFloat(GameInstance.GetEngineTime(this.GetGame()));
    if fresh || this.m_tandemBefriendAt == 0.0 || now - this.m_tandemBefriendAt >= 2.0 || now < this.m_tandemBefriendAt {
      this.m_tandemBefriendAt = now;
      let attitude = ps.ActionSetDeviceAttitude();
      attitude.Attitude = EAIAttitude.AIA_Friendly;
      attitude.Repeat = false;
      ps.ExecutePSAction(attitude, ps);
      did += 1;
    }
    toV = EAIAttitude.AIA_Friendly;
  }
  let mine = this.GetAttitudeAgent();
  if !IsDefined(mine) {
    return did;
  }
  let turned = false;
  for puppet in puppets {
    let theirs = puppet.GetAttitudeAgent();
    // Not only a hostile one: a guest's copy of the host's camera was
    // neutral to the host's puppet and stayed so, while friendly to the
    // guest's V (pair-devices-camera, 2026-11-28).
    if IsDefined(theirs) && NotEquals(mine.GetAttitudeTowards(theirs), toV) {
      mine.SetAttitudeTowards(theirs, toV);
      if IsDefined(this.m_senseComponent) {
        this.m_senseComponent.SetDetectionOverwrite(puppet.GetEntityID());
      }
      turned = true;
    }
  }
  if turned {
    // A puppet it followed (a guest's copy had it: Turrets.reds, TandemEngage)
    // is no target any more.
    let followed = this.GetCurrentlyFollowedTarget();
    for puppet in puppets {
      if IsDefined(followed) && followed == puppet {
        this.LoseTarget(puppet, true);
      }
    }
    this.ReevaluateTargets();
    did += 2;
  }
  return did;
}

// Its own side again (the session's word cleared the turn): its security
// system's group, as the game puts it back when that system is enabled
// again (SensorDevice.OnSecuritySystemEnabled), else the group it had
// before it was turned (SetDeviceFriendly cached it); that group's attitude
// to our V and to each puppet; its senses back on them; its sense preset,
// aim assist and lights as before it was turned. On the tick only.
@addMethod(SensorDevice)
public func TandemUnbefriend(puppets: array<ref<GameObject>>) -> Void {
  let ps = this.GetDevicePS();
  let mine = this.GetAttitudeAgent();
  let player = GetPlayer(this.GetGame());
  if !IsDefined(ps) || !IsDefined(mine) || !IsDefined(player) || !IsDefined(player.GetAttitudeAgent()) {
    return;
  }
  let security = ps.GetSecuritySystem();
  if IsDefined(security) {
    mine.SetAttitudeGroup(security.GetSecuritySystemAttitudeGroupName());
  } else if IsNameValid(this.m_initialAttitude) && NotEquals(this.m_initialAttitude, n"None") {
    mine.SetAttitudeGroup(this.m_initialAttitude);
  }
  this.m_initialAttitude = n"None";
  let relation = GameInstance.GetAttitudeSystem(this.GetGame()).GetAttitudeRelation(mine.GetAttitudeGroup(), player.GetAttitudeAgent().GetAttitudeGroup());
  mine.SetAttitudeTowards(player.GetAttitudeAgent(), relation);
  for puppet in puppets {
    let theirs = puppet.GetAttitudeAgent();
    if IsDefined(theirs) {
      mine.SetAttitudeTowards(theirs, relation);
    }
  }
  this.KeepHostilityTowardsPlayerHostiles(false);
  if TDBID.IsValid(this.m_defaultSensePreset) {
    SenseComponent.RequestPresetChange(this, this.m_defaultSensePreset, true);
  }
  this.QueueEvent(new EnableAimAssist());
  if IsDefined(this.m_senseComponent) {
    this.m_senseComponent.ReevaluateDetectionOverwrite(player);
    for puppet in puppets {
      this.m_senseComponent.ReevaluateDetectionOverwrite(puppet);
    }
  }
  this.ReevaluateTargets();
  this.DetermineLightAttitudeRefs();
  this.DetermineLightScanRefs(this.m_lightColors.red);
}

// Its own side (before it was turned: the group SetDeviceFriendly cached,
// else its group now) is hostile to group `ours` (our V's).
@addMethod(SensorDevice)
public func TandemOwnSideHostile(ours: CName) -> Bool {
  let group = IsNameValid(this.m_initialAttitude) && NotEquals(this.m_initialAttitude, n"None") ? this.m_initialAttitude : (IsDefined(this.GetAttitudeAgent()) ? this.GetAttitudeAgent().GetAttitudeGroup() : n"None");
  return IsNameValid(group) && NotEquals(group, n"None") && Equals(GameInstance.GetAttitudeSystem(this.GetGame()).GetAttitudeRelation(group, ours), EAIAttitude.AIA_Hostile);
}

// A turned camera or turret (its state says so: any player's doing, in any
// game) takes no V for a target in a session: our own, or a partner's
// puppet. Its entity is turned on the tick (KeepFriendly); until then (just
// streamed in) this keeps its alarm quiet. Job thread safe: it only reads.
@wrapMethod(SensorDevice)
public func SetAsIntrestingTarget(target: wref<GameObject>) -> Bool {
  if IsDefined(target) && TandemNative.IsStarted() {
    let ps = this.GetDevicePS();
    if IsDefined(ps) && ps.IsAttitudeChanged() && !ps.IsControlledByPlayer() && TandemDevices.IsAnyV(this.GetGame(), target) {
      return false;
    }
  }
  return wrappedMethod(target);
}
