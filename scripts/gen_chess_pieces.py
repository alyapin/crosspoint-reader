#!/usr/bin/env python3
"""Chess piece bitmaps for src/games/chess/ui (X4 Pro builds).

Rasterizes four icon sets into 1-bpp mask + ink planes with a white halo baked
in and writes src/games/chess/ui/PieceSetsData.h. --preview instead renders
boards from the planes parsed out of that header, so the picture shows exactly
the bytes that go into firmware.

    python3 -I scripts/gen_chess_pieces.py
    clang-format -i src/games/chess/ui/PieceSetsData.h
    python3 -I scripts/gen_chess_pieces.py --preview out.png

Requires rsvg-convert (librsvg) and Pillow. SVGs are vendored in
src/games/chess/ui/pieces/ (Lucide comes from the freeink-sdk submodule).
"""

import argparse
import io
import re
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
UI = ROOT / "src/games/chess/ui"
PIECES = UI / "pieces"
LUCIDE = ROOT / "freeink-sdk/libs/assets/Icons/lucide/icons"
OUT = UI / "PieceSetsData.h"

KINDS = ["pawn", "knight", "bishop", "rook", "queen", "king"]  # PieceKind order (Pawn = 1)
COLOURS = ["white", "black"]  # PieceColor order
ART = [52, 28]  # PieceSize::Board, PieceSize::Small: tallest piece of a set, halo excluded
HALO = 2
# Openings up to ~2 * GAP px (e.g. Font Awesome's body/base split) are sealed
# when finding a piece's inside, so open outlines still get a white interior.
GAP = 2


def tabler(kind):
    return "chess" if kind == "pawn" else f"chess-{kind}"


# PieceSet order: (name, white, black, fill holes of the black piece). A colour
# maps a kind to (svg, root attributes to override). Lucide pieces are open
# strokes, so the black one only becomes a silhouette once its holes are filled.
SETS = [
    (
        "Lucide",
        lambda k: (LUCIDE / f"chess-{k}.svg", {"fill": "#fff", "stroke": "#000"}),
        lambda k: (LUCIDE / f"chess-{k}.svg", {"fill": "#000", "stroke": "#000"}),
        True,
    ),
    (
        "Tabler",
        lambda k: (PIECES / f"tabler/outline/{tabler(k)}.svg", {"fill": "#fff", "stroke": "#000"}),
        lambda k: (PIECES / f"tabler/filled/{tabler(k)}.svg", {"fill": "#000"}),
        False,
    ),
    (
        "Material",
        lambda k: (
            PIECES / f"mdi/chess-{k}.svg",
            {"fill": "#fff", "stroke": "#000", "stroke-width": "1.3", "stroke-linejoin": "round"},
        ),
        lambda k: (PIECES / f"mdi/chess-{k}.svg", {"fill": "#000"}),
        False,
    ),
    (
        "FontAwesome",
        lambda k: (PIECES / f"fontawesome/regular/chess-{k}.svg", {"fill": "#000"}),
        lambda k: (PIECES / f"fontawesome/solid/chess-{k}.svg", {"fill": "#000"}),
        False,
    ),
]

ET.register_namespace("", "http://www.w3.org/2000/svg")


def load(path, attrs):
    root = ET.parse(path).getroot()
    for key, value in attrs.items():
        root.set(key, value)
    return root


def render(root, x, y, side, px):
    """Rasterize the square user-space window (x, y, side) to px x px RGBA."""
    root.set("viewBox", f"{x:.4f} {y:.4f} {side:.4f} {side:.4f}")
    root.set("width", str(px))
    root.set("height", str(px))
    png = subprocess.run(["rsvg-convert"], input=ET.tostring(root), capture_output=True, check=True).stdout
    return Image.open(io.BytesIO(png)).convert("RGBA")


def art_bbox(root):
    """Painted extent in user units (x0, y0, x1, y1), measured at high resolution."""
    vx, vy, vw, vh = (float(v) for v in root.get("viewBox").replace(",", " ").split())
    side = max(vw, vh) * 1.2
    x, y, px = vx + vw / 2 - side / 2, vy + vh / 2 - side / 2, 1024
    box = render(root, x, y, side, px).getchannel("A").getbbox()
    k = side / px
    return (x + box[0] * k, y + box[1] * k, x + box[2] * k, y + box[3] * k)


def disc(r):
    return [(dx, dy) for dy in range(-r, r + 1) for dx in range(-r, r + 1) if dx * dx + dy * dy <= r * r + 1]


