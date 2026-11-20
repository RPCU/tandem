#!/usr/bin/env bash
# Every option tandem-peer's usage lists must parse (a listed but unparsed
# --record cost an in-game test run). Usage: peer_flags.sh PATH/tandem-peer
set -euo pipefail
peer=$1
declare -A value=(
  ["--port"]=27451 ["--look"]=random ["--look-keys"]=looks ["--seed"]=7 ["--name"]=test ["--body"]=male ["--mode"]=idle ["--offset"]="1,2,3"
  ["--delay"]=100 ["--rate"]=30 ["--pose"]=x.tpose ["--vehicle"]=Vehicle.x
  ["--drive-seconds"]=5 ["--radio"]=radio_station_09_downtempo ["--time"]=06:30 ["--weather"]=24h_weather_rain
  ["--record"]=x.rec ["--replay"]=x.rec ["--anims"]=x.anims ["--hit-npcs"]=2 ["--hit-you"]=2 ["--pause-at"]=10 ["--pause-for"]=3
  ["--drive-seconds"]=5 ["--time"]=06:30 ["--weather"]=24h_weather_rain
  ["--record"]=x.rec ["--replay"]=x.rec ["--hit-npcs"]=2 ["--pause-at"]=10 ["--pause-for"]=3
  ["--grab-npcs"]=5 ["--carry-npcs"]=5 ["--hold-npcs"]=5
  ["--relay"]=relay.example.org:27460 ["--code"]=k7q-2m9
  ["--relay-token"]=00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff
  ["--hack-npcs"]=4 ["--hack-action"]=QuickHack.BlindHack ["--hack-effect"]=BaseStatusEffect.Blind
  ["--hack-player"]=6 ["--npc-hack"]=AIQuickHack.HackRevealPosition ["--hack-car"]=5
  ["--npc"]=Character.x ["--detect-at"]=15 ["--detect-for"]=10 ["--camo"]=10 ["--camo-record"]=BaseStatusEffect.OpticalCamoCoolPerkShort ["--shoot"]=3
  ["--strike"]=nonlethal ["--difficulty"]=veryhard ["--area-hit"]=5
  ["--friendly-fire"]=on ["--enemy-scaling"]=on ["--enemy-scaling-health"]=0.8 ["--near-npc"]=--check ["--throw"]=5 ["--throw-item"]=Items.x ["--smart"]=5 ["--defend"]=5
  ["--hit-you-melee"]=on
  ["--story"]=x.story ["--answer"]=1 ["--reply"]=12345 ["--suggest"]=0
  ["--restrict"]=GameplayRestriction.NoCombat ["--restrict-at"]=2 ["--restrict-for"]=5 ["--status"]=BaseStatusEffect.Burning@4-10
  ["--shoot-you"]=3 ["--shot-weapon"]=Items.x
  ["--sandevistan-at"]=10 ["--sandevistan-for"]=5 ["--sandevistan-scale"]=0.25 ["--scan-at"]=10
  ["--metro-at"]=5 ["--metro-for"]=40 ["--seat"]=seat_front_right ["--driver-npc"]=Character.x
  # A flag without a value: followed by another flag that takes none.
  ["--scene"]=--check ["--ride-along"]=--check ["--play-loop"]=--check
  ["--play"]=x.tplay ["--play-offset"]="1,2,3" ["--save-play"]=x.tplay ["--record-session"]=x.tsession ["--play-session"]=x.tsession
  ["--travel-at"]=15 ["--travel-to"]="-915,1879.5,36.3" ["--travel-point"]=FastTravel.x
  ["--offer-regroup"]=10 ["--regroup-answer"]=no
  ["--ask-skip"]=8 ["--skip-hours"]=2 ["--skip-reason"]=sleep ["--skip-answer"]=no
  ["--activity"]=workspot ["--activity-at"]=4 ["--activity-for"]=10
  ["--latency"]=120 ["--jitter"]=40 ["--loss"]=5 ["--reorder"]=2.5 ["--burst"]=3
  ["--traffic-report"]=--check ["--legacy-wire"]=--check
  ["--drop-item"]=Items.FirstAidWhiffV0 ["--loot-quest"]=Keycards.x ["--take-items"]=2 ["--remove-corpses"]=5
  ["--go-down"]=10 ["--go-out"]=10 ["--revive"]=3
  ["--path"]=x.path ["--save-path"]=x.path
  ["--drop"]=0.2 ["--dup"]=0.2 ["--fault-seed"]=7 ["--vanish-at"]=20
  ["--held-record"]=Items.x:Wea_Handgun:Weapon ["--vehicle-record"]=Vehicle.x:Car:2 ["--look-piece"]=Items.x:Clo_Legs
  ["--npc-record"]=Character.x:Human:Female ["--unknown"]="Items.x,Vehicle.x"
  ["--car-damage"]=5 ["--exit-at"]=8 ["--exit-kind"]=knocked ["--bump"]=3 ["--shoot-cars"]=4
  ["--gunner"]=5 ["--gunner-seat"]=seat_back_left ["--ram-npcs"]=3 ["--chase"]=5
  ["--device"]="1a2b3c4d5e6f7788:DoorControllerPS:open,unlocked@8" ["--device-answer"]=closed ["--device-after"]=2
  ["--breach"]="a0b1c2d3e4f50617:AccessPointControllerPS@4" ["--programs"]="NetworkCameraShutdown,0x1234" ["--breach-failed"]=--check
  ["--interact"]="5eed00000000aa01:QuestCustomAction:0x1234@3" ["--interact-answer"]=host-only
  ["--region-far"]=1000 ["--region-npcs"]=3 ["--region-record"]=Character.x ["--region-walk-at"]=10
  ["--region-leave-at"]=20 ["--region-leave-linked"]=3 ["--region-speed"]=25 ["--region-cars"]=2
  ["--region-driver"]=--check ["--region-chase"]=--check
  ["--region-at"]="1,2,3"
  ["--traffic"]=4 ["--traffic-vehicle"]=Vehicle.x ["--wreck-at"]=10 ["--ram-traffic"]=3 ["--steal-at"]=12
  ["--heat"]=3 ["--heat-at"]=5 ["--heat-for"]=20 ["--crime-at"]=8
  ["--hurt"]=4 ["--melee"]=5 ["--menu-at"]=10 ["--react-npcs"]=3
  ["--hold"]=Items.x ["--holster-at"]=10
  ["--level"]=30 ["--npc-level"]=25 ["--lend"]=owner ["--unlock-vehicle"]=Vehicle.x ["--vendor"]="Character.x@1,2,3#1a2b3c4d5e6f7788" ["--vendor-dies"]=10 ["--trade-offer"]=--check ["--trade-accept"]=--check
  ["--trade-give"]=Items.CommonMaterial1:2 ["--trade-money"]=100 ["--trade-asks"]=50
  ["--link"]=ask ["--link-at"]=5 ["--unlink-at"]=9 ["--ping"]=4 ["--tag"]=6 ["--reveal"]=8 ["--waypoint"]="-915,1879.5,36.3" ["--waypoint-clear"]=20 ["--mark-target"]=nearest ["--hack-npc"]=nearest
)
flags=$("$peer" --help 2>&1 | grep -oE '^  --[a-z-]+' | tr -d ' ' | sort -u) || true
[ -n "$flags" ] || {
  echo "no options in the usage text" >&2
  exit 1
}
status=0
for flag in $flags; do
  if [ -z "${value[$flag]:-}" ]; then
    echo "$flag: listed in the usage, no test value here (add one)" >&2
    status=1
  elif ! "$peer" host --check "$flag" "${value[$flag]}" >/dev/null 2>&1; then
    echo "$flag: listed in the usage, rejected by the parser" >&2
    status=1
  fi
done
exit $status
