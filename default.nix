# Nix builds that do not need the game: the relay and its image.
#   nix-build -A relay
#   nix-build -A relayImage
let
  sources = import ./npins;
in
{
  pkgs ? import sources.nixpkgs { },
}:
rec {
  relay = pkgs.callPackage ./nix/relay.nix { };
  relayImage = pkgs.callPackage ./nix/relay-image.nix { inherit relay; };
}
