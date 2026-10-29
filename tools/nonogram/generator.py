"""Nonogram picture pack: checks every picture and writes the SD files.

    python -I generator.py pictures.txt OUT_DIR [--preview sheet.png]

pictures.txt holds hand-drawn pictures in the pack format the firmware reads
(lib/Games/Nonogram.h):

    NONOGRAMS 1
    <id 1..255> <side> <name>
    <side rows of '#' black and '.' white>

Every picture must be solvable by line logic alone (rows and columns, one at a
time), which also proves its answer unique. OUT_DIR/nonograms/ receives
pictures.txt and ATTRIBUTION.txt; copy that folder to the root of the SD card.
A picture that fails is reported with the single-cell flips that would fix it.
"""

import argparse
import sys
from pathlib import Path

HEADER = "NONOGRAMS 1"
SIDES = (10, 15)
NAME_MAX_BYTES = 40
UNKNOWN, BLACK, WHITE = 0, 1, 2
ATTRIBUTION = """Pictures drawn for the Сова firmware (tools/nonogram/pictures.txt in its
repository), shared under the firmware's MIT licence. The random puzzles on the
device follow the generator of Pattern from Simon Tatham's Portable Puzzle
Collection (MIT licence, https://www.chiark.greenend.org.uk/~sgtatham/puzzles/).
"""


def clue(line):
    runs, run = [], 0
    for cell in list(line) + [0]:
        if cell:
            run += 1
        elif run:
            runs.append(run)
            run = 0
    return runs


def solve_line(runs, line):
    """Cells every fitting placement agrees on; None when none fits."""
    n, k = len(line), len(runs)
    whites = [0]
    for cell in line:
        whites.append(whites[-1] + (cell == WHITE))

    def can_black(start, length):
        return start + length <= n and whites[start + length] == whites[start]

    def can_white(i):
        return line[i] != BLACK

    # head[j][i]: runs[:j] fit in line[:i] with cell i-1 white; tail[j][i]: runs[j:] fit in line[i:].
    head = [[False] * (n + 2) for _ in range(k + 1)]
    head[0][0] = True
    for i in range(n + 1):
        for j in range(k + 1):
            if not head[j][i]:
                continue
            if i < n and can_white(i):
                head[j][i + 1] = True
            if j < k and can_black(i, runs[j]):
                end = i + runs[j]
                if end < n and can_white(end):
                    head[j + 1][end + 1] = True
    tail = [[False] * (n + 2) for _ in range(k + 1)]
    tail[k][n] = True
    for i in range(n - 1, -1, -1):
        for j in range(k, -1, -1):
            fits = can_white(i) and tail[j][i + 1]
            if not fits and j < k and can_black(i, runs[j]):
                end = i + runs[j]
                fits = j + 1 == k if end == n else can_white(end) and tail[j + 1][end + 1]
            tail[j][i] = fits
    if not tail[0][0]:
        return None
    black = [False] * n
    white = [any(head[j][i + 1] and tail[j][i + 1] for j in range(k + 1)) for i in range(n)]
    for j, length in enumerate(runs):
        for start in range(n - length + 1):
            if not head[j][start] or not can_black(start, length):
                continue
            end = start + length
            ok = j + 1 == k if end == n else can_white(end) and tail[j + 1][end + 1]
            if ok:
                for i in range(start, end):
                    black[i] = True
    out = []
    for i in range(n):
        if not black[i] and not white[i]:
            return None
        out.append(BLACK if not white[i] else WHITE if not black[i] else line[i])
    return out


def line_solve(grid):
    """Knowledge after line logic: grid of UNKNOWN/BLACK/WHITE."""
    side = len(grid)
    rows = [clue(r) for r in grid]
    cols = [clue([grid[y][x] for y in range(side)]) for x in range(side)]
    known = [[UNKNOWN] * side for _ in range(side)]
    dirty_rows, dirty_cols = set(range(side)), set(range(side))
    while dirty_rows or dirty_cols:
        for y in sorted(dirty_rows):
            new = solve_line(rows[y], known[y])
            for x in range(side):
                if new[x] != known[y][x]:
                    dirty_cols.add(x)
            known[y] = new
        dirty_rows.clear()
        for x in sorted(dirty_cols):
            new = solve_line(cols[x], [known[y][x] for y in range(side)])
            for y in range(side):
                if new[y] != known[y][x]:
                    dirty_rows.add(y)
                    known[y][x] = new[y]
        dirty_cols.clear()
    return known


