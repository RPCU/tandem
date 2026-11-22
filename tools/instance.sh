#!/usr/bin/env bash
# More games on the game machine, apart from the player's: each instance has
# its own copy of the game folder (the archives linked, not copied), its own
# Proton prefix with one test save, and runs outside Steam. Two of them, one
# hosting and one joining over loopback, put two real games against each
# other (docs/architecture/two-instances.md).
#
# Tried on anya (2026-11-19): one instance plays and hosts the mock player;
# two play a session over loopback (results in docs/architecture/two-instances.md).
#
#   tools/instance.sh [-n] VERB [NAME] [ARGS...]
#
#   create NAME [SAVE]   build the instance from the player's install, read
#                        only: the game folder (archive/ linked or reflinked,
#                        the rest copied), a copy of the prefix without its
#                        saves, and one save (SAVE, a folder name such as
#                        ManualSave-12; default: the newest)
#   sync NAME            bring the instance's game folder up to date with the
#                        player's (after a game or mod update)
#   role NAME off|host|join [ADDRESS] [KEY=VALUE...]
#                        the instance's tandem.ini (join: 127.0.0.1 by default)
#   settings NAME        light settings for a test game (low textures, no
#                        sound); create does it
#   start NAME           launch it (Proton, outside Steam, in gamescope when
#                        there is one, in a sandbox that cannot write the
#                        player's install, prefix or Steam cloud folder)
#   load NAME            once started, from the menu to a loaded save (a click
#                        through its own X server; tandem's log tells when)
#   stop NAME            kill this instance's processes, and only those
#   destroy NAME         stop it and delete its directory
#   status               the instances, what runs, the memory left
#   who                  each running game and whose it is: "ours (steam,
#                        started by game.sh)", "ours (instance a)" or "the
#                        player's" (changes nothing)
#   dir NAME            its game folder, for the other tools:
#                        GAME_DIR="$(tools/instance.sh dir b)" task deploy:files
#   cmd NAME TEXT...     a tandem command for it (its own tandem.cmd)
#   log NAME [N]         the last N lines of its tandem log (default 40)
#   stats NAME           its memory, VRAM, CPU and frame rate now
#   bt NAME              every thread's stack, to its run/bt.txt (a frozen game)
#   xdo NAME ARGS...     xdotool on the instance's own X server (gamescope's):
#                        keys and clicks reach this game only, focused or not
#                        (xdo b key Return; xdo b mousemove 640 360 click 1)
#   shot NAME FILE       a screenshot of the instance's own screen (PNG), to
#                        FILE inside the instance's run/ folder
#
#   -n   dry run: print every command instead of running it. What it only
#        reads (does the game run, which Proton, how much room) it still
#        reads, on the machine the game is on: from another machine that is
#        an ssh connection, like every other verb.
#
# It runs on the machine the game is on. From elsewhere (GAME_HOST, default
# anya) it sends itself there over ssh; GAME_HOST=local runs it here.
#
# What it promises:
# - It changes nothing while the player's game runs: create, sync, role,
#   settings, start, cmd and destroy refuse (stop only frees the machine for
#   that game). The player's is any game that carries no instance's mark and
#   is not the Steam game tools/game.sh started and recorded (its pid and
#   start time in ~/.cache/tandem/pilot/main/game.owner), nor a launcher or
#   child of that one; a game under a minute old is waited for (a minute at
#   most) until it is recorded. INSTANCE_ALLOW_FOREIGN=1, by hand only, says
#   any running game is our own.
# - It writes only under the instance's directory, TANDEM_INSTANCES/NAME
#   (checked before every write); the player's install and prefix are read.
# - It never kills by name: only processes carrying this instance's mark
#   (TANDEM_INSTANCE_ID in their environment).
#
# Environment (all optional):
#   GAME_DIR              the player's install (/data/Steam/steamapps/common/Cyberpunk 2077)
#   TANDEM_INSTANCES      where instances live (a sibling of the Steam library:
#                         /data/Steam/tandem-instances; the same filesystem as the
#                         game makes reflinks possible)
#   USER_COMPATDATA       the player's prefix (<library>/steamapps/compatdata/1091500)
#   STEAM_ROOT            the Steam client's folder (~/.local/share/Steam)
#   PROTON_DIR            the Proton to use (default: the one the player's prefix names)
#   INSTANCE_MIRROR       auto | reflink | link | copy: how archive/ gets there
#   INSTANCE_PREFIX       copy | fresh: start from the player's prefix (its
#                         runtimes and settings) or from an empty one
#   INSTANCE_LAUNCHER     auto | proton | umu
#   INSTANCE_RUNTIME      the Steam Linux Runtime's folder, or none
#   INSTANCE_FHS          auto | none: steam-run around Proton when there is
#                         one (NixOS: Steam's programs need its FHS environment)
#   INSTANCE_DISPLAY      auto | gamescope | headless | window
#   INSTANCE_SIZE         1280x720     INSTANCE_FPS  30
#   INSTANCE_SANDBOX      1 | 0: the bubblewrap guard around the game
#   INSTANCE_HIDE_STEAM   1 | 0: the Steam client is out of the game's sight
#   INSTANCE_SCOPE        1 | 0: a systemd scope with a memory limit
#   INSTANCE_MEMORY_MAX   14G          INSTANCE_MIN_FREE_MB  12000
#   INSTANCE_CPU_WEIGHT   100: the scope's CPU weight. Builds over ssh run in
#                         session scopes beside the user manager holding every
#                         instance: their nice does not reach across cgroups
#   INSTANCE_CPUS         the cores the game may use, as taskset -c takes them
#                         (6-11; default: all). The user manager's scopes get
#                         no cpuset controller, so not AllowedCPUs
#   INSTANCE_PORT         27450 (role)
#   INSTANCE_GAME_ARGS    arguments for Cyberpunk2077.exe (none)
#   INSTANCE_ENV          more VAR=VALUE for the game, space separated
#   INSTANCE_SETTINGS     1 | 0: create applies settings (INSTANCE_TEXTURES Low,
#                         INSTANCE_VOLUME 0)
#   INSTANCE_PAD          the USB product id of the one pad the game reads
#                         (Microsoft's vendor 045e; default: the first of
#                         02d1 02dd 02ea 0b12 028f no other instance has).
#                         The test pilot's pad for this instance
#                         (docs/architecture/autopilot.md); the player's own
#                         pads, Sunshine's and the main game's pilot pad
#                         (028e) never reach it
set -euo pipefail

dry=0
while [ $# -gt 0 ]; do
  case $1 in
  -n | --dry-run)
    dry=1
    shift
    ;;
  -h | --help)
    sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'
    exit 0
    ;;
  *) break ;;
  esac
