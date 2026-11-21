// Every string tandem shows the player, in one place
// (docs/architecture/strings.md).
//
// Texts are keyed ("Tandem-" + the key the scripts use), English is the
// source, and each language of the game we translate has a package below.
// They are registered with the game's own localization through Codeware
// (ModLocalizationProvider: its native side merges them into the on-screen
// texts as a language loads), so a key works wherever the game takes one:
// GetLocalizedTextByKey, "LocKey#..." fields of native widgets, the main
// menu. A language with no package gets English.
//
// Scripts never show a literal: TandemText.Get / F for a text,
// TandemText.Notice for the HUD's warning line, TandemText.Hint for an input
// hint (the game draws the player's own binding and device).
// tools/check-strings.py (task strings:check) refuses a literal in those
// places, a key that does not exist, and a key missing from a translation.

import Codeware.Localization.*

public class TandemText {
  // The text of a key, in the game's language.
  public static func Get(key: String) -> String {
    let full = "Tandem-" + key;
    let text = GetLocalizedTextByKey(StringToName(full));
    if StrLen(text) > 0 && NotEquals(text, full) {
      return text;
    }
    // Not merged (the texts were loaded before Codeware saw us): English.
    return TandemText.English(key);
  }

  // The source text of a key (the fallback).
  public static func English(key: String) -> String {
    let package = new TandemTextEnglish();
    let entry = package.GetEntries(EntryType.Interface).Get(LocalizationEntry.Hash("Tandem-" + key)) as LocalizationEntry;
    return IsDefined(entry) ? entry.GetVariant(PlayerGender.Default) : key;
  }

  // The same with its {1} and {2} filled in (names, addresses, numbers).
  public static func F(key: String, a: String, opt b: String) -> String {
    return TandemText.Fill(TandemText.Get(key), a, b);
  }

  // In English for the log (tests read it whatever the game's language), else
  // in the game's language.
  public static func T(english: Bool, key: String) -> String {
    return english ? TandemText.English(key) : TandemText.Get(key);
  }

  public static func FE(english: Bool, key: String, a: String, opt b: String) -> String {
    return TandemText.Fill(TandemText.T(english, key), a, b);
  }

  public static func Fill(text: String, a: String, b: String) -> String {
    return StrReplaceAll(StrReplaceAll(text, "{1}", a), "{2}", b);
  }

  // A vanilla text (the game's own key), for phrases the game already has.
  public static func Vanilla(key: String) -> String {
    return GetLocalizedText(key);
  }

  // A text that came over the wire as a key (a regroup offer's label); one
  // we do not know is shown as it came.
  public static func OfWire(label: String) -> String {
    let text = TandemText.Get(label);
    return Equals(text, label) ? label : text;
  }

  // Why a session ended or a relay failed: the plugin's words (English, the
  // log's), translated when we know them.
  public static func Reason(reason: String, opt english: Bool) -> String {
    if english {
      return reason;
    }
    let key = TandemText.ReasonKey(reason);
    if StrLen(key) > 0 {
      return TandemText.Get(key);
    }
    if StrBeginsWith(reason, "refused: ") {
      return TandemText.F("Reason-Refused", TandemText.Reason(StrAfterFirst(reason, "refused: ")));
    }
    if StrBeginsWith(reason, "protocol version ") {
      return TandemText.F("Reason-Version", StrAfterFirst(reason, "protocol version "));
    }
    return reason;
  }

  // The plugin's reasons (core/src/endpoint.cpp, relay.cpp) and their keys.
  private static func ReasonKey(reason: String) -> String {
    let reasons = [
      "the session is full", "the host is gone", "could not reach the host", "connecting",
      "the relay did not answer", "the relay refused our host token (relay_token)",
      "the session is not on the relay (the host left)", "the relay has too many sessions open",
      "another host holds this session on the relay", "no session has this join code (wrong, or expired)",
      "too many join attempts: try again in a minute", "could not open a socket to the relay",
      "the session is full (4 players at most)", "Resyncing with the host...",
      "the relay went silent: reconnecting", "lost the relay",
      "the relay's replicas sent us on twice (their peers lists disagree)",
      "the relay sent us to an address we cannot reach (IPv6)",
      "the relay is restarting: try again in a moment",
      "the relay hosts as many sessions from your address as it may"
    ];
    let keys = [
      "Reason-Full", "Reason-HostGone", "Reason-Unreachable", "Reason-Connecting",
      "Reason-RelaySilent", "Reason-RelayToken",
      "Reason-RelayNoSession", "Reason-RelayBusy",
      "Reason-RelayTaken", "Reason-RelayNoCode",
      "Reason-RelayThrottled", "Reason-RelaySocket",
      "Reason-FullMax", "Session-Resync",
      "Reason-RelayReconnecting", "Reason-RelayLost",
      "Reason-RelayLoop",
      "Reason-RelayIpv6",
      "Reason-RelayDraining",
      "Reason-RelaySourceBusy"
    ];

    let i = 0;
    while i < ArraySize(reasons) {
      if Equals(reasons[i], reason) {
        return keys[i];
      }
      i += 1;
    }
    return "";
  }

  // The HUD's warning line, the game's own short on-screen message. The log
  // names the key: tests read that, whatever the game's language.
  public static func Notice(game: GameInstance, key: String, opt a: String, opt b: String) -> Void {
    TandemText.NoticeAs(game, SimpleMessageType.Neutral, 5.0, key, a, b);
  }

  public static func NoticeAs(game: GameInstance, kind: SimpleMessageType, seconds: Float, key: String, opt a: String, opt b: String) -> Void {
    TandemText.Say(game, kind, seconds, key, TandemText.F(key, a, b), TandemText.FE(true, key, a, b));
  }

  // A notice about something the plugin said (a session's end, a refusal):
  // its words translated when we know them, in English in the log. No key:
  // the reason alone.
  public static func NoticeReason(game: GameInstance, key: String, reason: String) -> Void {
    let text = TandemText.Reason(reason);
    if StrLen(key) > 0 {
      TandemText.Say(game, SimpleMessageType.Neutral, 5.0, key, TandemText.F(key, text), TandemText.FE(true, key, reason));
    } else {
      TandemText.Say(game, SimpleMessageType.Neutral, 5.0, "", text, reason);
    }
  }

  // A notice whose text is already made (several keys joined), with its
  // English for the log.
  public static func Say(game: GameInstance, kind: SimpleMessageType, seconds: Float, key: String, text: String, english: String) -> Void {
    // The settings page's notices (all, important, minimal): logged either way.
    let shown = TandemText.Importance(key) >= TandemSettings.NoticeLevel();
    TandemNative.Log(s"coop notice: \(english) [\(key)]\(shown ? "" : " (not shown: notices)")");
    if !shown {
      return;
    }
    let message: SimpleScreenMessage;
    message.isShown = true;
    message.duration = seconds;
    message.message = text;
    message.type = kind;
    GameInstance
      .GetBlackboardSystem(game)
      .Get(GetAllBlackboardDefs().UI_Notifications)
      .SetVariant(GetAllBlackboardDefs().UI_Notifications.WarningMessage, ToVariant(message), true);
  }

  // 2: asks for an answer or went wrong (always shown), 1: the session and
  // lives (shown unless notices = minimal), 0: news (notices = all).
  public static func Importance(key: String) -> Int32 {
    let asks = [
      "", "Session-Ended", "Story-NotRestored", "Story-SceneStarted", "Scene-InScene", "Transit-Leaving",
      "Travel-FastTravelledTo", "Travel-FastTravelled", "Travel-Labelled", "Travel-Scene",
      "Travel-Taken", "Travel-Asks", "Travel-Blocked", "Revive-Down", "Revive-DownAlone",
      "Revive-Out", "Revive-AllDown", "Revive-WaitHost", "Activities-AsksSleep", "Activities-AsksWait",
      "Activities-CannotSkip", "Trade-Asks", "Comms-LinkAsked", "Comms-LinkBusy", "Saves-Failed", "Saves-WhenYouCan",
      "Saves-NotRestored", "Mods-Replaced-Item", "Trade-Unknown", "Exit-Saving", "Exit-NotSaved",
      "Checkpoint-Saving", "Checkpoint-Failed",
      "Interact-HostOnly", "Interact-Taken", "Interact-TooFar",
      "Interact-Missing", "Interact-Busy", "Trade-NotNow", "Phone-Unreachable"
    ];
    if ArrayContains(asks, key) {
      return 2;
    }
    // A record shown otherwise (Mods.reds): never only in the log.
    return StrBeginsWith(key, "Session-") || StrBeginsWith(key, "Checkpoint-") || StrBeginsWith(key, "Revive-") || StrBeginsWith(key, "Mods-") || StrBeginsWith(key, "World-") || StrBeginsWith(key, "Time-Mod") ? 1 : 0;
  }

  // An input hint in the HUD's hint bar: the game draws the key or button
  // the player bound to `action`, for the device in their hands.
  public static func Hint(game: GameInstance, source: CName, action: CName, show: Bool, label: String, hold: inkInputHintHoldIndicationType) -> Void {
    let data: InputHintData;
    data.action = action;
    data.source = source;
    data.localizedLabel = label;
    data.queuePriority = 10;
    data.sortingPriority = 10;
    data.holdIndicationType = hold;
    let evt = new UpdateInputHintEvent();
    evt.data = data;
    evt.show = show;
    evt.targetHintContainer = n"GameplayInputHelper";
    GameInstance.GetUISystem(game).QueueEvent(evt);
  }

  // "A", "A or B", "A, B or C".
  public static func AnyOf(names: array<String>, opt english: Bool) -> String {
    let text = "";
    let count = ArraySize(names);
    let i = 0;
    while i < count {
      if i > 0 {
        text += i == count - 1 ? TandemText.T(english, "List-Or") : ", ";
      }
      text += names[i];
      i += 1;
    }
    return text;
  }

  // A player with no name yet.
  public static func Unnamed(player: Uint32) -> String {
    return TandemText.F("Player-Unnamed", s"\(player)");
  }
}

public class TandemTextProvider extends ModLocalizationProvider {
  public func GetPackage(language: CName) -> ref<ModLocalizationPackage> {
    switch language {
      case n"en-us": return new TandemTextEnglish();
      case n"fr-fr": return new TandemTextFrench();
      default: return null;
    }
  }

  public func GetFallback() -> CName {
    return n"en-us";
  }
}

