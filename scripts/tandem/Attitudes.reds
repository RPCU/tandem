// How NPCs see the remote Vs: each puppet is in an attitude group of its own
// (tweaks/tandem/attitudes.tweak), never in V's `player` group, where an NPC
// turned hostile to one V turned hostile to every V (vanilla makes an NPC
// hostile to its target's whole group: AttitudeAgent.SetAttitudeTowardsAgentGroup;
// engine-knowledge §13.11).
//
// What the attitude system does (game 2.50 exe, engine-knowledge §13.17):
// - GetAttitudeRelation(a, b) reads the group table: TweakDB's relations
//   and the persistent overrides, symmetric. A group with no relation
//   record reads friendly.
// - SetAttitudeRelation(a, b, x) does not write that table: it sets x
//   between every agent now in group a and every agent now in group b
//   (per-agent pairs, both ways). Agents that join either group later do
//   not get it, and GetAttitudeRelation never shows it.
// So the group table must hold the puppets' relations from the start: the
// player's own Attitude records are cloned for each group of ours as the
// tweaks load (AttitudeTweak.reds). Start reads the table back and logs
// whether the engine took them ("attitudes: declared relations ...").
// What the records cannot give is copied at run time, agent by agent:
// - a relation of V's that changed since the records (a quest's or a
//   security system's persistent change), or every relation when the
//   records did not take: SetAttitudeRelation between a puppet's group and
//   each group seen on an NPC around V or a puppet;
// - each NPC, as it and V see each other, onto the puppet's agent
//   (AttitudeAgent.SetAttitudeTowards) where that differs from what the
//   groups give: on the tick after the NPC came into the world
//   (NPCPuppet.OnGameAttached, queued), and every 2 s for what changed
//   since (a quest turning one NPC friendly). What changed between an NPC
//   and a puppet alone stays (that V provoked it).
// Nothing is persistent: never saved.

// An NPC came into our world. It may run on a job thread while the world
// streams in: queued only, taken by TandemAttitudes.Spawned on the tick.
@wrapMethod(NPCPuppet)
protected cb func OnGameAttached() -> Bool {
  let result = wrappedMethod();
  TandemStreaming.QueueStreamed(TandemStreamKind.Npc(), this.GetEntityID(), n"None", true);
  return result;
}

public class TandemNpcAttitude {
  public let npc: EntityID;
  public let body: EntityID;
  // EAIAttitude values (-1: not yet).
  public let toV: Int32;
  public let fromV: Int32;
  public let seenAt: Float;
}

public class TandemAttitudes {
  private let m_game: GameInstance;
  // The player each of Groups() is held by (0: free).
  private let m_holders: array<Uint32>;
  // The groups seen on NPCs, with V's relations to each as last copied:
  // V's towards it, and its towards V (EAIAttitude values; -1: not yet).
  private let m_seen: array<CName>;
  private let m_vTowards: array<Int32>;
  private let m_towardsV: array<Int32>;
  private let m_lastScan: Float;
  // Each NPC around and each puppet, with the NPC's attitudes to and from V
  // as last copied onto that puppet.
  private let m_pairs: array<ref<TandemNpcAttitude>>;
  // NPCs that came into the world next to a puppet, by what the engine's
  // own relations gave the puppet on our first tick with them: V's
  // attitudes, or not (then copied).
  private let m_freshSame: Int32;
  private let m_freshDiffer: Int32;
  private let m_lastFresh: EntityID;
  // The NPC queue (TandemStreamKind.Npc), taken here and split: the NPCs
  // that came into the world (ours, Spawned), and what the game changed on
  // one (NpcAnims.reds: its states, weapon, workspot), kept for TakeChanged.
  private let m_came: array<TandemStreamed>;
  private let m_changed: array<TandemStreamed>;

  // One per puppet at a time (3 guests at most, core endpoint.hpp kMaxGuests).
  public static func Groups() -> array<CName> {
    return [n"tandem_player_1", n"tandem_player_2", n"tandem_player_3"];
  }

