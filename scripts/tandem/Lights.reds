// Traffic lights in step (docs/architecture/traffic.md, "Traffic lights").
//
// The game runs its lights natively (the traffic system's crossings, no
// script surface) and tells each light entity its colour with a
// TrafficLightChangeEvent; the light's script turns its lamps on and off
// (TrafficLight, engine/traffic-police.md §13.156). Two games run their
// crossings on their own clocks, so a crossing showed green on one screen
// and red on the other.
//
// The area owner's lights are the truth. A game that runs its street says
// each colour its game gives a light as it changes, and every 5 s all the
// lights it knows round its V (TrafficLights through the host, for the
// players linked with it). A game that does not run its street shows its
// area owner's colour on each light that owner named, the game's way (the
// light's own change sequence: yellow, then red or green), and drops its
// own game's changes on those lights; the others keep their own. A light
// the owner stops naming (12 s) goes back to its own game.
//
// The walk markings painted on a crossing are lights too: TrafficZebra, a
// TrafficLight told its colour the same way, which shows it as its mesh's
// appearance ("stop" or "default") from its own handler (engine facts
// §13.156). Its handler is wrapped as the light's is.
public class TandemLights {
  private let m_game: GameInstance;
  // Ours: each light our game gave a colour, the colour, and those that
  // changed since we last said them.
  private let m_ours: array<EntityID>;
  private let m_ourColors: array<Int32>;
  private let m_changed: array<EntityID>;
  private let m_changedColors: array<Int32>;
  private let m_fullAt: Float;
  // Theirs: each light our area owner names, its colour, when it last did.
  private let m_owner: Uint32;
  private let m_theirs: array<EntityID>;
  private let m_theirColors: array<Int32>;
  private let m_theirAt: array<Float>;
  private let m_followed: Int32;
  private let m_checkAt: Float;
  // (test: `lights`) counts since the start.
  private let m_sent: Int32;
  private let m_heard: Int32;
  private let m_shown: Int32;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
  }

  public func Tick(now: Float, you: Uint32) -> Void {
    let owns = you != 0u && TandemArea.Owns();
    let mine = TandemStreet.TakeLightsSaid();
    for said in mine {
      this.NoteOurs(said.entity, said.color);
    }
    let heard = TandemStreet.TakeLights();
    if you == 0u {
      this.Forget();
      ArrayClear(this.m_changed);
      ArrayClear(this.m_changedColors);
      return;
    }
    if owns {
      this.Forget();
      if ArraySize(this.m_changed) > 0 {
        TandemStreet.SendLights(false, this.m_changed, this.m_changedColors);
        this.m_sent += ArraySize(this.m_changed);
        ArrayClear(this.m_changed);
        ArrayClear(this.m_changedColors);
      }
      if now - this.m_fullAt >= 5.0 || now < this.m_fullAt {
        this.m_fullAt = now;
        this.SendAll();
      }
      return;
    }
    ArrayClear(this.m_changed);
    ArrayClear(this.m_changedColors);
    let owner = TandemRegions.AreaOwner(you);
    if owner != this.m_owner {
      this.Forget();
      this.m_owner = owner;
    }
    let fresh = false;
    for light in heard {
      if light.owner == owner {
        this.m_heard += 1;
        this.NoteTheirs(light.entity, light.color, now);
        fresh = true;
      }
    }
    if fresh || now - this.m_checkAt >= 0.5 || now < this.m_checkAt {
      this.m_checkAt = now;
      this.Follow(now);
    }
  }

  private func NoteOurs(entity: EntityID, color: Int32) -> Void {
    let i = 0;
    let n = ArraySize(this.m_ours);
    while i < n {
      if this.m_ours[i] == entity {
        if this.m_ourColors[i] != color {
          this.m_ourColors[i] = color;
          this.Changed(entity, color);
        }
        return;
      }
      i += 1;
    }
    if n >= 256 {
      ArrayErase(this.m_ours, 0);
      ArrayErase(this.m_ourColors, 0);
    }
    ArrayPush(this.m_ours, entity);
    ArrayPush(this.m_ourColors, color);
    this.Changed(entity, color);
  }

  private func Changed(entity: EntityID, color: Int32) -> Void {
    let i = ArrayFindFirst(this.m_changed, entity);
    if i >= 0 {
      this.m_changedColors[i] = color;
    } else {
      ArrayPush(this.m_changed, entity);
      ArrayPush(this.m_changedColors, color);
    }
  }

  // Every light we know that is still round our V (150 m), its colour as it
  // shows now.
  private func SendAll() -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let at = player.GetWorldPosition();
    let entities: array<EntityID>;
    let colors: array<Int32>;
    let keptIds: array<EntityID>;
    let keptColors: array<Int32>;
    let i = 0;
    while i < ArraySize(this.m_ours) {
      let light = GameInstance.FindEntityByID(this.m_game, this.m_ours[i]) as TrafficLight;
      if IsDefined(light) {
        ArrayPush(keptIds, this.m_ours[i]);
        ArrayPush(keptColors, light.TandemColor());
        if Vector4.Distance(light.GetWorldPosition(), at) < 150.0 {
          ArrayPush(entities, this.m_ours[i]);
          ArrayPush(colors, light.TandemColor());
        }
      }
      i += 1;
    }
    // (lights streamed out are forgotten: they say their colour again when
    // they come back)
    this.m_ours = keptIds;
    this.m_ourColors = keptColors;
    TandemStreet.SendLights(true, entities, colors);
    this.m_sent += ArraySize(entities);
  }

  private func NoteTheirs(entity: EntityID, color: Int32, now: Float) -> Void {
    let i = ArrayFindFirst(this.m_theirs, entity);
    if i >= 0 {
      this.m_theirColors[i] = color;
      this.m_theirAt[i] = now;
      return;
    }
    ArrayPush(this.m_theirs, entity);
    ArrayPush(this.m_theirColors, color);
    ArrayPush(this.m_theirAt, now);
  }

  // Each light our area owner named shows its colour; one it stopped naming
  // goes back to our game. The hook is told which lights follow.
  private func Follow(now: Float) -> Void {
    let ids: array<EntityID>;
    let colors: array<Int32>;
    let times: array<Float>;
    let i = 0;
    while i < ArraySize(this.m_theirs) {
      if now - this.m_theirAt[i] <= 12.0 && now >= this.m_theirAt[i] {
        ArrayPush(ids, this.m_theirs[i]);
        ArrayPush(colors, this.m_theirColors[i]);
        ArrayPush(times, this.m_theirAt[i]);
        let light = GameInstance.FindEntityByID(this.m_game, this.m_theirs[i]) as TrafficLight;
        if IsDefined(light) && light.TandemShow(this.m_theirColors[i]) {
          this.m_shown += 1;
        }
      }
      i += 1;
    }
    this.m_theirs = ids;
    this.m_theirColors = colors;
    this.m_theirAt = times;
    if ArraySize(ids) != this.m_followed || ArraySize(ids) > 0 {
      TandemStreet.FollowLights(ids);
      this.m_followed = ArraySize(ids);
    }
  }

  // Nobody's lights followed: each shows its own game's from its next change.
  private func Forget() -> Void {
    if ArraySize(this.m_theirs) > 0 || this.m_followed > 0 {
      ArrayClear(this.m_theirs);
      ArrayClear(this.m_theirColors);
      ArrayClear(this.m_theirAt);
      let none: array<EntityID>;
      TandemStreet.FollowLights(none);
      this.m_followed = 0;
    }
    this.m_owner = 0u;
  }

  // (test: `lights`) counts, and the lights nearest our V with their colour.
  public func Report() -> Void {
    let player = GetPlayer(this.m_game);
    let line = "";
    if IsDefined(player) {
      let at = player.GetWorldPosition();
      let all: array<EntityID>;
      for id in this.m_ours {
        ArrayPush(all, id);
      }
      for id in this.m_theirs {
        if !ArrayContains(all, id) {
          ArrayPush(all, id);
        }
      }
      for id in all {
        let light = GameInstance.FindEntityByID(this.m_game, id) as TrafficLight;
        if IsDefined(light) {
          let far = Vector4.Distance(light.GetWorldPosition(), at);
          if far < 60.0 {
            let lamp = light.GetWorldPosition();
            line += s" \(EntityID.ToDebugStringDecimal(id)) \(Cast<Int32>(far)) m \(TandemLights.Name(light.TandemColor()))\(IsDefined(light as CrossingLight) ? " crossing" : "")\(IsDefined(light as TrafficZebra) ? " zebra" : "")\(ArrayContains(this.m_theirs, id) ? " (followed)" : "") at \(lamp.X) \(lamp.Y) \(lamp.Z);";
          }
        }
      }
    }
    TandemNative.Log(s"lights: \(ArraySize(this.m_ours)) of ours known, \(this.m_sent) said, \(this.m_heard) heard from our area owner, \(ArraySize(this.m_theirs)) followed now, \(this.m_shown) shown its colour; within 60 m:\(line)");
  }

  public static func Name(color: Int32) -> String {
    switch color {
      case 0:
        return "green";
      case 1:
        return "red";
      case 2:
        return "yellow";
      default:
        return "off";
    }
  }
}

