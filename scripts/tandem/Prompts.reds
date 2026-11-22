// The prompts on another player's body ("Trade", "Revive") and on a
// partner's vendor, as the game's own interactions: a choice on the body's
// interaction component (ScriptedPuppet's `Interaction`, the one that
// carries an NPC's Talk, Loot and Grapple), on its ReturnTalk layer (else
// GenericTalk), put there with InteractionComponent.SetChoices as the layer
// activates; the key comes back as the game's InteractionChoiceEvent.
// Range, the look-at test, line of sight, no prompt from a seat, which
// prompt wins on screen and the key's binding are then the game's
// (docs/architecture/prompts.md; engine-knowledge §13.153).
//
// Who offers what stays with its feature (Revive.reds, Trade.reds,
// Vendors.reds): each calls OfferOn every tick its own rules allow, and
// draws the prompt itself (the UIInteractions blackboard, as before) when
// OfferOn says no. A body goes back to drawn prompts by itself when the
// game's interaction does not prove to work on it: one log line says which
// way each body goes, and why.
//
// Seen in game on instance b (2026-11-22): "[F] Trade" on a puppet.

public abstract class TandemPromptKind {
  // In order of priority on one body: a downed player's revive comes
  // before a trade.
  public static func Revive() -> Int32 { return 1; }
  public static func Trade() -> Int32 { return 2; }
  public static func Vendor() -> Int32 { return 3; }

  public static func PromptWord(kind: Int32) -> String {
    if kind == 1 {
      return "revive";
    }
    if kind == 2 {
      return "trade";
    }
    return kind == 3 ? "vendor" : "none";
  }

  // The choice's record, Interactions.<this> (tweaks/tandem/prompts.tweak).
  public static func ChoiceRecord(kind: Int32) -> String {
    if kind == 1 {
      return "TandemRevive";
    }
    if kind == 2 {
      return "TandemTrade";
    }
    return kind == 3 ? "TandemVendor" : "";
  }

  // The Text.reds key of the prompt: the caption of the kind's record
  // (tweaks/tandem/prompts.tweak, "LocKey#Tandem-<key>").
  public static func TextKey(kind: Int32) -> String {
    if kind == 1 {
      return "Revive-Prompt";
    }
    if kind == 2 {
      return "Trade-Prompt";
    }
    return kind == 3 ? "Vendor-Trade" : "";
  }

  // The kind a choice of the game's event is (0: not ours).
  public static func OfChoice(record: String, id: TweakDBID) -> Int32 {
    if Equals(record, "TandemRevive") || id == t"Interactions.TandemRevive" {
      return 1;
    }
    if Equals(record, "TandemTrade") || id == t"Interactions.TandemTrade" {
      return 2;
    }
    if Equals(record, "TandemVendor") || id == t"Interactions.TandemVendor" {
      return 3;
    }
    return 0;
  }
}

// One body we were asked to put a prompt on.
public class TandemPromptBody {
  public let entity: EntityID;
  public let body: wref<ScriptedPuppet>;
  // Who it is, for the log.
  public let label: String;
  // Another player's puppet (else an NPC's proxy).
  public let partner: Bool;
  // The caller draws its prompts (why: the reason); else the game's own
  // interaction carries them.
  public let drawn: Bool;
  public let why: String;
  // What was offered on it this tick (the tick's number).
  public let offerTick: Int32;
  public let offerKind: Int32;
  public let offerId: Uint32;
  public let offerText: String;
  // What it carries now (kind 0: nothing).
  public let kind: Int32;
  public let id: Uint32;
  public let text: String;
  // The layer of its component our choices go on (TandemPrompts.LayerOf).
  public let layer: CName;
  public let layerOn: Bool;
  public let layerSince: Float;
  // Our V activates the layer (in its range, looking at the body).
  public let active: Bool;
  public let activeSince: Float;
  // The game's own prompt on screen holds our choice.
  public let shown: Bool;
  public let shownSince: Float;
  // Our V stands right in front, looking at it, and the game shows nothing
  // of ours (-1: not so).
  public let facedSince: Float;
  public let said: Bool;
  public let activations: Int32;
  public let shows: Int32;
  public let presses: Int32;
}

// The key on one of our choices, from the game's choice event to the tick.
public class TandemPromptPress extends DelayCallback {
  public let game: GameInstance;
  public let entity: EntityID;
  public let kind: Int32;
  public let action: gameinputActionType;
  public let layer: CName;

  public func Call() -> Void {
    TandemNative.HookRan(n"Prompts.TandemPromptPress.Call");
    let prompts = TandemPrompts.Of(this.game);
    if IsDefined(prompts) {
      prompts.OnPress(this.entity, this.kind, this.action, this.layer);
    }
  }
}

