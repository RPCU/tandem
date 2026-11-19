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
      "the session is full (4 players at most)", "Resyncing with the host..."
    ];
    let keys = [
      "Reason-Full", "Reason-HostGone", "Reason-Unreachable", "Reason-Connecting",
      "Reason-RelaySilent", "Reason-RelayToken",
      "Reason-RelayNoSession", "Reason-RelayBusy",
      "Reason-RelayTaken", "Reason-RelayNoCode",
      "Reason-RelayThrottled", "Reason-RelaySocket",
      "Reason-FullMax", "Session-Resync"
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
      "", "Session-Ended", "Story-NotRestored", "Story-SceneStarted", "Transit-Leaving",
      "Travel-FastTravelledTo", "Travel-FastTravelled", "Travel-Labelled", "Travel-Scene",
      "Travel-Taken", "Travel-Asks", "Travel-Blocked", "Revive-Down", "Revive-DownAlone",
      "Revive-Out", "Revive-AllDown", "Revive-WaitHost"
    ];
    if ArrayContains(asks, key) {
      return 2;
    }
    return StrBeginsWith(key, "Session-") || StrBeginsWith(key, "Revive-") ? 1 : 0;
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
    this.Text("Tandem-Menu-Host", "Host a game");
    this.Text("Tandem-Menu-Join", "Join a game");
    this.Text("Tandem-Menu-Solo", "Play solo");
    this.Text("Tandem-Menu-HostNow", "Host this game");
    this.Text("Tandem-Menu-Invite", "Invite");
    this.Text("Tandem-Menu-Leave", "Leave session");
    this.Text("Tandem-Menu-EnterCode", "Enter a code or an address");
    this.Text("Tandem-Menu-Back", "Back");
    this.Text("Tandem-Menu-Recent", "{1} ({2})");
    // Its status panel.
    this.Text("Tandem-Status-HostAtLoad", "You host when your game loads (port {1})");
    this.Text("Tandem-Status-HostFailed", "Could not host: {1}");
    this.Text("Tandem-Status-Hosting", "Hosting on port {1}");
    this.Text("Tandem-Status-Code", "Join code: {1}");
    this.Text("Tandem-Status-Relay", "Relay: {1}");
    this.Text("Tandem-Status-JoinAtLoad", "You join {1} when your game loads");
    this.Text("Tandem-Status-InSessionOf", "In {1}'s session");
    this.Text("Tandem-Status-InSessionAt", "In the session at {1}");
    this.Text("Tandem-Status-Retrying", "{1} not reached ({2}), trying again");
    this.Text("Tandem-Status-Connecting", "Connecting to {1}...");
    this.Text("Tandem-Status-None", "Not in a session");
    this.Text("Tandem-Status-Host", "{1} (host)");
    this.Text("Tandem-Status-You", "{1} (you)");
    this.Text("Tandem-Status-Ping", "{1} - {2} ms");
    this.Text("Tandem-Status-Recorder", "Recorder: {1}");
    this.Text("Tandem-Common-On", "on");
    this.Text("Tandem-Common-Off", "off");
    this.Text("Tandem-Status-CodeTarget", "code {1}");
    this.Text("Tandem-Status-ChooseHostSave", "Choose the save to host");
    this.Text("Tandem-Status-ChooseJoinSave", "Choose the save your V comes from, to join {1}");
    this.Text("Tandem-Status-PickHost", "Pick a host, or enter their join code or address");
    // Invites and popups.
    this.Text("Tandem-Invite-Code", "Join code {1}. ");
    this.Text("Tandem-Invite-NoAddress", "Friends join you on port {1} (no network address found)");
    this.Text("Tandem-Invite-Addresses", "Friends on your network join you at {1}");
    this.Text("Tandem-Popup-NeedsRelay", "Joining by code needs a relay: set one in the co-op settings");
    this.Text("Tandem-Popup-EnterCode", "The host's join code (K7Q-2M9) or address (192.168.1.20, or 192.168.1.20:27450), as their Co-op menu shows it");
    this.Text("Tandem-Popup-NotATarget", "Not a join code or an address: {1}");
    // The session.
    this.Text("Tandem-Session-Hosting", "Hosting co-op. {1}");
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
    this.Text("Tandem-Player-Unnamed", "Player {1}");
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
    this.Text("Tandem-Dialogue-Suggests", "{1} suggests: {2}");
    this.Text("Tandem-Dialogue-Suggested", "Suggested: {1}");
    // Story.
    this.Text("Tandem-Story-NotRestored", "Co-op: your story could not be restored exactly. Load your last save");
    this.Text("Tandem-Story-SceneStarted", "{1} started a scene");
    this.Text("Tandem-Story-Busy", "{1} is busy");
    this.Text("Tandem-Story-Join", "Join {1}");
    // Loot, markers.
    this.Text("Tandem-Loot-Taken", "Someone already took it");
    this.Text("Tandem-Loot-TakenBy", "{1} picked it up first");
    this.Text("Tandem-Marker-Description", "Another V, in your city");
    this.Text("Tandem-Marker-Distance", "{1}  {2} M");
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
    this.Text("Tandem-Travel-Blocked", "You can't fast travel right now");
    // The settings page (Settings.reds).
    this.Text("Tandem-Menu-Settings", "Settings");
    this.Text("Tandem-Settings-Saved", "Saved to tandem.ini as you change them");
    this.Text("Tandem-Settings-Item", "{1}: {2}");
    this.Text("Tandem-Settings-NextSession", "{1} (from the next session)");
    this.Text("Tandem-Settings-AfterRestart", "{1} (after a restart)");
    this.Text("Tandem-Settings-Type", "Type the new value (empty: none)");
    this.Text("Tandem-Settings-Invalid", "Not a value for this setting: {1}");
    this.Text("Tandem-Settings-NotSet", "not set");
    this.Text("Tandem-Settings-TokenSet", "set");
    this.Text("Tandem-Settings-Name", "Name");
    this.Text("Tandem-Settings-FriendlyFire", "Friendly fire");
    this.Text("Tandem-Settings-Notices", "Notices");
    this.Text("Tandem-Settings-Notices-All", "all");
    this.Text("Tandem-Settings-Notices-Important", "important only");
    this.Text("Tandem-Settings-Notices-Minimal", "questions and problems");
    this.Text("Tandem-Settings-Markers", "Player markers");
    this.Text("Tandem-Settings-Nameplates", "Names over players");
    this.Text("Tandem-Settings-Recorder", "Recorder");
    this.Text("Tandem-Settings-Relay", "Relay");
    this.Text("Tandem-Settings-RelayToken", "Relay host token");
    this.Text("Tandem-Settings-Story", "Story sync");
    this.Text("Tandem-Settings-QuestFreeze", "Pause your own story (guest)");
    this.Text("Tandem-Settings-ScenePrivacy", "Private scenes");
    this.Text("Tandem-Settings-TimeHost", "Your Sandevistan (host)");
    this.Text("Tandem-Settings-TimeHost-Full", "slows your whole game");
    this.Text("Tandem-Settings-TimeHost-Bubble", "slows the NPCs around you");
  }
}

