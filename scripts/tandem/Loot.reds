// Loot in co-op (docs/ARCHITECTURE.md, "Loot").
//
// Corpses and containers are per player: every game rolls its own loot, by
// its own V's level and perks, and nobody takes from the other. Nothing is
// sent for them:
// - the host's NPCs die and roll their loot in the host's game, as in solo;
// - a guest's proxy is killed by the host's word (Npcs.reds, Die) and dies
//   the game's way: NPCPuppet.OnIncapacitated runs ProcessLoot, which rolls
//   its corpse's loot from its record (the real NPC's) and drops what it
//   holds, in the guest's world only. Here we check that it did (rolling it
//   if the death did not);
// - a proxy corpse the host's body takes away (Npcs.reds, Remove) leaves
//   what it still holds as a loot bag, as vanilla does for a disposed body;
// - containers have the same contents in every game's copy of the world and
//   are looted by each player in their own (nothing here).
//
// What a V drops from its inventory on purpose is a shared world item, the
// host's: it lies in every game and the first to pick a stack up keeps it.
// The dropper's game spawns its bag as vanilla does and sends it; the host
// numbers it and every other game spawns a copy (the same records and
// seeds) where it lies. Picking a stack up is immediate (no wait on the
// network) and a claim to the host; the rare loser of a race (both picked
// it up within a ping) gives it back, with a notice.
//
// Quest and important items are every player's, so no quest breaks: an item
// tagged Quest (quest items, their keys and shards), a keycard, or an
// iconic item (not bought at a vendor) that comes into a V's inventory
// goes into every other V's too, by the game's own item-add path (its
// pickup notification), unless that V has one already (SharedItem). The
// host's own quests see it in the host's inventory whoever looted it. The
// important items on a host NPC's corpse are put on the guests' proxy
// corpses too, so a guest finds them where the host would; a second copy
// of an item co-op already gave is taken back out on arrival. Quest
// rewards are Story.reds' (QuestReward: GiveReward, items, money, XP).
//
// tools/game.sh cmd loot | loot drop RECORD [N] | loot take | loot corpse
// | loot share RECORD

public class TandemWorldStack {
  public let record: TweakDBID;
  public let seed: Uint32;
  public let quantity: Int32;
  // Gone from our copy (picked up, here or elsewhere).
  public let taken: Bool;
  // Our V picked it up: claimed (sent), granted (the host said so).
  public let ours: Bool;
  public let claimed: Bool;
  // How many of it our V had at the last look (to see it arrive).
  public let had: Int32;
}

public class TandemWorldItem {
  // The host's number (0: ours, not numbered yet), and our token for ours.
  public let id: Uint32;
  public let token: Uint32;
  public let dropper: Uint32;
  // Ours: dropped from our inventory, so its bag is the one vanilla spawned.
  public let ownDrop: Bool;
  public let position: Vector4;
  public let stacks: array<ref<TandemWorldStack>>;
  // Its bag in our world, once found.
  public let entity: EntityID;
  // A copy we spawned (another player's drop), from whose inventory, when.
  public let spawned: Bool;
  public let holder: EntityID;
  public let spawnedAt: Float;
  public let warned: Bool;
}

// A loot object that just appeared in our world, to tell which bag is which.
public class TandemLootSeen {
  public let id: EntityID;
  public let at: Float;
}

public class TandemLootTick extends DelayCallback {
  public let loot: wref<TandemLoot>;

  public func Call() -> Void {
    if IsDefined(this.loot) {
      this.loot.Tick();
    }
  }
}

// A proxy's corpse, a moment after its death rolled its loot.
public class TandemCorpseCheck extends DelayCallback {
  public let game: GameInstance;
  public let id: EntityID;

  public func Call() -> Void {
    TandemLoot.CheckCorpse(this.game, this.id);
  }
}

// A host NPC's corpse, a moment after its death rolled its loot.
public class TandemHostCorpseCheck extends DelayCallback {
  public let game: GameInstance;
  public let id: EntityID;
  public let npc: Uint32;

  public func Call() -> Void {
    TandemLoot.CheckHostCorpse(this.game, this.id, this.npc);
  }
}

// A proxy corpse that left its loot bag: deleted once the bag is out.
public class TandemDeleteLater extends DelayCallback {
  public let id: EntityID;

  public func Call() -> Void {
    GameInstance.GetDynamicEntitySystem().DeleteEntity(this.id);
  }
}

public class TandemLoot extends ScriptableSystem {
  private let m_inGame: Bool;
  private let m_you: Uint32;
  private let m_nextToken: Uint32;
  private let m_items: array<ref<TandemWorldItem>>;
  private let m_seen: array<ref<TandemLootSeen>>;
  // Important items co-op gave our V, and those still on their way in.
  private let m_given: array<TweakDBID>;
  private let m_giving: array<TweakDBID>;

  public static func Get(game: GameInstance) -> ref<TandemLoot> {
    return GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemLoot") as TandemLoot;
  }

