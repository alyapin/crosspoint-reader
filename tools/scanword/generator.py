"""Scanword generator: clue-cell template search, word fill, SD file writer.

    python -I generator.py LEXICON.tsv OUT_DIR [--count 200] [--seed 1]

Grid model: every cell is a clue cell or a letter. Every maximal run of two or
more letters is an answer of 3-10 letters, every letter is in at least one
answer, and every answer is announced by an adjacent clue cell holding at most
two clues. Arrows: R (clue left of an across answer), D (clue above a down
answer), DR (clue above an across answer), RD (clue left of a down answer).
"""

import argparse
import math
import random
import sys
from dataclasses import dataclass, field
from pathlib import Path

ALPHABET = "абвгдежзийклмнопрстуфхцчшщъыьэюя"  # ё folds to е
CODE = {ch: i for i, ch in enumerate(ALPHABET)}
CLUE, LETTER, BLANK = "#", "L", "."
ACROSS, DOWN = 0, 1
MIN_LEN, MAX_LEN = 3, 10
MAX_CLUE = 26
# A two-clue cell has half the room, so its clues may be capped shorter.
SPLIT_CLUE = MAX_CLUE
ARROW_ORDER = {"R": 0, "RD": 1, "DR": 2, "D": 3}
ATTRIBUTION = """Clues and answers are derived from the Russian Wiktionary
(https://ru.wiktionary.org), licensed under CC BY-SA 4.0
(https://creativecommons.org/licenses/by-sa/4.0/). Words were ranked with
wordfreq (CC BY-SA 4.0) and pymorphy3 / OpenCorpora data. These puzzles are
shared under the same licence, CC BY-SA 4.0.
"""
FORMAT_HEADER = "SCANWORD 1"
INDEX_HEADER = "SCANWORDS 1"
# Per level: lowest word frequency (Zipf) and how much shuffle noise the
# frequency ordering gets. Level 1 sticks to everyday words.
LEVELS = {1: (3.2, 0.35), 2: (2.7, 0.6), 3: (2.2, 0.9)}
# Short words are the glue of a scanword and there are few of them, so they may
# be rarer than the level allows and repeat more often across a pack.
SHORT_LEN, SHORT_ZIPF_BONUS = 4, 0.6
MAX_USES_SHORT, MAX_USES_LONG = 25, 3
# Template cost weights.
W_CLUE, W_DOUBLE, W_UNCHECKED, W_SHORT, W_LONG, W_BLOCK = 1.0, 1.5, 0.2, 0.3, 1.5, 1.2


def fold(word):
    return word.lower().replace("ё", "е")


@dataclass
class Entry:
    word: str
    zipf: float
    clue: str


def clue_lines(text, chars):
    """Lines `text` takes when wrapped at `chars` per line, splitting long words with a hyphen."""
    lines, used = 1, 0
    for word in text.split():
        need = len(word) + (1 if used else 0)
        if used + need <= chars:
            used += need
            continue
        if len(word) <= chars:
            lines, used = lines + 1, len(word)
            continue
        # Hyphenate: fill the current line (two letters minimum), carry the rest.
        rest = len(word)
        room = chars - used - (1 if used else 0) - 1
        if room >= 2:
            rest -= room
        else:
            lines += 1
            rest -= chars - 1
        while rest > chars:
            lines += 1
            rest -= chars - 1
        lines, used = lines + 1, rest
    return lines


def load_lexicon(path):
    entries, seen = [], set()
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        word, zipf, clue = line.split("\t")[:3]
        key = fold(word)
        if key in seen or not MIN_LEN <= len(key) <= MAX_LEN or any(ch not in CODE for ch in key):
            continue
        seen.add(key)
        entries.append(Entry(word, float(zipf), clue))
    return entries


# --- template -------------------------------------------------------------


@dataclass
class Slot:
    row: int
    col: int
    dir: int
    length: int

    def cells(self, width):
        dr, dc = (0, 1) if self.dir == ACROSS else (1, 0)
        return [(self.row + dr * i) * width + self.col + dc * i for i in range(self.length)]


def runs(kind, width, height):
    """Maximal letter runs (length >= 2) as Slots; 1-runs are not answers."""
    out = []
    for r in range(height):
        c = 0
        while c < width:
            if kind[r * width + c] != LETTER:
                c += 1
                continue
            s = c
            while c < width and kind[r * width + c] == LETTER:
                c += 1
            if c - s >= 2:
                out.append(Slot(r, s, ACROSS, c - s))
    for c in range(width):
        r = 0
        while r < height:
            if kind[r * width + c] != LETTER:
                r += 1
                continue
            s = r
            while r < height and kind[r * width + c] == LETTER:
                r += 1
            if r - s >= 2:
                out.append(Slot(s, c, DOWN, r - s))
    return out


