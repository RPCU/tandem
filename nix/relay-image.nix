# The relay's OCI image: one static binary, no shell, no package manager,
# running as an unprivileged user. Loaded with `docker load` / `podman load`,
# or pushed with skopeo (see Taskfile.yml).
{ dockerTools, relay }:

let
  uid = 65532;
in
dockerTools.buildLayeredImage {
  name = "tandem-relay";
  tag = relay.version;

  contents = [
    relay
    dockerTools.caCertificates
  ];

  fakeRootCommands = ''
    mkdir -p ./etc/tandem-relay
    echo "relay:x:${toString uid}:${toString uid}::/nonexistent:/sbin/nologin" > ./etc/passwd
    echo "relay:x:${toString uid}:" > ./etc/group
  '';

  config = {
    User = "${toString uid}:${toString uid}";
    Entrypoint = [ "${relay}/bin/tandem-relay" ];
    Cmd = [ "serve" ];
    ExposedPorts = {
      "27460/udp" = { };
      "8080/tcp" = { };
    };
    Labels = {
      "org.opencontainers.image.title" = "tandem-relay";
      "org.opencontainers.image.description" = relay.meta.description;
      "org.opencontainers.image.version" = relay.version;
    };
  };
}
