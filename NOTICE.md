# Licensing and provenance

The public mecon-firmware documentation and MECON additions use MIT, continuing the project's established firmware licence. The [licence file](LICENSE) retains the MeshCore upstream notice and identifies MECON authorship separately.

This repository contains the firmware source: upstream MeshCore 1.18.0 with its history, plus this project's additions, and the documentation and target contracts. The upstream MeshCore copyright and licence notice are kept in [`LICENSE`](LICENSE) and `license.txt`; every change to an upstream file is listed in [`docs/UPSTREAM_DELTA.md`](docs/UPSTREAM_DELTA.md), and files this project adds are the rest. Preserve upstream attribution and applicable file notices when reusing any of it. Third-party components (for example the PubSubClient MQTT library, MIT) retain their own licences; the root licence does not replace them.

The public refounding uses a pinned released MeshCore baseline (v1.18.0) and reviewed source/provenance. Private implementation history is not published. A public build supplies its own OTA trust anchors; signing identities and keys used by private deployments are not part of this repository.

[MeshContinuum](https://github.com/hoejriis/MeshContinuum), the Backend/Reader sister project, uses Apache-2.0 separately.
