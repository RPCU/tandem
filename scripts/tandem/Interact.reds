// Quest interactions in co-op (docs/architecture/story.md, "Quest
// interactions"; engine facts: docs/research/engine/story.md §13.65).
//
// The quest runs on the host, and a grouped guest's own story waits, so a
// guest's V using one of the host's objects (a quest switch, an objective
// item, a quest container, the terminal an objective needs) changes only
// the guest's own copy: the host's quest does not move. Here the guest's
// game asks the host's, and the host's game decides from its own data, the
// same rule for every object, with no list:
//
// - Whose object: a placed entity (its id is the same in every game) while
//   the guest is grouped with the host is the host's; anything else (our
//   own spawned things, or everything while apart: our own gigs) is ours,
//   and nothing here runs: our game does it as solo.
// - The host's game answers (TandemStory.AnswerInteract):
//   - local: its copy is nothing of its quest (GameObject.IsQuest, the
//     game's own quest mark; for loot, the item's Quest tag): the guest's
//     own copy did it, and the usual sync (devices, loot) carries it;
//   - host only: it is its quest's, and the choice would cast its doer in a
//     scene or a workspot (the object is in a scene now, or the device's
//     own callback for that action puts the doer in a workspot: a personal
//     link, a computer's engineering spot; read from the game's compiled
//     scripts, engine/casts.cpp): the guest's prompt shows greyed with
//     "NAME must do this", and it watches through scene replay;
//   - performed: its own V does it on its own copy with its own interaction
//     code, so quests, facts and loot move as if it had; the device record
//     names the guest as the doer (TandemDevices.PerformedFor);
//   - taken (another player's use came first), too far (lag compensated:
//     tandem/interact.hpp), missing (no such object or choice there now),
//     busy (its V is in a workspot or a lore animation).
// - What is known is kept for the session (both sides): a second ask is
//   answered at once, a known host-only prompt is greyed before it is
//   pressed (ScriptableDeviceComponentPS.FinalizeGetActions below).
// - Our own copy still does it at once, for feel (the press, the pick-up),
//   except a choice our own game says casts our V: that waits for the
//   host's word. A device's change is not said meanwhile (TandemDevices.Hold):
//   when the host performs it, the host's word is the device's state.
// - The safety valve: tandem.ini quest_interact_hostonly / _allow, TweakDB
//   records of objects. Empty by default, and should stay so.
//
// tools/game.sh cmd story interact (counts) | story interact look [device]
// (what our V looks at, or the nearest device: placed or not, quest-marked
// or not) | story interact use [device] (our V uses it: a device's first
// choice, the game's way; loot: the question about its first item).

public class TandemAsk {
  public let seq: Uint32;
  public let entity: EntityID;
  public let choice: CName;
  public let detail: Uint64;
  public let since: Float;
  // A device whose change waits (its state class), the action held back
  // when our own game says it casts our V (run here if it was ours).
  public let cls: CName;
  public let action: ref<DeviceAction>;
  public let what: String;
}

public class TandemInteract {
  private let m_game: GameInstance;
  private let m_story: wref<TandemStorySystem>;
  private let m_asks: array<ref<TandemAsk>>;
  // Guest: the layer of the choice our V is executing now; a held action we
  // run ourselves now (not asked again).
  private let m_layer: CName;
  private let m_passing: Bool;
  // Host: the guest whose use our V performs now.
  private let m_performingFor: Uint32;
  private let m_asked: Int32;
  private let m_answers: array<Int32>;
  private let m_judged: array<Int32>;

  public func Start(game: GameInstance, story: ref<TandemStorySystem>) -> Void {
    this.m_game = game;
    this.m_story = story;
    ArrayClear(this.m_asks);
    ArrayResize(this.m_answers, 8);
    ArrayResize(this.m_judged, 8);
  }

  public static func Of(game: GameInstance) -> ref<TandemInteract> {
    let story = TandemStorySystem.Get(game);
    return IsDefined(story) ? story.Interact() : null;
  }

  private func Now() -> Float {
    return EngineTime.ToFloat(GameInstance.GetSimTime(this.m_game));
  }

  private static func Verdict(verdict: Int32) -> String {
    switch verdict {
      case 1: return "performed";
      case 2: return "host only";
      case 3: return "local";
      case 4: return "taken";
      case 5: return "too far";
      case 6: return "missing";
      case 7: return "busy";
      default: return "?";
    }
  }

  // ---- guest ----

