# Other mods' records and files

Engine facts behind [`architecture/mod-compat.md`](../../architecture/mod-compat.md).
Tags as in [`engine-knowledge.md`](../engine-knowledge.md).

## 13.55 Does this game have a record, and its vanilla stand-in

- **A TweakDB record exists** when `RED4ext::TweakDB::Get()->GetRecord(id)`
  returns a handle (`plugin/src/mods.cpp` `ModRecords::Known`; SDK
  `TweakDB.hpp`); scripts can ask the same with
  `IsDefined(TweakDBInterface.GetRecord(id))`. A record a TweakXL tweak or a
  CET `TweakDB:CreateRecord` added exists only in the game that loaded it.
  [unverified]: that the call is null for a missing id rather than a
  default record, and its cost (memoized per record anyway).
- **A resource exists** when `RED4ext::ResourceDepot::Get()->ResourceExists(path)`
  says so (SDK `ResourceDepot.hpp`, a virtual of the depot; ArchiveXL adds
  its archives to the same depot). Not trusted blindly: the plugin checks
  it against our own V's look first (`ModRecords::CheckDepot`: every piece
  of our own look is on our disk), and with any piece denied it never
  drops a partner's. [unverified]
- **The hint's names** are the game's own enum value names, read in
  scripts with `EnumValueToString("gamedataItemType", ...)` of
  `Item_Record.ItemType().Type()`, `ItemCategory().Type()`,
  `Vehicle_Record.Type().Type()` (`gamedataVehicleType`: `Car`, `Bike`,
  `AV`, `Tank`), `Character_Record.CharacterType().Type()`
  (`gamedataNPCType`: `Human`, `Android`, `Drone`, `Mech`...), and
  `GetResolvedGenderName()` (`Female`, `Male`); seats from
  `VehDataPackage().VehSeatSet().VehSeats()`. A mod's record keeps vanilla
  enum values (a TweakXL record's `itemType` points at a vanilla
  `ItemType` record), so a game without the record still knows the type.
  [unverified]: the compile check passes; the values in game are not seen.
- **The stand-ins** (`core/src/mods.cpp` tables): records the repository
  already uses in game or in scenarios
  (`Items.Preset_Lexington_Default`, `Items.Preset_Katana_Default`,
  `Items.GrenadeFragRegular`, `Vehicle.v_standard2_archer_hella_player`,
  `Vehicle.v_sportbike2_arch_player`, `Vehicle.v_sport2_porsche_911turbo`,
  `Vehicle.v_standard25_mahir_supron_player`, `Vehicle.max_tac_av`,
  `Character.kab_tyger_claws_biker1_ranged1_saratoga_ma`,
  `Character.rcr_sixthstreet_hooligan_melee1_knife_wa`, the mock player's
  clothes `Items.FormalShirt_02_basic_03`, `Items.Pants_02_basic_01`,
  `Items.CasualShoes_01_basic_01`) and preset names of the game's usual
  form (`Items.Preset_<Name>_Default` for Overture, Saratoga, Ajax,
  Achilles, Grad, Carnage, Igla, Defender, Knife, Machete, Hammer,
  Baseball_Bat). [unverified] for the second group: each stand-in is
  checked to exist before use, so a wrong name falls through to the
  kind's default, the handgun or the Archer.
- **An NPC appearance a game lacks**: what `DynamicEntitySpec.appearanceName`
  does with a name its template has not (an NPC appearance pack's) is not
  known; `Npcs.reds` notes it when the body's `GetCurrentAppearanceName()`
  differs from the name asked once it is ready. [unverified]
- **A mod's radio stations** (radioExt) are numbered after the game's own:
  an index of `RadioStationDataProvider.GetStationsCount()` or more is a
  mod's; radioExt turns the car's real receiver off while its station
  plays (`ToggleRadioReceiver(false)`). From radioExt's Lua (read
  2026-11-19). [unverified] in game.
- **Weather from another mod** (WeatherSwitcher): native
  `WeatherSystem.SetWeather(name, blend, priority)` at priority 9, re-forced
  in its lock mode; redscript cannot wrap a native, and a CET call does not
  go through a script function. tandem sets the host's back at priority 100. [unverified]: that a higher priority wins.
- **Slow motion from another mod** (OverclockOverheat's optional
  `OverclockDilation`, through `TimeSystem.SetTimeDilation` from a wrap of
  `TimeDilationFocusModeEvents.OnUpdate`): the reason of an active global
  dilation cannot be listed from scripts; `GetActiveTimeDilation()` gives
  the whole factor, `GetActiveTimeDilation(reason, true)` a known reason's.
  The menus' reasons are TweakDB flats `timeSystem.<profile>.reason`
  (§13.15). [unverified]