// The source. Voice: the game's UI, short, no full stop on a single
// sentence; widgets that show capitals (menu lists, the progress bar) do the
// capitals themselves.
public class TandemTextEnglish extends ModLocalizationPackage {
  protected func DefineTexts() -> Void {
    // The co-op menu.
    this.Text("Tandem-Menu-Coop", "Co-op");
    this.Text("Tandem-Menu-NewHost", "Start a new co-op game");
    this.Text("Tandem-Menu-Host", "Host a saved game");
    this.Text("Tandem-Menu-NewV", "Create a new V");
    this.Text("Tandem-Menu-Join", "Join a game");
    this.Text("Tandem-Menu-Solo", "Play solo");
    this.Text("Tandem-Menu-HostNow", "Host this game");
    this.Text("Tandem-Menu-Invite", "Invite");
    this.Text("Tandem-Menu-Leave", "Leave session");
    this.Text("Tandem-Menu-EnterCode", "Enter a join code");
    this.Text("Tandem-Menu-JoinAddress", "Join by address");
    this.Text("Tandem-Menu-Back", "Back");
    this.Text("Tandem-Menu-Report", "Write a bug report");
    this.Text("Tandem-Popup-CoopOff", "Co-op is off");
    this.Text("Tandem-Session-NeedsExpansion", "Co-op is off: tandem needs Phantom Liberty on every player's game");
    this.Text("Tandem-Popup-Report", "Bug report");
    this.Text("Tandem-Menu-Recent", "{1} ({2})");
    // Its status panel.
    this.Text("Tandem-Status-HostAtLoad", "You host when your game loads (port {1})");
    this.Text("Tandem-Status-HostFailed", "Could not host: {1}");
    this.Text("Tandem-Status-Hosting", "Hosting on port {1}");
    this.Text("Tandem-Status-Code", "Join code: {1}");
    this.Text("Tandem-Status-CodeKept", "Join code: {1} (kept: the same next time)");
    this.Text("Tandem-Status-CheckDirect", "Connection check: direct connections likely");
    this.Text("Tandem-Status-CheckRelay", "Connection check: relay only (your network changes ports)");
    this.Text("Tandem-Status-Direct", "Direct connection: {1} (not through the relay)");
    this.Text("Tandem-Menu-NewCode", "New join code");
    this.Text("Tandem-Status-Relay", "Relay: {1}");
    this.Text("Tandem-Status-JoinAtLoad", "You join {1} when your game loads");
    this.Text("Tandem-Status-InSessionOf", "In {1}'s session");
    this.Text("Tandem-Status-InSessionAt", "In the session at {1}");
    this.Text("Tandem-Status-Retrying", "{1} not reached ({2}), trying again");
    this.Text("Tandem-Status-Connecting", "Connecting to {1}...");
    this.Text("Tandem-Status-JoinFailed", "Could not join {1}");
    this.Text("Tandem-Status-None", "Not in a session");
    this.Text("Tandem-Status-Host", "{1} (host)");
    this.Text("Tandem-Status-You", "{1} (you)");
    this.Text("Tandem-Status-Ping", "{1} - {2} ms");
    this.Text("Tandem-Party-Metres", "{1} m");
    this.Text("Tandem-Party-Kilometres", "{1} km");
    this.Text("Tandem-Party-Down", "Down");
    this.Text("Tandem-Party-Reviving", "Reviving {1}");
    this.Text("Tandem-Party-Out", "Out");
    this.Text("Tandem-Party-Scene", "In a scene");
    this.Text("Tandem-Party-Menu", "In a menu");
    this.Text("Tandem-Party-Cyberspace", "Cyberspace");
    this.Text("Tandem-Party-Braindance", "Braindance");
    this.Text("Tandem-Party-Johnny", "As Johnny");
    this.Text("Tandem-Party-OtherBody", "As someone else");
    this.Text("Tandem-Party-Elsewhere", "Elsewhere");
    this.Text("Tandem-Status-Recorder", "Recorder: {1}");
    this.Text("Tandem-Common-On", "on");
    this.Text("Tandem-Common-Off", "off");
    this.Text("Tandem-Status-CodeTarget", "code {1}");
    this.Text("Tandem-Status-ChooseHostSave", "Choose the save to host");
    this.Text("Tandem-Status-ChooseJoinSave", "Your V to join {1}: a new one, or a saved one");
    this.Text("Tandem-Status-PickHost", "Pick a host, or enter their join code");
    // Invites and popups.
    this.Text("Tandem-Invite-Code", "Join code {1}: friends enter it in Co-op, Join a game");
    this.Text("Tandem-Invite-NoAddress", "Friends join you on port {1} (no network address found)");
    this.Text("Tandem-Invite-Addresses", "Friends on your network join you at {1}");
    this.Text("Tandem-Popup-NeedsRelay", "Joining by code needs a relay: set one in the co-op settings");
    this.Text("Tandem-Popup-EnterCode", "The join code your friend's game shows (like K7Q-2M9)");
    this.Text("Tandem-Popup-NotATarget", "Not a join code or an address: {1}");
    this.Text("Tandem-Popup-JoinAddress", "The host's address on your network or VPN (Tailscale, NetBird...): host or host:port, port {1} if none. A direct connection, without the relay: the host's port must be open to you. Over the internet, a join code is easier.");
    this.Text("Tandem-Popup-JoinAddressLast", "Leave it empty to join {1} again.");
    this.Text("Tandem-Address-Empty", "Type the host's address (192.168.1.20, or 192.168.1.20:27450)");
    this.Text("Tandem-Address-Port", "Not a port in {1}: 1 to 65535 after the colon");
    this.Text("Tandem-Address-Name", "Not a host name or an IPv4 address: {1}");
    this.Text("Tandem-Address-Ipv6", "{1} is an IPv6 address: tandem connects over IPv4 only. Use the host's IPv4 address (Tailscale and NetBird give one, 100.x.y.z)");
    // The session.
    this.Text("Tandem-Session-Hosting", "Hosting co-op. {1}");
    this.Text("Tandem-Session-Code", "Co-op join code: {1}");
    this.Text("Tandem-Session-NoCode", "No join code yet: {1}");
    this.Text("Tandem-Session-JoinedOf", "Joined {1}'s session");
    this.Text("Tandem-Session-Joined", "Joined the co-op session");
    this.Text("Tandem-Session-YouLeft", "You left the co-op session");
    this.Text("Tandem-Session-PlayerJoined", "{1} joined the session");
    this.Text("Tandem-Session-PlayerLeft", "{1} left the session");
    this.Text("Tandem-Session-Ended", "Co-op session ended: {1}");
    this.Text("Tandem-Session-Resync", "Resyncing with the host...");
    this.Text("Tandem-Reason-Refused", "refused: {1}");
    this.Text("Tandem-Reason-Full", "the session is full");
    this.Text("Tandem-Reason-FullMax", "the session is full (4 players at most)");
    this.Text("Tandem-Reason-Version", "another version of tandem (protocol {1})");
    this.Text("Tandem-Reason-HostGone", "the host is gone");
    this.Text("Tandem-Reason-Unreachable", "could not reach the host");
    this.Text("Tandem-Reason-Connecting", "connecting");
    this.Text("Tandem-Reason-RelaySilent", "the relay did not answer");
    this.Text("Tandem-Reason-RelayToken", "the relay refused our host token");
    this.Text("Tandem-Reason-RelayNoSession", "the host left");
    this.Text("Tandem-Reason-RelayBusy", "the relay has too many sessions open");
    this.Text("Tandem-Reason-RelayTaken", "another host holds this session on the relay");
    this.Text("Tandem-Reason-RelayNoCode", "no session has this join code (wrong, or expired)");
    this.Text("Tandem-Reason-RelayThrottled", "too many join attempts: try again in a minute");
    this.Text("Tandem-Reason-RelaySocket", "could not open a socket to the relay");
    this.Text("Tandem-Reason-RelayReconnecting", "the relay went silent: reconnecting");
    this.Text("Tandem-Reason-RelayLost", "lost the relay");
    this.Text("Tandem-Reason-RelayLoop", "the relay's servers disagree (sent on twice)");
    this.Text("Tandem-Reason-RelayIpv6", "the relay sent us to an address we cannot reach (IPv6)");
    this.Text("Tandem-Reason-RelayDraining", "the relay is restarting: try again in a moment");
    this.Text("Tandem-Reason-RelaySourceBusy", "the relay hosts as many sessions from your address as it may");
    this.Text("Tandem-Player-Unnamed", "Player {1}");
    // Names (the co-op menu's prompt; the host's (2), (3)).
    this.Text("Tandem-Name-Title", "Your co-op name");
    this.Text("Tandem-Name-Ask", "The name the other players see over your V and in their party list: 1 to 16 characters (cancel: V for now)");
    this.Text("Tandem-Name-Invalid", "Not a name: {1}. 1 to 16 characters, without ; or #");
    this.Text("Tandem-Session-Renamed", "{1} is taken in this session: the others see you as {2}");
    this.Text("Tandem-List-Or", " or ");
    // Players elsewhere, pausing, on the phone.
    this.Text("Tandem-Elsewhere-Back", "{1} is back");
    this.Text("Tandem-Elsewhere-Cyberspace", "{1} is in cyberspace");
    this.Text("Tandem-Elsewhere-Braindance", "{1} is in a braindance");
    this.Text("Tandem-Elsewhere-JohnnyMemory", "{1} is living one of Johnny's memories");
    this.Text("Tandem-Elsewhere-Johnny", "{1} is Johnny Silverhand now");
    this.Text("Tandem-Elsewhere-Somewhere", "{1} is somewhere else for now");
    this.Text("Tandem-Elsewhere-Someone", "{1} is someone else for now");
    this.Text("Tandem-Pause-By", "{1} paused the game");
    this.Text("Tandem-Phone-Declined", "{1} declined the call");
    this.Text("Tandem-Phone-Answered", "{1} answered the call");
    this.Text("Tandem-Phone-HostOnly", "Only {1} can answer this contact: it is theirs alone");
    this.Text("Tandem-Phone-Away", "Too far from {1}: you can only answer calls that leave no choice");
    this.Text("Tandem-Phone-AwayReply", "Too far from {1} to reply for them");
    this.Text("Tandem-Dialogue-Suggests", "{1} suggests: {2}");
    this.Text("Tandem-Dialogue-Suggested", "Suggested: {1}");
    // Story.
    this.Text("Tandem-Story-NotRestored", "Co-op: your story could not be restored exactly. Load your last save");
    this.Text("Tandem-Story-SceneStarted", "{1} started a scene");
    this.Text("Tandem-Story-Busy", "{1} is busy");
    this.Text("Tandem-Story-Join", "Join {1}");
    this.Text("Tandem-Scene-InScene", "{1} is in a scene");
    this.Text("Tandem-Scene-Watch", "Watch the scene");
    this.Text("Tandem-Scene-LeaveCamera", "Leave the camera");
    this.Text("Tandem-Spectate-Watching", "Watching through {1}'s eyes.");
    this.Text("Tandem-Spectate-Leave", "Stop watching");
    // Quest prefabs and story gates (StoryWorld.reds).
    this.Text("Tandem-Prefabs-Missing", "{1}'s quest is somewhere your game cannot load: follow them, or watch through their eyes when the story allows.");
    this.Text("Tandem-Gates-WatsonBehind", "{1}'s story has lifted the Watson lockdown, yours has not: you cannot leave Watson on foot. Ride in {1}'s car to go with them.");
    this.Text("Tandem-Gates-WatsonBehindHost", "{1}'s story is still in the Watson lockdown: they cannot leave Watson on foot. Pick them up in your car.");
    this.Text("Tandem-Gates-DogtownBehind", "{1}'s story has opened Dogtown, yours has not: its gate stays shut for you. Ride in {1}'s car to go in with them.");
    this.Text("Tandem-Gates-DogtownBehindHost", "Dogtown is not open yet in {1}'s story: its gate stays shut for them. Pick them up in your car.");
    this.Text("Tandem-Gates-WatsonMet", "{1} is past the Watson lockdown your story still keeps. Ride in {1}'s car to follow.");
    this.Text("Tandem-Gates-DogtownMet", "{1} is in Dogtown, still shut in your story. Ride in {1}'s car to follow.");
    // Quest interactions: a guest's use of the host's objects.
    this.Text("Tandem-Interact-HostOnly", "{1} must do this");
    this.Text("Tandem-Interact-Taken", "{1}'s world: someone else got there first");
    this.Text("Tandem-Interact-TooFar", "Too far from it in {1}'s world");
    this.Text("Tandem-Interact-Missing", "Not there in {1}'s world right now");
    this.Text("Tandem-Interact-Busy", "{1} is busy: try again in a moment");
    // Loot, markers.
    this.Text("Tandem-Loot-Taken", "Someone already took it");
    this.Text("Tandem-Loot-TakenBy", "{1} picked it up first");
    // Other mods (Mods.reds, docs/architecture/mod-compat.md).
    this.Text("Tandem-Menu-Mods", "Partners' mods");
    this.Text("Tandem-Mods-TheyHave", "{1} has mods you don't ({2})");
    this.Text("Tandem-Mods-YouHave", "You have mods {1} doesn't ({2})");
    this.Text("Tandem-Mods-Same", "{1} has the same mods as you");
    this.Text("Tandem-Mods-Differ", "Mods: {1}");
    this.Text("Tandem-Mods-Category-Items", "{1} with new items (shown as similar vanilla ones)");
    this.Text("Tandem-Mods-Category-Vehicles", "{1} with new vehicles (shown as similar vanilla ones)");
    this.Text("Tandem-Mods-Category-Characters", "{1} with new characters (shown as vanilla ones)");
    this.Text("Tandem-Mods-Category-Looks", "{1} with new looks (shown with vanilla pieces)");
    this.Text("Tandem-Mods-Category-Gameplay", "{1} changing gameplay (the host's rules apply to shared enemies)");
    this.Text("Tandem-Mods-Category-Unknown", "{1} tandem can't sort");
    this.Text("Tandem-Mods-Category-Local", "{1} only changing how the game looks or sounds (fine either way)");
    this.Text("Tandem-Mods-Seen", "{1} sees {2} of your things as vanilla ones: mods they don't have");
    this.Text("Tandem-Mods-NamesCame", "{1}'s mod names are in Co-op, Partners' mods");
    this.Text("Tandem-Mods-Names", "{1}'s mods you don't have: {2}");
    this.Text("Tandem-Mods-ReplacedCount", "Shown otherwise this session ({1}):");
    this.Text("Tandem-Mods-Nothing", "nothing");
    this.Text("Tandem-Mods-NoneCompared", "No other player's mods compared yet");
    this.Text("Tandem-Mods-Replaced-Weapon", "{1}'s weapon comes from a mod you don't have: shown as a similar one ({2})");
    this.Text("Tandem-Mods-Replaced-Clothing", "{1}'s clothes come from a mod you don't have: shown plain or left off ({2} pieces)");
    this.Text("Tandem-Mods-Replaced-Look", "{1}'s look has pieces from a mod you don't have: left off ({2})");
    this.Text("Tandem-Mods-Replaced-Vehicle", "{1}'s vehicle comes from a mod you don't have: shown as a similar one ({2})");
    this.Text("Tandem-Mods-Replaced-Character", "Characters from {1}'s game come from a mod you don't have: shown as vanilla ones ({2})");
    this.Text("Tandem-Mods-Replaced-CharacterLook", "Characters from {1}'s game wear looks from a mod you don't have: shown in their usual look ({2})");
    this.Text("Tandem-Mods-Replaced-Item", "An item from {1} comes from a mod you don't have: it can't come into your game and stays theirs ({2})");
    this.Text("Tandem-Mods-Replaced-Effect", "Effects from {1}'s game come from a mod you don't have: not shown ({2})");
    this.Text("Tandem-Mods-Replaced-Radio", "{1}'s radio station isn't installed here: the radio stays off");
    this.Text("Tandem-Mods-Line-Weapon", "{1}'s weapon {2}");
    this.Text("Tandem-Mods-Line-Clothing", "{1}'s clothing {2}");
    this.Text("Tandem-Mods-Line-Look", "{1}'s look piece {2}");
    this.Text("Tandem-Mods-Line-Vehicle", "{1}'s vehicle {2}");
    this.Text("Tandem-Mods-Line-Character", "a character of {1}'s game {2}");
    this.Text("Tandem-Mods-Line-CharacterLook", "a character look of {1}'s game {2}");
    this.Text("Tandem-Mods-Line-Item", "an item from {1} {2}");
    this.Text("Tandem-Mods-Line-Effect", "an effect from {1}'s game {2}");
    this.Text("Tandem-Mods-Line-Radio", "{1}'s radio station {2}");
    this.Text("Tandem-Trade-Unknown", "{1} offers an item from a mod you don't have: it can't be traded");
    this.Text("Tandem-World-WeatherIsHost", "Another mod changes your weather: in co-op the host's weather stays");
    this.Text("Tandem-Time-ModSlow", "A mod slowed time: shared as a time bubble around you");
    this.Text("Tandem-Marker-Description", "Another V, in your city");
    this.Text("Tandem-Marker-Distance", "{1}  {2} M");
    this.Text("Tandem-Marker-Metres", "{1} M");
    // Down, revive, respawn.
    this.Text("Tandem-Revive-Down", "You are down. {1} can revive you");
    this.Text("Tandem-Revive-DownAlone", "You are down");
    this.Text("Tandem-Revive-Out", "You are out. You will get up beside {1}");
    this.Text("Tandem-Revive-AllDown", "Everyone is down");
    this.Text("Tandem-Revive-WaitHost", "Everyone is down. Waiting for {1} to load the last checkpoint");
    this.Text("Tandem-Revive-RevivedBy", "{1} revived you");
    this.Text("Tandem-Revive-BackBeside", "Back beside {1}");
    this.Text("Tandem-Revive-PlayerDown", "{1} is down");
    this.Text("Tandem-Revive-PlayerOut", "{1} is out");
    this.Text("Tandem-Revive-PlayerUp", "{1} is back up");
    this.Text("Tandem-Revive-Prompt", "Revive");
    this.Text("Tandem-Revive-Reviving", "Reviving {1}");
    this.Text("Tandem-Revive-KeepHolding", "Hold to revive");
    this.Text("Tandem-Revive-RevivingYou", "{1} is reviving you");
    this.Text("Tandem-Revive-StayDown", "Stay down");
    this.Text("Tandem-Revive-Done", "Revived");
    this.Text("Tandem-Revive-CarMoving", "Wait for the car to stop");
    this.Text("Tandem-Revive-OutOfCar", "Pulled out of the wreck");
    this.Text("Tandem-Revive-AtViktor", "Vik patched you up");
    this.Text("Tandem-Revive-AtPoint", "Patched up, back on your feet at {1}");
    this.Text("Tandem-Revive-BackInCar", "Back in {1}'s car");
    // Rides and the NCART.
    this.Text("Tandem-Transit-GotOffAt", "{1} got off the NCART at {2}");
    this.Text("Tandem-Transit-GotOff", "{1} got off the NCART");
    this.Text("Tandem-Transit-RidingToward", "{1} is riding the NCART toward {2}");
    this.Text("Tandem-Transit-Riding", "{1} is riding the NCART");
    this.Text("Tandem-Transit-Skipping", "{1} is skipping the NCART ride");
    this.Text("Tandem-Transit-Leaving", "{1}'s ride is leaving: ride along, or follow");
    this.Text("Tandem-Transit-RideWith", "Ride with {1}");
    this.Text("Tandem-Transit-Follow", "Follow");
    this.Text("Tandem-Transit-Following", "Following {1}: join them when the ride is over");
    this.Text("Tandem-Transit-YouRide", "You ride with {1}");
    this.Text("Tandem-Transit-RidingAlong", "Riding along with {1}");
    this.Text("Tandem-Transit-RideOver", "the ride is over");
    // Fast travel and regroup.
    this.Text("Tandem-Travel-OnTheWay", "{1} is on the way");
    this.Text("Tandem-Travel-Stays", "{1} stays where they are");
    this.Text("Tandem-Travel-FastTravelledTo", "{1} fast travelled to {2}");
    this.Text("Tandem-Travel-FastTravelled", "{1} fast travelled");
    this.Text("Tandem-Travel-Labelled", "{1}: {2}");
    this.Text("Tandem-Travel-Scene", "{1} is starting a scene");
    this.Text("Tandem-Travel-Taken", "{1} was taken somewhere else");
    this.Text("Tandem-Travel-Asks", "{1} asks you to join them");
    this.Text("Tandem-Travel-Join", "Join {1}");
    this.Text("Tandem-Activities-WaitingAll", "Waiting for the others to agree...");
    this.Text("Tandem-Activities-WaitingSleep", "Waiting for the others to agree to sleep until {1}...");
    this.Text("Tandem-Activities-WaitingWait", "Waiting for the others to agree to wait until {1}...");
    this.Text("Tandem-Activities-Agrees", "{1} agrees");
    this.Text("Tandem-Activities-MovedOn", "{1} moved on to {2}");
    this.Text("Tandem-Activities-Slept", "{1} slept until {2}");
    this.Text("Tandem-Activities-Waited", "{1} waited until {2}");
    this.Text("Tandem-Activities-CannotSkip", "{1} wants to skip time: you can't right now");
    this.Text("Tandem-Activities-AsksSleep", "{1} wants to sleep until {2}");
    this.Text("Tandem-Activities-AsksWait", "{1} wants to wait until {2}");
    this.Text("Tandem-Activities-AgreeSleep", "Agree to sleep until {1}");
    this.Text("Tandem-Activities-AgreeWait", "Agree to wait until {1}");
    this.Text("Tandem-Activities-WaitingFor", "Waiting for {1}...");
    this.Text("Tandem-Activities-Photo", "{1} is in photo mode");
    this.Text("Tandem-Activities-Back", "{1} is back");
    this.Text("Tandem-Contest-Duel", "{1} is in a fist fight: you can only watch");
    this.Text("Tandem-Contest-Race", "{1} is in a race");
    this.Text("Tandem-Contest-Range", "{1} is in a shooting contest");
    this.Text("Tandem-Contest-Other", "{1} is in a contest");
    this.Text("Tandem-Contest-Over", "{1}'s contest is over");
    this.Text("Tandem-Contest-WatchOnly", "This is {1}'s fight: your hits don't count");
    this.Text("Tandem-Race-Offer", "{1} is lining up for a street race");
    this.Text("Tandem-Race-Join", "Join {1}'s race");
    this.Text("Tandem-Race-Joining", "Joining {1}'s race…");
    this.Text("Tandem-Race-Joined", "{1} joins your race");
    this.Text("Tandem-Race-Grid", "On the grid: wait for the start");
    this.Text("Tandem-Race-Ready", "Get ready…");
    this.Text("Tandem-Race-Go", "Go!");
    this.Text("Tandem-Race-TooLate", "{1}'s race has started: you watch");
    this.Text("Tandem-Race-NoCar", "No car for {1}'s race: you watch");
    this.Text("Tandem-Race-Finished", "You finished #{1}");
    this.Text("Tandem-Race-Over", "{1}'s race is over");
    this.Text("Tandem-Race-Abandoned", "{1} left the race: it is over");
    this.Text("Tandem-Race-Left", "You left {1}'s race");
    this.Text("Tandem-Race-RacerLeft", "{1} left the race");
    this.Text("Tandem-Race-Standings", "Race");
    this.Text("Tandem-Race-Npc", "Racer {1}");
    this.Text("Tandem-Race-Checkpoint", "Checkpoint {1}");
    this.Text("Tandem-Race-Out", "out");
    this.Text("Tandem-Activities-CantNow", "{1} can't skip time right now");
    this.Text("Tandem-Activities-Declined", "{1} declined");
    this.Text("Tandem-Activities-NoAnswer", "{1} didn't answer");
    this.Text("Tandem-Activities-Cancelled", "Time skip cancelled");
    this.Text("Tandem-Activities-AlreadyAsking", "{1} is already asking to skip time");
    this.Text("Tandem-Activities-IsDown", "{1} is down");
    this.Text("Tandem-Activities-Bending", "{1} is bending time");
    this.Text("Tandem-Activities-Elsewhere", "{1} is somewhere else");
    this.Text("Tandem-Activities-Paused", "{1} paused the game");
    this.Text("Tandem-Activities-CantSkip", "Time can't be skipped now");
    this.Text("Tandem-Activities-SleepWith", "You go to sleep too: waiting for {1}'s answer");
    this.Text("Tandem-Breach-Already", "{1} breached this network first");
    this.Text("Tandem-Breach-AlreadyWorld", "This network was breached already");
    this.Text("Tandem-Trade-Prompt", "Trade");
    this.Text("Tandem-Trade-Asked", "Asked {1} to trade");
    this.Text("Tandem-Trade-Asks", "{1} wants to trade");
    this.Text("Tandem-Trade-HintWith", "Trade with {1}");
    this.Text("Tandem-Trade-PriceMissing", "Trade: the asked price is not on the table");
    this.Text("Tandem-Trade-Done", "Trade with {1} done");
    this.Text("Tandem-Trade-Theirs", "That is {1}'s until you both agree");
    this.Text("Tandem-Trade-NotForTrade", "Not for trade: {1}");
    this.Text("Tandem-Trade-OnTable", "It is on the table already");
    this.Text("Tandem-Trade-TableFull", "The table is full");
    this.Text("Tandem-Trade-YouHave", "You have {1} eddies");
    this.Text("Tandem-Trade-InputGive", "Eddies you give (now {1})");
    this.Text("Tandem-Trade-InputAsk", "Eddies you ask for in return (now {1})");
    this.Text("Tandem-Trade-Declined", "{1} declined to trade");
    this.Text("Tandem-Trade-NoAnswer", "{1} did not answer");
    this.Text("Tandem-Trade-TooFar", "Trade ended: {1} is too far");
    this.Text("Tandem-Trade-Combat", "Trade ended: combat");
    this.Text("Tandem-Trade-Down", "Trade ended: a player is down");
    this.Text("Tandem-Trade-Left", "Trade ended: {1} left");
    this.Text("Tandem-Trade-Busy", "{1} is trading with someone else");
    this.Text("Tandem-Trade-Refuses", "{1} does not trade");
    this.Text("Tandem-Trade-Cancelled", "Trade cancelled");
    this.Text("Tandem-Trade-Not-Inventory", "not in your inventory");
    this.Text("Tandem-Trade-Not-Item", "not an item");
    this.Text("Tandem-Trade-Not-Eddies", "eddies have their own line");
    this.Text("Tandem-Trade-Not-Quest", "a quest item");
    this.Text("Tandem-Trade-Not-Iconic", "an iconic or important item");
    this.Text("Tandem-Trade-Not-Hidden", "not an item to trade");
    this.Text("Tandem-Trade-Not-Unsellable", "it cannot be sold");
    this.Text("Tandem-Vendor-Trade", "Trade");
    this.Text("Tandem-Vendor-Dead", "{1} is dead");
    this.Text("Tandem-Vendor-NotNow", "You can't trade with {1} right now");
    this.Text("Tandem-Vendor-NotHere", "{1} doesn't trade with you yet");
    this.Text("Tandem-Vendor-Locked", "{1}'s shop isn't open to you yet");
    this.Text("Tandem-Trade-Not-Equipped", "unequip it first");
    this.Text("Tandem-Trade-Not-Single", "it cannot be given");
    this.Text("Tandem-Trade-Not-UsedUp", "it would be used up");
    this.Text("Tandem-Trade-Not-Charges", "it is counted in charges");
    this.Text("Tandem-Garage-Credited", "{1} got a vehicle: noted as earned with them");
    this.Text("Tandem-Garage-Kept", "New vehicle in your garage, earned with {1}");
    this.Text("Tandem-Garage-LendLeft", "{1}'s car: they left the session");
    this.Text("Tandem-Garage-LendAboard", "{1}'s car: only with them aboard");
    this.Text("Tandem-Garage-LendKept", "{1}'s car: they keep it to themselves");
    this.Text("Tandem-Levels-Scaled", "Co-op: enemies are scaled to level {1}");
    this.Text("Tandem-Scaling-On", "Co-op: enemies near more than one of you are tougher");
    this.Text("Tandem-Saves-Failed", "Co-op: the game could not be saved. Save it yourself to keep what your V earned.");
    this.Text("Tandem-Saves-WhenYouCan", "Co-op: save your game when you can, to keep what your V earned.");
    this.Text("Tandem-Saves-NotRestored", "Co-op: your game could not be put back as it was. Load your last save.");
    this.Text("Tandem-Exit-Saving", "Co-op: saving your V before exiting...");
    this.Text("Tandem-Exit-NotSaved", "Co-op: your V could not be saved, so the game did not exit. You are out of the session: save when you can, or exit again to quit without saving.");
    this.Text("Tandem-Exit-Title", "Exit without saving your V?");
    this.Text("Tandem-Exit-CannotSave", "Your V cannot be saved right now ({1}). Exiting now loses what it earned in this co-op session: XP, loot, rewards. Exit anyway, or stay and exit once it can be saved?");
    this.Text("Tandem-Exit-Why-Combat", "you are in combat");
    this.Text("Tandem-Exit-Why-Scene", "a scene or a quest holds the save");
    this.Text("Tandem-Exit-Why-Down", "your V is down");
    this.Text("Tandem-Exit-Why-Dirty", "your game could not be put back as it was after the session");
    this.Text("Tandem-Exit-Why-Other", "the game does not allow a save here");
    this.Text("Tandem-Checkpoint-Saving", "Co-op checkpoint: saving your V, back in the session in a moment");
    this.Text("Tandem-Checkpoint-Saved", "Co-op checkpoint: your V is saved");
    this.Text("Tandem-Checkpoint-Failed", "Co-op checkpoint: your V could not be saved this time");
    this.Text("Tandem-Comms-Enemy", "Enemy");
    this.Text("Tandem-Comms-Device", "Device");
    this.Text("Tandem-Comms-Loot", "Loot");
    this.Text("Tandem-Comms-Vehicle", "Vehicle");
    this.Text("Tandem-Comms-Danger", "Danger");
    this.Text("Tandem-Comms-Target", "Target");
    this.Text("Tandem-Comms-Here", "Here");
    this.Text("Tandem-Comms-PingOf", "{1}: {2}");
    this.Text("Tandem-Comms-YourPing", "Your ping");
    this.Text("Tandem-Comms-TheirPing", "{1}'s ping");
    this.Text("Tandem-Comms-YourTarget", "Your team target");
    this.Text("Tandem-Comms-TheirTarget", "{1}'s target: focus it");
    this.Text("Tandem-Comms-MarkTitle", "Mark target");
    this.Text("Tandem-Comms-ClearTitle", "Clear the team mark");
    this.Text("Tandem-Comms-MarkDescription", "Show your linked partners this target to focus: marked on their HUD and outlined, until it goes down, you mark another or you choose it again. No RAM, no trace.");
    this.Text("Tandem-Comms-ClearDescription", "Take this target's mark off your partners' HUDs.");
    this.Text("Tandem-Comms-OnlyShared", "Only shared targets can be marked");
    this.Text("Tandem-Comms-LinksOff", "Links are off (tandem.ini)");
    this.Text("Tandem-Comms-Linked", "Linked with {1}: scans and pings are shared");
    this.Text("Tandem-Comms-LinkBroken", "Link with {1} broken");
    this.Text("Tandem-Comms-LinkSent", "Link request sent to {1}");
    this.Text("Tandem-Comms-LinkAsked", "{1} is calling: link to share scans and pings");
    this.Text("Tandem-Comms-LinkWith", "Link with {1}");
    this.Text("Tandem-Comms-LinkBusy", "{1} took another link request: call again later");
    this.Text("Tandem-Comms-WaypointOf", "{1}'s waypoint");
    this.Text("Tandem-Comms-PinPlaced", "The map pin {1} placed");
    this.Text("Tandem-Comms-PlaceTracked", "The place {1} tracks");
    this.Text("Tandem-Comms-WaypointSet", "{1} set a waypoint");
    this.Text("Tandem-Comms-ObjectiveOf", "{1}'s objective");
    this.Text("Tandem-Comms-ObjectiveTracked", "The objective {1} tracks");
    this.Text("Tandem-Comms-Decline", "Decline");
    // The other players in the game's phone (PhoneContacts.reds).
    this.Text("Tandem-Phone-NotLinked", "Not linked");
    this.Text("Tandem-Phone-Calling", "Calling...");
    this.Text("Tandem-Phone-CallingYou", "Calling you");
    this.Text("Tandem-Phone-Linked", "Linked");
    this.Text("Tandem-Phone-Call", "Call");
    this.Text("Tandem-Phone-CallAbout", "Ask {1} to link: share scans, pings and health");
    this.Text("Tandem-Phone-Answer", "Answer");
    this.Text("Tandem-Phone-AnswerAbout", "{1} is calling: link up");
    this.Text("Tandem-Phone-Cancel", "Cancel the call");
    this.Text("Tandem-Phone-CancelAbout", "Calling {1}...");
    this.Text("Tandem-Phone-HangUp", "Hang up");
    this.Text("Tandem-Phone-HangUpAbout", "Break the link with {1}");
    this.Text("Tandem-Phone-TradeAbout", "Give, swap or sell items and eddies, wherever you both are");
    this.Text("Tandem-Phone-JoinAbout", "Fast travel to {1}");
    this.Text("Tandem-Phone-Invite", "Ask {1} to join you");
    this.Text("Tandem-Phone-InviteAbout", "{1} is offered a fast travel to you");
    this.Text("Tandem-Phone-Cancelled", "Call to {1} cancelled");
    this.Text("Tandem-Phone-Invited", "Asked {1} to join you");
    this.Text("Tandem-Phone-Unreachable", "{1} can't be reached right now");
    this.Text("Tandem-Trade-NotNow", "You can't trade right now");
    this.Text("Tandem-Story-RestrictHolstered", "{1}'s scene: weapons away.");
    this.Text("Tandem-Story-RestrictSlow", "{1}'s scene: take it slow.");
    // Another V's slow time reaches ours (Time.reds), once a session.
    this.Text("Tandem-Time-SlowedScanner", "{1}'s scanner slows you.");
    this.Text("Tandem-Time-SlowedSandevistan", "{1}'s Sandevistan slows you.");
    this.Text("Tandem-Time-SlowedOther", "{1}'s slow motion reaches you.");
    // A partner's status effect this game has no record of (StatusVisuals.reds).
    this.Text("Tandem-Status-Unknown", "{1} shows an effect this game doesn't know (a mod?): not shown.");
    this.Text("Tandem-Travel-Blocked", "You can't fast travel right now");
    // The settings page (Settings.reds).
    this.Text("Tandem-Menu-Settings", "Co-op settings");
    this.Text("Tandem-Settings-Saved", "Saved to tandem.ini as you change them");
    this.Text("Tandem-Settings-Item", "{1}: {2}");
    this.Text("Tandem-Settings-NextSession", "{1} (from the next session)");
    this.Text("Tandem-Settings-HostsRule", "{1} (the host's rule)");
    this.Text("Tandem-Settings-AfterRestart", "{1} (after a restart)");
    this.Text("Tandem-Settings-Type", "Type the new value (empty: none)");
    this.Text("Tandem-Settings-Invalid", "Not a value for this setting: {1}");
    this.Text("Tandem-Settings-NotSet", "not set");
    this.Text("Tandem-Settings-TokenSet", "set");
    this.Text("Tandem-Settings-Name", "Name");
    this.Text("Tandem-Settings-FriendlyFire", "Friendly fire");
    this.Text("Tandem-Settings-EnemyScaling", "Tougher enemies for each extra player");
    this.Text("Tandem-Settings-LevelGap", "Enemies above the lowest level");
    this.Text("Tandem-Settings-LevelGap-Eight", "8 levels at most");
    this.Text("Tandem-Settings-LevelGap-Five", "5 levels at most");
    this.Text("Tandem-Settings-LevelGap-Twelve", "12 levels at most");
    this.Text("Tandem-Settings-Notices", "Notices");
    this.Text("Tandem-Settings-Notices-All", "all");
    this.Text("Tandem-Settings-Notices-Important", "important only");
    this.Text("Tandem-Settings-Notices-Minimal", "questions and problems");
    this.Text("Tandem-Settings-Markers", "Player markers");
    this.Text("Tandem-Settings-Nameplates", "Names over players");
    this.Text("Tandem-Settings-Nameplates-Near", "close by");
    this.Text("Tandem-Settings-Nameplates-Always", "at any distance");
    this.Text("Tandem-Settings-PartyHud", "Party list (linked partners' health)");
    this.Text("Tandem-Settings-Recorder", "Recorder");
    this.Text("Tandem-Settings-Relay", "Relay");
    this.Text("Tandem-Settings-Code", "Join code");
    this.Text("Tandem-Settings-Code-New", "new each session");
    this.Text("Tandem-Settings-Code-Keep", "kept (the same every session)");
    this.Text("Tandem-Settings-Direct", "Direct connection through the relay");
    this.Text("Tandem-Settings-Direct-Auto", "when possible");
    this.Text("Tandem-Settings-Link", "Share scans and pings with");
    this.Text("Tandem-Settings-Link-Auto", "every partner");
    this.Text("Tandem-Settings-Link-Ask", "partners you link with");
    this.Text("Tandem-Settings-Pings", "Pings");
    this.Text("Tandem-Settings-Story", "Story sync");
    this.Text("Tandem-Settings-QuestFreeze", "Pause your own story (guest)");
    this.Text("Tandem-Settings-ScenePrivacy", "Private scenes");
    this.Text("Tandem-Settings-TimeHost", "Your Sandevistan (host)");
    this.Text("Tandem-Settings-TimeHost-Full", "slows your whole game");
    this.Text("Tandem-Settings-TimeHost-Bubble", "slows the NPCs around you");
    this.Text("Tandem-Settings-RaceCar", "Your car in a partner's race");
    this.Text("Tandem-Settings-RaceCar-Own", "your own");
    this.Text("Tandem-Settings-RaceCar-Copy", "a copy of theirs");
    // Under the page, the description of the row the cursor is on.
    this.Text("Tandem-Settings-About-Name", "The name the others see over your V and in their party list");
    this.Text("Tandem-Settings-About-FriendlyFire", "Host's rule: the Vs' shots, blades and explosions hurt each other");
    this.Text("Tandem-Settings-About-EnemyScaling", "Host's rule: hostile NPCs get more health for each extra player near them");
    this.Text("Tandem-Settings-About-LevelGap", "Host's rule: enemies follow the players' mean level, but never more than this above the lowest player, so a new V can fight beside a veteran");
    this.Text("Tandem-Settings-About-Notices", "Which co-op notices show: all, no news, or only questions and problems");
    this.Text("Tandem-Settings-About-Markers", "A marker for each other player on the HUD, the minimap and the map");
    this.Text("Tandem-Settings-About-Nameplates", "The name over the other players: close by, at any distance, or none");
    this.Text("Tandem-Settings-About-PartyHud", "The list under your health bar: each linked partner's name, health and state");
    this.Text("Tandem-Settings-About-Link", "Who sees your scanner tags and Ping reveals: every partner, those you link with, or nobody");
    this.Text("Tandem-Settings-About-Pings", "Off: you neither send nor see pings (the tag key outside the scanner)");
    this.Text("Tandem-Settings-About-Recorder", "Records your play for tandem's tests (recordings/auto); changes nothing in game");
    this.Text("Tandem-Settings-About-Relay", "The relay that gives a host its join code and links players across the internet");
    this.Text("Tandem-Settings-About-Code", "Host with a relay token: a new join code each session, or the same one kept");
    this.Text("Tandem-Settings-About-Direct", "Through the relay, also try a direct link (lower latency); off: the relay only");
    this.Text("Tandem-Settings-About-Story", "The host's journal, calls, texts, scenes and choices reach the guests");
    this.Text("Tandem-Settings-About-QuestFreeze", "Guest: your own story waits while you play in the host's");
    this.Text("Tandem-Settings-About-ScenePrivacy", "Romance and one-to-one scenes are yours alone: the others see you busy");
    this.Text("Tandem-Settings-About-TimeHost", "Host: your Sandevistan slows your whole game, or only the NPCs around you");
    this.Text("Tandem-Settings-About-RaceCar", "Joining a partner's street race: you line up in your own car (the one you summon), or in a copy of the starter's; with no car of your own, a copy");
  }
}