public class TandemPrompts {
  private let m_game: GameInstance;
  private let m_system: wref<TandemSystem>;
  private let m_now: Float;
  private let m_tick: Int32;
  // Every prompt is drawn (tandem.ini native_prompts = off, "prompts off",
  // or a record of ours is missing), and why.
  private let m_off: Bool;
  private let m_offWhy: String;
  private let m_bodies: array<ref<TandemPromptBody>>;
  // Presses nobody took yet: the kind, whom, when.
  private let m_pressKinds: array<Int32>;
  private let m_pressIds: array<Uint32>;
  private let m_pressAt: array<Float>;
  // The interaction key as our input listener sees it (Revive.reds): a
  // press no choice event followed is taken by hand (-1: none waits).
  private let m_keyHeld: Bool;
  private let m_keyDownAt: Float;
  private let m_keyUpAt: Float;
  // The last choice event of ours, and the last one "prompts press" made.
  private let m_eventAt: Float;
  private let m_testAt: Float;
  // The layer our choices go on when a body's component has it (LayerOf):
  // an NPC's ReturnTalk, a talk layer whose choices vanilla pushes from the
  // record's actions only (ours have none: tweaks/tandem/puppet.tweak).
  // Never Grapple or a takedown layer: vanilla answers any choice on those
  // with a takedown (ProcessSyncedAnimationPuppetActions). (test) "prompts
  // layer NAME".
  private let m_layer: CName;
  // tandem.ini native_prompts = off: no body was given a component, and
  // none of the puppets' layers is touched.
  private let m_untouched: Bool;
  private let m_lastSweep: Float;
  private let m_lastQuiet: Float;

  public static func Of(game: GameInstance) -> ref<TandemPrompts> {
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.Prompts() : null;
  }

  public func Start(game: GameInstance, system: ref<TandemSystem>) -> Void {
    this.m_game = game;
    this.m_system = system;
    this.m_keyDownAt = -1.0;
    this.m_keyUpAt = -1000.0;
    this.m_eventAt = -1000.0;
    this.m_testAt = -1000.0;
    this.m_lastSweep = -1000.0;
    this.m_lastQuiet = -1000.0;
    if !IsNameValid(this.m_layer) {
      this.m_layer = n"ReturnTalk";
    }
    this.m_untouched = false;
    this.m_off = false;
    this.m_offWhy = "";
    let actions = "";
    let missing = "";
    for kind in [1, 2, 3] {
      let name = TandemPromptKind.ChoiceRecord(kind);
      let record = TweakDBInterface.GetInteractionBaseRecord(TDBID.Create("Interactions." + name));
      if IsDefined(record) {
        actions += s" \(name) (\(record.Action()))";
      } else {
        missing += s" Interactions.\(name)";
      }
    }
    if Equals(TandemNative.Setting("native_prompts"), "off") {
      this.m_off = true;
      this.m_untouched = true;
      this.m_offWhy = "tandem.ini native_prompts = off";
    } else if StrLen(missing) > 0 {
      this.m_off = true;
      this.m_offWhy = s"no record\(missing): tweaks/tandem/prompts.tweak is not loaded";
    }
    if this.m_off {
      TandemNative.Log(s"prompts: drawn by us, none is the game's own interaction (\(this.m_offWhy))");
      if this.m_untouched { TandemDegraded.Chosen("drawn prompts", this.m_offWhy); } else { TandemDegraded.State("drawn prompts", this.m_offWhy); }
    } else {
      TandemNative.Log(s"prompts: the game's own interactions, layer \(NameToString(this.m_layer)), choices\(actions); each body proves them or falls back to the drawn ones");
      TandemDegraded.Over("drawn prompts");
    }
  }

  public func Stop() -> Void {
    for b in this.m_bodies {
      if IsDefined(b.body) && b.kind != 0 {
        this.Withdraw(b, "the session is over");
      }
    }
    ArrayClear(this.m_bodies);
    ArrayClear(this.m_pressKinds);
    ArrayClear(this.m_pressIds);
    ArrayClear(this.m_pressAt);
  }

  // ---- for the features (every tick their rules allow the prompt) ----

  // `kind`'s prompt on `target` (a puppet or a proxy), for player or NPC
  // `id`. True: the game's own interaction shows it and its key comes
  // through TakePress; false: the caller draws it, as before.
  public func OfferOn(kind: Int32, id: Uint32, target: ref<GameObject>, partner: Bool, label: String, text: String) -> Bool {
    let body = target as ScriptedPuppet;
    if !IsDefined(body) || kind == 0 {
      return false;
    }
    let b = this.Find(body.GetEntityID());
    if !IsDefined(b) {
      b = this.Adopt(body, partner, label);
    }
    if b.drawn {
      return false;
    }
    b.label = label;
    // One choice per body, the first kind in priority.
    if b.offerTick != this.m_tick || kind < b.offerKind {
      b.offerTick = this.m_tick;
      b.offerKind = kind;
      b.offerId = id;
      b.offerText = text;
    }
    return true;
  }

  // Whose `kind` prompt the key was pressed on since the last call (0: none).
  public func TakePress(kind: Int32) -> Uint32 {
    let i = 0;
    while i < ArraySize(this.m_pressKinds) {
      if this.m_pressKinds[i] == kind {
        let id = this.m_pressIds[i];
        ArrayErase(this.m_pressKinds, i);
        ArrayErase(this.m_pressIds, i);
        ArrayErase(this.m_pressAt, i);
        return id;
      }
      i += 1;
    }
    return 0u;
  }

