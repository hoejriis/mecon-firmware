"""tools/stack_callgraph.py, the nRF52 stack gate's tool, on small hand-written call graphs (GCC's .ci format)."""
import os
import subprocess
import sys

import pytest

TOOL = os.path.join(os.path.dirname(__file__), "..", "..", "tools", "stack_callgraph.py")


def graph(tmp_path, frames, edges, indirect=()):
    """frames: {name: bytes}; edges: [(caller, callee)]; indirect: callers with an indirect call."""
    lines = ["graph: {"]
    for f, n in frames.items():
        lines.append('node: { title: "%s" label: "%s\\n%s\\n%d bytes (static)" }' % (f, f, "x.cpp:1:1", n))
    for a, b in edges:
        lines.append('edge: { sourcename: "%s" targetname: "%s" }' % (a, b))
    for a in indirect:
        lines.append('edge: { sourcename: "%s" targetname: "__indirect_call" }' % a)
    lines.append("}")
    (tmp_path / "a.ci").write_text("\n".join(lines) + "\n")
    return str(tmp_path)


def run(build, *args, seed="0"):
    env = dict(os.environ, PYTHONHASHSEED=seed)
    return subprocess.run([sys.executable, TOOL, build] + list(args), capture_output=True, text=True, env=env)


def test_a_root_over_its_limit_fails_and_its_path_is_shown(tmp_path):
    b = graph(tmp_path, {"loop": 100, "deep": 500, "flat": 10}, [("loop", "deep"), ("loop", "flat")])
    r = run(b, "--root", "loop=550")
    assert r.returncode == 1 and "OVER  loop: worst 600 B of 550" in r.stdout and "deep" in r.stdout
    assert run(b, "--root", "loop=600").returncode == 0


def test_a_root_that_matches_nothing_fails(tmp_path):
    b = graph(tmp_path, {"loop": 100}, [])
    r = run(b, "--root", "renamed=4000")
    assert r.returncode == 2 and "ROOT NOT FOUND" in r.stdout


def test_an_indirect_call_is_followed_only_where_named(tmp_path):
    b = graph(tmp_path, {"loop": 100, "job": 700}, [], indirect=["loop"])
    assert "worst 100 B" in run(b, "--root", "loop=4000").stdout
    r = run(b, "--root", "loop=4000", "--indirect", "loop=job")
    assert "worst 800 B" in r.stdout


def test_the_worst_path_does_not_depend_on_the_hash_seed(tmp_path):
    # A cycle reached by two routes: a memoised walk that cuts the cycle where it first meets it gives a different
    # depth for each order unless the order is fixed.
    frames = {"root": 8, "a": 100, "b": 300, "c": 50, "d": 200}
    edges = [("root", "a"), ("root", "b"), ("a", "c"), ("b", "c"), ("c", "d"), ("d", "c"), ("d", "a")]
    b = graph(tmp_path, frames, edges)
    outs = {run(b, "--root", "root=10000", seed=str(s)).stdout for s in range(8)}
    assert len(outs) == 1


def test_no_reentry_drops_a_path_back_into_a_locked_library(tmp_path):
    # lfs_commit -> driver -> its debug print -> (indirect, mapped to both) File::flush -> lfs_sync: a second entry
    # into LittleFS while it holds its lock, which cannot happen; the print really goes to Serial
    frames = {"loop": 10, "File::write": 20, "lfs_commit": 200, "driver": 30, "Print::print": 20, "Serial::write": 10,
              "File::flush": 20, "lfs_sync": 500}
    edges = [("loop", "File::write"), ("File::write", "lfs_commit"), ("lfs_commit", "driver"), ("driver", "Print::print"),
             ("File::flush", "lfs_sync")]
    b = graph(tmp_path, frames, edges, indirect=["Print::print"])
    ind = ["--indirect", "Print::=File::flush|Serial::write"]
    assert "worst 800 B" in run(b, "--root", "loop=100000", *ind).stdout      # through File::flush and lfs_sync
    cut = run(b, "--root", "loop=100000", *ind, "--no-reentry", "^lfs_=File::").stdout
    assert "worst 290 B" in cut and "Serial::write" in cut                    # the print ends in Serial


def test_no_reentry_keeps_a_job_off_the_task_that_hands_it_over(tmp_path):
    frames = {"loop_task": 8, "run": 40, "job": 3000, "worker_body": 24}
    edges = [("loop_task", "run"), ("worker_body", "run")]
    b = graph(tmp_path, frames, edges, indirect=["run", "worker_body"])
    args = ["--root", "loop_task=1000", "--root", "worker_body=8000", "--indirect", "run|worker_body=job"]
    assert run(b, *args).returncode == 1                                    # the job counted on the loop: over
    r = run(b, *args, "--no-reentry", "loop_task=job")
    assert r.returncode == 0 and "loop_task: worst 48 B" in r.stdout and "worker_body: worst 3064 B" in r.stdout


def test_require_fails_when_the_hand_off_is_gone(tmp_path):
    frames = {"loop": 8, "processRecvPacket": 40, "run": 40, "onRecvPacket": 500}
    handed = graph(tmp_path, frames, [("loop", "processRecvPacket"), ("processRecvPacket", "run")])
    r = run(handed, "--root", "loop=4000", "--require", "processRecvPacket=run")
    assert r.returncode == 0 and "ok  processRecvPacket reaches run" in r.stdout
    inline = tmp_path / "inline"
    inline.mkdir()
    graph(inline, frames, [("loop", "processRecvPacket"), ("processRecvPacket", "onRecvPacket")])
    r = run(str(inline), "--root", "loop=4000", "--require", "processRecvPacket=run")
    assert r.returncode == 1 and "MISSING  processRecvPacket does not reach run" in r.stdout
