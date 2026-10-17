# Releasing firmware

Public releases are numbered 0.1.x. The version a build reports is `git describe` of the nearest `v*` tag, so a release is a `v0.1.x` tag on a commit of `main` (see [Versioning and releases](docs/VERSIONING_AND_RELEASES.md)).

The releases so far are GitHub pre-releases of source. Each set of release notes states what was verified and how, what was not verified, behaviour changes from the previous release, and measured memory cost. No binaries are attached. A release is not promoted from pre-release until its target boards have passed a hardware gate.

Upstream MeshCore's tag-triggered release builders (`companion-v*`, `repeater-v*`, `room-server-v*`) are removed in this repository (see [`docs/UPSTREAM_DELTA.md`](docs/UPSTREAM_DELTA.md)). `.github/workflows/firmware-builder.yml` and the `build.sh` helpers remain from upstream, and nothing here triggers them on a tag.