  // Ours to ask about: a guest in a session, grouped with the host, the
  // object placed in the world (the same entity in every game).
  private func Asks(id: EntityID) -> Bool {
    let story = this.m_story;
    return IsDefined(story) && story.IsGuest() && story.Grouped() && EntityID.IsStatic(id) && TandemStory.InteractOn();
  }

  private func OurV(executor: wref<GameObject>) -> Bool {
    let player = GetPlayer(this.m_game);
    return IsDefined(player) && IsDefined(executor) && executor.GetEntityID() == player.GetEntityID();
  }

  private func HostName() -> String {
    let story = this.m_story;
    return IsDefined(story) ? story.NameOf(story.HostPlayer()) : "";
  }

  private func Refused(verdict: Int32) -> Void {
    let name = this.HostName();
    switch verdict {
      case 2:
        TandemText.Notice(this.m_game, "Interact-HostOnly", name);
        break;
      case 4:
        TandemText.Notice(this.m_game, "Interact-Taken", name);
        break;
      case 5:
        TandemText.Notice(this.m_game, "Interact-TooFar", name);
        break;
      case 6:
        TandemText.Notice(this.m_game, "Interact-Missing", name);
        break;
      case 7:
        TandemText.Notice(this.m_game, "Interact-Busy", name);
        break;
      default:
        break;
    }
  }

  // The layer of the choice being executed (Device.ExecuteAction's choice
  // form, below), for the question.
  public func ChoiceLayer(layer: CName) -> Void {
    this.m_layer = layer;
  }

  private func Ask(entity: EntityID, choice: CName, detail: Uint64, flags: Int32, what: String) -> ref<TandemAsk> {
    let player = GetPlayer(this.m_game);
    let at = IsDefined(player) ? player.GetWorldPosition() : new Vector4(0.0, 0.0, 0.0, 1.0);
    let ask = new TandemAsk();
    ask.seq = TandemStory.SendInteract(entity, choice, detail, this.m_layer, flags, at.X, at.Y, at.Z);
    ask.entity = entity;
    ask.choice = choice;
    ask.detail = detail;
    ask.since = this.Now();
    ask.what = what;
    ArrayPush(this.m_asks, ask);
    this.m_asked += 1;
    TandemNative.Log(s"interact: asked the host about \(what) (question \(ask.seq))");
    return ask;
  }

  // Device.ExecuteAction(action, executor): true lets our own copy do it now.
  public func OnDeviceAction(device: ref<Device>, action: ref<DeviceAction>, executor: wref<GameObject>) -> Bool {
    if this.m_passing || !IsDefined(device) || !this.Asks(device.GetEntityID()) || !this.OurV(executor) {
      return true;
    }
    let sAction = action as ScriptableDeviceAction;
    // A quickhack has its own path (Hacks.reds).
    if !IsDefined(sAction) || Equals(sAction.GetRequestType(), gamedeviceRequestType.Remote) {
      return true;
    }
    let ps = device.GetDevicePS();
    if !IsDefined(ps) {
      return true;
    }
    let id = device.GetEntityID();
    let choice = sAction.GetClassName();
    let detail = TandemStory.NameKey(sAction.GetActionName());
    let what = s"\(NameToString(choice)) (\(NameToString(sAction.GetActionName()))) on \(NameToString(device.GetClassName())) \(EntityID.ToDebugString(id))";
    let override = TandemStory.InteractOverride(device.GetTweakDBRecord());
    let known = TandemStory.InteractKnown(id, choice, detail);
    if known == 2 || override == 1 {
      TandemNative.Log(s"interact: \(what) is the host's to do");
      this.Refused(2);
      return false;
    }
    if known == 3 {
      return true;
    }
    // Would it cast our V (our own game reads the same code as the host's)?
    // Unknown counts as yes: the host is asked before anything moves.
    let casts = override != 2 && TandemStory.ActionCasts(device.GetClassName(), ps.GetClassName(), choice) != 0;
    let ask = this.Ask(id, choice, detail, 1 + (casts ? 2 : 0), what);
    if casts {
      ask.action = action;
      return false;
    }
    // Ours does it now; what it becomes waits for the host's word.
    ask.cls = ps.GetClassName();
    TandemDevices.Get(this.m_game).Hold(id, ask.cls, 4.0);
    return true;
  }