  private final func OnPlayerAttach(request: ref<PlayerAttachRequest>) -> Void {
    if GameInstance.GetSystemRequestsHandler().IsPreGame() {
      return;
    }
    this.Begin();
  }

  // At the load, or when the co-op menu starts a session in a loaded game
  // (TandemSystem.BeginSession): once per game, after a session was chosen.
  public func Begin() -> Void {
    if this.m_inGame || !TandemNative.IsConfigured() {
      return;
    }
    this.m_inGame = true;
    this.m_you = 0u;
    ArrayClear(this.m_items);
    ArrayClear(this.m_seen);
    // Bags (gameLootBag) and single item drops (gameItemDropObject).
    let callbacks = GameInstance.GetCallbackSystem();
    callbacks.RegisterCallback(n"Entity/Attach", this, n"OnLootAttach").AddTarget(EntityTarget.Type(n"gameLootBag"));
    callbacks.RegisterCallback(n"Entity/Attach", this, n"OnLootAttach").AddTarget(EntityTarget.Type(n"gameLootObject"));
    this.Schedule();
  }

  private final func OnPlayerDetach(request: ref<PlayerDetachRequest>) -> Void {
    this.m_inGame = false;
    ArrayClear(this.m_items);
    ArrayClear(this.m_seen);
  }

  // Bags and item drops appearing (any: only those near a world item matter).
  private cb func OnLootAttach(event: ref<EntityLifecycleEvent>) {
    let entity = event.GetEntity();
    if !this.m_inGame || this.m_you == 0u || !IsDefined(entity) || ArraySize(this.m_seen) >= 64 {
      return;
    }
    let id = entity.GetEntityID();
    for known in this.m_seen {
      if known.id == id {
        return;
      }
    }
    let seen = new TandemLootSeen();
    seen.id = id;
    seen.at = this.Now();
    ArrayPush(this.m_seen, seen);
  }

  private func Schedule() -> Void {
    let tick = new TandemLootTick();
    tick.loot = this;
    GameInstance.GetDelaySystem(this.GetGameInstance()).DelayCallback(tick, 0.25, false);
  }

  private func Now() -> Float {
    return EngineTime.ToFloat(GameInstance.GetSimTime(this.GetGameInstance()));
  }

  public func Tick() -> Void {
    if !this.m_inGame {
      return;
    }
    let you = TandemNative.You();
    if you != this.m_you {
      if this.m_you != 0u {
        this.EndSession();
      }
      this.m_you = you;
    }
    let events = TandemNative.TakeLootEvents();
    if you != 0u {
      for event in events {
        if event.kind == 1 {
          this.Appeared(event);
        } else if event.kind == 2 {
          this.Taken(event);
        } else if event.kind == 3 {
          this.SharedLooted(event);
        } else if event.kind == 4 {
          this.SharedOnCorpse(event);
        }
      }
      this.Bind();
      this.Watch();
      this.SpawnCopies();
    }
    this.Schedule();
  }

  // ---- what our V drops ----

  // Our V dropped these from its inventory (the backpack or inventory menu
  // closing with items to drop, Loot.reds' wraps below). In a session we
  // drop them as vanilla's RPGManager.DropManyItems does, in one bag, and
  // share it; false: not in a session, vanilla drops them.
  public static func DropShared(player: wref<PlayerPuppet>, queue: array<ItemModParams>) -> Bool {
    if !IsDefined(player) || ArraySize(queue) == 0 || TandemNative.You() == 0u {
      return false;
    }
    let loot = TandemLoot.Get(player.GetGame());
    return IsDefined(loot) && loot.Drop(player, queue);
  }

  private func Drop(player: ref<GameObject>, queue: array<ItemModParams>) -> Bool {
    let game = this.GetGameInstance();
    let instructions: array<DropInstruction>;
    let item = new TandemWorldItem();
    let records: array<TweakDBID>;
    let seeds: array<Uint32>;
    let quantities: array<Int32>;
    for entry in queue {
      if entry.quantity > 0 && ArraySize(item.stacks) < 16 {
        ArrayPush(instructions, DropInstruction.Create(entry.itemID, entry.quantity));
        RPGManager.ReturnRetrievableWeaponMods(RPGManager.GetItemData(game, player, entry.itemID), player);
        let stack = TandemLoot.StackOf(entry.itemID, entry.quantity);
        ArrayPush(item.stacks, stack);
        ArrayPush(records, stack.record);
        ArrayPush(seeds, stack.seed);
        ArrayPush(quantities, stack.quantity);
      }
    }
    if ArraySize(instructions) == 0 {
      return false;
    }
    let at = player.GetWorldPosition();
    if !GameInstance.GetLootManager(game).SpawnItemDropOfManyItems(player, instructions, n"playerDropBag", at) {
      TandemNative.Log("loot: the game would not drop our items: dropped the usual way, not shared");
      return false;
    }
    this.m_nextToken += 1u;
    item.token = this.m_nextToken;
    item.dropper = this.m_you;
    item.ownDrop = true;
    item.position = at;
    ArrayPush(this.m_items, item);
    TandemNative.SendWorldItem(item.token, at, records, seeds, quantities);
    TandemNative.Log(s"loot: we dropped \(ArraySize(item.stacks)) stack(s) (\(TDBID.ToStringDEBUG(records[0])) x\(quantities[0])...): shared, token \(item.token)");
    return true;
  }

