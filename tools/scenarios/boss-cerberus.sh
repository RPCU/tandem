# Two games: Cerberus (q305) fought by the partner: the host's boss, attacking the partner, with the same phase, bar, broken weak spot and death on both screens
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# BOSS_RECORD: another Character record (or kind:boss|chimera|cerberus,
# name:PART, as `boss spawn` takes it); default kind:cerberus.
# To look at: the robot's scan and eyes on both screens, its pick of the partner once it sees them.
boss_fight "${BOSS_RECORD:-kind:cerberus}" boss-cerberus
