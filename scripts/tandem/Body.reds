// V's bodies for the other players: V's own third-person template
// (player_{wa,ma}_tpp.ent, see tweaks/tandem/puppet.tweak) with NPC
// locomotion grafted on while the engine builds it, and a look applied once
// it exists.
//
// The graft (measured with the probe, docs/research/engine-knowledge.md §12):
// an NPC's walk/run clips live in the animation sets its
// entAnimationSetupExtensionComponents add ("Gameplay Animation Setup",
// "Special Locomotion Setup", "Fear Extension"...) and its root animated
// component runs the NPC graph. V's template has neither. At the start of a
// session a donor of each body (an NPC with the same skeleton) is spawned out
// of sight, its root graph, root sets and extension sets are captured in
// Codeware's Entity/Extract event, and it is deleted. Each puppet then gets,
// in the same event: the donor's graph, its root sets added to V's (under a
// new setup hash: the engine keys setups by it), and every donor extension
// set added to V's "Character Entity Animation Setup" extension (already
// bound to V's root).

public class TandemDonor {
  public let record: String;
  public let id: EntityID;
  public let ready: Bool;
  public let deleted: Bool;
  public let graph: ResourceRef;
  public let rootSets: array<animAnimSetupEntry>;
  public let extensionSets: array<animAnimSetupEntry>;
  // NPCStatesComponent's anim feature names (template data; V's template
  // leaves them empty, so its stance, upper body and high-level state never
  // reach the graph).
  public let stateFeatures: array<CName>;
  // The NPC's senseVisibleObjectComponent: what other NPCs' senses see.
  // V's TPP cutscene template has none, so without it a puppet is invisible
  // to every NPC (never detected, never a target: engine-knowledge §13.11).
  public let visible: ref<IComponent>;
  // Its senseComponent: NPC senses see "agents", objects with senses of
  // their own (IsAgentVisible stayed false with the visible object alone).
  public let senses: ref<IComponent>;
}

public class TandemBodies {
  private let m_game: GameInstance;
  private let m_startedAt: Float;
  private let m_female: ref<TandemDonor>;
  private let m_male: ref<TandemDonor>;
  // Codeware callbacks stay registered for the world's life: unregistering
  // while a job thread dispatches one hung the game (2026-11-17). Stopping
  // only turns the handler off.
  private let m_active: Bool;

  public static func FemaleRecord() -> String {
    return "Character.TandemPuppetWa";
  }

  public static func MaleRecord() -> String {
    return "Character.TandemPuppetMa";
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_active = true;
    this.m_startedAt = this.Now();
    this.m_female = new TandemDonor();
    this.m_female.record = "Character.TandemDonorWa";
    this.m_male = new TandemDonor();
    this.m_male.record = "Character.TandemDonorMa";

    let callbacks = GameInstance.GetCallbackSystem();
    for record in [this.m_female.record, this.m_male.record, TandemBodies.FemaleRecord(), TandemBodies.MaleRecord()] {
      callbacks.RegisterCallback(n"Entity/Extract", this, n"OnExtract")
        .AddTarget(EntityTarget.RecordID(TDBID.Create(record)));
    }
    this.SpawnDonor(this.m_female);
    this.SpawnDonor(this.m_male);
  }

  public func Stop() -> Void {
    this.m_active = false;
    this.DeleteDonor(this.m_female);
    this.DeleteDonor(this.m_male);
  }

  // Capture the donors again (after a hot reload), same callbacks.
  public func Recapture() -> Void {
    this.DeleteDonor(this.m_female);
    this.DeleteDonor(this.m_male);
    let female = new TandemDonor();
    female.record = this.m_female.record;
    let male = new TandemDonor();
    male.record = this.m_male.record;
    this.m_female = female;
    this.m_male = male;
    this.m_startedAt = this.Now();
    this.SpawnDonor(this.m_female);
    this.SpawnDonor(this.m_male);
  }

  // Both donors captured, or given up on (a puppet then glides).
  public func Ready() -> Bool {
    return (this.m_female.ready && this.m_male.ready) || this.Now() - this.m_startedAt > 15.0;
  }