  // The interaction key, as our own input listener sees it (Revive.reds'
  // OnAction: the one listener of the three that is always there).
  public func KeyIs(down: Bool) -> Void {
    this.m_keyHeld = down;
    if down {
      this.m_keyDownAt = this.m_now;
    } else {
      this.m_keyUpAt = this.m_now;
    }
  }

  public func KeyHeld() -> Bool {
    return this.m_keyHeld;
  }

  // Seconds since the key was let go (large: never).
  public func KeyUpFor() -> Float {
    return this.m_now - this.m_keyUpAt;
  }

  // The press just taken was "prompts press"'s: no key is held.
  public func TestPress() -> Bool {
    return this.m_now - this.m_testAt <= 1.0;
  }

  // A feature found the game's interaction wanting on that body: drawn
  // prompts from now on.
  public func Distrust(target: ref<GameObject>, why: String) -> Void {
    if !IsDefined(target) {
      return;
    }
    let b = this.Find(target.GetEntityID());
    if IsDefined(b) && !b.drawn {
      this.Fallback(b, why);
    }
  }

  // ---- every frame in a session, after the features (TandemSystem.OnTick) ----

  public func Tick(now: Float) -> Void {
    this.m_now = now;
    let tick = this.m_tick;
    this.m_tick += 1;
    let player = GetPlayer(this.m_game);
    // A press nobody took within a second is nobody's any more.
    let p = ArraySize(this.m_pressAt) - 1;
    while p >= 0 {
      if now - this.m_pressAt[p] > 1.0 {
        ArrayErase(this.m_pressKinds, p);
        ArrayErase(this.m_pressIds, p);
        ArrayErase(this.m_pressAt, p);
      }
      p -= 1;
    }
    // Every player's puppet, whether or not a prompt was ever offered on
    // it: which way its prompts go is said as it comes, and nothing of an
    // NPC's own shows on it.
    if now - this.m_lastSweep >= 1.0 || now < this.m_lastSweep {
      this.m_lastSweep = now;
      this.Sweep();
    }
    let hub: InteractionChoiceHubData;
    let read = false;
    let i = ArraySize(this.m_bodies) - 1;
    while i >= 0 {
      let b = this.m_bodies[i];
      if !IsDefined(b.body) {
        // Its entity is gone (a body built again is another entity).
        ArrayErase(this.m_bodies, i);
      } else if !b.drawn {
        if b.offerTick == tick && IsDefined(player) {
          if !read {
            hub = this.Hub();
            read = true;
          }
          this.Carry(player, b, hub);
        } else if b.kind != 0 {
          this.Withdraw(b, "no longer offered");
        }
      }
      i -= 1;
    }
    // The key went down on the game's prompt holding our choice and no
    // choice event came: the press is taken by hand, and that body's
    // prompts are drawn from now on.
    if this.m_keyDownAt >= 0.0 && now - this.m_keyDownAt >= 0.5 {
      let since = this.m_keyDownAt;
      this.m_keyDownAt = -1.0;
      for b in this.m_bodies {
        if now - this.m_eventAt > 1.0 && IsDefined(b.body) && !b.drawn && b.kind != 0 && b.shown && b.shownSince <= since {
          this.Pressed(b);
          this.Fallback(b, "the key was pressed on the game's prompt and no choice event came within 0.5 s (the press taken by hand)");
          return;
        }
      }
    }
  }

  // The game's choice event on one of our choices (TandemPromptPress).
  public func OnPress(entity: EntityID, kind: Int32, action: gameinputActionType, layer: CName) -> Void {
    let b = this.Find(entity);
    let how = s"\(action), layer \(NameToString(layer))";
    if !IsDefined(b) || b.drawn || b.kind != kind {
      TandemNative.Log(s"prompts: a \(TandemPromptKind.PromptWord(kind)) choice event (\(how)) on \(IsDefined(b) ? b.label : TandemNative.EntityHex(entity)), which does not carry that prompt now: dropped");
      return;
    }
    if NotEquals(action, gameinputActionType.BUTTON_PRESSED) && NotEquals(action, gameinputActionType.BUTTON_HOLD_COMPLETE) {
      TandemNative.LogDebug(s"prompts: \(TandemPromptKind.PromptWord(kind)) choice event on \(b.label) (\(how)): not a press");
      return;
    }
    this.m_keyDownAt = -1.0;
    this.m_eventAt = this.m_now;
    b.presses += 1;
    TandemNative.Log(s"prompts: \(TandemPromptKind.PromptWord(kind)) pressed on \(b.label): the game's choice event (\(how))");
    if b.presses == 1 {
      TandemNative.Log(s"prompts: \(b.label): the game's own interaction works (activated \(b.activations), shown \(b.shows), pressed)");
    }
    this.Pressed(b);
    // Vanilla's handler of the press resets the body's choices
    // (ScriptedPuppet.OnInteractionUsed): the layer off for a tick and on
    // again brings ours back with its next activation.
    if b.layerOn {
      b.body.EnableInteraction(b.layer, false);
      b.layerOn = false;
      b.active = false;
      b.shown = false;
    }
  }

