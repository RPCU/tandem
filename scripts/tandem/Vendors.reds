// Vendors in co-op (docs/architecture/progression.md, "Vendors"; engine
// facts: docs/research/engine/ui.md §13.80): shops, ripperdocs and
// clothing stores trade with a partner as in single player, with its own
// stock, prices, street cred gates and money.
//
// A vendor is an NPC placed in the world, so it stands in every game with
// the same static entity id; the game keeps one vendor per entity in its
// own save (MarketSystem). The NPC is the world's, run by one game: the
// others show it as a proxy, a body with its brain off and no shop of its
// own. So:
//
// - the game that runs a vendor says so with its entity (TandemProgress.
//   SendVendor, from Npcs.reds as it announces the NPC);
// - beside a vendor's proxy, our V gets the game's interaction prompt
//   ("Trade"); a press opens OUR OWN game's vendor of that entity (the copy
//   our own world placed there, hidden behind the proxy: Npcs.reds), the
//   game's own screen on it (UISystem.RequestVendorMenu, as the drop point
//   does): our save's stock and prices, our money, our street cred. A
//   ripperdoc is the same vendor, its screen the ripperdoc's;
// - where our own world has no such vendor (our story has not put it
//   there) or its access rules refuse our V, the shop stays shut, with a
//   notice (core/vendors.hpp Decide says which);
// - a vendor killed in the session is dead in every game: the host says
//   so, and our own copy is hidden until the session ends;
// - a proxy never gets a vendor of its own: the game would keep one in our
//   save under the proxy's dynamic id at every spawn (the wraps at the end).
//
// Vending machines and drop points are devices of our own world: the
// game's own, nothing to do here (their state is Devices.reds').
//
// tools/game.sh cmd vendor | vendor near | vendor goto | vendor open |
// vendor close

public class TandemVendors {
  private let m_game: GameInstance;
  private let m_system: wref<TandemSystem>;
  private let m_now: Float;
  private let m_you: Uint32;
  // The prompt: the NPC it offers (0: none), its body, and our answer.
  private let m_target: Uint32;
  private let m_body: wref<ScriptedPuppet>;
  private let m_shop: Int32;
  private let m_savedVisualizers: VisualizersInfo;
  private let m_listening: Bool;
  private let m_lastScan: Float;
  // Our own vendors the session says died: hidden, watched (a body that
  // streams in again is a new one).
  private let m_buried: array<EntityID>;
  private let m_buriedBodies: array<wref<GameObject>>;
  private let m_lastWatch: Float;
  // The shop we opened last: on which entity, when, how many so far.
  private let m_opened: EntityID;
  private let m_openedAt: Float;
  private let m_opens: Int32;
  // The vendor's screen, while it is up (VendorHubMenuGameController).
  private let m_screen: wref<VendorHubMenuGameController>;

  public static func Of(game: GameInstance) -> ref<TandemVendors> {
    let system = TandemSystem.Of(game);
    return IsDefined(system) ? system.Vendors() : null;
  }

  public static func HubId() -> Int32 {
    return -7703;
  }

  public func Start(game: GameInstance, system: ref<TandemSystem>) -> Void {
    this.m_game = game;
    this.m_system = system;
    // The last session's vendors are not this one's.
    TandemProgress.VendorsReset();
  }

  public func End() -> Void {
    this.HidePrompt();
    if this.m_listening {
      let player = GetPlayer(this.m_game);
      if IsDefined(player) {
        player.UnregisterInputListener(this, n"Choice1");
      }
      this.m_listening = false;
    }
    // What we hid stays hidden until it streams out (as every NPC of ours
    // hidden in a session: Npcs.reds); a save never holds it.
    ArrayClear(this.m_buried);
    ArrayClear(this.m_buriedBodies);
    TandemProgress.VendorsReset();
  }

  // ---- the owner's side (Npcs.reds, as it announces an NPC) ----

  // One of our NPCs, just announced: a vendor placed in the world says its
  // entity (a spawned NPC of a vendor record is nobody's shop).
  public static func Announce(npc: Uint32, body: ref<ScriptedPuppet>) -> Void {
    let id = body.GetEntityID();
    if body.IsVendor() && EntityID.IsStatic(id) {
      TandemProgress.SendVendor(npc, id);
    }
  }

