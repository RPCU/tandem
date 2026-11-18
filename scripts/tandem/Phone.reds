// Calls and texts in co-op (docs/research/coop-quests-design.md §3): both
// phones ring in sync from the host's PhoneSystem, either player answers for
// the crew (the first answer wins: plugin core/story CallArbiter) and the
// answer goes to the host's phone; texts come through the journal mirror
// (Story.reds) and the first reply wins. A guest presents the host's call on
// its own HUD through its own PhoneSystem's TriggerCall (final.reds:240618),
// with its phone facts held back, so nothing it shows can wake its own
// quests: every call shown as an audio call with the caller's portrait.

public class TandemPhone {
  private let m_game: GameInstance;
  private let m_story: wref<TandemStorySystem>;
  // Guest: the host's call we present (none: phase 0 or 3).
  private let m_contact: CName;
  private let m_phase: Int32;

  public func Start(game: GameInstance, story: ref<TandemStorySystem>) -> Void {
    this.m_game = game;
    this.m_story = story;
  }

  public func Stop() -> Void {
    this.m_phase = 0;
  }

  public static func Phone(game: GameInstance) -> ref<PhoneSystem> {
    return GameInstance.GetScriptableSystemsContainer(game).Get(n"PhoneSystem") as PhoneSystem;
  }

  public func OnEvent(event: TandemStoryEvent) -> Void {
    switch event.kind {
      case 20:
        this.OnHostCall(event);
        break;
      case 21:
        this.OnGuestAction(event);
        break;
      case 22:
        this.OnGuestReply(event);
        break;
      default:
        break;
    }
  }

  // ---- guest: the host's call on our phone ----

  private func OnHostCall(event: TandemStoryEvent) -> Void {
    if !this.m_story.IsGuest() {
      return;
    }
    let phone = TandemPhone.Phone(this.m_game);
    if !IsDefined(phone) {
      return;
    }
    let audio = GameInstance.GetAudioSystem(this.m_game);
    let phase = event.state;
    let answered = event.player != 0u;
    if phase == 1 && !answered {
      if NotEquals(this.m_contact, event.contact) || this.m_phase != 1 {
        // Ringing, as the host's PhoneSystem does (final.reds:240569).
        audio.Play(n"ui_phone_incoming_call");
        TandemNative.Log(s"phone: incoming call from \(NameToString(event.contact))");
      }
    } else {
      audio.Play(n"ui_phone_incoming_call_stop");
      audio.Play(n"ui_phone_initiation_call_stop");
    }
    if phase == 1 && answered {
      let who = event.player == this.m_story.You() ? "you" : this.m_story.NameOf(event.player);
      if (event.flags & 16) != 0 {
        TandemNative.Log(s"phone: call rejected by player \(event.player)");
        if event.player != this.m_story.You() {
          this.m_story.Notice(s"\(who) declined the call.");
        }
      } else {
        TandemNative.Log(s"phone: call answered by player \(event.player)");
        if event.player != this.m_story.You() {
          this.m_story.Notice(s"\(who) answered the call.");
        }
      }
      // The widget stays until the host's call starts or ends.
      this.m_contact = event.contact;
      this.m_phase = phase;
      return;
    }
    this.m_contact = event.contact;
    this.m_phase = phase;
    phone.TandemPresent(event.contact, phase, (event.flags & 4) != 0, (event.flags & 1) != 0, event.extra);
    if phase == 2 {
      TandemNative.Log(s"phone: call with \(NameToString(event.contact)) started");
    } else if phase == 3 {
      TandemNative.Log(s"phone: call with \(NameToString(event.contact)) ended");
    }
  }

  // Leaving: no call stays up, no ringtone goes on (NCO: on every exit).
  public func Close() -> Void {
    let phone = TandemPhone.Phone(this.m_game);
    if this.m_phase == 1 || this.m_phase == 2 {
      if IsDefined(phone) {
        phone.TandemPresent(this.m_contact, 3, false, false, 0);
      }
      TandemNative.Log("phone: mirrored call closed");
    }
    let audio = GameInstance.GetAudioSystem(this.m_game);
    audio.Play(n"ui_phone_incoming_call_stop");
    audio.Play(n"ui_phone_initiation_call_stop");
    if IsDefined(phone) {
      phone.m_tandemMirror = false;
    }
    this.m_phase = 0;
  }

