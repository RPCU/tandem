// Breach Protocol between players (docs/architecture/devices.md, "Breach
// Protocol"; engine facts: docs/research/engine/combat.md §13.60).
//
// The minigame is the breacher's own, as in single player: its screen, its
// timer, its rewards (the datamine's money, crafting components and shards
// roll on its game, for its V). Its outcome is not the breacher's to keep:
//
// - Breacher: the game ends every breach, won or lost, with an
//   AccessPointMiniGameStatus event to what was breached (an access point,
//   a device through the quickhack, an NPC through the quickhack); the
//   daemons are on the HackingMinigame blackboard. Both are caught here,
//   not the minigame's screen, so a mod that skips the minigame and fakes
//   the outcome (AccessPointSkip: Device.PerformDive, a succeeded status
//   with the programs) is covered as it is. The session hears the breach:
//   the access point (or the NPC) and the daemons.
// - Host: the first breach of a network stands, any later one is refused
//   (core devices::Breaches); the access point's record says breached for
//   everyone, so every game's copy is breached (Devices.reds) and the game
//   itself offers no second jack-in.
// - Every game but the breacher's runs the daemons on what it owns, the
//   game's own way (ProcessMinigameNetworkActions and ScriptedPuppetPS
//   .OnSetExposeQuickHacks, with the breacher's puppet for the player): the
//   host on the network's devices (cameras off, turrets friendly...) and
//   on its NPCs of it; a guest on its own NPCs of it; the owner of an NPC
//   breached through the quickhack on that NPC (and, the host, on its
//   network). What they did comes back the usual ways: devices through
//   their state (Devices.reds), NPCs' daemon effects through NpcStatus
//   (Hacks.reds), so proxies look weakened and partners see the effects.
// - Never run anywhere but on the breacher's: rewards (loot programs, the
//   material trap), the minigame's own XP and perks on the breacher's V.
//
// tools/game.sh cmd breach (what we breached and applied) | breach ap
// [PROGRAMS] | breach fail ap | breach npc [PROGRAMS]: the outcome of a
// breach of the nearest access point or proxy faked as AccessPointSkip does
// (the blackboard's programs, a status event), so the hooks below run as
// for a real minigame. PROGRAMS: MinigameAction records, comma separated.

public class TandemBreach extends ScriptableSystem {
  private let m_you: Uint32;
  private let m_host: Bool;
  private let m_now: Float;
  // NPCs our game just ran a breach on (their entity, when): an NPC and its
  // network said one after the other run once.
  private let m_npcs: array<EntityID>;
  private let m_npcsAt: array<Float>;
  private let m_ours: Int32;
  private let m_applied: Int32;
  private let m_refused: Int32;
  private let m_breacher: Uint32;

  public static func Get(game: GameInstance) -> ref<TandemBreach> {
    return GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemBreach") as TandemBreach;
  }

  // Every frame, from TandemSystem.Tick (you: 0 out of a session).
  public static func Tick(game: GameInstance, now: Float, you: Uint32) -> Void {
    let breach = TandemBreach.Get(game);
    if IsDefined(breach) {
      breach.Update(now, you);
    }
  }

  public func Update(now: Float, you: Uint32) -> Void {
    this.m_now = now;
    let events = TandemNative.TakeBreachEvents();
    if you == 0u {
      if this.m_you != 0u {
        ArrayClear(this.m_npcs);
        ArrayClear(this.m_npcsAt);
      }
      this.m_you = 0u;
      return;
    }
    if this.m_you != you {
      this.m_you = you;
      this.m_host = TandemNative.IsHost();
    }
    for event in events {
      if event.kind == 1 {
        this.Apply(event);
      } else if event.kind == 2 {
        this.Answered(event);
      }
    }
  }

  public func InSession() -> Bool {
    return this.m_you != 0u;
  }

  public func NoteOurs() -> Void {
    this.m_ours += 1;
  }

  // ---- the daemons ----