public class TandemTextFrench extends ModLocalizationPackage {
  protected func DefineTexts() -> Void {
    this.Text("Tandem-Menu-Coop", "Coop");
    this.Text("Tandem-Menu-Host", "Héberger une partie");
    this.Text("Tandem-Menu-Join", "Rejoindre une partie");
    this.Text("Tandem-Menu-Solo", "Jouer en solo");
    this.Text("Tandem-Menu-HostNow", "Héberger cette partie");
    this.Text("Tandem-Menu-Invite", "Inviter");
    this.Text("Tandem-Menu-Leave", "Quitter la session");
    this.Text("Tandem-Menu-EnterCode", "Saisir un code ou une adresse");
    this.Text("Tandem-Menu-Back", "Retour");
    this.Text("Tandem-Menu-Recent", "{1} ({2})");
    this.Text("Tandem-Status-HostAtLoad", "Vous hébergez dès le chargement de votre partie (port {1})");
    this.Text("Tandem-Status-HostFailed", "Hébergement impossible : {1}");
    this.Text("Tandem-Status-Hosting", "Hébergement sur le port {1}");
    this.Text("Tandem-Status-Code", "Code d'invitation : {1}");
    this.Text("Tandem-Status-Relay", "Relais : {1}");
    this.Text("Tandem-Status-JoinAtLoad", "Vous rejoignez {1} dès le chargement de votre partie");
    this.Text("Tandem-Status-InSessionOf", "Dans la session de {1}");
    this.Text("Tandem-Status-InSessionAt", "Dans la session {1}");
    this.Text("Tandem-Status-Retrying", "{1} injoignable ({2}), nouvel essai");
    this.Text("Tandem-Status-Connecting", "Connexion à {1}...");
    this.Text("Tandem-Status-None", "Aucune session");
    this.Text("Tandem-Status-Host", "{1} (hôte)");
    this.Text("Tandem-Status-You", "{1} (vous)");
    this.Text("Tandem-Status-Ping", "{1} - {2} ms");
    this.Text("Tandem-Status-Recorder", "Enregistreur : {1}");
    this.Text("Tandem-Common-On", "activé");
    this.Text("Tandem-Common-Off", "désactivé");
    this.Text("Tandem-Status-CodeTarget", "code {1}");
    this.Text("Tandem-Status-ChooseHostSave", "Choisissez la sauvegarde à héberger");
    this.Text("Tandem-Status-ChooseJoinSave", "Choisissez la sauvegarde de votre V pour rejoindre {1}");
    this.Text("Tandem-Status-PickHost", "Choisissez un hôte, ou saisissez son code d'invitation ou son adresse");
    this.Text("Tandem-Invite-Code", "Code d'invitation {1}. ");
    this.Text("Tandem-Invite-NoAddress", "Vos amis vous rejoignent sur le port {1} (aucune adresse réseau trouvée)");
    this.Text("Tandem-Invite-Addresses", "Vos amis sur votre réseau vous rejoignent à {1}");
    this.Text("Tandem-Popup-NeedsRelay", "Rejoindre par code demande un relais : indiquez-le dans les paramètres coop");
    this.Text("Tandem-Popup-EnterCode", "Le code d'invitation de l'hôte (K7Q-2M9) ou son adresse (192.168.1.20, ou 192.168.1.20:27450), comme l'affiche son menu Coop");
    this.Text("Tandem-Popup-NotATarget", "Ni un code d'invitation ni une adresse : {1}");
    this.Text("Tandem-Session-Hosting", "Partie coop hébergée. {1}");
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
    this.Text("Tandem-Player-Unnamed", "Joueur {1}");
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
    this.Text("Tandem-Dialogue-Suggests", "{1} suggère : {2}");
    this.Text("Tandem-Dialogue-Suggested", "Suggéré : {1}");
    this.Text("Tandem-Story-NotRestored", "Coop : votre histoire n'a pas pu être restaurée à l'identique. Chargez votre dernière sauvegarde");
    this.Text("Tandem-Story-SceneStarted", "{1} a lancé une scène");
    this.Text("Tandem-Story-Busy", "{1} est occupé(e)");
    this.Text("Tandem-Story-Join", "Rejoindre {1}");
    this.Text("Tandem-Loot-Taken", "Quelqu'un l'a déjà pris");
    this.Text("Tandem-Loot-TakenBy", "{1} l'a ramassé avant vous");
    this.Text("Tandem-Marker-Description", "Un autre V, dans votre ville");
    this.Text("Tandem-Marker-Distance", "{1}  {2} M");

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
    this.Text("Tandem-Travel-Blocked", "Voyage rapide impossible pour l'instant");
    this.Text("Tandem-Menu-Settings", "Paramètres");
    this.Text("Tandem-Settings-Saved", "Enregistrés dans tandem.ini dès leur modification");
    this.Text("Tandem-Settings-Item", "{1} : {2}");
    this.Text("Tandem-Settings-NextSession", "{1} (à la prochaine session)");
    this.Text("Tandem-Settings-AfterRestart", "{1} (après un redémarrage)");
    this.Text("Tandem-Settings-Type", "Saisissez la nouvelle valeur (vide : aucune)");
    this.Text("Tandem-Settings-Invalid", "Valeur refusée pour ce paramètre : {1}");
    this.Text("Tandem-Settings-NotSet", "non défini");
    this.Text("Tandem-Settings-TokenSet", "défini");
    this.Text("Tandem-Settings-Name", "Nom");
    this.Text("Tandem-Settings-FriendlyFire", "Tirs alliés");
    this.Text("Tandem-Settings-Notices", "Notifications");
    this.Text("Tandem-Settings-Notices-All", "toutes");
    this.Text("Tandem-Settings-Notices-Important", "importantes seulement");
    this.Text("Tandem-Settings-Notices-Minimal", "questions et problèmes");
    this.Text("Tandem-Settings-Markers", "Marqueurs des joueurs");
    this.Text("Tandem-Settings-Nameplates", "Noms au-dessus des joueurs");
    this.Text("Tandem-Settings-Recorder", "Enregistreur");
    this.Text("Tandem-Settings-Relay", "Relais");
    this.Text("Tandem-Settings-RelayToken", "Jeton d'hôte du relais");
    this.Text("Tandem-Settings-Story", "Histoire partagée");
    this.Text("Tandem-Settings-QuestFreeze", "Mettre votre histoire en pause (invité)");
    this.Text("Tandem-Settings-ScenePrivacy", "Scènes privées");
    this.Text("Tandem-Settings-TimeHost", "Votre Sandevistan (hôte)");
    this.Text("Tandem-Settings-TimeHost-Full", "ralentit tout votre jeu");
    this.Text("Tandem-Settings-TimeHost-Bubble", "ralentit les PNJ autour de vous");
  }
}
