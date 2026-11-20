// The probe (tandem.ini probe = on), a measuring tool, not part of a session:
// spawns bodies in rows in front of V, logs each one's components, animation
// setup and locomotion clip lengths, dresses V's bodies in the local V's look,
// and grafts a donor's NPC locomotion onto them, to find a body that looks
// like V and walks like an NPC. The graft happens in Codeware's
// Entity/Extract event, before the engine builds the entity.
//
// Row 1 (5 m): the female donor (Panam) and male donor candidates.
// Row 2 (9 m): V's bodies, plain and grafted.

public class TandemProbeBody {
  public let record: String;
  public let id: EntityID;
  public let spawnedAt: Float;
  public let measured: Bool;
  public let dressed: Bool;
}

public class TandemProbe {
  private let m_game: GameInstance;
  private let m_startedAt: Float;
  private let m_bodies: array<ref<TandemProbeBody>>;
  private let m_spawnedRest: Bool;
  private let m_stopped: Bool;
  // The donors' root animated component, captured on their extract.
  private let m_femaleReady: Bool;
  private let m_femaleGraph: ResourceRef;
  private let m_femaleSetup: animAnimSetup;
  private let m_femaleActions: ResourceRef;
  private let m_femaleDatabases: animAnimDatabaseCollection;
  private let m_femaleExtensions: array<animAnimSetupEntry>;
  private let m_maleReady: Bool;
  private let m_maleDonor: String;
  private let m_maleGraph: ResourceRef;
  private let m_maleSetup: animAnimSetup;
  private let m_maleActions: ResourceRef;
  private let m_maleDatabases: animAnimDatabaseCollection;
  private let m_maleExtensions: array<animAnimSetupEntry>;

  public static func FemaleDonor() -> String {
    return "Character.TandemPuppetTest";
  }

  // Story NPCs that may share male V's skeleton; the ones that exist spawn.
  public static func MaleCandidates() -> array<String> {
    return [
      "Character.Jackie", "Character.Kerry_Eurodyne", "Character.River_Ward",
      "Character.Viktor_Vektor", "Character.Goro_Takemura", "Character.Takemura",
      "Character.Johnny", "Character.Mitch", "Character.Saul", "Character.Dex",
      "Character.Viktor", "Character.River", "Character.Kerry"
    ];
  }

  // male V's root rig hash (measured: player_ma_tpp*.ent), as text: the
  // number is past what a redscript literal takes.
  public static func MaleRig() -> String {
    return "14264052059588999261";
  }

  public static func Bodies() -> array<String> {
    return [
      "Character.TandemProbeMaTpp",
      "Character.TandemProbeMaTppC", "Character.TandemProbeMaTppD",
      "Character.TandemProbeWaTppC", "Character.TandemProbeWaTppD",
      "Character.TandemProbeMaTppE", "Character.TandemProbeMaTppF"
    ];
  }

  // 0 none, 3 the donor's whole setup and graph, 4 V's sets plus the
  // donor's under a new setup hash, and the donor's graph.
  public static func GraftOf(record: String) -> Int32 {
    if StrEndsWith(record, "TppC") || StrEndsWith(record, "TppE") {
      return 3;
    }
    if StrEndsWith(record, "TppD") || StrEndsWith(record, "TppF") {
      return 4;
    }
    return 0;
  }

  // E and F graft once the entity is assembled instead of at extract.
  public static func AtAssemble(record: String) -> Bool {
    return StrEndsWith(record, "TppE") || StrEndsWith(record, "TppF");
  }

  public static func IsMaleBody(record: String) -> Bool {
    return StrContains(record, "PuppetMa") || StrContains(record, "ProbeMa");
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_startedAt = this.Now();
    let callbacks = GameInstance.GetCallbackSystem();
    let watched = TandemProbe.Bodies();
    ArrayPush(watched, TandemProbe.FemaleDonor());
    let slot = 0;
    this.Spawn(TandemProbe.FemaleDonor(), slot, 5.0);
    for record in TandemProbe.MaleCandidates() {
      if IsDefined(TweakDBInterface.GetCharacterRecord(TDBID.Create(record))) {
        slot += 1;
        ArrayPush(watched, record);
        this.Spawn(record, slot, 5.0);
      } else {
        TandemNative.Log(s"probe \(record): no such record");
      }
    }
    for record in watched {
      callbacks.RegisterCallback(n"Entity/Extract", this, n"OnExtract")
        .AddTarget(EntityTarget.RecordID(TDBID.Create(record)));
      if TandemProbe.AtAssemble(record) {
        callbacks.RegisterCallback(n"Entity/Assemble", this, n"OnAssemble")
          .AddTarget(EntityTarget.RecordID(TDBID.Create(record)));
      }
    }
  }