  // The puppet records' own (tweaks/tandem/puppet.tweak), and every
  // puppet's beyond Groups().
  public static func Shared() -> CName {
    return n"tandem_player";
  }

  public static func IsOurs(group: CName) -> Bool {
    return StrBeginsWith(NameToString(group), "tandem_player");
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_holders = [0u, 0u, 0u];
    let declared = 0;
    for id in [t"Attitudes.Group_TandemPlayer", t"Attitudes.Group_TandemPlayer1", t"Attitudes.Group_TandemPlayer2", t"Attitudes.Group_TandemPlayer3"] {
      if IsDefined(TweakDBInterface.GetAttitudeGroupRecord(id)) {
        declared += 1;
      }
    }
    TandemNative.Log(s"attitudes: \(declared) of 4 puppet groups declared");
    TandemStreaming.TakeStreamed(TandemStreamKind.Npc());
    ArrayClear(this.m_came);
    ArrayClear(this.m_changed);
    this.Befriend(TandemAttitudes.Shared());
    TandemNative.Log(this.Declared());
  }

  // Whether the engine's group table holds the relations AttitudeTweak.reds
  // declared: every vanilla group's relation to `player` next to its
  // relation to the first group of ours, both read from the table
  // (GetAttitudeRelation never shows what the runtime copy sets). A group
  // of ours with no relation reads friendly (0), so the groups that tell are
  // those `player` is not friendly with. One line, for the log and for
  // "puppet relations" (test).
  public func Declared() -> String {
    let read = TweakDBInterface.GetInt(TandemAttitudeRelations.ReadFlat(), -1);
    let cloned = TweakDBInterface.GetInt(TandemAttitudeRelations.MirroredFlat(), -1);
    let tweak = s"tweak: \(read) Attitude records read, \(TweakDBInterface.GetInt(TandemAttitudeRelations.PlayersFlat(), -1)) with player, \(cloned) cloned";
    if read < 0 {
      tweak = "tweak: never ran";
    }
    let system = GameInstance.GetAttitudeSystem(this.m_game);
    let all = TandemAttitudes.Groups();
    let ours = all[0];
    let groups = TandemAttitudeRelations.RecordsOf("AttitudeGroup");
    let telling = 0;
    let same = 0;
    let differ = "";
    for record in groups {
      let group = record as AttitudeGroup_Record;
      let name = n"None";
      if IsDefined(group) {
        name = group.Name();
      }
      if IsNameValid(name) && !TandemAttitudes.IsOurs(name) && NotEquals(name, n"player") {
        let players = EnumInt(system.GetAttitudeRelation(n"player", name));
        let puppets = EnumInt(system.GetAttitudeRelation(ours, name));
        if players != 0 {
          telling += 1;
        }
        if players == puppets && players != 0 {
          same += 1;
        } else if players != puppets && StrLen(differ) < 120 {
          differ += s" \(NameToString(name)) \(players)/\(puppets)";
        }
      }
    }
    let verdict = "unknown (no group player is not friendly with)";
    if telling > 0 && same == telling {
      verdict = "took";
    } else if telling > 0 && same == 0 {
      verdict = "did NOT take (the runtime copy carries them)";
    } else if telling > 0 {
      verdict = "took in part";
    }
    let line = s"attitudes: declared relations \(verdict): \(same) of \(telling) groups player is not friendly with read the same for \(NameToString(ours)), of \(ArraySize(groups)) groups (\(tweak))";
    if StrLen(differ) > 0 {
      line += s"; player/\(NameToString(ours)) differ:\(differ)";
    }
    return line;
  }