  // ---- the host's word ----

  // One stack of a world item (the host's numbering): ours coming back
  // numbered, or another player's to spawn a copy of.
  private func Appeared(event: TandemLootEvent) -> Void {
    let item = this.Find(event.id);
    if !IsDefined(item) && event.player == this.m_you {
      item = this.FindToken(event.token);
      if IsDefined(item) && event.stack == 0 {
        item.id = event.id;
        ArrayClear(item.stacks);
        TandemNative.Log(s"loot: our drop (token \(event.token)) is world item \(event.id)");
      }
    }
    if !IsDefined(item) {
      if event.stack != 0 {
        return; // its first stack was missed: never half a bag
      }
      item = new TandemWorldItem();
      item.id = event.id;
      item.dropper = event.player;
      item.position = new Vector4(event.x, event.y, event.z, 1.0);
      ArrayPush(this.m_items, item);
      TandemNative.Log(s"loot: world item \(event.id) from player \(event.player) lies at (\(event.x), \(event.y), \(event.z)): \(event.stacks) stack(s)");
    }
    if event.stack < ArraySize(item.stacks) {
      // A bag we have, as the host has it now (sent again with a refused
      // claim): a stack gone there is gone here too.
      let known = item.stacks[event.stack];
      if event.quantity <= 0 && !known.taken && !known.ours {
        known.taken = true;
        let removed = this.RemoveFrom(this.ObjectOf(item), known, known.quantity);
        TandemNative.Log(s"loot: world item \(item.id) stack \(event.stack) was already taken on the host: \(removed) removed from our copy");
        this.Forget(item);
      }
      return;
    }
    if event.stack != ArraySize(item.stacks) {
      return; // out of order: not expected on a reliable channel
    }
    let stack = new TandemWorldStack();
    stack.record = event.record;
    stack.seed = event.seed;
    stack.quantity = event.quantity;
    stack.taken = event.quantity <= 0;
    ArrayPush(item.stacks, stack);
    if item.ownDrop {
      // The bag is the one vanilla gave us: what it holds now is what we
      // dropped; anything already gone was taken by someone else.
      stack.had = this.CountIn(GetPlayer(this.GetGameInstance()), stack);
    }
  }

  // Who has a stack now: us (our claim granted), or someone else (taken
  // from our copy; if our V picked it up too, it goes back).
  private func Taken(event: TandemLootEvent) -> Void {
    let item = this.Find(event.id);
    if !IsDefined(item) || event.stack < 0 || event.stack >= ArraySize(item.stacks) {
      return;
    }
    let stack = item.stacks[event.stack];
    let what = s"\(TDBID.ToStringDEBUG(stack.record)) x\(stack.quantity)";
    if event.player == this.m_you {
      stack.ours = true;
      stack.taken = true;
      TandemNative.Log(s"loot: world item \(item.id) stack \(event.stack) (\(what)) is ours");
    } else if stack.ours {
      // Someone was first: what our V picked up goes back to nobody (the
      // winner has it in their game).
      let player = GetPlayer(this.GetGameInstance());
      let back = this.RemoveFrom(player, stack, stack.quantity);
      stack.ours = false;
      stack.taken = true;
      let name = this.NameOf(event.player);
      TandemNative.Log(s"loot: world item \(item.id) stack \(event.stack) (\(what)) went to \(event.player == 0u ? "nobody (it was gone)" : name) first: \(back) given back");
      this.Notice(event.player == 0u ? "Someone already took it" : s"\(name) picked it up first");
      // Nobody: the host has no such bag any more (every stack taken, the
      // news of it lost). The rest of our copy goes too: no ghost bag.
      if event.player == 0u {
        let object = this.ObjectOf(item);
        for other in item.stacks {
          if !other.taken && !other.ours {
            other.taken = true;
            this.RemoveFrom(object, other, other.quantity);
          }
        }
        TandemNative.Log(s"loot: world item \(item.id) is gone on the host: our copy emptied");
      }
    } else {
      stack.taken = true;
      let removed = this.RemoveFrom(this.ObjectOf(item), stack, stack.quantity);
      TandemNative.Log(s"loot: \(this.NameOf(event.player)) took world item \(item.id) stack \(event.stack) (\(what)): \(removed) removed from our copy");
    }
    this.Forget(item);
  }

  // ---- our world's copy of each item ----