  public func Stop() -> Void {
    // (callbacks stay registered: see TandemBodies.m_active)
    this.m_stopped = true;
    for body in this.m_bodies {
      GameInstance.GetDynamicEntitySystem().DeleteEntity(body.id);
    }
    ArrayClear(this.m_bodies);
  }

  private func Now() -> Float {
    return EngineTime.ToFloat(GameInstance.GetSimTime(this.m_game));
  }

  public func Tick() -> Void {
    let now = this.Now();
    if this.m_femaleReady && !this.m_spawnedRest && (this.m_maleReady || now - this.m_startedAt > 8.0) {
      this.m_spawnedRest = true;
      TandemNative.Log(s"probe: spawning V's bodies (male donor: \(this.m_maleReady ? this.m_maleDonor : "none, Panam instead"))");
      let slot = 0;
      for record in TandemProbe.Bodies() {
        this.Spawn(record, slot, 9.0);
        slot += 1;
      }
    }

    let player = GetPlayer(this.m_game);
    let localMale = IsDefined(player) && Equals(player.GetResolvedGenderName(), n"Male");
    let entities = GameInstance.GetDynamicEntitySystem();
    for body in this.m_bodies {
      let entity = entities.GetEntity(body.id);
      if IsDefined(entity) {
        if !body.dressed && StrContains(body.record, "Tandem") && !Equals(body.record, TandemProbe.FemaleDonor())
            && Equals(TandemProbe.IsMaleBody(body.record), localMale) {
          body.dressed = true;
          let state = GameInstance.GetCharacterCustomizationSystem(this.m_game).GetState();
          TandemNative.Log(s"probe \(body.record): look, \(TandemNative.ApplyLook(entity, state, localMale)) keys");
        }
        if !body.measured && now - body.spawnedAt > 4.0 {
          body.measured = true;
          TandemNative.Log(s"probe \(body.record): clips \(TandemNative.ClipLengths(entity))");
          let live = this.RootOf(entity.GetComponents());
          if IsDefined(live) {
            this.Describe(body.record, " live", live);
          }
        }
      }
    }
  }

  // Slot 0 in front of V, the others alternating left and right of it.
  private func Spawn(record: String, slot: Int32, distance: Float) -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let orientation = player.GetWorldOrientation();
    let side = Cast<Float>((slot + 1) / 2) * 1.3;
    if slot % 2 == 1 {
      side = -side;
    }
    let position = player.GetWorldPosition() + player.GetWorldForward() * distance
      + Quaternion.GetRight(orientation) * side;

    let facing = Quaternion.ToEulerAngles(orientation);
    facing.Yaw += 180.0;

    let spec = new DynamicEntitySpec();
    spec.recordID = TDBID.Create(record);
    spec.position = position;
    spec.orientation = EulerAngles.ToQuat(facing);
    spec.alwaysSpawned = true;
    spec.persistState = false;
    spec.persistSpawn = false;
    spec.spawnInView = true;
    spec.active = true;
    ArrayPush(spec.tags, n"Tandem.Probe");

