# Story

## Inside the plugin

- **Story** (`scripts/tandem/Story.reds`, `Phone.reds`, `Dialogue.reds`,
  `plugin/src/engine/quests.cpp`, `core/src/story.cpp`; milestone 6 step 1
  of `research/coop-quests-design.md`). The host's game runs the story;
  everything travels as the story messages (`Journal` … `QuestReward`,
  one path through `Endpoint::SendStory` / `events::Story`; the host keeps
  the journal, tracked entry, open call and hub for late joiners).
  - _A guest's own story waits_: a hook on `QuestPhaseInstance::ExecuteNode`
    (hash 3227858325, behind an RTTI layout check) refuses the nodes of the
    guest's story phases while it is in a session, the way the engine
    skips a disabled node (result 1, read offline: engine-knowledge §13.15),
    holds each refused signal and gives it back through the root phase's
    `ExecutePath` when the freeze ends. **Per phase, not global**:
    open-world phases (communities, vendors, NCPD hustles, city scenes,
    fixers: Codeware's list, plus `quest_freeze_allow`) and phases under
    them keep running, so a guest away from the host runs its own
    surroundings (`research/coop-regions.md`). **On by default** since
    2026-11-21 (the 11 `story-*` and `quest-trace` scenarios passed in game
    against the mock player; the two-game pass is next); `quest_freeze = off`
    gives a guest calls, lines, hubs and rewards but not the journal or texts.
  - _From the load_ (2026-11-21, not yet run in game): a join the co-op
    menu chose holds the guest's own quests and saving from the world's
    load, not from the host's welcome (`HoldEarly`), so a new V's prologue
    or a save's quest does not run on while it connects; a session that
    ends before the welcome gives both back (`ReleaseEarly`). A fresh
    guest's prologue: `coop-menu.md`, New co-op game.
  - _Saves_: a guest in a session never saves (`TandemStorySystem` overrides
    `IsSavingLocked`, plus a `SaveLocksManager` lock and dropped autosaves).
    Every journal entry the mirror changes keeps its state from before; on
    leaving (the guest quits or the host is gone) the journal and tracked
    entry go back, are checked, the freeze ends and saving unlocks. One
    journal change moves others (engine facts §13.99), so the entries go
    back parents first, a Failed one through Active, again until a pass
    changes nothing (`RestoreEntries`); then the journal is compared with
    the session's first snapshot of it (an entry the mirror never touched
    must be as it was; one new since is the guest's own game's, kept). Each
    step logs its counts and the first entries that differ. A
    mismatch keeps saving locked and says "load your last save", until a
    save is loaded (a later session's clean restore does not unlock it:
    its baseline is the damaged journal). The
    design's final step (one save, then its reload) is `Saves.reds`'s, by
    `tandem.ini` `leave` (`progression.md`, "Saves").
  - _Journal, tracker, markers_: the host's journal (snapshot at join,
    changes with their notify flag, so the guest gets the same toasts and
    sounds) and tracked quest; quest pins follow the journal. Shown only on
    a frozen guest; any other game sees the host's tracked objective as its
    shared waypoint, "NAME's objective" (`WaypointKind::Objective`,
    [`comms.md`](comms.md) decision 4). Entity-bound pins and objective
    counters: step 2.
  - _Calls and texts_ (`Phone.reds`): the host's `PhoneSystem` phases go to
    every phone; a guest presents them through its own `PhoneSystem`
    (ringtone, incoming widget, audio call with the caller's portrait) with
    its phone facts held back. Anyone answers: the first answer wins
    (`story::CallArbiter` on the host's endpoint) and picks up the host's
    phone. Texts come through the journal; a guest's reply goes to the host,
    which applies it if the option is still open (first reply wins).
    Who may answer for the host (2026-11-21, not yet run in game; scenario
    `story-private-calls`): a romance partner's calls and texts are the
    host's alone (`TandemPhone.HostOnly`: contact ids starting with the
    `private_actors` default, judy, panam, river, kerry, meredith; the game
    marks no contact as romance or lifepath, engine-knowledge §13.23;
    lifepath contacts: none named yet), Judy's or Panam's main-story calls
    included. A guest apart from the host (not grouped for quests, or not
    linked with it: beyond 150 m, 190 m to leave) may only answer a call
    that leaves no choice (not declinable) and never replies to a text.
    The guest's game refuses first, the call ringing on and the reason on
    its screen ("Only NAME can answer this contact: it is theirs alone",
    "Too far from NAME: you can only answer calls that leave no choice",
    "Too far from NAME to reply for them": `Phone-HostOnly`, `Phone-Away`,
    `Phone-AwayReply`); the host refuses the same acts again (its link to
    that guest), and a refused answer, which its endpoint had already
    marked, rings on every phone again (`TandemRing`).
  - _Dialogue_ (`Dialogue.reds`): the host's subtitles and its active choice
    hub reach guests within 25 m (holocall lines and phone hubs: everyone),
    drawn by the guest's own subtitle and hub widgets (a stand-in hub). A
    guest moves its own cursor and can suggest; the host's pick closes
    every hub. No story voice yet.
  - _Choices together_ (2026-11-21, **not yet run in game**): each
    player's cursor is marked on the others' hubs, on the option it is on:
    the player's name in the game's own option tag (the box before an
    option's text, as its lifepath and attribute tags: `[NAME] text`, which
    `DialogHubLogicController.UpdateDialogHubData` splits and gives each
    option's `DialogChoiceLogicController`; no panel of ours; beside a tag the
    option has, after the text when the option's tags are caption parts),
    "NAME suggests" once they suggest it. On the host's hub, every guest's;
    on a guest's, the host's and the other guests'. A player's own cursor
    is the widget's own selection. Live: a cursor move is a `ChoiceCursor`
    (`kCursorHover`), already sent both ways and relayed by the host (no
    protocol change); each game also says where its cursor starts when a
    hub opens (the host its `SelectedIndex`, a guest 0). The host still
    decides alone, nothing waits. The suggestion's notice ("NAME suggests:
    OPTION", `Dialogue-Suggests`) is shown only when the host's hub is not
    on screen (a menu, the HUD hidden). How: the game's choice widget
    (`dialogWidgetGameController`, found on the HUD layer by the story tick,
    10 per second) draws its copy of the `UIInteractions` board's hubs
    (`m_data`) and shows its root only while that copy has one
    (`m_AreDialogsOpen`). Two wraps of it (`UpdateDialogsData` keeps the
    board's hubs, `OnInteractionsChanged` rebuilds what it draws from them
    and ours before every redraw) put in, never on the board: the tags, and
    a hub of ours (a guest's stand-in, the host's test hub) **held on the
    widget**, open and active unless a hub of the game's is. The game takes
    a hub it did not make off the board a frame later (13 ms), so a hub
    only written to the board was never drawn (both screens, 2026-11-21);
    the first hold put it among the hubs but left `m_AreDialogsOpen` false,
    so the redraw hid the root it had just drawn it into. Held until
    `TandemRelease`; the story tick holds again while an option is missing
    (the game spawns them asynchronously) or the root is hidden. Log:
    `choice: hub N drawn by the game's choice widget`, and `choice: marks
not drawn on …: WHY` once per reason. To see in game: the tag against
    the game's own, the options of a timed hub. Test: `tandem-peer join
--hover N` (the mock's cursor on option N of every hub), the story verb
    `hover ID INDEX` (the mock host's). The host's test hub (`cmd story
hub`) is held until `story hub close` (the test reset drops it).
    Scenarios (the 2026-11-21 run, before the fix: no hub on either
    screen): `dialogue-marks-host` (the mock guest's cursor marked on our
    hub; `story-host` its suggestion's, without the notice),
    `dialogue-marks-guest` (the mock host's cursor marked on ours,
    following it).
  - _Scenes_: each game sends its V's scene tier (`StoryState`). A guest
    within 30 m of the host in a tier 3+ scene has its weapons lowered, tier
    4+ also invulnerable; a guest further than 100 m from a tier 4+ scene
    gets an offer to join ("NAME started a scene", "Join NAME" on Choice1),
    once per scene, not across a story gate (`GateBetween`) nor while
    watching. Taking it is the regroup action (`TandemTravel.RegroupTo`,
    `travel.md`: its loading screen or hop, our own fast travel locks, the
    host told "NAME is on the way"); when our V cannot go the offer stays
    (scenario `story-join`, `cmd story join`; not yet run in game).
  - _Fast travel blocked_ (2026-11-21, not yet run in game): a guest's own
    quest locks say nothing of the host's mission (its story is frozen), so
    the host says it: `kStoryNoFastTravel` (128, the last free bit of
    `StoryState`'s flags; protocol 12) while its own game would not let its
    V fast travel (a `BlockFastTravel` status effect, or a fast travel lock
    other than combat's, a car's or the police's: `TandemRevive.TravelLock`),
    re-sent when it changes (`SendFlags`, twice a second). The host's word
    only (referee: `authority.host-only`). A guest then never respawns far
    away (`revive.md`, "No far respawn out of a locked quest area");
    `tandem-peer host --no-fast-travel` (or the story verb
    `fasttravel off|on`) sends it.
  - _Restrictions_ (`scripts/tandem/Restrictions.reds`, `core/src/story.cpp`
    `InMoment` and `Restrictions`, engine facts §13.45). The quest runs on
    the host, so the restrictions its story puts on the host's V (no
    weapons, no sprint, no jump, forced walk, no vehicle, no quickhacks)
    would hold the host alone, and a guest could draw and fire beside the
    host's scene, kill a quest NPC or break the scene. Now a guest that
    shares the host's story moment and does not watch it (the rule: it
    watches only while it cannot take part from its own V, spectating
    below) wears the same ones on its own V; a spectator wears none of
    them, only the spectating holds.
    - _The host reports_ four times a second the gameplay restrictions on
      its V that a guest beside it shares: its status effects tagged
      `GameplayRestriction`, but not what holds the host's own body or
      camera (NoMovement, NoCameraControl, ForceCrouch, camera animations),
      its menus and fast travel, nor what its own activity puts on (a body
      carried, a call, a braindance, a time skip, focus mode, driving in
      combat, a locker); and NoCombat when its hands are forced empty by
      its scene or quest (the upper body state machine's ForceEmptyHands,
      not a safe zone's). As `StoryRestrictions` (the whole set, sorted, on
      every change; a story message: kept for late joiners, in the story
      digest, an explicit "none" in a story resync). The records are
      logged, not their sources: Codeware's `sourcesData` crashes the
      script VM when read (`engine/story.md`).
    - _Who shares the moment_ (`story::InMoment`): the host's V is in a
      scene (tier 2 and up, **private scenes included**: a guest inside
      one's area could break it too) or has story restrictions on; the
      guest is grouped with the host (`TandemStorySystem.Grouped()`: not
      apart; the regions' leader rule later), neither V is in another world,
      and it stands within 40 m of the host's V (the earshot of the scene's
      voices; 50 m to leave, so the edge does not flicker) or inside the
      staged scene when scene replay says where (its actors' spread, at most
      30 m, plus 12 m; 20 m to leave). A straggler outside the moment
      firing at a quest NPC is the combat path's, as always: the
      restriction never reaches beyond the moment.
    - _The guest wears them_ by the same records
      (`StatusEffectHelper.ApplyStatusEffect`, one stack), so its game
      holsters it and blocks its wheel the game's way (NoCombat: the upper
      body state machine's ForceEmptyHands, §13.45). It takes them off when
      it walks out, when the host lifts them, when the session ends or the
      host vanishes (`GuestLeave`), one stack each. **Only what tandem put
      on comes off**: an effect its V already had (its own world's, its
      own quest's) is "theirs" and never applied nor removed; one its own
      game applies again while ours is on (a refresh, not a second stack)
      becomes theirs; one its game takes off is put on again while the
      moment wants it. The scene holds above (weapons lowered at tier 3+,
      invulnerable at 4+, within 30 m) go through the same ledger, so the
      two never undo each other.
    - _A notice_, once per moment, when a restriction first comes on:
      "NAME's scene: weapons away." (or "take it slow." for the rest),
      plain strings in `TandemRestrictText` until the localization table
      (`Text.reds`, branch `polish`) is here.
    - _The host's backstop_: while its V is in a scene or restricted, the
      host refuses a guest's strike, takedown or quickhack on one of the
      scene's actors (scene replay's cast, or an NPC in a scene that is a
      quest NPC or kept alive by god mode), logging `story: refused a guest's hit
during the host's scene` (strikes, takedowns) or `story: refused a
guest's quickhack during the host's scene` (player, NPC, the
      quickhack's record), each at most once a second per kind of act. A
      quickhack is refused whole: its upload (`NpcHack`: the host answers
      it failed, `NpcHackState` step 2, as for an NPC out of reach, and
      never holds the NPC "being hacked"), every effect it lands
      (`NpcEffect` tagged `Quickhack`) and its damage ticks (strikes). A
      guest's Breach Protocol, won or lost, leaves the actor alone too (its
      daemons, its alert; logged as a quickhack, `breach`; `Breach.reds`
      `OnNpc`): the network stays breached. The
      rule is core's (`story::Backstop`, `IsSceneActor`; native
      `TandemStory.BackstopRefuses`), tested in
      `tests/restrictions_test.cpp`; `Restrictions.reds` reads what the NPC
      is.
    - Static safe zones are not shared: they are a quest fact of the
      guest's own world (`CityAreaType`), which its own open-world phases
      set (to verify with the freeze on).
    - Tests: `tests/restrictions_test.cpp` (the moment, the ledger, the
      record, the endpoints: late joiner, lift, a lost lift, a vanished
      host; the backstop: who is an actor, each act refused in the moment
      only, the log's rate per act); mock player `--restrict REC[,REC] --restrict-at S
--restrict-for S` and the story verb `restrict`; scenarios
      `story-restrict-guest`, `story-restrict-host`; `tools/game.sh cmd
story restrict` (report), `story restrict on|off REC` (a test
      restriction on our V).
  - _Spectating_ (`scripts/tandem/Spectate.reds`, `core/src/story.cpp`
    `MomentReasons`, `HostMoment`, `Spectator`, engine facts §13.96; built
    2026-11-21, nothing run in game; the audit of the base game's and
    Phantom Liberty's moments: [`scripted-moments.md`](scripted-moments.md)).
    **The last resort** (the user's rule, 2026-11-21): a guest in the host's
    moment plays it as itself and sees it from its own eyes (scene replay
    shows the actors, voices, subtitles and choices; the restrictions above
    keep it from breaking the scene; the ride along seats it in the host's
    quest vehicle, passenger combat included; its hits and uses go to their
    owner as always). It watches through the host's eyes, its own V
    parked, only while it cannot take part from its own body:
    (a) a cutscene (tier 5: a scene camera, no player control);
    (b) the host's V in another world (cyberspace, a braindance, Johnny,
    the Blackwall: `Elsewhere.reds`); (c) the host's V taken beyond its
    reach during the moment (a quest ride it could not ride along in, a
    quest teleport); (d) only the host's view shows it (the quest's camera
    animation, a moment the moment list marks watch); (e) the host's V among
    quest interiors the guest's game cannot load (the prefabs' layout check
    failed), moment or not. The reason gone, it plays again at once, the
    moment going on. Every switch happens behind a short black
    (`Curtain.reds`), at most one notice says why, and the guest ends beside
    the host with no prompt ([`scripted-moments.md`](scripted-moments.md),
    "The fallback", "Seamless").
    - _The host's moment_, read four times a second from its own V, no
      list of quests: a scene of tier 2 and up, quest restrictions on how
      it moves (`Restrictions.reds` `IsShared`, but not a weapons-only zone:
      a club's rule, nor the phone kept away alone: `PhoneOnly`, NoPhone and
      PhoneInterrupted, which a save can hold through hours of free roam
      and which are then not shared either), a ride its scene drives (`Transit.reds` kind Scene),
      a scripted body mode (GameplayRestriction effects NoMovement,
      NoCameraControl, ForceCrouch; not its own activities), a camera
      animation (the CameraAnimation tag), another world, or a
      moment the moment list marks watch (`moments.txt`, tandem.ini
      `moment_watch`: by the scene it plays, a quest phase that runs, its
      tracked journal path; `play` there keeps a cutscene and the camera
      animation played; [`scripted-moments.md`](scripted-moments.md), "The
      moment list"; `TandemStory.MomentListed`, four times a second). A
      fight, the player's own driving, a cab it called: none. A private
      scene: never (off at once). On, it holds 1.5 s after its reasons go
      (`story::kMomentHoldUs`: between a scene's sections, a loading
      screen). Said as `kStoryMoment` in the host's `StoryState` flags, and
      `kStoryWatch` (64) with it while a reason only its view shows holds
      (`story::kMomentWatch`: tier 5, another world, the camera animation,
      a moment marked watch, the test moment; held alike). Free bits: no new
      message, protocol 7.
      While the moment lasts the host's active camera (first person
      included: its position, orientation and field of view) goes out every
      frame as `SceneCamera`, numbered as the scene it announces, else as
      its last one (1 before any), paced by the scene budget
      (`Endpoint::SendSceneCamera`): (c) needs it in any moment. It goes out
      too while any guest says it spectates (`kStorySpectating`), moment or
      not: (e) is no moment of the host's (`TandemStory.SpectateViewWanted`).
    - _Who watches_ (`story::Spectator`): a guest shares the moment once it
      stands in it (grouped, its own V in this world, within 40 m:
      `InMoment`'s distance), until the moment ends. Then it watches while
      `kStoryWatch` holds, (b) when the host is elsewhere, else (a) at tier
      5, else (d); or (c) while it rides nothing (neither along with the
      host nor in a car of its own) and the host's V is more than 150 m
      away (`story::kSpectateReach`: the NPC links' reach; in reach again
      at 120 m, carried there by `Follow`; watching while the host rides, it
      stays (c) until that ride ends); the host riding, (c) already at 30 m
      (`story::kSpectateLeftBehind`: the moment's ride left without it).
      (c) needs a scene or a scripted ride: while the host's moment is
      restrictions alone (its tier under 2 and no ride its scene or quest
      drives, Transit kind 3: `SpectateInput::hostFree`), where its V
      walks or drives is its own free roam and nobody is made to watch
      (2026-11-22: a host driving off put its guest "in a scene"; the
      cases (c) was written for, a quest car ride and a scene, are tier 2
      or kind 3, and a watch already on lasts until that ride ends as
      before; a quest teleport under a restriction alone, no scene, is no
      longer watched: the guest stays where it is and regroups as after
      any travel). Scenario `spectate-freeroam`. Or
      (e), moment or not, while the host's quests have prefabs on that its
      game cannot load and the host is within 150 m (170 m to stop). Each
      start logs its letter and why
      (`spectate: watching … our V parked: (a) a cutscene…`), a change of
      reason `spectate: still watching, now …`, the end `our own V again
(the reason is over): …`. "Stop watching" (`Choice1`) declines until
      that reason ends; watching also stops when the host's scene turns
      private, the guest goes apart, the host vanishes or the session ends
      (`GuestLeave`). `spectate = off` (`tandem.ini`) keeps a guest's own V
      in every moment.
    - _Seamless_ (the user's rule, 2026-11-21): the switch in is a black
      of our own on the HUD layer (`TandemCurtain`: 0.25 s down), then the
      host's view as a cut (`Activate(0.0, true)`), then the black lifts
      (0.35 s); the switch out the same, our V put back under it. A reason
      that returns before the black is down keeps the host's view. The
      curtain never stays black more than 2.5 s (`curtain: black too long,
lifted`). One notice per moment: none for (a) and (b), `Spectate-Taken`
      for (c), `Spectate-Watching` for (d), `Spectate-Missing` for (e) (in
      place of `Prefabs-Missing`); the "Stop watching" hint as before.
    - _What watching does_: the host's view on a camera of the guest's own
      (the watch camera's: `simple_free_camera.ent`, drawn from the same
      `scene::CameraTrack`, any play), activated with the audio listener
      (engine facts §13.96: else the listener stays the V's). First person
      (the view within 1 m of the host's puppet, 1.5 m to leave): only the
      puppet's head meshes and head, eyes and face items are hidden, as V's
      own first person has no head; its arms, hands and weapon stay in the
      picture. Its own V: unseen by the NPCs (`PlayerPuppet.SetInvisible`,
      only if it was not already), out of the picture (the animation system's fade),
      held by the spectating holds through the restrictions' ledger
      (NoMovement, NoCameraControl, NoCombat, Invulnerable), its scene
      watch offer, join offer and ride offer not shown. It says
      `kStorySpectating` in its own `StoryState`: every other game hides
      its puppet (`TandemVisibility` reason `spectate`) and turns its
      puppet's visible object component off (their NPCs do not see it); the
      party HUD shows it "in a scene". Its V is **carried along** with the
      host's, four times a second (`Follow`), so its game streams the world,
      links with the host's NPCs (`regions.md`) and hears around the
      picture: in the host's vehicle, mounted to the copy's trunk body slot
      (the ride along's carry, `Transit.reds`; placed in the cabin each
      check if the copy has none); on foot, put on navigation ground 3 m
      short of the host's V on its side when more than 15 m away (once a
      second at most). Only once the picture is the host's: our own eyes
      never see our V moved. Never across a story gate between the two Vs
      (`TandemStoryWorld.GateBetween`: a carry already mounted rides on, as
      a seat would). Already seated or riding along in the host's ride:
      not moved. Back: everything undone, the V where it was carried (off
      the copy: beside it, on its right); only when the reason ended with
      the host still more than 60 m away (the carry fell short: no ground,
      a loading screen) does the guest join it (`TandemTravel.RegroupTo`,
      behind a loading screen; not across a gate either).
    - Tests: `tests/restrictions_test.cpp` (the reasons, what is to watch,
      the holds; who watches: played in a tier 2 scene and a ride along,
      each of (a) to (e), back at once when it ends, a moment not shared,
      private, apart, left, lifted; a ride that leaves without the guest;
      the list's verdicts in the reasons), `tests/moments_test.cpp` (keys,
      lines, which entry decides, the shipped `moments.txt`),
      `tests/referee_test.cpp` (a moment and
      a moment to watch are the host's word, spectating a guest's),
      `tests/presence_test.cpp`; mock player story verb `moment on [watch]
[TIER]|off` (with `camera on`, whose views go out in a scene, a moment
      or neither, as a watched host's); scenarios `spectate-guest` (a tier 2
      walk and talk played, a tier 5 cutscene and a dive watched),
      `spectate-leave`, `spectate-host`, `spectate-ride` (mock host
      `--mode drive --drive-radius 35`: a moment's ride leaving the guest),
      `spectate-missing`; `tools/game.sh cmd story spectate
report` (state: the host's reasons and whether to watch, whether we
      share the moment, why we watch), `story spectate on|off` (host: a test
      moment, one to watch; guest: watching allowed, or off for the
      session), `story spectate parts` (the host's puppet's meshes, the head
      ones marked), `story spectate missing on|off` (guest: as if its game
      could not load the host's prefabs), `story moments` (host: its scene,
      running phases, tracked path and the list's verdict), `story moments
test watch|play|off`.
  - _Johnny, Songbird, takeovers_ (`research/coop-quests-design.md` §2.11):
    Johnny and Songbird are NPCs of the host's world, so a guest near the
    host sees them as proxies where the host does. Johnny's relic glitches
    and Songbird's glitch effects on the host's V (`StoryEffect`) go onto a
    guest's V within 30 m while they last. Songbird's calls keep their own
    widget. The host playing as Johnny (`isReplacer`) or in a braindance
    counts as a cinematic for the holds and the offer, with a notice. The
    host's puppet does not yet take Johnny's look.
  - _Private scenes_ (`engine/scenes.cpp`, design §2.12): each game
    classifies the scene it plays from the scene resource's category tag
    and actors (chats, open-world and minor one-to-one scenes, a romance
    partner alone: private). In a private scene, the others' puppets near
    that player fade out. The others see the player "busy", standing where
    the scene began and idling on its own animation graph (no pose sent),
    with no hold or offer. `scene_privacy = on` by default; it installs the
    quest node hook in observe mode on every game.
  - _Rewards_: the host's `OnRewardEvent` goes to the guests that took part
    (within 150 m of the host as the quest moved or completed;
    `story::Participation`); each guest gives its own V the reward
    (`RPGManager.GiveReward`) once, and keeps it in a persistent ledger so
    its own story pays nothing again for it.
  - _Scene replay_ (the scene's actors, voices and Johnny for a guest; `scene_replay = on`): [`scene-replay.md`](scene-replay.md).
  - _Whose phases wait_ (`quest-carryover.md`, "Whose gig is it"): the
    freeze holds the phases this game does not own. Story phases are the
    host's for the whole session. A gig's phase (one under a resource of
    `quest_freeze_gigs`, until spike C2 gives a better test) waits while
    the guest is grouped with the host and runs while it is apart, when
    the gig signals held meanwhile are given back. **Always grouped
    today** (`TandemStorySystem.SetApart` is for the regions work; the
    test commands `quest apart` / `quest grouped` flip it), so nothing
    changes yet. Apart, the journal mirror leaves the guest's own gig
    entries alone and does not show the host's (going apart gives them
    back as they were, checked as on leaving; grouped again, the host's
    come back as last heard and every own gig entry is hidden again: the
    log counts those given back, hidden again, new while apart and
    cleared meanwhile by the guest's game, which `story-gigs-apart`
    checks). Apart, one phone, one call: the host's call waits while the
    guest's own is up, and the guest's own waits (retried every 5 s)
    while the host's is on its phone. Grouped, as before.
    `tools/game.sh cmd quest trace on` logs every phase met (its chain of
    resources) and every journal node in it: the spike's tool.
  - _Session items_ (decided 2026-11-19): the host's quest props (tag
    `Quest`, keycards) that come into a guest's inventory are noted (a
    persistent list) and removed at the top of its leave, whether it chose
    to go or the host vanished, or at the next load of a save that holds
    the list. Iconics and rewards stay.
  - _Quest interactions_ (`scripts/tandem/Interact.reds`,
    `core/include/tandem/interact.hpp`, `plugin/src/engine/casts.cpp`;
    engine facts §13.65; built 2026-11-19, nothing run in game). The quest
    runs on the host and a grouped guest's own story waits, so a guest's V
    using an object of the host's world (a quest switch, an objective item,
    a quest container, the terminal an objective needs, a personal link)
    changed its own copy only, and the host's quest did not move. Now the
    guest asks and the host's game decides. **No list of quests or objects:
    one rule, read from each game's own data at run time.**
    - _Whose object it is_: the existing rules. Grouped
      (`TandemStorySystem.Grouped()`; the regions' leader rule when it
      merges), an entity placed in the world (`EntityID.IsStatic`: the same
      id in every game, as devices are named) is the host's. Anything else
      is the guest's own: its spawned things, and **everything while apart**
      (its own gigs, `quest-carryover.md` decision 6): nothing is asked, its
      game does it as solo. Characters are not asked about at all: a talk
      is a scene's choice (the host's hub, `Dialogue.reds`, the host picks),
      a takedown the combat path's.
    - _The question_ (`QuestInteract`, appended, a story message; guest to
      host, the answer to that guest alone, never relayed): the object's
      static id, the choice (a device action's class and its action name;
      for loot the loot action and the item's record), the interaction
      layer, the guest's V's position and the time (host clock). Caught at
      the two places every interaction of this kind goes through:
      `Device.ExecuteAction(action, executor)` (prompts and screens alike)
      and `Inventory.OnInteractionUsed` (an item taken from a placed
      container or lying in the world: asked only for a quest prop, tag
      `Quest` or a keycard; other loot is per player).
    - _What the guest's own copy does meanwhile_: it does it at once, for
      feel (the press, the pick-up), because every local effect on the
      host's objects is mirrored state the host's word overrides (devices),
      per player (loot), or deduplicated (quest props, `loot.md`). A
      device's change is not said meanwhile (`TandemDevices.Hold`): if the
      host performs it, the host's V's doing is the device's state (a toggle
      is never applied twice); if not, it is asked of the host the usual
      way. **The one exception: a choice that casts the doer** (the guest's
      own game runs the same check as the host's, below): not run here, the
      host is asked first.
    - _The host's verdict_ (`Interact.reds` `Judge`, in this order):
      missing (its game has no such object loaded, or no such choice for
      its V now) → too far (the reach check, below) → **local** when its
      copy is nothing of its quest (`GameObject.IsQuest()`, the game's own
      quest mark a quest sets on the object; for loot, the item is not a
      quest prop): the guest's copy is the whole of it → **host only** when
      the object is in a scene now (`SceneSystemInterface.IsEntityInScene`)
      or the action casts its doer (`TandemStory.ActionCasts`, below): the
      guest's prompt shows greyed with "NAME must do this" and it watches
      the host do it through scene replay → busy when the host's V is in a
      workspot or a lore animation → **performed**: its own V does the same
      action on its own copy with the game's own interaction code
      (`Device.ExecuteAction` with the action found among the device's own
      actions for its V by class and name; loot: `TransactionSystem.TakeItem`
      or `TransferItem` of the quest prop into its V), so quests, facts and
      loot move exactly as if its V had done it. The device record names the
      guest as the doer (`TandemDevices.PerformedFor`: `DeviceState.by`).
      What it did comes back through the existing sync: device state, the
      quest prop shared (`loot.md`), the journal.
    - _Casting the doer, read from the game's data_ (`engine/casts.cpp`,
      §13.65): a device action reaches the device's entity as an event, and
      the entity's own callback for that event's class is where the game
      puts the doer in a workspot. The plugin finds those callbacks by the
      class's event listeners (and by their parameter type) on the entity's
      class chain and its state's, follows the functions they call (5
      levels, 400 functions at most) through the compiled bytecode the game
      holds, and answers yes when one of them calls the workspot system's
      `PlayInDevice` / `PlayInDeviceSimple` or the device's `EnterWorkspot`
      / `EnterWorkspotWithIK`: a personal-link jack-in, a computer's
      engineering spot, a turret taken apart, a door forced with V's own
      hands. Cached per (entity class, action class). Unknown (the layout is
      not as measured, a class not found) counts as casting: the host does
      it, and nothing of the host's V is moved by a guess.
    - _Known for the session_ (`interact::Known`, on both sides): the
      verdicts that say what a choice is (performed, host only, local), per
      (object, choice kind). A known host-only or local choice is answered
      at once by the guest's own endpoint (never asked again), and by the
      host's without its game for another guest; a performed one is done by
      the host's game each time (a switch flips twice). A known host-only
      device prompt is greyed before it is pressed
      (`ScriptableDeviceComponentPS.FinalizeGetActions`:
      `SetInactiveWithReason`). The rest (taken, too far, missing, busy) is
      of the moment and asked again.
    - _The safety valve_: `tandem.ini` `quest_interact_hostonly` and
      `quest_interact_allow`, TweakDB records of objects (host only always;
      the host always does it, even unmarked or casting). **Empty by
      default, and they should stay empty**: an entry means the generic
      rule missed something, which is a bug to report.
      `quest_interact = off` turns the whole path off (a guest's use stays
      in its own world).
    - _Edge cases, decided_:
      - two players at one object: **first come, first served**, as the
        devices' Ledger (`interact::Referee`): a question is the object's
        while the host's game has it, and a performed use holds it 2 s; the
        second player is told "someone else got there first", and its copy
        is put right by the device record;
      - a guest vanishing mid-request: its questions and holds go with it
        (`Endpoint::RemovePlayer`); an answer the host's game gives later is
        sent nowhere and nothing is learnt from it; what the host's V
        already did stays done (the world's, as for a device);
      - the host vanishing, or its game silent: a question with no answer in
        3 s is the guest's own, as solo (a held casting choice is then run on
        its own copy; a held device change is asked of the host the usual
        way); the session's end settles every question the same way;
      - too far: the guest's word on where its V stood is believed within
        2 m of the host's own track of that V (its states, rewound to the
        moment it chose: the time it says, bounded by netcode's 400 ms
        rewind), else the track is used; the object must be within 4 m of it
        (`interact::CheckReach`, the lag compensation of `lagcomp.hpp`);
      - **an interaction that needs an item or a skill the guest has and the
        host's V lacks**: the guest is the doer, so its own game's checks
        count: its prompt was not "check failed" (the flag
        `kInteractChecked`), and the host's V does the action without being
        checked again (the action is executed directly, not through the
        choice's check). The item itself is the session's anyway: the quest
        props a guest holds came from the host's world and are on the host's
        V too (`loot.md`); a quest prop from the guest's own story could open
        a host's quest object early, which is accepted (the same key in two
        worlds is the same quest's key).
    - Tests: `tests/interact_test.cpp` (the known verdicts, first come, the
      reach, the message; over loopback: performed each time, host only and
      local known for the session, two guests at one object, a guest
      vanishing mid-request, a guest never answers nor hears another's),
      `tests/protocol_test.cpp`, `tests/referee_test.cpp` (a guest asks, the
      host answers that guest alone), `tests/peer_interact.sh`; mock player
      `--interact ENTITY:CHOICE[:DETAIL][@S]` (a guest's use) and
      `--interact-answer performed|host-only|local|missing|busy|too-far|none`
      (a host's verdict; local by default: it has no world); scenarios
      `interact-guest-pickup`, `interact-guest-hostonly`,
      `interact-apart-own-gig`; `tools/game.sh cmd story interact` (counts),
      `story interact look [device]`, `story interact use [device]`.
  - _Quest prefabs_ (`scripts/tandem/StoryWorld.reds`,
    `plugin/src/engine/prefabs.cpp`, `core/include/tandem/prefabs.hpp`;
    engine facts §13.111; built 2026-11-21, nothing run in game). The
    interiors and props a quest phase streams in (The Pickup's All Foods
    plant, The Heist's Konpeki Plaza) exist only in a game whose quest runs
    that phase: a guest's own quests never do, so the host walked into
    geometry the guest's world lacked.
    - _The host_ reads once a second which prefabs its quests have on (the
      quests system's prefab handler: mode ForceLoad) and sends the set
      when it changes, as `WorldPrefabs` (appended, protocol 10; their world node refs,
      sorted, 256 at most; a story message: kept for late joiners, in the
      story digest, an explicit "none" in a story resync). Its own copy is
      never changed.
    - _A guest grouped with the host_ (`TandemStorySystem.Grouped()`) turns
      the same ones on in its own world, the engine's way (the handler's
      `SetMode`, under its lock, as a phase does), and remembers which it
      turned on (`prefabs::Mirror`). It turns off only those: when the
      host's set drops one, when it goes apart, when the session ends or the
      host vanishes (`GuestLeave`, before its journal goes back; "Saves").
      One already on (its own quest's) is never touched; one its own quest
      asked for meanwhile is left on and no longer ours; one its own quest
      turned off while the host still has it is put on again. The devices
      inside are placed ones (static ids): the host's word reaches them as
      they stream in and the leave puts them back (`devices.md`); its NPCs
      are the host's quest spawns, shown as proxies.
    - _The layout check_ (once: RTTI sizes of the quests system, the
      handler, its entry and the streaming system; the classes of the
      objects at the measured places): on a mismatch nothing is read or
      set, the log says why, and a guest near the host (150 m) while the
      host's quests have prefabs on is told once per set that its game
      cannot load them ("follow them, or watch through their eyes when the
      story allows": `Prefabs-Missing`).
    - `tandem.ini` `quest_prefabs = off`: a guest loads none (the host
      sends them anyway).
    - Tests: `tests/prefabs_test.cpp` (the mirror, the record, both
      messages over loopback), `tests/protocol_test.cpp`,
      `tests/referee_test.cpp` (the host's world only); mock player
      `--prefabs REF[,REF]` and the story verb `prefabs`; scenarios
      `prefabs-guest`, `prefabs-host`; `tools/game.sh cmd story prefabs`.
  - _Story gates_ (`StoryWorld.reds`, `core/include/tandem/prefabs.hpp`
    `gates::`; engine facts §13.112; built 2026-11-21, not run in game). A
    guest earlier in the story than the host is kept where the host walks:
    the Watson lockdown (Act 1 and Playing for Time: fact `q101_done`) and
    Dogtown's gate (fact `ep1_side_content`). Each game sends its own gates
    and which side its V is on (its district: in Watson, in Dogtown) as
    `StoryGates` (appended, protocol 10; on every change; the host stamps a guest's,
    passes it on and keeps every player's for late joiners).
    - _At the join_ (`gates::Behind`), both players are told in words what
      the host's story has open and the guest's keeps closed, the lockdown
      first: the guest "… has lifted the Watson lockdown, yours has not …
      ride in NAME's car", the host "… is still in the Watson lockdown …
      pick them up in your car" (`Gates-WatsonBehind`,
      `Gates-WatsonBehindHost`, `Gates-DogtownBehind`,
      `Gates-DogtownBehindHost`).
    - _In play_ (`gates::Watcher`): a guest grouped with the host, on the
      closed side (in Watson under its lockdown; outside Dogtown with its
      gate shut) while the host is past it and within 250 m (it just
      crossed where the guest stands), is told once per crossing to ride in
      the host's car (`Gates-WatsonMet`, `Gates-DogtownMet`): the guest's
      seat in the host's copy carries it across, as any ride does. Never a
      teleport through. Spectating across a gate is not offered: the host
      sends its view only in a scripted moment, and the spectator's carry
      would leave its V past the barrier.
    - Tests: `tests/prefabs_test.cpp`; mock player `--gates N`; scenario
      `gates-guest`; `tools/game.sh cmd story gates`.
  - Not yet verified in game: scenarios `story-guest`, `story-join`, `story-host`, `story-johnny`,
    `story-private`, `story-private-host`, `story-private-calls`, `story-session-items`,
    `story-gigs-apart`, `story-restrict-guest`, `story-restrict-host`,
    `spectate-guest`, `spectate-leave`, `spectate-host`, `spectate-ride`,
    `spectate-missing`, `interact-guest-pickup`, `interact-guest-hostonly`, `interact-apart-own-gig`,
    `prefabs-guest`, `prefabs-host`, `gates-guest`, `dialogue-marks-host`,
    `dialogue-marks-guest`.

## Open problems

- **What a guest brings home** (proposal 2026-11-19): `quest-carryover.md`.
  The ledger of what a guest did and earned (`core/src/carryover.cpp`,
  tested) is written; its natives and script, the removal of quest props
  on leave and the join notes are staged there.

- **Scenes for guests.** Replaying the host's quest-scene nodes in a guest's
  game (the campaign fork's approach) is unproven: nodes run outside their
  phase. Alternatives to study: guests follow the scene as spectators of the
  host's camera; or run the same phase on guests with quest progression
  locked to the host.
- **Story decisions** (2026-11-18; design in `research/coop-quests-design.md`):
  the host's game runs the story; a guest's own story is frozen for the
  session (not overwritten), its saves locked, and restored as it was when
  it leaves (the leave steps, `progression.md`). Decided by the user: in dialogues with
  choices **the host picks and guests suggest** (the same native choice hub
  on every screen, each with its own cursor, the host sees suggestions);
  **holocalls and texts reach everyone in sync and anyone can answer or
  reply for all, first wins**; a guest far away when the host starts a
  story scene **is offered to join** (a native-looking prompt to regroup:
  `TandemTravel.OfferRegroup(game, guest, 2, title)`, milestone 4's;
  declining leaves it the subtitles and journal); a guest earns a quest's
  rewards when it was near the host (150 m) as the quest completed or
  advanced, **only once**: a quest it was rewarded for in co-op pays
  nothing again when it later plays it in its own save. Decided
  2026-11-19 (`quest-carryover.md`): **a gig belongs to the world of the
  player who starts it** (grouped at the start: the host's, no prompt);
  **a helper's own copy of a gig stays playable at home and pays nothing
  again**; **a guest gives the host's quest props back when it leaves**
  (items tagged `Quest`, keycards; iconics and rewards stay);
  **Phantom Liberty is required for every player**. Decided 2026-11-19
  (the design's proposals accepted): romance and intimate scenes are never
  replayed for guests; a guest that leaves stays where it is
  (`leave = stay`, `progression.md`); subtitles and choices reach guests
  in the host's language (`strings.md`); shards and the codex stay per player; Johnny's
  appearances beside the host are shown to guests later; after the first
  version: scene voice, then quest-only interiors, then solo gigs.
- **Quests together, gigs apart.** A quest or gig finished together rewards
  both: each game gives its own V the rewards (XP, street cred, money,
  items, the fixer's message) by the game's own rules, the guest's game
  told by the host when it completes with them taking part. Players may
  also split up and do gigs on their own (fixer jobs, side gigs, NCPD
  hustles): the game of the player doing it runs that gig's quest and NPCs
  in its region (Region limits) and reports the outcome (gig done, NPCs
  dead, items taken) to the host's record of the world, so it stays one
  world. Main story quests stay the host's, played together. Solo gigs come
  after the first story version if they prove too costly; design in
  `research/coop-quests-design.md`.
