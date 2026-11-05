"""Goldens of tugaphone.text_normalization.

A "text" record runs every text function of the module on one text, each
function on its own and the full chain, for one language code. A
"stays_portuguese" record holds the words, the index, the words that
is_foreign calls foreign (null for the default argument) and the result.
"""
import json
import pathlib

from tugaphone import codeswitch
from tugaphone import text_normalization as tn
from tugaphone.number_utils import NumberParser

from . import abbreviation_corpus, common

LANGS = ["pt-PT", "pt-BR", "PT-br", "pt-br-x-rio", "", "en", "pt-AO"]


def record(text, lang):
    return ["text", text, lang, [
        common.call(tn.split_number_ranges, text),
        common.call(tn.normalize_number_separators, text),
        common.call(tn.separate_unit_symbols, text),
        common.call(tn.expand_clock_times, text),
        common.call(tn.expand_abbreviations, text),
        common.call(tn.expand_regnal_numerals, text, lang),
        common.call(tn.expand_acronyms, text),
        common.call(tn.expand_letter_digit_codes, text, lang),
        common.call(tn.normalize_orthography, text, lang),
    ]]


def stays_portuguese(words, i, foreign):
    if foreign is None:
        return ["stays_portuguese", words, i, None, common.call(tn.stays_portuguese, words, i)]
    chosen = set(foreign)
    return ["stays_portuguese", words, i, foreign,
            common.call(tn.stays_portuguese, words, i, lambda word: word in chosen)]


def stays_records(texts):
    """Every index of every text, with no foreign word, each word foreign on
    its own, every word foreign, and the words the router sends out."""
    out = []
    for text in texts:
        words = text.split()
        routed = [[w for w in words if codeswitch._classify(w, "en") is not None],
                  [w for w in words if codeswitch._is_contact(w)]]
        sets = common.unique(tuple(f) for f in [[], words] + [[w] for w in words] + routed)
        for i in range(len(words)):
            out.append(stays_portuguese(words, i, None))
            out += [stays_portuguese(words, i, list(f)) for f in sets]
        out.append(stays_portuguese(words, len(words), None))
    return out


def to_roman(n):
    out = ""
    for value, sym in ((1000, "M"), (900, "CM"), (500, "D"), (400, "CD"), (100, "C"), (90, "XC"), (50, "L"),
                       (40, "XL"), (10, "X"), (9, "IX"), (5, "V"), (4, "IV"), (1, "I")):
        while n >= value:
            out += sym
            n -= value
    return out