  // (test query attitude ...) "V a/b puppet c/d": how V and a puppet stand
  // with a group (the engine's group table: the group to it / it to the
  // group) or with an NPC (agent to agent: the NPC to it / it to the NPC);
  // EAIAttitude values, 0 friendly, 1 neutral, 2 hostile.
  public func QueryGroup(group: CName, puppet: CName) -> String {
    let system = GameInstance.GetAttitudeSystem(this.m_game);
    let vGroup = this.PlayerGroup();
    return s"V \(EnumInt(system.GetAttitudeRelation(group, vGroup)))/\(EnumInt(system.GetAttitudeRelation(vGroup, group))) puppet \(EnumInt(system.GetAttitudeRelation(group, puppet)))/\(EnumInt(system.GetAttitudeRelation(puppet, group)))";
  }

  public func QueryNpc(npc: ref<GameObject>, body: ref<GameObject>) -> String {
    let player = GetPlayer(this.m_game);
    if !IsDefined(npc) || !IsDefined(body) || !IsDefined(player) || !IsDefined(npc.GetAttitudeAgent()) || !IsDefined(body.GetAttitudeAgent()) {
      return "none";
    }
    let group = npc.GetAttitudeAgent().GetAttitudeGroup();
    return s"V \(EnumInt(GameObject.GetAttitudeBetween(npc, player)))/\(EnumInt(GameObject.GetAttitudeBetween(player, npc))) puppet \(EnumInt(GameObject.GetAttitudeBetween(npc, body)))/\(EnumInt(GameObject.GetAttitudeBetween(body, npc))) (\(NameToString(group)))";
  }

  // (test query attitude fresh) the NPCs that came next to a puppet since
  // the session began, by what the engine alone gave the puppet, and the
  // last one (QueryNpc reads it: "test query attitude npc fresh").
  public func QueryFresh() -> String {
    return s"\(this.m_freshSame) same \(this.m_freshDiffer) differ";
  }

  public func LastFresh() -> EntityID {
    return this.m_lastFresh;
  }

  // The NPC queue since the last call, each event to its reader's list.
  private func Drain() -> Void {
    let streamed = TandemStreaming.TakeStreamed(TandemStreamKind.Npc());
    for event in streamed {
      if event.attached {
        ArrayPush(this.m_came, event);
      } else {
        ArrayPush(this.m_changed, event);
      }
    }
    // Nobody takes the changes (no session, not recording): they are of no
    // use later.
    if ArraySize(this.m_changed) > 4096 {
      ArrayClear(this.m_changed);
    }
  }

  // What the game changed on NPCs since the last call (cls: what; queued by
  // the wraps of NpcAnims.reds), in order. Game thread.
  public func TakeChanged() -> array<TandemStreamed> {
    this.Drain();
    let taken = this.m_changed;
    ArrayClear(this.m_changed);
    return taken;
  }

  // Each tick: the NPCs that came into the world since the last one get
  // V's attitudes on every puppet now, not at the next 2 s scan. Logged
  // (debug) per NPC and puppet: what the engine's relations alone gave
  // ("engine alone": the declared relations at work), and what stands after
  // the copy, which is what the NPC's first decisions read.
  public func Spawned(now: Float, bodies: array<ref<GameObject>>) -> Void {
    this.Drain();
    let streamed = this.m_came;
    ArrayClear(this.m_came);
    let player = GetPlayer(this.m_game);
    if ArraySize(streamed) == 0 || ArraySize(bodies) == 0 || !IsDefined(player) {
      return;
    }
    let vAgent = player.GetAttitudeAgent();
    let fresh: array<ref<NPCPuppet>>;
    for event in streamed {
      let npc = GameInstance.FindEntityByID(this.m_game, event.entity) as NPCPuppet;
      if IsDefined(npc) && IsDefined(npc.GetAttitudeAgent()) && !npc.IsPrevention() && !ArrayContains(fresh, npc) {
        let group = npc.GetAttitudeAgent().GetAttitudeGroup();
        if !TandemAttitudes.IsOurs(group) {
          ArrayPush(fresh, npc);
        }
      }
    }
    if ArraySize(fresh) == 0 || !IsDefined(vAgent) {
      return;
    }
    let before: array<Bool>;
    for npc in fresh {
      for body in bodies {
        ArrayPush(before, this.AsV(npc, vAgent, body));
      }
    }
    this.See(fresh);
    this.CopyAgents(now, player, fresh, bodies);
    let i = 0;
    for npc in fresh {
      this.m_lastFresh = npc.GetEntityID();
      for body in bodies {
        let after = this.AsV(npc, vAgent, body);
        if before[i] {
          this.m_freshSame += 1;
        } else {
          this.m_freshDiffer += 1;
        }
        let group = npc.GetAttitudeAgent().GetAttitudeGroup();
        TandemNative.LogDebug(s"attitudes: fresh npc \(EntityID.ToDebugStringDecimal(npc.GetEntityID())) (\(NameToString(group))) to V \(EnumInt(npc.GetAttitudeAgent().GetAttitudeTowards(vAgent))), puppet \(EntityID.ToDebugStringDecimal(body.GetEntityID())) engine alone: \(before[i] ? "same" : "differs"), on its first tick: \(after ? "same" : "differs")");
        i += 1;
      }
    }
  }

