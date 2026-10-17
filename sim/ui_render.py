"""Render a recorded OLED frame as a character grid (a text approximation of the 128x64 display).

The simulator's display records what the firmware drew as (x, y, size, text) tuples. This turns one frame into a
21 x 8 character grid: one column is 6 px (the 6x8 font at text size 1), one row is 8 px. A size-2 string takes
two columns and two rows per character (shown as the character followed by a blank). Graphics (battery icon,
page dots, bitmaps, popup box) are not recorded by the simulator and are not drawn, except that an alert popup is
drawn as a box over the page, the way the firmware does.
"""
COLS, ROWS = 21, 8
WIDTH_PX, HEIGHT_PX = 128, 64
COLLISION = "#"


def render(frame, alert=None, title=None):
    """frame: [(x, y, size, text)] as sim.harness.UiProbe.frames() returns. alert: the popup text, if the frame
    carries one (it is the last string the firmware drew). Returns the grid as a list of lines, with a border.

    Every string is drawn at its recorded coordinates, never moved. Cells claimed by two draws are shown as '#' and
    listed under the grid ("overlap"); a string that runs past the 128x64 panel is cut off there and listed
    ("clipped"). Spaces draw no pixels and claim no cells."""
    grid = [[" "] * COLS for _ in range(ROWS)]
    owner = {}          # (row, col) -> index of the draw that claimed the cell
    problems = []
    items = list(frame)
    if alert is not None and items and items[-1][3] == alert:
        items = items[:-1]
    hit = {}            # (row, col) -> sorted draw indexes, for cells claimed more than once
    for n, (x, y, size, text) in enumerate(items):
        col = int(round(x / 6.0))
        row = y // 8
        span = max(1, size)
        if x + 6 * span * len(text) > WIDTH_PX or y + 8 * span > HEIGHT_PX or x < 0 or y < 0:
            problems.append("clipped: %r at x=%d y=%d size=%d runs past the %dx%d panel"
                            % (text, x, y, size, WIDTH_PX, HEIGHT_PX))
        for i, ch in enumerate(text):
            c = col + i * span
            if not (0 <= c < COLS and 0 <= row < ROWS):
                continue
            if ch == " ":
                continue
            for dr in range(span):
                for dc in range(span):
                    cell = (row + dr, c + dc)
                    if cell[0] >= ROWS or cell[1] >= COLS:
                        continue
                    if cell in owner and owner[cell] != n:
                        hit.setdefault(cell, {owner[cell]}).add(n)
                    owner[cell] = n
            grid[row][c] = ch
    for (r, c), who in sorted(hit.items()):
        grid[r][c] = COLLISION
    # one line per colliding pair, with the cells it covers
    pairs = {}
    for (r, c), who in sorted(hit.items()):
        w = sorted(who)
        for a in range(len(w)):
            for b in range(a + 1, len(w)):
                pairs.setdefault((w[a], w[b]), []).append((r, c))
    for (a, b), cells in sorted(pairs.items()):
        problems.append("overlap: %r (x=%d y=%d) and %r (x=%d y=%d) share %d cell(s), row %d col %d..%d"
                        % (items[a][3], items[a][0], items[a][1], items[b][3], items[b][0], items[b][1],
                           len(cells), cells[0][0], min(c for _, c in cells), max(c for _, c in cells)))
    if alert is not None:
        # the firmware's popup: a box from y=21 to y=42, x=4 to x=123, text centred inside
        for r in range(2, 6):
            grid[r][0] = grid[r][COLS - 1] = " "
        for r in range(3, 5):
            for c in range(1, COLS - 1):
                grid[r][c] = " "
        for c in range(1, COLS - 1):
            grid[2][c] = "-"
            grid[5][c] = "-"
        for r in range(2, 6):
            grid[r][1] = "|" if r not in (2, 5) else "+"
            grid[r][COLS - 2] = "|" if r not in (2, 5) else "+"
        start = max(2, (COLS - len(alert)) // 2)
        for i, ch in enumerate(alert[:COLS - 4]):
            grid[3][start + i] = ch
    out = ["+" + "-" * COLS + "+"]
    out += ["|" + "".join(r) + "|" for r in grid]
    out.append("+" + "-" * COLS + "+")
    out += ["! " + p for p in problems]
    if title:
        out.insert(0, "# " + title)
    return out
