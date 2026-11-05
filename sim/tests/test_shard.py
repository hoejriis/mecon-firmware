"""sim/shard.py: every test file runs in exactly one shard, so splitting the suite cannot quietly drop one."""
import glob
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import shard  # noqa: E402

SHARDS = 3   # as the matrix in .github/workflows/sim-tests.yml


def test_every_file_is_in_exactly_one_shard():
    here = os.path.dirname(os.path.abspath(__file__))
    want = sorted(os.path.basename(p) for p in glob.glob(os.path.join(here, "test_*.py")))
    got = sorted(f for s in shard.shards(SHARDS)[0] for f in s)
    assert got == want


def test_the_workflow_runs_every_shard():
    wf = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".github", "workflows", "sim-tests.yml")).read()
    assert "shard: [%s]" % ", ".join(str(i) for i in range(SHARDS)) in wf
    assert "sim/shard.py ${{ matrix.shard }} %d" % SHARDS in wf


def test_no_shard_carries_most_of_the_load():
    load = shard.shards(SHARDS)[1]
    assert max(load) < 1.5 * sum(load) / SHARDS, load