  // Whether an NPC and a puppet see each other as the NPC and V do.
  private func AsV(npc: ref<NPCPuppet>, vAgent: ref<AttitudeAgent>, body: ref<GameObject>) -> Bool {
    let npcAgent = npc.GetAttitudeAgent();
    let bodyAgent = body.GetAttitudeAgent();
    if !IsDefined(bodyAgent) || !TandemAttitudes.IsOurs(bodyAgent.GetAttitudeGroup()) {
      return true;
    }
    return Equals(npcAgent.GetAttitudeTowards(bodyAgent), npcAgent.GetAttitudeTowards(vAgent)) && Equals(bodyAgent.GetAttitudeTowards(npcAgent), vAgent.GetAttitudeTowards(npcAgent));
  }

  // A group for this player's puppet: the one it holds, a free one, or the
  // shared one when all are taken.
  public func Claim(player: Uint32) -> CName {
    let groups = TandemAttitudes.Groups();
    let i = 0;
    while i < ArraySize(groups) {
      if this.m_holders[i] == player {
        return groups[i];
      }
      i += 1;
    }
    i = 0;
    while i < ArraySize(groups) {
      if this.m_holders[i] == 0u {
        this.m_holders[i] = player;
        return groups[i];
      }
      i += 1;
    }
    return TandemAttitudes.Shared();
  }

  public func Release(player: Uint32) -> Void {
    let i = 0;
    while i < ArraySize(this.m_holders) {
      if this.m_holders[i] == player {
        this.m_holders[i] = 0u;
      }
      i += 1;
    }
  }

  // Puts a puppet in a group, then, in one of ours, gives it V's
  // relations towards the agents around now (SetAttitudeRelation only
  // reaches agents already in the group: the puppet must be in it first).
  // Returns the group the puppet reads back.
  public func Put(body: ref<GameObject>, group: CName) -> CName {
    let agent = body.GetAttitudeAgent();
    if !IsDefined(agent) {
      return n"";
    }
    agent.SetAttitudeGroup(group);
    let now = agent.GetAttitudeGroup();
    if TandemAttitudes.IsOurs(now) {
      this.Befriend(now);
      this.CopyAll(now);
    }
    return now;
  }

  // Every 2 s while there are puppets: new groups around V and the puppets,
  // and V's relations that changed.
  public func Tick(now: Float, bodies: array<ref<GameObject>>) -> Void {
    if now - this.m_lastScan < 2.0 {
      return;
    }
    this.m_lastScan = now;
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let around = player.GetNPCsAroundObject(60.0);
    for body in bodies {
      let near = body.GetNPCsAroundObject(60.0);
      for npc in near {
        if !ArrayContains(around, npc) {
          ArrayPush(around, npc);
        }
      }
    }
    this.See(around);
    this.CopyAgents(now, player, around, bodies);
  }

