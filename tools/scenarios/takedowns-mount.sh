# As a guest: the host's puppet's body is probed for the grapple slot as it comes, and a proxy mounted on that slot is read back: the game's mount, or why not
# shellcheck shell=bash disable=SC2317
ensure join
host_world recordings/npcs.rec --mode front --body male
expect 'puppet 6[0-9]{4}: clips walk' 60
# Said once a body: which way a grapple by that player goes here.
expect 'takedowns: player 1.s puppet: grapple by the (game.s mount|fallback: )' 30
cmd takedown probe
expect 'takedown probe: player 1.s puppet: slot component (true|false), grapple slot (true|false), leftShoulder slot (true|false)' 5
# The mount itself, whatever the probe said: the nearest proxy onto the
# puppet's grapple slot, read back half a second on, unmounted.
cmd takedown mount
expect 'takedown mount: proxy [0-9]+ asked onto player 1.s puppet.s grapple slot' 5
expect 'takedown mount: on player 1.s puppet (by the game.s mount|refused: )' 5
shot takedowns-mount
reject 'watchdog: game thread silent'
