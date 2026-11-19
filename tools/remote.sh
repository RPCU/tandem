#!/usr/bin/env bash
# Run a build or the tests on the game machine (more cores than the laptop).
#
#   tools/remote.sh build:plugin      # build tandem.dll there, copy it back
#   tools/remote.sh test              # run the core tests there
#   tools/remote.sh TASK...           # any Taskfile task
#
# The working tree (tracked and untracked files, submodules, not ignored
# ones) is synced to ~/.cache/tandem/src/<worktree name> on GAME_HOST, the
# Windows sysroot to ~/.cache/tandem/xwin (once), and the task runs inside
# devenv in one of anya's build slots (tools/build-slot.sh: TANDEM_REMOTE_SLOTS,
# default 3, each CMAKE_BUILD_PARALLEL_LEVEL jobs, by default the cores
# shared among the busy slots (TANDEM_JOBS=auto); one task at a
# time per worktree), at low priority so a running game keeps its frames. Nothing is written inside the game's directory.
# build/windows/plugin/tandem.{dll,pdb} come back into this tree, so
# `task deploy` and tools/scenario.sh pick them up; after build, build:linux
# or test, so does build/linux/peer/tandem-peer (scenarios run it here).
set -euo pipefail

host=${GAME_HOST:-anya}
root=$(git rev-parse --show-toplevel)
name=$(basename "$root")
remote=".cache/tandem/src/$name" # relative to the remote home
xwin=${TANDEM_XWIN:-$HOME/.cache/tandem/xwin}
[ $# -gt 0 ] || set -- build:plugin

# Never while someone plays there: a build takes the game's frames. Our own
# test instances (tools/instance.sh, under tandem-instances/) are not someone.
if ssh "$host" 'pgrep -af "[C]yberpunk2077.exe" | grep -v tandem-instances | grep -q .' &&
  [ -z "${TANDEM_REMOTE_WHILE_PLAYING:-}" ]; then
  echo "remote: the game runs on $host: build here instead (tools/build.sh picks)" >&2
  exit 1
fi

if ! ssh "$host" test -d .cache/tandem/xwin/crt; then
  echo "remote: copying the Windows sysroot to $host (once)" >&2
  ssh "$host" mkdir -p .cache/tandem
  rsync -a --delete "$xwin/" "$host:.cache/tandem/xwin/"
fi

cd "$root"
ssh "$host" mkdir -p "$remote"
{
  git ls-files -z --cached --recurse-submodules
  git ls-files -z --others --exclude-standard
} | rsync -a --from0 --files-from=- --ignore-missing-args . "$host:$remote/"

# Fixed when the caller sets it, else the cores shared among busy slots.
if [ -n "${CMAKE_BUILD_PARALLEL_LEVEL:-}" ]; then jobs="CMAKE_BUILD_PARALLEL_LEVEL=$CMAKE_BUILD_PARALLEL_LEVEL"; else jobs=TANDEM_JOBS=auto; fi
slots=${TANDEM_REMOTE_SLOTS:-3}
# The copy there has no history: the build is named after this commit
# (core/CMakeLists.txt, TANDEM_COMMIT).
commit=$(git rev-parse --short=7 HEAD)
# The compiler cache there (devenv.nix sets it up; TANDEM_CCACHE=0 here
# turns it off there too): bigger than the laptop's, since most worktrees
# build there.
cache="TANDEM_CCACHE=${TANDEM_CCACHE:-1} TANDEM_CCACHE_SIZE=${TANDEM_REMOTE_CCACHE_SIZE:-15G}"
# A build runs in a systemd scope of its own with a low CPU and IO weight:
# nice does not reach across cgroups, so niced builds still starved the test
# games (tools/instance.sh scopes) to 9 fps and pushed them into swap. While
# a test instance runs, one slot only (memory). The compile slot is held
# for compiling only: the tests then run in a pool of their own
# (TANDEM_TEST_SLOTS, default 6), since they mostly wait on real time and
# held a compile slot idle for minutes. The integration worktree (the merge
# queue, everyone's critical path) may also use one slot nobody else takes.
builds=() tests=''
for task in "$@"; do
  if [ "$task" = test ]; then tests=1; else builds+=("$task"); fi
done
if [ -n "$tests" ]; then
  case " ${builds[*]} " in *" build "* | *" build:linux "*) ;; *) builds+=(build:linux) ;; esac
fi
[ "$name" = integration ] && slots=$((slots + 1))
in_slot() {
  local pool=$1 count=$2
  shift 2
  # shellcheck disable=SC2029 # expanded here on purpose
  ssh "$host" "cd $remote && $jobs $cache TANDEM_XWIN=\$HOME/.cache/tandem/xwin TANDEM_COMMIT=$commit TANDEM_SLOT_POOL=$pool \
    flock /tmp/tandem-src-$name.lock tools/build-slot.sh $count \
    systemd-run --user --scope --quiet -p CPUWeight=10 -p IOWeight=10 nice -n 19 devenv shell -- task $*"
}
if [ ${#builds[@]} -gt 0 ]; then
  in_slot build "\$(pgrep -f '[t]andem-instances/' >/dev/null && echo 1 || echo $slots)" "${builds[@]}"
fi
if [ -n "$tests" ]; then
  in_slot test "${TANDEM_TEST_SLOTS:-6}" test
fi

for task in "$@"; do
  case $task in
  build | build:plugin)
    mkdir -p build/windows/plugin
    rsync -a "$host:$remote/build/windows/plugin/tandem.dll" "$host:$remote/build/windows/plugin/tandem.pdb" \
      build/windows/plugin/
    # The bug report tool, for `task package` (absent in older trees).
    rsync -a --ignore-missing-args "$host:$remote/build/windows/plugin/tandem-report.exe" \
      "$host:$remote/build/windows/plugin/tandem-report.pdb" build/windows/plugin/
    echo "remote: build/windows/plugin/tandem.dll from $host" >&2
    ;;
  esac
  case $task in
  build | build:linux | test)
    # Same devenv lock on both machines, so the same store paths: it runs here.
    mkdir -p build/linux/peer
    rsync -a "$host:$remote/build/linux/peer/tandem-peer" build/linux/peer/
    echo "remote: build/linux/peer/tandem-peer from $host" >&2
    ;;
  esac
done