done
verb=${1:-}
[ -n "$verb" ] || {
  sed -n '11,45p' "$0" >&2
  exit 2
}
shift

# Elsewhere than on the game machine: send this script there, with the
# settings of this environment.
host=${GAME_HOST:-anya}
if [ "$host" != local ]; then
  forward=(GAME_HOST=local)
  for variable in GAME_DIR TANDEM_INSTANCES USER_COMPATDATA STEAM_ROOT PROTON_DIR INSTANCE_MIRROR \
    INSTANCE_PREFIX INSTANCE_LAUNCHER INSTANCE_RUNTIME INSTANCE_FHS INSTANCE_DISPLAY INSTANCE_SIZE INSTANCE_FPS \
    INSTANCE_SANDBOX INSTANCE_HIDE_STEAM INSTANCE_SCOPE INSTANCE_MEMORY_MAX INSTANCE_CPU_WEIGHT INSTANCE_CPUS INSTANCE_MIN_FREE_MB \
    INSTANCE_PORT INSTANCE_GAME_ARGS INSTANCE_ENV INSTANCE_PAD INSTANCE_SETTINGS INSTANCE_TEXTURES INSTANCE_VOLUME INSTANCE_ALLOW_FOREIGN; do
    [ -z "${!variable+set}" ] || forward+=("$variable=${!variable}")
  done
  options=()
  [ "$dry" = 0 ] || options+=(-n)
  # shellcheck disable=SC2029 # expanded here on purpose, quoted for the other side
  exec ssh "$host" "$(printf '%q ' env "${forward[@]}" bash -s -- "${options[@]}" "$verb" "$@")" <"$0"
fi

appid=1091500
game=${GAME_DIR:-/data/Steam/steamapps/common/Cyberpunk 2077}
game=${game%/}
# The harness exports GAME_DIR as the game it tests: an instance's own game
# folder is not the player's install (taken for it, the sandbox bound the
# instance's game read only and RED4ext could not write its log).
case $game in
*/tandem-instances/*/game | "${TANDEM_INSTANCES:-/nonexistent}"/*/game) game='/data/Steam/steamapps/common/Cyberpunk 2077' ;;
esac
library=$(dirname "$(dirname "$(dirname "$game")")")
root=${TANDEM_INSTANCES:-$library/tandem-instances}
user_prefix=${USER_COMPATDATA:-$library/steamapps/compatdata/$appid}
steam_root=${STEAM_ROOT:-$HOME/.local/share/Steam}
saves_in_prefix='pfx/drive_c/users/steamuser/Saved Games/CD Projekt Red/Cyberpunk 2077'

die() {
  echo "instance: $*" >&2
  exit 1
}
note() { echo "instance: $*" >&2; }

# Every command that changes something goes through here.
run() {
  if [ "$dry" = 1 ]; then
    printf '+'
    printf ' %q' "$@"
    printf '\n'
  else
    "$@"
  fi
}

# --- the instance, and the only place we write -------------------------------

name='' inst=''
instance() {
  name=${1:-}
  [[ $name =~ ^[a-z0-9][a-z0-9_-]{0,15}$ ]] || die "an instance name: a-z, 0-9, - and _, at most 16 (got '${name}')"
  inst=$root/$name
}

# Fails unless every path is inside this instance's directory once links
# are followed. Called before each write, so a link planted in the
# instance cannot send a write elsewhere.
inside() {
  local path real base
  base=$(realpath -m -- "$inst")
  [ "$base" = "$(realpath -m -- "$root")/$name" ] || die "$inst is not $root/$name once resolved"
  for path in "$@"; do
    real=$(realpath -m -- "$path")
    case $real/ in
    "$base"/*) ;;
    *) die "refusing to write $path: outside $inst" ;;
    esac
  done
}

exists() { [ -f "$inst/.tandem-instance" ] || die "no instance $name in $root (create it first)"; }

# write PATH: stdin becomes the file.
write() {
  inside "$1"
  if [ "$dry" = 1 ]; then
    echo "+ write $1:"
    sed 's/^/+   /'
  else
    mkdir -p "$(dirname "$1")"
    cat >"$1"
  fi
}

# A program: the machine's own, else nixpkgs' (NixOS: built once and kept by
# a GC root in the instance's run/; nothing is installed on the system).
tool() {
  local program=$1 package=${2:-$1} link
  if command -v "$program" >/dev/null; then
    command -v "$program"
    return
  fi
  command -v nix >/dev/null || return 1
  link=$inst/run/nix-$package
  # A package with several outputs links each (nix-jq-bin, nix-jq-man).
  if [ -x "$link-bin/bin/$program" ]; then
    echo "$link-bin/bin/$program"
    return
  fi
  if [ ! -x "$link/bin/$program" ]; then
    inside "$link"
    if [ "$dry" = 1 ]; then
      note "dry run: would nix build nixpkgs#$package -o $link"
    else
      mkdir -p "$inst/run"
      nix --extra-experimental-features 'nix-command flakes' build "nixpkgs#$package" -o "$link" >&2 || return 1
    fi
  fi
  if [ -x "$link-bin/bin/$program" ]; then echo "$link-bin/bin/$program"; else echo "$link/bin/$program"; fi
}

# --- whose game is running ---------------------------------------------------

# A game (or a launcher naming it) is ours when it carries an instance's
# mark, or when it is the Steam game tools/game.sh started: the pid in its
# record, whose kernel start time is the one written beside it (a pid reused
# since does not match), with the launchers above it (Steam's reaper,
# Proton) and what it started. Every other one is the player's.
steam_record=$HOME/.cache/tandem/pilot/main/game.owner

# The instance a process belongs to ("" for a game we did not start).
mark_of() { tr '\0' '\n' <"/proc/$1/environ" 2>/dev/null | sed -n 's/^TANDEM_INSTANCE_ID=//p' | head -1; }

# /proc/PID/stat's field N past "PID (comm)": 2 the parent, 20 the start
# time (clock ticks since boot).
stat_field() { sed 's/.*) //' "/proc/$1/stat" 2>/dev/null | awk -v n="$2" '{ print $n }'; }

# The game's own process (Wine's "Z:\...\Cyberpunk2077.exe" as its first
# argument), not a launcher that names it further on.
is_game() {
  local first='' re='^[a-z]:.*\\cyberpunk2077\.exe$'
  IFS= read -r -d '' first 2>/dev/null <"/proc/$1/cmdline" || [ -n "$first" ] || return 1
  first=${first,,}
  [[ $first =~ $re ]]
}

# The Steam game tools/game.sh recorded, while it runs: its pid, or nothing.
steam_game() {
  local pid='' start=''
  [ -r "$steam_record" ] || return 0
  read -r pid start <"$steam_record" || true
  [[ $pid =~ ^[0-9]+$ ]] && [ -n "$start" ] && [ "$(stat_field "$pid" 20)" = "$start" ] && echo "$pid"
  return 0
}

# under PID ANCESTOR: ANCESTOR is PID or one of its parents.
under() {
  local p=$1
  while [ -n "$p" ] && [ "$p" -gt 1 ]; do
    [ "$p" = "$2" ] && return 0
    p=$(stat_field "$p" 2)
  done
  return 1
}

# whose PID STEAM (steam_game's answer): "instance NAME", "steam" or nothing
# (the player's).
whose() {
  local mark
  mark=$(mark_of "$1")
  if [ -n "$mark" ]; then
    echo "instance ${mark##*/}"
  elif [ -n "$2" ] && { under "$1" "$2" || under "$2" "$1"; }; then
    echo steam
  fi
  return 0
}