  // What a breach pays: its breacher's, rolled on its game only.
  public static func IsLoot(program: TweakDBID) -> Bool {
    return program == t"MinigameAction.NetworkDataMineLootAll" || program == t"MinigameAction.NetworkDataMineLootAllAdvanced" || program == t"MinigameAction.NetworkDataMineLootAllMaster" || program == t"MinigameAction.NetworkLootQ003" || program == t"MinigameAction.NetworkLootShard" || program == t"MinigameAction.NetworkLootMaterials" || program == t"MinigameAction.NetworkLootMoney" || program == t"MinigameAction.NetworkLootMQ024" || program == t"MinigameAction.NetworkLootMQ015" || program == t"MinigameAction.NetworkLootMQ015Recipe" || program == t"minigame_v2.FindAnna";
  }

  // The status effects the daemons put on NPCs (Hacks.reds sends them as
  // NpcStatus; Combat.reds never forwards them from a proxy: the owner
  // runs the breach).
  public static func IsDaemonEffect(effect: TweakDBID) -> Bool {
    return effect == t"MinigameAction.ICEBrokenMinigameMinor" || effect == t"MinigameAction.ICEBrokenMinigameMedium" || effect == t"MinigameAction.ICEBrokenMinigameMajor" || effect == t"MinigameAction.ICEBrokenMinigamePlacide" || effect == t"MinigameAction.VulnerabilityMinigame" || effect == t"MinigameAction.BuffDamageReductionMinigame";
  }

  // The daemons another game runs: no rewards, and a turret shut down
  // or turned friendly, never both (FilterRedundantPrograms).
  public static func Runnable(programs: array<TweakDBID>) -> array<TweakDBID> {
    let out: array<TweakDBID>;
    let friendly = ArrayContains(programs, t"MinigameAction.NetworkTurretFriendly");
    for program in programs {
      if !TandemBreach.IsLoot(program) && !(friendly && program == t"MinigameAction.NetworkTurretShutdown") {
        ArrayPush(out, program);
      }
    }
    return out;
  }

  private static func Blackboard(game: GameInstance) -> ref<IBlackboard> {
    return GameInstance.GetBlackboardSystem(game).Get(GetAllBlackboardDefs().HackingMinigame);
  }

  public static func Programs(game: GameInstance) -> array<TweakDBID> {
    let board = TandemBreach.Blackboard(game);
    let none: array<TweakDBID>;
    return IsDefined(board) ? FromVariant<array<TweakDBID>>(board.GetVariant(GetAllBlackboardDefs().HackingMinigame.ActivePrograms)) : none;
  }

  public static func Traps(game: GameInstance) -> array<TweakDBID> {
    let board = TandemBreach.Blackboard(game);
    let none: array<TweakDBID>;
    return IsDefined(board) ? FromVariant<array<TweakDBID>>(board.GetVariant(GetAllBlackboardDefs().HackingMinigame.ActiveTraps)) : none;
  }

  private static func Names(ids: array<TweakDBID>) -> String {
    let out = "";
    for id in ids {
      out = Equals(out, "") ? TDBID.ToStringDEBUG(id) : s"\(out),\(TDBID.ToStringDEBUG(id))";
    }
    return Equals(out, "") ? "none" : out;
  }

  // ---- our own breach ----