  public func Presenting() -> Bool {
    return this.m_phase == 1 || this.m_phase == 2;
  }

  // ---- host: a guest's answer, dial or reply ----

  private func OnGuestAction(event: TandemStoryEvent) -> Void {
    if !this.m_story.IsHostInSession() {
      return;
    }
    let phone = TandemPhone.Phone(this.m_game);
    if event.index == 3 {
      // TODO: queue questTriggerCallRequest{caller = Player} for the contact.
      TandemNative.Log(s"phone: player \(event.player) dials \(NameToString(event.contact)) (not yet)");
      return;
    }
    let reject = event.index == 2;
    if IsDefined(phone) && phone.TandemAnswerFor(event.contact, reject) {
      TandemNative.Log(s"phone: \(reject ? "rejected" : "answered") by player \(event.player)");
      this.m_story.Notice(s"\(this.m_story.NameOf(event.player)) \(reject ? "declined" : "answered") the call.");
    } else {
      TandemNative.Log(s"phone: player \(event.player)'s answer came too late");
    }
  }

  // A guest's text reply: applied as our messenger would (ActivateReply,
  // final.reds:348893), if it is still an open option.
  private func OnGuestReply(event: TandemStoryEvent) -> Void {
    if !this.m_story.IsHostInSession() {
      return;
    }
    let journal = GameInstance.GetJournalManager(this.m_game);
    let entry = journal.GetEntry(event.hash);
    if !IsDefined(entry) || !Equals(journal.GetEntryState(entry), gameJournalEntryState.Active) {
      TandemNative.Log(s"phone: reply \(event.hash) from player \(event.player) dropped (no longer open)");
      return;
    }
    let group = journal.GetParentEntry(entry);
    let filter: JournalRequestStateFilter;
    filter.active = true;
    filter.inactive = true;
    let options: array<wref<JournalEntry>>;
    journal.GetChildren(group, filter, options);
    for option in options {
      if IsDefined(option as JournalPhoneChoiceEntry) && NotEquals(option.GetId(), entry.GetId()) {
        journal.SetEntryVisited(option, true);
        journal.ChangeEntryStateByHash(Cast<Uint32>(journal.GetEntryHash(option)), gameJournalEntryState.Inactive, JournalNotifyOption.Notify);
      }
    }
    journal.SetEntryVisited(entry, true);
    journal.ChangeEntryStateByHash(event.hash, gameJournalEntryState.Succeeded, JournalNotifyOption.Notify);
    TandemNative.Log(s"phone: reply from player \(event.player) applied");
  }

  // ---- test commands (story-host.sh) ----

  // story call CONTACT | story call end CONTACT: our quests would.
  public func Command(command: String) -> Void {
    let ending = StrBeginsWith(command, "story call end ");
    let contact = StringToName(ending ? StrAfterFirst(command, "story call end ") : StrAfterFirst(command, "story call "));
    let request = new questTriggerCallRequest();
    request.caller = contact;
    request.addressee = n"Player";
    request.callPhase = ending ? questPhoneCallPhase.EndCall : questPhoneCallPhase.IncomingCall;
    request.callMode = questPhoneCallMode.Audio;
    request.isPlayerTriggered = false;
    request.isRejectable = true;
    request.showAvatar = true;
    request.visuals = questPhoneCallVisuals.Default;
    GameInstance.QueueScriptableSystemRequest(this.m_game, n"PhoneSystem", request);
    TandemNative.Log(s"story: call \(ending ? "end" : "from") \(NameToString(contact)) queued");
  }
}

// ---- the PhoneSystem (final.reds:240531) ----

// Guest: a mirrored call is up; its phone facts are not ours to write.
@addField(PhoneSystem)
public let m_tandemMirror: Bool;

// Host: a pickup tandem makes for a guest (not sent as ours).
@addField(PhoneSystem)
public let m_tandemAnswering: Bool;

// Guest: the host's call on our HUD (blackboard, widget, portrait).
@addMethod(PhoneSystem)
public final func TandemPresent(contact: CName, phase: Int32, playerCalling: Bool, rejectable: Bool, visuals: Int32) -> Void {
  this.m_tandemMirror = true;
  this.TriggerCall(questPhoneCallMode.Audio, true, contact, playerCalling, IntEnum<questPhoneCallPhase>(phase), false, rejectable, true, IntEnum<questPhoneCallVisuals>(visuals));
}