# Games (and their launchers) that are not ours: the player's.
foreign() {
  local pid steam
  steam=$(steam_game)
  for pid in $(pgrep -f '[C]yberpunk2077\.exe' || true); do
    [ -e "/proc/$pid" ] || continue
    [ -n "$(whose "$pid" "$steam")" ] || echo "$pid"
  done
}

# Seconds since a process started.
age_of() {
  local ticks
  ticks=$(stat_field "$1" 20)
  awk -v t="${ticks:-0}" -v hz="$(getconf CLK_TCK)" '{ print int($1 - t / hz) }' /proc/uptime
}

# A game started less than a minute ago may be a Steam game tools/game.sh
# has not recorded yet (it does once the game's own process exists, within
# seconds): waited for, a minute at most, then judged.
refuse_if_foreign() {
  local pids pid young waited=0
  while :; do
    pids=$(foreign | tr '\n' ' ')
    pids=${pids% }
    [ -n "$pids" ] || return 0
    young=''
    for pid in $pids; do
      [ "$(age_of "$pid")" -ge 60 ] || young=1
    done
    [ -n "$young" ] && [ "$waited" -lt 60 ] || break
    [ "$waited" -gt 0 ] || note "a game started under a minute ago (pid $pids): waiting to see whose it is"
    sleep 2
    waited=$((waited + 2))
  done
  if [ "${INSTANCE_ALLOW_FOREIGN:-0}" = 1 ]; then
    note "a game we did not start runs (pid $pids): allowed by INSTANCE_ALLOW_FOREIGN"
    return 0
  fi
  die "a game we did not start is running (pid $pids): the player's (tools/instance.sh who). Nothing done."
}

# Every running game and whose it is; changes nothing. The launchers of our
# games are left out, the player's are named.
who() {
  local pid steam owner any=''
  steam=$(steam_game)
  for pid in $(pgrep -f '[C]yberpunk2077\.exe' || true); do
    [ -e "/proc/$pid" ] || continue
    owner=$(whose "$pid" "$steam")
    case $owner in
    steam) owner="ours (steam, started by game.sh)" ;;
    instance*) owner="ours ($owner)" ;;
    *) owner="the player's" ;;
    esac
    if is_game "$pid"; then
      echo "pid $pid: $owner"
    elif [ "$owner" = "the player's" ]; then
      echo "pid $pid (a launcher): $owner"
    else
      continue
    fi
    any=1
  done
  [ -n "$any" ] || echo "no game running"
}

# This instance's processes: the exact mark in their environment.
pids() {
  local file
  for file in $(grep -lzxF -- "TANDEM_INSTANCE_ID=$inst" /proc/[0-9]*/environ 2>/dev/null || true); do
    file=${file#/proc/}
    echo "${file%/environ}"
  done
}

# --- create ------------------------------------------------------------------

# True when a file of the game can be reflinked into the instance: a copy
# that shares its blocks (btrfs, xfs), free and independent.
reflinks() {
  [ "$dry" = 0 ] || return 1
  local probe=$inst/.reflink-probe ok=1
  inside "$probe"
  cp --reflink=always -- "$game/bin/x64/Cyberpunk2077.exe" "$probe" 2>/dev/null && ok=0
  rm -f -- "$probe"
  return $ok
}

mirror_game() {
  [ -f "$game/bin/x64/Cyberpunk2077.exe" ] || die "no game in $game"
  inside "$inst/game"
  run mkdir -p "$inst/game"
  # Everything but the archives: real copies, so nothing the game or a mod
  # writes (the script cache, logs, mod settings) can reach the player's
  # files. Left alone on the instance's side: its logs and its tandem.
  run rsync -a --delete \
    --exclude=/archive/ \
    --exclude=/r6/logs/ --exclude=/red4ext/logs/ --exclude='*.log' \
    --exclude=/red4ext/plugins/tandem/ --exclude=/r6/scripts/tandem/ --exclude=/r6/tweaks/tandem/ \
    --exclude=/bin/x64/steam_appid.txt --exclude=/bin/x64/GameServicesSteam.dll \
    "$game/" "$inst/game/"
  # The game's Steam services: with them and no Steam client the game exits
  # at start (code 3) or hangs on its logo. Without them it runs with no
  # platform (no achievements, no Steam cloud: what we want).
  inside "$inst/game/bin/x64/GameServicesSteam.dll"
  run rm -f -- "$inst/game/bin/x64/GameServicesSteam.dll"

  # The archives (most of the game's size) are only ever read.
  local how=${INSTANCE_MIRROR:-auto}
  if [ "$how" = auto ]; then
    if reflinks; then how="reflink"; else how="link"; fi
    [ "$dry" = 0 ] || note "dry run: reflinks not probed, showing symbolic links"
  fi
  inside "$inst/game/archive"
  run rm -rf --one-file-system -- "$inst/game/archive"
  case $how in
  reflink) run cp -a --reflink=always -- "$game/archive" "$inst/game/archive" ;;
  copy) run cp -a --reflink=auto -- "$game/archive" "$inst/game/archive" ;;
  link) run cp -as -- "$game/archive" "$inst/game/archive" ;;
  *) die "INSTANCE_MIRROR: auto, reflink, link or copy" ;;
  esac
  note "archives: $how"
}