def clue_options(slot, kind, width):
    """(clue cell, arrow) choices for a slot, straight arrow first."""
    r, c = slot.row, slot.col
    opts = []
    left = r * width + c - 1 if c > 0 else None
    above = (r - 1) * width + c if r > 0 else None
    if slot.dir == ACROSS:
        if left is not None and kind[left] == CLUE:
            opts.append((left, "R"))
        if above is not None and kind[above] == CLUE:
            opts.append((above, "DR"))
    else:
        if above is not None and kind[above] == CLUE:
            opts.append((above, "D"))
        if left is not None and kind[left] == CLUE:
            opts.append((left, "RD"))
    return opts


def assign_clues(slots, kind, width):
    """Slot -> (clue cell, arrow); single-clue cells first, then pairs.

    Returns (owner list with None for unplaced slots, load per clue cell)."""
    options = [clue_options(s, kind, width) for s in slots]
    owner = [None] * len(slots)
    load = {}

    def place(s, cap, seen):
        for cell, arrow in options[s]:
            if cell in seen:
                continue
            seen.add(cell)
            here = load.setdefault(cell, [])
            if len(here) < cap:
                here.append(s)
                owner[s] = (cell, arrow)
                return True
            for t in list(here):
                if place(t, cap, seen):
                    here.remove(t)
                    here.append(s)
                    owner[s] = (cell, arrow)
                    return True
        return False

    order = sorted(range(len(slots)), key=lambda i: len(options[i]))
    for cap in (1, 2):
        for s in order:
            if owner[s] is None:
                place(s, cap, set())
    # Give empty clue cells a clue taken from a cell holding two.
    for cell in range(len(kind)):
        if kind[cell] != CLUE or load.get(cell):
            continue
        for s, opts in enumerate(options):
            mine = [a for cc, a in opts if cc == cell]
            if mine and owner[s] and len(load[owner[s][0]]) == 2:
                load[owner[s][0]].remove(s)
                owner[s] = (cell, mine[0])
                load[cell] = [s]
                break
    return owner, load


@dataclass
class Template:
    width: int
    height: int
    kind: list
    slots: list = field(default_factory=list)
    owner: list = field(default_factory=list)
    dead: int = 0
    doubles: int = 0


def template_cost(kind, width, height):
    """(hard violations, soft cost, slots, owner, dead, doubles)."""
    slots = runs(kind, width, height)
    hard = 0
    covered = [False] * len(kind)
    long_slots = 0
    for s in slots:
        if s.length < MIN_LEN or s.length > MAX_LEN:
            hard += 1
        long_slots += s.length >= 8
        for cell in s.cells(width):
            covered[cell] = True
    letters = [i for i, k in enumerate(kind) if k == LETTER]
    hard += sum(not covered[i] for i in letters)
    owner, load = assign_clues(slots, kind, width)
    hard += sum(o is None for o in owner)
    clue_cells = [i for i, k in enumerate(kind) if k == CLUE]
    dead = sum(not load.get(i) for i in clue_cells)
    doubles = sum(len(load.get(i, ())) == 2 for i in clue_cells)
    crossed = [0] * len(kind)
    for s in slots:
        for cell in s.cells(width):
            crossed[cell] += 1
    unchecked = sum(crossed[i] == 1 for i in letters)
    short = sum(s.length == 3 for s in slots)
    # Solid letter blocks make every row and column a long crossing answer;
    # with a noun-only lexicon they rarely fill.
    blocks = sum(
        kind[i] == kind[i + 1] == kind[i + width] == kind[i + width + 1] == LETTER
        for i in range(len(kind) - width)
        if i % width != width - 1
    )
    soft = (
        W_CLUE * len(clue_cells)
        + W_DOUBLE * doubles
        + 25 * dead
        + W_UNCHECKED * unchecked
        + W_SHORT * short
        + W_LONG * long_slots
        + W_BLOCK * blocks
    )
    return hard, soft, slots, owner, dead, doubles