// Host: a guest answered first: the pickup our HUD would make (:408455).
@addMethod(PhoneSystem)
public final func TandemAnswerFor(contact: CName, reject: Bool) -> Bool {
  if NotEquals(this.m_LastCallInformation.callPhase, questPhoneCallPhase.IncomingCall)
    || NotEquals(this.m_LastCallInformation.contactName, contact) {
    return false;
  }
  let request = new PickupPhoneRequest();
  request.CallInformation = this.m_LastCallInformation;
  request.shouldBeRejected = reject;
  this.m_tandemAnswering = true;
  this.OnPickupPhone(request);
  this.m_tandemAnswering = false;
  return true;
}

// Host: every phase of our call goes to the guests.
@wrapMethod(PhoneSystem)
private final func OnTriggerCall(request: questTriggerCallRequest) {
  wrappedMethod(request);
  let story = TandemStorySystem.Get(this.GetGameInstance());
  if IsDefined(story) && story.IsHostInSession() {
    let info = this.m_LastCallInformation;
    let flags = 0;
    if info.isRejectable {
      flags += 1;
    }
    if info.showAvatar {
      flags += 2;
    }
    if info.isPlayerCalling {
      flags += 4;
    }
    if info.isPlayerTriggered {
      flags += 8;
    }
    TandemStory.SendCall(info.contactName, EnumInt(info.callPhase), EnumInt(info.callMode), EnumInt(info.visuals), flags, 0u);
    TandemNative.Log(s"phone: our call with \(NameToString(info.contactName)), phase \(EnumInt(info.callPhase)), sent");
  }
}

// Guest: our answer goes to the host's phone instead of our own quests.
// Host: our own answer is sent so every phone shows it picked up.
@wrapMethod(PhoneSystem)
private final func OnPickupPhone(request: PickupPhoneRequest) {
  let story = TandemStorySystem.Get(this.GetGameInstance());
  if IsDefined(story) && story.IsGuest() && this.m_tandemMirror {
    let audio = GameInstance.GetAudioSystem(this.GetGameInstance());
    audio.Play(n"ui_phone_initiation_call_stop");
    audio.Play(n"ui_phone_incoming_call_stop");
    TandemStory.SendPhoneAction(request.shouldBeRejected ? 2 : 1, request.CallInformation.contactName);
    TandemNative.Log(s"phone: our \(request.shouldBeRejected ? "reject" : "answer") sent to the host");
    return;
  }
  let ringing = Equals(this.m_LastCallInformation.callPhase, questPhoneCallPhase.IncomingCall)
    && Equals(request.CallInformation.contactName, this.m_LastCallInformation.contactName);
  wrappedMethod(request);
  if IsDefined(story) && story.IsHostInSession() && ringing && !this.m_tandemAnswering {
    let info = this.m_LastCallInformation;
    TandemStory.SendCall(info.contactName, EnumInt(info.callPhase), EnumInt(info.callMode), EnumInt(info.visuals), request.shouldBeRejected ? 16 : 0, story.You());
  }
}

// Guest: no phonecall_* fact while a mirrored call is up (they would wake
// our own quests when they resume).
@wrapMethod(PhoneSystem)
private final func SetPhoneFact(isPlayerCalling: Bool, contactName: CName, state: questPhoneTalkingState) {
  if this.m_tandemMirror {
    let story = TandemStorySystem.Get(this.GetGameInstance());
    if IsDefined(story) && story.IsGuest() {
      return;
    }
  }
  wrappedMethod(isPlayerCalling, contactName, state);
}

// Guest: a text reply goes to the host; the journal mirror brings back what
// the host applied (the first reply wins there).
@wrapMethod(MessengerDialogViewController)
private final func ActivateReply(target: ListItemController) {
  let story: ref<TandemStorySystem>;
  if IsDefined(this.m_playerObject) {
    story = TandemStorySystem.Get(this.m_playerObject.GetGame());
  }
  if IsDefined(story) && story.Mirroring() {
    let data = target.GetData() as JournalEntryListItemData;
    if IsDefined(data) && IsDefined(data.m_entry) {
      TandemStory.SendReply(Cast<Uint32>(this.m_journalManager.GetEntryHash(data.m_entry)));
      TandemNative.Log("phone: our reply sent to the host");
      return;
    }
  }
  wrappedMethod(target);
}
