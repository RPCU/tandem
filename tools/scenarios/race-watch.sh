# As a guest: the host's V is in a race (its Activity says so, the mock player sends no race of its own): we watch, nobody offers a race to join, and the race report answers
# shellcheck shell=bash disable=SC2317
ensure join
host_world recordings/npcs.rec --mode idle --body female --contest race --activity-at 8 --activity-for 15
expect 'puppet 6[0-9]{4}: clips walk' 60
expect 'contests: player 1 is in a race' 30
cmd contest races
expect 'races: ours phase 0 .*theirs: player 0 race 0 phase 0, joined 0' 5
reject 'races: asking to join'
cmd contest join
expect 'races: no partner.s race to join' 5
expect 'contests: player 1 is no longer in a race' 30
reject 'watchdog: game thread silent'
