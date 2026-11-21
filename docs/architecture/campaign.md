# A shared campaign

**Status: 2026-11-21, built, not yet run in game** (`campaign = off` by
default, `tandem.ini`). A group shares one campaign save. Whoever hosts it
loads it and plays its V, the main V; the others join with their own V as
guests, as in any session; anyone may also play it solo. Saves cannot be
merged, so tandem keeps each copy's lineage, as git keeps commits, and
tells the players which copy is the newest, or that two went on
separately. This step moves the save between machines by hand (Export and
Import campaign); a transfer through the session is the next one (Seam,
below).

Code: `core/include/tandem/campaign.hpp` (the rules, the records, the
archive; tested in `tests/campaign_test.cpp`), `core/src/endpoint_campaign.cpp`
(the copies through the session), `plugin/src/campaign.hpp`, `campaign.cpp`
(the copy played, the folder, the worker) and `campaign_natives.cpp` ↔
`scripts/tandem/CampaignNatives.reds`, `scripts/tandem/Campaign.reds` (the
save's field, the notices, the co-op menu's page). Engine facts:
[`research/engine/progression.md`](../research/engine/progression.md) §13.38.10.

## A copy and its lineage

<!-- prettier-ignore -->
| Field | Where | What |
| --- | --- | --- |
| campaign | save, record, wire | 64 random bits, made when a save becomes a campaign |
| copy | save, record, wire | 64 random bits per line of play: a new one at every load of the save |
| generation | save, record, wire | 0 for the first copy, its parent's + 1 |
| ancestors | save, record, wire | the copies it came from, parent first, the newest 32 (`kMaxAncestors`) |
| saved | record, wire | when its newest save was written (UTC seconds) |
| who | record, wire | who played it last (their tandem name) |
| quest | record, wire | the last quest done in it (the journal's title of the last quest that succeeded; a guest's own journal in a session is the host's story and does not count) |
| slot | record | the save folder of its newest save on this machine (`ManualSave-12`) |
| foreign, set aside, chosen | record | another player's copy heard in a session; a copy the players chose not to go on from; the copy we chose |
| mods | archive's record | the exporting game's other mods (`mods::Mod`: name, hash, what it adds) |

- **In the save**: the first four, as the words of a persistent field
  (`TandemCampaignSystem.m_words`, `campaign::Pack`). The save and its
  identity are written together and lost together, as the reward ledger is
  (`quest-carryover.md`, "The ledger"); no file has to be matched to a save.
- **In tandem's folder**: one record per copy,
  `red4ext/plugins/tandem/campaigns/lineage/<campaign>-<copy>.txt`, a small
  `key = value` text (`campaign::Write` / `Read`). Nothing is written beside
  the game's own saves; the only write into the saves folder is an import's
  new slot.

**Moving on.** Every load of a campaign save starts a new copy, a child of
the loaded one (`campaign::Advance`, `TandemCampaignNative.CampaignLoaded`
from `OnPlayerAttach`): a co-op session or a solo play is one generation. A
load that is never saved leaves nothing (the record is written at the
copy's first save). Reloading after a death is a play too: generations
count loads that were saved, not sessions. An **export ends a copy**: the
running game goes on as a child of the one exported, so a copy that left
the machine never moves on there, and two players who both play on from
it make a fork, as they should.

**A save of the copy.** The plugin's worker (a thread of its own, files
only) looks every few seconds for a slot whose `sav.dat` is newer than the
last one seen since the load, and records it as the copy's newest save:
the slot, the time, the last quest. The game's `OnSavingComplete` only
wakes it (whether it reaches us for autosaves is unverified, §13.38.10).

## The rules

`campaign::Relate(ours, theirs)`:

<!-- prettier-ignore -->
| Relation | When | Offered |
| --- | --- | --- |
| none, unrelated | one is no campaign copy; two campaigns | nothing |
| same | the same copy, saved at the same time | nothing ("everyone has the same copy") |
| behind | theirs descends from ours (ours among its ancestors), or the same copy saved later | a fast-forward: "Continue from NAME's save (DATE, after QUEST)?" |
| ahead | ours descends from theirs | the others are offered ours |
| fork | neither descends from the other | both shown, with who, when and the last quest; the players choose |

`campaign::Judge` takes the host's copy and everyone's copy of the same
campaign: the **heads** are the copies nobody's is ahead of (the same copy
twice is one). One head: a fast-forward to it (everyone else is behind);
several: a fork. It also says whether our copy is behind and whether the
host is about to play an older copy than someone has. Every game draws the
same verdict from the same copies. Past 32 ancestors a copy that does
descend from ours reads as a fork: the players choose, nothing is lost.

## In the session

`proto::Campaign` (appended after `HackTraced`, protocol 12; Event channel): a player's
copy, with `kCampaignPlaying` for the copy its game plays now and `chosen`
for the copy it chose to go on from. The host says its own at every
Welcome and when it changes; a guest says its copy of the host's campaign
(the copy it plays, else its newest own record of that campaign, picked
once the host's copy arrived), and the host stamps it, keeps it for late
joiners and passes it to everyone else (`Endpoint::SetCampaign`,
`OnCampaign`). A repeat is dropped; a copy with campaign 0 takes one back;
a player who leaves takes its copy with it. The referee: either side's own
word (`SaidBy` Either, stamped), a valid lineage (no more ancestors than
generations, never its own ancestor), never a player's own copy passed
back to it (`tests/referee_test.cpp`). Not host-owned state: no digest, no
resync; a lost one is said again at the next join.

What each game does with the verdict (`Campaign.reds`, `TellVerdict`), as
notices and on the co-op menu's **Shared campaign** page:

- **Behind, one head** (a fast-forward): "Campaign: NAME's save (DATE,
  after QUEST) is newer than your copy". The page offers "Continue from
  NAME's save (…)": a question, then the choice is recorded and said
  (`chosen`), our copy set aside, and the player is told to get the
  head's export. The host behind is told "you host an older copy"; a guest
  ahead hears "your copy is the newest".
