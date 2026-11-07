# Release keys

A firmware build accepts an update only if its manifest is signed by one of the production keys compiled into it ([UPDATES](contract/UPDATES.md), section 3). There are three, kept apart so that losing or leaking one is recoverable. Their public keys are in [`release-keys.json`](contract/release-keys.json).

| Key | `key_id` | Kept | Used |
|---|---|---|---|
| 1 | `e1716b2d488a1c09` | On the release signing host, readable only by its own user | To sign every release |
| 2 | `3225364d7272549d` | On the maintainer's laptop, outside any synced folder | If key 1 is lost or dropped |
| 3 | `9c496a197e7468fd` | In the owner's cloud storage, unencrypted | Last resort |

No private key is ever printed, logged, committed or sent. `tools/mecon_release_key.py` writes a seed to a new file, mode 600, and prints only the public key.

**What this means for you.** Anyone who gets hold of one of these seeds can sign an image your device accepts, until that key is dropped. A Deployment can narrow what its devices accept, and a release key alone cannot enrol a device or sign a management request.

## How a release is signed

Pushing a tag `vX.Y.Z` runs [`signed-release.yml`](https://github.com/hoejriis/mecon-firmware/blob/main/.github/workflows/signed-release.yml):

1. GitHub's hosted runners build every variant in `ci/release-variants.json`. They hold no key.
2. The job `sign` runs on the runner labelled `f1-mecon-sign`, which holds key 1. It writes a manifest for each image, signs it, and writes `update-set.json`.
3. A hosted job checks every signature against the published keys and leaves a draft pre-release with the set attached. Its notes are written by hand, and a person publishes it.

**The runner's guard.** In a public repository a fork's workflow can name any runner label, so the label protects nothing. The runner has a job-started hook, [`ci/signing-runner-hook.sh`](https://github.com/hoejriis/mecon-firmware/blob/main/ci/signing-runner-hook.sh), installed root-owned outside anything a job can write. It fails any job that is not `sign` in that workflow on a version tag, before any of the job's steps runs.

**What it does not cover.** Anyone who can push a version tag to this repository gets a signature on that tag's code. Protecting tags is the repository's write access, not this guard.

## The drill

### Key 1 leaks, or may have

1. Stop signing with it: `sudo systemctl disable --now mecon-sign` on the signing host, and move the seed aside.
2. **Protect devices now.** A Deployment drops the key by a DAK-signed authority update that sets `allowed_release_keys` without it. Its devices refuse manifests signed by that key at once.
3. **Remove it from builds.** Delete its entry from `docs/contract/release-keys.json`, run `python3 tools/gen_release_anchors.py`, and merge.
4. **Release with key 2,** signing by hand on the laptop:
   ```
   python3 tools/mecon_release_set.py manifests <images> vX.Y.Z <commit> 3225364d7272549d <issued_at> <manifests>
   python3 tools/mecon_release_key.py sign ~/.mecon-release/key-2.seed <manifest.txt> <signed>/<name>.manifest.json
   python3 tools/mecon_release_set.py index <images> vX.Y.Z <signed>
   python3 tools/mecon_release_set.py verify <signed>
   ```
5. **Make a new key 1** on the signing host, publish it in the same way, and start the runner again. A device trusts it only once it runs a build carrying it.
6. If a leaked key signed a higher version, the genuine fix goes on as a downgrade with `downgrade:true` and a Deployment-signed offer ([UPDATES](contract/UPDATES.md), section 3).

### Key 1 is lost

The same, without step 2: nothing can be signed with a lost key. Sign with key 2, and publish a new key 1.

### Key 2 leaks or is lost

Steps 2 and 3 above for key 2, if it leaked. Make a new spare on the laptop and publish it in the next build.

### Key 3 leaks, or the storage account may have been reached

Steps 2 and 3 above for key 3. The owner makes a new one himself; no agent reads or copies this seed.

### Every key is lost

No device can be updated remotely again. Recovery is by cable, with a new release carrying new keys ([UPDATES](contract/UPDATES.md), section 3).

### The signing host is lost

Key 1 goes with it. Treat it as a leak unless the disk is known to be destroyed, since the disk's encryption only protects it powered off.
