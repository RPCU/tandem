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
    surroundings (`research/coop-regions.md`). **Off by default**
    (`quest_freeze = on` to use it) until checked in game; without it a
    guest gets calls, lines, hubs and rewards but not the journal or texts.
  - _Saves_: a guest in a session never saves (`TandemStorySystem` overrides
    `IsSavingLocked`, plus a `SaveLocksManager` lock and dropped autosaves).
    Every journal entry the mirror changes keeps its state from before; on
    leaving (the guest quits or the host is gone) the journal and tracked
    entry go back, are checked, the freeze ends and saving unlocks. A
    mismatch keeps saving locked and says "load your last save". The
    design's final step (write one "back from co-op" save and reload it) is
    a logged TODO: the guest goes on in place.
  - _Journal, tracker, markers_: the host's journal (snapshot at join,
    changes with their notify flag, so the guest gets the same toasts and
    sounds) and tracked quest; quest pins follow the journal. Shown only on
    a frozen guest. Entity-bound pins and objective counters: step 2.
  - _Calls and texts_ (`Phone.reds`): the host's `PhoneSystem` phases go to
    every phone; a guest presents them through its own `PhoneSystem`
    (ringtone, incoming widget, audio call with the caller's portrait) with
    its phone facts held back. Anyone answers: the first answer wins
    (`story::CallArbiter` on the host's endpoint) and picks up the host's
    phone. Texts come through the journal; a guest's reply goes to the host,
    which applies it if the option is still open (first reply wins).
  - _Dialogue_ (`Dialogue.reds`): the host's subtitles and its active choice
    hub reach guests within 25 m (holocall lines and phone hubs: everyone),
    drawn by the guest's own subtitle and hub widgets (a stand-in hub). A
    guest moves its own cursor and can suggest; the host sees the
    suggestion; the host's pick closes every hub. No story voice yet.
  - _Scenes_: each game sends its V's scene tier (`StoryState`). A guest
    within 30 m of the host in a tier 3+ scene has its weapons lowered, tier
    4+ also invulnerable; a guest further than 100 m from a tier 4+ scene
    gets an offer to join (the regroup itself: TODO).
  - _Restrictions_ (`scripts/tandem/Restrictions.reds`, `core/src/story.cpp`
    `InMoment` and `Restrictions`, engine facts §13.45). The quest runs on
    the host, so the restrictions its story puts on the host's V (no
    weapons, no sprint, no jump, forced walk, no vehicle, no quickhacks)
    would hold the host alone, and a guest could draw and fire beside the
    host's scene, kill a quest NPC or break the scene. Now a guest that
    shares the host's story moment wears the same ones on its own V.
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
      digest, an explicit "none" in a story resync). Each record's sources
      (Codeware's `sourcesData`) are logged.
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
      host refuses a guest's strike or takedown on one of the scene's
      actors (scene replay's cast, or an NPC in a scene that is a quest NPC
      or kept alive by god mode), logging `story: refused a guest's hit
during the host's scene`.
    - Static safe zones are not shared: they are a quest fact of the
      guest's own world (`CityAreaType`), which its own open-world phases
      set (to verify with the freeze on).
    - Tests: `tests/restrictions_test.cpp` (the moment, the ledger, the
      record, the endpoints: late joiner, lift, a lost lift, a vanished
      host); mock player `--restrict REC[,REC] --restrict-at S
--restrict-for S` and the story verb `restrict`; scenarios
      `story-restrict-guest`, `story-restrict-host`; `tools/game.sh cmd
story restrict` (report), `story restrict on|off REC` (a test
      restriction on our V).
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
    back as they were; grouped again, the host's come back as last
    heard). Apart, one phone, one call: the host's call waits while the
    guest's own is up, and the guest's own waits (retried every 5 s)
    while the host's is on its phone. Grouped, as before.
    `tools/game.sh cmd quest trace on` logs every phase met (its chain of
    resources) and every journal node in it: the spike's tool.
  - _Session items_ (decided 2026-11-19): the host's quest props (tag
    `Quest`, keycards) that come into a guest's inventory are noted (a
    persistent list) and removed at the top of its leave, whether it chose
    to go or the host vanished, or at the next load of a save that holds
    the list. Iconics and rewards stay.
  - Not yet verified in game: scenarios `story-guest`, `story-host`, `story-johnny`,
    `story-private`, `story-private-host`, `story-session-items`,
    `story-gigs-apart`, `story-restrict-guest`, `story-restrict-host`.

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
  it leaves (one save and reload). Decided by the user: in dialogues with
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
  **Phantom Liberty is required for every player**. Provisional (the
  design's proposals, to confirm): romance and intimate scenes are never
  replayed for guests; leaving reloads the guest's game; subtitles in the
  host's language for now; shards and the codex stay per player; Johnny's
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
