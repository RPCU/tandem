// Why each remote player's puppet is hidden: one set of reasons per puppet,
// so that one feature never shows what another hid (docs/architecture/elsewhere.md,
// other worlds). Reasons: n"private" (our private scene, Story.reds),
// n"elsewhere" (another world, ours or theirs, Elsewhere.reds), n"noseat"
// (no free seat in a car here, System.reds), n"nolook" (no body look yet:
// clothes alone are never shown, System.reds StandIn).
//
// The first reason hides the puppet: it fades out the animation system's
// way (GameObject.ToggleForcedVisibilityInAnimSystemEvent, final.reds:14005,
// as a private scene always did), then, once faded, its meshes and what it
// wears and holds are hidden too (IVisualComponent.TemporaryHide, :106206),
// again every tick for what it puts on later. The last reason gone shows it
// again the same way back. The puppet stays in the world all along: its
// marker, its movement and its holds (HoldPuppet) are not touched here.

public class TandemHiddenPuppet {
  public let player: Uint32;
  public let reasons: array<CName>;
  // The body the fade was sent to (a puppet built again needs it again),
  // and when its meshes go too.
  public let body: EntityID;
  public let hardAt: Float;
}

public class TandemVisibility {
  private let m_game: GameInstance;
  private let m_now: Float;
  private let m_hidden: array<ref<TandemHiddenPuppet>>;

  public static func Get(game: GameInstance) -> ref<TandemVisibility> {
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.Visibility() : null;
  }

  public static func Hide(game: GameInstance, player: Uint32, reason: CName, fade: Float) -> Void {
    let visibility = TandemVisibility.Get(game);
    if IsDefined(visibility) {
      visibility.Add(player, reason, fade);
    }
  }

  public static func Show(game: GameInstance, player: Uint32, reason: CName, fade: Float) -> Void {
    let visibility = TandemVisibility.Get(game);
    if IsDefined(visibility) {
      visibility.Remove(player, reason, fade);
    }
  }

  // Hidden for any reason, or for this one.
  public static func IsHidden(game: GameInstance, player: Uint32) -> Bool {
    let visibility = TandemVisibility.Get(game);
    return IsDefined(visibility) && IsDefined(visibility.Find(player));
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    ArrayClear(this.m_hidden);
  }

  public func Find(player: Uint32) -> ref<TandemHiddenPuppet> {
    for hidden in this.m_hidden {
      if hidden.player == player {
        return hidden;
      }
    }
    return null;
  }

  private func BodyOf(player: Uint32) -> ref<GameObject> {
    let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.PuppetOf(player) : null;
  }

  public func Add(player: Uint32, reason: CName, fade: Float) -> Void {
    let hidden = this.Find(player);
    if !IsDefined(hidden) {
      hidden = new TandemHiddenPuppet();
      hidden.player = player;
      ArrayPush(this.m_hidden, hidden);
    }
    if ArrayContains(hidden.reasons, reason) {
      return;
    }
    ArrayPush(hidden.reasons, reason);
    if ArraySize(hidden.reasons) == 1 {
      let body = this.BodyOf(player);
      if IsDefined(body) {
        TandemVisibility.Fade(this.m_game, body, false, fade);
        hidden.body = body.GetEntityID();
      }
      hidden.hardAt = this.m_now + fade;
      TandemNative.Log(s"visibility: player \(player)'s puppet hidden (\(NameToString(reason)))");
    } else {
      TandemNative.Log(s"visibility: player \(player)'s puppet also hidden for \(NameToString(reason))");
    }
  }

  public func Remove(player: Uint32, reason: CName, fade: Float) -> Void {
    let hidden = this.Find(player);
    if !IsDefined(hidden) || !ArrayContains(hidden.reasons, reason) {
      return;
    }
    ArrayRemove(hidden.reasons, reason);
    if ArraySize(hidden.reasons) > 0 {
      TandemNative.Log(s"visibility: player \(player)'s puppet no longer hidden for \(NameToString(reason)), still for \(NameToString(hidden.reasons[0]))");
      return;
    }
    this.Reveal(hidden, fade);
    ArrayRemove(this.m_hidden, hidden);
    TandemNative.Log(s"visibility: player \(player)'s puppet shown (\(NameToString(reason)) over)");
  }

  private func Reveal(hidden: ref<TandemHiddenPuppet>, fade: Float) -> Void {
    let body = this.BodyOf(hidden.player);
    if IsDefined(body) {
      TandemElsewhere.HideBody(this.m_game, body, false);
      TandemVisibility.Fade(this.m_game, body, true, fade);
    }
  }

  // The animation system's fade, on the body and on what it wears and
  // holds (item entities of their own: a fade of the body alone left its
  // clothes in the air until the meshes went).
  public static func Fade(game: GameInstance, body: ref<GameObject>, visible: Bool, time: Float) -> Void {
    GameObject.ToggleForcedVisibilityInAnimSystemEvent(body, n"tandem_hidden", visible, time);
    let transactions = GameInstance.GetTransactionSystem(game);
    let slots = TandemOutfit.Slots();
    ArrayPush(slots, t"AttachmentSlots.WeaponRight");
    ArrayPush(slots, t"AttachmentSlots.WeaponLeft");
    for slot in slots {
      let item = transactions.GetItemInSlot(body, slot);
      if IsDefined(item) {
        GameObject.ToggleForcedVisibilityInAnimSystemEvent(item, n"tandem_hidden", visible, time);
      }
    }
  }

  // Every frame (TandemSystem.Tick): faded puppets lose their meshes, a
  // puppet built again is hidden again.
  public func Tick(now: Float, system: ref<TandemSystem>) -> Void {
    this.m_now = now;
    for hidden in this.m_hidden {
      let body = system.PuppetOf(hidden.player);
      if IsDefined(body) {
        if body.GetEntityID() != hidden.body {
          hidden.body = body.GetEntityID();
          TandemVisibility.Fade(this.m_game, body, false, 0.0);
        }
        if now >= hidden.hardAt {
          TandemElsewhere.HideBody(this.m_game, body, true);
        }
      }
    }
  }

  // The player left: its puppet is gone, and so are its reasons.
  public func Forget(player: Uint32) -> Void {
    let hidden = this.Find(player);
    if IsDefined(hidden) {
      ArrayRemove(this.m_hidden, hidden);
    }
  }

  // The session ended: nothing stays hidden.
  public func End() -> Void {
    for hidden in this.m_hidden {
      this.Reveal(hidden, 0.0);
    }
    if ArraySize(this.m_hidden) > 0 {
      TandemNative.Log(s"visibility: the session is over, \(ArraySize(this.m_hidden)) puppet(s) shown");
    }
    ArrayClear(this.m_hidden);
  }
}