  private func Pressed(b: ref<TandemPromptBody>) -> Void {
    ArrayPush(this.m_pressKinds, b.kind);
    ArrayPush(this.m_pressIds, b.id);
    ArrayPush(this.m_pressAt, this.m_now);
  }

  // ---- one body ----

  private func Find(entity: EntityID) -> ref<TandemPromptBody> {
    for b in this.m_bodies {
      if b.entity == entity {
        return b;
      }
    }
    return null;
  }

  // The puppets not met yet; and every 5 s each puppet's NPC layers off
  // again (vanilla turns the takedown layers back on when an NPC gets up,
  // ScriptedPuppet.OnResurrected).
  private func Sweep() -> Void {
    if !IsDefined(this.m_system) {
      return;
    }
    for other in this.m_system.PuppetPlayers() {
      let body = this.m_system.PuppetOf(other) as ScriptedPuppet;
      if IsDefined(body) && !IsDefined(this.Find(body.GetEntityID())) {
        let name = TandemNative.PlayerName(other);
        this.Adopt(body, true, StrLen(name) > 0 ? name : s"player \(other)");
      }
    }
    if this.m_now - this.m_lastQuiet >= 5.0 || this.m_now < this.m_lastQuiet {
      this.m_lastQuiet = this.m_now;
      for b in this.m_bodies {
        if b.partner && IsDefined(b.body) {
          this.Quiet(b);
        }
      }
    }
  }

  // Nothing of an NPC's own on a player: no grapple, takedown, finisher,
  // loot or talk prompt (a body with a grafted component gets every NPC
  // layer with it). Ours stays as it is while it carries a prompt.
  private func Quiet(b: ref<TandemPromptBody>) -> Void {
    if this.m_untouched || !IsDefined(b.body.TandemInteraction()) {
      return;
    }
    for layer in [n"Grapple", n"TakedownLayer", n"AerialTakedown", n"NewPerkFinisherLayer", n"BossTakedownLayer", n"MassiveTargetTakedownLayer", n"RemoteTakedownLayer", n"OnePunchAttackLayer", n"Loot", n"GenericTalk", n"ReturnTalk"] {
      if !(b.layerOn && Equals(layer, b.layer)) {
        b.body.EnableInteraction(layer, false);
      }
    }
  }

  // The first prompt asked on a body: which way its prompts go.
  private func Adopt(body: ref<ScriptedPuppet>, partner: Bool, label: String) -> ref<TandemPromptBody> {
    let b = new TandemPromptBody();
    b.entity = body.GetEntityID();
    b.body = body;
    b.label = label;
    b.partner = partner;
    b.offerTick = -1;
    b.facedSince = -1.0;
    if this.m_off {
      b.drawn = true;
      b.why = this.m_offWhy;
    } else if !IsDefined(body.TandemInteraction()) {
      b.drawn = true;
      b.why = "its body has no Interaction component";
      TandemDegraded.Fallback("prompt drawn", s"\(label): its body has no Interaction component");
    } else {
      b.layer = TandemPrompts.LayerOf(body.TandemInteraction(), this.m_layer);
    }
    let who = s"\(label) (\(partner ? "a player's puppet" : "an NPC's proxy"), entity \(TandemNative.EntityHex(b.entity)))";
    if b.drawn {
      TandemNative.Log(s"prompts: \(who): drawn prompts (\(b.why))");
    } else {
      TandemNative.Log(s"prompts: \(who): the game's own interaction (its Interaction component, layer \(NameToString(b.layer))), not yet seen working");
    }
    if partner {
      this.Quiet(b);
    }
    ArrayPush(this.m_bodies, b);
    return b;
  }

