# Nix builds that do not need the game: the relay and its image, and the
# redscript compiler `task scripts:check` runs.
#   nix-build -A relay
#   nix-build -A relayImage
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
  redscript = pkgs.callPackage ./nix/redscript.nix { };
}