  // The minigame ended on a device (an access point jacked into, a device
  // through the quickhack; Device.OnAccessPointMiniGameStatus, below).
  public static func DeviceOutcome(device: ref<Device>, state: HackingMinigameState) -> Void {
    if !IsDefined(device) || (!Equals(state, HackingMinigameState.Succeeded) && !Equals(state, HackingMinigameState.Failed)) {
      return;
    }
    let game = device.GetGame();
    let breach = TandemBreach.Get(game);
    let ps = device.GetDevicePS();
    if !IsDefined(breach) || !breach.InSession() || !IsDefined(ps) {
      return;
    }
    // The networks it breached: its own, or (through the quickhack) those
    // of the device, as ResolveDive exposes them.
    let points: array<ref<AccessPointControllerPS>>;
    let own = ps as AccessPointControllerPS;
    if IsDefined(own) {
      ArrayPush(points, own);
    } else {
      points = ps.GetAccessPoints();
    }
    let flags = Equals(state, HackingMinigameState.Failed) ? 2 : 0;
    let network = GameInstance.GetBlackboardSystem(game).Get(GetAllBlackboardDefs().NetworkBlackboard);
    if !IsDefined(own) || (IsDefined(network) && network.GetBool(GetAllBlackboardDefs().NetworkBlackboard.RemoteBreach)) {
      flags += 1;
    }
    let programs: array<TweakDBID>;
    let traps: array<TweakDBID>;
    if flags < 2 {
      programs = TandemBreach.Programs(game);
      traps = TandemBreach.Traps(game);
    }
    let sent = 0;
    for point in points {
      let id = point.GetID();
      if sent < 4 && PersistentID.IsDefined(id) && !PersistentID.IsDynamic(id) {
        TandemNative.SendBreach(PersistentID.ExtractEntityID(id), point.GetClassName(), 0u, programs, traps, flags, 0u);
        sent += 1;
        breach.NoteOurs();
        let hex = TandemNative.EntityHex(PersistentID.ExtractEntityID(id));
        let how = flags >= 2 ? "lost" : "won";
        TandemNative.Log(s"breach: ours of network \(hex) \(how) (\(TandemBreach.Names(programs)); traps \(TandemBreach.Names(traps))): for the host");
      }
    }
    if sent == 0 {
      TandemNative.Log(s"breach: ours of \(TandemNative.EntityHex(device.GetEntityID())) is on no network the session knows");
    }
  }

  // The minigame ended on an NPC (the Breach Protocol quickhack;
  // ScriptedPuppet.OnAccessPointMiniGameStatus, below).
  public static func NpcOutcome(npc: ref<ScriptedPuppet>, state: HackingMinigameState) -> Void {
    if !IsDefined(npc) || (!Equals(state, HackingMinigameState.Succeeded) && !Equals(state, HackingMinigameState.Failed)) {
      return;
    }
    let game = npc.GetGame();
    let breach = TandemBreach.Get(game);
    if !IsDefined(breach) || !breach.InSession() {
      return;
    }
    let id = TandemHacks.NpcOf(game, npc.GetEntityID());
    // One of ours: the game ran its network too (NPCBreachEvent), so the
    // network is the session's to hear. A proxy has no device link here:
    // its owner finds its network.
    let entity = EntityID();
    let cls = n"None";
    if !TandemOwn.IsProxy(game, npc.GetEntityID()) {
      let ps = npc.GetPuppetPS();
      let point = IsDefined(ps) && ps.IsConnectedToAccessPoint() ? ps.GetAccessPoint() : null;
      if IsDefined(point) && !PersistentID.IsDynamic(point.GetID()) {
        entity = PersistentID.ExtractEntityID(point.GetID());
        cls = point.GetClassName();
      }
    }
    if id == 0u && !EntityID.IsDefined(entity) {
      return; // nothing the session shares
    }
    let flags = 1 + (Equals(state, HackingMinigameState.Failed) ? 2 : 0);
    let programs: array<TweakDBID>;
    let traps: array<TweakDBID>;
    if flags < 2 {
      programs = TandemBreach.Programs(game);
      traps = TandemBreach.Traps(game);
    }
    TandemNative.SendBreach(entity, cls, id, programs, traps, flags, 0u);
    breach.NoteOurs();
    let on = EntityID.IsDefined(entity) ? " on network " + TandemNative.EntityHex(entity) : "";
    let how = flags >= 2 ? "lost" : "won";
    TandemNative.Log(s"breach: ours of npc \(id)\(on) \(how) (\(TandemBreach.Names(programs))): for its owner");
  }

