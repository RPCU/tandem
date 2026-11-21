// A call between players drawn as the game draws a holocall
// (docs/architecture/comms.md, "The call on screen"): a partner's link
// request rings with the game's incoming-call widget (their name, the
// game's unknown avatar in their colour, the hold-to-reject glyph); our own
// call shows the call portrait connecting; an answered call is the audio
// call view, "Linked", for a few seconds, and holding PhoneReject hangs up
// (the link breaks, as the phone's Hang up). The link itself is Comms.reds'
// and unchanged: this only draws it.
//
// The widgets are the HUD phone's own (NewHudPhoneGameController's
// incomingCallElement and holoAudioCallElement, spawned from its library
// into its slots), held in fields of ours: the game's call state
// (m_CurrentCallInformation, PhoneSystem, the phonecall_* facts quests and
// story sync read) never hears of them. A call of the game's comes first:
// ours goes while one rings or is on. Everything here runs on our tick
// (TandemComms.Tick, from TandemSystem's): no wrap, no callback.

public class TandemHolocall {
  private let m_game: GameInstance;
  private let m_phone: wref<NewHudPhoneGameController>;
  private let m_scannedAt: Float;
  // Who the call is with, and what is shown: 0 nothing, 1 their call rings,
  // 2 we call them, 3 on the call, 4 the call ends (its hiding animation),
  // 5 the view closes with the link on.
  private let m_player: Uint32;
  private let m_name: String;
  private let m_mode: Int32;
  private let m_since: Float;
  // What the HUD shows now (the mode last drawn; 0: nothing of ours).
  private let m_drawn: Int32;
  private let m_hint: Bool;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
  }

  public func Stop() -> Void {
    this.Clear();
  }

  // ---- what Comms.reds says ----

  // Their link request rings here.
  public func Ring(player: Uint32, name: String, now: Float) -> Void {
    if this.m_player != player && this.m_drawn != 0 {
      // Another call rang before (declined by this one): drawn again.
      this.Clear();
    }
    this.Set(player, name, 1, now);
  }

  // We call them (our request, from the phone or a ping on their V).
  public func Calling(player: Uint32, name: String, now: Float) -> Void {
    if this.m_mode == 3 && this.m_player == player {
      return;
    }
    this.Set(player, name, 2, now);
  }

  // The link is made: the call we placed or answered is on.
  public func Answered(player: Uint32, name: String, now: Float) -> Void {
    if (this.m_mode == 1 || this.m_mode == 2) && this.m_player == player {
      this.Set(player, name, 3, now);
    }
  }

  // Their request stopped ringing unanswered (declined, taken back, gone).
  public func RingStopped(player: Uint32) -> Void {
    if this.m_mode == 1 && this.m_player == player {
      this.Clear();
    }
  }

  // The call is over: the link broke, or ours was cancelled.
  public func Ended(player: Uint32, now: Float) -> Void {
    if this.m_player != player || this.m_mode == 0 || this.m_mode == 4 {
      return;
    }
    if this.m_mode == 1 {
      this.Clear();
      return;
    }
    TandemNative.Log(s"holocall: the call with \(this.m_name) ended");
    this.Set(player, this.m_name, 4, now);
  }

  // The call's own button applies: we call them or are on the call.
  public func CanHangUp() -> Bool {
    return this.m_mode == 2 || this.m_mode == 3;
  }

  public func Player() -> Uint32 {
    return this.CanHangUp() ? this.m_player : 0u;
  }

  // Our call widgets are on screen (the party HUD moves below them,
  // presence.md).
  public func Up() -> Bool {
    return this.m_drawn != 0;
  }

  public func Report() -> String {
    return s"holocall: mode \(this.m_mode) with \(this.m_player != 0u ? this.m_name : "nobody"), drawn \(this.m_drawn)";
  }

  private func Set(player: Uint32, name: String, mode: Int32, now: Float) -> Void {
    this.m_player = player;
    this.m_name = name;
    this.m_mode = mode;
    this.m_since = now;
  }

  // ---- every tick ----

  // `gameCall`: a call of the game's own rings or is on.
  public func Tick(now: Float, gameCall: Bool) -> Void {
    if this.m_mode == 0 && this.m_drawn == 0 {
      return;
    }
    let phone = this.Phone(now);
    if !IsDefined(phone) {
      return;
    }
    if gameCall {
      // The game's call has the phone: ours goes (the link stays as it is).
      if this.m_drawn != 0 {
        phone.TandemRing("", TandemMarkers.ColorOf(0u), false, true);
        phone.TandemCall(0, "", TandemMarkers.ColorOf(0u), "", true);
        this.m_drawn = 0;
        TandemNative.Log("holocall: a call of the game's: ours is not shown");
      }
      this.ShowHint(false);
      return;
    }
    let color = TandemMarkers.ColorOf(this.m_player);
    switch this.m_mode {
      case 1:
        if phone.TandemRing(this.m_name, color, true, false) && this.m_drawn != 1 {
          if this.m_drawn >= 2 {
            phone.TandemCall(0, "", color, "", false);
          }
          this.m_drawn = 1;
          TandemNative.Log(s"holocall: \(this.m_name) is calling: the incoming call widget shown");
        }
        break;
      case 2:
        // Unanswered (the request stands, as the phone's row says).
        if now - this.m_since > 20.0 || now < this.m_since {
          TandemNative.Log(s"holocall: \(this.m_name) did not answer: the call view closed");
          this.m_mode = 5;
          this.m_since = now;
          break;
        }
        if this.m_drawn != 2 && phone.TandemCall(2, this.m_name, color, TandemText.Get("Phone-Calling"), false) {
          this.m_drawn = 2;
          TandemNative.Log(s"holocall: calling \(this.m_name): the call view shown");
        }
        break;
      case 3:
        if this.m_drawn != 3 {
          phone.TandemRing("", color, false, false);
          if phone.TandemCall(3, this.m_name, color, TandemText.Get("Phone-Linked"), false) {
            this.m_drawn = 3;
            TandemNative.Log(s"holocall: on the call with \(this.m_name): the audio call view shown");
          }
        } else if now - this.m_since > 8.0 || now < this.m_since {
          // A link lasts the session: the call is its first seconds, then
          // the party HUD shows it (presence.md).
          TandemNative.Log(s"holocall: the call view of \(this.m_name) closed, the link stays");
          this.m_mode = 5;
          this.m_since = now;
        }
        break;
      case 4:
      case 5:
        if this.m_drawn == 1 || this.m_drawn == 0 {
          this.Clear();
          break;
        }
        if this.m_drawn != this.m_mode {
          phone.TandemCall(this.m_mode, this.m_name, color, this.m_mode == 4 ? TandemText.Get("Holocall-Ended") : TandemText.Get("Phone-Linked"), false);
          this.m_drawn = this.m_mode;
        } else if phone.TandemCallHidden() || now - this.m_since > 4.0 || now < this.m_since {
          this.Clear();
        }
        break;
      default:
        this.Clear();
        break;
    }
    this.ShowHint(this.CanHangUp() && this.m_drawn >= 2);
  }

  // Nothing of ours on screen.
  private func Clear() -> Void {
    if this.m_drawn != 0 {
      let phone = this.m_phone;
      if IsDefined(phone) {
        phone.TandemRing("", TandemMarkers.ColorOf(0u), false, false);
        phone.TandemCall(0, "", TandemMarkers.ColorOf(0u), "", false);
      }
      this.m_drawn = 0;
    }
    this.ShowHint(false);
    this.m_mode = 0;
    this.m_player = 0u;
  }

  // The game's own hang-up: PhoneReject held (TandemComms listens).
  private func ShowHint(show: Bool) -> Void {
    if Equals(show, this.m_hint) {
      return;
    }
    this.m_hint = show;
    TandemText.Hint(this.m_game, n"TandemHangUp", n"PhoneReject", show, show ? TandemText.Get("Phone-HangUp") : "", inkInputHintHoldIndicationType.Hold);
  }

  // The HUD phone (Codeware's controllers of the HUD layer), looked for
  // again at most once a second while it is missing.
  private func Phone(now: Float) -> wref<NewHudPhoneGameController> {
    if IsDefined(this.m_phone) {
      return this.m_phone;
    }
    if this.m_scannedAt > 0.0 && now >= this.m_scannedAt && now - this.m_scannedAt < 1.0 {
      return null;
    }
    this.m_scannedAt = now;
    let layer = GameInstance.GetInkSystem().GetLayer(n"inkHUDLayer");
    if !IsDefined(layer) {
      return null;
    }
    for controller in layer.GetGameControllers() {
      let phone = controller as NewHudPhoneGameController;
      if IsDefined(phone) {
        this.m_phone = phone;
        TandemNative.Log("holocall: the HUD phone found");
        return phone;
      }
    }
    return null;
  }
}