create() {
  local save=${1:-}
  refuse_if_foreign
  [ ! -e "$inst" ] || die "$inst exists already (sync it, or destroy it first)"
  [ -f "$game/bin/x64/Cyberpunk2077.exe" ] || die "no game in $game"
  local saves=$user_prefix/$saves_in_prefix
  if [ -z "$save" ] && [ -d "$saves" ]; then
    # The newest save's folder.
    save=$(find "$saves" -mindepth 1 -maxdepth 1 -type d -printf '%T@ %f\n' | sort -rn | head -1 | cut -d' ' -f2-)
  fi
  [ -z "$save" ] || [ -d "$saves/$save" ] || die "no save $save in $saves"

  # Room for what is copied: everything but the archives, and the prefix.
  local need avail
  need=$(du -sm --exclude=archive "$game" 2>/dev/null | cut -f1)
  [ ! -d "$user_prefix" ] || need=$((need + $(du -sm "$user_prefix" 2>/dev/null | cut -f1)))
  avail=$(df -m --output=avail "$(dirname "$root")" 2>/dev/null | tail -1 | tr -d ' ')
  note "copies: about ${need:-?} MB; free at $(dirname "$root"): ${avail:-?} MB"
  [ -z "$avail" ] || [ -z "$need" ] || [ "$avail" -gt $((need + 2048)) ] || die "not enough room"

  inside "$inst/.tandem-instance"
  run mkdir -p "$inst/run"
  printf 'name = %s\nfrom = %s\nprefix = %s\nsave = %s\ncreated = %s\n' "$name" "$game" "$user_prefix" "${save:-none}" "$(date -Is)" |
    write "$inst/.tandem-instance"
  mirror_game

  inside "$inst/compatdata"
  if [ "${INSTANCE_PREFIX:-copy}" = copy ] && [ -d "$user_prefix/pfx" ]; then
    # The player's prefix as a start: its runtimes (what RED4ext and the mods
    # need) and the game's settings. Not its saves, not its crash reports.
    run rsync -a \
      --exclude="/$saves_in_prefix/*" \
      --exclude='/pfx/drive_c/users/steamuser/AppData/Local/REDEngine/ReportQueue/' \
      "$user_prefix/" "$inst/compatdata/"
  else
    note "an empty prefix: Proton fills it at the first start (the mods' runtimes are then missing)"
    run mkdir -p "$inst/compatdata"
  fi
  inside "$inst/compatdata/$saves_in_prefix"
  run mkdir -p "$inst/compatdata/$saves_in_prefix"
  # The folder's own files (user.gls: the first-run answers, the user
  # agreement; EnabledMods.json, HDRUserSettings.json) are settings, not saves.
  # Not steam_autocloud.vdf: Steam's mark of a cloud folder.
  local file
  if [ -d "$saves" ]; then
    while IFS= read -r -d '' file; do
      run cp -a -- "$file" "$inst/compatdata/$saves_in_prefix/"
    done < <(find "$saves" -mindepth 1 -maxdepth 1 -type f ! -name steam_autocloud.vdf -print0)
  fi
  if [ -n "$save" ]; then
    run cp -a -- "$saves/$save" "$inst/compatdata/$saves_in_prefix/$save"
    note "save: $save"
  else
    note "no save copied: the instance starts on the main menu"
  fi
  role_file off
  [ "${INSTANCE_SETTINGS:-1}" = 0 ] || settings
  note "created $inst"
  note "next: GAME_DIR=\"$inst/game\" task deploy:files; tools/instance.sh role $name host; tools/instance.sh start $name"
}

sync_game() {
  exists
  refuse_if_foreign
  [ -z "$(pids)" ] || die "$name is running: stop it first"
  mirror_game
}

# --- tandem.ini --------------------------------------------------------------

role_file() {
  local role=${1:-} address=127.0.0.1 text extra
  shift || true
  case $role in
  off | host) ;;
  join)
    if [ $# -gt 0 ] && [[ $1 != *=* ]]; then
      address=$1
      shift
    fi
    ;;
  *) die "role: off, host or join [ADDRESS], then KEY=VALUE..." ;;
  esac
  text="role = $role"$'\n'
  [ "$role" != join ] || text+="address = $address"$'\n'
  text+="port = ${INSTANCE_PORT:-27450}"$'\n'"name = $name"$'\n'
  # log = debug, as tools/game.sh role writes: scenarios read per-puppet,
  # per-hit lines (plugin/src/log.hpp); a KEY=VALUE below can change it.
  text+="log = debug"$'\n'
  # A test game never saves: one that hosted autosaved in the fight that
  # killed its V, and loaded that save at every start after (2026-11-22).
  text+="saves = off"$'\n'
  for extra in "$@"; do
    [[ $extra == *=* ]] || die "not KEY=VALUE: $extra"
    text+="${extra%%=*} = ${extra#*=}"$'\n'
  done
  printf '%s' "$text" | write "$inst/game/red4ext/plugins/tandem/tandem.ini"
  note "role: $role"
}

# --- the instance's pad --------------------------------------------------------