def dilate(cells, r):
    return {(x + dx, y + dy) for x, y in cells for dx, dy in disc(r)}


def outside(blocked, n):
    """Background cells 4-connected to a border drawn GAP + 1 cells outside the canvas."""
    lo, hi = -GAP - 1, n + GAP
    seen = set()
    stack = [(lo, lo)]
    while stack:
        x, y = stack.pop()
        if not (lo <= x <= hi and lo <= y <= hi) or (x, y) in seen or (x, y) in blocked:
            continue
        seen.add((x, y))
        stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return seen


def planes(img, label, solid):
    """(mask, ink) cell sets: mask = piece + interior + halo, ink = black pixels."""
    n = img.size[0]
    ink, shape = set(), set()
    for y in range(n):
        for x in range(n):
            r, g, b, a = img.getpixel((x, y))
            dark = a / 255 * (1 - (0.299 * r + 0.587 * g + 0.114 * b) / 255)
            if dark >= 0.5:
                ink.add((x, y))
            if a >= 128 or dark >= 0.5:
                shape.add((x, y))
    canvas = {(x, y) for y in range(n) for x in range(n)}
    if solid:
        ink = canvas - outside(shape, n)
    inside = canvas - dilate(outside(dilate(shape, GAP), n), GAP)
    if any(min(x, y, n - 1 - x, n - 1 - y) < HALO for x, y in inside):
        raise SystemExit(f"{label}: artwork leaves no room for the halo")
    return dilate(inside, HALO), ink


