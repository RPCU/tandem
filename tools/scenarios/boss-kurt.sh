# Two games: Kurt Hansen (q307) fought by the partner: the host's boss, attacking the partner, with the same phase, bar, broken weak spot and death on both screens; his knife look on the partner's proxy too
# shellcheck shell=bash disable=SC2317,SC2154,SC2034
# needs two games
# BOSS_RECORD: another Character record (or kind:boss|chimera|cerberus,
# name:PART, as `boss spawn` takes it); default name:kurt.
# To look at: his knife and pistol looks on both screens, the boss bar on both.
BOSS_AI_EVENT=KnifeOn
boss_fight "${BOSS_RECORD:-name:kurt}" boss-kurt
