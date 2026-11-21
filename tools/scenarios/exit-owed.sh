# A guest whose host went (leave = stay wrote nothing) still owes the session's save: the pause menu's Exit makes it first, then quits to the desktop (dry: no save written, no exit made)
# shellcheck shell=bash disable=SC2317
ensure join
host_world - --mode idle
expect 'in the session as player' 30
expect 'save: leave solo -> session' 20
# The host goes: our world comes back and saving unlocks, nothing written.
mark
pkill -x tandem-peer
expect 'session ended' 60
expect 'save: back from co-op: our own world is back, saving unlocked \(leave = stay\)' 40
cmd progress
expect 'saves: leave = stay, step solo, our lock false, .* owed true, exit none' 10
cmd exit check
expect 'exit: owes a save true, blocked by nothing' 10
mark
cmd exit desktop dry
expect 'exit: the desktop held: our V is saved first \(dry\)' 10
expect 'save: leave solo -> waiting' 10
expect 'save: back from co-op: dry: no save written, counted as written' 10
expect 'exit: dry: would go to the desktop now' 10
cmd progress
expect 'saves: leave = stay, step solo, .* owed false, exit none' 10
# Owed no more: the next Exit is the game's own.
cmd exit check
expect 'exit: owes a save false' 10
reject 'save: back from co-op: quick save asked'
reject 'watchdog: game thread silent'
