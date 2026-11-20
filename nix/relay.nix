{ lib, buildGoModule }:

buildGoModule {
  pname = "tandem-relay";
  version = "0.1.0";

  src = lib.fileset.toSource {
    root = ../relay;
    fileset = lib.fileset.unions [
      ../relay/go.mod
      (lib.fileset.fileFilter (f: f.hasExt "go") ../relay)
    ];
  };

  # Standard library only.
  vendorHash = null;
  subPackages = [ "cmd/tandem-relay" ];

  env.CGO_ENABLED = "0";
  # Go's own linker, whatever the toolchain's default: nixpkgs builds its
  # cross Go toolchains with external linking on, which made the arm64
  # binary dynamic (glibc's ld.so as interpreter, glibc in the image).
  ldflags = [
    "-s"
    "-w"
    "-linkmode=internal"
  ];

  meta = {
    description = "UDP relay for tandem co-op sessions behind NAT";
    mainProgram = "tandem-relay";
    license = lib.licenses.mit;
  };
}
