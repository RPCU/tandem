# Two games: Royce (his exo) fought by the partner: the host's boss, attacking the partner, with the same phase, bar, broken weak spot and death on both screens
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# BOSS_RECORD: another Character record (or kind:boss|chimera|cerberus,
# name:PART, as `boss spawn` takes it); default Character.main_boss_royce.
# To look at: the exo's weak spot broken on both, the boss bar on both.
boss_fight "${BOSS_RECORD:-Character.main_boss_royce}" boss-royce
