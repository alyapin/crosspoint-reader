"""QA preview of a .scw puzzle as a PNG.

    python -I preview.py PUZZLE.scw OUT.png [--blank] [--cell 48]

Clue cells are grey with the clue text, arrows sit in the answer's first cell
on the side of its clue, letters show the solution unless --blank.
"""

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generator as G

FONTS = (
    "/System/Library/Fonts/Supplemental/Arial Narrow.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed.ttf",
)


def font(size):
    for path in FONTS:
        if Path(path).exists():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default(size)


def wrap(text, fnt, width):
    """Greedy word wrap; words wider than the cell are split with a hyphen."""
    lines, cur = [], ""
    for word in text.split():
        while fnt.getlength(word) > width:
            cut = len(word) - 1
            while cut > 1 and fnt.getlength(word[:cut] + "-") > width:
                cut -= 1
            if cur:
                lines.append(cur)
                cur = ""
            lines.append(word[:cut] + "-")
            word = word[cut:]
        trial = f"{cur} {word}".strip()
        if fnt.getlength(trial) <= width:
            cur = trial
        else:
            lines.append(cur)
            cur = word
    if cur:
        lines.append(cur)
    return lines


def fit_text(draw, box, text):
    """Largest font (7-11 px) that fits `text` into box (x0, y0, x1, y1)."""
    x0, y0, x1, y1 = box
    for size in range(11, 6, -1):
        fnt = font(size)
        lines = wrap(text, fnt, x1 - x0 - 2)
        height = len(lines) * (size + 1)
        if height <= y1 - y0 - 1:
            break
    y = y0 + (y1 - y0 - height) / 2
    for line in lines:
        w = fnt.getlength(line)
        draw.text((x0 + (x1 - x0 - w) / 2, y), line, font=fnt, fill=0)
        y += size + 1


def arrow(draw, start, arrow_kind, cell, half, halves):
    """Small arrow in the answer's first cell, on the edge facing its clue."""
    r, c = start
    x, y = c * cell, r * cell
    s = cell // 6
    if arrow_kind in ("R", "RD"):
        # Enters from the left edge; vertical position follows the clue's half.
        ay = y + (cell // 4 if halves == 2 and half == 0 else 3 * cell // 4 if halves == 2 else cell // 2)
        if arrow_kind == "R":
            draw.polygon([(x + 1, ay - s // 2), (x + 1, ay + s // 2), (x + s, ay)], fill=0)
        else:
            draw.line([(x + 1, ay), (x + s, ay), (x + s, ay + s)], fill=0, width=1)
            draw.polygon([(x + s - s // 2, ay + s), (x + s + s // 2, ay + s), (x + s, ay + s + s // 2)], fill=0)
    else:
        ax = x + cell // 2
        if arrow_kind == "D":
            draw.polygon([(ax - s // 2, y + 1), (ax + s // 2, y + 1), (ax, y + s)], fill=0)
        else:
            ax = x + cell // 4
            draw.line([(ax, y + 1), (ax, y + s), (ax + s, y + s)], fill=0, width=1)
            draw.polygon([(ax + s, y + s - s // 2), (ax + s, y + s + s // 2), (ax + s + s // 2, y + s)], fill=0)


def render(puzzle, cell=48, blank=False):
    w, h = puzzle.width, puzzle.height
    img = Image.new("L", (w * cell + 1, h * cell + 1), 255)
    draw = ImageDraw.Draw(img)
    letter_font = font(int(cell * 0.6))
    by_cell = {}
    for clue in puzzle.clues:
        by_cell.setdefault((clue[0], clue[1]), []).append(clue)
    for i, g in enumerate(puzzle.grid):
        r, c = divmod(i, w)
        x0, y0, x1, y1 = c * cell, r * cell, (c + 1) * cell, (r + 1) * cell
        if g == G.CLUE:
            draw.rectangle([x0, y0, x1, y1], fill=225)
            clues = by_cell.get((r, c), [])
            if len(clues) == 2:
                mid = (y0 + y1) // 2
                draw.line([(x0, mid), (x1, mid)], fill=0)
                fit_text(draw, (x0, y0, x1, mid), clues[0][3])
                fit_text(draw, (x0, mid, x1, y1), clues[1][3])
            elif clues:
                fit_text(draw, (x0, y0, x1, y1), clues[0][3])
        elif g == G.BLANK:
            for k in range(-cell, cell, 6):
                draw.line([(x0 + k, y1), (x0 + k + cell, y0)], fill=160)
        elif not blank:
            tw = letter_font.getlength(g)
            draw.text((x0 + (cell - tw) / 2, y0 + cell * 0.18), g, font=letter_font, fill=60)
        draw.rectangle([x0, y0, x1, y1], outline=0)
    for (r, c), clues in by_cell.items():
        for half, (_, _, a, _) in enumerate(clues):
            start = (r, c + 1) if a in ("R", "RD") else (r + 1, c)
            arrow(draw, start, a, cell, half, len(clues))
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("puzzle")
    ap.add_argument("out")
    ap.add_argument("--blank", action="store_true")
    ap.add_argument("--cell", type=int, default=48)
    args = ap.parse_args()
    puzzle = G.parse(Path(args.puzzle).read_text(encoding="utf-8"))
    render(puzzle, args.cell, args.blank).save(args.out)


if __name__ == "__main__":
    main()