  // This tick's offer, on the game's interaction.
  private func Carry(player: ref<PlayerPuppet>, b: ref<TandemPromptBody>, hub: InteractionChoiceHubData) -> Void {
    let now = this.m_now;
    let changed = b.kind != b.offerKind || b.id != b.offerId || NotEquals(b.text, b.offerText);
    if changed {
      if b.kind != b.offerKind || b.id != b.offerId {
        TandemNative.Log(s"prompts: \(TandemPromptKind.PromptWord(b.offerKind)) offered on \(b.label) through the game's interaction");
      }
      let kindChanged = b.kind != b.offerKind;
      b.kind = b.offerKind;
      b.id = b.offerId;
      b.text = b.offerText;
      b.shown = false;
      // Where the body's activation handler puts our choice (the wrap of
      // ScriptedPuppet.OnInteractionActivated below).
      b.body.tandemPromptLayer = b.layer;
      if kindChanged && b.layerOn {
        // Its layer is on (maybe active, a prompt up): off for a tick and on
        // again, so that it activates anew with our choice.
        b.body.EnableInteraction(b.layer, false);
        b.layerOn = false;
        b.active = false;
        TandemNative.LogDebug(s"prompts: \(b.label): layer off and on again for the \(TandemPromptKind.PromptWord(b.kind)) choice");
        return;
      }
    }
    // What it puts there.
    b.body.tandemPromptKind = b.kind;
    if !b.layerOn {
      b.body.EnableInteraction(b.layer, true);
      b.layerOn = true;
      b.layerSince = now;
    }
    let active = this.ActiveFor(player, b);
    if active && !b.active {
      b.active = true;
      b.activeSince = now;
      b.said = false;
      b.facedSince = -1.0;
      b.activations += 1;
      if b.activations == 1 {
        TandemNative.Log(s"prompts: \(b.label): its layer is active for our V (the game's interaction scanner sees the body)");
      } else {
        TandemNative.LogDebug(s"prompts: \(b.label): layer active (#\(b.activations))");
      }
    } else if !active && b.active {
      b.active = false;
      b.shown = false;
      b.facedSince = -1.0;
      TandemNative.LogDebug(s"prompts: \(b.label): layer no longer active");
    }
    if !active {
      // Our V right in front of it, looking at it, the layer on for 2 s,
      // and the game never once activated it: not a layer of this body,
      // or not a body the scanner sees.
      if b.activations == 0 && now - b.layerSince >= 2.0 && this.Faces(player, b) {
        if b.facedSince < 0.0 {
          b.facedSince = now;
        } else if now - b.facedSince >= 2.0 {
          this.Fallback(b, "its layer never activated with our V in front of it, looking at it for 2 s");
        }
      } else {
        b.facedSince = -1.0;
      }
      return;
    }
    let shown = !changed && TandemPrompts.Holds(hub, b.text);
    if shown && !b.shown {
      b.shows += 1;
      b.shownSince = now;
      b.facedSince = -1.0;
      TandemNative.Log(s"prompts: \(TandemPromptKind.PromptWord(b.kind)) shown on \(b.label) by the game (\(TandemPrompts.HubWords(hub)))");
    } else if !shown && b.shown {
      TandemNative.LogDebug(s"prompts: \(TandemPromptKind.PromptWord(b.kind)) on \(b.label) no longer on screen (\(TandemPrompts.HubWords(hub)))");
    }
    b.shown = shown;
    if shown {
      return;
    }
    // Not on screen. Our choice went on the layer as it activated (the wrap
    // of ScriptedPuppet.OnInteractionActivated below): one set later never
    // brings a prompt up (2026-11-22, instance b).
    if now - b.activeSince < 1.5 {
      return;
    }
    // Still not shown. A body the game never showed a choice of
    // ours on, while no prompt we draw is up, goes back to drawn prompts:
    // when it is the game's own look-at target and the game's prompt holds
    // something else,
    let judged = b.shows == 0 && !(hub.active && TandemPrompts.IsDrawn(hub.id));
    if judged && GameInstance.GetInteractionManager(this.m_game).IsInteractionLookAtTarget(player, b.body) {
      this.Fallback(b, s"it is the game's interaction target, our choice is set and the game shows \(TandemPrompts.HubWords(hub))");
      return;
    }
    // or when there is no prompt at all with our V right in front of it
    // for 3 s.
    if judged && !hub.active && this.Faces(player, b) {
      if b.facedSince < 0.0 {
        b.facedSince = now;
      } else if now - b.facedSince >= 3.0 {
        this.Fallback(b, "its layer is active, our choice is set and the game shows no prompt with our V in front of it for 3 s");
      }
      return;
    }
    b.facedSince = -1.0;
    if !b.said {
      // Another prompt comes first (the game's priority), or the body is
      // not what our V looks at: the game's call.
      b.said = true;
      TandemNative.Log(s"prompts: \(b.label): our \(TandemPromptKind.PromptWord(b.kind)) choice is set and the game shows \(TandemPrompts.HubWords(hub))");
    }
  }

  // Our choice, alone on its layer: the kind's record, whose caption is
  // the prompt's text (a key of Text.reds, tweaks/tandem/prompts.tweak), and
  // the text again as the choice's own caption, which the choice event
  // carries back.
  public static func ChoicesOf(kind: Int32, text: String) -> array<InteractionChoice> {
    let record = TandemPromptKind.ChoiceRecord(kind);
    let choice: InteractionChoice;
    choice.caption = text;
    choice.choiceMetaData.tweakDBName = record;
    choice.choiceMetaData.tweakDBID = TDBID.Create("Interactions." + record);
    let choices: array<InteractionChoice>;
    ArrayPush(choices, choice);
    return choices;
  }

  // The layer of a body's component our choices go on: `preferred` when
  // its interaction descriptor has it, else GenericTalk (every NPC's).
  // ReturnTalk is preferred: GenericTalk is the game's crowd talk, which
  // deactivates it every ~1.2 s and activates it again a second later (a
  // prompt that blinks; 2026-11-22, instance b), ReturnTalk stays active.
  public static func LayerOf(component: ref<InteractionComponent>, preferred: CName) -> CName {
    let descriptor = ResourceRef.GetResource(component.definitionResource) as gameinteractionsInteractionDescriptorResource;
    if IsDefined(descriptor) {
      let linked = descriptor.definition.layersDefinition;
      for set in linked {
        if Equals(set.tag, preferred) {
          return preferred;
        }
      }
    }
    return n"GenericTalk";
  }

