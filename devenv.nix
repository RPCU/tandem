let
  sources = import ./npins;
in
{
  pkgs,
  lib,
  ...
}:
let
  llvm = pkgs.llvmPackages_20;
in
{
  # nixbook's shared hooks and formatters (treefmt, shellcheck, mdsh, difftastic).
  imports = [ "${sources.nixbook}/devenvModules/devenv.nix" ];

  packages = with pkgs; [
    go-task
    cmake
    ninja
    ccache # CMakeLists.txt puts it in front of the compilers (TANDEM_CCACHE=0: off)
    xwin
    clang-tools # clangd, clang-format for the editor
    rsync
    jq
    npins
    # relay
    kustomize
    kubeconform
    # podman: the system's (it needs the host's setuid helpers)
  ];

  languages.go.enable = true;

  # The plugin is a Windows DLL built on Linux: clang-cl and lld-link against the
  # MSVC CRT and Windows SDK headers xwin downloads (`task sysroot`). The unwrapped
  # clang is used on purpose: Nix's wrapper adds Linux flags.
  env = {
    TANDEM_CLANG_CL = "${llvm.clang-unwrapped}/bin/clang-cl";
    TANDEM_LLD_LINK = "${llvm.lld}/bin/lld-link";
    TANDEM_LLVM_BIN = "${llvm.llvm}/bin";
  };

  enterShell = ''
    export TANDEM_XWIN="''${TANDEM_XWIN:-''${XDG_CACHE_HOME:-$HOME/.cache}/tandem/xwin}"
    [ -d "$TANDEM_XWIN/crt" ] || echo "tandem: no Windows sysroot yet, run: task sysroot"

    # One compiler cache per machine, shared by every checkout and worktree
    # (docs/architecture/testing.md, "The compiler cache"). Set here, not in a
    # user-wide ccache.conf: other projects keep their own settings.
    export CCACHE_DIR="''${TANDEM_CCACHE_DIR:-''${XDG_CACHE_HOME:-$HOME/.cache}/tandem/ccache}"
    # tools/remote.sh raises it on the game machine (more worktrees build there).
    export CCACHE_MAXSIZE="''${TANDEM_CCACHE_SIZE:-6G}"
    # Paths under the directory holding every checkout are hashed relative to
    # the build directory, and the build directory is not hashed: a worktree
    # hits what another compiled. Here, the main checkout (its worktrees live
    # inside it); in a copy without git (tools/remote.sh's ~/.cache/tandem/src/
    # <worktree>), the directory around it.
    if common=$(git -C "''${DEVENV_ROOT:-$PWD}" rev-parse --path-format=absolute --git-common-dir 2>/dev/null); then
      export CCACHE_BASEDIR="''${common%/.git}"
    else
      CCACHE_BASEDIR=$(dirname "''${DEVENV_ROOT:-$PWD}")
      export CCACHE_BASEDIR
    fi
    export CCACHE_NOHASHDIR=1
    # Nix store files all have the same mtime: hash the compiler's store path
    # instead (it changes with the compiler), not its size and mtime.
    export CCACHE_COMPILERCHECK="echo %compiler%"
    # Hits from the dependency list the compiler writes anyway (-MD,
    # /showIncludes): a miss never runs the preprocessor twice.
    export CCACHE_DEPEND=1
  '';

  treefmt.config.programs.clang-format.enable = true;
  treefmt.config.settings.global.excludes = [ "vendor/**" ];

  git-hooks.hooks.shellcheck.excludes = [ "^vendor/" ];
}