    let body = new TandemProbeBody();
    body.record = record;
    body.id = GameInstance.GetDynamicEntitySystem().CreateEntity(spec);
    body.spawnedAt = this.Now();
    if !EntityID.IsDefined(body.id) {
      TandemNative.Log(s"probe \(record): could not spawn");
      return;
    }
    ArrayPush(this.m_bodies, body);
  }

  private func NameOf(id: TweakDBID) -> String {
    let all = TandemProbe.Bodies();
    ArrayPush(all, TandemProbe.FemaleDonor());
    for record in TandemProbe.MaleCandidates() {
      ArrayPush(all, record);
    }
    for record in all {
      if Equals(id, TDBID.Create(record)) {
        return record;
      }
    }
    return "?";
  }

  private func ControllerOf(components: array<ref<IComponent>>) -> ref<AnimationControllerComponent> {
    for component in components {
      let controller = component as AnimationControllerComponent;
      if IsDefined(controller) {
        return controller;
      }
    }
    return null;
  }

  private func ExtensionsOf(components: array<ref<IComponent>>) -> array<ref<entAnimationSetupExtensionComponent>> {
    let found: array<ref<entAnimationSetupExtensionComponent>>;
    for component in components {
      let extension = component as entAnimationSetupExtensionComponent;
      if IsDefined(extension) {
        ArrayPush(found, extension);
      }
    }
    return found;
  }

  // Every gameplay set the extensions add, the character's own first.
  private func ExtensionSets(extensions: array<ref<entAnimationSetupExtensionComponent>>) -> array<animAnimSetupEntry> {
    let sets: array<animAnimSetupEntry>;
    for extension in extensions {
      for entry in extension.animations.gameplay {
        ArrayPush(sets, entry);
      }
    }
    return sets;
  }

  private func RootOf(components: array<ref<IComponent>>) -> ref<AnimatedComponent> {
    for component in components {
      let animated = component as AnimatedComponent;
      if IsDefined(animated) && Equals(animated.GetName(), n"root") {
        return animated;
      }
    }
    return null;
  }

  // On a job thread, before the entity is built from these components.
  private cb func OnExtract(event: ref<EntityBuilderEvent>) {
    TandemNative.HookRan(n"Probe.TandemProbe.OnExtract");
    if this.m_stopped {
      return;
    }
    let builder = event.GetEntityBuilder();
    let record = this.NameOf(builder.GetRecordID());
    let components = builder.GetTemplate().GetComponents();
    let appearance = builder.GetAppearance();
    if IsDefined(appearance) {
      for component in appearance.GetComponents() {
        ArrayPush(components, component);
      }
    }
    let root = this.RootOf(components);
    if !IsDefined(root) {
      TandemNative.Log(s"probe \(record): no root animated component (\(ArraySize(components)) components)");
      return;
    }
    let controller = this.ControllerOf(components);
    let extensions = this.ExtensionsOf(components);
    this.Describe(record, "", root);
    for extension in extensions {
      TandemNative.Log(s"probe \(record): extension '\(NameToString(extension.GetName()))' \(ArraySize(extension.animations.gameplay)) sets, setup \(extension.animations.hash), override \(extension.isOverrideContainer), bound \(IsDefined(extension.controlBinding))");
    }
    this.DescribeController(record, "", controller);

    if Equals(record, TandemProbe.FemaleDonor()) {
      this.m_femaleGraph = root.graph;
      this.m_femaleSetup = root.animations;
      this.m_femaleExtensions = this.ExtensionSets(extensions);
      if IsDefined(controller) {
        this.m_femaleActions = controller.actionAnimDatabaseRef;
        this.m_femaleDatabases = controller.animDatabaseCollection;
      }
      this.m_femaleReady = true;
    } else if !StrContains(record, "Tandem") {
      if !this.m_maleReady && Equals(s"\(ResourceRef.GetHash(root.rig))", TandemProbe.MaleRig()) {
        this.m_maleDonor = record;
        this.m_maleGraph = root.graph;
        this.m_maleSetup = root.animations;
        this.m_maleExtensions = this.ExtensionSets(extensions);
        if IsDefined(controller) {
          this.m_maleActions = controller.actionAnimDatabaseRef;
          this.m_maleDatabases = controller.animDatabaseCollection;
        }
        this.m_maleReady = true;
        TandemNative.Log(s"probe \(record): shares male V's skeleton, the male donor");
      }
    } else {
      let graft = TandemProbe.GraftOf(record);
      if graft > 0 && !TandemProbe.AtAssemble(record) {
        this.Graft(record, root, graft, TandemProbe.IsMaleBody(record));
        this.GraftExtensions(record, extensions, TandemProbe.IsMaleBody(record));
        if IsDefined(controller) {
          let male = TandemProbe.IsMaleBody(record) && this.m_maleReady;
          controller.actionAnimDatabaseRef = male ? this.m_maleActions : this.m_femaleActions;
          controller.animDatabaseCollection = male ? this.m_maleDatabases : this.m_femaleDatabases;
        }
        this.Describe(record, " after", root);
        this.DescribeController(record, " after", controller);
      }
    }
  }

  // The entity exists, its components are not attached yet.
  private cb func OnAssemble(event: ref<EntityLifecycleEvent>) {
    if this.m_stopped {
      return;
    }
    let entity = event.GetEntity();
    if !IsDefined(entity) {
      return;
    }
    let record = "?";
    for body in this.m_bodies {
      if Equals(body.id, entity.GetEntityID()) {
        record = body.record;
      }
    }
    let root = this.RootOf(entity.GetComponents());
    if IsDefined(root) && TandemProbe.GraftOf(record) > 0 {
      this.Graft(record, root, TandemProbe.GraftOf(record), TandemProbe.IsMaleBody(record));
      this.Describe(record, " assembled", root);
    }
  }

  private func DescribeController(record: String, when: String, controller: ref<AnimationControllerComponent>) -> Void {
    if !IsDefined(controller) {
      TandemNative.Log(s"probe \(record)\(when): no animation controller");
      return;
    }
    let line = s"probe \(record)\(when): controller actions \(ResourceRef.GetHash(controller.actionAnimDatabaseRef)), databases";
    for entry in controller.animDatabaseCollection.animDatabases {
      line += s" \(NameToString(entry.name))=\(ResourceRef.GetHash(entry.animDatabase))";
    }
    TandemNative.Log(line);
  }

  private func Describe(record: String, when: String, animated: ref<AnimatedComponent>) -> Void {
    let line = s"probe \(record)\(when): root rig \(ResourceRef.GetHash(animated.rig)) graph \(ResourceRef.GetHash(animated.graph)) setup \(animated.animations.hash), \(ArraySize(animated.animations.gameplay)) sets";
    TandemNative.Log(line);
  }

  // The donor's extension sets go into V's "Character Animation Setup"
  // extension, already bound to V's root, under a new setup hash.
  private func GraftExtensions(record: String, extensions: array<ref<entAnimationSetupExtensionComponent>>, male: Bool) -> Void {
    let sets = male && this.m_maleReady ? this.m_maleExtensions : this.m_femaleExtensions;
    for extension in extensions {
      if StrContains(NameToString(extension.GetName()), "Character") {
        let setup = extension.animations;
        let added = 0;
        for entry in sets {
          ArrayPush(setup.gameplay, entry);
          added += 1;
        }
        setup.hash = setup.hash + Cast<Uint64>(7919);
        extension.animations = setup;
        TandemNative.Log(s"probe \(record): \(added) extension sets added to '\(NameToString(extension.GetName()))'");
        return;
      }
    }
    TandemNative.Log(s"probe \(record): no Character extension to graft into");
  }

  private func Graft(record: String, root: ref<AnimatedComponent>, mode: Int32, male: Bool) -> Void {
    let useMale = male && this.m_maleReady;
    let graph = useMale ? this.m_maleGraph : this.m_femaleGraph;
    let donor = useMale ? this.m_maleSetup : this.m_femaleSetup;
    if mode == 3 {
      root.animations = donor;
    } else {
      let setup = root.animations;
      for entry in donor.gameplay {
        ArrayPush(setup.gameplay, entry);
      }
      // A new key: the engine may cache a setup by its hash.
      setup.hash = setup.hash + donor.hash;
      root.animations = setup;
    }
    root.graph = graph;
    TandemNative.Log(s"probe \(record): grafted mode \(mode) from \(useMale ? this.m_maleDonor : "Panam")");
  }
}