def make_template(width, height, rng, steps=6000):
    """Simulated annealing over clue/letter cells."""
    kind = [LETTER] * (width * height)
    for c in range(0, width, 2):
        kind[c] = CLUE
    for r in range(0, height, 2):
        kind[r * width] = CLUE
    for i in range(width * height):
        r, c = divmod(i, width)
        if r and c and rng.random() < 0.18:
            kind[i] = CLUE
    hard, soft = template_cost(kind, width, height)[:2]
    cost = hard * 100 + soft
    best = (cost, list(kind))
    for step in range(steps):
        temp = 6.0 * (1 - step / steps) + 0.05
        i = rng.randrange(1, width * height)
        kind[i] = LETTER if kind[i] == CLUE else CLUE
        h, s = template_cost(kind, width, height)[:2]
        new = h * 100 + s
        if new <= cost or rng.random() < math.exp((cost - new) / temp):
            cost = new
            if cost < best[0]:
                best = (cost, list(kind))
        else:
            kind[i] = LETTER if kind[i] == CLUE else CLUE
    kind = best[1]
    hard, _, slots, owner, dead, doubles = template_cost(kind, width, height)
    if hard or dead > 1:
        return None
    for cell in range(len(kind)):
        if kind[cell] == CLUE and not any(o and o[0] == cell for o in owner):
            kind[cell] = BLANK
    return Template(width, height, kind, slots, owner, dead, doubles)


# --- fill -----------------------------------------------------------------


class WordIndex:
    """Per-length bitsets of candidate words, ordered by preference."""

    def __init__(self, entries):
        self.by_len = {}
        for e in entries:
            self.by_len.setdefault(len(e.word), []).append(e)
        self.all = {}
        self.bits = {}
        self.short = {}  # words whose clue fits half a cell
        for length, words in self.by_len.items():
            pos = [[0] * len(ALPHABET) for _ in range(length)]
            short = 0
            for i, e in enumerate(words):
                for p, ch in enumerate(fold(e.word)):
                    pos[p][CODE[ch]] |= 1 << i
                if len(e.clue) <= SPLIT_CLUE:
                    short |= 1 << i
            self.bits[length] = pos
            self.all[length] = (1 << len(words)) - 1
            self.short[length] = short


def ordered(entries, rng, noise, uses):
    return sorted(entries, key=lambda e: -(e.zipf + rng.gauss(0, noise) - 0.8 * uses.get(fold(e.word), 0)))


def fill(template, index, rng, max_nodes=20000, branch=10):
    """Assigns a word to every slot; returns [Entry] per slot or None."""
    width = template.width
    slots = template.slots
    cells = [s.cells(width) for s in slots]
    crossing = [[] for _ in slots]
    at = {}
    for si, cs in enumerate(cells):
        for p, cell in enumerate(cs):
            at.setdefault(cell, []).append((si, p))
    for si, cs in enumerate(cells):
        crossing[si] = sorted({o for cell in cs for o, _ in at[cell] if o != si})
    owners = [o[0] for o in template.owner]
    split = {si for si, cell in enumerate(owners) if owners.count(cell) == 2}
    letter = {}
    chosen = [None] * len(slots)
    used = {length: 0 for length in index.all}
    clues = set()  # two answers with one clue text would confuse the solver
    nodes = 0

    def domain(si):
        s = slots[si]
        d = index.all.get(s.length, 0) & ~used.get(s.length, 0)
        if si in split:
            d &= index.short.get(s.length, 0)
        bits = index.bits.get(s.length)
        for p, cell in enumerate(cells[si]):
            ch = letter.get(cell)
            if ch is not None:
                d &= bits[p][ch]
                if not d:
                    return 0
        return d

    def search():
        nonlocal nodes
        nodes += 1
        if nodes > max_nodes:
            return False
        best, best_n, best_d = None, None, 0
        for si in range(len(slots)):
            if chosen[si] is not None:
                continue
            d = domain(si)
            n = d.bit_count()
            if n == 0:
                return False
            key = (n, -slots[si].length)
            if best is None or key < best_n:
                best, best_n, best_d = si, key, d
        if best is None:
            return True
        length = slots[best].length
        d, tried = best_d, 0
        while d and tried < branch:
            low = d & -d
            d ^= low
            wi = low.bit_length() - 1
            tried += 1
            entry = index.by_len[length][wi]
            if entry.clue in clues:
                continue
            placed = []
            for cell, ch in zip(cells[best], fold(entry.word)):
                if cell not in letter:
                    letter[cell] = CODE[ch]
                    placed.append(cell)
            chosen[best] = entry
            used[length] |= low
            clues.add(entry.clue)
            if all(chosen[o] is not None or domain(o) for o in crossing[best]) and search():
                return True
            clues.discard(entry.clue)
            used[length] &= ~low
            chosen[best] = None
            for cell in placed:
                del letter[cell]
            if nodes > max_nodes:
                return False
        return False

    return chosen if search() else None


