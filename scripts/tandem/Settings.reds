// The co-op menu's settings page (docs/architecture/coop-menu.md,
// "Settings"): tandem.ini's keys the player may change, each saved there as
// it changes (TandemNative.SetSetting) and applied at once where that is
// safe (the link's default and pings by the plugin's comms, in a session too); what a running session was set up with (the name, the relay, the
// story) waits for the session's end, the hooks installed at launch (quest
// freeze, scene privacy) for a restart. Tests: `tools/game.sh cmd coop
// settings` logs them, `coop set KEY VALUE` changes one.

public class TandemSettings {
  // In the page's order. kind: 0 on/off, 1 a choice, 2 typed. when: 0 at
  // once, 1 from the next session, 2 after a restart. (relay_token is not
  // here: relays are open, a token is set in tandem.ini alone.)
  public static func Keys() -> array<String> {
    return [
      "name", "friendly_fire", "enemy_scaling", "notices", "markers", "nameplates", "party_hud", "link", "pings", "recorder",
      "relay", "code", "direct", "story", "quest_freeze", "scene_privacy", "time_host", "race_car"
    ];
  }

  private static func Labels() -> array<String> {
    return [
      "Settings-Name", "Settings-FriendlyFire", "Settings-EnemyScaling", "Settings-Notices", "Settings-Markers", "Settings-Nameplates", "Settings-PartyHud", "Settings-Link", "Settings-Pings", "Settings-Recorder",
      "Settings-Relay", "Settings-Code", "Settings-Direct", "Settings-Story", "Settings-QuestFreeze", "Settings-ScenePrivacy", "Settings-TimeHost", "Settings-RaceCar"
    ];
  }

  // What each one does, shown under the page for the row the cursor is on.
  private static func Abouts() -> array<String> {
    return [
      "Settings-About-Name", "Settings-About-FriendlyFire", "Settings-About-EnemyScaling", "Settings-About-Notices", "Settings-About-Markers", "Settings-About-Nameplates", "Settings-About-PartyHud", "Settings-About-Link", "Settings-About-Pings", "Settings-About-Recorder",
      "Settings-About-Relay", "Settings-About-Code", "Settings-About-Direct", "Settings-About-Story", "Settings-About-QuestFreeze", "Settings-About-ScenePrivacy", "Settings-About-TimeHost", "Settings-About-RaceCar"
    ];
  }

  private static func Kinds() -> array<Int32> {
    return [2, 0, 0, 1, 0, 1, 0, 1, 0, 0, 2, 1, 1, 0, 0, 0, 1, 1];
  }

  private static func Whens() -> array<Int32> {
    return [1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 1, 0];
  }

  private static func IndexOf(key: String) -> Int32 {
    let keys = TandemSettings.Keys();
    let i = 0;
    while i < ArraySize(keys) {
      if Equals(keys[i], key) {
        return i;
      }
      i += 1;
    }
    return -1;
  }

  public static func Kind(key: String) -> Int32 {
    let i = TandemSettings.IndexOf(key);
    let kinds = TandemSettings.Kinds();
    return i < 0 ? -1 : kinds[i];
  }

  // A choice's values (tandem.ini's words) and their texts.
  private static func Choices(key: String) -> array<String> {
    if Equals(key, "notices") {
      return ["all", "important", "minimal"];
    }
    if Equals(key, "time_host") {
      return ["full", "bubble"];
    }
    if Equals(key, "race_car") {
      return ["own", "copy"];
    }
    if Equals(key, "nameplates") {
      return ["near", "always", "off"];
    }
    if Equals(key, "direct") {
      return ["auto", "off"];
    }
    if Equals(key, "code") {
      return ["new", "keep"];
    }
    if Equals(key, "link") {
      return ["auto", "ask", "off"];
    }
    return ["on", "off"];
  }

  private static func ChoiceTexts(key: String) -> array<String> {
    if Equals(key, "notices") {
      return ["Settings-Notices-All", "Settings-Notices-Important", "Settings-Notices-Minimal"];
    }
    if Equals(key, "time_host") {
      return ["Settings-TimeHost-Full", "Settings-TimeHost-Bubble"];
    }
    if Equals(key, "race_car") {
      return ["Settings-RaceCar-Own", "Settings-RaceCar-Copy"];
    }
    if Equals(key, "nameplates") {
      return ["Settings-Nameplates-Near", "Settings-Nameplates-Always", "Common-Off"];
    }
    if Equals(key, "direct") {
      return ["Settings-Direct-Auto", "Common-Off"];
    }
    if Equals(key, "code") {
      return ["Settings-Code-New", "Settings-Code-Keep"];
    }
    if Equals(key, "link") {
      return ["Settings-Link-Auto", "Settings-Link-Ask", "Common-Off"];
    }
    return ["Common-On", "Common-Off"];
  }

  // Friendly fire and enemy scaling are the host's rules for the whole
  // session (proto::Rules, docs/architecture/combat.md, npcs.md): a guest's
  // page shows the host's and cannot change them; its own tandem.ini's
  // count when it hosts.
  public static func IsHostsRule(key: String) -> Bool {
    return (Equals(key, "friendly_fire") || Equals(key, "enemy_scaling")) && TandemNative.IsStarted() && !TandemNative.IsHost();
  }

  // The host's value of one of its rules, as tandem.ini writes it.
  private static func HostsValue(key: String) -> String {
    if Equals(key, "enemy_scaling") {
      return TandemProgress.EnemyScaling() ? "on" : "off";
    }
    return TandemFight.FriendlyFire() ? "on" : "off";
  }