  // The groups of these NPCs that are new to us, and V's relation to every
  // group seen, where it is new or changed, between the agents of that
  // group and of ours.
  private func See(around: array<ref<NPCPuppet>>) -> Void {
    let vGroup = this.PlayerGroup();
    let added = "";
    for npc in around {
      let agent = npc.GetAttitudeAgent();
      if IsDefined(agent) {
        let npcGroup = agent.GetAttitudeGroup();
        // The police look at each V as its own heat says (Police.reds):
        // V's relation to them is never a puppet's.
        if IsNameValid(npcGroup) && !TandemAttitudes.IsOurs(npcGroup) && NotEquals(npcGroup, vGroup) && NotEquals(npcGroup, n"police") && !npc.IsPrevention() && !ArrayContains(this.m_seen, npcGroup) {
          ArrayPush(this.m_seen, npcGroup);
          ArrayPush(this.m_vTowards, -1);
          ArrayPush(this.m_towardsV, -1);
          added += s" \(NameToString(npcGroup))";
        }
      }
    }
    if StrLen(added) > 0 {
      TandemNative.Log(s"attitudes: \(ArraySize(this.m_seen)) groups follow \(NameToString(vGroup))'s relations, new:\(added)");
    }
    let system = GameInstance.GetAttitudeSystem(this.m_game);
    let active = this.Active();
    let i = 0;
    while i < ArraySize(this.m_seen) {
      let other = this.m_seen[i];
      let vTowards = EnumInt(system.GetAttitudeRelation(vGroup, other));
      let towardsV = EnumInt(system.GetAttitudeRelation(other, vGroup));
      if vTowards != this.m_vTowards[i] || towardsV != this.m_towardsV[i] {
        let why = this.m_vTowards[i] < 0 ? "new" : "V's changed";
        for ours in active {
          this.CopyRelation(ours, other, vTowards, towardsV, why);
        }
        this.m_vTowards[i] = vTowards;
        this.m_towardsV[i] = towardsV;
      }
      i += 1;
    }
  }

  // V's group relation to a group, between the agents in one of ours and
  // the agents in that group now (relations are symmetric: one call sets
  // both ways; the group table it does not touch is logged for reference).
  private func CopyRelation(ours: CName, other: CName, vTowards: Int32, towardsV: Int32, why: String) -> Void {
    let system = GameInstance.GetAttitudeSystem(this.m_game);
    // A group met for the first time whose declared relation is V's
    // already: nothing to set (the engine gives it to every agent, those
    // to come too).
    if Equals(why, "new") && EnumInt(system.GetAttitudeRelation(ours, other)) == vTowards && EnumInt(system.GetAttitudeRelation(other, ours)) == towardsV {
      return;
    }
    system.SetAttitudeRelation(ours, other, IntEnum<EAIAttitude>(vTowards));
    TandemNative.Log(s"attitudes: \(NameToString(ours)) / \(NameToString(other)) (\(why)): V's \(vTowards)/\(towardsV) set between their agents (group table \(EnumInt(system.GetAttitudeRelation(ours, other))))");
  }

