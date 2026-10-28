"""Build the scanword lexicon: common Russian nouns with short clues.

    python -I lexicon.py extract ruwiktionary-pages-articles.xml.bz2 pages.jsonl
    python -I lexicon.py build pages.jsonl lexicon.tsv [max_clue_chars]

`extract` streams a ru.wiktionary pages-articles dump (text CC BY-SA 4.0) and
keeps the Russian noun sections of 3-10 letter titles; `build` turns them into
rows of word, Zipf frequency (wordfreq), clue, clue kind. A word is kept when
its page title is used often enough as that noun (pymorphy3 parse share), and its
clue is the head of a definition, a synonym or a hypernym, at most MAX_CLUE
characters, never containing the answer's stem.
"""

import bz2
import json
import math
import re
import sys
import xml.etree.ElementTree as ET

import pymorphy3
import wordfreq

MIN_LEN, MAX_LEN = 3, 10
MAX_CLUE = 20
MIN_ZIPF = 1.8

TITLE_RE = re.compile(rf"^[а-яё]{{{MIN_LEN},{MAX_LEN}}}$")
LANG_HEADER_RE = re.compile(r"^=\s*\{\{-([a-z-]+)-\}\}\s*=\s*$", re.MULTILINE)
SECTION_RE = re.compile(r"^(={2,4})\s*([^=].*?)\s*\1\s*$", re.MULTILINE)
LINK_RE = re.compile(r"\[\[(?:[^\]|]*\|)?([^\]]*)\]\]")
KEPT_SECTIONS = ("Морфологические и синтаксические свойства", "Значение", "Синонимы", "Гиперонимы")

# Sense labels that make a sense unsuitable for a general-audience puzzle.
BAD_LABELS = (
    "устар",
    "истор",
    "диал",
    "обл.",
    "прост",
    "жарг",
    "угол",
    "вульг",
    "обсц",
    "бран",
    "презр",
    "пренебр",
    "неодобр",
    "уничиж",
    "груб",
    "сниж",
    "арх.",
    "рег.",
    "сленг",
    "эвф",
    "оскорб",
    "нецен",
    "редк",
    "церк.-слав",
    "крим",
    "нарк",
    "фам.",
    "пейор",
    "ирон",
    "шутл",
    "мол.",
)
# Templates that mean "derived from another word": poor clues.
DERIVED_TEMPLATES = {
    "действие",
    "свойство",
    "состояние",
    "рез.",
    "собир.",
    "женск.",
    "уменьш.",
    "увелич.",
    "ласк.",
    "умласк.",
    "уменьш.-ласк.",
    "унич.",
    "отн.",
    "соотн.",
    "свойств.",
    "процесс",
    "мн.",
    "лицо",
}
DERIVED_PREFIXES = (
    "действие ",
    "процесс ",
    "состояние ",
    "свойство ",
    "результат ",
    "то, что ",
    "тот, кто ",
    "та, которая ",
    "тот, который ",
    "уменьш",
    "увелич",
    "ласк",
    "женск",
    "мужск",
    "отвлеч",
    "собир",
    "название ",
    "одно из ",
    "один из ",
    "имя ",
    "буква ",
    "житель",
    "уроженец",
    "приверженец",
    "сторонник",
    "последователь",
    "специалист по",
    "отдельный ",
    "форма ",
    "мн. ",
    "множ",
    "тривиальное название",
    "собственное название",
    "общее название",
    "некое ",
    "некоторое ",
)
PROPER_TAGS = {"Name", "Surn", "Patr", "Geox", "Orgn", "Trad", "Abbr", "Slng", "Arch", "Obsc", "Dist", "Erro"}
STOP_POS = {"PREP", "CONJ", "PRCL", "INTJ", "VERB", "INFN", "GRND", "ADVB", "NPRO", "PRED", "COMP"}

morph = pymorphy3.MorphAnalyzer()


# --- stage 1: dump -> Russian noun sections ---------------------------------


def ru_section(text):
    start = None
    for h in LANG_HEADER_RE.finditer(text):
        if start is not None:
            return text[start : h.start()]
        if h.group(1) == "ru":
            start = h.end()
    return text[start:] if start is not None else ""