def rule_texts():
    out = []
    numbers = ["7", "0", "1", "1.5", "1,5", "12.345", "1.234,56", "٣", "１２", "7.", "7,", ".7", "x7", "7.7.7"]
    for unit in NumberParser.UNIT_NAMES:
        for n in numbers:
            out += [n + unit, f"{n} {unit}", f"a{n}{unit}", f"{n}{unit}/h", f"{n}{unit}x", f"{n}{unit}²",
                    f"({n}{unit})", f"{n}{unit}.", f"pesa {n}{unit} hoje"]
    for n in numbers + ["2,50", "0,50", "0,05", "2,5", "0,00", "1.000,01", "1.0000,5", "2,505"]:
        out += [f"€{n}", f"€ {n}", f"€{n}x", f"{n} €", f"custa {n} € e", f"a{n} €", f"{n}€"]
    for a, b in (("15", "8"), ("2,5", "3"), ("1", "2"), ("3", "4,5,6"), ("0", "x1F")):
        for sep in ("x", "×", " x ", " × ", "x ", " x"):
            out += [a + sep + b, a + sep + b + sep + "2", f"({a}{sep}{b})", f"a{a}{sep}{b}", f"{a}{sep}{b}a"]
    for h in list(range(0, 26)) + ["00", "01", "09", "001"]:
        for m in ("00", "05", "30", "59", "60", "5"):
            for p in ("", ".", ",", "!", "…", "”", "h"):
                out.append(f"às {h}:{m}{p}")
    out += ["1139-1185", "1–2", "1—2", "1−2", "a-1", "1-a", "1-2-3", "chamo-me", "92.073", "10,4", "3.º", "1.2.3"]
    for abbr in list(tn._UNCONDITIONAL_ABBREVIATIONS) + list(tn._HONORIFIC_ABBREVIATIONS) + list(tn._POSSESSIVES):
        out += [f"o {abbr} Silva", abbr, f"{abbr},", f"{abbr} Maria", f"{abbr} maria", f"vitamina {abbr} Maria",
                f"{abbr} Ex.ª", f"{abbr} Ex.as", f"{abbr} Exas.", f"{abbr} Exa.", f"“{abbr}” Isabel", f"{abbr}) x"]
    for name in ("Maria", "João", "Isabel", "Afonso", "Pio", "Luís", "Ana", "xpto", "“Maria”", "(Maria"):
        out += [f"D. {name}", f"D. {name} II", f"{name} I", f"{name} IV", f"{name} XIV", f"{name} V."]
    for n in list(range(1, 60)) + [99, 400, 444, 1000, 1999, 2024, 3888, 3999]:
        r = to_roman(n)
        out += [f"século {r}", f"o século {r}", f"O SÉCULO {r}", f"Afonso {r}", f"o {r} Congresso", f"a {r} guerra",
                f"a {r}", r, f"{r}.", f"eu vi {r}", f"a parte {r}", f"tamanho {r}"]
    letters = pathlib.Path(tn.__file__).parent / "data" / "letter_expressions.json"
    for phrase in json.loads(letters.read_text(encoding="utf-8"))["expressions"]:
        out += [phrase, phrase.upper(), phrase.capitalize(), f"o {phrase} é"]
    out += ["EU VI O FILME", "ELE IA SAIR", "ELA E EU", "A IA aprende", "IIII", "MMMM", "IIX", "VX", "XM", "ii", "Ii"]
    for acronym in tn._ACRONYMS:
        out += [f"A {acronym} aprende", f"ELA E {acronym}", f"{acronym}.", f"({acronym})", acronym.lower()]
    for code in ("T2", "A3", "RTP2", "mp3", "COVID19", "10 m2", "abc1", "Ab1", "T٣", "t2", "ABCD1", "a12345678901234567890",
                 "(T2)", "T2.", "x 5 T2", "5 T2", "T0", "T01", "A1B2"):
        out += [code, f"o {code} na cidade"]
    return out


def corpora(repo):
    tests = common.unique(common.test_strings(repo))
    rules = common.unique(rule_texts())
    probes = common.unique(common.UNICODE_PROBES + [f"7{p}kg {p}12:00 D. {p}Maria {p}XIV" for p in common.UNICODE_PROBES]
                           + [f"{p}1-2{p}" for p in common.UNICODE_PROBES])
    alphabet = list("0123456789.,-–:x×€% ºª") + ["kg", "km", "D.", "Sr.", "Maria", "IV", "X", "T", "IA", "EU", " ",
                                                 "٣", "é", "(", ")", "“", "”"]
    fuzz = common.unique(common.fuzz_strings(23, 3000, alphabet, 10))
    edges = common.unique(f"{lead}{core}{trail} e {lead}{core}{trail}"
                          for core in ("IA", "EU", "GPS", "PDF", "T2", "A1B2", "XIV", "7kg", "16:00")
                          for lead in ("", "(", "“", "\"", "'", "¿", "¡")
                          for trail in ("", ".", ",", ";", ":", "!", "?", ")", "\"", "”", "…", "...", ".…"))
    edges += ["T" + "1" * 4300, "T" + "1" * 4301, "T" + "0" * 4301]
    out = [("tests", [record(t, LANGS[i % 2]) for i, t in enumerate(tests)]),
           ("rules", [record(t, "pt-PT") for t in rules]),
           ("langs", [record(t, lang) for t in rules[::17] for lang in LANGS]),
           ("unicode", [record(t, "pt-PT") for t in probes]),
           ("edges", [record(f"A {t}", LANGS[i % 2]) for i, t in enumerate(edges)]),
           ("fuzz", [record(t, LANGS[i % len(LANGS)]) for i, t in enumerate(fuzz)])]
    abbreviations = abbreviation_corpus.sentences(repo)
    grid = abbreviation_corpus.marker_grid()
    out += [("abbreviations", [record(t, lang) for t in abbreviations for lang in ("pt-PT", "pt-BR")]),
            ("roman_heads", [record(t, lang) for t in abbreviation_corpus.roman_heads()
                             for lang in ("pt-PT", "pt-BR")]),
            ("marker_grid", [record(t, "pt-PT") for t in grid]),
            ("stays_portuguese", stays_records(abbreviations + grid[::7]))]
    return out
