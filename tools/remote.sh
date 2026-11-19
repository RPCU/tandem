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
# default 3, each CMAKE_BUILD_PARALLEL_LEVEL jobs, default 5; one task at a
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

jobs=${CMAKE_BUILD_PARALLEL_LEVEL:-5}
slots=${TANDEM_REMOTE_SLOTS:-3}
# The copy there has no history: the build is named after this commit
# (core/CMakeLists.txt, TANDEM_COMMIT).
commit=$(git rev-parse --short=7 HEAD)
# A build runs in a systemd scope of its own with a low CPU and IO weight:
# nice does not reach across cgroups, so niced builds still starved the test
# games (tools/instance.sh scopes) to 9 fps and pushed them into swap. While
# a test instance runs, one slot only (memory).
# shellcheck disable=SC2029 # expanded here on purpose
ssh "$host" "cd $remote && CMAKE_BUILD_PARALLEL_LEVEL=$jobs TANDEM_XWIN=\$HOME/.cache/tandem/xwin \
  TANDEM_COMMIT=$commit flock /tmp/tandem-src-$name.lock tools/build-slot.sh \$(pgrep -f '[t]andem-instances/' >/dev/null && echo 1 || echo $slots) \
  systemd-run --user --scope --quiet -p CPUWeight=10 -p IOWeight=10 nice -n 19 devenv shell -- task $*"

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
