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
// So a puppet's group gives it nothing by itself. It gets V's attitudes
// agent by agent:
// - when it is put in its group: SetAttitudeRelation from it to every
//   group seen on an NPC around V or a puppet, with V's group relation;
// - every 2 s, each NPC around V or a puppet: the attitudes it and V have
//   to each other, onto the puppet's agent (AttitudeAgent.SetAttitudeTowards),
//   once per NPC and puppet, again when the NPC's attitude to V changes (a
//   quest turning a faction friendly or the police hostile). What changed
//   between an NPC and a puppet alone stays (that V provoked it).
// Nothing is persistent: never saved.

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
    this.Befriend(TandemAttitudes.Shared());
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
    let vGroup = this.PlayerGroup();
    let around = player.GetNPCsAroundObject(60.0);
    for body in bodies {
      for npc in body.GetNPCsAroundObject(60.0) {
        if !ArrayContains(around, npc) {
          ArrayPush(around, npc);
        }
      }
    }
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
    this.CopyAgents(now, player, around, bodies);
  }

  // V's group relation to a group, between the agents in one of ours and
  // the agents in that group now (relations are symmetric: one call sets
  // both ways; the group table it does not touch is logged for reference).
  private func CopyRelation(ours: CName, other: CName, vTowards: Int32, towardsV: Int32, why: String) -> Void {
    let system = GameInstance.GetAttitudeSystem(this.m_game);
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
