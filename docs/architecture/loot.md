# Loot

## Inside the plugin

- **Loot** (`scripts/tandem/Loot.reds`, decided 2026-11-18): **per-player
  loot.** Every corpse and container is looted by each player in their own
  game, rolled from its loot tables by that game's V (its level, its
  perks): nobody takes from the other, and a body one player emptied is
  still full for the other. No item crosses the network for a corpse: the
  host's loot never appears on a guest. The host's NPCs roll their loot
  when they die, as in solo; a guest's proxy, killed by the host's word,
  dies the game's way and its death rolls the guest's own loot from the
  real NPC's record (`NPCPuppet.OnIncapacitated` → `ProcessLoot`); a
  second later tandem checks that it did (and rolls it the save-load way
  if not). **Weapons** an enemy drops at death are part of its loot, so
  per player too: each game's corpse drops what its copy holds (sharing
  them would make players race for the enemy's gun). **Quest and
  important items are every player's** (decided by the user 2026-11-18:
  no quest may break): an item tagged `Quest` (quest items, their keys
  and shards), a keycard (`Gen_Keycard`) or an iconic item (the
  `IsItemIconic` stat) that comes into a V's inventory, not bought at a
  vendor, goes into every other V's too (`SharedItem`), by the game's own
  item-add path and its pickup notification, unless that V has one
  already. A guest's goes through the host, which gives it to its own V
  first, so the host's quests see it whoever looted it. The important
  items on a host NPC's corpse are also put on the guests' proxy corpses
  (a guest finds them where the host would); a second copy of an item
  co-op already gave is taken back out when it arrives. **Rewards**
  (quest and gig rewards: items, money, XP, street cred) are the story's
  `QuestReward`: the host's game pays its own V as in solo and every
  guest that took part gets the same reward record through
  `RPGManager.GiveReward`, once (Story.reds). When the host's body goes
  (streamed out, disposed), a guest's proxy corpse leaves what it still
  holds as a loot bag, as vanilla does for a disposed body. **Items a V
  drops on purpose** (the backpack or inventory menu) are shared world
  items, the host's: the dropper's game drops them in one bag as vanilla
  does and sends `WorldItem` (records, seeds for a unique item's stats,
  quantities, where); the host numbers it, keeps it for late joiners and
  sends it to everyone; every other game spawns a copy of the bag there,
  from the dropper's puppet. Picking a stack up is immediate and a claim
  to the host (`WorldItemTaken`): the host grants the first and tells
  everyone, who take it out of their copy; the loser of a race (two
  pickups within a ping) gives it back with a notice. When a guest's
  session ends, its copies of the others' drops go. Mock player
  `--drop-item`, `--take-items`, `--remove-corpses`, `--loot-quest`;
  scenarios `loot-drop`, `loot-corpse`, `loot-quest`.
  **Not yet verified in game** (engine-knowledge §13.19): that a proxy's
  death rolls its loot and drops its weapon (a proxy whose brain went off
  before it drew its weapon may drop none), which class the drop bag is,
  that a copy from a puppet's inventory and a seed has the same stats,
  and that the bag a removed corpse drops stays.
