"""Codeword generator: block template, dense word fill, uniqueness check, SD pack writer.

    python -I tools/codeword/generator.py LEXICON.tsv OUT_DIR [--count 120] [--seed 1] [--jobs 8]

LEXICON.tsv comes from tools/scanword/lexicon.py (word, Zipf, clue, kind). The
fill takes nouns of 3-10 letters at Zipf >= FILL_ZIPF minus
tools/wordlists/blocked.py (EXCLUDED and RUDE). The uniqueness check runs
against every lexicon word: with the starter letters revealed, exactly one
letter-per-number assignment may turn every answer into a lexicon word. Ё folds
into Е.

Grid model: '.' blocks and letter cells. Every maximal run of two or more
letters is an answer of 3-10 letters, every letter is in an answer, the
letters are connected and no 2x2 square is all letters.
"""

import argparse
import math
import random
import sys
from multiprocessing import Pool
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "wordlists"))

import blocked  # noqa: E402

SIZE = 11
ALPHABET = "абвгдежзийклмнопрстуфхцчшщъыьэюя"  # ё folds to е
CODE = {ch: i for i, ch in enumerate(ALPHABET)}
BLOCK, LETTER = ".", "L"
MIN_LEN, MAX_LEN = 3, 10
FILL_ZIPF = 2.8
GIVEN = 2
MIN_LETTERS = 64  # letter cells of 121
MIN_DISTINCT = 20  # numbers in use
FORMAT_HEADER = "CODEWORD 1"
INDEX_HEADER = "CODEWORDS 1"
ATTRIBUTION = """The codeword answers come from the Russian Wiktionary
(https://ru.wiktionary.org) by the Russian Wiktionary contributors, licensed
under CC BY-SA 4.0 (https://creativecommons.org/licenses/by-sa/4.0/). Words
were ranked with wordfreq data (CC BY-SA 4.0) and checked with pymorphy3
against OpenCorpora dictionary data (CC BY-SA 3.0,
https://creativecommons.org/licenses/by-sa/3.0/). The puzzles are shared
under CC BY-SA 4.0.
"""


def fold(word):
    return blocked.fold(word)


def load_words(path):
    """(fill entries as (word, zipf), check set) of folded words."""
    skip = {fold(w) for w in blocked.EXCLUDED | blocked.RUDE}
    fill, check, seen = [], set(), set()
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        word, zipf = line.split("\t")[:2]
        key = fold(word)
        if not MIN_LEN <= len(key) <= MAX_LEN or any(ch not in CODE for ch in key):
            continue
        check.add(key)
        if key in seen or key in skip or float(zipf) < FILL_ZIPF:
            continue
        seen.add(key)
        fill.append((key, float(zipf)))
    return fill, check


# --- template -------------------------------------------------------------


def runs(kind):
    """Maximal letter runs of length >= 2 as cell lists."""
    out = []
    for line in [[r * SIZE + c for c in range(SIZE)] for r in range(SIZE)] + [
        [r * SIZE + c for r in range(SIZE)] for c in range(SIZE)
    ]:
        cur = []
        for cell in line + [None]:
            if cell is not None and kind[cell] == LETTER:
                cur.append(cell)
                continue
            if len(cur) >= 2:
                out.append(cur)
            cur = []
    return out


def components(kind):
    letters = [i for i, k in enumerate(kind) if k == LETTER]
    seen, count = set(), 0
    for start in letters:
        if start in seen:
            continue
        count += 1
        stack = [start]
        seen.add(start)
        while stack:
            cell = stack.pop()
            r, c = divmod(cell, SIZE)
            for nr, nc in ((r - 1, c), (r + 1, c), (r, c - 1), (r, c + 1)):
                n = nr * SIZE + nc
                if 0 <= nr < SIZE and 0 <= nc < SIZE and kind[n] == LETTER and n not in seen:
                    seen.add(n)
                    stack.append(n)
    return count


def template_cost(kind):
    slots = runs(kind)
    hard = sum(not MIN_LEN <= len(s) <= MAX_LEN for s in slots)
    covered = {cell for s in slots for cell in s}
    letters = [i for i, k in enumerate(kind) if k == LETTER]
    hard += sum(i not in covered for i in letters)
    hard += sum(
        kind[i] == kind[i + 1] == kind[i + SIZE] == kind[i + SIZE + 1] == LETTER
        for i in range(len(kind) - SIZE)
        if i % SIZE != SIZE - 1
    )
    hard += max(0, components(kind) - 1)
    crossed = sum(sum(cell in s for s in slots) == 2 for cell in letters)
    long_slots = sum(len(s) >= 8 for s in slots)
    soft = -1.0 * len(letters) - 0.3 * crossed + 1.5 * long_slots
    return hard, soft


