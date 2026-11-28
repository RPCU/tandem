# Two games: the tower's Smasher (q116) fought by the partner: the host's boss, attacking the partner, with the same phase, bar, broken weak spot and death on both screens
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# BOSS_RECORD: another Character record (or kind:boss|chimera|cerberus,
# name:PART, as `boss spawn` takes it); default Character.q116_boss_smasher.
# To look at: as boss-smasher, the ending's record.
boss_fight "${BOSS_RECORD:-Character.q116_boss_smasher}" boss-smasher-tower