def pack(cells, n):
    """Rows top-to-bottom, MSB-first; bit 0 = cell in the set (freeink::Icon convention)."""
    row = (n + 7) // 8
    out = bytearray([0xFF] * (row * n))
    for x, y in cells:
        out[y * row + x // 8] &= ~(0x80 >> (x % 8)) & 0xFF
    return bytes(out)


def generate():
    arrays = {}  # bytes -> C name; identical planes are stored once
    lines, table = [], []
    for set_name, *sources, solid in SETS:
        roots = {(c, k): load(*sources[ci](k)) for ci, c in enumerate(COLOURS) for k in KINDS}
        boxes = {key: art_bbox(root) for key, root in roots.items()}
        union = {
            k: tuple(f(boxes[(c, k)][i] for c in COLOURS) for i, f in enumerate((min, min, max, max))) for k in KINDS
        }
        y0 = min(u[1] for u in union.values())
        y1 = max(u[3] for u in union.values())
        extent = max(y1 - y0, max(u[2] - u[0] for u in union.values()))
        set_rows = []
        for c in COLOURS:
            colour_rows = []
            for k in KINDS:
                entries = []
                for art in ART:
                    n = art + 2 * HALO
                    scale = art / extent
                    side = n / scale
                    # Centre the piece horizontally and the set vertically, with the
                    # set's common bottom edge on a pixel row so bases stay crisp.
                    wx = (union[k][0] + union[k][2]) / 2 - side / 2
                    wy = (y0 + y1) / 2 - side / 2
                    wy = y1 - round((y1 - wy) * scale) / scale
                    label = f"{set_name} {c} {k} {n}"
                    img = render(roots[(c, k)], wx, wy, side, n)
                    mask, ink = planes(img, label, solid and c == "black")
                    names = []
                    for plane, data in (("mask", pack(mask, n)), ("ink", pack(ink, n))):
                        if data not in arrays:
                            arrays[data] = f"piece_{set_name.lower()}_{c}_{k}_{n}_{plane}"
                            body = ", ".join(f"0x{b:02X}" for b in data)
                            lines.append(f"static const uint8_t {arrays[data]}[] = {{{body}}};")
                        names.append(arrays[data])
                    entries.append(f"{{{n}, {names[0]}, {names[1]}}}")
                colour_rows.append(f"{{{', '.join(entries)}}}")
            set_rows.append(f"{{{', '.join(colour_rows)}}}")
        table.append(f"{{{', '.join(set_rows)}}}")

    header = [
        "#pragma once",
        "",
        "// Generated by scripts/gen_chess_pieces.py. Do not edit.",
        "// Sources: Lucide (ISC, freeink-sdk/libs/assets/Icons/lucide), Tabler Icons 3.34.0 (MIT),",
        "// Material Design Icons 7.4.47 (Apache 2.0), Font Awesome Free 6.5.1 (CC BY 4.0).",
        "// Licence files: src/games/chess/ui/pieces/.",
        f"// Art {ART[0]} / {ART[1]} px plus a {HALO} px halo. Plane format: PieceBitmap in PieceSets.h.",
        "",
        "#include <cstdint>",
        "",
        '#include "PieceSets.h"',
        "",
        *lines,
        "",
        "// [PieceSet][PieceColor][PieceKind - 1][PieceSize]",
        "static const chess::ui::PieceBitmap piece_bitmaps[chess::ui::PIECE_SET_COUNT][2][6][2] = {"
        + ", ".join(table)
        + "};",
        "",
    ]
    OUT.write_text("\n".join(header))
    print(f"wrote {OUT.relative_to(ROOT)}: {len(arrays)} planes, {sum(len(d) for d in arrays)} bytes")


# --- preview -----------------------------------------------------------------

SQUARE = 60
START = "r1bqkbnr/1ppp1ppp/p1n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R"  # 1.e4 e5 2.Nf3 Nc6 3.Bb5 a6
FEN_KIND = {"p": 0, "n": 1, "b": 2, "r": 3, "q": 4, "k": 5}


def read_header():
    text = OUT.read_text()
    arrays = {
        name: bytes(int(v, 16) for v in body.replace(",", " ").split())
        for name, body in re.findall(r"static const uint8_t (\w+)\[\] = \{([^}]*)\};", text)
    }
    table = text[text.index("piece_bitmaps[") :]
    entries = [(int(n), arrays[m], arrays[i]) for n, m, i in re.findall(r"\{\s*(\d+),\s*(\w+),\s*(\w+)\s*\}", table)]
    assert len(entries) == len(SETS) * 2 * 6 * 2, len(entries)
    return entries


def dither(img, x0, y0, w, h):
    """GfxRenderer LightGray: black where logical x and y are both even."""
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            img.putpixel((x, y), 0 if x % 2 == 0 and y % 2 == 0 else 255)


def blit(img, piece, x0, y0):
    """Same compositing as chess::ui::drawPiece."""
    n, mask, ink = piece
    row = (n + 7) // 8
    for y in range(n):
        for x in range(n):
            bit = 0x80 >> (x % 8)
            if not mask[y * row + x // 8] & bit:
                img.putpixel((x0 + x, y0 + y), 0 if not ink[y * row + x // 8] & bit else 255)


def preview(path):
    entries = read_header()

    def piece(s, c, k, size):
        return entries[((s * 2 + c) * 6 + k) * 2 + size]

    board = 8 * SQUARE
    small = piece(0, 0, 0, 1)[0]
    label_h, pad = 32, 24
    cell_w, cell_h = board, label_h + board + 2 * (small + 12)
    img = Image.new("L", (2 * cell_w + 3 * pad, 2 * cell_h + 3 * pad), 255)
    draw = ImageDraw.Draw(img)
    font = ImageFont.load_default(size=20)
    for s, (name, *_) in enumerate(SETS):
        ox = pad + (s % 2) * (cell_w + pad)
        oy = pad + (s // 2) * (cell_h + pad)
        draw.text((ox, oy + 4), name, fill=0, font=font)
        by = oy + label_h
        for rank, row in enumerate(START.split("/")):
            for f in range(8):
                if (f + rank) % 2:
                    dither(img, ox + f * SQUARE, by + rank * SQUARE, SQUARE, SQUARE)
            file = 0
            for ch in row:
                if ch.isdigit():
                    file += int(ch)
                    continue
                p = piece(s, 0 if ch.isupper() else 1, FEN_KIND[ch.lower()], 0)
                off = (SQUARE - p[0]) // 2
                blit(img, p, ox + file * SQUARE + off, by + rank * SQUARE + off)
                file += 1
        draw.rectangle((ox - 1, by - 1, ox + board, by + board), outline=0)
        # Small size: all twelve pieces on white, then on the dark-square dither.
        for strip in range(2):
            sy = by + board + 12 + strip * (small + 12)
            if strip:
                dither(img, ox, sy, board, small)
            for i in range(12):
                blit(img, piece(s, i // 6, i % 6, 1), ox + i * (small + 8), sy)
    img.save(path)
    print(f"wrote {path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", metavar="PNG", help="render boards from the existing header instead")
    args = ap.parse_args()
    if args.preview:
        preview(args.preview)
    else:
        generate()


if __name__ == "__main__":
    main()
