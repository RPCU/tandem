// How NPCs see the remote Vs: each puppet is in an attitude group of its own
// (tweaks/tandem/attitudes.tweak), never in V's `player` group, where an NPC
// turned hostile to one V turned hostile to every V (vanilla makes an NPC
// hostile to its target's whole group: AttitudeAgent.SetAttitudeTowardsAgentGroup;
// engine-knowledge §13.11).
//
// A group's relations are not part of its record: the attitude system holds
// one per pair of groups (AttitudeSystem.GetAttitudeRelation /
// SetAttitudeRelation, final.reds 174691-174704). Ours start as copies of
// V's group's, for every group seen on an NPC around V or a puppet:
// friendly to the friendly, neutral to the neutral, hostile to the hostile,
// so an ally never sees a stranger. When V's relation to a group changes (a
// quest making a faction friendly, or the police hostile) every puppet's
// follows. A puppet's own relation that changed while V's did not (a
// security system turned hostile to that puppet's group only, final.reds
// 114508) is left alone. Copies are not persistent: never saved.

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

  // Puts a puppet in a group, one of ours starting from V's relations as
  // they are now (a group held before by a player who left starts afresh).
  // Returns the group the puppet reads back.
  public func Put(body: ref<GameObject>, group: CName) -> CName {
    let agent = body.GetAttitudeAgent();
    if !IsDefined(agent) {
      return n"";
    }
    if TandemAttitudes.IsOurs(group) {
      this.Befriend(group);
      this.CopyAll(group);
    }
    agent.SetAttitudeGroup(group);
    return agent.GetAttitudeGroup();
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
        ArrayPush(around, npc);
      }
    }
    let added = "";
    for npc in around {
      let agent = npc.GetAttitudeAgent();
      if IsDefined(agent) {
        let group = agent.GetAttitudeGroup();
        if IsNameValid(group) && !TandemAttitudes.IsOurs(group) && NotEquals(group, vGroup) && !ArrayContains(this.m_seen, group) {
          ArrayPush(this.m_seen, group);
          ArrayPush(this.m_vTowards, -1);
          ArrayPush(this.m_towardsV, -1);
          added += s" \(NameToString(group))";
        }
      }
    }
    let system = GameInstance.GetAttitudeSystem(this.m_game);
    let active = this.Active();
    let changed = "";
    let i = 0;
    while i < ArraySize(this.m_seen) {
      let other = this.m_seen[i];
      let vTowards = EnumInt(system.GetAttitudeRelation(vGroup, other));
      let towardsV = EnumInt(system.GetAttitudeRelation(other, vGroup));
      if vTowards != this.m_vTowards[i] || towardsV != this.m_towardsV[i] {
        for group in active {
          system.SetAttitudeRelation(group, other, IntEnum<EAIAttitude>(vTowards));
          system.SetAttitudeRelation(other, group, IntEnum<EAIAttitude>(towardsV));
        }
        if this.m_vTowards[i] >= 0 {
          changed += s" \(NameToString(other)) \(vTowards)/\(towardsV)";
        }
        this.m_vTowards[i] = vTowards;
        this.m_towardsV[i] = towardsV;
      }
      i += 1;
    }
    if StrLen(added) > 0 {
      TandemNative.Log(s"attitudes: \(ArraySize(this.m_seen)) groups follow \(NameToString(vGroup))'s relations, new:\(added)");
    }
    if StrLen(changed) > 0 {
      TandemNative.Log(s"attitudes: \(NameToString(vGroup))'s relations changed, puppets follow:\(changed)");
    }
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
      system.SetAttitudeRelation(group, other, system.GetAttitudeRelation(vGroup, other));
      system.SetAttitudeRelation(other, group, system.GetAttitudeRelation(other, vGroup));
    }
  }
}
