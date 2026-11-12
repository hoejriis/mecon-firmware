"""ci/all_green.py's verdict (.github/workflows/all-green.yml): green only when every other check run and workflow run on
the head commit finished as success, skipped or neutral; the latest of each name counts; this workflow never counts."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "ci"))
import all_green  # noqa: E402


def chk(i, name, status="completed", conclusion="success"):
    return {"id": i, "name": name, "status": status, "conclusion": conclusion}


def run(i, wf, name, status="completed", conclusion="success"):
    return {"id": i, "workflow_id": wf, "name": name, "status": status, "conclusion": conclusion}


OWN = 900


def test_green_when_all_passed_or_skipped():
    done, red, _ = all_green.verdict([chk(1, "sim (1)"), chk(2, "memory", conclusion="skipped"), chk(3, "all-green", "in_progress")],
                                     [run(10, 1, "Simulated boards"), run(OWN, 9, "ci", "in_progress")], OWN)
    assert done and red == []


def test_waits_while_anything_runs():
    done, red, _ = all_green.verdict([chk(1, "sim (1)", "in_progress", None)], [run(10, 1, "Simulated boards", "queued", None)], OWN)
    assert not done and red == []
    done, _, _ = all_green.verdict([chk(1, "sim (1)", "in_progress", None)], [run(10, 1, "Simulated boards")], OWN)
    assert not done, "a check still running holds it, even when its workflow reads completed"
    done, _, _ = all_green.verdict([chk(1, "sim (1)")], [run(10, 1, "Simulated boards", "in_progress", None)], OWN)
    assert not done, "a workflow still running holds it"


def test_red_on_a_failed_check_or_a_workflow_that_failed_before_any_job():
    _, red, _ = all_green.verdict([chk(1, "memory", conclusion="failure")], [], OWN)
    assert red == ["check memory: failure"]
    _, red, _ = all_green.verdict([], [run(10, 1, "Target boards", conclusion="startup_failure")], OWN)
    assert red == ["workflow Target boards: startup_failure"]
    _, red, _ = all_green.verdict([chk(1, "x", conclusion="cancelled")], [], OWN)
    assert red, "cancelled is not green"


def test_the_latest_of_each_counts_and_this_workflow_never():
    checks = [chk(1, "sim (1)", conclusion="failure"), chk(5, "sim (1)")]
    runs = [run(10, 1, "Simulated boards", conclusion="failure"), run(11, 1, "Simulated boards"),
            run(800, 9, "ci", conclusion="cancelled"), run(OWN, 9, "ci", "in_progress")]
    done, red, _ = all_green.verdict(checks, runs, OWN)
    assert done and red == [], red