- **A fork**: "N copies went on separately", and one "Go on from …" item
  per head (ours: "your own copy"). The copies not chosen are set aside;
  each player chooses for their own records, the others see the choice in
  the copies they hear (`chosen`, logged).
- **Set aside** means: no longer offered, and our own copy set aside leaves
  the campaign at its next load (its words cleared, a notice): **a normal
  save its owner can still play**.
- **Before hosting**: copies heard from others are kept as foreign records
  of a campaign we have. A load of a copy that one of them is ahead of, or
  forked from, says so at once ("this copy is older than NAME's"), before
  anyone joins. A guest that joins with a newer copy is told to the host at
  its join.
- **Mods**: the join's own mod comparison (`mod-compat.md`) says what
  differs, as for any session; nothing more is checked for the campaign.

## Transfer by hand (this step)

- **Make this a shared campaign** (pause menu → Co-op → Shared campaign;
  not as a guest in a session): a question, then the save's words get a
  new campaign and the game makes a new manual save, its first copy.
- **Export campaign** (the same page, a campaign save loaded; not as a
  guest in a session; not when the game would refuse a save: `SaveBlocker`
  says why): a question, a new manual save of the copy
  (`ManualSave("ManualSave-")`, the game's own numbering), then the worker waits for
  its files to settle and writes `campaigns/campaign-WHO-YYYYMMDD-HHMM-gN.zip`
  (UTC; another `-2` if taken), and says where. The running game then goes
  on as a new copy.
- **The archive**: a zip of stored (uncompressed) files: no compression
  library is vendored, so `report::Zip`'s writer (the bug report's) is
  reused and `campaign::ReadArchive` reads it back (stored entries only,
  CRC checked). Its first entry is `tandem-campaign.txt`, the copy's
  record with the exporting game's mods; then `save/` and each file of the
  slot (`sav.dat`, `metadata.9.json`, `screenshot.png`). Refused on read:
  a compressed or damaged entry, any entry outside `save/` or with a
  folder in its name, a name twice, no `sav.dat`, more than 512 MB.
- **Import campaign** (Shared campaign page, main menu and pause menu): one
  item per zip in the folder (newest save first, read from the zip's first
  entry only), then a question naming the save and, when its game had mods
  ours lacks (those that only change what one game shows left out), their
  names. Yes installs it as a **new manual slot**: one past the highest
  `ManualSave-N` there, made with `create_directory` (never an existing
  folder), its files written aside and renamed, `sav.dat` last; a
  `metadata.9.json` that names the old slot in quotes names the new one.
  Then: "Installed as a new save (SLOT): load it with Load game". Its record
  joins ours (the same copy, not foreign).
- **Campaign folder** says where the files go: the player sends the zip
  to the others any way they like, and they put it there.
- Saves folder: `SHGetKnownFolderPath(FOLDERID_SavedGames)` + `CD Projekt
Red\Cyberpunk 2077`, else `%USERPROFILE%\Saved Games\…`; none found: no
  start, export or import (logged at load).

Tests drive the same with `tools/game.sh cmd campaign` (the state in the
log), `campaign list`, `campaign start`, `campaign export`, `campaign
import N`, `campaign choose N` (no question asked; in the main menu
`campaign`, `list` and `import`).

### Seam: the transfer through the session (next step)

Everything a transfer needs is bytes: `campaign::MakeArchive(record,
files)` and `ReadArchive(bytes)` / `Install(archive, saves)`. A session
transfer adds a message pair on the Bulk channel (an offer of a copy, then
its archive in chunks, like the journal snapshot), started by "Continue
from NAME's save" instead of the instruction to ask for the file, and the
worker installs what arrives with `Install`. The choice, the verdict and
the records do not change.

## What needs the game

- That the persistent field is saved and restored, and `OnPlayerAttach`
  sees it (the ledger relies on the same, also unverified).
- The saves folder under Proton, its layout, and that the game lists an
  imported slot (with a restart of the load menu or of the game), loads it,
  and that the metadata needs nothing more than the slot's name.
- That a manual save asked from the pause menu writes while it is open,
  and the worker finds its slot; `OnSavingComplete` for autosaves.
- The journal's last quest done (a quest's state Succeeded, its title).
- The menus: the page, the questions, the popups at a job's end.
- Scenarios (the mock player plays a part; not yet run):
  `campaign-fastforward`, `campaign-fork`, `campaign-host-behind`. Start,
  export and import write the player's saves: by hand, with a save that can
  be lost.

## Tests and tools

- `tests/campaign_test.cpp`: generations and ancestors, the save's words,
  same, older, a fast-forward and a fork, the verdict (same, a guest ahead,
  the host ahead, a fork of three, one head twice, another campaign), the
  record's text, the records folder (newest own copy, a newer one
  elsewhere), the archive (a way out, no lineage, no save, a name twice, a
  damaged one), the next manual slot, export then import into new slots
  (never over one, the metadata's slot renamed), and the copies through a
  session of three over loopback; `tests/protocol_test.cpp` and
  `tests/referee_test.cpp` for `Campaign`.
- Mock player: `--campaign C:COPY:GEN[:ANCESTORS]` (hex ids, ancestors
  parent first), `--campaign-quest TEXT`, `--campaign-saved S`: its copy,
  said to the others; theirs and the verdict are logged
  (`testing.md`).
