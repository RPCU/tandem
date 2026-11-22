// The puppets' attitude groups' relations, declared as the engine's own
// records (docs/architecture/npcs.md, "How NPCs see a remote V";
// engine-knowledge §13.17).
//
// A relation between two groups is an Attitude record (group1, group2,
// value), not a field of a group. A puppet's group (tweaks/tandem/attitudes.tweak)
// must stand towards every group as V's `player` group does, so every
// Attitude record that names Attitudes.Group_Player is cloned once per
// group of ours, with ours in the player's place. Done by a scripted tweak
// (TweakXL runs it as it loads the .tweak files, before any world) and not
// listed in the .tweak file: the list is then the running game's own, with
// what a patch or another mod changed, and no dump of the game's TweakDB
// is needed to write it.
//
// Not known until run in game (§13.17): whether the engine's relation
// table takes Attitude records TweakXL adds. TandemAttitudes.Start reads
// the relations back and logs the answer; the runtime copy
// (Attitudes.reds) stays as the net under it.

public abstract class TandemAttitudeRelations {
  // The group records of ours (attitudes.tweak), as TandemAttitudes names
  // their groups.
  public static func Ours() -> array<TweakDBID> {
    return [t"Attitudes.Group_TandemPlayer", t"Attitudes.Group_TandemPlayer1", t"Attitudes.Group_TandemPlayer2", t"Attitudes.Group_TandemPlayer3"];
  }

  public static func Player() -> TweakDBID {
    return t"Attitudes.Group_Player";
  }

  // Every record of a type, by its TweakDB name ("Attitude"): TweakXL's
  // GetRecords is asked under each spelling a version of it may want.
  public static func RecordsOf(type: String) -> array<ref<TweakDBRecord>> {
    let records = TweakDBInterface.GetRecords(StringToName(type));
    if ArraySize(records) == 0 {
      records = TweakDBInterface.GetRecords(StringToName(s"\(type)_Record"));
    }
    if ArraySize(records) == 0 {
      records = TweakDBInterface.GetRecords(StringToName(s"gamedata\(type)_Record"));
    }
    return records;
  }

  // The flats Start logs: how many Attitude records were read, how many
  // name the player's group, how many clones exist (-1: the tweak never ran).
  public static func ReadFlat() -> TweakDBID {
    return t"Tandem.attitudeRecordsRead";
  }

  public static func PlayersFlat() -> TweakDBID {
    return t"Tandem.attitudeRecordsOfPlayer";
  }

  public static func MirroredFlat() -> TweakDBID {
    return t"Tandem.attitudeRecordsMirrored";
  }

  // The clone for one of ours (its index in Ours) and another group.
  public static func NameOf(ours: Int32, other: CName) -> String {
    return "Attitudes.TandemRelation" + IntToString(ours) + "_" + NameToString(other);
  }

  public static func Mirror() -> Void {
    let player = TandemAttitudeRelations.Player();
    let ours = TandemAttitudeRelations.Ours();
    let records = TandemAttitudeRelations.RecordsOf("Attitude");
    let players = 0;
    let mirrored = 0;
    for record in records {
      let relation = record as Attitude_Record;
      let first: wref<AttitudeGroup_Record>;
      let second: wref<AttitudeGroup_Record>;
      if IsDefined(relation) {
        first = relation.Group1();
        second = relation.Group2();
      }
      if IsDefined(first) && IsDefined(second) {
        let firstId = first.GetID();
        let secondId = second.GetID();
        // The flat that names the player, and the group on the other side.
        let side = "";
        let other = n"None";
        if firstId == player && secondId != player {
          side = ".group1";
          other = second.Name();
        } else if secondId == player && firstId != player {
          side = ".group2";
          other = first.Name();
        }
        if StrLen(side) > 0 && IsNameValid(other) && !StrBeginsWith(NameToString(other), "tandem_player") {
          players += 1;
          let i = 0;
          while i < ArraySize(ours) {
            let name = TandemAttitudeRelations.NameOf(i, other);
            let id = TDBID.Create(name);
            if !IsDefined(TweakDBInterface.GetRecord(id)) {
              TweakDBManager.CloneRecord(StringToName(name), relation.GetID());
            }
            if TweakDBManager.SetFlat(TDBID.Create(name + side), ToVariant(ours[i])) && TweakDBManager.UpdateRecord(id) {
              mirrored += 1;
            }
            i += 1;
          }
        }
      }
    }
    TweakDBManager.SetFlat(n"Tandem.attitudeRecordsRead", ToVariant(ArraySize(records)));
    TweakDBManager.SetFlat(n"Tandem.attitudeRecordsOfPlayer", ToVariant(players));
    TweakDBManager.SetFlat(n"Tandem.attitudeRecordsMirrored", ToVariant(mirrored));
  }
}

public class TandemAttitudeTweak extends ScriptableTweak {
  // job thread safe: TweakXL calls it once while it loads the tweaks, before
  // any world or session; it reads TweakDB records and adds records and
  // flats through TweakXL, and touches no script object.
  protected cb func OnApply() -> Void {
    TandemAttitudeRelations.Mirror();
  }
}