  // The host's answer to ours.
  private func Answered(event: TandemBreachEvent) -> Void {
    let what = EntityID.IsDefined(event.entity) ? "network " + TandemNative.EntityHex(event.entity) : "npc " + IntToString(Cast<Int32>(event.npc));
    if event.verdict == 0 {
      TandemNative.Log(s"breach: the host grants ours of \(what)");
      return;
    }
    this.m_refused += 1;
    TandemNative.Log(s"breach: the host refuses ours of \(what): breached first by player \(event.player)");
    // Our rewards were rolled already (the game pays at once); the world's
    // breach is the first breacher's, and our daemons ran nowhere else.
    let game = this.GetGameInstance();
    if event.player != 0u && event.player != this.m_you {
      TandemText.Notice(game, "Breach-Already", TandemBreach.NameOf(event.player));
    } else {
      TandemText.Notice(game, "Breach-AlreadyWorld");
    }
  }

  private static func NameOf(player: Uint32) -> String {
    let name = TandemNative.PlayerName(player);
    return StrLen(name) > 0 ? name : s"\(player)";
  }

  // ---- another player's breach, on what our game owns ----

  private func Apply(event: TandemBreachEvent) -> Void {
    let game = this.GetGameInstance();
    let failed = (event.flags & 2) != 0;
    let programs = TandemBreach.Runnable(TandemNative.BreachPrograms(event.index));
    let traps = TandemNative.BreachTraps(event.index);
    let executor = TandemNpcs.PuppetOf(game, event.player);
    this.m_breacher = event.player;
    if !IsDefined(executor) && !failed {
      // A breacher gone (or not in our world): the network stays breached
      // (the record's), its daemons run from no one here.
      TandemNative.Log(s"breach: player \(event.player)'s, no puppet of theirs here: daemons not run");
    }
    let count = 0;
    if EntityID.IsDefined(event.entity) {
      let point = GameInstance.GetPersistencySystem(game).GetConstAccessToPSObject(CreatePersistentID(event.entity, n"controller"), event.cls) as AccessPointControllerPS;
      if IsDefined(point) {
        count += this.OnNetwork(point, programs, traps, executor, failed, 0);
      } else {
        TandemNative.Log(s"breach: player \(event.player)'s network \(TandemNative.EntityHex(event.entity)) has no state in our world");
      }
    }
    if event.npc != 0u && TandemOwn.Npc(game, event.npc) {
      let npc = TandemHacks.NpcEntity(game, event.npc);
      if IsDefined(npc) && !npc.IsDead() {
        count += this.OnNpc(npc, programs, traps, executor, failed);
        // Host: the network the NPC is on (its device link, which the
        // breacher's proxy has not) is breached too, by that player.
        let ps = npc.GetPuppetPS();
        if this.m_host && !failed && !EntityID.IsDefined(event.entity) && IsDefined(ps) && ps.IsConnectedToAccessPoint() {
          let point = ps.GetAccessPoint();
          if IsDefined(point) && !point.IsNetworkBreached() && !PersistentID.IsDynamic(point.GetID()) {
            TandemNative.SendBreach(PersistentID.ExtractEntityID(point.GetID()), point.GetClassName(), 0u, TandemNative.BreachPrograms(event.index), traps, 1, event.player);
            let hex = TandemNative.EntityHex(PersistentID.ExtractEntityID(point.GetID()));
            TandemNative.Log(s"breach: npc \(event.npc) is on network \(hex): breached for player \(event.player)");
          }
        }
      }
    }
    this.m_applied += 1;
    let how = failed ? "lost" : "won";
    let network = EntityID.IsDefined(event.entity) ? TandemNative.EntityHex(event.entity) : "no network";
    let target = event.npc != 0u ? ", npc " + IntToString(Cast<Int32>(event.npc)) : "";
    TandemNative.Log(s"breach: player \(event.player)'s \(how) breach of \(network)\(target): \(count) run here (\(TandemBreach.Names(programs)))");
  }

