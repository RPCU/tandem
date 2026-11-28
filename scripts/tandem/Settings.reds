// The co-op menu's settings page (docs/architecture/coop-menu.md,
// "Settings"): one short page, kept to what a player decides. The host's
// rules (friendly fire, enemy scaling, level gap) are the host's to set for
// everyone: a guest in a session has no row for them, its status says the
// host's (HostRulesLine). The others only change one's own screen: the
// name, the notices, markers, names and party list. Each change is saved to
// tandem.ini (TandemNative.SetSetting) and applied at once (the name: from
// the next session). Every other key stays in tandem.ini alone, at its
// default (IniKeys: the test commands still set them). What the row under
// the cursor does, and when a change takes effect, is the description
// beside the list. Tests: `tools/game.sh cmd coop settings` logs them,
// `coop set KEY VALUE` changes one.

public class TandemSettings {
  // In the page's order. kind: 0 on/off, 1 a choice, 2 typed. when: 0 at
  // once, 1 from the next session, 2 after a restart.
  public static func Keys() -> array<String> {
    return ["name", "friendly_fire", "enemy_scaling", "level_gap", "campaign", "notices", "markers", "nameplates", "party_hud"];
  }

  private static func Labels() -> array<String> {
    return ["Settings-Name", "Settings-FriendlyFire", "Settings-EnemyScaling", "Settings-LevelGap", "Settings-Campaign", "Settings-Notices", "Settings-Markers", "Settings-Nameplates", "Settings-PartyHud"];
  }

  // What each one does, shown beside the page for the row the cursor is on.
  private static func Abouts() -> array<String> {
    return ["Settings-About-Name", "Settings-About-FriendlyFire", "Settings-About-EnemyScaling", "Settings-About-LevelGap", "Settings-About-Campaign", "Settings-About-Notices", "Settings-About-Markers", "Settings-About-Nameplates", "Settings-About-PartyHud"];
  }

  private static func Kinds() -> array<Int32> {
    return [2, 0, 0, 1, 1, 1, 0, 1, 0];
  }

  private static func Whens() -> array<Int32> {
    return [1, 0, 0, 0, 0, 0, 0, 0, 0];
  }

  // The host's rules (proto::Rules, sent to every game as they change): rows
  // of the page on a host and out of a session (they apply to the sessions
  // it hosts), left out on a guest in a session.
  public static func HostRules() -> array<String> {
    return ["friendly_fire", "enemy_scaling", "level_gap"];
  }

  // The host's own, not a rule of the session (no other game needs it): a
  // row on a host's page and out of a session, none on a guest in one.
  public static func HostOnly() -> array<String> {
    return ["campaign"];
  }

  // tandem.ini's keys off the page, at their defaults for every player
  // (technical or rarely changed: plugin/src/config.hpp says each): `coop
  // settings` logs them, `coop set` changes them (the scenarios).
  public static func IniKeys() -> array<String> {
    return ["link", "pings", "recorder", "relay", "code", "direct", "story", "quest_freeze", "scene_privacy", "time_host", "race_car"];
  }

  // The rows the page shows now.
  public static func Rows() -> array<String> {
    let keys = TandemSettings.Keys();
    let guest = TandemSettings.IsGuest();
    let rows: array<String>;
    for key in keys {
      let hosts = TandemSettings.HostOnly();
      if !guest || !TandemSettings.IsHostRule(key) && !ArrayContains(hosts, key) {
        ArrayPush(rows, key);
      }
    }
    return rows;
  }

  private static func IsGuest() -> Bool {
    return TandemNative.IsStarted() && !TandemNative.IsHost();
  }

  public static func IsHostRule(key: String) -> Bool {
    let rules = TandemSettings.HostRules();
    return ArrayContains(rules, key);
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
    if Equals(key, "nameplates") {
      return ["near", "always", "off"];
    }
    if Equals(key, "level_gap") {
      return ["8", "5", "12", "off"];
    }
    if Equals(key, "campaign") {
      return ["open", "invite", "closed"];
    }
    return ["on", "off"];
  }

  private static func ChoiceTexts(key: String) -> array<String> {
    if Equals(key, "notices") {
      return ["Settings-Notices-All", "Settings-Notices-Important", "Settings-Notices-Minimal"];
    }
    if Equals(key, "nameplates") {
      return ["Settings-Nameplates-Near", "Settings-Nameplates-Always", "Common-Off"];
    }
    if Equals(key, "level_gap") {
      return ["Settings-LevelGap-Eight", "Settings-LevelGap-Five", "Settings-LevelGap-Twelve", "Common-Off"];
    }
    if Equals(key, "campaign") {
      return ["Settings-Campaign-Open", "Settings-Campaign-Invite", "Settings-Campaign-Closed"];
    }
    return ["Common-On", "Common-Off"];
  }