  // Inventory.OnInteractionUsed: an item taken from a placed container or
  // lying in the world (the game takes it itself: we only ask, for the
  // host's copy, when it is a quest prop).
  public func OnLoot(owner: ref<GameObject>, evt: ref<InteractionChoiceEvent>) -> Void {
    if !IsDefined(owner) || !IsDefined(evt) || !this.Asks(owner.GetEntityID()) || !this.OurV(evt.activator) {
      return;
    }
    let wrapper = LootChoiceActionWrapper.Unwrap(evt);
    if !LootChoiceActionWrapper.IsValid(wrapper) {
      return;
    }
    let data = RPGManager.GetItemData(this.m_game, owner, wrapper.itemId);
    if !TandemLoot.QuestProp(data) {
      return; // per-player loot: nothing of anyone's quest
    }
    let record = ItemID.GetTDBID(wrapper.itemId);
    let detail = TDBID.ToNumber(record);
    let known = TandemStory.InteractKnown(owner.GetEntityID(), wrapper.action, detail);
    if known == 3 || known == 2 {
      return;
    }
    this.m_layer = evt.layerData.tag;
    this.Ask(owner.GetEntityID(), wrapper.action, detail, 1 + 4, s"\(NameToString(wrapper.action)) \(TDBID.ToStringDEBUG(record)) from \(NameToString(owner.GetClassName())) \(EntityID.ToDebugString(owner.GetEntityID()))");
    this.m_layer = n"";
  }

  // The host's verdict on one of our questions.
  private func OnAnswer(e: TandemStoryEvent) -> Void {
    let i = ArraySize(this.m_asks) - 1;
    let ask: ref<TandemAsk>;
    while i >= 0 {
      if this.m_asks[i].seq == e.hash {
        ask = this.m_asks[i];
        ArrayErase(this.m_asks, i);
        break;
      }
      i -= 1;
    }
    if e.state >= 0 && e.state < ArraySize(this.m_answers) {
      this.m_answers[e.state] += 1;
    }
    if !IsDefined(ask) {
      return; // waited too long for it: ours already
    }
    TandemNative.Log(s"interact: the host: \(ask.what) \(TandemInteract.Verdict(e.state))");
    this.Settle(ask, e.state);
  }

  // verdict 0: no answer in time (the host is gone, or its game silent).
  private func Settle(ask: ref<TandemAsk>, verdict: Int32) -> Void {
    let performed = verdict == 1;
    if IsNameValid(ask.cls) {
      // Ours after all (local, someone else's first, nothing there, no
      // answer): said as our V's doing. Else the host's word is the device's.
      let ours = verdict == 3 || verdict == 4 || verdict == 6 || verdict == 0;
      TandemDevices.Get(this.m_game).Release(ask.entity, ask.cls, ours);
    }
    if IsDefined(ask.action) && (verdict == 3 || verdict == 0) {
      // Ours after all (nothing of the host's quest): our own copy does it.
      let device = GameInstance.FindEntityByID(this.m_game, ask.entity) as Device;
      let player = GetPlayer(this.m_game);
      if IsDefined(device) && IsDefined(player) {
        this.m_passing = true;
        device.TandemExecute(ask.action, player);
        this.m_passing = false;
      }
    }
    if !performed && verdict != 3 && verdict != 0 {
      this.Refused(verdict);
    }
  }

  // ---- host ----

  // A guest's question: judged by our game, done by our V or not, answered.
  private func OnAsked(e: TandemStoryEvent) -> Void {
    let verdict = this.Judge(e);
    if verdict >= 0 && verdict < ArraySize(this.m_judged) {
      this.m_judged[verdict] += 1;
    }
    TandemStory.AnswerInteract(e.player, e.hash, verdict);
  }