  public func Tick() -> Void {
    if this.m_female.ready {
      this.DeleteDonor(this.m_female);
    }
    if this.m_male.ready {
      this.DeleteDonor(this.m_male);
    }
  }

  private func Now() -> Float {
    return EngineTime.ToFloat(GameInstance.GetSimTime(this.m_game));
  }

  // Below V, out of sight; it lives for a frame or two.
  private func SpawnDonor(donor: ref<TandemDonor>) -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let position = player.GetWorldPosition();
    position.Z -= 40.0;
    let spec = new DynamicEntitySpec();
    spec.recordID = TDBID.Create(donor.record);
    spec.position = position;
    spec.orientation = player.GetWorldOrientation();
    spec.alwaysSpawned = true;
    spec.persistState = false;
    spec.persistSpawn = false;
    spec.spawnInView = true;
    ArrayPush(spec.tags, n"Tandem.Donor");
    donor.id = GameInstance.GetDynamicEntitySystem().CreateEntity(spec);
    if !EntityID.IsDefined(donor.id) {
      TandemNative.Log(s"body: no donor \(donor.record)");
    }
  }

  private func DeleteDonor(donor: ref<TandemDonor>) -> Void {
    if IsDefined(donor) && !donor.deleted && EntityID.IsDefined(donor.id) {
      donor.deleted = true;
      GameInstance.GetDynamicEntitySystem().DeleteEntity(donor.id);
    }
  }

  // ---- the graft, on a job thread, before the entity is built ----

  private cb func OnExtract(event: ref<EntityBuilderEvent>) {
    if !this.m_active {
      return;
    }
    let builder = event.GetEntityBuilder();
    let id = builder.GetRecordID();
    let components = builder.GetTemplate().GetComponents();
    let appearance = builder.GetAppearance();
    if IsDefined(appearance) {
      for component in appearance.GetComponents() {
        ArrayPush(components, component);
      }
    }
    if Equals(id, TDBID.Create(this.m_female.record)) {
      this.Capture(this.m_female, components);
    } else if Equals(id, TDBID.Create(this.m_male.record)) {
      this.Capture(this.m_male, components);
    } else if Equals(id, TDBID.Create(TandemBodies.FemaleRecord())) {
      this.Graft("female", this.m_female, components);
      this.MakeVisible("female", this.m_female, builder);
    } else if Equals(id, TDBID.Create(TandemBodies.MaleRecord())) {
      this.Graft("male", this.m_male, components);
      this.MakeVisible("male", this.m_male, builder);
    }
  }

  private func Capture(donor: ref<TandemDonor>, components: array<ref<IComponent>>) -> Void {
    let root = TandemBodies.RootOf(components);
    if !IsDefined(root) {
      TandemNative.Log(s"body: donor \(donor.record) has no root animated component");
      return;
    }
    donor.graph = root.graph;
    donor.rootSets = root.animations.gameplay;
    ArrayClear(donor.extensionSets);
    for component in components {
      let extension = component as entAnimationSetupExtensionComponent;
      if IsDefined(extension) {
        for entry in extension.animations.gameplay {
          ArrayPush(donor.extensionSets, entry);
        }
      }
    }
    donor.visible = null;
    donor.senses = null;
    for component in components {
      if Equals(component.GetClassName(), n"senseVisibleObjectComponent") {
        donor.visible = component;
      } else if Equals(component.GetClassName(), n"senseComponent") {
        donor.senses = component;
      }
    }
    ArrayClear(donor.stateFeatures);
    let states = TandemBodies.StatesOf(components);
    if IsDefined(states) {
      donor.stateFeatures = (states as NPCStatesComponent).TandemFeatureNames();
    }
    // Which wrappers gate the donor's sets (inCrouch, combatLocomotion…).
    let gates = "";
    for entry in donor.extensionSets {
      for variable in entry.variableNames {
        if !StrContains(gates, NameToString(variable)) {
          gates += s" \(NameToString(variable))";
        }
      }
    }
    TandemNative.Log(s"body: donor \(donor.record) set gates:\(gates)");
    donor.ready = true;
    TandemNative.Log(s"body: donor \(donor.record) captured, \(ArraySize(donor.rootSets)) root and \(ArraySize(donor.extensionSets)) extension sets, state features \(TandemBodies.Names(donor.stateFeatures)), visible object \(IsDefined(donor.visible))");
  }

  // A puppet is seen by NPCs as its V is by them: the donor's visible
  // object, cloned into the body being built.
  private func MakeVisible(body: String, donor: ref<TandemDonor>, builder: ref<EntityBuilderWrapper>) -> Void {
    if !donor.ready || !IsDefined(donor.visible) {
      TandemNative.Log(s"body: no visible object for the \(body) body: NPCs will not see it");
      return;
    }
    builder.GetTemplate().AddComponent(donor.visible.Clone() as IComponent);
    if IsDefined(donor.senses) {
      builder.GetTemplate().AddComponent(donor.senses.Clone() as IComponent);
    }
    TandemNative.Log(s"body: \(body) body visible to NPCs (senses \(IsDefined(donor.senses)))");
  }

  private func Graft(body: String, donor: ref<TandemDonor>, components: array<ref<IComponent>>) -> Void {
    if !donor.ready {
      TandemNative.Log(s"body: no donor for the \(body) body yet, it will glide");
      return;
    }
    let root = TandemBodies.RootOf(components);
    if !IsDefined(root) {
      TandemNative.Log(s"body: the \(body) body has no root animated component");
      return;
    }
    let setup = root.animations;
    for entry in donor.rootSets {
      ArrayPush(setup.gameplay, entry);
    }
    setup.hash = setup.hash + Cast<Uint64>(7919);
    root.animations = setup;
    root.graph = donor.graph;

    let states = TandemBodies.StatesOf(components);
    if IsDefined(states) && ArraySize(donor.stateFeatures) == 3 {
      (states as NPCStatesComponent).TandemSetFeatureNames(donor.stateFeatures);
    }

    for component in components {
      let extension = component as entAnimationSetupExtensionComponent;
      if IsDefined(extension) && StrContains(NameToString(extension.GetName()), "Character") {
        let sets = extension.animations;
        for entry in donor.extensionSets {
          ArrayPush(sets.gameplay, entry);
        }
        sets.hash = sets.hash + Cast<Uint64>(7919);
        extension.animations = sets;
        TandemNative.Log(s"body: \(body) body grafted from \(donor.record)");
        return;
      }
    }
    TandemNative.Log(s"body: the \(body) body has no Character extension, locomotion sets missing");
  }

  public static func StateFeatureFields() -> array<CName> {
    return [n"m_stanceAnimFeatureName", n"m_upperBodyAnimFeatureName", n"m_highLevelAnimFeatureName"];
  }

  public static func StatesOf(components: array<ref<IComponent>>) -> ref<IComponent> {
    for component in components {
      if IsDefined(component as NPCStatesComponent) {
        return component;
      }
    }
    return null;
  }

  public static func Names(names: array<CName>) -> String {
    let text = "";
    for name in names {
      text += s" \(NameToString(name))";
    }
    return text;
  }

  public static func RootOf(components: array<ref<IComponent>>) -> ref<AnimatedComponent> {
    for component in components {
      let animated = component as AnimatedComponent;
      if IsDefined(animated) && Equals(animated.GetName(), n"root") {
        return animated;
      }
    }
    return null;
  }
}

// The anim feature names an NPC's state machine applies its stance, upper
// body and high-level state under (private fields, template data).
@addMethod(NPCStatesComponent)
public func TandemFeatureNames() -> array<CName> {
  return [this.m_stanceAnimFeatureName, this.m_upperBodyAnimFeatureName, this.m_highLevelAnimFeatureName];
}

@addMethod(NPCStatesComponent)
public func TandemSetFeatureNames(names: array<CName>) -> Void {
  this.m_stanceAnimFeatureName = names[0];
  this.m_upperBodyAnimFeatureName = names[1];
  this.m_highLevelAnimFeatureName = names[2];
}