  // A new NPC of ours that is the copy of a vendor the session says died.
  public static func IsBuried(body: ref<ScriptedPuppet>) -> Bool {
    let id = body.GetEntityID();
    return EntityID.IsStatic(id) && TandemProgress.VendorDead(id) && body.IsVendor();
  }

  // A body of ours that is a copy of another game's NPC (a proxy), never a
  // shop of its own (the MarketSystem wraps below).
  public static func IsCopy(owner: wref<GameObject>) -> Bool {
    if !IsDefined(owner) {
      return false;
    }
    let id = owner.GetEntityID();
    if !EntityID.IsDynamic(id) {
      return false;
    }
    if GameInstance.GetDynamicEntitySystem().IsTagged(id, n"Tandem.Proxy") {
      return true;
    }
    let system = TandemSystem.Of(owner.GetGame());
    let npcs = IsDefined(system) ? system.Npcs() : null;
    return IsDefined(npcs) && npcs.IsProxyEntity(id);
  }

  // ---- every frame in a session (TandemSystem.OnTick) ----

  public func Tick(now: Float, you: Uint32) -> Void {
    this.m_now = now;
    this.m_you = you;
    let player = GetPlayer(this.m_game);
    if you == 0u || !IsDefined(player) {
      this.HidePrompt();
      return;
    }
    if !this.m_listening {
      player.RegisterInputListener(this, n"Choice1");
      this.m_listening = true;
    }
    for hash in TandemProgress.TakeDeadVendors() {
      this.Bury(EntityID.FromHash(hash));
    }
    if now - this.m_lastWatch >= 1.0 {
      this.m_lastWatch = now;
      this.Watch();
    }
    if now - this.m_lastScan >= 0.1 {
      this.m_lastScan = now;
      this.Prompt(player);
    }
  }

  // ---- the prompt ----

  // Another prompt of ours comes first: a downed partner's revive, a trade.
  private func Yields() -> Bool {
    let revive = this.m_system.Revive();
    if IsDefined(revive) && revive.Prompting() {
      return true;
    }
    let trade = this.m_system.Trade();
    return IsDefined(trade) && trade.Prompting();
  }

  // Our V may trade now: up, not fighting, not in a scene that holds it,
  // not at a trade's table, in its own body.
  private func Free(player: ref<PlayerPuppet>) -> Bool {
    if player.IsInCombat() || PlayerPuppet.GetSceneTier(player) > 2 || TandemElsewhere.AwayFromBody(this.m_game) {
      return false;
    }
    if TandemTrade.TradeState() != 0 {
      return false;
    }
    let revive = this.m_system.Revive();
    return !IsDefined(revive) || !revive.IsDown();
  }

  // The vendor beside our V, in front of it: a proxy (another game runs the
  // NPC), or one we took over (its body is ours now, a spawned copy).
  private func Nearest(player: ref<PlayerPuppet>, reach: Float, out npc: Uint32) -> ref<ScriptedPuppet> {
    npc = 0u;
    let npcs = this.m_system.Npcs();
    if !IsDefined(npcs) {
      return null;
    }
    let best: ref<ScriptedPuppet>;
    let bestAt = reach;
    let here = player.GetWorldPosition();
    let ahead = player.GetWorldForward();
    for proxy in npcs.Proxies() {
      let body = npcs.ProxyBody(proxy.id) as ScriptedPuppet;
      if !proxy.dead && IsDefined(body) && EntityID.IsDefined(TandemProgress.VendorOf(proxy.npc)) {
        let far = Vector4.Distance(here, body.GetWorldPosition());
        if far <= bestAt && TandemVendors.InFront(here, ahead, body.GetWorldPosition(), far) {
          best = body;
          bestAt = far;
          npc = proxy.npc;
        }
      }
    }
    for known in npcs.Known() {
      if known.managed && !known.dead && EntityID.IsDefined(TandemProgress.VendorOf(known.npc)) {
        let body = npcs.ProxyBody(known.id) as ScriptedPuppet;
        if IsDefined(body) {
          let far = Vector4.Distance(here, body.GetWorldPosition());
          if far <= bestAt && TandemVendors.InFront(here, ahead, body.GetWorldPosition(), far) {
            best = body;
            bestAt = far;
            npc = known.npc;
          }
        }
      }
    }
    return best;
  }

