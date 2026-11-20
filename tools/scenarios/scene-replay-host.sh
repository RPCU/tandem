# As host: a (test) scene with the NPCs around our V is announced to a guest with its cast and staging, the actors' bodies are read and sent, a line of ours is voiced for it, and the end is said
# shellcheck shell=bash disable=SC2317
# Scene replay is off by default (not yet verified in game): turned on here.
ensure host scene_replay=on
# Actors whatever the street holds.
cmd npc spawn 2
expect 'npc spawn: 2 gangers' 10
pause 3
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male
expect 'joined \(player [0-9]+' 20
expect 'scene: replay on, with the actors. bodies' 5
pause 5
# As if a scene played with the shared NPCs within 12 m of our V.
cmd story scene test on
expect 'scene: test scene on' 5
expect 'scene: our scene plays for the guests \(play 1, [1-9][0-9]* actor' 10
expect_peer 'scene: 0x[0-9a-f]+ play 1 tier [0-9] rate 1.0 flags 0x8 staged at' 10
expect_peer 'scene cast: play 1, [1-9][0-9]* actor' 10
# Their bodies: read after the engine animated them, sent as poses.
expect 'pose stream: reading actor [0-9]+' 10
expect "pose stream: actor [0-9]+'s skeleton 0x[0-9a-f]+ \([0-9]+\), mapped" 10
expect_peer 'scene: [0-9]+ actor pose\(s\) heard' 15
# A prop in the first actor's hand, and put away.
cmd story scene prop Items.FirstAidWhiffV0
expect_peer 'scene prop: 1 \(0x[0-9a-f]+\) held by npc [0-9]+ \(play 1\)' 10
cmd story scene prop off
expect_peer 'scene prop: 1 \(0x[0-9a-f]+\) gone' 10
# Our camera's view, as a cutscene's would be sent.
cmd story scene camera on
expect 'scene: test camera on' 5
expect_peer 'scene camera: [0-9]+ view\(s\) heard \(play 1' 10
cmd story scene camera off
# A line of our V's: its voice for the guest, with the scene's play.
cmd story line Hello from the scene
expect 'scene: line voiced for the guests \(npc 0, player 1' 10
expect_peer 'scene line: [0-9]+ voiced by player 1 \(3.0 s, play 1\)' 10
cmd story scene report
expect 'scene: host play 1 \(on\)' 5
expect '[1-9] actor\(s\) read \([1-9][0-9]* poses' 5
cmd story scene test off
expect 'scene: our scene is over for the guests' 10
expect_peer 'scene: play 1 over' 10
expect 'pose stream: actor [0-9]+ no longer read' 10
reject 'watchdog: game thread silent'
reject 'not a body the stream knows'