  // Which bag is which: an item without one takes a loot object that
  // appeared within 3 m of where it lies holding its first stack.
  private func Bind() -> Void {
    let now = this.Now();
    let kept: array<ref<TandemLootSeen>>;
    for seen in this.m_seen {
      if now - seen.at < 15.0 {
        ArrayPush(kept, seen);
      }
    }
    this.m_seen = kept;
    for item in this.m_items {
      if !EntityID.IsDefined(item.entity) {
        let first = this.FirstLeft(item);
        if IsDefined(first) {
          let best = -1;
          let bestDistance = 3.0;
          let i = 0;
          while i < ArraySize(this.m_seen) {
            let object = GameInstance.FindEntityByID(this.GetGameInstance(), this.m_seen[i].id) as GameObject;
            if IsDefined(object) {
              let distance = Vector4.Distance(object.GetWorldPosition(), item.position);
              if distance < bestDistance && this.CountIn(object, first) > 0 {
                best = i;
                bestDistance = distance;
              }
            }
            i += 1;
          }
          if best >= 0 {
            this.BindTo(item, this.m_seen[best].id);
            ArrayErase(this.m_seen, best);
          }
        }
      }
    }
  }

  private func BindTo(item: ref<TandemWorldItem>, id: EntityID) -> Void {
    item.entity = id;
    let object = this.ObjectOf(item);
    let player = GetPlayer(this.GetGameInstance());
    // Stacks taken while we had not found it yet.
    for stack in item.stacks {
      if stack.taken && !stack.ours {
        this.RemoveFrom(object, stack, stack.quantity);
      }
      stack.had = this.CountIn(player, stack);
    }
    // A copy: what the game left in its holder's inventory, if anything.
    if item.spawned {
      let holder = GameInstance.FindEntityByID(this.GetGameInstance(), item.holder) as GameObject;
      for stack in item.stacks {
        this.RemoveFrom(holder, stack, stack.quantity);
      }
    }
    TandemNative.Log(s"loot: world item \(item.id)\(item.ownDrop ? " (ours)" : "") found in our world");
  }

  // Our V picking stacks up: gone from the bag, arrived in its inventory.
  private func Watch() -> Void {
    let player = GetPlayer(this.GetGameInstance());
    if !IsDefined(player) {
      return;
    }
    for item in this.m_items {
      if EntityID.IsDefined(item.entity) {
        let object = this.ObjectOf(item);
        let near = Vector4.Distance(player.GetWorldPosition(), item.position) < 5.0;
        let i = 0;
        for stack in item.stacks {
          if !stack.taken && !stack.ours {
            let mine = this.CountIn(player, stack);
            let left = IsDefined(object) ? this.CountIn(object, stack) : -1;
            if (left == 0 && mine > stack.had) || (left < 0 && near && mine >= stack.had + stack.quantity) {
              stack.ours = true;
              TandemNative.Log(s"loot: we picked up world item \(item.id) stack \(i) (\(TDBID.ToStringDEBUG(stack.record)) x\(stack.quantity))");
            }
            stack.had = mine;
          }
          if stack.ours && !stack.claimed && item.id != 0u {
            stack.claimed = true;
            TandemNative.TakeWorldItem(item.id, i);
          }
          i += 1;
        }
        // Streamed out with nothing picked up: found again when it returns.
        if !IsDefined(object) && !near {
          let none: EntityID;
          item.entity = none;
        }
      }
    }
  }

  // Another player's drop near our V: a copy of its bag where it lies, from
  // the dropper's puppet (an inventory the items can leave from).
  private func SpawnCopies() -> Void {
    let game = this.GetGameInstance();
    let player = GetPlayer(game);
    if !IsDefined(player) {
      return;
    }
    let now = this.Now();
    for item in this.m_items {
      if item.spawned && !EntityID.IsDefined(item.entity) && !item.warned && now - item.spawnedAt > 10.0 {
        item.warned = true;
        TandemNative.Log(s"loot: world item \(item.id): our copy never showed up");
      }
      if !item.ownDrop && !item.spawned && IsDefined(this.FirstLeft(item)) && Vector4.Distance(player.GetWorldPosition(), item.position) < 80.0 {
        let holder = this.HolderFor(item.dropper);
        if IsDefined(holder) {
          let ts = GameInstance.GetTransactionSystem(game);
          let instructions: array<DropInstruction>;
          for stack in item.stacks {
            if !stack.taken {
              let id = TandemLoot.ItemOf(stack);
              ts.GiveItem(holder, id, stack.quantity);
              ArrayPush(instructions, DropInstruction.Create(id, stack.quantity));
            }
          }
          let ok = GameInstance.GetLootManager(game).SpawnItemDropOfManyItems(holder, instructions, n"playerDropBag", item.position);
          item.spawned = true;
          item.spawnedAt = now;
          item.holder = holder.GetEntityID();
          TandemNative.Log(s"loot: world item \(item.id) from player \(item.dropper): copy spawning (\(ok))");
        }
      }
    }
  }

  // A remote player's puppet: the dropper's, else anyone's.
  private func HolderFor(dropper: Uint32) -> ref<GameObject> {
    let system = GameInstance.GetScriptableSystemsContainer(this.GetGameInstance()).Get(n"TandemSystem") as TandemSystem;
    if !IsDefined(system) {
      return null;
    }
    let holder = system.PuppetOf(dropper);
    let p = 1u;
    while !IsDefined(holder) && p <= 8u {
      if p != this.m_you {
        holder = system.PuppetOf(p);
      }
      p += 1u;
    }
    return holder;
  }