  private static func InFront(here: Vector4, ahead: Vector4, there: Vector4, far: Float) -> Bool {
    if far < 1.0 {
      return true;
    }
    let to = there - here;
    to.Z = 0.0;
    ahead.Z = 0.0;
    return Vector4.Dot(Vector4.Normalize(to), Vector4.Normalize(ahead)) > 0.3;
  }

  // Our own world's copy of the vendor a body stands for (null: none here).
  private func OwnOf(npc: Uint32) -> ref<ScriptedPuppet> {
    let entity = TandemProgress.VendorOf(npc);
    if !EntityID.IsDefined(entity) {
      return null;
    }
    return GameInstance.FindEntityByID(this.m_game, entity) as ScriptedPuppet;
  }

  // May our V trade with that vendor now (vendors::Shop, core/vendors.hpp).
  private func Decide(player: ref<PlayerPuppet>, npc: Uint32, body: ref<ScriptedPuppet>) -> Int32 {
    let entity = TandemProgress.VendorOf(npc);
    let own = this.OwnOf(npc);
    let ownFound = IsDefined(own);
    let ownVendor = ownFound && own.IsVendor();
    let ownAlive = ownFound && !own.IsDead() && !ScriptedPuppet.IsDefeated(own);
    let accessible = ownVendor && MarketSystem.IsAccessible(player, MarketSystem.GetVendorID(own));
    let far = Vector4.Distance(player.GetWorldPosition(), body.GetWorldPosition());
    return TandemProgress.VendorShop(body.IsVendor(), entity, far, body.IsDead(), TandemProgress.VendorDead(entity), this.Free(player), ownFound, ownVendor, ownAlive, accessible);
  }

