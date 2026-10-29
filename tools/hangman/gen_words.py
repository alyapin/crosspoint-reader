"""Hangman word lists: nouns with a whole short definition, packed into a C++ header.

    tools/scanword/.venv/bin/python -I tools/hangman/gen_words.py \\
        LEXICON.tsv PAGES.jsonl lib/Games/HangmanWords.h [--max-bytes 40000]

LEXICON.tsv and PAGES.jsonl come from tools/scanword/lexicon.py (build and
extract). The script imports lexicon.py, so it runs in that tool's environment
(tools/scanword/requirements.txt).

A word of 5-10 letters is kept when the definition of its main sense (first
homonym, first sense without a crude or dated label) fits whole, up to the
first ";", in max_clue_chars = 48 characters and has at least two words: a
definition is never cut. Words are taken most frequent first
until the data reaches --max-bytes. EXCLUDED words (tools/wordlists/blocked.py)
never ship; RUDE words form a second list for the «С крепкими словами» mode,
where RUDE_CLUES replaces a misleading definition. One byte per letter:
lib/Games/Hangman.h ALPHABET.
"""

import argparse
import json
import re
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
sys.path[:0] = [str(TOOLS / "wordlists"), str(TOOLS / "scanword")]

import blocked  # noqa: E402
import lexicon  # noqa: E402

ALPHABET_RU = "абвгдеёжзийклмнопрстуфхцчшщъыьэюя"
ALPHABET = "abvgdeEjziyklmnoprstufhcCwWXYxqUQ"  # must match lib/Games/Hangman.h
CODE = dict(zip(ALPHABET_RU, ALPHABET))
MIN_LEN, MAX_LEN = 5, 10
MAX_CLUE_CHARS = 48
# Rude words whose Wiktionary definition misleads (ублюдок is first a mongrel plant)
# or that the lexicon lacks.
RUDE_CLUES = {
    "ублюдок": "Внебрачный ребёнок",
    "шлюха": "Распущенная женщина",
    "сучка": "Грубо о женщине",
    "девка": "Грубо о девушке",
    "тёлка": "Жаргонно о молодой женщине",
    "тварь": "Мерзкий, подлый человек",
    "скотина": "Грубый, подлый человек",
    "сволочь": "Подлый человек, негодяй",
    "подонок": "Подлый, мерзкий человек",
    "мерзавец": "Подлый, бесчестный человек",
    "стерва": "Злая, вредная женщина",
    "дурак": "Глупый человек",
    "идиот": "Крайне глупый человек",
    "кретин": "Тупой, глупый человек",
    "придурок": "Глуповатый, странный человек",
    "засранец": "Грубо о негодяе",
    "сопляк": "Неопытный юнец",
    "морда": "Грубо о лице",
    "башка": "Грубо о голове",
    "задница": "Мягкое место",
    "дерьмо": "Грубо об испражнениях",
    "киска": "Ласково о кошке",
    "пенис": "Мужской половой орган",
    "матка": "Женский детородный орган",
    "рвота": "Выброс пищи из желудка",
    "аборт": "Прерывание беременности",
    "бордель": "Дом терпимости",
    "резня": "Погром с массовыми убийствами",
    "порно": "Откровенное эротическое кино",
    "пацан": "Мальчишка, подросток",
}
CLUE_RE = re.compile(r"[а-яё][а-яё ,\-]*[а-яё]")
# A clause that ends on one of these was cut short by the source's own markup.
OPEN_END = {"ADJF", "PRTF", "ADVB", "PREP", "CONJ", "PRCL"}
# "Ягода такого растения" leans on the sense before it.
REFERS_RE = re.compile(
    r"\b(так(ой|ого|ому|им|ая|ую|ие|их)|эт(от|ого|ой|им|ом|а|у|и|их)|данн\w+|указанн\w+)\b"
)


def encode(text):
    return "".join(CODE.get(ch, ch) for ch in text)