  // The session is over: copies of the others' drops were the host's world
  // and go; our own bags stay in our world, ours again.
  private func EndSession() -> Void {
    for item in this.m_items {
      if !item.ownDrop {
        let object = this.ObjectOf(item);
        for stack in item.stacks {
          if !stack.taken {
            this.RemoveFrom(object, stack, stack.quantity);
          }
        }
      }
    }
    TandemNative.Log(s"loot: session over, \(ArraySize(this.m_items)) world item(s) forgotten");
    ArrayClear(this.m_items);
    ArrayClear(this.m_seen);
  }

  // Every stack gone: nothing left to track.
  private func Forget(item: ref<TandemWorldItem>) -> Void {
    if IsDefined(this.FirstLeft(item)) {
      return;
    }
    for stack in item.stacks {
      if stack.ours && !stack.claimed {
        return; // our claim is still to send
      }
    }
    ArrayRemove(this.m_items, item);
  }

  // ---- proxy corpses (Npcs.reds) ----

  // A guest's proxy was just killed: its corpse is checked once its death
  // has rolled its loot (well before the game lets anyone loot it, 2 s).
  public static func Corpse(game: GameInstance, id: EntityID) -> Void {
    let check = new TandemCorpseCheck();
    check.game = game;
    check.id = id;
    GameInstance.GetDelaySystem(game).DelayCallback(check, 1.0, false);
  }

  public static func CheckCorpse(game: GameInstance, id: EntityID) -> Void {
    let body = GameInstance.GetDynamicEntitySystem().GetEntity(id) as ScriptedPuppet;
    if !IsDefined(body) {
      return;
    }
    // Rolled by its death (NPCPuppet.OnIncapacitated, ProcessLoot), as any
    // corpse's; if not, rolled now the way a corpse loaded from a save is.
    let rolled = body.WasLootGenerated();
    if !rolled {
      body.GenerateLoot();
    }
    // Quest and important items stay: whoever loots one, every V gets it.
    let items: array<wref<gameItemData>>;
    GameInstance.GetTransactionSystem(game).GetItemList(body, items);
    let important = 0;
    for data in items {
      if TandemLoot.Important(data) {
        important += 1;
      }
    }
    ScriptedPuppet.EvaluateLootQualityByTask(body);
    TandemNative.Log(s"loot: proxy corpse \(TDBID.ToStringDEBUG(body.GetRecordID())): \(ArraySize(items)) item(s)\(rolled ? "" : " (rolled here: its death rolled none)"), \(important) important");
  }

  // A dead proxy is going (the host's body went): what it still holds is
  // left as a loot bag where it lay, as vanilla does for a disposed body.
  // True: it is deleted a moment later, once the bag is out.
  public static func LeaveCorpse(game: GameInstance, id: EntityID) -> Bool {
    let body = GameInstance.GetDynamicEntitySystem().GetEntity(id) as ScriptedPuppet;
    if !IsDefined(body) || !body.IsDead() || !body.WasLootGenerated() || RPGManager.IsInventoryEmpty(body) {
      return false;
    }
    body.DropLootBag();
    let later = new TandemDeleteLater();
    later.id = id;
    GameInstance.GetDelaySystem(game).DelayCallback(later, 1.0, false);
    TandemNative.Log(s"loot: proxy corpse \(TDBID.ToStringDEBUG(body.GetRecordID())) gone, its loot left as a bag");
    return true;
  }

  // ---- quest and important items: every player's ----

  // Quest items (tag Quest: quest items, their keys and shards), keycards,
  // and iconic items (the IsItemIconic stat) not bought at a vendor.
  public static func Important(data: wref<gameItemData>) -> Bool {
    if !IsDefined(data) || data.HasStatData(gamedataStatType.ItemPurchasedAtVendor) {
      return false;
    }
    return data.HasTag(n"Quest") || Equals(RPGManager.GetItemType(data.GetID()), gamedataItemType.Gen_Keycard) || RPGManager.IsItemIconic(data);
  }

  // Something came into our V's inventory (PlayerPuppet wrap below).
  public static func Added(owner: ref<GameObject>, id: ItemID, data: wref<gameItemData>) -> Void {
    if TandemNative.You() == 0u || !IsDefined(owner) || !TandemLoot.Important(data) {
      return;
    }
    let player = GetPlayer(owner.GetGame());
    if !IsDefined(player) || owner.GetEntityID() != player.GetEntityID() {
      return;
    }
    let loot = TandemLoot.Get(owner.GetGame());
    if IsDefined(loot) {
      loot.OurImportant(id, data.GetQuantity(), false);
    }
  }

