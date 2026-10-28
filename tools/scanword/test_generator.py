"""Generator invariants. Run: .venv/bin/python -I test_generator.py

Set SCANWORD_LEXICON=path/to/lexicon.tsv to also fill real puzzles.
"""

import itertools
import os
import random
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import generator as G
import lexicon as L

SAMPLES = HERE / "samples"


def toy_lexicon(seed=1):
    """Dense random words over a small alphabet: any template fills."""
    rng = random.Random(seed)
    letters = "абвгдеё"
    entries, seen = [], set()
    for length in range(G.MIN_LEN, G.MAX_LEN + 1):
        pool = ["".join(p) for p in itertools.product(letters, repeat=length)] if length <= 4 else None
        for i in range(3000):
            word = rng.choice(pool) if pool else "".join(rng.choice(letters) for _ in range(length))
            if G.fold(word) in seen:
                continue
            seen.add(G.fold(word))
            entries.append(G.Entry(word, rng.uniform(2, 5), f"Подсказка {len(entries)}"))
    return entries


class GeneratorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.entries = toy_lexicon()
        cls.lex = {G.fold(e.word): e for e in cls.entries}
        rng = random.Random(5)
        cls.puzzles = []
        for level in (1, 3):
            G.LEVELS[level] = (0, 0.5)
            puzzle, words = G.generate(cls.entries, level, rng, {})
            cls.puzzles.append((puzzle, words))

    def test_generated_puzzles_are_valid(self):
        for puzzle, words in self.puzzles:
            self.assertIsNotNone(puzzle)
            self.assertEqual(G.validate(puzzle, self.lex), [])
            self.assertEqual(len({G.fold(e.word) for e in words}), len(words))
            self.assertTrue(all(G.MIN_LEN <= len(e.word) <= G.MAX_LEN for e in words))
            text = puzzle.text()
            self.assertEqual(G.parse(text).text(), text)

    def test_every_cell_is_clue_or_letter_mostly(self):
        for puzzle, _ in self.puzzles:
            self.assertLessEqual(puzzle.grid.count(G.BLANK), 1)
            per_cell = {}
            for r, c, _, _ in puzzle.clues:
                per_cell[(r, c)] = per_cell.get((r, c), 0) + 1
            self.assertTrue(all(1 <= n <= 2 for n in per_cell.values()))
            self.assertEqual(len(per_cell), puzzle.grid.count(G.CLUE))

    def test_yo_shown_only_where_every_answer_has_it(self):
        for puzzle, words in self.puzzles:
            width = puzzle.width
            through = {}
            for _, cells, word in G.answers(puzzle):
                entry = self.lex[G.fold(word)]
                for cell, ch in zip(cells, entry.word):
                    through.setdefault(cell, []).append(ch)
            for cell, chars in through.items():
                expected = "Ё" if all(ch == "ё" for ch in chars) else G.fold(chars[0]).upper()
                self.assertEqual(puzzle.grid[cell], expected, divmod(cell, width))

    def test_yo_crossing_with_e_shows_e(self):
        kind = list("######LLLL#L####L###")
        template = G.Template(5, 4, kind, G.runs(kind, 5, 4))
        template.owner, _ = G.assign_clues(template.slots, kind, 5)
        across = [s.dir == G.ACROSS for s in template.slots]

        def cell_11(down_word):
            words = [G.Entry("ёжик" if a else down_word, 3, str(a)) for a in across]
            return G.build_puzzle(template, words, 1).grid[1 * 5 + 1]

        self.assertEqual(cell_11("еда"), "Е")  # ё across, е down
        self.assertEqual(cell_11("ёла"), "Ё")

    def test_validator_catches_broken_puzzles(self):
        puzzle, _ = self.puzzles[0]
        base = puzzle.text()

        def broken(edit):
            p = G.parse(base)
            edit(p)
            return G.validate(p)

        self.assertTrue(broken(lambda p: p.clues.pop()))
        self.assertTrue(broken(lambda p: p.clues.append(p.clues[0])))
        self.assertTrue(broken(lambda p: p.clues.__setitem__(0, p.clues[0][:3] + ("x" * 40,))))
        self.assertTrue(broken(lambda p: p.clues.__setitem__(1, p.clues[1][:3] + (p.clues[0][3],))))

        def uncover(p):
            # A letter next to nothing but clue cells and the border is in no answer.
            p.grid[0] = "А"

        self.assertTrue(broken(uncover))

        def duplicate(p):
            ans = G.answers(p)
            same = [(c, w) for _, c, w in ans if len(w) == len(ans[0][2])]
            if len(same) < 2:
                return p.clues.pop()
            for cell, ch in zip(same[1][0], same[0][1]):
                p.grid[cell] = ch

        self.assertTrue(broken(duplicate))

    def test_template_rules(self):
        rng = random.Random(9)
        for _ in range(3):
            t = G.make_template(10, 14, rng)
            if t is None:
                continue
            hard = G.template_cost([G.CLUE if k == G.BLANK else k for k in t.kind], 10, 14)[0]
            self.assertEqual(hard, 0)
            self.assertTrue(all(G.MIN_LEN <= s.length <= G.MAX_LEN for s in t.slots))
            self.assertTrue(all(o is not None for o in t.owner))

    def test_samples_validate(self):
        index = (SAMPLES / "index.txt").read_text(encoding="utf-8").splitlines()
        self.assertEqual(index[0], G.INDEX_HEADER)
        self.assertGreaterEqual(len(index) - 1, 3)
        for line in index[1:]:
            name, level = line.split()
            p = G.parse((SAMPLES / f"{name}.scw").read_text(encoding="utf-8"))
            self.assertEqual(p.level, int(level))
            self.assertEqual(G.validate(p), [], name)
            self.assertTrue(all(len(t) <= G.MAX_CLUE for _, _, _, t in p.clues))
        self.assertTrue((SAMPLES / "ATTRIBUTION.txt").exists())

    @unittest.skipUnless(os.environ.get("SCANWORD_LEXICON"), "SCANWORD_LEXICON not set")
    def test_real_lexicon_fills(self):
        entries = G.load_lexicon(os.environ["SCANWORD_LEXICON"])
        lex = {G.fold(e.word): e for e in entries}
        rng = random.Random(1)
        for level in (1, 2, 3):
            G.LEVELS.update({1: (3.2, 0.35), 2: (2.7, 0.6), 3: (2.2, 0.9)})
            puzzle, _ = G.generate(entries, level, rng, {})
            self.assertIsNotNone(puzzle)
            self.assertEqual(G.validate(puzzle, lex), [])


