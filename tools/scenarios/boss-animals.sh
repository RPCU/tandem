# Two games: the Animals' boss of the Grand Imperial Mall (q110) fought by the partner: the host's boss, attacking the partner, with the same phase, bar, broken weak spot and death on both screens
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# BOSS_RECORD: another Character record (or kind:boss|chimera|cerberus,
# name:PART, as `boss spawn` takes it); default Character.q110_animals_boss.
# To look at: the boss bar on both, the brawler closing on the partner.
boss_fight "${BOSS_RECORD:-Character.q110_animals_boss}" boss-animals
