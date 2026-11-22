// No silent fallback (docs/architecture/degradations.md): what this game
// and the other players' games run without, told to the player and kept in
// the co-op menu's status panel. The registry is the plugin's
// (TandemDegraded, DegradedNatives.reds); whatever takes a lesser path
// registers there, by an id of core/src/degraded.cpp's catalogue:
//
//   TandemDegraded.State("vehicle move", "why");     // holds until Over
//   TandemDegraded.Chosen("vehicle move", "tandem.ini kinematic_ride = off");
//   TandemDegraded.Fallback("prompt drawn", "why");  // one body, once
//
// Each id has its words in Text.reds (`Degraded-Id-<id with dashes>`,
// English and French: `task degraded:check`).

public class TandemDegradedSay {
  // What an id means, for the player; the id itself when a newer build's
  // game said one we have no words for.
  public static func Name(id: String, opt english: Bool) -> String {
    let key = "Degraded-Id-" + StrReplaceAll(id, " ", "-");
    let words = TandemText.T(english, key);
    return Equals(words, key) ? id : words;
  }

  // Every tick of a session: what started, ended or was taken since the
  // last one, on screen. A core feature that goes off while we play ends the
  // session, with the reason (co-op cannot be right without it).
  public static func Tick(game: GameInstance, system: ref<TandemSystem>) -> Void {
    let news = TandemDegraded.TakeNews();
    let leave = "";
    for entry in news {
      let what = TandemDegradedSay.Name(entry.id);
      let english = TandemDegradedSay.Name(entry.id, true);
      if entry.player != 0u {
        let who = TandemCoop.NameOf(entry.player);
        let key = entry.over ? "Degraded-OtherBack" : "Degraded-Other";
        TandemText.Say(game, SimpleMessageType.Negative, 10.0, key, TandemText.F(key, who, what), TandemText.FE(true, key, who, english));
      } else if entry.over {
        TandemText.Say(game, SimpleMessageType.Neutral, 6.0, "Degraded-Back", TandemText.F("Degraded-Back", what), TandemText.FE(true, "Degraded-Back", english));
      } else if entry.fallback {
        TandemText.Say(game, SimpleMessageType.Negative, 10.0, "Degraded-Fallback", TandemText.F("Degraded-Fallback", what), TandemText.FE(true, "Degraded-Fallback", english));
      } else if entry.chosen {
        TandemText.Say(game, SimpleMessageType.Neutral, 8.0, "Degraded-Chosen", TandemText.F("Degraded-Chosen", what), TandemText.FE(true, "Degraded-Chosen", english));
      } else {
        TandemText.Say(game, SimpleMessageType.Negative, 12.0, "Degraded-Off", TandemText.F("Degraded-Off", what), TandemText.FE(true, "Degraded-Off", english));
        if entry.core {
          leave = entry.id;
        }
      }
    }
    // (Not with compat = off: a developer's game runs on, and says so to
    // the others.)
    let problems = TandemNative.CompatProblems();
    if StrLen(leave) > 0 && ArraySize(problems) > 0 {
      TandemNative.Log(s"degraded: \(leave) went off in a session: leaving it (co-op cannot run without it)");
      system.CoopLeaveSaying("Degraded-CoreEnded");
    }
  }

  // A session tandem.ini or the menu chose was dropped because a core
  // feature is off: said once, when a V is in the game.
  public static func SayRefused(game: GameInstance) -> Void {
    if !TandemDegraded.TakeRefused() {
      return;
    }
    let off = TandemDegraded.CoreOff();
    if ArraySize(off) == 0 {
      return;
    }
    let first = off[0];
    TandemText.Say(game, SimpleMessageType.Negative, 15.0, "Session-CoreOff", TandemText.F("Session-CoreOff", TandemDegradedSay.Name(first.id)), TandemText.FE(true, "Session-CoreOff", TandemDegradedSay.Name(first.id, true)));
  }

  // Why nothing can be hosted or joined, for the co-op menu's popup: one
  // paragraph per core feature that is off ("" when none).
  public static func CoreText() -> String {
    let text = "";
    for entry in TandemDegraded.CoreOff() {
      if StrLen(text) > 0 {
        text += "\n\n";
      }
      text += TandemText.F("Degraded-CoreOff", TandemDegradedSay.Name(entry.id), entry.reason);
    }
    return text;
  }

  // The status panel's lines (Coop.reds), in a session or not: what this
  // game runs without, the fallbacks it took, and what each other player's
  // game said.
  public static func StatusLines(english: Bool) -> array<String> {
    let lines: array<String>;
    for entry in TandemDegraded.Entries() {
      let what = TandemDegradedSay.Name(entry.id, english);
      if entry.player != 0u {
        ArrayPush(lines, TandemText.FE(english, "Degraded-Other", TandemCoop.NameOf(entry.player), what));
      } else if entry.fallback {
        ArrayPush(lines, TandemText.FE(english, "Degraded-StatusFallback", what, s"\(entry.count)"));
      } else if entry.chosen {
        ArrayPush(lines, TandemText.FE(english, "Degraded-StatusChosen", what));
      } else {
        ArrayPush(lines, TandemText.FE(english, "Degraded-StatusOff", what));
      }
    }
    return lines;
  }
}