  private func OurImportant(id: ItemID, quantity: Int32, forced: Bool) -> Void {
    let record = ItemID.GetTDBID(id);
    let player = GetPlayer(this.GetGameInstance());
    // Co-op's own gift arriving.
    if !forced && ArrayContains(this.m_giving, record) {
      ArrayRemove(this.m_giving, record);
      return;
    }
    // A second copy of what co-op gave us (the corpse's own, looted later).
    if !forced && ArrayContains(this.m_given, record) {
      let owned = new TandemWorldStack();
      owned.record = record;
      if this.CountIn(player, owned) > quantity {
        GameInstance.GetTransactionSystem(this.GetGameInstance()).RemoveItem(player, id, quantity);
        TandemNative.Log(s"loot: \(TDBID.ToStringDEBUG(record)) is ours already (co-op): the second copy taken out");
      }
      return;
    }
    let stack = TandemLoot.StackOf(id, quantity);
    TandemNative.SendSharedItem(0, 0u, stack.record, stack.seed, stack.quantity);
    TandemNative.Log(s"loot: we got \(TDBID.ToStringDEBUG(record)) x\(quantity): every player gets it");
  }

  // Another V got an important item: ours too, unless we have one.
  private func SharedLooted(event: TandemLootEvent) -> Void {
    let player = GetPlayer(this.GetGameInstance());
    let owned = new TandemWorldStack();
    owned.record = event.record;
    let name = this.NameOf(event.player);
    if this.CountIn(player, owned) > 0 {
      TandemNative.Log(s"loot: \(name) looted \(TDBID.ToStringDEBUG(event.record)): we have it already");
      return;
    }
    let stack = new TandemWorldStack();
    stack.record = event.record;
    stack.seed = event.seed;
    stack.quantity = event.quantity;
    ArrayPush(this.m_given, event.record);
    ArrayPush(this.m_giving, event.record);
    let ok = GameInstance.GetTransactionSystem(this.GetGameInstance()).GiveItem(player, TandemLoot.ItemOf(stack), Max(event.quantity, 1));
    TandemNative.Log(s"loot: \(name) looted \(TDBID.ToStringDEBUG(event.record)) x\(event.quantity): given to our V (\(ok))");
  }

  // Guest: an important item on the host's corpse of NPC `id`: on our proxy
  // corpse too, to be looted where the host would.
  private func SharedOnCorpse(event: TandemLootEvent) -> Void {
    let system = GameInstance.GetScriptableSystemsContainer(this.GetGameInstance()).Get(n"TandemSystem") as TandemSystem;
    let body: ref<ScriptedPuppet>;
    if IsDefined(system) {
      body = GameInstance.GetDynamicEntitySystem().GetEntity(system.ProxyOfNpc(event.id)) as ScriptedPuppet;
    }
    if !IsDefined(body) {
      TandemNative.Log(s"loot: \(TDBID.ToStringDEBUG(event.record)) on npc \(event.id)'s corpse: no proxy of it here");
      return;
    }
    let owned = new TandemWorldStack();
    owned.record = event.record;
    if this.CountIn(body, owned) == 0 {
      let stack = new TandemWorldStack();
      stack.record = event.record;
      stack.seed = event.seed;
      stack.quantity = event.quantity;
      GameInstance.GetTransactionSystem(this.GetGameInstance()).GiveItem(body, TandemLoot.ItemOf(stack), Max(event.quantity, 1));
      ScriptedPuppet.EvaluateLootQualityByTask(body);
    }
    TandemNative.Log(s"loot: \(TDBID.ToStringDEBUG(event.record)) put on our proxy corpse of npc \(event.id)");
  }

  // Host: one of our NPCs died; a moment later, once its loot is rolled,
  // its important items go on the guests' proxy corpses too.
  public static func HostCorpse(game: GameInstance, id: EntityID, npc: Uint32) -> Void {
    let check = new TandemHostCorpseCheck();
    check.game = game;
    check.id = id;
    check.npc = npc;
    GameInstance.GetDelaySystem(game).DelayCallback(check, 1.5, false);
  }

  public static func CheckHostCorpse(game: GameInstance, id: EntityID, npc: Uint32) -> Void {
    let body = GameInstance.FindEntityByID(game, id) as ScriptedPuppet;
    if !IsDefined(body) {
      return;
    }
    let items: array<wref<gameItemData>>;
    GameInstance.GetTransactionSystem(game).GetItemList(body, items);
    for data in items {
      if TandemLoot.Important(data) {
        let stack = TandemLoot.StackOf(data.GetID(), data.GetQuantity());
        TandemNative.SendSharedItem(1, npc, stack.record, stack.seed, stack.quantity);
        TandemNative.Log(s"loot: npc \(npc)'s corpse holds \(TDBID.ToStringDEBUG(stack.record)): on the guests' proxies too");
      }
    }
  }

  // ---- (test) tools/game.sh cmd loot ... ----

  public static func Command(game: GameInstance, command: String) -> Void {
    let loot = TandemLoot.Get(game);
    if IsDefined(loot) {
      loot.Run(command);
    }
  }