  // Nothing of ours on its layer, and the layer off (no "Talk" of the
  // game's own on a player or on another game's NPC meanwhile).
  private func Withdraw(b: ref<TandemPromptBody>, why: String) -> Void {
    if IsDefined(b.body) {
      b.body.tandemPromptKind = 0;
    }
    let component = b.body.TandemInteraction();
    if IsDefined(component) {
      component.ResetChoices(b.layer);
    }
    if b.layerOn {
      b.body.EnableInteraction(b.layer, false);
      b.layerOn = false;
    }
    if b.kind != 0 {
      TandemNative.LogDebug(s"prompts: \(TandemPromptKind.PromptWord(b.kind)) withdrawn from \(b.label) (\(why))");
    }
    b.kind = 0;
    b.id = 0u;
    b.text = "";
    b.active = false;
    b.shown = false;
    b.facedSince = -1.0;
  }

  private func Fallback(b: ref<TandemPromptBody>, why: String) -> Void {
    this.Withdraw(b, "back to drawn prompts");
    b.drawn = true;
    b.why = why;
    TandemNative.Log(s"prompts: \(b.label) (entity \(TandemNative.EntityHex(b.entity))): back to drawn prompts: \(why)");
    TandemDegraded.Fallback("prompt drawn", s"\(b.label): \(why)");
  }

  // Our V is an activator of the body's layer, by the component's own lists.
  private func ActiveFor(player: ref<PlayerPuppet>, b: ref<TandemPromptBody>) -> Bool {
    let component = b.body.TandemInteraction();
    if !IsDefined(component) {
      return false;
    }
    let ours = player.GetEntityID();
    let layers: array<gameinteractionsActiveLayerData>;
    component.GetActivatorsForLayer(b.layer, layers);
    for layer in layers {
      if IsDefined(layer.activator) && layer.activator.GetEntityID() == ours {
        return true;
      }
    }
    let inputs: array<gameinteractionsActiveLayerData>;
    component.GetActiveInputLayers(inputs);
    for layer in inputs {
      if Equals(layer.layerName, b.layer) && IsDefined(layer.activator) && layer.activator.GetEntityID() == ours {
        return true;
      }
    }
    return false;
  }

  // Our V on foot within 1.3 m, its view within 20 degrees of the body
  // (its chest; a downed one lies): where any prompt of the game shows.
  // Only to tell a dead layer from one the game keeps off.
  private func Faces(player: ref<PlayerPuppet>, b: ref<TandemPromptBody>) -> Bool {
    if GameInstance.GetMountingFacility(this.m_game).IsMountedToAnything(player.GetEntityID()) {
      return false;
    }
    let eye = player.GetWorldPosition();
    let aim = b.body.GetWorldPosition();
    let flat = SqrtF((aim.X - eye.X) * (aim.X - eye.X) + (aim.Y - eye.Y) * (aim.Y - eye.Y));
    if flat > 1.3 || AbsF(aim.Z - eye.Z) > 1.0 {
      return false;
    }
    eye.Z += 1.6;
    aim.Z += b.kind == TandemPromptKind.Revive() ? 0.3 : 1.2;
    let to = Vector4.Normalize(aim - eye);
    let view = Vector4.Normalize(GameInstance.GetCameraSystem(this.m_game).GetActiveCameraForward());
    return Vector4.Dot(view, to) >= 0.94;
  }