  // A network: its devices (host) and the NPCs of it our game runs. Its
  // access points below it too, as RefreshSlaves reaches them.
  private func OnNetwork(point: ref<AccessPointControllerPS>, programs: array<TweakDBID>, traps: array<TweakDBID>, executor: ref<GameObject>, failed: Bool, depth: Int32) -> Int32 {
    let game = this.GetGameInstance();
    let count = 0;
    if this.m_host && !failed {
      // Breached, the game's way, and no jack-in offered again.
      GameInstance.GetPersistencySystem(game).QueuePSEvent(point.GetID(), point.GetClassName(), new BreachAccessPointEvent());
      point.TandemSetMinigameWon(true);
      count += this.OnDevice(point, programs, traps, executor);
    }
    for slave in point.GetImmediateSlaves() {
      let link = slave as PuppetDeviceLinkPS;
      let community = slave as CommunityProxyPS;
      let below = slave as AccessPointControllerPS;
      let device = slave as ScriptableDeviceComponentPS;
      if IsDefined(link) {
        count += this.OnNpcEntity(PersistentID.ExtractEntityID(link.GetID()), programs, traps, executor, failed);
      } else if IsDefined(community) {
        for id in community.ExtractEntityIDs() {
          count += this.OnNpcEntity(id, programs, traps, executor, failed);
        }
      } else if IsDefined(below) {
        if depth < 3 {
          count += this.OnNetwork(below, programs, traps, executor, failed, depth + 1);
        }
      } else if IsDefined(device) && this.m_host && !failed {
        if !device.IsQuickHacksExposed() {
          let expose = new SetExposeQuickHacks();
          expose.clearanceLevel = 2;
          expose.SetUp(device);
          expose.SetProperties();
          device.ExecutePSAction(expose, device);
        }
        count += this.OnDevice(device, programs, traps, executor);
      }
    }
    return count;
  }

  // ProcessMinigameNetworkActions, with the breacher's puppet for the
  // player: each daemon meant for this kind of device, as the device's own
  // minigame action (a camera's shutdown, a turret's friendly...).
  private func OnDevice(device: ref<ScriptableDeviceComponentPS>, programs: array<TweakDBID>, traps: array<TweakDBID>, executor: ref<GameObject>) -> Int32 {
    let game = this.GetGameInstance();
    let count = 0;
    let entity = PersistentID.ExtractEntityID(device.GetID());
    for trap in traps {
      if trap == t"MinigameTraps.IncreaseAwareness" {
        let sensor = GameInstance.FindEntityByID(game, entity) as SensorDevice;
        if IsDefined(sensor) {
          let detection = new SetDetectionMultiplier();
          detection.multiplier = 10.0;
          sensor.QueueEvent(detection);
        }
      }
    }
    if !IsDefined(executor) {
      return 0;
    }
    let context: GetActionsContext;
    context.requestType = gamedeviceRequestType.Remote;
    for program in programs {
      let record = TweakDBInterface.GetObjectActionRecord(program);
      let target = TweakDBInterface.GetCName(program + t".targetClass", n"None");
      if IsDefined(record) && (Equals(target, device.GetClassName()) || Equals(target, n"None")) {
        let action = device.GetMinigameActionByName(record.ActionName(), context) as ScriptableDeviceAction;
        if !IsDefined(action) {
          action = new PuppetAction();
          action.SetUp(device);
        }
        action.RegisterAsRequester(entity);
        action.SetExecutor(executor);
        action.SetObjectActionID(program);
        action.SetCanSkipPayCost(true);
        action.ProcessRPGAction(game);
        count += 1;
      }
    }
    return count;
  }

