# Other worlds

## Inside the plugin

- **Other worlds** (`scripts/tandem/Elsewhere.reds`, `core/src/elsewhere.cpp`,
  `plugin/src/elsewhere_natives.cpp`; engine-knowledge §13.18): a V in
  cyberspace, a braindance, playing as Johnny (a takeover in the city, a
  flashback) or as someone else (another replacer), any player, host or
  guest. Its own game reads which world it is in four times a second (the
  `Braindance` blackboard; a status effect tagged `Cyberspace` or
  `CyberspacePresence`; a `*Cyberspace` district on `UI_Map`; Johnny's or
  another replacer record) and sends it as `Elsewhere` (reliable, kept by
  the host for late joiners, back to the city when a player leaves or comes
  back from a crash). Every state it sends goes through a tracker
  (`elsewhere::Tracker`): a jump of more than 50 m outside a vehicle is a
  teleport, and when a world comes with it (or within 4 s) the scene has
  taken V away from its body; cyberspace and braindances always do, jump or
  not (V's entity is their camera: a braindance flies it). The body stays
  where V was just before: that game then sends the body's place as its
  `PlayerState` (no velocity), holds the body's pose as it was then (kept
  5 s back, sent again 10 times a second), says whether it sat or lay (a
  workspot: a netrunner chair, a braindance rig, a bed) and shares no NPCs
  (they are the other world's). A teleport with no world for 3 s was one in
  the city: the body goes with V. Back from the world, the body follows V
  again once V is near it or jumps back (3 s at most). What the others do:
  - _A body left sitting or lying_ stays there as it was, jacked in;
    _one moved by the scene_ is hidden (`IVisualComponent.TemporaryHide` on
    the puppet and what it wears and holds, after the animation system's
    fade; `PuppetVisibility.reds` keeps one set of reasons per puppet, so a
    private scene and another world never show what the other hid) with
    its marker off. A takeover in the city keeps the puppet. A notice says
    it the game's way ("… is in cyberspace", "… is living one of Johnny's
    memories", "… is back"); a hidden puppet shows again 0.75 s after its
    player is back, once it has moved there.
  - _Our own V elsewhere_ (cyberspace, a braindance, or away from its body)
    sees no puppet and no marker of the others; they come back with it.
  - _The story_: a host away from its body, or our V in another world,
    holds nobody and offers no join (there is no scene here to join); the
    guest keeps playing. NPCs never hit a player in another world (the host
    does not send the hit); a downed V whose body is out of sight cannot
    be revived until it is back (one left sitting or lying can), and a V
    in another world revives nobody. A puppet held by a takedown or a
    workspot (`HoldPuppet`) stays where its holder puts it: the body's
    place is only ever a `PlayerState`, which a held puppet ignores.
  - Nothing stays hidden: a player leaving (or vanishing) takes its state
    away, the end of the session clears everyone's, a new game starts in
    its body, and the host's players digest covers everyone's place
    (`consistency.md`).
  - Not yet verified in game: scenarios `story-worlds` (a scripted host's
    dives, as a guest), `elsewhere-dive` (our V's, as host, with
    `tools/game.sh cmd elsewhere test PLACE|off` and a teleport). Open: the
    signals of the Phantom Liberty Blackwall and Songbird sequences, and of
    Mikoshi (§13.18), the guest following a host's dive (built: it
    watches through the host's camera, `story.md`, "Spectating", reason b), Johnny's look on the puppet, a fade on the
    puppet instead of a cut.
