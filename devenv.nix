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
  '';

  treefmt.config.programs.clang-format.enable = true;
  treefmt.config.settings.global.excludes = [ "vendor/**" ];

  git-hooks.hooks.shellcheck.excludes = [ "^vendor/" ];
}
