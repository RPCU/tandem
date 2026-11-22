# A guest's checkpoint (tandem.ini checkpoint, here asked by command): the leave steps, the save (dry: counted as written), then the same session joined again
# shellcheck shell=bash disable=SC2317
# Reads whether a save may be written (test games lock theirs: saves = off);
# runs dry, and a guest in a session saves nothing.
ensure join saves=on
host_world - --mode idle
expect 'in the session as player' 30
expect 'save: leave solo -> session' 20
pause 3
mark
cmd checkpoint dry
expect 'checkpoint: leaving to save our V, then joining again \(dry\)' 10
expect 'coop notice: Co-op checkpoint: saving your V' 10
expect 'save: leave session -> cleaning' 10
expect 'save: back from co-op: our own world is back, saving unlocked \(leave = stay, a save wanted\)' 30
expect 'save: back from co-op: dry: no save written, counted as written' 20
expect 'checkpoint: saved: joining the session again' 10
expect 'session: chosen join' 10
expect 'in the session as player' 30
expect 'save: leave solo -> session' 20
cmd progress
expect 'saves: leave = stay, step session, our lock true, .* owed false, exit none' 10
reject 'save: back from co-op: quick save asked'
reject 'checkpoint: the session could not be chosen again'
reject 'save: stays locked'
reject 'watchdog: game thread silent'
