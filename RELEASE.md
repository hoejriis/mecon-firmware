# Releasing firmware

Public releases are numbered 0.x.y. The version a build reports is `git describe` of the nearest `v*` tag, so a release is a `vX.Y.Z` tag on a commit of `main` (see [Versioning and releases](docs/VERSIONING_AND_RELEASES.md)).

The releases so far are GitHub pre-releases. v0.1.0 to v0.1.3 are source only. From v0.2.0, a tag also builds every variant and attaches the images with their signed manifests ([release keys](docs/RELEASE_KEYS.md)). Each set of release notes states what was verified and how, what was not verified, behaviour changes from the previous release, and measured memory cost. A release is not promoted from pre-release until its target boards have passed a hardware gate.

Upstream MeshCore's tag-triggered release builders (`companion-v*`, `repeater-v*`, `room-server-v*`) are removed in this repository (see [`docs/UPSTREAM_DELTA.md`](docs/UPSTREAM_DELTA.md)). `.github/workflows/firmware-builder.yml` and the `build.sh` helpers remain from upstream, and nothing here triggers them on a tag.