# The one pad this instance's game reads: kept in .tandem-instance ("pad =
# 0x02d1"), chosen once, so the test pilot (which reads it there) makes
# that pad and no other game takes it.
pad_of() {
  local pad other used=''
  pad=$(sed -n 's/^pad = //p' "$inst/.tandem-instance" | head -1)
  if [ -n "${INSTANCE_PAD:-}" ]; then
    pad=$(printf '0x%04x' "$((INSTANCE_PAD))")
  fi
  if [ -z "$pad" ]; then
    for other in "$root"/*/.tandem-instance; do
      [ -f "$other" ] && [ "$other" != "$inst/.tandem-instance" ] || continue
      used+=" $(sed -n 's/^pad = //p' "$other" | head -1)"
    done
    for pad in 0x02d1 0x02dd 0x02ea 0x0b12 0x028f none; do
      [[ " $used " == *" $pad "* ]] || break
    done
    [ "$pad" != none ] || die "every pad id is taken by another instance: set INSTANCE_PAD"
  fi
  [[ $pad =~ ^0x[0-9a-f]{4}$ ]] && [ "$pad" != 0x028e ] || die "pad $pad: four hex digits, not 0x028e (the main game's)"
  if ! grep -qx "pad = $pad" "$inst/.tandem-instance"; then
    # (Its dry-run lines go to stderr: stdout is the answer.)
    {
      sed '/^pad = /d' "$inst/.tandem-instance"
      echo "pad = $pad"
    } | write "$inst/.tandem-instance.new" >&2
    inside "$inst/.tandem-instance"
    run mv -- "$inst/.tandem-instance.new" "$inst/.tandem-instance" >&2
  fi
  echo "$pad"
}

# --- the game's settings ------------------------------------------------------

# Light settings for a test game (two share one GPU), in the instance's own
# UserSettings.json: low textures, no sound, and the resolution of its
# gamescope screen (else the game keeps the player's shape inside it:
# 1152x720, a window 64 pixels in, and xdo's coordinates shifted).
# INSTANCE_TEXTURES, INSTANCE_VOLUME, INSTANCE_SIZE.
settings() {
  local file="$inst/compatdata/pfx/drive_c/users/steamuser/AppData/Local/CD Projekt Red/Cyberpunk 2077/UserSettings.json"
  local textures=${INSTANCE_TEXTURES:-Low} volume=${INSTANCE_VOLUME:-0} size=${INSTANCE_SIZE:-1280x720} jq
  if [ ! -f "$file" ]; then
    note "no UserSettings.json yet: the game's defaults (run settings again after a first start)"
    return 0
  fi
  inside "$file"
  jq=$(tool jq) || die "no jq"
  if [ "$dry" = 1 ]; then
    echo "+ $jq: TextureQuality=$textures MasterVolume=$volume Resolution=$size in $file"
    return 0
  fi
  # shellcheck disable=SC2016 # jq variables, not the shell's
  "$jq" --arg textures "$textures" --argjson volume "$volume" --arg size "$size" '
    .data |= map(.options |= map(
      if .name == "TextureQuality" and (.values | index($textures)) != null
      then .value = $textures | .index = (.values | index($textures))
      elif .name == "MasterVolume" then .value = $volume
      elif .name == "Resolution" then .value = $size
      else . end))' "$file" >"$file.new" || {
    rm -f -- "$file.new"
    die "settings: $jq failed; $file left as it was"
  }
  mv -- "$file.new" "$file"
  note "settings: textures $textures, volume $volume, resolution $size"
}

# --- start -------------------------------------------------------------------

# The Proton the player's game runs with: its prefix names the files it
# was set up from (config_info: the version, then paths inside Proton).
proton_dir() {
  local dir=${PROTON_DIR:-}
  if [ -z "$dir" ] && [ -f "$user_prefix/config_info" ]; then
    dir=$(sed -n '2,$p' "$user_prefix/config_info" | sed -n 's#/\(files\|dist\)/.*##p' | head -1)
  fi
  [ -n "$dir" ] && [ -f "$dir/proton" ] || die "no Proton found (set PROTON_DIR to the folder holding the proton script)"
  realpath -- "$dir" # not through ~/.steam, which the sandbox hides
}

# The graphical session's environment, for a start over ssh: taken from a
# process of the session (Steam's, else anything started under Sway).
session_env() {
  [ -z "${WAYLAND_DISPLAY:-}${DISPLAY:-}" ] || return 0
  local pid file
  pid=$(pgrep -u "$(id -u)" -f 'steam\.sh' | head -1 || true)
  if [ -z "$pid" ]; then
    for file in $(grep -lz '^SWAYSOCK=' /proc/[0-9]*/environ 2>/dev/null || true); do
      grep -qz '^WAYLAND_DISPLAY=' "$file" 2>/dev/null || continue
      pid=${file#/proc/}
      pid=${pid%/environ}
      break
    done
  fi
  [ -n "$pid" ] || {
    note "no graphical session found: starting without WAYLAND_DISPLAY and DISPLAY"
    return 0
  }
  local line
  while IFS= read -r line; do
    export "${line?}"
  done < <(tr '\0' '\n' <"/proc/$pid/environ" | grep -E '^(DISPLAY|WAYLAND_DISPLAY|XDG_RUNTIME_DIR|DBUS_SESSION_BUS_ADDRESS|SWAYSOCK)=')
}