def trim(body):
    """Homonym headers plus the sections the build stage reads."""
    out, keep = [], False
    for line in body.splitlines():
        m = SECTION_RE.match(line)
        if m:
            keep = m.group(2) in KEPT_SECTIONS
            if len(m.group(1)) == 2 or keep:
                out.append(line)
            continue
        if keep:
            out.append(line)
    return "\n".join(out)


def extract(dump, out_path):
    kept = 0
    with bz2.open(dump, "rb") as src, open(out_path, "w", encoding="utf-8") as out:
        title = text = ""
        root = None
        for event, el in ET.iterparse(src, events=("start", "end")):
            if root is None:
                root = el
            if event == "start":
                continue
            tag = el.tag.rsplit("}", 1)[-1]
            if tag == "title":
                title = el.text or ""
            elif tag == "text":
                text = el.text or ""
            elif tag == "page":
                root.clear()  # drop finished pages so memory stays flat
                if not TITLE_RE.match(title) or "{{-ru-}}" not in text:
                    continue
                if wordfreq.zipf_frequency(title.replace("ё", "е"), "ru") < MIN_ZIPF - 0.5:
                    continue
                body = trim(ru_section(text))
                if re.search(r"\{\{сущ[ -]ru", body):
                    out.write(json.dumps({"title": title, "text": body}, ensure_ascii=False) + "\n")
                    kept += 1
    print(f"extracted {kept} pages", file=sys.stderr)


# --- stage 2: sections -> lexicon --------------------------------------------


def iter_templates(text):
    """Yields (start, end) of top-level {{...}} spans, handling nesting."""
    depth, start, i = 0, 0, 0
    while i < len(text) - 1:
        pair = text[i : i + 2]
        if pair == "{{":
            if depth == 0:
                start = i
            depth += 1
            i += 2
        elif pair == "}}" and depth:
            depth -= 1
            i += 2
            if depth == 0:
                yield start, i
        else:
            i += 1


def split_args(body):
    """Splits a template body on top-level '|'."""
    args, depth, cur, i = [], 0, [], 0
    while i < len(body):
        two = body[i : i + 2]
        if two in ("{{", "[["):
            depth += 1
            cur.append(two)
            i += 2
        elif two in ("}}", "]]") and depth:
            depth -= 1
            cur.append(two)
            i += 2
        else:
            if body[i] == "|" and depth == 0:
                args.append("".join(cur))
                cur = []
            else:
                cur.append(body[i])
            i += 1
    args.append("".join(cur))
    return [a.strip() for a in args]


class Sense:
    def __init__(self):
        self.labels = []
        self.synonyms = []
        self.hypernyms = []
        self.derived = False
        self.text = ""


def render(text, sense):
    """Wikitext of one definition to plain text; fills labels/synonyms/derived."""
    out, pos = [], 0
    for s, e in iter_templates(text):
        out.append(text[pos:s])
        pos = e
        args = split_args(text[s + 2 : e - 2])
        name = args[0]
        low = name.lower()
        if low in ("пример", "семантика", "lang", "l", "l.", "илл", "ref"):
            continue
        if low == "помета" and len(args) > 1:
            sense.labels.append(args[1])
        elif low in ("t:=", "=", "то же", "t:то же"):
            if len(args) > 1:
                sense.synonyms.append(render(args[1], Sense()))
            if not text[e:].strip(" ;,.") and not text[:s].strip(" ;,."):
                sense.derived = True
        elif low in DERIVED_TEMPLATES:
            sense.derived = True
        elif low in ("выдел", "nobr", "lang-ru"):
            out.append(render(args[-1], Sense()))
        elif low in ("-", "--", "—"):
            out.append(" — ")
        elif name.endswith(".") or (len(args) > 1 and args[1] == "ru"):
            sense.labels.append(name)
    out.append(text[pos:])
    plain = "".join(out)
    plain = re.sub(r"<ref[^>]*/>|<ref.*?</ref>|<[^>]+>", "", plain, flags=re.DOTALL)
    plain = LINK_RE.sub(r"\1", plain)
    plain = plain.replace("'''", "").replace("''", "").replace("&nbsp;", " ")
    plain = re.sub("[\u0300\u0301\u00ad]", "", plain)  # stress marks, soft hyphens
    plain = re.sub(r"\([^()]*\)", "", plain)
    return re.sub(r"\s+", " ", plain).strip(" ;,.:")


