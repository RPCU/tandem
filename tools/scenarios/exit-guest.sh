# As a guest in a session, the pause menu's Exit saves our V first: the leave steps, the save, then the exit to the main menu (dry: the save counted as written, the exit only logged)
# shellcheck shell=bash disable=SC2317
# The exit flow reads whether a save may be written: a test game locks its
# saves (saves = off); this one runs dry and a guest in a session saves
# nothing, so the lock is off here.
ensure join saves=on
host_world - --mode idle
expect 'in the session as player' 30
expect 'save: leave solo -> session' 20
# What the pause menu's wrap reads: a save is owed, nothing blocks it.
cmd exit check
expect 'exit: owes a save true, blocked by nothing, the game.s locks: ' 10
mark
cmd exit menu dry
expect 'exit: the main menu held: our V is saved first \(dry\)' 10
expect 'coop notice: Co-op: saving your V before exiting' 10
expect 'save: leave session -> cleaning' 10
expect 'save: back from co-op: our own world is back, saving unlocked \(leave = stay, a save wanted\)' 30
expect 'save: leave waiting -> saving' 20
expect 'save: back from co-op: dry: no save written, counted as written' 10
expect 'save: back from co-op: saved' 10
expect 'exit: dry: would go to the main menu now' 10
cmd progress
expect 'saves: leave = stay, step solo, our lock false, .* owed false, exit none' 10
# Never a save of ours in a test, never a load, never a verdict that co-op state stayed.
reject 'save: back from co-op: quick save asked'
reject 'save: back from co-op: saved; loading'
reject 'save: stays locked'
reject 'watchdog: game thread silent'