  private func Judge(e: TandemStoryEvent) -> Int32 {
    let object = GameInstance.FindEntityByID(this.m_game, e.entity) as GameObject;
    let player = GetPlayer(this.m_game);
    let what = s"\(NameToString(e.choice)) on \(EntityID.ToDebugString(e.entity))";
    if !IsDefined(object) || !IsDefined(player) {
      TandemNative.Log(s"interact: player \(e.player)'s \(what): not in our world");
      return 6;
    }
    what = s"\(NameToString(e.choice)) on \(NameToString(object.GetClassName())) \(EntityID.ToDebugString(e.entity))";
    if !TandemStory.InteractInReach(e.player, e.hash, object.GetWorldPosition().X, object.GetWorldPosition().Y, object.GetWorldPosition().Z) {
      return 5;
    }
    let loot = (e.flags & 4) != 0;
    let device = object as Device;
    let record = IsDefined(device) ? device.GetTweakDBRecord() : TDBID.None();
    let override = TandemStory.InteractOverride(record);
    // Nothing of our quest there: the guest's own copy is the whole of it.
    // The game's own marks: the object's (GameObject.IsQuest), its master's
    // (a terminal that commands it), a fact that ends the mark.
    let quest = loot ? this.LootIsQuest(object, e.detail) : object.IsQuest();
    if !quest && IsDefined(device) && IsDefined(device.GetDevicePS()) {
      let ps = device.GetDevicePS();
      quest = ps.IsAnyMasterFlaggedAsQuest() || IsNameValid(ps.GetFactToDisableQuestMarkName());
    }
    if !quest && override == 0 {
      TandemNative.Log(s"interact: player \(e.player)'s \(what): nothing of our quest");
      return 3;
    }
    // In a scene now, or it would put the doer in a workspot: ours to do.
    let scene = GameInstance.GetSceneSystem(this.m_game).GetScriptInterface().IsEntityInScene(e.entity);
    let casts = false;
    if IsDefined(device) && !loot {
      casts = TandemStory.ActionCasts(device.GetClassName(), device.GetDevicePS().GetClassName(), e.choice) != 0;
    }
    if override == 1 || (override != 2 && (scene || casts)) {
      TandemNative.Log(s"interact: player \(e.player)'s \(what): ours to do (\(scene ? "in a scene" : "it casts its doer"))");
      return 2;
    }
    if this.Busy(player) {
      return 7;
    }
    let done = loot ? this.PerformLoot(object, e.detail, player) : this.PerformDevice(device, e, player);
    if !done {
      TandemNative.Log(s"interact: player \(e.player)'s \(what): no such choice here now");
      return 6;
    }
    TandemNative.Log(s"interact: player \(e.player)'s \(what): done by our V");
    return 1;
  }

  // Our V cannot act for anyone now: in a workspot, or a lore animation.
  private func Busy(player: ref<PlayerPuppet>) -> Bool {
    if GameInstance.GetWorkspotSystem(this.m_game).IsActorInWorkspot(player) {
      return true;
    }
    let psm = GameInstance.GetBlackboardSystem(this.m_game).GetLocalInstanced(player.GetEntityID(), GetAllBlackboardDefs().PlayerStateMachine);
    return IsDefined(psm) && psm.GetBool(GetAllBlackboardDefs().PlayerStateMachine.IsInLoreAnimationScene);
  }

  private func LootIsQuest(object: ref<GameObject>, detail: Uint64) -> Bool {
    let item = this.FindItem(object, detail);
    return ItemID.IsValid(item) && TandemLoot.QuestProp(RPGManager.GetItemData(this.m_game, object, item));
  }

  // The item of that record in an object's inventory (a container, or the
  // item lying there).
  private func FindItem(object: ref<GameObject>, detail: Uint64) -> ItemID {
    let items: array<wref<gameItemData>>;
    if !GameInstance.GetTransactionSystem(this.m_game).GetItemList(object, items) {
      return ItemID.None();
    }
    for data in items {
      if IsDefined(data) && TDBID.ToNumber(ItemID.GetTDBID(data.GetID())) == detail {
        return data.GetID();
      }
    }
    return ItemID.None();
  }

  // Our V takes the guest's item out of our copy (loot is per player: only
  // a quest prop comes here; Loot.reds shares it, and takes a second copy
  // back out).
  private func PerformLoot(object: ref<GameObject>, detail: Uint64, player: ref<PlayerPuppet>) -> Bool {
    let drop = object as gameItemDropObject;
    if IsDefined(drop) && IsDefined(drop.GetItemObject()) {
      return GameInstance.GetTransactionSystem(this.m_game).TakeItem(player, drop.GetItemObject());
    }
    let item = this.FindItem(object, detail);
    if !ItemID.IsValid(item) {
      return false;
    }
    let quantity = GameInstance.GetTransactionSystem(this.m_game).GetItemQuantity(object, item);
    return GameInstance.GetTransactionSystem(this.m_game).TransferItem(object, player, item, Max(quantity, 1));
  }

