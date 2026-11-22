# Prompts on a player and on a partner's vendor

`scripts/tandem/Prompts.reds`, `tweaks/tandem/prompts.tweak`. Built
2026-11-22, compiled against 2.50, **not run in game**: "To see" below is
the order to check it in. Engine facts: engine-knowledge §13.153.

## What it is

"Trade" and "Revive" on another player's puppet, and "Trade" on the proxy
of a vendor a partner's game runs, are the game's own interactions: a
choice on the body's interaction component, pressed with the game's
interaction key. The game then decides what our scripts decided before:
range, that our V looks at the body, line of sight, no prompt from a seat,
which prompt wins when a door or a container is nearer, the key as the
player bound it, the prompt on the body.

Who may be offered what stays with each feature: `Revive.reds` (a downed
player, our V able), `Trade.reds` (`Free`: trade on, no combat, scene tier,
on foot, no trade open; the partner up and in our world), `Vendors.reds`
(`Nearest`, `Decide`, `VendorPrompts`). Each calls `TandemPrompts.OfferOn`
every tick its rules hold. `true`: the game shows it, the key comes back
through `TakePress`. `false`: the feature draws the prompt itself, as
before (an `InteractionChoiceHubData` written into the `UIInteractions`
blackboard, the key from its own input listener).

## The carrier

| What        | Which                                                                                                                                                                                                                            | Why                                                                                                                                                                                                                                                                   |
| ----------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| component   | the body's own `Interaction` (`gameinteractionsComponent`), the one `ScriptedPuppet` resolves in `OnTakeControl` and vanilla sets an NPC's Talk, Loot and Grapple on                                                             | an NPC proxy has it (a real NPC template; `Takedowns.reds` already switches its layers); `ScriptedPuppet.EnableInteraction` and the choice event's handler are the puppet class's own                                                                                 |
| on V's body | V's `player_*_tpp_cutscene.ent` is not known to have one: `Body.reds` (`MakeInteractive`) keeps the template's own, else clones the donor's (Panam's, Kerry's) into the body being built, beside the senses and the hit mesh     | the same graft, the same event; the stand-in bodies are whole NPC templates                                                                                                                                                                                           |
| layer       | `GenericTalk`                                                                                                                                                                                                                    | the layer vanilla turns on for a person one walks up to (`ToggleInteractionLayers`); its handlers do nothing with a choice that carries no action. Never `Grapple` or a takedown layer: any choice on those starts a takedown (`ProcessSyncedAnimationPuppetActions`) |
| choice      | `InteractionComponent.SetSingleChoice(choice, n"GenericTalk")`, `choice.caption` the text of `Text.reds`, `choiceMetaData.tweakDBName` one of `Interactions.TandemRevive`, `TandemTrade`, `TandemVendor`                         | records of our own name the choice in the event; each is the door's `Interactions.Open` (a press of `Choice1`) renamed                                                                                                                                                |
| key         | `InteractionChoiceEvent` on the body: a wrap of `ScriptedPuppet.OnInteractionUsed` reads it and hands a `TandemPromptPress` to the delay system; `TandemPrompts.OnPress` on its call, the feature's `TakePress` on the next tick | the hook may run on a job thread: it reads and enqueues                                                                                                                                                                                                               |

Rejected: a scripted `PuppetAction` in the record's `objectActions`
(vanilla's way for Talk; needs an action class, prereqs and a record per
action for no gain: the layer's rules are the same); a wrap of
`ScriptedPuppetPS.PushChoicesToInteractionComponent` to add ours to
vanilla's (a persistent state's method, run from script tasks: state read
across threads).

Vanilla resets the layer's choices when it activates
(`OnInteractionActivated` → `DetermineInteractionState`) and after any
press (`OnInteractionUsed`): ours is set once the layer is active for our
V (`GetActivatorsForLayer`, `GetActiveInputLayers`, asked every tick) and
again every 0.25 s while the game's prompt does not hold it, 8 times per
activation at most.

On a player's puppet the NPC layers are switched off (`Grapple`, the
takedown and finisher layers, `Loot`, `GenericTalk`, `ReturnTalk`): a
grafted component brings them all. Each puppet is met within a second of
its body existing, offered a prompt or not (`TandemPrompts.Sweep`), and
its layers are switched off again every 5 s (vanilla turns the takedown
layers back on when an NPC gets up, `ScriptedPuppet.OnResurrected`); not
with `native_prompts = off`, which touches nothing. The `GenericTalk`
layer is on only while a prompt is offered.

