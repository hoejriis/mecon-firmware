"""The character-grid renderer keeps recorded coordinates and reports collisions and clipping."""
from sim.ui_render import render


def body(lines):
    return [l for l in lines if l.startswith("|")]


def notes(lines):
    return [l for l in lines if l.startswith("! ")]


def test_side_by_side_on_one_row_stay_on_one_row():
    out = render([(0, 8, 1, "Left"), (60, 8, 1, "Right")])
    rows = body(out)
    assert rows[1].startswith("|Left") and "Right" in rows[1]
    assert not notes(out)
    assert all(r.strip("| ") == "" for i, r in enumerate(rows) if i != 1)


def test_identical_coordinates_are_a_collision():
    out = render([(0, 0, 1, "ABCD"), (0, 0, 1, "WXYZ")])
    assert body(out)[0] == "|####" + " " * 17 + "|"
    assert len(notes(out)) == 1 and "overlap" in notes(out)[0]


def test_partial_overlap_is_reported_and_marked():
    out = render([(0, 0, 1, "ABCD"), (12, 0, 1, "WXYZ")])   # second starts at column 2
    assert body(out)[0].startswith("|AB##YZ")
    assert len(notes(out)) == 1 and "2 cell(s)" in notes(out)[0]


def test_touching_strings_do_not_collide():
    out = render([(0, 0, 1, "AB"), (12, 0, 1, "CD")])
    assert body(out)[0].startswith("|ABCD") and not notes(out)


def test_spaces_claim_nothing():
    out = render([(0, 0, 1, "A  B"), (6, 0, 1, "xy")])
    assert not notes(out)


def test_clipping_at_the_panel_edge_is_reported():
    out = render([(100, 0, 1, "ABCDEFGH")])      # 100 + 48 > 128
    assert any(n.startswith("! clipped") for n in notes(out))
    out = render([(0, 60, 1, "low")])            # 60 + 8 > 64
    assert any(n.startswith("! clipped") for n in notes(out))
    assert not notes(render([(80, 56, 1, "ok")]))


def test_size_two_footprint_collides_with_next_row():
    out = render([(0, 0, 2, "AB"), (0, 8, 1, "x")])
    assert len(notes(out)) == 1