  private func Run(command: String) -> Void {
    let game = this.GetGameInstance();
    let player = GetPlayer(game);
    if !IsDefined(player) {
      return;
    }
    if StrBeginsWith(command, "loot drop ") {
      // Drop N of a record from our inventory (given first if we have none),
      // the way the inventory menu does.
      let rest = StrAfterFirst(command, "drop ");
      let name = StrContains(rest, " ") ? StrBeforeFirst(rest, " ") : rest;
      let count = StrContains(rest, " ") ? StringToInt(StrAfterFirst(rest, " "), 1) : 1;
      let record = TDBID.Create(name);
      let ts = GameInstance.GetTransactionSystem(game);
      let id = this.FindItem(player, record);
      if !ItemID.IsValid(id) || ts.GetItemQuantity(player, id) < count {
        ts.GiveItem(player, ItemID.FromTDBID(record), count);
        id = this.FindItem(player, record);
      }
      let entry: ItemModParams;
      entry.itemID = id;
      entry.quantity = count;
      let queue: array<ItemModParams>;
      ArrayPush(queue, entry);
      let shared = TandemLoot.DropShared(player, queue);
      TandemNative.Log(s"loot drop: \(name) x\(count), shared \(shared)");
    } else if StrBeginsWith(command, "loot share ") {
      // As if our V had looted an important item: given here, then shared.
      let record = TDBID.Create(StrAfterFirst(command, "share "));
      let id = ItemID.FromTDBID(record);
      ArrayPush(this.m_giving, record);
      GameInstance.GetTransactionSystem(game).GiveItem(player, id, 1);
      this.OurImportant(id, 1, true);
    } else if Equals(command, "loot take") {
      // Take everything from the nearest world item's bag, as "take all".
      let best: ref<TandemWorldItem>;
      let bestDistance = 6.0;
      for item in this.m_items {
        let object = this.ObjectOf(item);
        if IsDefined(object) && IsDefined(this.FirstLeft(item)) {
          let distance = Vector4.Distance(object.GetWorldPosition(), player.GetWorldPosition());
          if distance < bestDistance {
            best = item;
            bestDistance = distance;
          }
        }
      }
      if IsDefined(best) {
        let ok = GameInstance.GetTransactionSystem(game).TransferAllItems(this.ObjectOf(best), player);
        TandemNative.Log(s"loot take: world item \(best.id), \(ok)");
      } else {
        TandemNative.Log("loot take: no world item within 6 m");
      }
    } else if Equals(command, "loot corpse") {
      // The nearest dead proxy's loot.
      let entities = GameInstance.GetDynamicEntitySystem();
      let best: ref<ScriptedPuppet>;
      let bestDistance = 30.0;
      for entity in entities.GetTagged(n"Tandem.Proxy") {
        let body = entity as ScriptedPuppet;
        if IsDefined(body) && body.IsDead() {
          let distance = Vector4.Distance(body.GetWorldPosition(), player.GetWorldPosition());
          if distance < bestDistance {
            best = body;
            bestDistance = distance;
          }
        }
      }
      if IsDefined(best) {
        let items: array<wref<gameItemData>>;
        GameInstance.GetTransactionSystem(game).GetItemList(best, items);
        TandemNative.Log(s"loot corpse: \(TDBID.ToStringDEBUG(best.GetRecordID())) \(bestDistance) m away, loot rolled \(best.WasLootGenerated()), looted \(best.IsLooted()), \(ArraySize(items)) item(s)");
        for data in items {
          if IsDefined(data) {
            TandemNative.Log(s"loot corpse:   \(TDBID.ToStringDEBUG(ItemID.GetTDBID(data.GetID()))) x\(data.GetQuantity())\(data.HasTag(n"Quest") ? " (quest)" : "")");
          }
        }
      } else {
        TandemNative.Log("loot corpse: no dead proxy within 30 m");
      }
    } else {
      TandemNative.Log(s"loot: \(ArraySize(this.m_items)) world item(s)");
      for item in this.m_items {
        let left = 0;
        for stack in item.stacks {
          if !stack.taken {
            left += 1;
          }
        }
        TandemNative.Log(s"loot:   \(item.id) from player \(item.dropper)\(item.ownDrop ? " (ours)" : ""): \(left)/\(ArraySize(item.stacks)) stack(s) left, found \(EntityID.IsDefined(item.entity)), \(Vector4.Distance(item.position, player.GetWorldPosition())) m away");
      }
    }
  }

  // ---- helpers ----

  // A stack as the protocol knows it: record, and the seed of a unique item
  // (its stats: the same in every game's copy).
  public static func StackOf(id: ItemID, quantity: Int32) -> ref<TandemWorldStack> {
    let stack = new TandemWorldStack();
    stack.record = ItemID.GetTDBID(id);
    stack.seed = Equals(ItemID.GetStructure(id), gamedataItemStructure.Unique) ? ItemID.GetRngSeed(id) : 0u;
    stack.quantity = quantity;
    return stack;
  }