public class TandemTextFrench extends ModLocalizationPackage {
  protected func DefineTexts() -> Void {
    this.Text("Tandem-Menu-Coop", "Coop");
    this.Text("Tandem-Menu-NewHost", "Commencer une nouvelle partie coop");
    this.Text("Tandem-Menu-Host", "Héberger une partie sauvegardée");
    this.Text("Tandem-Menu-NewV", "Créer un nouveau V");
    this.Text("Tandem-Menu-Join", "Rejoindre une partie");
    this.Text("Tandem-Menu-Solo", "Jouer en solo");
    this.Text("Tandem-Menu-HostNow", "Héberger cette partie");
    this.Text("Tandem-Menu-Invite", "Inviter");
    this.Text("Tandem-Menu-Leave", "Quitter la session");
    this.Text("Tandem-Menu-EnterCode", "Saisir un code d'invitation");
    this.Text("Tandem-Menu-JoinAddress", "Rejoindre par adresse");
    this.Text("Tandem-Menu-Back", "Retour");
    this.Text("Tandem-Menu-Report", "Écrire un rapport de bug");
    this.Text("Tandem-Popup-CoopOff", "Coop désactivée");
    this.Text("Tandem-Session-NeedsExpansion", "Coop désactivée : tandem demande Phantom Liberty dans le jeu de chaque joueur");
    this.Text("Tandem-Popup-Report", "Rapport de bug");
    this.Text("Tandem-Menu-Recent", "{1} ({2})");
    this.Text("Tandem-Status-HostAtLoad", "Vous hébergez dès le chargement de votre partie (port {1})");
    this.Text("Tandem-Status-HostFailed", "Hébergement impossible : {1}");
    this.Text("Tandem-Status-Hosting", "Hébergement sur le port {1}");
    this.Text("Tandem-Status-Code", "Code d'invitation : {1}");
    this.Text("Tandem-Status-CodeKept", "Code d'invitation : {1} (gardé : le même la prochaine fois)");
    this.Text("Tandem-Status-CheckDirect", "Test de connexion : connexion directe probable");
    this.Text("Tandem-Status-CheckRelay", "Test de connexion : relais seulement (votre réseau change de port)");
    this.Text("Tandem-Status-Direct", "Connexion directe : {1} (sans le relais)");
    this.Text("Tandem-Menu-NewCode", "Nouveau code d'invitation");
    this.Text("Tandem-Status-Relay", "Relais : {1}");
    this.Text("Tandem-Status-JoinAtLoad", "Vous rejoignez {1} dès le chargement de votre partie");
    this.Text("Tandem-Status-InSessionOf", "Dans la session de {1}");
    this.Text("Tandem-Status-InSessionAt", "Dans la session {1}");
    this.Text("Tandem-Status-Retrying", "{1} injoignable ({2}), nouvel essai");
    this.Text("Tandem-Status-Connecting", "Connexion à {1}...");
    this.Text("Tandem-Status-JoinFailed", "Impossible de rejoindre {1}");
    this.Text("Tandem-Status-None", "Aucune session");
    this.Text("Tandem-Status-Host", "{1} (hôte)");
    this.Text("Tandem-Status-You", "{1} (vous)");
    this.Text("Tandem-Status-Ping", "{1} - {2} ms");
    this.Text("Tandem-Party-Metres", "{1} m");
    this.Text("Tandem-Party-Kilometres", "{1} km");
    this.Text("Tandem-Party-Down", "À terre");
    this.Text("Tandem-Party-Reviving", "Réanimation {1}");
    this.Text("Tandem-Party-Out", "Hors combat");
    this.Text("Tandem-Party-Scene", "Dans une scène");
    this.Text("Tandem-Party-Menu", "Dans un menu");
    this.Text("Tandem-Party-Cyberspace", "Cyberespace");
    this.Text("Tandem-Party-Braindance", "Danse sensorielle");
    this.Text("Tandem-Party-Johnny", "Dans la peau de Johnny");
    this.Text("Tandem-Party-OtherBody", "Dans la peau d'un autre");
    this.Text("Tandem-Party-Elsewhere", "Ailleurs");
    this.Text("Tandem-Status-Recorder", "Enregistreur : {1}");
    this.Text("Tandem-Common-On", "activé");
    this.Text("Tandem-Common-Off", "désactivé");
    this.Text("Tandem-Status-CodeTarget", "code {1}");
    this.Text("Tandem-Status-ChooseHostSave", "Choisissez la sauvegarde à héberger");
    this.Text("Tandem-Status-ChooseJoinSave", "Votre V pour rejoindre {1} : un nouveau, ou une sauvegarde");
    this.Text("Tandem-Status-PickHost", "Choisissez un hôte, ou saisissez son code d'invitation");
    this.Text("Tandem-Invite-Code", "Code d'invitation {1} : vos amis le saisissent dans Coop, Rejoindre une partie");
    this.Text("Tandem-Invite-NoAddress", "Vos amis vous rejoignent sur le port {1} (aucune adresse réseau trouvée)");
    this.Text("Tandem-Invite-Addresses", "Vos amis sur votre réseau vous rejoignent à {1}");
    this.Text("Tandem-Popup-NeedsRelay", "Rejoindre par code demande un relais : indiquez-le dans les paramètres coop");
    this.Text("Tandem-Popup-EnterCode", "Le code d'invitation qu'affiche le jeu de votre ami (comme K7Q-2M9)");
    this.Text("Tandem-Popup-NotATarget", "Ni un code d'invitation ni une adresse : {1}");
    this.Text("Tandem-Popup-JoinAddress", "L'adresse de l'hôte sur votre réseau ou votre VPN (Tailscale, NetBird...) : hôte ou hôte:port, port {1} par défaut. Une connexion directe, sans le relais : le port de l'hôte doit vous être ouvert. Par Internet, un code d'invitation est plus simple.");
    this.Text("Tandem-Popup-JoinAddressLast", "Laissez vide pour rejoindre {1} de nouveau.");
    this.Text("Tandem-Address-Empty", "Saisissez l'adresse de l'hôte (192.168.1.20, ou 192.168.1.20:27450)");
    this.Text("Tandem-Address-Port", "Port refusé dans {1} : 1 à 65535 après les deux-points");
    this.Text("Tandem-Address-Name", "Ni un nom d'hôte ni une adresse IPv4 : {1}");
    this.Text("Tandem-Address-Ipv6", "{1} est une adresse IPv6 : tandem se connecte en IPv4 seulement. Utilisez l'adresse IPv4 de l'hôte (Tailscale et NetBird en donnent une, 100.x.y.z)");
    this.Text("Tandem-Session-Hosting", "Partie coop hébergée. {1}");
    this.Text("Tandem-Session-Code", "Code d'invitation coop : {1}");
    this.Text("Tandem-Session-NoCode", "Pas encore de code d'invitation : {1}");
    this.Text("Tandem-Session-JoinedOf", "Vous avez rejoint la session de {1}");
    this.Text("Tandem-Session-Joined", "Vous avez rejoint la session coop");
    this.Text("Tandem-Session-YouLeft", "Vous avez quitté la session coop");
    this.Text("Tandem-Session-PlayerJoined", "{1} a rejoint la session");
    this.Text("Tandem-Session-PlayerLeft", "{1} a quitté la session");
    this.Text("Tandem-Session-Ended", "Session coop terminée : {1}");
    this.Text("Tandem-Session-Resync", "Resynchronisation avec l'hôte...");
    this.Text("Tandem-Reason-Refused", "refusé : {1}");
    this.Text("Tandem-Reason-Full", "la session est complète");
    this.Text("Tandem-Reason-FullMax", "la session est complète (4 joueurs au plus)");
    this.Text("Tandem-Reason-Version", "une autre version de tandem (protocole {1})");

    this.Text("Tandem-Reason-HostGone", "l'hôte est parti");
    this.Text("Tandem-Reason-Unreachable", "hôte injoignable");
    this.Text("Tandem-Reason-Connecting", "connexion");
    this.Text("Tandem-Reason-RelaySilent", "le relais n'a pas répondu");
    this.Text("Tandem-Reason-RelayToken", "le relais a refusé notre jeton d'hôte");
    this.Text("Tandem-Reason-RelayNoSession", "l'hôte est parti");
    this.Text("Tandem-Reason-RelayBusy", "le relais a trop de sessions ouvertes");
    this.Text("Tandem-Reason-RelayTaken", "un autre hôte tient cette session sur le relais");
    this.Text("Tandem-Reason-RelayNoCode", "aucune session n'a ce code d'invitation (erroné ou expiré)");
    this.Text("Tandem-Reason-RelayThrottled", "trop de tentatives : réessayez dans une minute");
    this.Text("Tandem-Reason-RelaySocket", "impossible d'ouvrir une connexion vers le relais");
    this.Text("Tandem-Reason-RelayReconnecting", "le relais ne répond plus : reconnexion");
    this.Text("Tandem-Reason-RelayLost", "relais perdu");
    this.Text("Tandem-Reason-RelayLoop", "les serveurs du relais se contredisent (renvoyé deux fois)");
    this.Text("Tandem-Reason-RelayIpv6", "le relais nous envoie vers une adresse injoignable (IPv6)");
    this.Text("Tandem-Reason-RelayDraining", "le relais redémarre : réessayez dans un instant");
    this.Text("Tandem-Reason-RelaySourceBusy", "le relais héberge déjà autant de sessions de votre adresse qu'il le peut");
    this.Text("Tandem-Player-Unnamed", "Joueur {1}");
    this.Text("Tandem-Name-Title", "Votre nom en coop");
    this.Text("Tandem-Name-Ask", "Le nom que les autres joueurs voient au-dessus de votre V et dans leur liste d'équipe : 1 à 16 caractères (annuler : V pour l'instant)");
    this.Text("Tandem-Name-Invalid", "Nom refusé : {1}. 1 à 16 caractères, sans ; ni #");
    this.Text("Tandem-Session-Renamed", "{1} est déjà pris dans cette session : les autres vous voient comme {2}");
    this.Text("Tandem-List-Or", " ou ");
    this.Text("Tandem-Elsewhere-Back", "{1} est de retour");
    this.Text("Tandem-Elsewhere-Cyberspace", "{1} est dans le cyberespace");
    this.Text("Tandem-Elsewhere-Braindance", "{1} est dans une danse sensorielle");
    this.Text("Tandem-Elsewhere-JohnnyMemory", "{1} revit un souvenir de Johnny");
    this.Text("Tandem-Elsewhere-Johnny", "{1} est Johnny Silverhand pour l'instant");
    this.Text("Tandem-Elsewhere-Somewhere", "{1} est ailleurs pour l'instant");
    this.Text("Tandem-Elsewhere-Someone", "{1} est quelqu'un d'autre pour l'instant");
    this.Text("Tandem-Pause-By", "{1} a mis le jeu en pause");
    this.Text("Tandem-Phone-Declined", "{1} a refusé l'appel");
    this.Text("Tandem-Phone-Answered", "{1} a répondu à l'appel");
    this.Text("Tandem-Phone-HostOnly", "Seul(e) {1} peut répondre à ce contact : il lui est réservé");
    this.Text("Tandem-Phone-Away", "Trop loin de {1} : vous ne pouvez répondre qu'aux appels qui ne laissent pas de choix");
    this.Text("Tandem-Phone-AwayReply", "Trop loin de {1} pour répondre à sa place");
    this.Text("Tandem-Dialogue-Suggests", "{1} suggère : {2}");
    this.Text("Tandem-Dialogue-Suggested", "Suggéré : {1}");
    this.Text("Tandem-Story-NotRestored", "Coop : votre histoire n'a pas pu être restaurée à l'identique. Chargez votre dernière sauvegarde");
    this.Text("Tandem-Story-SceneStarted", "{1} a lancé une scène");
    this.Text("Tandem-Story-Busy", "{1} est occupé(e)");
    this.Text("Tandem-Story-Join", "Rejoindre {1}");
    this.Text("Tandem-Scene-InScene", "{1} est dans une scène");
    this.Text("Tandem-Scene-Watch", "Regarder la scène");
    this.Text("Tandem-Scene-LeaveCamera", "Quitter la caméra");
    this.Text("Tandem-Spectate-Watching", "Vous regardez par les yeux de {1}.");
    this.Text("Tandem-Spectate-Leave", "Arrêter de regarder");
    this.Text("Tandem-Prefabs-Missing", "La quête de {1} se passe dans un lieu que votre jeu ne peut pas charger : suivez {1}, ou regardez par ses yeux quand l'histoire le permet.");
    this.Text("Tandem-Gates-WatsonBehind", "L'histoire de {1} a levé le confinement de Watson, pas la vôtre : vous ne pouvez pas quitter Watson à pied. Montez dans la voiture de {1} pour l'accompagner.");
    this.Text("Tandem-Gates-WatsonBehindHost", "L'histoire de {1} en est encore au confinement de Watson : {1} ne peut pas quitter Watson à pied. Passez le ou la prendre en voiture.");
    this.Text("Tandem-Gates-DogtownBehind", "L'histoire de {1} a ouvert Dogtown, pas la vôtre : sa porte reste fermée pour vous. Montez dans la voiture de {1} pour y entrer ensemble.");
    this.Text("Tandem-Gates-DogtownBehindHost", "Dogtown n'est pas encore ouvert dans l'histoire de {1} : sa porte reste fermée pour {1}. Passez le ou la prendre en voiture.");
    this.Text("Tandem-Gates-WatsonMet", "{1} a franchi le confinement de Watson que votre histoire maintient. Montez dans la voiture de {1} pour suivre.");
    this.Text("Tandem-Gates-DogtownMet", "{1} est à Dogtown, encore fermé dans votre histoire. Montez dans la voiture de {1} pour suivre.");
    this.Text("Tandem-Interact-HostOnly", "{1} doit le faire");
    this.Text("Tandem-Interact-Taken", "Monde de {1} : quelqu'un d'autre a été plus rapide");
    this.Text("Tandem-Interact-TooFar", "Trop loin dans le monde de {1}");
    this.Text("Tandem-Interact-Missing", "Pas là dans le monde de {1} pour l'instant");
    this.Text("Tandem-Interact-Busy", "{1} est occupé(e) : réessayez dans un instant");
    this.Text("Tandem-Loot-Taken", "Quelqu'un l'a déjà pris");
    this.Text("Tandem-Loot-TakenBy", "{1} l'a ramassé avant vous");
    // Other mods (Mods.reds, docs/architecture/mod-compat.md).
    this.Text("Tandem-Menu-Mods", "Mods des partenaires");
    this.Text("Tandem-Mods-TheyHave", "{1} a des mods que vous n'avez pas ({2})");
    this.Text("Tandem-Mods-YouHave", "Vous avez des mods que {1} n'a pas ({2})");
    this.Text("Tandem-Mods-Same", "{1} a les mêmes mods que vous");
    this.Text("Tandem-Mods-Differ", "Mods : {1}");
    this.Text("Tandem-Mods-Category-Items", "{1} avec de nouveaux objets (montrés comme des objets d'origine proches)");
    this.Text("Tandem-Mods-Category-Vehicles", "{1} avec de nouveaux véhicules (montrés comme des véhicules d'origine proches)");
    this.Text("Tandem-Mods-Category-Characters", "{1} avec de nouveaux personnages (montrés comme ceux d'origine)");
    this.Text("Tandem-Mods-Category-Looks", "{1} avec de nouvelles apparences (montrées avec des pièces d'origine)");
    this.Text("Tandem-Mods-Category-Gameplay", "{1} qui changent le jeu (les règles de l'hôte s'appliquent aux ennemis partagés)");
    this.Text("Tandem-Mods-Category-Unknown", "{1} que tandem ne sait pas classer");
    this.Text("Tandem-Mods-Category-Local", "{1} qui ne changent que l'image ou le son (sans souci)");
    this.Text("Tandem-Mods-Seen", "{1} voit {2} de vos éléments comme ceux d'origine : des mods qu'il n'a pas");
    this.Text("Tandem-Mods-NamesCame", "Les noms des mods de {1} sont dans Coop, Mods des partenaires");
    this.Text("Tandem-Mods-Names", "Les mods de {1} que vous n'avez pas : {2}");
    this.Text("Tandem-Mods-ReplacedCount", "Montrés autrement pendant cette session ({1}) :");
    this.Text("Tandem-Mods-Nothing", "rien");
    this.Text("Tandem-Mods-NoneCompared", "Aucun mod d'un autre joueur comparé pour l'instant");
    this.Text("Tandem-Mods-Replaced-Weapon", "L'arme de {1} vient d'un mod que vous n'avez pas : montrée comme une arme proche ({2})");
    this.Text("Tandem-Mods-Replaced-Clothing", "Les vêtements de {1} viennent d'un mod que vous n'avez pas : montrés simples ou retirés ({2} pièces)");
    this.Text("Tandem-Mods-Replaced-Look", "L'apparence de {1} a des pièces d'un mod que vous n'avez pas : retirées ({2})");
    this.Text("Tandem-Mods-Replaced-Vehicle", "Le véhicule de {1} vient d'un mod que vous n'avez pas : montré comme un véhicule proche ({2})");
    this.Text("Tandem-Mods-Replaced-Character", "Des personnages du jeu de {1} viennent d'un mod que vous n'avez pas : montrés comme ceux d'origine ({2})");
    this.Text("Tandem-Mods-Replaced-CharacterLook", "Des personnages du jeu de {1} portent des apparences d'un mod que vous n'avez pas : montrés avec leur apparence habituelle ({2})");
    this.Text("Tandem-Mods-Replaced-Item", "Un objet de {1} vient d'un mod que vous n'avez pas : il ne peut pas entrer dans votre jeu et reste le sien ({2})");
    this.Text("Tandem-Mods-Replaced-Effect", "Des effets du jeu de {1} viennent d'un mod que vous n'avez pas : pas affichés ({2})");
    this.Text("Tandem-Mods-Replaced-Radio", "La station de radio de {1} n'est pas installée ici : la radio reste éteinte");
    this.Text("Tandem-Mods-Line-Weapon", "arme de {1} {2}");
    this.Text("Tandem-Mods-Line-Clothing", "vêtement de {1} {2}");
    this.Text("Tandem-Mods-Line-Look", "pièce d'apparence de {1} {2}");
    this.Text("Tandem-Mods-Line-Vehicle", "véhicule de {1} {2}");
    this.Text("Tandem-Mods-Line-Character", "un personnage du jeu de {1} {2}");
    this.Text("Tandem-Mods-Line-CharacterLook", "une apparence de personnage du jeu de {1} {2}");
    this.Text("Tandem-Mods-Line-Item", "un objet de {1} {2}");
    this.Text("Tandem-Mods-Line-Effect", "un effet du jeu de {1} {2}");
    this.Text("Tandem-Mods-Line-Radio", "station de radio de {1} {2}");
    this.Text("Tandem-Trade-Unknown", "{1} propose un objet d'un mod que vous n'avez pas : il ne peut pas être échangé");
    this.Text("Tandem-World-WeatherIsHost", "Un autre mod change votre météo : en coop, la météo de l'hôte reste");
    this.Text("Tandem-Time-ModSlow", "Un mod a ralenti le temps : partagé comme une bulle de temps autour de vous");
    this.Text("Tandem-Marker-Description", "Un autre V, dans votre ville");
    this.Text("Tandem-Marker-Distance", "{1}  {2} M");
    this.Text("Tandem-Marker-Metres", "{1} M");

    this.Text("Tandem-Revive-Down", "Vous êtes à terre. {1} peut vous réanimer");
    this.Text("Tandem-Revive-DownAlone", "Vous êtes à terre");
    this.Text("Tandem-Revive-Out", "Vous êtes hors combat. Vous vous relèverez près de {1}");
    this.Text("Tandem-Revive-AllDown", "Tout le monde est à terre");
    this.Text("Tandem-Revive-WaitHost", "Tout le monde est à terre. En attente du dernier point de contrôle de {1}");
    this.Text("Tandem-Revive-RevivedBy", "{1} vous a réanimé(e)");
    this.Text("Tandem-Revive-BackBeside", "De retour près de {1}");
    this.Text("Tandem-Revive-PlayerDown", "{1} est à terre");
    this.Text("Tandem-Revive-PlayerOut", "{1} est hors combat");
    this.Text("Tandem-Revive-PlayerUp", "{1} s'est relevé(e)");
    this.Text("Tandem-Revive-Prompt", "Réanimer");
    this.Text("Tandem-Revive-Reviving", "Réanimation de {1}");
    this.Text("Tandem-Revive-KeepHolding", "Maintenez pour réanimer");
    this.Text("Tandem-Revive-RevivingYou", "{1} vous réanime");
    this.Text("Tandem-Revive-StayDown", "Restez à terre");
    this.Text("Tandem-Revive-Done", "Réanimation réussie");
    this.Text("Tandem-Revive-CarMoving", "Attendez l'arrêt du véhicule");
    this.Text("Tandem-Revive-OutOfCar", "Sorti(e) de l'épave");
    this.Text("Tandem-Revive-AtViktor", "Vik vous a rafistolé(e)");
    this.Text("Tandem-Revive-AtPoint", "Rafistolé(e), de retour sur pied à {1}");
    this.Text("Tandem-Revive-BackInCar", "De retour dans le véhicule de {1}");
    this.Text("Tandem-Transit-GotOffAt", "{1} est descendu(e) du NCART à {2}");
    this.Text("Tandem-Transit-GotOff", "{1} est descendu(e) du NCART");
    this.Text("Tandem-Transit-RidingToward", "{1} prend le NCART vers {2}");
    this.Text("Tandem-Transit-Riding", "{1} prend le NCART");
    this.Text("Tandem-Transit-Skipping", "{1} passe le trajet en NCART");
    this.Text("Tandem-Transit-Leaving", "Le véhicule de {1} part : montez, ou suivez");
    this.Text("Tandem-Transit-RideWith", "Monter avec {1}");
    this.Text("Tandem-Transit-Follow", "Suivre");
    this.Text("Tandem-Transit-Following", "Vous suivez {1} : rejoignez-le à la fin du trajet");
    this.Text("Tandem-Transit-YouRide", "Vous voyagez avec {1}");
    this.Text("Tandem-Transit-RidingAlong", "En route avec {1}");
    this.Text("Tandem-Transit-RideOver", "le trajet est terminé");
    this.Text("Tandem-Travel-OnTheWay", "{1} arrive");
    this.Text("Tandem-Travel-Stays", "{1} reste sur place");
    this.Text("Tandem-Travel-FastTravelledTo", "{1} s'est déplacé(e) rapidement vers {2}");
    this.Text("Tandem-Travel-FastTravelled", "{1} a utilisé le voyage rapide");
    this.Text("Tandem-Travel-Labelled", "{1} : {2}");
    this.Text("Tandem-Travel-Scene", "{1} lance une scène");
    this.Text("Tandem-Travel-Taken", "{1} a été emmené(e) ailleurs");
    this.Text("Tandem-Travel-Asks", "{1} vous demande de le rejoindre");
    this.Text("Tandem-Travel-Join", "Rejoindre {1}");
    this.Text("Tandem-Activities-WaitingAll", "En attente de l'accord des autres...");
    this.Text("Tandem-Activities-WaitingSleep", "En attente de l'accord des autres pour dormir jusqu'à {1}...");
    this.Text("Tandem-Activities-WaitingWait", "En attente de l'accord des autres pour attendre jusqu'à {1}...");
    this.Text("Tandem-Activities-Agrees", "{1} est d'accord");
    this.Text("Tandem-Activities-MovedOn", "{1} est passé à {2}");
    this.Text("Tandem-Activities-Slept", "{1} a dormi jusqu'à {2}");
    this.Text("Tandem-Activities-Waited", "{1} a attendu jusqu'à {2}");
    this.Text("Tandem-Activities-CannotSkip", "{1} veut passer le temps : impossible pour vous en ce moment");
    this.Text("Tandem-Activities-AsksSleep", "{1} veut dormir jusqu'à {2}");
    this.Text("Tandem-Activities-AsksWait", "{1} veut attendre jusqu'à {2}");
    this.Text("Tandem-Activities-AgreeSleep", "Accepter de dormir jusqu'à {1}");
    this.Text("Tandem-Activities-AgreeWait", "Accepter d'attendre jusqu'à {1}");
    this.Text("Tandem-Activities-WaitingFor", "En attente de {1}...");
    this.Text("Tandem-Activities-Photo", "{1} est en mode photo");
    this.Text("Tandem-Activities-Back", "{1} est de retour");
    this.Text("Tandem-Contest-Duel", "{1} est en plein combat à mains nues : vous ne pouvez que regarder");
    this.Text("Tandem-Contest-Race", "{1} fait une course");
    this.Text("Tandem-Contest-Range", "{1} participe à un concours de tir");
    this.Text("Tandem-Contest-Other", "{1} participe à une épreuve");
    this.Text("Tandem-Contest-Over", "L'épreuve de {1} est terminée");
    this.Text("Tandem-Contest-WatchOnly", "C'est le combat de {1} : vos coups ne comptent pas");
    this.Text("Tandem-Race-Offer", "{1} se place pour une course de rue");
    this.Text("Tandem-Race-Join", "Rejoindre la course de {1}");
    this.Text("Tandem-Race-Joining", "Vous rejoignez la course de {1}…");
    this.Text("Tandem-Race-Joined", "{1} rejoint votre course");
    this.Text("Tandem-Race-Grid", "Sur la grille : attendez le départ");
    this.Text("Tandem-Race-Ready", "Préparez-vous…");
    this.Text("Tandem-Race-Go", "Partez !");
    this.Text("Tandem-Race-TooLate", "La course de {1} a commencé : vous regardez");
    this.Text("Tandem-Race-NoCar", "Pas de voiture pour la course de {1} : vous regardez");
    this.Text("Tandem-Race-Finished", "Vous terminez n° {1}");
    this.Text("Tandem-Race-Over", "La course de {1} est terminée");
    this.Text("Tandem-Race-Abandoned", "{1} a quitté la course : elle est terminée");
    this.Text("Tandem-Race-Left", "Vous quittez la course de {1}");
    this.Text("Tandem-Race-RacerLeft", "{1} a quitté la course");
    this.Text("Tandem-Race-Standings", "Course");
    this.Text("Tandem-Race-Npc", "Pilote {1}");
    this.Text("Tandem-Race-Checkpoint", "Point de passage {1}");
    this.Text("Tandem-Race-Out", "abandon");
    this.Text("Tandem-Activities-CantNow", "{1} ne peut pas passer le temps pour l'instant");
    this.Text("Tandem-Activities-Declined", "{1} a refusé");
    this.Text("Tandem-Activities-NoAnswer", "{1} n'a pas répondu");
    this.Text("Tandem-Activities-Cancelled", "Passage du temps annulé");
    this.Text("Tandem-Activities-AlreadyAsking", "{1} demande déjà à passer le temps");
    this.Text("Tandem-Activities-IsDown", "{1} est à terre");
    this.Text("Tandem-Activities-Bending", "{1} ralentit le temps");
    this.Text("Tandem-Activities-Elsewhere", "{1} est ailleurs");
    this.Text("Tandem-Activities-Paused", "{1} a mis le jeu en pause");
    this.Text("Tandem-Activities-CantSkip", "Impossible de passer le temps maintenant");
    this.Text("Tandem-Activities-SleepWith", "Vous allez dormir aussi : en attente de la réponse de {1}");
    this.Text("Tandem-Breach-Already", "{1} a piraté ce réseau en premier");
    this.Text("Tandem-Breach-AlreadyWorld", "Ce réseau était déjà piraté");
    this.Text("Tandem-Trade-Prompt", "Échanger");
    this.Text("Tandem-Trade-Asked", "Échange proposé à {1}");
    this.Text("Tandem-Trade-Asks", "{1} veut échanger");
    this.Text("Tandem-Trade-HintWith", "Échanger avec {1}");
    this.Text("Tandem-Trade-PriceMissing", "Échange : le prix demandé n'est pas sur la table");
    this.Text("Tandem-Trade-Done", "Échange avec {1} effectué");
    this.Text("Tandem-Trade-Theirs", "C'est à {1} tant que vous n'êtes pas d'accord tous les deux");
    this.Text("Tandem-Trade-NotForTrade", "Pas échangeable : {1}");
    this.Text("Tandem-Trade-OnTable", "C'est déjà sur la table");
    this.Text("Tandem-Trade-TableFull", "La table est pleine");
    this.Text("Tandem-Trade-YouHave", "Vous avez {1} eddies");
    this.Text("Tandem-Trade-InputGive", "Eddies que vous donnez (actuellement {1})");
    this.Text("Tandem-Trade-InputAsk", "Eddies que vous demandez en retour (actuellement {1})");
    this.Text("Tandem-Trade-Declined", "{1} a refusé l'échange");
    this.Text("Tandem-Trade-NoAnswer", "{1} n'a pas répondu");
    this.Text("Tandem-Trade-TooFar", "Échange terminé : {1} est trop loin");
    this.Text("Tandem-Trade-Combat", "Échange terminé : combat");
    this.Text("Tandem-Trade-Down", "Échange terminé : un joueur est à terre");
    this.Text("Tandem-Trade-Left", "Échange terminé : {1} est parti");
    this.Text("Tandem-Trade-Busy", "{1} échange déjà avec quelqu'un d'autre");
    this.Text("Tandem-Trade-Refuses", "{1} n'échange pas");
    this.Text("Tandem-Trade-Cancelled", "Échange annulé");
    this.Text("Tandem-Trade-Not-Inventory", "pas dans votre inventaire");
    this.Text("Tandem-Trade-Not-Item", "pas un objet");
    this.Text("Tandem-Trade-Not-Eddies", "les eddies ont leur propre ligne");
    this.Text("Tandem-Trade-Not-Quest", "un objet de quête");
    this.Text("Tandem-Trade-Not-Iconic", "un objet iconique ou important");
    this.Text("Tandem-Trade-Not-Hidden", "pas un objet à échanger");
    this.Text("Tandem-Trade-Not-Unsellable", "il ne peut pas être vendu");
    this.Text("Tandem-Vendor-Trade", "Commercer");
    this.Text("Tandem-Vendor-Dead", "{1} est mort");
    this.Text("Tandem-Vendor-NotNow", "Impossible de commercer avec {1} pour l'instant");
    this.Text("Tandem-Vendor-NotHere", "{1} ne commerce pas encore avec vous");
    this.Text("Tandem-Vendor-Locked", "La boutique de {1} ne vous est pas encore ouverte");
    this.Text("Tandem-Trade-Not-Equipped", "déséquipez-le d'abord");
    this.Text("Tandem-Trade-Not-Single", "il ne peut pas être donné");
    this.Text("Tandem-Trade-Not-UsedUp", "il serait consommé");
    this.Text("Tandem-Trade-Not-Charges", "il se compte en charges");
    this.Text("Tandem-Garage-Credited", "{1} a obtenu un véhicule : noté comme gagné ensemble");
    this.Text("Tandem-Garage-Kept", "Nouveau véhicule dans votre garage, gagné avec {1}");
    this.Text("Tandem-Garage-LendLeft", "Voiture de {1} : il a quitté la session");
    this.Text("Tandem-Garage-LendAboard", "Voiture de {1} : seulement avec lui à bord");
    this.Text("Tandem-Garage-LendKept", "Voiture de {1} : il la garde pour lui");
    this.Text("Tandem-Levels-Scaled", "Coop : les ennemis sont ajustés au niveau {1}");
    this.Text("Tandem-Scaling-On", "Coop : les ennemis proches de plusieurs d'entre vous sont plus coriaces");
    this.Text("Tandem-Saves-Failed", "Coop : la partie n'a pas pu être sauvegardée. Sauvegardez vous-même pour garder ce que votre V a gagné.");
    this.Text("Tandem-Saves-WhenYouCan", "Coop : sauvegardez dès que possible, pour garder ce que votre V a gagné.");
    this.Text("Tandem-Saves-NotRestored", "Coop : votre partie n'a pas pu être remise comme avant. Chargez votre dernière sauvegarde.");
    this.Text("Tandem-Exit-Saving", "Coop : sauvegarde de votre V avant de quitter...");
    this.Text("Tandem-Exit-NotSaved", "Coop : votre V n'a pas pu être sauvegardé, le jeu n'a donc pas quitté. Vous êtes hors de la session : sauvegardez dès que possible, ou quittez à nouveau pour partir sans sauvegarder.");
    this.Text("Tandem-Exit-Title", "Quitter sans sauvegarder votre V ?");
    this.Text("Tandem-Exit-CannotSave", "Votre V ne peut pas être sauvegardé pour l'instant ({1}). Quitter maintenant perd ce qu'il a gagné dans cette session coop : XP, butin, récompenses. Quitter quand même, ou rester et quitter dès qu'il pourra être sauvegardé ?");
    this.Text("Tandem-Exit-Why-Combat", "vous êtes en combat");
    this.Text("Tandem-Exit-Why-Scene", "une scène ou une quête bloque la sauvegarde");
    this.Text("Tandem-Exit-Why-Down", "votre V est à terre");
    this.Text("Tandem-Exit-Why-Dirty", "votre partie n'a pas pu être remise comme avant après la session");
    this.Text("Tandem-Exit-Why-Other", "le jeu n'autorise pas de sauvegarde ici");
    this.Text("Tandem-Checkpoint-Saving", "Point de sauvegarde coop : sauvegarde de votre V, retour dans la session dans un instant");
    this.Text("Tandem-Checkpoint-Saved", "Point de sauvegarde coop : votre V est sauvegardé");
    this.Text("Tandem-Checkpoint-Failed", "Point de sauvegarde coop : votre V n'a pas pu être sauvegardé cette fois");
    this.Text("Tandem-Comms-Enemy", "Ennemi");
    this.Text("Tandem-Comms-Device", "Appareil");
    this.Text("Tandem-Comms-Loot", "Butin");
    this.Text("Tandem-Comms-Vehicle", "Véhicule");
    this.Text("Tandem-Comms-Danger", "Danger");
    this.Text("Tandem-Comms-Target", "Cible");
    this.Text("Tandem-Comms-Here", "Ici");
    this.Text("Tandem-Comms-PingOf", "{1} : {2}");
    this.Text("Tandem-Comms-YourPing", "Votre signal");
    this.Text("Tandem-Comms-TheirPing", "Signal de {1}");
    this.Text("Tandem-Comms-YourTarget", "Votre cible d'équipe");
    this.Text("Tandem-Comms-TheirTarget", "Cible de {1} : concentrez-vous dessus");
    this.Text("Tandem-Comms-MarkTitle", "Marquer la cible");
    this.Text("Tandem-Comms-ClearTitle", "Retirer la marque d'équipe");
    this.Text("Tandem-Comms-MarkDescription", "Montre cette cible à vos partenaires liés : marquée sur leur ATH et soulignée, jusqu'à ce qu'elle tombe, que vous en marquiez une autre ou que vous la choisissiez de nouveau. Sans RAM, sans traçage.");
    this.Text("Tandem-Comms-ClearDescription", "Retire la marque de cette cible de l'ATH de vos partenaires.");
    this.Text("Tandem-Comms-OnlyShared", "Seules les cibles partagées peuvent être marquées");
    this.Text("Tandem-Comms-LinksOff", "Les liens sont désactivés (tandem.ini)");
    this.Text("Tandem-Comms-Linked", "Lié à {1} : analyses et signaux partagés");
    this.Text("Tandem-Comms-LinkBroken", "Lien avec {1} rompu");
    this.Text("Tandem-Comms-LinkSent", "Demande de lien envoyée à {1}");
    this.Text("Tandem-Comms-LinkAsked", "{1} vous appelle : se lier pour partager analyses et signaux");
    this.Text("Tandem-Comms-LinkWith", "Se lier à {1}");
    this.Text("Tandem-Comms-LinkBusy", "{1} a pris une autre demande de lien : rappelez plus tard");
    this.Text("Tandem-Comms-WaypointOf", "Point de passage de {1}");
    this.Text("Tandem-Comms-PinPlaced", "Le repère placé par {1}");
    this.Text("Tandem-Comms-PlaceTracked", "Le lieu que suit {1}");
    this.Text("Tandem-Comms-WaypointSet", "{1} a placé un point de passage");
    this.Text("Tandem-Comms-ObjectiveOf", "Objectif de {1}");
    this.Text("Tandem-Comms-ObjectiveTracked", "L'objectif que suit {1}");
    this.Text("Tandem-Comms-Decline", "Refuser");
    this.Text("Tandem-Phone-NotLinked", "Pas de lien");
    this.Text("Tandem-Phone-Calling", "Appel en cours...");
    this.Text("Tandem-Phone-CallingYou", "Vous appelle");
    this.Text("Tandem-Phone-Linked", "Lien actif");
    this.Text("Tandem-Phone-Call", "Appeler");
    this.Text("Tandem-Phone-CallAbout", "Demander un lien à {1} : partager analyses, signaux et santé");
    this.Text("Tandem-Phone-Answer", "Répondre");
    this.Text("Tandem-Phone-AnswerAbout", "{1} vous appelle : se lier");
    this.Text("Tandem-Phone-Cancel", "Annuler l'appel");
    this.Text("Tandem-Phone-CancelAbout", "Appel à {1} en cours...");
    this.Text("Tandem-Phone-HangUp", "Raccrocher");
    this.Text("Tandem-Phone-HangUpAbout", "Rompre le lien avec {1}");
    this.Text("Tandem-Phone-TradeAbout", "Donner, échanger ou vendre objets et eddies, où que vous soyez");
    this.Text("Tandem-Phone-JoinAbout", "Voyage rapide jusqu'à {1}");
    this.Text("Tandem-Phone-Invite", "Proposer à {1} de vous rejoindre");
    this.Text("Tandem-Phone-InviteAbout", "{1} se voit proposer un voyage rapide jusqu'à vous");
    this.Text("Tandem-Phone-Cancelled", "Appel à {1} annulé");
    this.Text("Tandem-Phone-Invited", "Proposé à {1} de vous rejoindre");
    this.Text("Tandem-Phone-Unreachable", "{1} est injoignable pour l'instant");
    this.Text("Tandem-Trade-NotNow", "Échange impossible pour l'instant");
    this.Text("Tandem-Story-RestrictHolstered", "Scène de {1} : armes rangées.");
    this.Text("Tandem-Story-RestrictSlow", "Scène de {1} : doucement.");
    this.Text("Tandem-Time-SlowedScanner", "Le scanner de {1} vous ralentit.");
    this.Text("Tandem-Time-SlowedSandevistan", "Le Sandevistan de {1} vous ralentit.");
    this.Text("Tandem-Time-SlowedOther", "Le ralenti de {1} vous atteint.");
    this.Text("Tandem-Status-Unknown", "{1} porte un effet que ce jeu ne connaît pas (un mod ?) : non affiché.");
    this.Text("Tandem-Travel-Blocked", "Voyage rapide impossible pour l'instant");
    this.Text("Tandem-Menu-Settings", "Paramètres coop");
    this.Text("Tandem-Settings-Saved", "Enregistrés dans tandem.ini dès leur modification");
    this.Text("Tandem-Settings-Item", "{1} : {2}");
    this.Text("Tandem-Settings-NextSession", "{1} (à la prochaine session)");
    this.Text("Tandem-Settings-HostsRule", "{1} (règle de l'hôte)");
    this.Text("Tandem-Settings-AfterRestart", "{1} (après un redémarrage)");
    this.Text("Tandem-Settings-Type", "Saisissez la nouvelle valeur (vide : aucune)");
    this.Text("Tandem-Settings-Invalid", "Valeur refusée pour ce paramètre : {1}");
    this.Text("Tandem-Settings-NotSet", "non défini");
    this.Text("Tandem-Settings-TokenSet", "défini");
    this.Text("Tandem-Settings-Name", "Nom");
    this.Text("Tandem-Settings-FriendlyFire", "Tirs alliés");
    this.Text("Tandem-Settings-EnemyScaling", "Ennemis plus coriaces à chaque joueur en plus");
    this.Text("Tandem-Settings-LevelGap", "Ennemis au-dessus du niveau le plus bas");
    this.Text("Tandem-Settings-LevelGap-Eight", "8 niveaux au plus");
    this.Text("Tandem-Settings-LevelGap-Five", "5 niveaux au plus");
    this.Text("Tandem-Settings-LevelGap-Twelve", "12 niveaux au plus");
    this.Text("Tandem-Settings-Notices", "Notifications");
    this.Text("Tandem-Settings-Notices-All", "toutes");
    this.Text("Tandem-Settings-Notices-Important", "importantes seulement");
    this.Text("Tandem-Settings-Notices-Minimal", "questions et problèmes");
    this.Text("Tandem-Settings-Markers", "Marqueurs des joueurs");
    this.Text("Tandem-Settings-Nameplates", "Noms au-dessus des joueurs");
    this.Text("Tandem-Settings-Nameplates-Near", "de près");
    this.Text("Tandem-Settings-Nameplates-Always", "à toute distance");
    this.Text("Tandem-Settings-PartyHud", "Liste du groupe (santé des partenaires liés)");
    this.Text("Tandem-Settings-Recorder", "Enregistreur");
    this.Text("Tandem-Settings-Relay", "Relais");
    this.Text("Tandem-Settings-Code", "Code d'invitation");
    this.Text("Tandem-Settings-Code-New", "nouveau à chaque session");
    this.Text("Tandem-Settings-Code-Keep", "gardé (le même à chaque session)");
    this.Text("Tandem-Settings-Direct", "Connexion directe via le relais");
    this.Text("Tandem-Settings-Direct-Auto", "si possible");
    this.Text("Tandem-Settings-Link", "Partager analyses et signaux avec");
    this.Text("Tandem-Settings-Link-Auto", "chaque partenaire");
    this.Text("Tandem-Settings-Link-Ask", "les partenaires avec qui vous vous liez");
    this.Text("Tandem-Settings-Pings", "Signaux");
    this.Text("Tandem-Settings-Story", "Histoire partagée");
    this.Text("Tandem-Settings-QuestFreeze", "Mettre votre histoire en pause (invité)");
    this.Text("Tandem-Settings-ScenePrivacy", "Scènes privées");
    this.Text("Tandem-Settings-TimeHost", "Votre Sandevistan (hôte)");
    this.Text("Tandem-Settings-TimeHost-Full", "ralentit tout votre jeu");
    this.Text("Tandem-Settings-TimeHost-Bubble", "ralentit les PNJ autour de vous");
    this.Text("Tandem-Settings-RaceCar", "Votre voiture dans la course d'un partenaire");
    this.Text("Tandem-Settings-RaceCar-Own", "la vôtre");
    this.Text("Tandem-Settings-RaceCar-Copy", "une copie de la sienne");
    this.Text("Tandem-Settings-About-Name", "Le nom que les autres voient au-dessus de votre V et dans leur liste de groupe");
    this.Text("Tandem-Settings-About-FriendlyFire", "Règle de l'hôte : les tirs, lames et explosions des V se blessent entre eux");
    this.Text("Tandem-Settings-About-EnemyScaling", "Règle de l'hôte : les PNJ hostiles ont plus de santé pour chaque joueur en plus près d'eux");
    this.Text("Tandem-Settings-About-LevelGap", "Règle de l'hôte : les ennemis suivent le niveau moyen des joueurs, mais jamais plus que ceci au-dessus du joueur le plus bas, pour qu'un nouveau V puisse se battre à côté d'un vétéran");
    this.Text("Tandem-Settings-About-Notices", "Les notifications coop affichées : toutes, sans les nouvelles, ou seulement questions et problèmes");
    this.Text("Tandem-Settings-About-Markers", "Un marqueur pour chaque autre joueur sur l'ATH, la mini-carte et la carte");
    this.Text("Tandem-Settings-About-Nameplates", "Le nom au-dessus des autres joueurs : de près, à toute distance, ou aucun");
    this.Text("Tandem-Settings-About-PartyHud", "La liste sous votre barre de santé : nom, santé et état de chaque partenaire lié");
    this.Text("Tandem-Settings-About-Link", "Qui voit vos marquages d'analyse et révélations Ping : chaque partenaire, ceux avec qui vous vous liez, ou personne");
    this.Text("Tandem-Settings-About-Pings", "Désactivé : vous n'envoyez ni ne voyez de signaux (la touche de marquage hors analyse)");
    this.Text("Tandem-Settings-About-Recorder", "Enregistre votre partie pour les tests de tandem (recordings/auto) ; ne change rien en jeu");
    this.Text("Tandem-Settings-About-Relay", "Le relais qui donne son code d'invitation à l'hôte et relie les joueurs par internet");
    this.Text("Tandem-Settings-About-Code", "Hôte avec un jeton de relais : un nouveau code à chaque session, ou le même gardé");
    this.Text("Tandem-Settings-About-Direct", "Via le relais, essayer aussi un lien direct (latence plus faible) ; désactivé : le relais seulement");
    this.Text("Tandem-Settings-About-Story", "Le journal, les appels, messages, scènes et choix de l'hôte parviennent aux invités");
    this.Text("Tandem-Settings-About-QuestFreeze", "Invité : votre propre histoire attend pendant que vous jouez dans celle de l'hôte");
    this.Text("Tandem-Settings-About-ScenePrivacy", "Les romances et scènes en tête-à-tête sont à vous seul : les autres vous voient occupé");
    this.Text("Tandem-Settings-About-TimeHost", "Hôte : votre Sandevistan ralentit tout votre jeu, ou seulement les PNJ autour de vous");
    this.Text("Tandem-Settings-About-RaceCar", "En rejoignant la course de rue d'un partenaire : vous prenez le départ dans votre propre voiture (celle que vous appelez) ou dans une copie de celle du lanceur ; sans voiture à vous, une copie");
  }
}
