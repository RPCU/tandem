// The player's co-op playthroughs (plugin/src/campaign_natives.cpp,
// tandem/campaigns.hpp; docs/architecture/coop-menu.md, "Campaigns"): which
// playthroughs are co-op campaigns of ours, and which V a guest last joined
// each host with. Keep the two in sync: tools/check-natives.py checks them
// with Natives.reds. Each only takes the plugin's own lock: any thread may
// call it (a menu callback too).

// One save of the main menu's list.
public native struct TandemSave {
  // -1: none
  native let index: Int32;
  native let level: Int32;
  native let location: String;
  native let playthrough: String;
  native let modded: Bool;
}

public native class TandemCampaignNative extends IScriptable {
  // The main menu's list starts over (its saves are being read).
  public static native func SavesReset()
  // One save's metadata, as the main menu's callback has it.
  public static native func SaveSeen(index: Int32, playthrough: String, seconds: Float, level: Float, location: String, stamp: Uint64, modded: Bool)
  // The main menu loads this save of its list (-1: a new game, or one it
  // does not know).
  public static native func Loading(index: Int32)
  // A world is loaded: its playthrough time now, whether its save says it is
  // a co-op campaign. True: it is one.
  public static native func WorldLoaded(seconds: Float, marked: Bool) -> Bool
  // The world was saved (its playthrough time then).
  public static native func WorldSaved(seconds: Float)
  // This world is a co-op campaign from now on, or no longer.
  public static native func Mark(on: Bool)
  public static native func IsCampaign() -> Bool
  // The newest save of a co-op campaign in the main menu's list.
  public static native func CampaignSave() -> TandemSave
  // The newest save of the V this host (TandemHost.key) was last joined with.
  public static native func LastV(host: String) -> TandemSave
  // (tests: coop campaign next) The next world loads as a co-op campaign.
  public static native func TestNext()
  // What is known, in one line (coop campaign).
  public static native func CampaignReport() -> String
}
