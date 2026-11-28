# Two games: Sasquatch fought by the partner: the host's boss, attacking the partner, with the same phase, bar, broken weak spot and death on both screens
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# BOSS_RECORD: another Character record (or kind:boss|chimera|cerberus,
# name:PART, as `boss spawn` takes it); default Character.main_boss_sasquatch.
# To look at: her hammer, the boss bar on both; once her weak spot breaks she drops the hammer on both.
boss_fight "${BOSS_RECORD:-Character.main_boss_sasquatch}" boss-sasquatch