// The colour the light shows now (worldTrafficLightColor as an Int32).
@addMethod(TrafficLight)
public func TandemColor() -> Int32 {
  return EnumInt(this.m_lightState);
}

// Another game's colour, the game's way (OnTrafficLightChangeEvent's own
// sequence: lamps off, yellow from the colour it had, or red or green). True
// when it changed.
@addMethod(TrafficLight)
public func TandemShow(color: Int32) -> Bool {
  let zebra = this as TrafficZebra;
  if IsDefined(zebra) {
    return zebra.TandemShowZebra(color);
  }
  let wanted = IntEnum<worldTrafficLightColor>(color);
  if Equals(this.m_lightState, wanted) || !IsDefined(this.GetDevicePS()) || !this.GetDevicePS().IsInitialized() {
    return false;
  }
  this.TurnOffLights();
  if Equals(wanted, worldTrafficLightColor.YELLOW) {
    this.CommenceLightChangeSequence(this.m_lightState);
    this.m_lightState = wanted;
  } else {
    this.m_lightState = wanted;
    this.CompleteLightChangeSequence();
  }
  return true;
}

// The game's own change of a light's colour: said to TandemLights (the
// colours we send while we run our street), and dropped on a light that
// shows another game's colours now (TandemLights.Follow puts that one's).
// job thread safe: it only calls natives that queue (LightSaid) or read (FollowsLight) and the wrapped method.
@wrapMethod(TrafficLight)
protected cb func OnTrafficLightChangeEvent(evt: ref<TrafficLightChangeEvent>) -> Bool {
  let id = this.GetEntityID();
  TandemStreet.LightSaid(id, EnumInt(evt.lightColor));
  if TandemStreet.FollowsLight(id) {
    return true;
  }
  return wrappedMethod(evt);
}

