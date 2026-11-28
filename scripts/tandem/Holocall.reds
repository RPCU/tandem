// A call between players, the game's own call (docs/architecture/comms.md,
// "The call on screen"): a partner's link request rings as an NPC's call
// does, and our own call to them shows as ours to an NPC: the phone system
// runs it (PhoneSystem's questTriggerCallRequest, the UI_ComDevice call
// information, the HUD phone's incoming-call widget, ringtone, call view,
// timeout and hang-up), its contact a journal contact of the partner's own
// that the call UI finds by name (TandemCommsNative.PartnerContact: their
// name, the unknown avatar; research engine/comms.md §13.163). The link
// itself is Comms.reds' and unchanged: this only places the call.
//
// The partner's contact is no journal entry: the call UI's own look-up
// (GetIncomingContact, by the call's contact name) is wrapped to find it,
// and its phone facts (phonecall_tandem_partner_*) are never written
// (Phone.reds), nor sent as a story call. A call of the game's comes first:
// ours is not placed while one rings or is on.

public class TandemHolocall {
  private let m_game: GameInstance;
  // Who the call is with, and its state: 0 none, 1 their call rings, 2 we
  // call them, 3 on the call, 5 the call view closes with the link on.
  private let m_player: Uint32;
  private let m_name: String;
  private let m_mode: Int32;
  // We placed the call (they did not call us).
  private let m_ours: Bool;
  private let m_since: Float;
  // The phase the phone system was last given for our call (-1 none).
  private let m_placed: Int32;
  private let m_contacts: array<ref<JournalContact>>;
  private let m_hint: Bool;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_placed = -1;
  }

  public func Stop() -> Void {
    this.Clear();
    ArrayClear(this.m_contacts);
  }

  // ---- what Comms.reds says ----

  // Their link request rings here.
  public func Ring(player: Uint32, name: String, now: Float) -> Void {
    if this.m_player != player && this.m_mode != 0 {
      this.Clear();
    }
    this.Set(player, name, 1, now);
    this.m_ours = false;
  }

  // We call them (our request, from the phone or a ping on their V).
  public func Calling(player: Uint32, name: String, now: Float) -> Void {
    if this.m_mode == 3 && this.m_player == player {
      return;
    }
    this.Set(player, name, 2, now);
    this.m_ours = true;
  }

  // The link is made: the call we placed or answered is on.
  public func Answered(player: Uint32, name: String, now: Float) -> Void {
    if (this.m_mode == 1 || this.m_mode == 2) && this.m_player == player {
      this.Set(player, name, 3, now);
    }
  }

  // Our call to them was not taken (declined, busy, no answer, gone): the
  // phone's call view says why for 2 s (the game's own status line), then
  // the call ends.
  public func Refused(player: Uint32, status: String, now: Float) -> Void {
    if this.m_player != player || this.m_mode != 2 {
      return;
    }
    TandemNative.Log(s"holocall: \(this.m_name): \(status)");
    let request = new questSetPhoneStatusRequest();
    request.status = StringToName(status);
    GameInstance.QueueScriptableSystemRequest(this.m_game, n"PhoneSystem", request);
    this.m_mode = 4;
    this.m_since = now;
  }

  // We are calling them now.
  public func IsCalling(player: Uint32) -> Bool {
    return this.m_player == player && this.m_mode == 2;
  }

  // Their request stopped ringing unanswered (declined, taken back, gone).
  public func RingStopped(player: Uint32) -> Void {
    if this.m_mode == 1 && this.m_player == player {
      this.Clear();
    }
  }

  // The call is over: the link broke, or ours was cancelled.
  public func Ended(player: Uint32, now: Float) -> Void {
    if this.m_player != player || this.m_mode == 0 {
      return;
    }
    if this.m_mode != 1 {
      TandemNative.Log(s"holocall: the call with \(this.m_name) ended");
    }
    this.Clear();
  }

  // The call's own button applies: we call them or are on the call.
  public func CanHangUp() -> Bool {
    return this.m_mode == 2 || this.m_mode == 3;
  }

  public func Player() -> Uint32 {
    return this.CanHangUp() ? this.m_player : 0u;
  }

  // Our call is on the HUD (the party HUD moves below it, presence.md).
  public func Up() -> Bool {
    return this.m_placed == 0 || this.m_placed == 1;
  }

  // Ours, then the phone's own call as the game holds it (the UI_ComDevice
  // board: its contact and phase, 0 none 1 ringing 2 on 3 ended) and the
  // name and avatar its call UI found for that contact.
  public func Report() -> String {
    let line = s"holocall: mode \(this.m_mode) with \(this.m_player != 0u ? this.m_name : "nobody"), placed \(this.m_placed)";
    let board = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UI_ComDevice);
    if !IsDefined(board) {
      return line + "; the phone: no board";
    }
    let info = board.GetVariant(GetAllBlackboardDefs().UI_ComDevice.PhoneCallInformation);
    if !IsDefined(info) {
      return line + "; the phone: no call";
    }
    let call = FromVariant<PhoneCallInformation>(info);
    let contact = TandemHolocall.ContactOf(this.m_game, call.contactName);
    let shown = IsDefined(contact) ? contact.GetLocalizedName(GameInstance.GetJournalManager(this.m_game)) : "(a journal contact)";
    let avatar = IsDefined(contact) ? TDBID.ToStringDEBUG(contact.GetAvatarID(GameInstance.GetJournalManager(this.m_game))) : "";
    return line + s"; the phone: \(NameToString(call.contactName)) phase \(EnumInt(call.callPhase)) \(call.isPlayerCalling ? "ours" : "theirs"), contact \(shown) \(avatar); the HUD's ring: \(TandemHolocall.Ringing())";
  }

  // The name the HUD phone's incoming-call widget shows ("" when none rings).
  public static func Ringing() -> String {
    let layer = GameInstance.GetInkSystem().GetLayer(n"inkHUDLayer");
    if !IsDefined(layer) {
      return "";
    }
    for controller in layer.GetGameControllers() {
      let phone = controller as NewHudPhoneGameController;
      if IsDefined(phone) {
        return phone.TandemRinging();
      }
    }
    return "";
  }

  // The partner's contact the call UI looks up (GetIncomingContact).
  public static func ContactOf(game: GameInstance, contact: CName) -> wref<JournalContact> {
    let comms = TandemComms.Of(game);
    let call = IsDefined(comms) ? comms.Holocall() : null;
    return IsDefined(call) ? call.Contact(contact) : null;
  }

  public static func IsPartner(contact: CName) -> Bool {
    return StrBeginsWith(NameToString(contact), "tandem_partner_");
  }

  public func Contact(contact: CName) -> wref<JournalContact> {
    let id = NameToString(contact);
    for c in this.m_contacts {
      if Equals(c.GetId(), id) {
        return c;
      }
    }
    return null;
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
    if this.m_mode == 0 {
      return;
    }
    if gameCall && this.m_placed < 0 {
      // The game's call has the phone: ours is not placed (the link stays
      // as it is; the request's notice and hints say it).
      return;
    }
    switch this.m_mode {
      case 1:
        this.Place(this.m_player, this.m_name, false, 0);
        break;
      case 2:
        // Unanswered (the request stands, as the phone's row says).
        if now - this.m_since > 20.0 || now < this.m_since {
          TandemNative.Log(s"holocall: \(this.m_name) did not answer: the call ended");
          this.Clear();
          break;
        }
        this.Place(this.m_player, this.m_name, true, 0);
        break;
      case 3:
        this.Place(this.m_player, this.m_name, this.m_ours, 1);
        if now - this.m_since > 8.0 || now < this.m_since {
          // A link lasts the session: the call is its first seconds, then
          // the party HUD shows it (presence.md).
          TandemNative.Log(s"holocall: the call with \(this.m_name) closed, the link stays");
          this.Clear();
        }
        break;
      case 4:
        // Not taken: its status shown, then the phone's own end.
        if now - this.m_since > 2.0 || now < this.m_since {
          this.Clear();
        }
        break;
      default:
        this.Clear();
        break;
    }
    this.ShowHint(this.CanHangUp() && this.m_placed >= 0);
  }

  // The phone system told the phase (0 ringing, 1 on the call), once.
  private func Place(player: Uint32, name: String, ourCall: Bool, phase: Int32) -> Void {
    if this.m_placed == phase {
      return;
    }
    let contact = this.Make(player, name);
    if !IsDefined(contact) {
      TandemDegraded.Fallback("partner call", s"\(name): no journal contact could be made: no call on the phone, the request's notice only");
      this.m_mode = 0;
      return;
    }
    let request = new questTriggerCallRequest();
    let id = StringToName(contact.GetId());
    request.caller = ourCall ? n"Player" : id;
    request.addressee = ourCall ? id : n"Player";
    request.callPhase = phase == 0 ? questPhoneCallPhase.IncomingCall : questPhoneCallPhase.StartCall;
    request.callMode = questPhoneCallMode.Audio;
    request.isPlayerTriggered = false;
    request.isRejectable = !ourCall;
    request.showAvatar = true;
    request.visuals = questPhoneCallVisuals.Default;
    GameInstance.QueueScriptableSystemRequest(this.m_game, n"PhoneSystem", request);
    this.m_placed = phase;
    TandemNative.Log(s"holocall: \(phase == 0 ? (ourCall ? "calling" : "ringing from") : "on the call with") \(name): the phone's own call");
  }

  // The partner's contact, made once per player (and renamed as they are).
  private func Make(player: Uint32, name: String) -> ref<JournalContact> {
    let id = s"tandem_partner_\(player)";
    for c in this.m_contacts {
      if Equals(c.GetId(), id) {
        TandemCommsNative.RenameContact(c, name);
        return c;
      }
    }
    let made = TandemCommsNative.PartnerContact(player, name);
    if IsDefined(made) {
      ArrayPush(this.m_contacts, made);
    }
    return made;
  }

  // The call ended: the phone system's own end (the call view closes with
  // its animation), nothing of ours left.
  private func Clear() -> Void {
    if this.m_placed >= 0 && this.m_player != 0u {
      let request = new questTriggerCallRequest();
      let id = StringToName(s"tandem_partner_\(this.m_player)");
      request.caller = id;
      request.addressee = n"Player";
      request.callPhase = questPhoneCallPhase.EndCall;
      request.callMode = questPhoneCallMode.Audio;
      request.isPlayerTriggered = false;
      request.showAvatar = true;
      request.visuals = questPhoneCallVisuals.Default;
      GameInstance.QueueScriptableSystemRequest(this.m_game, n"PhoneSystem", request);
    }
    if this.m_mode == 4 {
      let status = new questSetPhoneStatusRequest();
      status.status = n"";
      GameInstance.QueueScriptableSystemRequest(this.m_game, n"PhoneSystem", status);
    }
    this.m_placed = -1;
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
}

