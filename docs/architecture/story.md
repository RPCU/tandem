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
  - Not yet verified in game: scenarios `story-guest`, `story-host`, `story-johnny`,
    `story-private`, `story-private-host`.

## Open problems

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
  nothing again when it later plays it in its own save. Provisional (the
  design's proposals, to confirm): romance and intimate scenes are never
  replayed for guests; leaving reloads the guest's game; subtitles in the
  host's language for now; shards and the codex stay per player; Johnny's
  appearances beside the host are shown to guests later; after the first
  version: scene voice, then quest-only interiors, then solo gigs.
- **Quests together, gigs apart.** A quest or gig the players finish
  together rewards both: each game gives its own V the rewards (XP,
  street cred, money, items, the fixer's message) by the game's own
  rules, the guest's game told by the host when it completes with them
  taking part. Players may also split up and do gigs on their own (fixer
  jobs, side gigs, NCPD hustles) while the other does something else: the
  game of the player doing it runs that gig's quest and NPCs in its region
  (Region limits) and reports the outcome (gig done, NPCs dead, items
  taken) to the host's record of the world, so it stays one world. Main
  story quests stay the host's, played together. Solo gigs come after the
  first story version if they prove too costly; design in
  `research/coop-quests-design.md`.