class LexiconTest(unittest.TestCase):
    def test_leaks(self):
        self.assertTrue(L.leaks("учитель", "Тот, кто учит"))
        self.assertTrue(L.leaks("кот", "Большой кот"))
        self.assertFalse(L.leaks("кот", "Самец домашней кошки"))
        self.assertTrue(L.leaks("мёд", "Медовый напиток"))  # ё folds to е

    def test_head_phrase(self):
        self.assertEqual(L.head_phrase("часть суток между вечером и утром"), "Часть суток")
        self.assertEqual(L.head_phrase("нормативный акт высшего органа государственной власти"), "Нормативный акт")
        self.assertEqual(L.head_phrase("в семейном общении"), "")
        self.assertEqual(L.head_phrase("небольшой как правило"), "")

    def test_frequency_ignores_homographs(self):
        self.assertLess(L.zipf("перёд"), 3.5)  # not "перед" (5.65)
        self.assertGreater(L.zipf("ёлка"), 3.0)  # counts "елка" too
        self.assertLess(L.zipf("том"), 4.5)  # raw 6.0, mostly "о том"

    def test_render_definition(self):
        sense = L.Sense()
        text = L.render("{{зоол.|ru}} [[самец]] домашней [[кошка|кошки]] {{пример|Кот спал.}}", sense)
        self.assertEqual(text, "самец домашней кошки")
        self.assertEqual(sense.labels, ["зоол."])
        sense = L.Sense()
        L.render("{{t:=|гиппопотам}}", sense)
        self.assertEqual(sense.synonyms, ["гиппопотам"])
        self.assertTrue(sense.derived)


if __name__ == "__main__":
    unittest.main()