// ---- the call UI's look-up of the caller (final.reds:413190, :391401) ----
// Game thread (the HUD's own call listeners): only a read of ours.

@wrapMethod(NewHudPhoneGameController)
public final func GetIncomingContact() -> wref<JournalContact> {
  let contact = wrappedMethod();
  if !IsDefined(contact) && TandemHolocall.IsPartner(this.m_CurrentCallInformation.contactName) {
    return TandemHolocall.ContactOf(this.m_player.GetGame(), this.m_CurrentCallInformation.contactName);
  }
  return contact;
}

@wrapMethod(IncomingCallGameController)
private final func GetIncomingContact(phoneCallInfo: PhoneCallInformation) -> wref<JournalContact> {
  let contact = wrappedMethod(phoneCallInfo);
  if !IsDefined(contact) && TandemHolocall.IsPartner(phoneCallInfo.contactName) {
    return TandemHolocall.ContactOf(this.m_player.GetGame(), phoneCallInfo.contactName);
  }
  return contact;
}

// (test) the caller's name on the incoming-call widget, as drawn.
@addMethod(NewHudPhoneGameController)
public final func TandemRinging() -> String {
  let widget = this.incomingCallElement.widget;
  let ring = IsDefined(widget) ? widget.GetController() as IncomingCallLogicController : null;
  return IsDefined(ring) ? ring.TandemName() : "";
}

@addMethod(IncomingCallLogicController)
public final func TandemName() -> String {
  return this.GetRootWidget().IsVisible() ? inkTextRef.GetText(this.m_contactNameWidget) : "";
}