# --- puzzle ---------------------------------------------------------------


@dataclass
class Puzzle:
    width: int
    height: int
    level: int
    grid: list  # per cell: '#', '.', or an uppercase letter (Ё only if every answer has ё there)
    clues: list  # (row, col, arrow, text) in file order

    def text(self):
        lines = [FORMAT_HEADER, f"size {self.width} {self.height}", f"level {self.level}", "grid"]
        for r in range(self.height):
            lines.append("".join(self.grid[r * self.width : (r + 1) * self.width]))
        lines.append("clues")
        lines += [f"{r} {c} {a} {t}" for r, c, a, t in self.clues]
        lines.append("end")
        return "\n".join(lines) + "\n"


def build_puzzle(template, words, level):
    width = template.width
    grid = [CLUE if k == CLUE else BLANK if k == BLANK else None for k in template.kind]
    yo = {}
    for slot, entry in zip(template.slots, words):
        for cell, ch in zip(slot.cells(width), entry.word.lower()):
            grid[cell] = fold(ch).upper()
            yo[cell] = yo.get(cell, True) and ch == "ё"
    for cell, all_yo in yo.items():
        if all_yo:
            grid[cell] = "Ё"
    clues = []
    for slot, entry, (cell, arrow) in zip(template.slots, words, template.owner):
        r, c = divmod(cell, width)
        clues.append((r, c, arrow, entry.clue))
    clues.sort(key=lambda t: (t[0], t[1], ARROW_ORDER[t[2]]))
    return Puzzle(width, template.height, level, grid, clues)


def parse(text):
    """Reads a .scw file back (strict); raises ValueError on malformed input."""
    lines = text.splitlines()
    if not lines or lines[0] != FORMAT_HEADER:
        raise ValueError("bad header")
    _, w, h = lines[1].split()
    width, height = int(w), int(h)
    level = int(lines[2].split()[1])
    if lines[3] != "grid":
        raise ValueError("no grid")
    rows = lines[4 : 4 + height]
    if any(len(r) != width for r in rows) or lines[4 + height] != "clues":
        raise ValueError("bad grid")
    clues = []
    for line in lines[5 + height :]:
        if line == "end":
            break
        r, c, a, t = line.split(" ", 3)
        clues.append((int(r), int(c), a, t))
    else:
        raise ValueError("no end")
    return Puzzle(width, height, level, list("".join(rows)), clues)


def answers(p):
    """Answer per clue: (clue, cells, word). Raises ValueError on bad arrows."""
    out = []
    for r, c, a, t in p.clues:
        if p.grid[r * p.width + c] != CLUE:
            raise ValueError(f"clue at non-clue cell {r},{c}")
        sr, sc = (r, c + 1) if a in ("R", "RD") else (r + 1, c)
        dr, dc = (0, 1) if a in ("R", "DR") else (1, 0)
        cells = []
        rr, cc = sr, sc
        while 0 <= rr < p.height and 0 <= cc < p.width and p.grid[rr * p.width + cc] not in (CLUE, BLANK):
            cells.append(rr * p.width + cc)
            rr, cc = rr + dr, cc + dc
        pr, pc = sr - dr, sc - dc
        if 0 <= pr < p.height and 0 <= pc < p.width and p.grid[pr * p.width + pc] not in (CLUE, BLANK):
            raise ValueError(f"answer of {r},{c} {a} does not start a run")
        out.append(((r, c, a, t), cells, "".join(p.grid[i] for i in cells)))
    return out


