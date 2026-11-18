# redscript's scc (the script compiler the game runs, engine/tools/scc.exe) built
# for Linux, so `task scripts:check` compiles our scripts without the game.
# Keep the version equal to the game's: the release zip's scc.exe and
# scc_lib.dll match the ones installed on anya byte for byte.
{
  lib,
  rustPlatform,
  fetchFromGitHub,
}:

rustPlatform.buildRustPackage rec {
  pname = "redscript-scc";
  version = "0.5.32";

  src = fetchFromGitHub {
    owner = "jac3km4";
    repo = "redscript";
    # v0.5.32 (an annotated tag; this is its commit)
    rev = "f73003ac55fe41c10a9eecd61352343a3741ddd5";
    hash = "sha256-Two76KpmdHsOU9V03tC1LD3mE+V4wn1vCUARyES8fpI=";
  };

  cargoHash = "sha256-k0gJpr3YaHMpd2v4CAOi+YiEUfovoiyEjmGyqTtv4Qk=";

  # scc is a thin CLI that loads libscc_lib.so from its own directory.
  cargoBuildFlags = [
    "-p"
    "scc"
    "-p"
    "scc-lib"
  ];
  doCheck = false;

  # The workspace denies every warning; a newer rustc than upstream's must not
  # turn new lints into build failures.
  env.RUSTFLAGS = "--cap-lints warn";

  postInstall = ''
    mv "$out"/lib/libscc_lib.so "$out"/bin/
    rmdir "$out"/lib 2>/dev/null || true
  '';

  meta = {
    description = "redscript compiler (scc) ${version}, as installed in the game";
    homepage = "https://github.com/jac3km4/redscript";
    license = lib.licenses.mit;
    mainProgram = "scc";
  };
}