  // Our V does the same action on our device: found among the device's own
  // actions for our V now, by its class and name, and executed the game's
  // way. The guest's own game passed its checks (a skill, an item its V
  // holds): the guest is the doer, so ours are not asked again.
  private func PerformDevice(device: ref<Device>, e: TandemStoryEvent, player: ref<PlayerPuppet>) -> Bool {
    if !IsDefined(device) {
      return false;
    }
    let ps = device.GetDevicePS();
    let action = this.FindAction(ps, e.choice, e.detail, player);
    if !IsDefined(action) {
      return false;
    }
    TandemDevices.Get(this.m_game).PerformedFor(device.GetEntityID(), ps.GetClassName(), e.player);
    this.m_performingFor = e.player;
    device.TandemExecute(action, player);
    this.m_performingFor = 0u;
    return true;
  }

  private func FindAction(ps: ref<ScriptableDeviceComponentPS>, choice: CName, detail: Uint64, player: ref<PlayerPuppet>) -> ref<DeviceAction> {
    if !IsDefined(ps) {
      return null;
    }
    let kinds = [gamedeviceRequestType.Direct, gamedeviceRequestType.External, gamedeviceRequestType.Internal];
    for kind in kinds {
      let actions: array<ref<DeviceAction>>;
      ps.GetActions(actions, ps.GenerateContext(kind, Device.GetInteractionClearance(), player));
      for action in actions {
        let sAction = action as ScriptableDeviceAction;
        if IsDefined(sAction) && Equals(sAction.GetClassName(), choice) && TandemStory.NameKey(sAction.GetActionName()) == detail {
          return action;
        }
      }
    }
    return null;
  }

  // Host: the guest our V acts for now (0: none), for the other systems.
  public func PerformingFor() -> Uint32 {
    return this.m_performingFor;
  }

  // ---- both ----

  public func OnEvent(e: TandemStoryEvent) -> Void {
    if e.kind == 60 {
      this.OnAsked(e);
    } else if e.kind == 61 {
      this.OnAnswer(e);
    }
  }

  // A few times a second: questions with no answer in time are ours.
  public func Tick() -> Void {
    let now = this.Now();
    let i = ArraySize(this.m_asks) - 1;
    while i >= 0 {
      let ask = this.m_asks[i];
      if now - ask.since > 3.0 {
        ArrayErase(this.m_asks, i);
        TandemNative.Log(s"interact: no answer about \(ask.what): ours, as solo");
        this.Settle(ask, 0);
      }
      i -= 1;
    }
  }

  // The session ended: what waits is ours.
  public func End() -> Void {
    for ask in this.m_asks {
      this.Settle(ask, 0);
    }
    ArrayClear(this.m_asks);
  }

  // Guest: is this action of this device known to be the host's to do?
  public func KnownHostOnly(entity: EntityID, action: ref<ScriptableDeviceAction>) -> Bool {
    if !this.Asks(entity) || !IsDefined(action) {
      return false;
    }
    return TandemStory.InteractKnown(entity, action.GetClassName(), TandemStory.NameKey(action.GetActionName())) == 2;
  }

  public func GreyReason() -> String {
    return TandemText.F("Interact-HostOnly", this.HostName());
  }

  public func Command(command: String) -> Void {
    if Equals(command, "story interact") {
      TandemNative.Log(s"interact: \(this.m_asked) asked, \(ArraySize(this.m_asks)) waiting; answers performed \(this.m_answers[1]), host only \(this.m_answers[2]), local \(this.m_answers[3]), taken \(this.m_answers[4]), too far \(this.m_answers[5]), missing \(this.m_answers[6]), busy \(this.m_answers[7]); judged performed \(this.m_judged[1]), host only \(this.m_judged[2]), local \(this.m_judged[3]), missing \(this.m_judged[6]), too far \(this.m_judged[5]), busy \(this.m_judged[7])");
    } else if StrBeginsWith(command, "story interact look") {
      this.Near(true, StrAfterFirst(command, "look "));
    } else if StrBeginsWith(command, "story interact use") {
      let kind = StrAfterFirst(command, "use ");
      this.Near(false, StrLen(kind) > 0 ? kind : "any");
    } else {
      TandemNative.Log(s"unknown command: \(command)");
    }
  }