  public static func ItemOf(stack: ref<TandemWorldStack>) -> ItemID {
    return stack.seed != 0u ? ItemID.CreateFromSeedWithOffset(stack.record, stack.seed, 0) : ItemID.FromTDBID(stack.record);
  }

  private func Matches(data: wref<gameItemData>, stack: ref<TandemWorldStack>) -> Bool {
    let id = data.GetID();
    return ItemID.GetTDBID(id) == stack.record && (stack.seed == 0u || ItemID.GetRngSeed(id) == stack.seed);
  }

  // How many of a stack's item an inventory holds (0 when it has no inventory).
  private func CountIn(object: ref<GameObject>, stack: ref<TandemWorldStack>) -> Int32 {
    if !IsDefined(object) {
      return 0;
    }
    let items: array<wref<gameItemData>>;
    GameInstance.GetTransactionSystem(this.GetGameInstance()).GetItemList(object, items);
    let count = 0;
    for data in items {
      if IsDefined(data) && this.Matches(data, stack) {
        count += data.GetQuantity();
      }
    }
    return count;
  }

  // Takes up to `amount` of a stack's item out of an inventory; how many.
  private func RemoveFrom(object: ref<GameObject>, stack: ref<TandemWorldStack>, amount: Int32) -> Int32 {
    if !IsDefined(object) {
      return 0;
    }
    let ts = GameInstance.GetTransactionSystem(this.GetGameInstance());
    let items: array<wref<gameItemData>>;
    ts.GetItemList(object, items);
    let removed = 0;
    for data in items {
      if removed < amount && IsDefined(data) && this.Matches(data, stack) {
        let take = Min(amount - removed, data.GetQuantity());
        if ts.RemoveItem(object, data.GetID(), take) {
          removed += take;
        }
      }
    }
    return removed;
  }

  private func FindItem(object: ref<GameObject>, record: TweakDBID) -> ItemID {
    let items: array<wref<gameItemData>>;
    GameInstance.GetTransactionSystem(this.GetGameInstance()).GetItemList(object, items);
    for data in items {
      if IsDefined(data) && ItemID.GetTDBID(data.GetID()) == record {
        return data.GetID();
      }
    }
    return ItemID.None();
  }

  private func FirstLeft(item: ref<TandemWorldItem>) -> ref<TandemWorldStack> {
    for stack in item.stacks {
      if !stack.taken && !stack.ours {
        return stack;
      }
    }
    return null;
  }

  private func ObjectOf(item: ref<TandemWorldItem>) -> ref<GameObject> {
    if !EntityID.IsDefined(item.entity) {
      return null;
    }
    return GameInstance.FindEntityByID(this.GetGameInstance(), item.entity) as GameObject;
  }

  private func Find(id: Uint32) -> ref<TandemWorldItem> {
    for item in this.m_items {
      if item.id == id && id != 0u {
        return item;
      }
    }
    return null;
  }

  private func FindToken(token: Uint32) -> ref<TandemWorldItem> {
    for item in this.m_items {
      if item.ownDrop && item.token == token && item.id == 0u {
        return item;
      }
    }
    return null;
  }

  private func NameOf(player: Uint32) -> String {
    let name = TandemNative.PlayerName(player);
    return StrLen(name) > 0 ? name : s"player \(player)";
  }

  // The game's own simple on-screen message (as Pause.reds).
  private func Notice(text: String) -> Void {
    let message: SimpleScreenMessage;
    message.isShown = true;
    message.duration = 4.0;
    message.message = text;
    message.type = SimpleMessageType.Neutral;
    GameInstance
      .GetBlackboardSystem(this.GetGameInstance())
      .Get(GetAllBlackboardDefs().UI_Notifications)
      .SetVariant(GetAllBlackboardDefs().UI_Notifications.WarningMessage, ToVariant(message), true);
  }
}

// What a V drops from the backpack or the inventory: vanilla drops the
// queue when the menu closes; in a session it is dropped here, shared.
@wrapMethod(BackpackMainGameController)
protected cb func OnCloseMenu(userData: ref<IScriptable>) -> Bool {
  if TandemLoot.DropShared(this.m_player, this.m_itemDropQueue) {
    ArrayClear(this.m_itemDropQueue);
  }
  return wrappedMethod(userData);
}

@wrapMethod(gameuiInventoryGameController)
protected cb func OnCloseMenu(userData: ref<IScriptable>) -> Bool {
  let logic = this.m_itemModeLogicController;
  if IsDefined(logic) && TandemLoot.DropShared(this.m_player, logic.m_itemDropQueue) {
    ArrayClear(logic.m_itemDropQueue);
  }
  return wrappedMethod(userData);
}

// Everything that comes into our V's inventory: quest and important items
// go to every player (TandemLoot.Added).
@wrapMethod(PlayerPuppet)
protected cb func OnItemAddedToInventory(evt: ref<ItemAddedEvent>) -> Bool {
  let result = wrappedMethod(evt);
  TandemLoot.Added(this, evt.itemID, evt.itemData);
  return result;
}