def unknown_cells(grid):
    known = line_solve(grid)
    return [(y, x) for y, row in enumerate(known) for x, k in enumerate(row) if k == UNKNOWN]


def fixes(grid):
    """Single-cell flips that make the picture line-solvable."""
    out = []
    side = len(grid)
    for y in range(side):
        for x in range(side):
            grid[y][x] ^= 1
            if not unknown_cells(grid):
                out.append((y, x))
            grid[y][x] ^= 1
    return out


def parse(text):
    lines = text.splitlines()
    if not lines or lines[0] != HEADER:
        raise ValueError(f"first line must be {HEADER!r}")
    pictures, i = [], 1
    while i < len(lines):
        if not lines[i].strip():
            i += 1
            continue
        head = lines[i].split(" ", 2)
        if len(head) != 3:
            raise ValueError(f"line {i + 1}: expected '<id> <side> <name>'")
        pid, side, name = int(head[0]), int(head[1]), head[2]
        rows = lines[i + 1 : i + 1 + side]
        if len(rows) != side or any(len(r) != side or set(r) - {"#", "."} for r in rows):
            raise ValueError(f"picture {pid}: needs {side} rows of {side} '#'/'.'")
        pictures.append((pid, side, name, [[1 if c == "#" else 0 for c in r] for r in rows]))
        i += 1 + side
    return pictures


def check(pictures):
    errors, ids = [], set()
    for pid, side, name, grid in pictures:
        tag = f"{pid} {name}"
        if not 1 <= pid <= 255 or pid in ids:
            errors.append(f"{tag}: id must be unique in 1..255")
        ids.add(pid)
        if side not in SIDES:
            errors.append(f"{tag}: side must be one of {SIDES}")
        if not name or len(name.encode()) > NAME_MAX_BYTES or any(c in name for c in '"\\') or name != name.strip():
            errors.append(f"{tag}: bad name")
        if unknown := unknown_cells(grid):
            hint = ", ".join(f"r{y}c{x}" for y, x in fixes(grid)[:12]) or "none"
            errors.append(f"{tag}: not line-solvable, {len(unknown)} cells open; single flips that fix it: {hint}")
    return errors


def emit(pictures, out_dir):
    folder = Path(out_dir) / "nonograms"
    folder.mkdir(parents=True, exist_ok=True)
    lines = [HEADER]
    for pid, side, name, grid in pictures:
        lines.append(f"{pid} {side} {name}")
        lines.extend("".join("#" if c else "." for c in row) for row in grid)
    (folder / "pictures.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (folder / "ATTRIBUTION.txt").write_text(ATTRIBUTION, encoding="utf-8")


def preview(pictures, path):
    from PIL import Image, ImageDraw

    cell, gap, per_row = 8, 24, 8
    tile = 15 * cell + gap
    rows = (len(pictures) + per_row - 1) // per_row
    image = Image.new("1", (per_row * tile + gap, rows * (tile + 12) + gap), 1)
    draw = ImageDraw.Draw(image)
    for n, (pid, _, _, grid) in enumerate(pictures):
        ox = gap + n % per_row * tile
        oy = gap + n // per_row * (tile + 12)
        draw.text((ox, oy - 12), str(pid), fill=0)
        for y, row in enumerate(grid):
            for x, c in enumerate(row):
                box = (ox + x * cell, oy + y * cell, ox + x * cell + cell - 1, oy + y * cell + cell - 1)
                draw.rectangle(box, fill=0 if c else 1, outline=0)
    image.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("pictures")
    parser.add_argument("out_dir")
    parser.add_argument("--preview")
    args = parser.parse_args()
    pictures = parse(Path(args.pictures).read_text(encoding="utf-8"))
    if args.preview:
        preview(pictures, args.preview)
    if errors := check(pictures):
        print("\n".join(errors), file=sys.stderr)
        return 1
    emit(pictures, args.out_dir)
    sizes = {s: sum(1 for p in pictures if p[1] == s) for s in SIDES}
    print(f"{len(pictures)} pictures ({', '.join(f'{v} of {s}x{s}' for s, v in sizes.items())}) -> {args.out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
