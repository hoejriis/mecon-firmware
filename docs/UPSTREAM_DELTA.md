# Upstream delta register

This repository is a fork of [MeshCore](https://github.com/meshcore-dev/MeshCore), pinned at **v1.18.0** (upstream commit `81dbae592ffad40b4212bc0b37e82b38da08f4bc`, the `companion-v1.18.0`, `repeater-v1.18.0` and `room-server-v1.18.0` tags). Upstream history is kept, so a later upstream release merges in the ordinary way.

This register lists **every change this repository makes to a file that exists upstream**. Files this project adds are not listed. An entry stays here for as long as the change exists; when an upstream release makes it unnecessary, the entry is removed in the same change that removes the patch.

| Path | Change | Why |
|---|---|---|
| `README.md`, `CONTRIBUTING.md`, `SECURITY.md` | This project's own versions | The project describes and governs itself; upstream's versions remain in upstream history. |
| `.devcontainer/devcontainer.json` | Replaced | Upstream's container runs privileged on the host network, installs an unpinned PlatformIO and pipes a third-party installer through `curl … \| bash`. This project builds in an unprivileged container from the root `Dockerfile` with a pinned PlatformIO, matching CI. |
| `.github/workflows/github-pages.yml` | Removed | Deploys upstream's documentation site under upstream's domain on every push to `main`. |
| `.github/workflows/build-companion-firmwares.yml`, `build-repeater-firmwares.yml`, `build-room-server-firmwares.yml` | Removed | Release builders triggered by upstream's tag names. This project's releases have their own workflow. |
| `.github/FUNDING.yml`, `CNAME` | Removed | Upstream's funding link and documentation domain. |
| `.gitignore` | `.sim-logs/` added | Local logs from simulated-board runs. |

Upstream's `stale-bot.yml` is kept unchanged; it only runs in the upstream repository.

## Offered upstream

Generic fixes made here that would also benefit stock MeshCore are listed in this section once they exist, with whether they have been offered upstream.