def fold(s):
    return s.lower().replace("ё", "е")


def leaks(answer, clue):
    """True when a clue word shares the answer's stem."""
    a = fold(answer)
    need = 3 if len(a) <= 4 else 4
    for tok in re.findall(r"[а-яё]+", fold(clue)):
        n = 0
        while n < min(len(a), len(tok)) and a[n] == tok[n]:
            n += 1
        if n >= need or tok == a:
            return True
    return False


def bad_label(labels):
    joined = " ".join(labels).lower()
    return any(b in joined for b in BAD_LABELS)


def zipf(word):
    """Zipf frequency of `word` used as this common noun.

    Each spelling (texts often write е for ё) counts by the share of its
    parses that are this noun, so homographs such as "том" (тот) or "перёд"
    (перед) do not borrow the other word's frequency."""
    f = 0.0
    for spelling in {word, word.replace("ё", "е")}:
        share = sum(
            p.score
            for p in morph.parse(spelling)
            if p.tag.POS == "NOUN" and p.normal_form == word and not (PROPER_TAGS & p.tag.grammemes)
        )
        f += wordfreq.word_frequency(spelling, "ru") * share
    return math.log10(f * 1e9) if f > 0 else 0.0


def is_head_noun(token):
    return any(p.tag.POS == "NOUN" and p.tag.case in ("nomn", "accs") and p.score >= 0.3 for p in morph.parse(token))


def capitalize(clue):
    return clue[:1].upper() + clue[1:]


def usable(answer, clue):
    return (
        3 < len(clue) <= MAX_CLUE
        and re.fullmatch(r"[А-ЯЁа-яё][А-ЯЁа-яё ,\-]*[А-ЯЁа-яё]", clue) is not None
        and not leaks(answer, clue)
    )


def head_phrase(clause):
    """Adjectives + first nominative noun + a genitive tail, cut to MAX_CLUE."""
    words = re.findall(r"[А-ЯЁа-яё\-]+", clause)
    if not words or morph.parse(words[0])[0].tag.POS in STOP_POS:
        return ""
    head = next((i for i, w in enumerate(words[:5]) if is_head_noun(w)), None)
    if head is None:
        return ""
    for w in words[:head]:
        if not any(p.tag.POS in ("ADJF", "PRTF", "NUMR") and p.score > 0.1 for p in morph.parse(w)):
            return ""
    phrase = words[: head + 1]
    for w in words[head + 1 :]:
        genitive = any(
            p.tag.case == "gent" and p.tag.POS in ("NOUN", "ADJF", "PRTF") and p.score > 0.05 for p in morph.parse(w)
        )
        if not genitive:
            break
        if len(" ".join(phrase + [w])) > MAX_CLUE:
            phrase = words[: head + 1]  # never cut a genitive chain in half
            break
        phrase.append(w)
    while phrase and phrase[-1] and morph.parse(phrase[-1])[0].tag.POS in ("ADJF", "PRTF"):
        phrase.pop()
    while (
        len(" ".join(phrase)) > MAX_CLUE and len(phrase) > 1 and morph.parse(phrase[0])[0].tag.POS in ("ADJF", "PRTF")
    ):
        phrase.pop(0)
    return capitalize(" ".join(phrase))


def definition_clue(answer, sense):
    text = sense.text
    if not text or sense.derived or any(text.lower().startswith(p) for p in DERIVED_PREFIXES):
        return ""
    clause = re.split(r";| — |: ", text)[0]
    if re.match(r"[А-ЯЁ]{2}", clause):
        return ""  # starts with an abbreviation
    first = capitalize(clause.split(",")[0].strip())
    for clue in (first, head_phrase(clause)):
        # Only whole noun phrases: "Самец домашней кошки", not "Небольшой как правило".
        if not usable(answer, clue) or len(clue.split()) > 4 or head_phrase(clue) != clue:
            continue
        if " " not in clue and zipf(clue.lower()) > 4.3:
            continue  # "Человек", "Время": too vague alone
        return clue
    return ""


