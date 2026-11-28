# Two games: Oda fought by the partner: the host's boss, attacking the partner, with the same phase, bar, broken weak spot and death on both screens; his blades' light on the partner's proxy too
# shellcheck shell=bash disable=SC2317,SC2154,SC2034
# needs two games
# BOSS_RECORD: another Character record (or kind:boss|chimera|cerberus,
# name:PART, as `boss spawn` takes it); default Character.main_boss_oda.
# To look at: his mask, cloak and blades the same on both screens, the boss bar on both.
BOSS_AI_EVENT=BladesOn
boss_fight "${BOSS_RECORD:-Character.main_boss_oda}" boss-oda