def whole_clue(word, text):
    """The definition's first clause when it fits whole; else None."""
    clause = re.sub(r" ,", ",", re.split(r";| — |: ", text)[0]).strip()
    clue = lexicon.capitalize(clause)
    if not clause or len(clue) > MAX_CLUE_CHARS or len(clue.split()) < 2:
        return None
    if not CLUE_RE.fullmatch(clue[0].lower() + clue[1:]) or REFERS_RE.search(
        clause.lower()
    ):
        return None
    if lexicon.leaks(word, clue) or clause.lower().startswith(lexicon.DERIVED_PREFIXES):
        return None
    words = clause.split()
    if lexicon.morph.parse(words[0])[0].tag.POS in lexicon.STOP_POS:
        return None
    if lexicon.morph.parse(words[-1])[0].tag.POS in OPEN_END:
        return None
    return clue


def clue_of(word, text, rude):
    """The main sense's definition when it fits whole; a rude word may keep its labelled senses."""
    homonyms = lexicon.page_senses(text)
    senses = (
        [s for s in homonyms[0] if rude or not lexicon.bad_label(s.labels)]
        if homonyms
        else []
    )
    if not senses or senses[0].derived:
        return None
    return whole_clue(word, senses[0].text)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("lexicon", type=Path)
    parser.add_argument("pages", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--max-bytes", type=int, default=40000)
    args = parser.parse_args()

    excluded = {blocked.fold(w) for w in blocked.EXCLUDED}
    rude = {blocked.fold(w) for w in blocked.RUDE}
    zipf = {}
    for line in args.lexicon.read_text(encoding="utf-8").splitlines():
        word, freq = line.split("\t")[:2]
        if MIN_LEN <= len(word) <= MAX_LEN and blocked.fold(word) not in excluded:
            zipf[word] = float(freq)
    pages = {}
    with args.pages.open(encoding="utf-8") as src:
        for line in src:
            page = json.loads(line)
            if page["title"] in zipf:
                pages[page["title"]] = page["text"]

    normal, crude = [], []
    for word in sorted(zipf, key=lambda w: (-zipf[w], w)):
        is_rude = blocked.fold(word) in rude
        clue = RUDE_CLUES.get(word) if is_rude else None
        if clue is None:
            clue = clue_of(word, pages.get(word, ""), is_rude)
        if clue is not None:
            (crude if is_rude else normal).append(
                f"{encode(word)}:{encode(clue.lower())}"
            )
    crude += [
        f"{encode(w)}:{encode(c.lower())}"
        for w, c in RUDE_CLUES.items()
        if w not in zipf
    ]

    size = sum(len(line) + 1 for line in crude)
    kept = []
    for line in normal:
        if size + len(line) + 1 > args.max_bytes:
            break
        kept.append(line)
        size += len(line) + 1

    body = "\n".join(f'    "{line}\\n"' for line in kept + crude)
    args.out.write_text(
        f"""#pragma once

// Generated by tools/hangman/gen_words.py; do not edit. Regenerate from the repository root with
//   tools/scanword/.venv/bin/python -I tools/hangman/gen_words.py LEXICON.tsv PAGES.jsonl {args.out.as_posix()}
// Words and definitions derived from the Russian Wiktionary (https://ru.wiktionary.org), CC BY-SA 4.0;
// see tools/hangman/ATTRIBUTION.txt.

#include <cstdint>

namespace hangman {{

constexpr uint16_t WORD_COUNT = {len(kept)};
// Played only in the «С крепкими словами» mode, after the WORD_COUNT others.
constexpr uint16_t RUDE_WORD_COUNT = {len(crude)};

// "word:definition\\n" per word, most frequent first, in ALPHABET; the definition starts with a capital.
constexpr char WORDS[] =
{body};

}}  // namespace hangman
""",
        encoding="utf-8",
    )
    print(f"{len(kept)} words + {len(crude)} rude, {size} bytes", file=sys.stderr)


if __name__ == "__main__":
    main()