def section_text(block, name):
    m = re.search(rf"^={{2,4}}\s*{re.escape(name)}\s*={{2,4}}\s*$", block, re.MULTILINE)
    if not m:
        return ""
    nxt = SECTION_RE.search(block, m.end())
    return block[m.end() : nxt.start() if nxt else len(block)]


def page_senses(body):
    """Senses of each noun homonym: list of [Sense], in page order."""
    result = []
    for block in re.split(r"^==\s*[^=].*?==\s*$", body, flags=re.MULTILINE):
        morph_text = section_text(block, KEPT_SECTIONS[0])
        if not re.match(r"\s*\{\{сущ[ -]ru", morph_text):
            continue
        senses = []
        for line in section_text(block, "Значение").splitlines():
            if line.startswith("#") and not line.startswith(("#*", "#:")):
                sense = Sense()
                sense.text = render(line[1:].strip(), sense)
                senses.append(sense)
        syn_lines = [line for line in section_text(block, "Синонимы").splitlines() if line.startswith("#")]
        for sense, line in zip(senses, syn_lines):
            if not bad_label(re.findall(r"\{\{помета\|([^}]*)\}\}", line)):
                sense.synonyms += [s for s in LINK_RE.findall(line) if s]
        hyp_lines = [line for line in section_text(block, "Гиперонимы").splitlines() if line.startswith("#")]
        for sense, line in zip(senses, hyp_lines):
            sense.hypernyms = [
                h.strip() for h in re.split(r"[,;]", LINK_RE.sub(r"\1", re.sub(r"\{\{[^}]*\}\}", "", line[1:])))
            ]
        result.append(senses)
    return result


def word_clue(word, single, max_zipf=9.0):
    """Synonym/hypernym clue: a single noun, frequent but not vague."""
    c = capitalize(single.strip())
    if not usable(word, c) or " " in c or not is_head_noun(c) or not 2.3 <= zipf(c.lower()) <= max_zipf:
        return ""
    return c


def choose_clue(word, homonyms):
    """Cheapest (clue, kind) over the first good senses of the noun homonyms."""
    candidates = []
    for h, senses in enumerate(homonyms):
        good = [s for s in senses if not bad_label(s.labels)][:3]
        for rank, s in enumerate(good):
            base = h * 3 + rank * 2
            clue = definition_clue(word, s)
            if clue:
                # A one-word definition is as vague as a hypernym; a synonym beats it.
                # Past two words the clue gets hard to fit into a cell.
                words = len(clue.split())
                cost = base + len(clue) / 40 + (1.0 if words == 1 else 0.4 * max(0, words - 2))
                candidates.append((cost, clue, "def"))
            for syn in s.synonyms[:4]:
                if c := word_clue(word, syn):
                    candidates.append((base + 0.9, c, "syn"))
            for hyp in s.hypernyms[:2]:
                # "Человек" or "Женщина" fits too many answers to be a clue.
                if c := word_clue(word, hyp, max_zipf=4.6):
                    candidates.append((base + 2.5, c, "hyp"))
    return min(candidates)[1:] if candidates else None


def build(pages_path, out_path):
    kept = 0
    with open(pages_path, encoding="utf-8") as src, open(out_path, "w", encoding="utf-8") as out:
        for line in src:
            page = json.loads(line)
            word = page["title"]
            z = zipf(word)
            if z < MIN_ZIPF:
                continue
            homonyms = page_senses(page["text"])
            best = choose_clue(word, homonyms) if homonyms else None
            if best is None:
                continue
            out.write(f"{word}\t{z:.2f}\t{best[0]}\t{best[1]}\n")
            kept += 1
    print(f"kept {kept} nouns", file=sys.stderr)


if __name__ == "__main__":
    if len(sys.argv) > 4:
        MAX_CLUE = int(sys.argv[4])
    {"extract": extract, "build": build}[sys.argv[1]](sys.argv[2], sys.argv[3])