A lead, not used: `PlayerPuppet.OnIncapacitated` turns on a layer named
`Revive` (the cut multiplayer's), so V's own templates may carry an
interaction component with that layer; `prompts layer Revive` tries our
choices on it.

## One body, two ways

Each body goes one way, said once in the log, and back to drawn prompts by
itself when the game's interaction does not prove to work on it.

| Line                                                                                                                                                           | When                                                                                                            |
| -------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| `prompts: the game's own interactions, layer GenericTalk, choices …` / `prompts: drawn by us, none is the game's own interaction (WHY)`                        | a session's start: the switch, the three records and their key                                                  |
| `body: female body has an Interaction component of its own` / `given Character.TandemDonorWa's Interaction component` / `no Interaction component …`           | a puppet's body is built                                                                                        |
| `prompts: NAME (a player's puppet, entity X): the game's own interaction (…), not yet seen working` / `: drawn prompts (WHY)`                                  | the first prompt offered on a body                                                                              |
| `prompts: trade offered on NAME through the game's interaction`                                                                                                | an offer starts or changes kind                                                                                 |
| `prompts: NAME: its layer is active for our V (…)`                                                                                                             | the first activation: the game's scanner sees the body                                                          |
| `prompts: trade shown on NAME by the game (hub ID: 'Trade' key Choice1 hold false)`                                                                            | the game's own prompt holds our choice (read from `UIInteractions.InteractionChoiceHub`, which the game writes) |
| `prompts: trade pressed on NAME: the game's choice event (BUTTON_PRESSED, layer GenericTalk)`, then once `prompts: NAME: the game's own interaction works (…)` | the key                                                                                                         |
| `prompts: NAME (entity X): back to drawn prompts: WHY`                                                                                                         | the fallback                                                                                                    |
| `prompts: NAME: our trade choice is set and the game shows …`                                                                                                  | another prompt comes first, or the body is not looked at: the game's call, no fallback                          |

The fallbacks (`TandemPrompts.Carry`, `Tick`):

- tandem.ini `native_prompts = off` (no graft either), `prompts off`, or a
  record of ours missing: every body drawn.
- No `Interaction` component on the body.
- The layer never activated with our V on foot within 1.3 m, its view
  within 20° of the body, for 2 s (the layer on for 2 s).
- Never yet shown on that body: it is the game's interaction look-at
  target (`InteractionManager.IsInteractionLookAtTarget`), our choice was
  set 3 times over 1.5 s, and the game's prompt holds something else (not
  a prompt we draw).
- Never yet shown on that body: the layer is active, our choice set, and
  no prompt at all for 3 s with our V in front of the body.
- The key went down (our input listener) on a prompt of the game holding
  our choice and no choice event came within 0.5 s: the press is taken by
  hand, then drawn.
- Revive only: a choice event came and our listener never saw the key (a
  hold could not be followed to its release).

## Each prompt

- **Trade**: on every player here who is up; `TakePress` asks
  (`TandemTradeTable.Ask`). The drawn one keeps its rules (2 m, 70° each
  side, on foot).
- **Revive** comes before Trade on one body. On a downed player lying in
  the open; one **in a seat keeps the drawn prompt** and its rules (the
  door we face, the car stopped, `KeepFromCar`): the look-at would be the
  car's. The choice event is the press: the 10 s hold starts if the key is
  still down, and its release is the input listener's (`Revive.reds`
  `OnAction`, which tells `TandemPrompts.KeyIs`). The prompt reads "[F]
  Revive", not "[Hold F]": the record's key is a press (no hold action's
  name is known, §13.153).
- **Vendor**: on the proxy the scan found (`Nearest`: in reach, in front;
  `VendorPrompts`), offered every tick between scans; `TakePress` opens
  (`TandemVendors.Open`, which says why a shop stays shut).

## Tests

- `tools/game.sh cmd prompts`: every body and the game's prompt now;
  `prompts off | on` (all drawn | the game's again, bodies adopted again);
  `prompts face [N]` (our V turned to player N's puppet, or put 1.2 m in
  front of it); `prompts layer NAME` (our choices on another layer, bodies
  adopted again); `prompts press` (a choice event queued on the body that
  carries a choice: the way from the event to the feature, no key).
- `cmd test query prompt [player N | npc N]` = `game|drawn STATE layer
on|off active B shown B choice KIND activations N shows N presses N`
  (STATE: unproven, activated, shown, pressed, drawn).
- Scenarios: `prompts-game` (active, shown, drawn after `prompts off`, the
  game's again, the event asks the trade), `prompts-revive` (Revive before
  Trade on a downed player, the event starts the hold), `prompts-drawn`
  (`native_prompts = off`); `vendor-guest` takes either prompt. `trade-peer`,
  `phone-trade`, `revive-game` drive by command and do not ask which way.
- The thread probe is the first statement of `TandemPromptPress.Call`:
  `threads: hook Prompts.TandemPromptPress.Call ran beside the script tick`
  would say the delay system calls it beside our tick (it changes
  `TandemPrompts`).

## To see, in this order

1. `prompts-drawn`, then `trade-peer`, `revive-game`, `vendor-guest` with
   `native_prompts=off`: nothing changed on the drawn way.
2. A join with the default: the `body:` line (own, grafted or none), no
   crash building a body with the grafted component, the puppet moving and
   hit as before.
3. `prompts-game` up to `its layer is active`: whether a cloned component
   keeps its layers and the scanner sees V's body. Then `shown`: whether
   `caption` is the text shown (else the line names what the game shows).
4. By hand: the key on the prompt (`prompts: trade pressed … the game's
choice event`, and no `back to drawn prompts`); no prompt from behind a
   wall, facing away, from a seat; a door's or a container's prompt beside
   the player.
5. `prompts-revive`, then by hand on a body lying down: looking down at
   it shows "Revive"; holding F fills the bar, letting go stops it
   (`revive: stopped reviving NAME (released)`).
6. No grapple, takedown or loot prompt on a player's puppet (crouched
   behind it, on a downed one, with `trade = off` too); no "Talk" on one
   when nothing is offered (in combat).
7. `vendor-guest` with the default: the prompt on the proxy, the shop
   opening on the key.
8. A hold action for Revive ("[Hold F]"): the `action` of
   `Interactions.TandemRevive`, once a hold action's name is read from the
   game's `inputContexts.xml`.