// ---- the HUD phone's call widgets (final.reds:408140) ----
//
// Called from TandemHolocall.Tick only (our tick, the game thread): the
// game's own spawn and show functions, on widgets held apart from its call
// state. EnumInt(Call) is 0, as in the game's own calls.

@addField(NewHudPhoneGameController)
private let m_tandemRing: wref<inkWidget>;

@addField(NewHudPhoneGameController)
private let m_tandemCall: wref<inkWidget>;

// The incoming-call widget for a partner's call: shown (and its ring
// animation played again while it rings), or taken away. False: not shown
// (the game's own is there). `gameCall`: the game's call has the element.
@addMethod(NewHudPhoneGameController)
public final func TandemRing(name: String, color: HDRColor, show: Bool, gameCall: Bool) -> Bool {
  if !show {
    if IsDefined(this.m_tandemRing) {
      inkCompoundRef.RemoveChild(this.incomingCallElement.slot, this.m_tandemRing);
      this.m_tandemRing = null;
      if !gameCall && !IsDefined(this.incomingCallElement.widget) {
        this.DeactivatePhoneElement(gameuiActivePhoneElement.IncomingCall);
      }
    }
    return true;
  }
  if !IsDefined(this.m_tandemRing) {
    if IsDefined(this.incomingCallElement.widget) {
      return false;
    }
    let widget = this.SpawnFromLocal(inkWidgetRef.Get(this.incomingCallElement.slot), this.incomingCallElement.libraryID);
    let ring = IsDefined(widget) ? widget.GetController() as IncomingCallLogicController : null;
    if !IsDefined(ring) {
      return false;
    }
    this.m_tandemRing = widget;
    this.ActivatePhoneElement(gameuiActivePhoneElement.IncomingCall);
    this.PlaySound(n"PhoneCallPopup", n"OnOpen");
    ring.TandemSetCall(name, color);
    return true;
  }
  let controller = this.m_tandemRing.GetController() as IncomingCallLogicController;
  if IsDefined(controller) {
    controller.TandemRingOn();
  }
  return true;
}