  // The game's interaction prompt on the vendor beside us: [F] Trade, its
  // name above. Shown whatever the answer: a press says why it is shut.
  private func Prompt(player: ref<PlayerPuppet>) -> Void {
    let npc = 0u;
    let body = this.Yields() ? null : this.Nearest(player, TandemProgress.VendorReach(), npc);
    let shop = IsDefined(body) ? this.Decide(player, npc, body) : 3;
    if !IsDefined(body) || !TandemProgress.VendorPrompts(shop) {
      this.HidePrompt();
      return;
    }
    let board = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UIInteractions);
    if !IsDefined(board) {
      return;
    }
    this.m_shop = shop;
    let shown = FromVariant<InteractionChoiceHubData>(board.GetVariant(GetAllBlackboardDefs().UIInteractions.InteractionChoiceHub));
    // Ours already, for this NPC; or the game's own prompt is up (it wins).
    if npc == this.m_target && (shown.id == TandemVendors.HubId() || shown.active) {
      return;
    }
    if this.m_target == 0u {
      this.m_savedVisualizers = FromVariant<VisualizersInfo>(board.GetVariant(GetAllBlackboardDefs().UIInteractions.VisualizersInfo));
    }
    let choice: InteractionChoiceData;
    choice.inputAction = n"Choice1";
    choice.isHoldAction = false;
    choice.localizedName = TandemText.Get("Vendor-Trade");
    let hub: InteractionChoiceHubData;
    hub.id = TandemVendors.HubId();
    hub.active = true;
    hub.title = TandemVendors.NameOf(body);
    ArrayPush(hub.choices, choice);
    board.SetVariant(GetAllBlackboardDefs().UIInteractions.InteractionChoiceHub, ToVariant(hub), true);
    let visualizers: VisualizersInfo;
    visualizers.activeVisId = TandemVendors.HubId();
    ArrayPush(visualizers.visIds, TandemVendors.HubId());
    board.SetVariant(GetAllBlackboardDefs().UIInteractions.VisualizersInfo, ToVariant(visualizers), true);
    if npc != this.m_target {
      TandemNative.Log(s"vendor: beside \(TandemVendors.NameOf(body)) (npc \(npc), \(TandemProgress.VendorShopName(shop))): trade prompt shown");
    }
    this.m_target = npc;
    this.m_body = body;
  }

  private func HidePrompt() -> Void {
    if this.m_target == 0u {
      return;
    }
    this.m_target = 0u;
    this.m_body = null;
    let board = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UIInteractions);
    if !IsDefined(board) {
      return;
    }
    // Only ours: the game may have put its own up since.
    let shown = FromVariant<InteractionChoiceHubData>(board.GetVariant(GetAllBlackboardDefs().UIInteractions.InteractionChoiceHub));
    if shown.id == TandemVendors.HubId() {
      let none: InteractionChoiceHubData;
      none.id = TandemVendors.HubId();
      none.active = false;
      board.SetVariant(GetAllBlackboardDefs().UIInteractions.InteractionChoiceHub, ToVariant(none), true);
      board.SetVariant(GetAllBlackboardDefs().UIInteractions.VisualizersInfo, ToVariant(this.m_savedVisualizers), true);
    }
  }

  protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
    if Equals(ListenerAction.GetName(action), n"Choice1") && ListenerAction.IsButtonJustPressed(action) && this.m_target != 0u && !this.Yields() {
      this.Open(this.m_target, this.m_body);
    }
    return false;
  }

  // The vendor's name as its record says (the shop's own, else the NPC's).
  public static func NameOf(body: ref<ScriptedPuppet>) -> String {
    let character = TweakDBInterface.GetCharacterRecord(body.GetRecordID());
    let vendor = IsDefined(character) ? character.VendorID() : null;
    if IsDefined(vendor) {
      let name = vendor.LocalizedName();
      if StrLen(name) > 0 {
        let text = GetLocalizedText(name);
        if StrLen(text) > 0 {
          return text;
        }
      }
    }
    return body.GetDisplayName();
  }

  // ---- the shop ----

  // A press on the prompt (or `vendor open`): our own world's vendor of that
  // entity, on the game's own screen, or the reason it stays shut.
  public func Open(npc: Uint32, body: ref<ScriptedPuppet>) -> Bool {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) || !IsDefined(body) {
      return false;
    }
    let name = TandemVendors.NameOf(body);
    let shop = this.Decide(player, npc, body);
    if shop != 0 {
      TandemNative.Log(s"vendor: \(name) (npc \(npc)) stays shut: \(TandemProgress.VendorShopName(shop))");
      let key = TandemVendors.WhyShut(shop);
      if StrLen(key) > 0 {
        TandemText.NoticeAs(this.m_game, SimpleMessageType.Neutral, 4.0, key, name);
      }
      return false;
    }
    let own = this.OwnOf(npc);
    let data = new VendorPanelData();
    data.data.vendorId = TDBID.ToStringDEBUG(MarketSystem.GetVendorID(own));
    data.data.entityID = own.GetEntityID();
    data.data.isActive = true;
    GameInstance.GetUISystem(this.m_game).RequestVendorMenu(data);
    this.HidePrompt();
    this.m_opened = own.GetEntityID();
    this.m_openedAt = this.m_now;
    this.m_opens += 1;
    TandemNative.Log(s"vendor: shop open: \(name) (npc \(npc), type \(EnumInt(own.GetVendorType()))), our own vendor \(TandemNative.EntityHex(own.GetEntityID())) \(TDBID.ToStringDEBUG(MarketSystem.GetVendorID(own)))");
    return true;
  }

  // The notice's key for a shop that stays shut ("": nothing to say).
  public static func WhyShut(shop: Int32) -> String {
    switch shop {
      case 4:
        return "Vendor-Dead";
      case 5:
        return "Vendor-NotNow";
      case 6:
        return "Vendor-NotHere";
      case 7:
        return "Vendor-Locked";
      default:
        return "";
    }
  }

  public func Bind(screen: ref<VendorHubMenuGameController>) -> Void {
    this.m_screen = screen;
    TandemNative.Log(s"vendor: the shop's screen is up (\(this.m_now - this.m_openedAt) s after we opened one)");
  }

  public func Unbind(screen: ref<VendorHubMenuGameController>) -> Void {
    if this.m_screen == screen {
      this.m_screen = null;
      TandemNative.Log("vendor: the shop's screen closed");
    }
  }

  // ---- the session's dead vendors ----

  // That vendor died in the session: our own copy goes out of sight (and
  // out of the shared world if we ran it), and stays so while we play.
  private func Bury(entity: EntityID) -> Void {
    if ArrayContains(this.m_buried, entity) {
      return;
    }
    let npcs = this.m_system.Npcs();
    let hidden = IsDefined(npcs) && npcs.Bury(entity);
    ArrayPush(this.m_buried, entity);
    ArrayPush(this.m_buriedBodies, GameInstance.FindEntityByID(this.m_game, entity) as GameObject);
    TandemNative.Log(s"vendor: \(TandemNative.EntityHex(entity)) died in the session: our own copy \(hidden ? "hidden" : "is not here, or is the dead one")");
  }

  // A buried vendor's body that streamed in again is a new one: hidden too.
  private func Watch() -> Void {
    let npcs = this.m_system.Npcs();
    let i = 0;
    while i < ArraySize(this.m_buried) {
      let body = GameInstance.FindEntityByID(this.m_game, this.m_buried[i]) as GameObject;
      if IsDefined(body) && body != this.m_buriedBodies[i] {
        this.m_buriedBodies[i] = body;
        if IsDefined(npcs) && npcs.Bury(this.m_buried[i]) {
          TandemNative.Log(s"vendor: \(TandemNative.EntityHex(this.m_buried[i])) streamed in again: hidden (it died in the session)");
        }
      }
      i += 1;
    }
  }

  // ---- tools/game.sh cmd vendor ... ----
  // vendor | vendor near | vendor goto | vendor open | vendor close
  public func Command(command: String) -> Void {
    let words = StrSplit(command, " ");
    let verb = ArraySize(words) > 1 ? words[1] : "status";
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      TandemNative.Log("vendor: no V");
      return;
    }
    if Equals(verb, "near") || Equals(verb, "goto") {
      let own = this.NearestOwn(player, 200.0);
      if !IsDefined(own) {
        TandemNative.Log("vendor: no vendor of our own world within 200 m");
        return;
      }
      let at = own.GetWorldPosition();
      if Equals(verb, "goto") {
        // In front of it, facing it.
        let ahead = own.GetWorldForward();
        ahead.Z = 0.0;
        let to = at + Vector4.Normalize(ahead) * 1.5;
        to.W = 1.0;
        GameInstance.GetTeleportationFacility(this.m_game).Teleport(player, to, Vector4.ToRotation(at - to));
      }
      TandemNative.Log(s"vendor: near \(TandemVendors.NameOf(own)) record \(TDBID.ToStringDEBUG(own.GetRecordID())) entity \(TandemNative.EntityHex(own.GetEntityID())) at \(at.X) \(at.Y) \(at.Z) (\(Vector4.Distance(player.GetWorldPosition(), at)) m, vendor \(TDBID.ToStringDEBUG(MarketSystem.GetVendorID(own))), accessible \(MarketSystem.IsAccessible(player, MarketSystem.GetVendorID(own))))");
      return;
    }
    if Equals(verb, "open") {
      // The nearest vendor's proxy within a few metres, wherever V looks.
      let npc = 0u;
      let body = this.NearestAny(player, 6.0, npc);
      if !IsDefined(body) {
        TandemNative.Log("vendor: no vendor's proxy within 6 m");
        return;
      }
      if Vector4.Distance(player.GetWorldPosition(), body.GetWorldPosition()) > TandemProgress.VendorReach() {
        // Beside it first, as a player would walk up.
        let at = body.GetWorldPosition();
        let back = player.GetWorldPosition() - at;
        back.Z = 0.0;
        let to = at + Vector4.Normalize(back) * 1.2;
        to.W = 1.0;
        GameInstance.GetTeleportationFacility(this.m_game).Teleport(player, to, Vector4.ToRotation(at - to));
        TandemNative.Log("vendor: walked up to it first");
      }
      this.Open(npc, body);
      return;
    }
    if Equals(verb, "close") {
      if IsDefined(this.m_screen) {
        this.m_screen.TandemClose();
        TandemNative.Log("vendor: shop closed");
      } else {
        TandemNative.Log("vendor: no shop open");
      }
      return;
    }
    let npcs = this.m_system.Npcs();
    let shown = 0;
    if IsDefined(npcs) {
      for proxy in npcs.Proxies() {
        if EntityID.IsDefined(TandemProgress.VendorOf(proxy.npc)) {
          shown += 1;
          let body = npcs.ProxyBody(proxy.id) as ScriptedPuppet;
          if IsDefined(body) {
            TandemNative.Log(s"  npc \(proxy.npc): \(TandemVendors.NameOf(body)), entity \(TandemNative.EntityHex(TandemProgress.VendorOf(proxy.npc))), our own copy \(IsDefined(this.OwnOf(proxy.npc)) ? "here" : "absent"), \(TandemProgress.VendorShopName(this.Decide(player, proxy.npc, body))), \(Vector4.Distance(player.GetWorldPosition(), body.GetWorldPosition())) m");
          }
        }
      }
    }
    TandemNative.Log(s"vendor: \(shown) vendors' proxies, \(ArraySize(this.m_buried)) dead in the session, \(this.m_opens) shops opened, prompt on npc \(this.m_target), screen \(IsDefined(this.m_screen))");
  }

  // The nearest vendor's proxy within reach, wherever our V looks.
  private func NearestAny(player: ref<PlayerPuppet>, reach: Float, out npc: Uint32) -> ref<ScriptedPuppet> {
    npc = 0u;
    let npcs = this.m_system.Npcs();
    if !IsDefined(npcs) {
      return null;
    }
    let best: ref<ScriptedPuppet>;
    let bestAt = reach;
    for proxy in npcs.Proxies() {
      let body = npcs.ProxyBody(proxy.id) as ScriptedPuppet;
      if IsDefined(body) && EntityID.IsDefined(TandemProgress.VendorOf(proxy.npc)) {
        let far = Vector4.Distance(player.GetWorldPosition(), body.GetWorldPosition());
        if far <= bestAt {
          best = body;
          bestAt = far;
          npc = proxy.npc;
        }
      }
    }
    return best;
  }

  // The nearest vendor of our own world (placed, alive, not a proxy).
  private func NearestOwn(player: ref<PlayerPuppet>, reach: Float) -> ref<ScriptedPuppet> {
    let npcs = this.m_system.Npcs();
    let best: ref<ScriptedPuppet>;
    let bestAt = reach;
    for body in player.GetNPCsAroundObject(reach) {
      let id = IsDefined(body) ? body.GetEntityID() : EntityID.FromHash(0ul);
      if IsDefined(body) && EntityID.IsStatic(id) && body.IsVendor() && !body.IsDead() && !(IsDefined(npcs) && npcs.IsProxyEntity(id)) {
        let far = Vector4.Distance(player.GetWorldPosition(), body.GetWorldPosition());
        if far <= bestAt {
          best = body;
          bestAt = far;
        }
      }
    }
    return best;
  }
}

