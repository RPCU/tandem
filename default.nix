# Nix builds that do not need the game: the relay and its image, and the
# redscript compiler `task scripts:check` runs.
#   nix-build -A relay
#   nix-build -A relayImage        (this machine's architecture)
#   nix-build -A relayImageArm64   (arm64, cross-built: the Go binary is static)
#   nix-build -A redscript
let
  sources = import ./npins;
in
{
  pkgs ? import sources.nixpkgs { },
}:
rec {
  relay = pkgs.callPackage ./nix/relay.nix { };
  relayImage = pkgs.callPackage ./nix/relay-image.nix { inherit relay; };
  # The same image for arm64 nodes. Pushed with relayImage as one
  # multi-arch tag (docs/relay.md, "The image").
  relayArm64 = pkgs.pkgsCross.aarch64-multiplatform.callPackage ./nix/relay.nix { };
  relayImageArm64 = pkgs.pkgsCross.aarch64-multiplatform.callPackage ./nix/relay-image.nix {
    relay = relayArm64;
  };
  redscript = pkgs.callPackage ./nix/redscript.nix { };
}