  // The game's interaction prompt, as its UI is told.
  private func Hub() -> InteractionChoiceHubData {
    let hub: InteractionChoiceHubData;
    let board = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UIInteractions);
    if IsDefined(board) {
      hub = FromVariant<InteractionChoiceHubData>(board.GetVariant(GetAllBlackboardDefs().UIInteractions.InteractionChoiceHub));
    }
    return hub;
  }

  // The game's prompt holds a choice with our text. Never one of the drawn
  // prompts' hubs (Revive.reds, Trade.reds, Vendors.reds write the same
  // texts there under their own ids).
  private static func Holds(hub: InteractionChoiceHubData, text: String) -> Bool {
    if !hub.active || StrLen(text) == 0 || TandemPrompts.IsDrawn(hub.id) {
      return false;
    }
    for choice in hub.choices {
      if Equals(GetLocalizedText(choice.localizedName), text) || StrContains(choice.localizedName, text) {
        return true;
      }
    }
    return false;
  }

  private static func IsDrawn(hub: Int32) -> Bool {
    return hub == TandemRevive.HubId() || hub == TandemTradeTable.HubId() || hub == TandemVendors.HubId();
  }

  // (logs) What the game's prompt holds.
  private static func HubWords(hub: InteractionChoiceHubData) -> String {
    if !hub.active {
      return "no prompt";
    }
    let held = "";
    for choice in hub.choices {
      held += s"\(StrLen(held) > 0 ? ", " : "")'\(choice.localizedName)' key \(NameToString(choice.inputAction)) hold \(choice.isHoldAction)";
    }
    return s"hub \(hub.id)\(TandemPrompts.IsDrawn(hub.id) ? " (drawn by us)" : ""): \(StrLen(held) > 0 ? held : "no choice")";
  }

  // ---- tests (tools/game.sh cmd prompts ..., cmd test query prompt ...) ----

  // "game|drawn STATE layer on|off active B shown B choice KIND
  // activations N shows N presses N": which way that body's prompts go and
  // how far the game's interaction has proven itself on it (unproven,
  // activated, shown, pressed); "none": no prompt was ever asked on it.
  public func StatusLine(target: ref<GameObject>) -> String {
    if !IsDefined(target) {
      return "none";
    }
    let b = this.Find(target.GetEntityID());
    if !IsDefined(b) {
      return "none";
    }
    let state = "unproven";
    if b.drawn {
      state = "drawn";
    } else if b.presses > 0 {
      state = "pressed";
    } else if b.shows > 0 {
      state = "shown";
    } else if b.activations > 0 {
      state = "activated";
    }
    return s"\(b.drawn ? "drawn" : "game") \(state) layer \(b.layerOn ? "on" : "off") active \(b.active) shown \(b.shown) choice \(TandemPromptKind.PromptWord(b.kind)) activations \(b.activations) shows \(b.shows) presses \(b.presses)";
  }

  // prompts            every body a prompt was asked on, and the game's prompt now
  // prompts off | on   all drawn, as tandem.ini native_prompts = off | the
  //                    game's own again (each body adopted again)
  // prompts face [N]   our V turned to player N's puppet (the first one)
  //                    where it stands when the puppet is within 2 m, else
  //                    put 1.2 m in front of it, facing it
  // prompts layer NAME (test) our choices on another layer of the bodies'
  //                    components (GenericTalk: the default), every body
  //                    adopted again; never Grapple or a takedown layer
  // prompts press      (test) a choice event queued on the body that
  //                    carries a choice, as the game sends one: the way
  //                    from the event to the feature, without the key
  public func Command(command: String) -> Void {
    let words = StrSplit(command, " ");
    let verb = ArraySize(words) > 1 ? words[1] : "status";
    let player = GetPlayer(this.m_game);
    if Equals(verb, "off") || Equals(verb, "on") {
      for b in this.m_bodies {
        if IsDefined(b.body) && !b.drawn {
          this.Withdraw(b, "prompts " + verb);
        }
      }
      ArrayClear(this.m_bodies);
      if Equals(verb, "off") {
        this.m_off = true;
        this.m_offWhy = "prompts off";
        TandemDegraded.State("drawn prompts", "prompts off (the test command)");
      } else {
        this.Start(this.m_game, this.m_system);
      }
      TandemNative.Log(s"prompts: \(this.m_off ? s"drawn by us (\(this.m_offWhy))" : "the game's own interactions"), every body adopted again");
    } else if Equals(verb, "layer") {
      let name = ArraySize(words) > 2 ? StringToName(words[2]) : n"GenericTalk";
      if Equals(name, n"Grapple") || StrContains(NameToString(name), "Takedown") || StrContains(NameToString(name), "Finisher") {
        TandemNative.Log(s"prompts layer: not \(NameToString(name)) (vanilla answers a choice there with a takedown)");
        return;
      }
      for b in this.m_bodies {
        if IsDefined(b.body) && !b.drawn {
          this.Withdraw(b, "prompts layer");
        }
      }
      ArrayClear(this.m_bodies);
      this.m_layer = name;
      this.m_lastSweep = -1000.0;
      TandemNative.Log(s"prompts layer: our choices go on layer \(NameToString(name)), every body adopted again");
    } else if Equals(verb, "face") {
      let who = ArraySize(words) > 2 ? Cast<Uint32>(StringToInt(words[2], 0)) : 0u;
      if who == 0u {
        for other in this.m_system.PuppetPlayers() {
          if who == 0u {
            who = other;
          }
        }
      }
      let puppet = this.m_system.PuppetOf(who);
      if !IsDefined(puppet) || !IsDefined(player) {
        TandemNative.Log("prompts face: no puppet");
        return;
      }
      let at = puppet.GetWorldPosition();
      let to = player.GetWorldPosition();
      if Vector4.Distance(at, to) > 2.0 {
        let ahead = puppet.GetWorldForward();
        ahead.Z = 0.0;
        to = at + Vector4.Normalize(ahead) * 1.2;
      }
      to.W = 1.0;
      let toward = at - to;
      toward.Z = 0.0;
      GameInstance.GetTeleportationFacility(this.m_game).Teleport(player, to, Vector4.ToRotation(toward));
      TandemNative.Log(s"prompts face: our V \(Vector4.Distance(at, to)) m from player \(who)'s puppet, facing it");
    } else if Equals(verb, "press") {
      let best: ref<TandemPromptBody>;
      for b in this.m_bodies {
        if IsDefined(b.body) && !b.drawn && b.kind != 0 && (!IsDefined(best) || (b.active && !best.active)) {
          best = b;
        }
      }
      if !IsDefined(best) || !IsDefined(player) {
        TandemNative.Log("prompts press: no body carries a choice of ours on the game's interaction");
        return;
      }
      let record = TandemPromptKind.ChoiceRecord(best.kind);
      let event = new InteractionChoiceEvent();
      event.hotspot = best.body;
      event.activator = player;
      event.layerData.tag = best.layer;
      event.choice.caption = best.text;
      event.choice.choiceMetaData.tweakDBName = record;
      event.choice.choiceMetaData.tweakDBID = TDBID.Create("Interactions." + record);
      event.actionType = gameinputActionType.BUTTON_PRESSED;
      best.body.QueueEvent(event);
      this.m_testAt = this.m_now;
      TandemNative.Log(s"prompts press: (test) a \(TandemPromptKind.PromptWord(best.kind)) choice event queued on \(best.label) (layer active \(best.active), shown \(best.shown))");
    } else {
      let hub = this.Hub();
      TandemNative.Log(s"prompts: \(this.m_off ? s"drawn by us (\(this.m_offWhy))" : "the game's own interactions"), \(ArraySize(this.m_bodies)) bodies; the game's prompt now: \(TandemPrompts.HubWords(hub)); key \(this.m_keyHeld ? "held" : "up")");
      let board = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UIInteractions);
      if IsDefined(board) {
        let dialogs = FromVariant<DialogChoiceHubs>(board.GetVariant(GetAllBlackboardDefs().UIInteractions.DialogChoiceHubs));
        for d in dialogs.choiceHubs {
          let names = "";
          for c in d.choices {
            names += s" '\(c.localizedName)'";
          }
          TandemNative.Log(s"prompts:   dialog hub \(d.id) '\(d.title)' state \(d.activityState):\(names)");
        }
      }
      for b in this.m_bodies {
        TandemNative.Log(s"prompts:   \(b.label) entity \(TandemNative.EntityHex(b.entity)): \(this.StatusLine(b.body))\(b.drawn ? s" (\(b.why))" : "")");
      }
    }
  }
}