// `tools/game.sh cmd gates RECORD...`: spawns each record below V for a
// moment and logs its skeleton (root rig hash) and whether its animation sets
// can crouch (sets gated by inCrouch / stealthLocomotion) — to find donors.
public class TandemGates {
  private let m_game: GameInstance;
  private let m_records: array<String>;
  private let m_ids: array<EntityID>;

  public func Run(game: GameInstance, records: array<String>) -> Void {
    this.m_game = game;
    this.m_records = records;
    let callbacks = GameInstance.GetCallbackSystem();
    let player = GetPlayer(game);
    for record in records {
      if !IsDefined(TweakDBInterface.GetCharacterRecord(TDBID.Create(record))) {
        TandemNative.Log(s"gates \(record): no such record");
      } else {
        callbacks.RegisterCallback(n"Entity/Extract", this, n"OnExtract")
          .AddTarget(EntityTarget.RecordID(TDBID.Create(record)));
        let spec = new DynamicEntitySpec();
        spec.recordID = TDBID.Create(record);
        spec.position = player.GetWorldPosition();
        spec.position.Z -= 40.0;
        spec.orientation = player.GetWorldOrientation();
        spec.alwaysSpawned = true;
        spec.persistState = false;
        spec.persistSpawn = false;
        ArrayPush(this.m_ids, GameInstance.GetDynamicEntitySystem().CreateEntity(spec));
      }
    }
  }

  public func Clear() -> Void {
    for id in this.m_ids {
      GameInstance.GetDynamicEntitySystem().DeleteEntity(id);
    }
    ArrayClear(this.m_ids);
  }

  private cb func OnExtract(event: ref<EntityBuilderEvent>) {
    let builder = event.GetEntityBuilder();
    let record = "?";
    for name in this.m_records {
      if Equals(builder.GetRecordID(), TDBID.Create(name)) {
        record = name;
      }
    }
    let components = builder.GetTemplate().GetComponents();
    let appearance = builder.GetAppearance();
    if IsDefined(appearance) {
      for component in appearance.GetComponents() {
        ArrayPush(components, component);
      }
    }
    let root = TandemBodies.RootOf(components);
    let rig = IsDefined(root) ? s"\(ResourceRef.GetHash(root.rig))" : "none";
    let crouch = 0;
    let stealth = 0;
    let sets = 0;
    for component in components {
      let extension = component as entAnimationSetupExtensionComponent;
      if IsDefined(extension) {
        for entry in extension.animations.gameplay {
          sets += 1;
          for variable in entry.variableNames {
            if Equals(variable, n"inCrouch") {
              crouch += 1;
            }
            if Equals(variable, n"stealthLocomotion") {
              stealth += 1;
            }
          }
        }
      }
    }
    TandemNative.Log(s"gates \(record): rig \(rig), \(sets) sets, \(crouch) inCrouch, \(stealth) stealthLocomotion");
  }
}