// The call portrait: 2 connecting (our call), 3 the audio call view with
// the avatar, 4 the call ends (the game's disconnecting, then hidden), 5
// hidden quietly; 0 taken away. False: not shown (the game's call is there).
@addMethod(NewHudPhoneGameController)
public final func TandemCall(mode: Int32, name: String, color: HDRColor, status: String, gameCall: Bool) -> Bool {
  if mode == 0 {
    if IsDefined(this.m_tandemCall) {
      inkCompoundRef.RemoveChild(this.holoAudioCallElement.slot, this.m_tandemCall);
      this.m_tandemCall = null;
      if !gameCall && !IsDefined(this.holoAudioCallElement.widget) {
        this.DeactivatePhoneElement(gameuiActivePhoneElement.Call);
      }
    }
    return true;
  }
  if !IsDefined(this.m_tandemCall) {
    if mode >= 4 || IsDefined(this.holoAudioCallElement.widget) {
      return false;
    }
    let widget = this.SpawnFromLocal(inkWidgetRef.Get(this.holoAudioCallElement.slot), this.holoAudioCallElement.libraryID);
    if !IsDefined(widget) || !IsDefined(widget.GetController() as HoloAudioCallLogicController) {
      return false;
    }
    this.m_tandemCall = widget;
    this.ActivatePhoneElement(gameuiActivePhoneElement.Call);
  }
  let call = this.m_tandemCall.GetController() as HoloAudioCallLogicController;
  if !IsDefined(call) {
    return false;
  }
  let label = name;
  let avatar = TDBID.None(); // a player is no journal contact: the unknown avatar
  switch mode {
    case 2:
      this.PlaySound(n"PhoneCallPopup", n"OnOpen");
      call.ShowIncomingContact(avatar, label);
      call.Show();
      break;
    case 3:
      call.StartAudiocall(avatar, label, true);
      call.Show();
      break;
    case 4:
      call.ShowEndCallContact(avatar, label);
      break;
    default:
      call.TandemHide();
      break;
  }
  let text = status;
  call.SetStatusText(text);
  call.TandemTint(color);
  return true;
}

