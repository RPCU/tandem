// The shared campaign's natives (plugin/src/campaign_natives.cpp). Keep the
// two in sync: tools/check-natives.py checks them with Natives.reds.

// One copy of the campaign: whose (`player` 0: none; `mine`: ours), who
// played it last, the last quest done in it, when it was last saved (local
// time; "": not yet), its generation and its id.
public native struct TandemCampaignCopy {
  native let player: Uint32;
  native let name: String;
  native let who: String;
  native let lastQuest: String;
  native let when: String;
  native let generation: Uint32;
  native let copy: Uint64;
  native let mine: Bool;
}

// What the session's copies say: kind 0 nothing, 1 everyone has the same,
// 2 one is ahead of the others, 3 a fork; ours behind, the host's behind,
// we host.
public native struct TandemCampaignVerdict {
  native let kind: Int32;
  native let selfBehind: Bool;
  native let hostBehind: Bool;
  native let hosting: Bool;
}

// An archive in the campaigns folder: its path and file name, its copy (who,
// the last quest, when, the generation), how many of its game's mods ours
// lacks and the first of their names, and whether we have that copy already.
public native struct TandemCampaignArchive {
  native let path: String;
  native let file: String;
  native let who: String;
  native let lastQuest: String;
  native let when: String;
  native let generation: Uint32;
  native let missing: Int32;
  native let missingNames: String;
  native let known: Bool;
}

public native class TandemCampaignNative extends IScriptable {
  public static native func CampaignOn() -> Bool
  public static native func CampaignFolder() -> String
  // A save loaded with these words: the words to keep in it now (a new copy
  // of the campaign; the same; none, a copy set aside).
  public static native func CampaignLoaded(words: array<Uint64>) -> array<Uint64>
  public static native func CampaignStart() -> array<Uint64>
  public static native func CampaignWords() -> array<Uint64>
  public static native func CampaignWordsChanged() -> Bool
  public static native func CampaignPlaying() -> Bool
  public static native func CampaignRunning() -> TandemCampaignCopy
  public static native func CampaignLoadWarning() -> TandemCampaignCopy
  public static native func CampaignLeft() -> Bool
  // Only store: callable from a callback on any thread.
  public static native func CampaignQuest(title: String) -> Void
  public static native func CampaignSaved(success: Bool) -> Void
  // 1: a start's save, 2: an export's.
  public static native func CampaignAskSave(purpose: Int32) -> Bool
  public static native func CampaignImport(path: String) -> Bool
  // 0 idle, 1 saving, 2 writing, 3 done, 4 failed; its purpose (1 start, 2
  // export, 3 import), its text (the zip, the slot, or what went wrong).
  public static native func CampaignJob() -> Int32
  public static native func CampaignJobPurpose() -> Int32
  public static native func CampaignJobText() -> String
  public static native func CampaignJobSeen() -> Void
  public static native func CampaignArchives() -> array<TandemCampaignArchive>
  public static native func CampaignNews() -> Uint32
  public static native func CampaignVerdict() -> TandemCampaignVerdict
  public static native func CampaignHeads() -> array<TandemCampaignCopy>
  public static native func CampaignChoose(copy: Uint64) -> Void
  public static native func CampaignChosen() -> Uint64
  public static native func CampaignReport() -> Void
}