  // What the value is, in words.
  public static func ValueText(key: String) -> String {
    let value = TandemSettings.IsHostsRule(key) ? TandemSettings.HostsValue(key) : TandemNative.Setting(key);
    if TandemSettings.Kind(key) == 2 {
      if StrLen(value) == 0 {
        return TandemText.Get("Settings-NotSet");
      }
      return Equals(key, "relay_token") ? TandemText.Get("Settings-TokenSet") : value;
    }
    let values = TandemSettings.Choices(key);
    let texts = TandemSettings.ChoiceTexts(key);
    let i = 0;
    while i < ArraySize(values) {
      if Equals(values[i], value) {
        return TandemText.Get(texts[i]);
      }
      i += 1;
    }
    return value;
  }

  // "Name: V", and when it takes effect if not now.
  public static func Label(key: String, inSession: Bool) -> String {
    let i = TandemSettings.IndexOf(key);
    if i < 0 {
      return key;
    }
    let label = TandemText.F("Settings-Item", TandemSettings.Title(key), TandemSettings.ValueText(key));
    if TandemSettings.IsHostsRule(key) {
      return TandemText.F("Settings-HostsRule", label);
    }
    let whens = TandemSettings.Whens();
    let when = whens[i];
    if when == 2 {
      return TandemText.F("Settings-AfterRestart", label);
    }
    if when == 1 && inSession {
      return TandemText.F("Settings-NextSession", label);
    }
    return label;
  }

  // Each array in a local before it is indexed: a String read straight from
  // a returned array (Labels()[i]) came out empty in game, so every row
  // showed ": V" (2026-11-20; tools/menu-check.py refuses it).
  public static func Title(key: String) -> String {
    let i = TandemSettings.IndexOf(key);
    let labels = TandemSettings.Labels();
    return i < 0 ? key : TandemText.Get(labels[i]);
  }

  public static func About(key: String) -> String {
    let i = TandemSettings.IndexOf(key);
    let abouts = TandemSettings.Abouts();
    return i < 0 ? "" : TandemText.Get(abouts[i]);
  }

  // What a typed setting's popup says: the name its rules, the others
  // "type the new value".
  public static func Prompt(key: String) -> String {
    return TandemText.Get(Equals(key, "name") ? "Name-Ask" : "Settings-Type");
  }

  // Why a typed value was refused, in words.
  public static func Refusal(key: String, value: String) -> String {
    return TandemText.F(Equals(key, "name") ? "Name-Invalid" : "Settings-Invalid", value);
  }

  // The next value of an on/off or a choice, as tandem.ini writes it.
  public static func Next(key: String) -> String {
    let values = TandemSettings.Choices(key);
    let value = TandemNative.Setting(key);
    let i = 0;
    while i < ArraySize(values) {
      if Equals(values[i], value) {
        return values[(i + 1) % ArraySize(values)];
      }
      i += 1;
    }
    return values[0];
  }

  // Change it and save it (false: not a value of it; the plugin logs why).
  public static func Set(key: String, value: String) -> Bool {
    if TandemSettings.IsHostsRule(key) {
      TandemNative.Log(s"coop: setting \(key) refused: the host's rule while we are its guest");
      return false;
    }
    let done = TandemNative.SetSetting(key, value);
    TandemNative.Log(s"coop: setting \(key) \(done ? "changed" : "refused")");
    return done;
  }

  // ---- what the scripts apply themselves, read as they need it ----

  // Off: no marker for the other players (Markers.reds).
  public static func Markers() -> Bool {
    return NotEquals(TandemNative.Setting("markers"), "off");
  }

  // The name over the other players (PartyHud.reds' nameplate, Markers.reds'
  // label): 0 near (to 12 m, gone at 20 m), 1 always (any distance, smaller
  // and fainter far away), 2 off. Read every frame.
  public static func NameplateMode() -> Int32 {
    let value = TandemNative.Setting("nameplates");
    if Equals(value, "always") {
      return 1;
    }
    return Equals(value, "off") ? 2 : 0;
  }

  // Off: no party list on the HUD (PartyHud.reds); the nameplates are
  // `nameplates`' own. Read every frame.
  public static func PartyHud() -> Bool {
    return NotEquals(TandemNative.Setting("party_hud"), "off");
  }

  // The least important notice shown (TandemText.Importance): 0 all, 1 no
  // news, 2 only what asks for an answer or went wrong.
  public static func NoticeLevel() -> Int32 {
    let value = TandemNative.Setting("notices");
    if Equals(value, "minimal") {
      return 2;
    }
    return Equals(value, "important") ? 1 : 0;
  }

  // coop settings | coop set KEY VALUE | coop name [reset] (tools/game.sh
  // cmd). True: it was one.
  public static func Command(command: String) -> Bool {
    if Equals(command, "coop name") || Equals(command, "coop name reset") {
      if Equals(command, "coop name reset") {
        TandemNative.NameAsked(false);
      }
      TandemNative.Log(s"coop: name \"\(TandemNative.Setting("name"))\", the co-op menu asks for one: \(TandemNative.NameToAsk() ? "yes" : "no")");
      return true;
    }
    if Equals(command, "coop settings") {
      for key in TandemSettings.Keys() {
        let value = Equals(key, "relay_token") ? (StrLen(TandemNative.Setting(key)) > 0 ? "set" : "") : TandemNative.Setting(key);
        if TandemSettings.IsHostsRule(key) {
          value = s"\(TandemSettings.HostsValue(key)) (the host's)";
        }
        TandemNative.Log(s"coop: setting \(key) = \(value)");
      }
      return true;
    }
    if !StrBeginsWith(command, "coop set ") {
      return false;
    }
    let rest = StrAfterFirst(command, "coop set ");
    let key = StrContains(rest, " ") ? StrBeforeFirst(rest, " ") : rest;
    let value = StrContains(rest, " ") ? StrAfterFirst(rest, " ") : "";
    TandemSettings.Set(key, value);
    return true;
  }
}