  // Each NPC around, as it sees V and V sees it, onto each puppet's agent
  // where the groups alone do not give it (a relation the attitude system
  // did not take, or an NPC's own attitude to V that a quest set): once per
  // NPC and puppet, then whenever the NPC's attitude to V changes. What
  // changed between an NPC and a puppet alone stays.
  private func CopyAgents(now: Float, player: ref<GameObject>, around: array<ref<NPCPuppet>>, bodies: array<ref<GameObject>>) -> Void {
    let vAgent = player.GetAttitudeAgent();
    if !IsDefined(vAgent) {
      return;
    }
    let copied = 0;
    for body in bodies {
      let bodyAgent = body.GetAttitudeAgent();
      if IsDefined(bodyAgent) && TandemAttitudes.IsOurs(bodyAgent.GetAttitudeGroup()) {
        for npc in around {
          let npcAgent = npc.GetAttitudeAgent();
          if IsDefined(npcAgent) && !TandemAttitudes.IsOurs(npcAgent.GetAttitudeGroup()) && !npc.IsPrevention() {
            let toV = npcAgent.GetAttitudeTowards(vAgent);
            let fromV = vAgent.GetAttitudeTowards(npcAgent);
            let pair = this.Pair(npc.GetEntityID(), body.GetEntityID());
            pair.seenAt = now;
            if EnumInt(toV) != pair.toV || EnumInt(fromV) != pair.fromV {
              if NotEquals(npcAgent.GetAttitudeTowards(bodyAgent), toV) {
                npcAgent.SetAttitudeTowards(bodyAgent, toV);
                copied += 1;
              }
              if NotEquals(bodyAgent.GetAttitudeTowards(npcAgent), fromV) {
                bodyAgent.SetAttitudeTowards(npcAgent, fromV);
                copied += 1;
              }
              pair.toV = EnumInt(toV);
              pair.fromV = EnumInt(fromV);
            }
          }
        }
      }
    }
    // NPCs gone for 30 s are forgotten (back in range, they start from V's again).
    let kept: array<ref<TandemNpcAttitude>>;
    for pair in this.m_pairs {
      if now - pair.seenAt < 30.0 {
        ArrayPush(kept, pair);
      }
    }
    this.m_pairs = kept;
    if copied > 0 {
      TandemNative.Log(s"attitudes: \(copied) NPC attitudes copied from V's onto puppets");
    }
  }

  private func Pair(npc: EntityID, body: EntityID) -> ref<TandemNpcAttitude> {
    for pair in this.m_pairs {
      if pair.npc == npc && pair.body == body {
        return pair;
      }
    }
    let pair = new TandemNpcAttitude();
    pair.npc = npc;
    pair.body = body;
    pair.toV = -1;
    pair.fromV = -1;
    ArrayPush(this.m_pairs, pair);
    return pair;
  }

  // V's group (quests may move V to another).
  private func PlayerGroup() -> CName {
    let player = GetPlayer(this.m_game);
    if IsDefined(player) && IsDefined(player.GetAttitudeAgent()) {
      return player.GetAttitudeAgent().GetAttitudeGroup();
    }
    return n"player";
  }

  // Our groups in use: the held ones and the shared one.
  private func Active() -> array<CName> {
    let groups = TandemAttitudes.Groups();
    let active: array<CName>;
    let i = 0;
    while i < ArraySize(groups) {
      if this.m_holders[i] != 0u {
        ArrayPush(active, groups[i]);
      }
      i += 1;
    }
    ArrayPush(active, TandemAttitudes.Shared());
    return active;
  }

  // Vs are friends: V and every puppet.
  private func Befriend(group: CName) -> Void {
    let system = GameInstance.GetAttitudeSystem(this.m_game);
    let vGroup = this.PlayerGroup();
    system.SetAttitudeRelation(group, vGroup, EAIAttitude.AIA_Friendly);
    system.SetAttitudeRelation(vGroup, group, EAIAttitude.AIA_Friendly);
    let ours = TandemAttitudes.Groups();
    ArrayPush(ours, TandemAttitudes.Shared());
    for other in ours {
      system.SetAttitudeRelation(group, other, EAIAttitude.AIA_Friendly);
      system.SetAttitudeRelation(other, group, EAIAttitude.AIA_Friendly);
    }
  }

  private func CopyAll(group: CName) -> Void {
    let system = GameInstance.GetAttitudeSystem(this.m_game);
    let vGroup = this.PlayerGroup();
    for other in this.m_seen {
      this.CopyRelation(group, other, EnumInt(system.GetAttitudeRelation(vGroup, other)), EnumInt(system.GetAttitudeRelation(other, vGroup)), "puppet put in");
    }
  }
}