  // An NPC of the network, when it is a real one of our world.
  private func OnNpcEntity(id: EntityID, programs: array<TweakDBID>, traps: array<TweakDBID>, executor: ref<GameObject>, failed: Bool) -> Int32 {
    let game = this.GetGameInstance();
    if !EntityID.IsDefined(id) || TandemOwn.IsProxy(game, id) {
      return 0;
    }
    let npc = GameInstance.FindEntityByID(game, id) as ScriptedPuppet;
    if !IsDefined(npc) || npc.IsDead() || npc.IsPlayer() {
      return 0;
    }
    return this.OnNpc(npc, programs, traps, executor, failed);
  }

  // ScriptedPuppetPS.OnSetExposeQuickHacks, with the breacher's puppet for
  // the player: the daemons its character record lists, the squad trap.
  // A lost breach: it is alerted, as MinigameFailEvent does.
  private func OnNpc(npc: ref<ScriptedPuppet>, programs: array<TweakDBID>, traps: array<TweakDBID>, executor: ref<GameObject>, failed: Bool) -> Int32 {
    let game = this.GetGameInstance();
    if this.JustDone(npc.GetEntityID()) {
      return 0;
    }
    // Host: an actor of our V's scene, while it lasts, is left alone by a
    // guest's breach, won or lost, as by its quickhacks (Restrictions.reds,
    // the host's backstop).
    let story = TandemStorySystem.Get(game);
    if this.m_host && IsDefined(story) && IsDefined(story.Restrictions())
      && story.Restrictions().RefusesHack(npc, TandemHacks.NpcOf(game, npc.GetEntityID()), this.m_breacher, "breach") {
      return 0;
    }
    if failed {
      NPCStatesComponent.AlertPuppet(npc);
      return 1;
    }
    if !IsDefined(executor) {
      return 0;
    }
    let count = 0;
    for trap in traps {
      if trap == t"MinigameTraps.SquadBuff" {
        StatusEffectHelper.ApplyStatusEffect(npc, t"MinigameAction.BuffDamageReductionMinigame", executor.GetEntityID());
      }
    }
    let actions: array<wref<ObjectAction_Record>>;
    let character = TweakDBInterface.GetCharacterRecord(npc.GetRecordID());
    let ps = npc.GetPuppetPS();
    if !IsDefined(character) || !IsDefined(ps) {
      return 0;
    }
    character.ObjectActions(actions);
    for program in programs {
      for action in actions {
        if IsDefined(action) && action.GetID() == program {
          let run = new PuppetAction();
          run.RegisterAsRequester(npc.GetEntityID());
          run.SetExecutor(executor);
          run.SetObjectActionID(program);
          run.SetUp(ps);
          run.SetCanSkipPayCost(true);
          run.ProcessRPGAction(game);
          count += 1;
        }
      }
    }
    return count;
  }

  // An NPC and its network may be said one after the other: once.
  private func JustDone(id: EntityID) -> Bool {
    let i = ArraySize(this.m_npcs) - 1;
    while i >= 0 {
      if this.m_now - this.m_npcsAt[i] > 5.0 {
        ArrayErase(this.m_npcs, i);
        ArrayErase(this.m_npcsAt, i);
      } else if this.m_npcs[i] == id {
        return true;
      }
      i -= 1;
    }
    ArrayPush(this.m_npcs, id);
    ArrayPush(this.m_npcsAt, this.m_now);
    return false;
  }

  // ---- commands (tools/game.sh cmd breach ...) ----

  private static func ProgramsOf(words: array<String>, from: Int32) -> array<TweakDBID> {
    let programs: array<TweakDBID>;
    if ArraySize(words) > from {
      for name in StrSplit(words[from], ",") {
        ArrayPush(programs, TDBID.Create(StrContains(name, ".") ? name : s"MinigameAction.\(name)"));
      }
    }
    if ArraySize(programs) == 0 {
      ArrayPush(programs, t"MinigameAction.NetworkCameraShutdown");
      ArrayPush(programs, t"MinigameAction.NetworkLowerICEMedium");
      ArrayPush(programs, t"MinigameAction.NetworkDataMineLootAll");
    }
    return programs;
  }