// The body's interaction component (ScriptedPuppet.OnTakeControl: its
// component named Interaction; null when the template has none).
@addMethod(ScriptedPuppet)
public func TandemInteraction() -> ref<InteractionComponent> {
  return this.m_interactionComponent;
}

// What the body's activation handler puts on its layer: the kind of our
// choice (0: none) and the layer, written on the game thread's tick
// (TandemPrompts.Carry, Withdraw).
@addField(ScriptedPuppet)
public let tandemPromptKind: Int32;

@addField(ScriptedPuppet)
public let tandemPromptLayer: CName;

// The game's prompt is built from the choices a layer holds as it
// activates: a choice set later shows only in place of one already up
// (2026-11-22, instance b). Vanilla's own handler pushes the record's
// choices here (DetermineInteractionState → PushChoicesToInteractionComponent:
// a reset, then SetChoices); ours replaces them in the same event, so the
// prompt comes up with it, as an NPC's "Talk" does.
// job thread safe: it reads two fields of its own entity and calls its own
// interaction component, as the wrapped handler itself does.
@wrapMethod(ScriptedPuppet)
protected cb func OnInteractionActivated(evt: ref<InteractionActivationEvent>) -> Bool {
  let result = wrappedMethod(evt);
  let kind = this.tandemPromptKind;
  if kind != 0 && Equals(evt.eventType, gameinteractionsEInteractionEventType.EIET_activate) && Equals(evt.layerData.tag, this.tandemPromptLayer) && IsDefined(evt.activator as PlayerPuppet) && IsDefined(this.m_interactionComponent) {
    let text = TandemText.Get(TandemPromptKind.TextKey(kind));
    this.m_interactionComponent.SetChoices(TandemPrompts.ChoicesOf(kind, text), this.tandemPromptLayer);
  }
  return result;
}

// The key on a choice of ours, as the game's own choice event
// (final.reds (2.50):22615; vanilla's handler finds no action in our choice
// and resets the component's choices). Only read here: the press goes to
// the tick through the delay system, as a callback of its own.
@wrapMethod(ScriptedPuppet)
protected cb func OnInteractionUsed(evt: ref<InteractionChoiceEvent>) -> Bool {
  let kind = TandemPromptKind.OfChoice(evt.choice.choiceMetaData.tweakDBName, evt.choice.choiceMetaData.tweakDBID);
  if kind != 0 {
    let press = new TandemPromptPress();
    press.game = this.GetGame();
    press.entity = this.GetEntityID();
    press.kind = kind;
    press.action = evt.actionType;
    press.layer = evt.layerData.tag;
    GameInstance.GetDelaySystem(this.GetGame()).DelayCallback(press, 0.0, false);
  }
  return wrappedMethod(evt);
}