  // The object our V looks at, or (device) the nearest followed device
  // within 6 m: what it is (look), or our V uses it (use: a device's first
  // choice, through the game's own ExecuteAction and so this path; loot:
  // the question for its first item, as if it were a quest prop, no item
  // taken).
  private func Near(list: Bool, kind: String) -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let object: ref<GameObject>;
    if Equals(kind, "device") {
      object = TandemDevices.Get(this.m_game).NearestOf("any", 6.0);
    } else {
      object = GameInstance.GetTargetingSystem(this.m_game).GetLookAtObject(player, false, false);
    }
    if !IsDefined(object) {
      TandemNative.Log("interact look: nothing there");
      return;
    }
    let id = object.GetEntityID();
    let placed = EntityID.IsStatic(id);
    TandemNative.Log(s"interact look: \(NameToString(object.GetClassName())) \(EntityID.ToDebugString(id)), placed \(placed), quest \(object.IsQuest()), \(Vector4.Distance(object.GetWorldPosition(), player.GetWorldPosition())) m");
    if list {
      return;
    }
    let device = object as Device;
    if IsDefined(device) {
      let action = this.FindFirst(device.GetDevicePS(), player);
      if IsDefined(action) {
        TandemNative.Log(s"interact use: \(NameToString(action.GetClassName())) on \(NameToString(device.GetClassName()))");
        device.TandemExecute(action, player);
      } else {
        TandemNative.Log("interact use: the device offers our V nothing now");
      }
      return;
    }
    let items: array<wref<gameItemData>>;
    GameInstance.GetTransactionSystem(this.m_game).GetItemList(object, items);
    if ArraySize(items) == 0 || !IsDefined(items[0]) {
      TandemNative.Log("interact use: nothing to take there");
      return;
    }
    if this.Asks(id) {
      let record = ItemID.GetTDBID(items[0].GetID());
      this.Ask(id, n"Loot", TDBID.ToNumber(record), 1 + 4, s"Loot \(TDBID.ToStringDEBUG(record)) from \(NameToString(object.GetClassName())) (test)");
    } else {
      TandemNative.Log("interact use: not the host's object (apart, alone, or not placed): ours, as solo");
    }
  }

  private func FindFirst(ps: ref<ScriptableDeviceComponentPS>, player: ref<PlayerPuppet>) -> ref<DeviceAction> {
    if !IsDefined(ps) {
      return null;
    }
    let actions: array<ref<DeviceAction>>;
    ps.GetActions(actions, ps.GenerateContext(gamedeviceRequestType.Direct, Device.GetInteractionClearance(), player));
    return ArraySize(actions) > 0 ? actions[0] : null;
  }
}

// A device action by our V, the way an interaction runs it (the host's V
// for a guest, our V for a test or a held action that was ours).
@addMethod(Device)
public func TandemExecute(action: ref<DeviceAction>, executor: ref<GameObject>) -> Bool {
  return this.ExecuteAction(action, executor);
}

// Every device action our V does from an interaction or a screen goes
// through here (Device.OnUIAction too). final.reds:98590.
@wrapMethod(Device)
protected const final func ExecuteAction(action: ref<DeviceAction>, opt executor: wref<GameObject>) -> Bool {
  let interact = TandemInteract.Of(this.GetGame());
  if IsDefined(interact) && !interact.OnDeviceAction(this, action, executor) {
    return false;
  }
  return wrappedMethod(action, executor);
}

// The choice form remembers the layer it came on. final.reds:98567.
@wrapMethod(Device)
protected const final func ExecuteAction(const choice: script_ref<InteractionChoice>, executor: wref<GameObject>, layerTag: CName) -> Void {
  let interact = TandemInteract.Of(this.GetGame());
  if IsDefined(interact) {
    interact.ChoiceLayer(layerTag);
  }
  wrappedMethod(choice, executor, layerTag);
  if IsDefined(interact) {
    interact.ChoiceLayer(n"");
  }
}

// An item taken from a placed container or the world. final.reds:69278.
@wrapMethod(Inventory)
protected cb final func OnInteractionUsed(evt: ref<InteractionChoiceEvent>) -> Bool {
  let owner = this.GetEntity() as GameObject;
  if IsDefined(owner) {
    let interact = TandemInteract.Of(owner.GetGame());
    if IsDefined(interact) {
      interact.OnLoot(owner, evt);
    }
  }
  return wrappedMethod(evt);
}

// A prompt the host must press shows greyed, with why. final.reds:30290.
@wrapMethod(ScriptableDeviceComponentPS)
protected final func FinalizeGetActions(out outActions: array<ref<DeviceAction>>) -> Void {
  wrappedMethod(outActions);
  let interact = TandemInteract.Of(this.GetGameInstance());
  if !IsDefined(interact) {
    return;
  }
  let entity = PersistentID.ExtractEntityID(this.GetID());
  for action in outActions {
    let sAction = action as ScriptableDeviceAction;
    if interact.KnownHostOnly(entity, sAction) {
      sAction.SetInactiveWithReason(false, interact.GreyReason());
    }
  }
}