  // What AccessPointSkip does: the programs on the blackboard, then the
  // minigame's outcome to what was breached.
  private static func Fake(game: GameInstance, target: ref<GameObject>, programs: array<TweakDBID>, won: Bool) -> Void {
    let board = TandemBreach.Blackboard(game);
    let traps: array<TweakDBID>;
    if IsDefined(board) {
      board.SetVariant(GetAllBlackboardDefs().HackingMinigame.ActivePrograms, ToVariant(programs));
      board.SetVariant(GetAllBlackboardDefs().HackingMinigame.ActiveTraps, ToVariant(traps));
    }
    let status = new AccessPointMiniGameStatus();
    status.minigameState = won ? HackingMinigameState.Succeeded : HackingMinigameState.Failed;
    target.QueueEvent(status);
  }

  public func Command(command: String, proxy: ref<ScriptedPuppet>) -> Void {
    let game = this.GetGameInstance();
    let words = StrSplit(command, " ");
    let verb = ArraySize(words) > 1 ? words[1] : "";
    if Equals(verb, "") {
      let role = this.m_host ? "host" : "guest";
      TandemNative.Log(s"breach: \(this.m_ours) of ours sent, \(this.m_applied) of others applied, \(this.m_refused) of ours refused (\(role))");
      return;
    }
    let fail = Equals(verb, "fail");
    let what = fail && ArraySize(words) > 2 ? words[2] : verb;
    if Equals(what, "ap") {
      let point = TandemDevices.Get(game).NearestOf("ap", 40.0);
      if !IsDefined(point) {
        TandemNative.Log("breach: no access point within 40 m");
        return;
      }
      let programs = TandemBreach.ProgramsOf(words, fail ? 3 : 2);
      let how = fail ? "lost" : "won";
      TandemNative.Log(s"breach: faking a \(how) breach of access point \(TandemNative.EntityHex(point.GetEntityID())) (\(TandemBreach.Names(programs)))");
      TandemBreach.Fake(game, point, programs, !fail);
    } else if Equals(what, "npc") {
      if !IsDefined(proxy) {
        TandemNative.Log("breach: no proxy near");
        return;
      }
      let programs = TandemBreach.ProgramsOf(words, fail ? 3 : 2);
      let how = fail ? "lost" : "won";
      TandemNative.Log(s"breach: faking a \(how) breach of npc \(TandemHacks.NpcOf(game, proxy.GetEntityID())) (\(TandemBreach.Names(programs)))");
      TandemBreach.Fake(game, proxy, programs, !fail);
    } else {
      TandemNative.Log(s"breach: unknown command \(command)");
    }
  }
}

// The game's way to say a minigame won is over for good on a device copy
// the session breached (the jack-in is offered only until it is won:
// ScriptableDeviceComponentPS.WasHackingMinigameSucceeded), and back when
// our own save's state is put back.
@addMethod(ScriptableDeviceComponentPS)
public func TandemSetMinigameWon(won: Bool) -> Void {
  if won {
    this.m_hackingMinigameState = HackingMinigameState.Succeeded;
  } else if Equals(this.m_hackingMinigameState, HackingMinigameState.Succeeded) {
    this.m_hackingMinigameState = HackingMinigameState.Unknown;
  }
}

// Every breach of a device or an access point ends here, from the
// minigame or from a mod that skips it.
@wrapMethod(Device)
protected cb func OnAccessPointMiniGameStatus(evt: ref<AccessPointMiniGameStatus>) -> Bool {
  TandemBreach.DeviceOutcome(this, evt.minigameState);
  return wrappedMethod(evt);
}

// ...and every breach of an NPC (the quickhack), a proxy's included.
@wrapMethod(ScriptedPuppet)
protected cb func OnAccessPointMiniGameStatus(evt: ref<AccessPointMiniGameStatus>) -> Bool {
  TandemBreach.NpcOutcome(this, evt.minigameState);
  return wrappedMethod(evt);
}