def validate(p, lexicon=None):
    """Lists invariant violations of a puzzle (empty when valid)."""
    errors = []
    try:
        ans = answers(p)
    except ValueError as e:
        return [str(e)]
    kinds = [CLUE if g == CLUE else BLANK if g == BLANK else LETTER for g in p.grid]
    expected = {(s.row, s.col, s.dir, s.length) for s in runs(kinds, p.width, p.height)}
    got = set()
    per_cell = {}
    for (r, c, a, t), cells, word in ans:
        start = cells[0] if cells else None
        d = ACROSS if a in ("R", "DR") else DOWN
        if start is None:
            errors.append(f"empty answer at {r},{c} {a}")
            continue
        key = (start // p.width, start % p.width, d, len(cells))
        if key in got:
            errors.append(f"two clues for one answer at {key}")
        got.add(key)
        per_cell[(r, c)] = per_cell.get((r, c), 0) + 1
        if not MIN_LEN <= len(cells) <= MAX_LEN:
            errors.append(f"answer length {len(cells)} at {r},{c}")
        if not t or len(t) > MAX_CLUE:
            errors.append(f"clue length at {r},{c}: {t!r}")
    if expected - got:
        errors.append(f"runs without a clue: {sorted(expected - got)}")
    if got - expected:
        errors.append(f"answers that are not maximal runs: {sorted(got - expected)}")
    covered = {cell for _, cells, _ in ans for cell in cells}
    for i, k in enumerate(kinds):
        if k == LETTER and i not in covered:
            errors.append(f"letter {divmod(i, p.width)} in no answer")
        if k == CLUE and not per_cell.get(divmod(i, p.width)):
            errors.append(f"empty clue cell {divmod(i, p.width)}")
    if any(n > 2 for n in per_cell.values()):
        errors.append("clue cell with more than two clues")
    words = [fold(w) for _, _, w in ans]
    if len(set(words)) != len(words):
        errors.append("duplicate answers")
    texts = [t for (_, _, _, t), _, _ in ans]
    if len(set(texts)) != len(texts):
        errors.append("duplicate clue texts")
    if lexicon is not None:
        for (r, c, a, t), _, w in ans:
            e = lexicon.get(fold(w))
            if e is None:
                errors.append(f"answer {w} not in lexicon")
            elif e.clue != t:
                errors.append(f"clue of {w} does not match the lexicon")
            elif "Ё" in w and "ё" not in e.word:
                errors.append(f"Ё shown in {w}")
    return errors


def generate(entries, level, rng, uses, width=10, height=14, attempts=40):
    min_zipf, noise = LEVELS[level]
    pool = [
        e
        for e in entries
        if e.zipf >= min_zipf - (SHORT_ZIPF_BONUS if len(e.word) <= SHORT_LEN else 0)
        and uses.get(fold(e.word), 0) < (MAX_USES_SHORT if len(e.word) <= SHORT_LEN else MAX_USES_LONG)
    ]
    for _ in range(attempts):
        template = make_template(width, height, rng)
        if template is None:
            continue
        for _ in range(3):
            index = WordIndex(ordered(pool, rng, noise, uses))
            words = fill(template, index, rng)
            if words:
                return build_puzzle(template, words, level), words
    return None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("lexicon")
    ap.add_argument("out")
    ap.add_argument("--count", type=int, default=200)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--width", type=int, default=10)
    ap.add_argument("--height", type=int, default=14)
    ap.add_argument("--max-clue", type=int, default=MAX_CLUE, help="skip words with longer clues")
    ap.add_argument("--split-clue", type=int, default=MAX_CLUE, help="clue length cap in two-clue cells")
    ap.add_argument("--line-chars", type=int, default=0, help="skip clues longer than 3 lines of this many chars")
    args = ap.parse_args()
    global SPLIT_CLUE
    SPLIT_CLUE = args.split_clue
    entries = [
        e
        for e in load_lexicon(args.lexicon)
        if len(e.clue) <= args.max_clue and (not args.line_chars or clue_lines(e.clue, args.line_chars) <= 3)
    ]
    lex = {fold(e.word): e for e in entries}
    rng = random.Random(args.seed)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    uses = {}
    index_lines = [INDEX_HEADER]
    n = 0
    for _ in range(3 * args.count):
        if n == args.count:
            break
        level = 1 + n % 3
        puzzle, words = generate(entries, level, rng, uses, args.width, args.height)
        if puzzle is None:
            print(f"puzzle {n + 1}: no fill, retrying", file=sys.stderr)
            continue
        n += 1
        errors = validate(puzzle, lex)
        if errors:
            raise SystemExit(f"puzzle {n} invalid: {errors[:3]}")
        for e in words:
            uses[fold(e.word)] = uses.get(fold(e.word), 0) + 1
        name = f"{n:04d}"
        (out / f"{name}.scw").write_text(puzzle.text(), encoding="utf-8")
        index_lines.append(f"{name} {level}")
        zipf = sum(e.zipf for e in words) / len(words)
        print(f"{name} level {level}: {len(words)} words, zipf {zipf:.2f}", file=sys.stderr)
    (out / "index.txt").write_text("\n".join(index_lines) + "\n", encoding="utf-8")
    (out / "ATTRIBUTION.txt").write_text(ATTRIBUTION, encoding="utf-8")


if __name__ == "__main__":
    main()
