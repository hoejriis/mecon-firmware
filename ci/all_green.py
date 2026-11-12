#!/usr/bin/env python3
"""One required check for a pull request: waits until every other check run and workflow run on the head commit has
finished, then passes only when each passed, was skipped or was neutral (.github/workflows/all-green.yml).

Branch protection names one stable check instead of the per-PR matrix names, and a path-filtered workflow that does
not trigger leaves no run, so nothing waits for it. Workflow runs are read beside the check runs because a run can fail
before any job starts (a matrix over GitHub's job limit), and then it has no check run at all.

    all_green.py <owner/repo> <head sha> <own run id> [poll seconds] [grace seconds]
Reads GH_TOKEN; exits 0 green, 1 red, 2 on timeout."""
import json
import os
import sys
import time
import urllib.request

OK = {"success", "skipped", "neutral"}
OWN_CHECK = "all-green"


def get(path):
    req = urllib.request.Request("https://api.github.com" + path, headers={
        "Authorization": "Bearer " + os.environ["GH_TOKEN"], "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def pages(path, key):
    out, page = [], 1
    while True:
        sep = "&" if "?" in path else "?"
        items = get("%s%sper_page=100&page=%d" % (path, sep, page)).get(key, [])
        out += items
        if len(items) < 100:
            return out
        page += 1


def verdict(checks, runs, own_run):
    """(done, red, lines): the latest check run per name and the latest workflow run per workflow, bar this workflow."""
    latest = {}
    for c in checks:
        if c["name"] == OWN_CHECK:
            continue
        if c["name"] not in latest or c["id"] > latest[c["name"]]["id"]:
            latest[c["name"]] = c
    own_wf = {r["workflow_id"] for r in runs if r["id"] == own_run}   # this workflow's earlier, cancelled runs too
    wf = {}
    for r in runs:
        if r["workflow_id"] in own_wf:
            continue
        if r["workflow_id"] not in wf or r["id"] > wf[r["workflow_id"]]["id"]:
            wf[r["workflow_id"]] = r
    done, red, lines = True, [], []
    for name, c in sorted(latest.items()):
        if c["status"] != "completed":
            done = False
        elif c["conclusion"] not in OK:
            red.append("check %s: %s" % (name, c["conclusion"]))
    for r in sorted(wf.values(), key=lambda r: r["name"]):
        if r["status"] != "completed":
            done = False
        elif r["conclusion"] not in OK:
            red.append("workflow %s: %s" % (r["name"], r["conclusion"]))
    lines.append("%d checks, %d workflows" % (len(latest), len(wf)))
    return done, red, lines


def main():
    repo, sha, own = sys.argv[1], sys.argv[2], int(sys.argv[3])
    poll = float(sys.argv[4]) if len(sys.argv) > 4 else 60
    grace = float(sys.argv[5]) if len(sys.argv) > 5 else 120
    time.sleep(grace)   # every workflow of the push or label event has registered its run by then
    end = time.time() + 175 * 60
    while time.time() < end:
        checks = pages("/repos/%s/commits/%s/check-runs" % (repo, sha), "check_runs")
        runs = pages("/repos/%s/actions/runs?head_sha=%s" % (repo, sha), "workflow_runs")
        done, red, lines = verdict(checks, runs, own)
        print(time.strftime("%H:%M:%SZ", time.gmtime()), lines[0], "red:", red or "none", flush=True)
        if red:
            print("\n".join(red))
            return 1
        if done:
            print("all green")
            return 0
        time.sleep(poll)
    print("timed out waiting for the other checks")
    return 2


if __name__ == "__main__":
    sys.exit(main())
