# syntax=docker/dockerfile:1
# The relay's image without Nix, the same as nix/relay-image.nix: one static
# binary, CA certificates, no shell and no package manager, user
# 65532:65532, entrypoint tandem-relay, command serve. The Kubernetes base
# (deploy/kubernetes/base) runs it with a read-only root and no
# capabilities; the host tokens are mounted at /etc/tandem-relay.
#
#   docker build -t tandem-relay relay/
FROM --platform=$BUILDPLATFORM golang:1.25 AS build
ARG TARGETOS
ARG TARGETARCH
WORKDIR /src
# Standard library only: no go.sum, nothing to download.
COPY go.mod ./
COPY cmd ./cmd
COPY internal ./internal
RUN --mount=type=cache,target=/root/.cache/go-build \
    CGO_ENABLED=0 GOOS=$TARGETOS GOARCH=$TARGETARCH \
    go build -trimpath -ldflags "-s -w" -o /out/tandem-relay ./cmd/tandem-relay

# distroless/static: CA certificates, /etc/passwd with nonroot (65532), no shell.
FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=build /out/tandem-relay /usr/local/bin/tandem-relay
USER 65532:65532
EXPOSE 27460/udp 27461/udp 8080/tcp
ENTRYPOINT ["/usr/local/bin/tandem-relay"]
CMD ["serve"]