start() {
  exists
  refuse_if_foreign
  [ -z "$(pids)" ] || die "$name is running already"
  local exe=$inst/game/bin/x64/Cyberpunk2077.exe
  [ -f "$exe" ] || die "no game in $inst/game"
  [ -f "$inst/game/red4ext/plugins/tandem/tandem.dll" ] ||
    note "tandem is not deployed there: GAME_DIR=\"$inst/game\" task deploy:files"

  # Room for one more game.
  local free
  free=$(awk '/^MemAvailable:/ { print int($2 / 1024) }' /proc/meminfo)
  [ "$free" -ge "${INSTANCE_MIN_FREE_MB:-12000}" ] ||
    die "only $free MB of memory available (INSTANCE_MIN_FREE_MB=${INSTANCE_MIN_FREE_MB:-12000})"

  # With an app id the game would sign in to the running Steam client as the
  # player: playtime, achievements, the cloud. The copy has no such file.
  if [ -e "$inst/game/bin/x64/steam_appid.txt" ]; then
    inside "$inst/game/bin/x64/steam_appid.txt"
    run rm -f -- "$inst/game/bin/x64/steam_appid.txt"
  fi

  local size=${INSTANCE_SIZE:-1280x720} fps=${INSTANCE_FPS:-30}
  local width=${size%x*} height=${size#*x}
  local environment=(
    "TANDEM_INSTANCE_ID=$inst"
    "STEAM_COMPAT_DATA_PATH=$inst/compatdata"
    "STEAM_COMPAT_CLIENT_INSTALL_PATH=$steam_root"
    # The Steam Linux Runtime shows the game only the folders it is told of
    # (else: the working directory alone, and the game quits without a word).
    "STEAM_COMPAT_INSTALL_PATH=$inst/game"
    "PRESSURE_VESSEL_FILESYSTEMS_RW=$inst"
    "PRESSURE_VESSEL_FILESYSTEMS_RO=$game"
    # RED4ext (winmm.dll), the ASI loader (version.dll) and upscaler mods
    # (dxgi.dll) are DLLs next to the exe; absent ones fall back to Wine's.
    # lsteamclient off: the game must not reach the player's Steam client.
    'WINEDLLOVERRIDES=winmm,version,dxgi=n,b;lsteamclient=d'
    # A game with no Steam id for Proton (its "umu" path: started through
    # umu.exe, no Steam bridge); the GE/CachyOS Protons know this one.
    UMU_ID=0
    # The player's launch option (their prefix's FSR4 files stay as they are).
    PROTON_FSR4_UPGRADE=0
    # GE and CachyOS Protons look their fixes up by Steam app id, which the
    # instance does not have (the game has no fix of its own there).
    PROTONFIXES_DISABLE=1
    "VKD3D_FRAME_RATE=$fps"
    "VKD3D_SHADER_CACHE_PATH=$inst/run"
  )
  # One pad only: the instance's (SDL inside Proton's winebus reads the hint).
  local pad
  pad=$(pad_of)
  environment+=("SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x045e/$pad")
  local extra
  for extra in ${INSTANCE_ENV:-}; do environment+=("$extra"); done
  # No Steam identity: nothing the game could report to the client.
  local forget=(-u SteamAppId -u SteamGameId -u SteamOverlayGameId -u STEAM_COMPAT_APP_ID
    -u SteamClientLaunch -u SteamEnv -u ENABLE_VK_LAYER_VALVE_steam_overlay_1)

  # Proton, as Steam would run it, without Steam.
  local launch=() args=()
  read -r -a args <<<"${INSTANCE_GAME_ARGS:-}"
  local launcher=${INSTANCE_LAUNCHER:-auto}
  if [ "$launcher" = auto ]; then
    if [ -n "${PROTON_DIR:-}" ] || [ -f "$user_prefix/config_info" ]; then
      launcher=proton
    elif command -v umu-run >/dev/null; then
      launcher=umu
    else launcher=proton; fi
  fi
  case $launcher in
  proton)
    local proton runtime=${INSTANCE_RUNTIME:-}
    proton=$(proton_dir)
    if [ -z "$runtime" ]; then
      # The runtime this Proton asks for (toolmanifest.vdf names its app),
      # wherever Steam installed it; else sniper.
      local tool_app manifest folder
      tool_app=$(sed -n 's/.*"require_tool_appid"[[:space:]]*"\([0-9]*\)".*/\1/p' "$proton/toolmanifest.vdf" 2>/dev/null | head -1)
      runtime=none
      for folder in "$library" "$steam_root"; do
        manifest=$folder/steamapps/appmanifest_${tool_app:-1628350}.acf
        [ -f "$manifest" ] || continue
        runtime=$folder/steamapps/common/$(sed -n 's/.*"installdir"[[:space:]]*"\(.*\)".*/\1/p' "$manifest" | head -1)
        if [ -x "$runtime/run" ]; then break; fi
        runtime=none
      done
      [ "$runtime" != none ] || note "no Steam Linux Runtime found for this Proton (app ${tool_app:-?}): running without"
    fi
    # NixOS: Steam's programs need the FHS environment steam-run gives them.
    if [ "${INSTANCE_FHS:-auto}" = auto ] && command -v steam-run >/dev/null; then launch+=(steam-run); fi
    [ "$runtime" = none ] || launch+=("$(realpath -- "$runtime")/run" --)
    launch+=("$proton/proton" waitforexitandrun "$exe" "${args[@]}")
    ;;
  umu)
    environment+=("WINEPREFIX=$inst/compatdata/pfx" GAMEID=0)
    [ -z "${PROTON_DIR:-}" ] || environment+=("PROTONPATH=$PROTON_DIR")
    launch+=(umu-run "$exe" "${args[@]}")
    ;;
  *) die "INSTANCE_LAUNCHER: auto, proton or umu" ;;
  esac

  # The guard: whatever the game or a mod does, the player's install is read
  # only, and their prefix, Steam cloud folder and Steam client are not there.
  local sandbox=()
  if [ "${INSTANCE_SANDBOX:-1}" = 1 ]; then
    local bwrap
    bwrap=$(tool bwrap bubblewrap) || die "no bwrap (bubblewrap) for the sandbox; INSTANCE_SANDBOX=0 runs without the guard"
    sandbox=("$bwrap" --dev-bind / / --ro-bind "$game" "$game")
    [ ! -d "$user_prefix" ] || sandbox+=(--tmpfs "$user_prefix")
    local cloud
    for cloud in "$steam_root"/userdata/*/"$appid"; do
      [ ! -d "$cloud" ] || sandbox+=(--tmpfs "$cloud")
    done
    if [ "${INSTANCE_HIDE_STEAM:-1}" = 1 ] && [ -d "$HOME/.steam" ] && [ ! -L "$HOME/.steam" ]; then
      sandbox+=(--tmpfs "$HOME/.steam")
    fi
    sandbox+=(--chdir "$inst/game/bin/x64" --)
  else
    note "no sandbox: nothing but the game's own good manners keeps it off the player's files"
  fi

  # A display of its own: gamescope gives the game one screen of the size we
  # want, a frame limit, and input that does not depend on Sway's focus.
  local display=${INSTANCE_DISPLAY:-auto} screen=()
  if [ "$display" = auto ]; then
    if command -v gamescope >/dev/null || command -v nix >/dev/null; then display=gamescope; else display=window; fi
  fi
  local gamescope=''
  [ "$display" = window ] || gamescope=$(tool gamescope) || die "no gamescope (INSTANCE_DISPLAY=window runs without)"
  case $display in
  gamescope) screen=("$gamescope" -w "$width" -h "$height" -W "$width" -H "$height" -r "$fps" --framerate-limit "$fps" --) ;;
  headless) screen=("$gamescope" --backend headless -w "$width" -h "$height" -W "$width" -H "$height" -r "$fps" --framerate-limit "$fps" --) ;;
  window) ;;
  *) die "INSTANCE_DISPLAY: auto, gamescope, headless or window" ;;
  esac

  # A memory limit: a leaking or looping instance dies alone.
  local scope=()
  if [ "${INSTANCE_SCOPE:-1}" = 1 ] && command -v systemd-run >/dev/null; then
    scope=(systemd-run --user --scope --quiet --collect --unit "tandem-instance-$name"
      -p "MemoryMax=${INSTANCE_MEMORY_MAX:-14G}" -p "CPUWeight=${INSTANCE_CPU_WEIGHT:-100}" --)
  fi
  # Cores of its own (two games beside each other): every process it starts
  # inherits the affinity.
  local cpus=()
  if [ -n "${INSTANCE_CPUS:-}" ]; then
    [[ $INSTANCE_CPUS =~ ^[0-9]+([-,][0-9]+)*$ ]] || die "INSTANCE_CPUS: a list such as 6-11 or 0,2,4"
    cpus=("$(tool taskset util-linux)" -c "$INSTANCE_CPUS") || die "no taskset"
  fi

  session_env
  inside "$inst/run/launch.log" "$inst/run/pid"
  local full=(env "${forget[@]}" "${environment[@]}" "${scope[@]}" "${cpus[@]}" "${screen[@]}" "${sandbox[@]}" "${launch[@]}")
  if [ "$dry" = 1 ]; then
    echo "+ cd $(printf '%q' "$inst/game/bin/x64")"
    printf '+ setsid nohup'
    printf ' %q' "${full[@]}"
    printf ' > %q 2>&1 &\n' "$inst/run/launch.log"
    return
  fi
  mkdir -p "$inst/run"
  (
    cd "$inst/game/bin/x64"
    setsid nohup "${full[@]}" >"$inst/run/launch.log" 2>&1 &
    echo $! >"$inst/run/pid"
  )
  # A launcher that dies at once (no display, a missing runtime) says why.
  sleep 2
  if ! kill -0 "$(cat "$inst/run/pid")" 2>/dev/null && [ -z "$(pids)" ]; then
    tail -n 20 "$inst/run/launch.log" >&2 || true
    die "$name did not start (above: the end of $inst/run/launch.log)"
  fi
  note "started $name (pid $(cat "$inst/run/pid"), $display, $launcher); its output: $inst/run/launch.log"
  note "tandem's log: tools/instance.sh log $name"
}

# --- stop, destroy -----------------------------------------------------------

stop() {
  local list scoped=0
  # Its scope first, when it has one: systemd kills the whole group.
  if systemctl --user --quiet is-active "tandem-instance-$name.scope" 2>/dev/null; then
    run systemctl --user stop "tandem-instance-$name.scope" || true
    scoped=1
  fi
  list=$(pids | tr '\n' ' ')
  if [ -z "$list" ]; then
    if [ "$scoped" = 1 ]; then note "stopped $name"; else note "$name is not running"; fi
    return 0
  fi
  # shellcheck disable=SC2086 # a list of pids
  run kill -TERM $list || true
  [ "$dry" = 0 ] || return 0
  for _ in $(seq 1 10); do
    [ -n "$(pids)" ] || break
    sleep 1
  done
  list=$(pids | tr '\n' ' ')
  # shellcheck disable=SC2086
  [ -z "$list" ] || kill -KILL $list 2>/dev/null || true
  for _ in $(seq 1 10); do
    [ -n "$(pids)" ] || break
    sleep 1
  done
  [ -z "$(pids)" ] || die "$name: processes left: $(pids | tr '\n' ' ')"
  note "stopped $name"
}

destroy() {
  [ -e "$inst" ] || die "no instance $name in $root"
  # Only what create made: its marker is there.
  [ -f "$inst/.tandem-instance" ] || die "$inst has no .tandem-instance: not ours to delete"
  [ ! -L "$inst" ] || die "$inst is a link: not ours to delete"
  refuse_if_foreign
  stop
  inside "$inst/game"
  # Links to the player's archives go as links: rm does not follow them.
  run rm -rf --one-file-system -- "$inst"
  note "destroyed $name"
}

# --- look --------------------------------------------------------------------

status() {
  local dir other steam
  other=$(foreign | tr '\n' ' ')
  steam=$(steam_game)
  echo "the player's game (or one we did not start): ${other:-not running}"
  echo "the Steam game tools/game.sh started: $([ -n "$steam" ] && echo "pid $steam" || echo "not running")"
  echo "memory available: $(awk '/^MemAvailable:/ { print int($2 / 1024) }' /proc/meminfo) MB"
  echo "instances in $root:"
  for dir in "$root"/*/; do
    [ -f "$dir.tandem-instance" ] || continue
    instance "$(basename "$dir")"
    echo "  $name: $([ -n "$(pids)" ] && echo "running ($(pids | wc -l) processes)" || echo stopped), role $(sed -n 's/^role *= *//p' "$inst/game/red4ext/plugins/tandem/tandem.ini" 2>/dev/null || true), pad $(sed -n 's/^pad = //p' "$inst/.tandem-instance" | head -1)"
  done
}