// The portrait finished hiding (or is gone).
@addMethod(NewHudPhoneGameController)
public final func TandemCallHidden() -> Bool {
  if !IsDefined(this.m_tandemCall) {
    return true;
  }
  let call = this.m_tandemCall.GetController() as HoloAudioCallLogicController;
  return !IsDefined(call) || call.TandemHidden();
}

// Its name, the unknown avatar in the caller's colour, the reject glyph (a
// partner's call can be declined), and the game's ring animation.
@addMethod(IncomingCallLogicController)
public final func TandemSetCall(name: String, color: HDRColor) -> Void {
  inkTextRef.SetLetterCase(this.m_contactNameWidget, textLetterCase.UpperCase);
  inkTextRef.SetText(this.m_contactNameWidget, name);
  InkImageUtils.RequestAvatarOrUnknown(this, this.m_avatar, TDBID.None());
  inkWidgetRef.SetTintColor(this.m_avatar, color);
  inkWidgetRef.SetVisible(this.m_buttonHint, true);
  this.GetRootWidget().SetVisible(true);
  this.TandemRingOn();
}

// The ring animation again once it ended (the game's call removes the
// widget then; ours rings until answered or declined). No callback.
@addMethod(IncomingCallLogicController)
public final func TandemRingOn() -> Void {
  if IsDefined(this.m_animProxy) && this.m_animProxy.IsPlaying() {
    return;
  }
  if IsDefined(this.m_animProxy) {
    this.m_animProxy.Stop();
  }
  this.m_animProxy = this.PlayLibraryAnimation(n"ring");
}

@addMethod(HoloAudioCallLogicController)
public final func TandemTint(color: HDRColor) -> Void {
  if IsDefined(this.m_AvatarController) {
    this.m_AvatarController.TandemTint(color);
  }
}

@addMethod(HoloAudioCallLogicController)
public final func TandemHide() -> Void {
  if IsDefined(this.m_AvatarController) {
    this.m_AvatarController.Hide();
  } else {
    this.Hide();
  }
}

@addMethod(HoloAudioCallLogicController)
public final func TandemHidden() -> Bool {
  return !IsDefined(this.m_AvatarController) || Equals(this.m_AvatarController.GetState(), EHudPhoneVisibility.Invisible);
}

@addMethod(HudPhoneAvatarController)
public final func TandemTint(color: HDRColor) -> Void {
  inkWidgetRef.SetTintColor(this.m_ContactAvatar, color);
}