  // A guest in a session plays by the host's rules: the host said them
  // (proto::Rules); its own tandem.ini's count when it hosts.
  public static func IsHostsRule(key: String) -> Bool {
    return TandemSettings.IsHostRule(key) && TandemSettings.IsGuest();
  }

  // The session's value of one of the host's rules, as tandem.ini writes it.
  private static func HostsValue(key: String) -> String {
    if Equals(key, "enemy_scaling") {
      return TandemProgress.EnemyScaling() ? "on" : "off";
    }
    if Equals(key, "level_gap") {
      let gap = TandemFight.LevelGap();
      return gap > 0 ? s"\(gap)" : "off";
    }
    return TandemFight.FriendlyFire() ? "on" : "off";
  }

  // A guest's status line: the host's rules, read only ("Host's rules:
  // friendly fire off, enemy scaling on, level gap 8").
  public static func HostRulesLine(english: Bool) -> String {
    let gap = TandemFight.LevelGap();
    let fire = TandemText.FE(english, "Rule-FriendlyFire", TandemText.T(english, TandemFight.FriendlyFire() ? "Common-On" : "Common-Off"));
    let scaling = TandemText.FE(english, "Rule-EnemyScaling", TandemText.T(english, TandemProgress.EnemyScaling() ? "Common-On" : "Common-Off"));
    let levels = TandemText.FE(english, "Rule-LevelGap", gap > 0 ? s"\(gap)" : TandemText.T(english, "Common-Off"));
    return TandemText.FE(english, "Status-HostRules", fire + ", " + scaling + ", " + levels);
  }

  // What the value is, in words.
  public static func ValueText(key: String) -> String {
    let value = TandemSettings.IsHostsRule(key) ? TandemSettings.HostsValue(key) : TandemNative.Setting(key);
    if TandemSettings.Kind(key) == 2 {
      return StrLen(value) == 0 ? TandemText.Get("Settings-NotSet") : value;
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

  // The row: "Name: V", on one line (a long typed value is cut, the
  // description shows it whole). When a change takes effect is the
  // description's (Note), where the game's own settings say it.
  public static func Label(key: String) -> String {
    if TandemSettings.IndexOf(key) < 0 {
      return key;
    }
    let value = TandemSettings.ValueText(key);
    if StrLen(value) > 18 {
      value = StrLeft(value, 17) + "…";
    }
    return TandemText.F("Settings-Item", TandemSettings.Title(key), value);
  }

  // When a change takes effect, if not at once ("" then); for a host's
  // rule, that it is everyone's.
  public static func Note(key: String, inSession: Bool) -> String {
    let i = TandemSettings.IndexOf(key);
    if i < 0 {
      return "";
    }
    if TandemSettings.IsHostRule(key) {
      return TandemText.Get("Settings-HostsRule");
    }
    let whens = TandemSettings.Whens();
    let when = whens[i];
    if when == 2 {
      return TandemText.Get("Settings-AfterRestart");
    }
    if when == 1 && inSession {
      return TandemText.Get("Settings-NextSession");
    }
    return "";
  }

  // Each array in a local before it is indexed: a String read straight from
  // a returned array (Labels()[i]) came out empty in game, so every row
  // showed ": V" (2026-11-20; tools/menu-check.py refuses it).
  public static func Title(key: String) -> String {
    let i = TandemSettings.IndexOf(key);
    let labels = TandemSettings.Labels();
    return i < 0 ? key : TandemText.Get(labels[i]);
  }

  // What it does; a typed value the row cut, whole after it.
  public static func About(key: String) -> String {
    let i = TandemSettings.IndexOf(key);
    let abouts = TandemSettings.Abouts();
    if i < 0 {
      return "";
    }
    let about = TandemText.Get(abouts[i]);
    let value = TandemSettings.ValueText(key);
    if TandemSettings.Kind(key) == 2 && StrLen(value) > 18 {
      about += "\n" + value;
    }
    return about;
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
  // A guest in a session leaves the host's rules alone: they are the host's.
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
      let keys = TandemSettings.Keys();
      let others = TandemSettings.IniKeys();
      for other in others {
        ArrayPush(keys, other);
      }
      for key in keys {
        let value = TandemNative.Setting(key);
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