def make_template(rng, steps=5000):
    """Simulated annealing over block/letter cells, from a lattice."""
    kind = [LETTER if (i // SIZE) % 2 == 0 or (i % SIZE) % 2 == 0 else BLOCK for i in range(SIZE * SIZE)]
    for i in range(SIZE * SIZE):
        if rng.random() < 0.15:
            kind[i] = BLOCK if kind[i] == LETTER else LETTER
    hard, soft = template_cost(kind)
    cost = hard * 100 + soft
    best = (cost, hard, list(kind))
    for step in range(steps):
        temp = 4.0 * (1 - step / steps) + 0.05
        i = rng.randrange(SIZE * SIZE)
        kind[i] = BLOCK if kind[i] == LETTER else LETTER
        h, s = template_cost(kind)
        new = h * 100 + s
        if new <= cost or rng.random() < math.exp((cost - new) / temp):
            cost = new
            if cost < best[0]:
                best = (cost, h, list(kind))
        else:
            kind[i] = BLOCK if kind[i] == LETTER else LETTER
    return best[2] if best[1] == 0 else None


# --- fill -----------------------------------------------------------------


class WordIndex:
    """Per-length bitsets of candidate words, in preference order."""

    def __init__(self, words):
        self.by_len, self.bits, self.all = {}, {}, {}
        for w in words:
            self.by_len.setdefault(len(w), []).append(w)
        for length, ws in self.by_len.items():
            pos = [[0] * len(ALPHABET) for _ in range(length)]
            for i, w in enumerate(ws):
                for p, ch in enumerate(w):
                    pos[p][CODE[ch]] |= 1 << i
            self.bits[length] = pos
            self.all[length] = (1 << len(ws)) - 1


def fill(slots, index, max_nodes=30000, branch=8):
    """A distinct word per slot, or None."""
    at = {}
    for si, cells in enumerate(slots):
        for cell in cells:
            at.setdefault(cell, []).append(si)
    crossing = [sorted({o for cell in cells for o in at[cell] if o != si}) for si, cells in enumerate(slots)]
    letter, chosen = {}, [None] * len(slots)
    used = {length: 0 for length in index.all}
    nodes = 0

    def domain(si):
        length = len(slots[si])
        d = index.all.get(length, 0) & ~used.get(length, 0)
        bits = index.bits.get(length)
        for p, cell in enumerate(slots[si]):
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
        best, best_key, best_d = None, None, 0
        for si in range(len(slots)):
            if chosen[si] is not None:
                continue
            d = domain(si)
            n = d.bit_count()
            if n == 0:
                return False
            key = (n, -len(slots[si]))
            if best is None or key < best_key:
                best, best_key, best_d = si, key, d
        if best is None:
            return True
        length = len(slots[best])
        d, tried = best_d, 0
        while d and tried < branch:
            low = d & -d
            d ^= low
            tried += 1
            word = index.by_len[length][low.bit_length() - 1]
            placed = []
            for cell, ch in zip(slots[best], word):
                if cell not in letter:
                    letter[cell] = CODE[ch]
                    placed.append(cell)
            chosen[best] = word
            used[length] |= low
            if all(chosen[o] is not None or domain(o) for o in crossing[best]) and search():
                return True
            used[length] &= ~low
            chosen[best] = None
            for cell in placed:
                del letter[cell]
            if nodes > max_nodes:
                return False
        return False

    return chosen if search() else None


# --- uniqueness -----------------------------------------------------------


def shape(seq):
    first = {}
    return tuple(first.setdefault(x, len(first)) for x in seq)


class CheckIndex:
    def __init__(self, words):
        self.by_shape = {}
        for w in words:
            self.by_shape.setdefault(shape(w), []).append(w)


def count_solutions(patterns, check, given, limit=2, max_nodes=200000):
    """Assignments number -> letter (injective, `given` fixed) that make every
    pattern a check word, counted up to `limit`; None past max_nodes."""
    cands = [check.by_shape.get(shape(p), []) for p in patterns]
    nodes = 0
    found = 0

    def consistent(word, pat, assign, used):
        for n, ch in zip(pat, word):
            have = assign.get(n)
            if have is None:
                if ch in used:
                    return False
            elif have != ch:
                return False
        return True

    def search(assign, used, lists):
        nonlocal nodes, found
        nodes += 1
        if nodes > max_nodes:
            return
        best = None
        filtered = []
        for si, pat in enumerate(patterns):
            words = [w for w in lists[si] if consistent(w, pat, assign, used)]
            if not words:
                return
            filtered.append(words)
            if all(n in assign for n in pat):
                continue
            if best is None or len(words) < len(filtered[best]):
                best = si
        if best is None:
            found += 1
            return
        for word in filtered[best]:
            added = []
            for n, ch in zip(patterns[best], word):
                if n not in assign:
                    assign[n] = ch
                    used.add(ch)
                    added.append(n)
            search(assign, used, filtered)
            for n in added:
                used.discard(assign.pop(n))
            if found >= limit or nodes > max_nodes:
                return

    assign = dict(given)
    search(assign, set(assign.values()), cands)
    return None if nodes > max_nodes else found


# --- puzzle ---------------------------------------------------------------


def build(seed, index, check):
    rng = random.Random(seed)
    for _attempt in range(40):
        kind = make_template(rng)
        if kind is None:
            continue
        letters = sum(k == LETTER for k in kind)
        if letters < MIN_LETTERS:
            continue
        slots = runs(kind)
        rng.shuffle(slots)
        chosen = fill(slots, index)
        if chosen is None:
            continue
        grid = [BLOCK] * (SIZE * SIZE)
        for cells, word in zip(slots, chosen):
            for cell, ch in zip(cells, word):
                grid[cell] = ch
        distinct = sorted({ch for ch in grid if ch != BLOCK}, key=CODE.get)
        if len(distinct) < MIN_DISTINCT:
            continue
        order = list(distinct)
        rng.shuffle(order)
        number = {ch: i + 1 for i, ch in enumerate(order)}
        patterns = [tuple(number[grid[c]] for c in cells) for cells in slots]
        counts = {n: sum(grid[c] != BLOCK and number[grid[c]] == n for c in range(len(grid))) for n in number.values()}
        frequent = sorted(counts, key=lambda n: -counts[n])[:10]
        pairs = [(a, b) for i, a in enumerate(frequent) for b in frequent[i + 1 :]]
        rng.shuffle(pairs)
        for pair in pairs[:12]:
            given = {n: order[n - 1] for n in pair}
            if count_solutions(patterns, check, given) == 1:
                return grid, order, sorted(pair)
    return None


def puzzle_text(grid, order, given):
    lines = [FORMAT_HEADER, f"size {SIZE} {SIZE}", "grid"]
    for r in range(SIZE):
        lines.append("".join(grid[r * SIZE + c].upper() for c in range(SIZE)))
    lines.append("code " + "".join(order).upper())
    lines.append("given " + " ".join(str(n) for n in given))
    lines.append("end")
    return "\n".join(lines) + "\n"


_index = _check = None


def _init(lexicon, fill_seed):
    global _index, _check
    words, check = load_words(lexicon)
    rng = random.Random(fill_seed)
    words.sort(key=lambda e: -(e[1] + rng.gauss(0, 0.5)))
    _index = WordIndex([w for w, _ in words])
    _check = CheckIndex(check)


def _work(seed):
    return seed, build(seed, _index, _check)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("lexicon", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--count", type=int, default=120)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    made, seen = [], set()
    seeds = iter(range(args.seed * 100000, args.seed * 100000 + 100000))
    with Pool(args.jobs, _init, (args.lexicon, args.seed)) as pool:
        for seed, result in pool.imap(_work, seeds):
            if result is None:
                continue
            grid, order, given = result
            key = "".join(grid)
            if key in seen:
                continue
            seen.add(key)
            made.append(puzzle_text(grid, order, given))
            print(
                f"{len(made)}/{args.count} seed {seed}: {sum(c != BLOCK for c in grid)} letters, {len(order)} numbers",
                file=sys.stderr,
                flush=True,
            )
            if len(made) >= args.count:
                pool.terminate()
                break
    ids = [f"{i + 1:04d}" for i in range(len(made))]
    for pid, text in zip(ids, made):
        (args.out / f"{pid}.cwd").write_text(text, encoding="utf-8")
    (args.out / "index.txt").write_text(INDEX_HEADER + "\n" + "".join(f"{pid}\n" for pid in ids), encoding="utf-8")
    (args.out / "ATTRIBUTION.txt").write_text(ATTRIBUTION, encoding="utf-8")


if __name__ == "__main__":
    main()