# What one instance costs now: the game's memory, the memory of its whole
# scope, its VRAM (the amdgpu driver's per-client counts), its CPU over a
# few seconds, and its last frame rate (when started with
# INSTANCE_ENV=WINEDEBUG=-all,+fps: Wine logs it at each present).
stats() {
  exists
  local list game pid
  list=$(pids)
  [ -n "$list" ] || die "$name is not running"
  game=$(game_pid)
  [ -z "$game" ] || echo "game: pid $game, $(awk '/^VmRSS:/ { print int($2 / 1024) " MB resident" }' "/proc/$game/status")"
  local unit=tandem-instance-$name.scope
  if systemctl --user --quiet is-active "$unit" 2>/dev/null; then
    local before after
    echo "scope: $(($(systemctl --user show -p MemoryCurrent --value "$unit") / 1048576)) MB (with page cache)"
    before=$(systemctl --user show -p CPUUsageNSec --value "$unit")
    sleep 5
    after=$(systemctl --user show -p CPUUsageNSec --value "$unit")
    echo "cpu: $(((after - before) / 50000000)) % of one core (5 s)"
  fi
  # VRAM: each DRM client once (a client's fd can be open in several processes).
  local file
  for pid in $list; do
    for file in /proc/"$pid"/fdinfo/*; do
      ! grep -qs '^drm-client-id:' "$file" || cat "$file" 2>/dev/null || true
    done
  done | awk '/^drm-client-id:/ { id = $2 } /^drm-memory-vram:/ && !(id in seen) { seen[id]; kib += $2 }
    END { print "vram: " int(kib / 1024) " MB" }'
  # "trace:fps:win32u_vkQueuePresentKHR 0x… @ approx 30.01fps, total 30.08fps"
  sed -n 's/.*trace:fps:.*@ approx \([0-9.]*\)fps.*/fps: \1/p' "$inst/run/launch.log" 2>/dev/null | tail -1
}

command_file() {
  exists
  [ -n "$(pids)" ] || die "$name is not running"
  [ $# -gt 0 ] || die "cmd NAME TEXT..."
  local file=$inst/game/red4ext/plugins/tandem/tandem.cmd
  # One at a time: tandem takes (removes) the file twice a second in a game.
  for _ in $(seq 1 20); do
    [ -e "$file" ] || break
    sleep 0.25
  done
  printf '%s\n' "$*" | write "$file"
  [ "$dry" = 0 ] || return 0
  for _ in $(seq 1 120); do
    [ -e "$file" ] || {
      echo "sent to $name: $*"
      return 0
    }
    sleep 0.25
  done
  die "not taken (in a game?)"
}

log() {
  exists
  local file
  file=$(find "$inst/game/red4ext/logs" -maxdepth 1 -name 'tandem-*.log' -printf '%T@ %p\n' 2>/dev/null | sort -rn | head -1 | cut -d' ' -f2-)
  [ -n "$file" ] || die "$name has no tandem log yet ($inst/game/red4ext/logs)"
  tail -n "${1:-40}" "$file"
}

# The game's process among this instance's: the largest (its name is its
# main thread's, GameThread).
game_pid() {
  local pid
  for pid in $(pids); do
    awk -v pid="$pid" '/^VmRSS:/ { print $2, pid }' "/proc/$pid/status" 2>/dev/null || true
  done | sort -rn | head -1 | cut -d' ' -f2
}

# Every thread's stack (winedbg, in the game's own namespaces and with its
# own wineserver: the instance's), to run/bt.txt. As tools/game.sh bt.
bt() {
  exists
  local game wineserver pid file=$inst/run/bt.txt
  game=$(game_pid)
  [ -n "$game" ] || die "$name is not running"
  wineserver=$(for pid in $(pids); do [ "$(cat "/proc/$pid/comm" 2>/dev/null)" != wineserver ] || echo "$pid"; done | head -1)
  [ -n "$wineserver" ] || die "$name: no wineserver among its processes"
  inside "$file"
  local wine env=() kv
  wine=$(dirname "$(readlink -f "/proc/$wineserver/exe")")
  while IFS= read -r -d '' kv; do
    case $kv in WINESERVERSOCKET=* | LD_PRELOAD=* | WINELOADERNOEXEC=*) ;; *) env+=("$kv") ;; esac
  done <"/proc/$game/environ"
  in_game() { timeout 120 nsenter --preserve-credentials -U -m -r -t "$game" /usr/bin/env -i "${env[@]}" WINEDEBUG=-all "$wine/wine" winedbg "$@"; }
  [ "$dry" = 0 ] || {
    echo "+ winedbg in pid $game's namespaces: bt all > $file"
    return 0
  }
  pid=$(in_game --command "info process" 2>/dev/null | awk '/Cyberpunk2077.exe/ { print $1 }')
  [ -n "$pid" ] || die "winedbg does not see the game"
  printf 'attach 0x%s\nbt all\ndetach\nquit\n' "$pid" | in_game 2>&1 |
    grep -v "could not attach\|can.t debug several\|Either .detach" >"$file" || true
  echo "$file: $(grep -c '^Backtracing' "$file") threads"
}

