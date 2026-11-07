#!/bin/sh
# The job-started hook of the runner holding release key 1 (docs/RELEASE_KEYS.md). Installed on F1 root-owned, outside anything a
# job can write, as /etc/mecon-sign/job-started.sh. This repository is public and a fork's workflow can name any runner label,
# so the label guards nothing: a job that is not the signing job on a version tag fails here, before any of its steps runs.
ok=true
[ "$GITHUB_REPOSITORY" = "hoejriis/mecon-firmware" ] || ok=false
[ "$GITHUB_EVENT_NAME" = "push" ] || ok=false
[ "$GITHUB_JOB" = "sign" ] || ok=false
case "$GITHUB_REF" in refs/tags/v[0-9]*) ;; *) ok=false ;; esac
case "$GITHUB_WORKFLOW_REF" in "hoejriis/mecon-firmware/.github/workflows/signed-release.yml@refs/tags/v"[0-9]*) ;; *) ok=false ;; esac
if [ "$ok" != true ]; then
  echo "signing runner: refused: repo=$GITHUB_REPOSITORY event=$GITHUB_EVENT_NAME job=$GITHUB_JOB ref=$GITHUB_REF workflow=$GITHUB_WORKFLOW_REF" >&2
  exit 1
fi