// ---- the shop's screen ----

@wrapMethod(VendorHubMenuGameController)
protected cb func OnInitialize() -> Bool {
  let result = wrappedMethod();
  let vendors = TandemVendors.Of(GetGameInstance());
  if IsDefined(vendors) {
    vendors.Bind(this);
  }
  return result;
}

@wrapMethod(VendorHubMenuGameController)
protected cb func OnUninitialize() -> Bool {
  let vendors = TandemVendors.Of(GetGameInstance());
  if IsDefined(vendors) {
    vendors.Unbind(this);
  }
  return wrappedMethod();
}

// `vendor close`: as the screen's own close.
@addMethod(VendorHubMenuGameController)
public func TandemClose() -> Void {
  this.m_menuEventDispatcher.SpawnEvent(n"OnVendorClose");
}

// ---- no shop for a proxy ----

// A proxy of a vendor NPC attaching to our world asks for a vendor of its
// own (NPCPuppet.OnGameAttached, PrepareVendor): MarketSystem would add one
// to its saved list under the proxy's dynamic id, a new entry at every
// spawn, in our save. A proxy is never a shop: none for it.
@wrapMethod(MarketSystem)
private final func OnAttachVendorRequest(request: ref<AttachVendorRequest>) {
  if TandemVendors.IsCopy(request.owner) {
    return;
  }
  wrappedMethod(request);
}

// Detached, an entity with no vendor gets one made only to be marked
// detached: never for a dynamic entity that never had one (our proxies,
// deleted, are no longer known as ours by then).
@wrapMethod(MarketSystem)
private final func OnDeattachVendorRequest(request: ref<DeattachVendorRequest>) {
  if IsDefined(request.owner) && EntityID.IsDynamic(request.owner.GetEntityID()) && !IsDefined(this.GetVendorHashMap(request.owner.GetEntityID())) {
    return;
  }
  wrappedMethod(request);
}