# This start's tandem log (the newest, written since the start).
start_log() {
  find "$inst/game/red4ext/logs" -maxdepth 1 -name 'tandem-*.log' -newer "$inst/run/pid" -printf '%T@ %p\n' 2>/dev/null |
    sort -rn | head -1 | cut -d' ' -f2-
}

# wait_log PATTERN SECONDS [COUNT]: true once this start's tandem log has
# COUNT lines (default 1) matching PATTERN (an extended regular expression).
wait_log() {
  local file
  for _ in $(seq 1 "$2"); do
    file=$(start_log)
    [ -z "$file" ] || [ "$(grep -cE -- "$1" "$file")" -lt "${3:-1}" ] || return 0
    sleep 1
  done
  return 1
}

# From launch to a loaded save, by the instance's own mouse and keys
# (tandem deployed: its log says where the game is). Load Begone passes the
# intro and title screens; on the menu a click on Continue (the top item,
# at an eighth of the width and 40% of the height whatever the size) loads
# the latest save; Space answers the loading screen's last prompt. Keys
# alone do not do it here: the pointer rests where the menu has an item,
# and Return takes that one (it was New Game).
load() {
  exists
  [ -n "$(pids)" ] || die "$name is not running"
  [ "$dry" = 0 ] || {
    echo "+ wait for the menu; xdo: a click on Continue, then space until playing"
    return 0
  }
  wait_log 'main menu world|in game|playing \(solo\)' 300 || die "$name: no main menu in 5 minutes ($(start_log || true))"
  if ! wait_log 'in game|playing \(solo\)' 20; then
    local size=${INSTANCE_SIZE:-1280x720}
    local width=${size%x*} height=${size#*x}
    # The first click can come before the menu takes clicks (a first start,
    # its shaders still compiling): again until the load starts, which the
    # script system's second "system attached" tells (the menu had the first;
    # "in game" comes only once the world is in, minutes later on a first start).
    local try
    for try in 1 2 3 4 5; do
      note "$name: on the menu, Continue ($try)"
      xdo mousemove $((width / 8)) $((height * 2 / 5)) sleep 0.5 mousemove $((width / 8 + 1)) $((height * 2 / 5)) sleep 0.5 click 1 >/dev/null
      ! wait_log 'system attached|in game|playing \(solo\)' 20 2 || break
    done
  fi
  wait_log 'in game|playing \(solo\)' 300 || die "$name: the save did not load in 5 minutes"
  local tries=0
  until wait_log 'script. playing' 8; do
    xdo key space >/dev/null
    tries=$((tries + 1))
    [ "$tries" -le 30 ] || die "$name: the save did not finish loading in 4 minutes"
  done
  # A save that loads a dead V (the death screen) is no game to test on.
  command_file test query health >/dev/null 2>&1 || true
  sleep 2
  local health
  health=$(log 200 2>/dev/null | sed -n 's/.*test query: health = \([0-9.]*\).*/\1/p' | tail -1)
  case ${health:-} in
  "") die "$name: loaded, but the game answers no command (paused on the death screen?)" ;;
  0 | 0.0*) die "$name: the save loads a dead V (health $health): move its newest save aside" ;;
  esac
  note "$name: playing (health $health)"
}

# The X server gamescope started for this instance (its launch log names it).
x_display() {
  local display
  display=$(sed -n 's/.*Starting Xwayland on \(:[0-9][0-9]*\).*/\1/p' "$inst/run/launch.log" 2>/dev/null | tail -1)
  [ -n "$display" ] || die "$name has no X server of its own (not started in gamescope?)"
  echo "$display"
}

xdo() {
  exists
  [ -n "$(pids)" ] || die "$name is not running"
  local display program
  display=$(x_display)
  program=$(tool xdotool) || die "no xdotool"
  DISPLAY=$display run "$program" "$@"
}

shot() {
  exists
  [ -n "$(pids)" ] || die "$name is not running"
  local file=$inst/run/${1:-shot.png} socket gamescope
  inside "$file"
  # gamescope takes it itself (X clients cannot read its composited screen).
  socket=$(sed -n "s/.*Running compositor on wayland display '\\([^']*\\)'.*/\\1/p" "$inst/run/launch.log" 2>/dev/null | tail -1)
  [ -n "$socket" ] || die "$name has no gamescope (INSTANCE_DISPLAY=window?)"
  gamescope=$(tool gamescope) || die "no gamescope"
  run rm -f -- "$file"
  XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/$(id -u)} GAMESCOPE_WAYLAND_DISPLAY=$socket \
    run timeout 10 "$(dirname "$gamescope")/gamescopectl" screenshot "$file" >/dev/null
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    [ ! -s "$file" ] || break
    sleep 0.5
  done
  echo "$file"
}

case $verb in
status) status ;;
who) who ;;
create | sync | role | settings | start | load | stop | destroy | dir | cmd | log | stats | bt | xdo | shot)
  instance "${1:-}"
  shift
  case $verb in
  create) create "$@" ;;
  sync) sync_game ;;
  role)
    exists
    refuse_if_foreign
    role_file "$@"
    ;;
  settings)
    exists
    refuse_if_foreign
    [ -z "$(pids)" ] || die "$name is running: the game rewrites its settings on exit"
    settings
    ;;
  start) start ;;
  load) load ;;
  stop)
    exists
    stop
    ;;
  destroy) destroy ;;
  dir)
    exists
    echo "$inst/game"
    ;;
  cmd)
    refuse_if_foreign
    command_file "$@"
    ;;
  log) log "$@" ;;
  stats) stats ;;
  bt) bt ;;
  xdo) xdo "$@" ;;
  shot) shot "$@" ;;
  esac
  ;;
*)
  echo "instance: no verb $verb (create, sync, role, settings, start, load, stop, destroy, status, who, dir, cmd, log, stats, bt, xdo, shot)" >&2
  exit 2
  ;;
esac
