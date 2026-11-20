# As host: a guest's footsteps at a sprint are noise from its puppet for our NPCs (the stimulus's record and tags are logged once), with a ganger hostile to puppets standing by
# shellcheck shell=bash disable=SC2317
ensure host
cmd npc hostile puppets
expect 'npc hostile: .* spawning \(true\)' 10
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode course --body female --footsteps
expect 'joined \(player [0-9]+' 20
expect_peer 'course: sprint' 90
expect_peer 'footsteps: noise 1 sent \(stim 2[56]' 10
expect 'stealth: player [0-9]+ makes noise: stim 2[56] \(FootStep(Sprint|Regular)\)' 10
# The record's reach and the tags vanilla judges it by (stealth.md: which of
# the two wraps a puppet's footsteps need).
expect "stealth: a puppet's FootStep(Sprint|Regular): radius [0-9.]+ m, reaction stim (true|false), tags Stealth (true|false), PlayerOnly (true|false)" 5
# With the log at debug, an NPC that takes the stimulus up says so
# ("stealth: ID reacts to player N's FootStepSprint"): it needs the puppet
# within the radius above and out of the ganger's sight, so a person checks
# it (sprint behind a guard: it turns; crouch-walk behind it: it does not).
cmd npc hostile clear
reject 'watchdog: game thread silent'
