"""Render a recorded OLED frame as a character grid (a text approximation of the 128x64 display).

The simulator's display records what the firmware drew as (x, y, size, text) tuples. This turns one frame into a
21 x 8 character grid: one column is 6 px (the 6x8 font at text size 1), one row is 8 px. A size-2 string takes
two columns and two rows per character (shown as the character followed by a blank). Graphics (battery icon,
page dots, bitmaps, popup box) are not recorded by the simulator and are not drawn, except that an alert popup is
drawn as a box over the page, the way the firmware does.
"""
COLS, ROWS = 21, 8


def render(frame, alert=None, title=None):
    """frame: [(x, y, size, text)] as sim.harness.UiProbe.frames() returns. alert: the popup text, if the frame
    carries one (it is the last string the firmware drew). Returns the grid as a list of lines, with a border."""
    grid = [[" "] * COLS for _ in range(ROWS)]
    items = list(frame)
    if alert is not None and items and items[-1][3] == alert:
        items = items[:-1]
    used = set()
    for x, y, size, text in items:
        col = int(round(x / 6.0))
        row = y // 8
        span = max(1, size)
        while any((row + k) in used for k in range(span)) and row + span < ROWS:
            row += 1
        for k in range(span):
            used.add(row + k)
        for i, ch in enumerate(text):
            c = col + i * span
            if 0 <= c < COLS and 0 <= row < ROWS:
                grid[row][c] = ch
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
    if title:
        out.insert(0, "# " + title)
    return out