// A crossing's walk markings: another game's colour, the zebra's own way
// (its handler: green shows the "default" appearance, anything else "stop").
@addMethod(TrafficZebra)
public func TandemShowZebra(color: Int32) -> Bool {
  let wanted = IntEnum<worldTrafficLightColor>(color);
  if Equals(this.m_lightState, wanted) || !IsDefined(this.GetDevicePS()) || !this.GetDevicePS().IsInitialized() {
    return false;
  }
  this.m_lightState = wanted;
  if Equals(wanted, worldTrafficLightColor.GREEN) {
    this.HandleGreenLight();
  } else {
    this.HandleRedLight();
  }
  return true;
}

// The zebra's own handler overrides the light's (it never calls it), so the
// light's wrap above never saw a zebra: its colour was neither said nor
// followed, and the walk markings ran on each game's own phase.
// job thread safe: it only calls natives that queue (LightSaid) or read (FollowsLight) and the wrapped method.
@wrapMethod(TrafficZebra)
protected cb func OnTrafficLightChangeEvent(evt: ref<TrafficLightChangeEvent>) -> Bool {
  let id = this.GetEntityID();
  TandemStreet.LightSaid(id, EnumInt(evt.lightColor));
  if TandemStreet.FollowsLight(id) {
    return true;
  }
  return wrappedMethod(evt);
}
