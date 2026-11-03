"""Goldens of tugaphone.number_utils and of the Portuguese pronounce_number
of ovos-number-parser that it calls.

An int is written as {"int": "<digits>"} and a float as {"float": "<repr>"},
so that no value loses digits in JSON.
"""
import itertools
import math
import random
import warnings

from ovos_number_parser import pronounce_number
from ovos_number_parser.util import DigitPronunciation, GrammaticalGender, Scale
from tugalex import TugaLexicon
from tugaphone import text_normalization as tn
from tugaphone.number_utils import NumberParser as NP, normalize_numbers

from . import common
from .text_normalization import rule_texts

warnings.simplefilter("ignore")


def num(value):
    if value is None:
        return None
    if isinstance(value, bool):
        return value
    if isinstance(value, int):
        return {"int": str(value)}
    return {"float": repr(value)}


def name(value):
    return None if value is None else value.name


def pronounce(number, lang, places=3, short_scale=None, scientific=False, ordinals=False, digits=None,
              gender=GrammaticalGender.MASCULINE, scale=None):
    result = common.call(pronounce_number, number, lang, places=places, short_scale=short_scale,
                         scientific=scientific, ordinals=ordinals, digits=digits, gender=gender, scale=scale)
    return ["pronounce_number", num(number), lang, places, short_scale, scientific, ordinals, name(digits),
            name(gender), name(scale), result]


def number_grid():
    ints = list(range(0, 1101)) + [1999, 2000, 2001, 2024, 9999, 10001, 21000, 100000, 100001, 101000, 999999]
    for k in list(range(3, 64)) + [66, 72, 99, 100, 120, 303, 600, 1000, 4299]:
        ints += [10 ** k, 10 ** k - 1, 10 ** k + 1, 2 * 10 ** k, 21 * 10 ** k + 1001, 10 ** k + 10 ** (k // 2)]
    ints = [n for n in ints if n < 10 ** 4300]  # str(int) refuses more than 4300 digits
    ints += [-n for n in ints[::7]]
    rng = random.Random(5)
    ints += [rng.randrange(10 ** rng.randint(1, 40)) for _ in range(600)]
    floats = [0.0, -0.0, 0.5, 1.0, 1.5, 1.25, 0.001, 0.0004, 0.0005, 0.9995, 0.999999, 2.675, 1.005, 10.1, 12.34,
              123.456789, 1e15, 1e16, 1e16 + 2, 1.5e300, 1e-5, 1e-300, 5e-324, 1.7976931348623157e308, 3.14159,
              100.0, 1000000.0, 0.1 + 0.2, 21.21, 1001.001, 2.0 ** 53, 2.0 ** 70, math.inf, -math.inf, math.nan,
              -1.5, -0.001, -1e20, 99.995, 0.045, 1.0000001]
    floats += [rng.uniform(-1e6, 1e6) for _ in range(300)] + [rng.uniform(0, 2) for _ in range(100)]
    floats += [float(i) / 8 for i in range(-40, 41)]
    return ints, floats


def pronounce_records():
    ints, floats = number_grid()
    langs = ["pt-PT", "pt-BR"]
    out = []
    for n in ints + floats:
        for lang in langs:
            out.append(pronounce(n, lang))
    small = [n for n in ints if abs(n) < 3000][::3] + [10 ** 6, 10 ** 9, 2 * 10 ** 12, 10 ** 15, 10 ** 18 + 1]
    for n in small:
        for lang, ordinals, gender in itertools.product(langs, (False, True), list(GrammaticalGender)):
            out.append(pronounce(n, lang, ordinals=ordinals, gender=gender))
    big = [n for n in ints if abs(n) >= 10 ** 5][::5]
    for n in big + floats[::3]:
        for lang, short_scale, scale in itertools.product(["pt-PT", "pt-BR", "pt", "PT-br", "pt-AO", "pt-BR-x-sp"],
                                                          (None, True, False), (None, Scale.SHORT, Scale.LONG)):
            out.append(pronounce(n, lang, short_scale=short_scale, scale=scale))
    for n in floats + ints[:50] + [10 ** 20, -10 ** 30]:
        for places, scientific, digits in itertools.product((0, 1, 2, 5), (False, True),
                                                            (None, *DigitPronunciation)):
            out.append(pronounce(n, "pt-PT", places=places, scientific=scientific, digits=digits))
    # Ten or more decimal places reach the scale words inside the fraction.
    long_fractions = [0.1234567891234, 1.0000000001, 3.141592653589793, 2.718281828459045, 0.3333333333333333,
                      123.45678901234, 1e-12, 9.99999999999]
    for n in long_fractions + floats[:40]:
        for lang, places in itertools.product(["pt-PT", "pt-BR"], (7, 10, 12, 16, 20)):
            out.append(pronounce(n, lang, places=places))
    return out


TOKENS = ["1", "0", "01", "21", "1º", "1ª", "1 º", "2 ª", "1.º", "3.ª", "2,5", "2.5", "1.5", "1,5", "0,5", "1e9",
          "1,5e3", "2.5E-3", "1e", "e5", "1e5e5", "1E+5", "-3", "+3", "−3", "٣", "１２", "1_000", "_1", " 7 ", "0x10",
          "inf", "-inf", "Infinity", "nan", "NaN", "1.2.3", "10,", "10.", ",5", ".5", "3.º", "1.000.000", "1,000,000",
          "", " ", "abc", "º", "ª", "ºª", "1ºª", "54.", "05,", "5!", "5…", "5”", "5)", "12:30", "2024", "10000000000",
          "123456789012345678901234567890", "9" * 400, "1" + "0" * 4300, "1" + "0" * 4299, "2e400", "1e-400",
          "1,2,3", "1.5,3", "5%", "5kg", "1x2", "²", "1²", " 1 ", "1 ", "0.0", "00", "-0", "-0.0",
          "1e1.5", "1.5e1.5", "e", "E", "1e09", "1e-09", "1 e 5", "1.e5", "1e5.", "1,e5", "3,14", "3,14159"]
NEIGHBOURS = [None, "", "casas", "casa", "horas", "hora", "a", "as", "o", "os", "da", "dos", "º", "ª", "kg", "€",
              "Maria", "de", "pessoas", "vezes", "à", "às", "anos", "andar", "lugar", "vez", "km", "%"]


def token_records(tokens):
    out = []
    for w in tokens:
        out.append(["token", w, [
            list(NP.split_trailing_punctuation(w)), num(common.call(NP.to_int, w)), common.call(NP.is_int, w),
            num(common.call(NP.to_float, w)), common.call(NP.is_float, w), common.call(NP.is_decimal, w),
            common.call(NP.is_scientific_notation, w),
            [common.call(NP.pronounce_scientific, w, b, s) for b in (False, True) for s in (None, "short", "long")],
        ]])
    return out


def context_records(tokens):
    rng = random.Random(9)
    out = []
    for w in tokens:
        for _ in range(6):
            prev, nxt = rng.choice(NEIGHBOURS), rng.choice(NEIGHBOURS)
            out.append(["context", w, prev, nxt, common.call(NP.is_ordinal, w, nxt),
                        common.call(NP.get_number_gender, w, prev, nxt)])
        combos = itertools.product((None, "", "masculine", "feminine", "neuter"), (None, False, True), (False, True),
                                   (None, "", "short", "long", "other"))
        for gender, as_ordinal, br, scale in combos:
            if rng.random() < 0.25:
                prev, nxt = rng.choice(NEIGHBOURS), rng.choice(NEIGHBOURS)
                out.append(["word", w, prev, nxt, gender, as_ordinal, br, scale,
                            common.call(NP.pronounce_number_word, w, prev, nxt, gender, as_ordinal, br, scale)])
    return out


def table_records():
    out = [["sets", {k: sorted(getattr(NP, k)) for k in ("LABEL_HEADS", "PLURAL_FUNCTION_WORDS", "FEMININE_DETERMINERS",
                                                         "MASCULINE_DETERMINERS", "FUNCTION_WORDS", "RANGE_LINKS")},
            NP.ORDINAL_MALE, NP.ORDINAL_FEMALE, NP.ORDINAL_TOKENS, NP.TRAILING_PUNCTUATION,
            [[k, list(v)] for k, v in NP.UNIT_NAMES.items()]]]
    for symbol in list(NP.UNIT_NAMES) + ["", "xyz", "KG"]:
        for plural, br in itertools.product((False, True), (False, True)):
            out.append(["unit_name", symbol, plural, br, common.call(NP.unit_name, symbol, plural, br)])
    for value in (None, 0, 1, 2, -1, 0.5, 1.0, 1.5, 0.0, -0.0, math.inf, -math.inf, math.nan, 10 ** 30):
        for is_decimal, br in itertools.product((False, True), (False, True)):
            out.append(["unit_takes_plural", num(value), is_decimal, br,
                        common.call(NP.unit_takes_plural, value, is_decimal, br)])
    return out


def sentence_records(texts, langs, stricts=(True, False), scales=(None,)):
    out = []
    for i, text in enumerate(texts):
        lang = langs[i % len(langs)]
        for strict in stricts:
            for scale in scales:
                out.append(["normalize_numbers", text, lang, strict, scale,
                            common.call(normalize_numbers, text, lang, strict, scale)])
    return out


# Python's int() and float() read these words as numbers, so normalize_numbers
# treats them as numeric tokens.
NONFINITE = ["o Inf. Henrique", "inf", "-inf", "+inf", "INF.", "Inf,", "nan", "NaN!", "o NaN", "Infinity", "infinity…",
             "-Infinity", "iNfInItY", "nan e 2", "2 inf", "1 Inf. 2", "(inf)", "inf.", "infs", "nans", "infinite"]


def noun_texts():
    """Counted nouns of the lexicon that end in -agem or hold a hyphen, after a number."""
    nouns = sorted(TugaLexicon().get_ipa_map("NOUN", "lbx"))
    agem = [w for w in nouns if w.endswith("agem")]
    hyphen = [w for w in nouns if "-" in w]
    out = []
    for w in agem:
        out += [f"1 {w}", f"2 {w[:-1]}ns", f"a 1 {w}", f"o 1 {w}.", f"21 {w.upper()}"]
    for w in hyphen:
        out += [f"1 {w}", f"2 {w}s", f"1 {w}…"]
    return out


def corpora(repo):
    test_strings = common.unique(common.test_strings(repo))
    test_tokens = common.unique(t for s in test_strings for t in s.split() if any(c.isdigit() for c in t))
    tokens = common.unique(TOKENS + test_tokens)
    rules = common.unique(rule_texts())
    rules_normalized = common.unique(tn.normalize_orthography(t) for t in rules)
    langs = ["pt-PT", "pt-BR", "pt-br-x-rio", "PT-BR", "pt-AO", "en", ""]
    unit_texts = []
    for symbol in NP.UNIT_NAMES:
        for n in ("1", "0", "2", "1,5", "0,5", "1.000", "um", "21", "1º"):
            unit_texts += [f"{n} {symbol}", f"são {n} {symbol}.", f"a {n} {symbol} de"]
    alphabet = list("0123456789.,-:eEº ª%") + [" kg", " €", " casas", " horas", " a ", " o ", " as ", "1º", " "]
    fuzz = common.unique(common.fuzz_strings(31, 2500, alphabet, 8))
    probes = common.unique([f"{p}1 {p}2,5{p} 3{p}º" for p in common.UNICODE_PROBES] + common.UNICODE_PROBES)
    return [
        ("tables", table_records()),
        ("tokens", token_records(tokens)),
        ("context", context_records(tokens)),
        ("pronounce_number", pronounce_records()),
        ("tests", sentence_records(test_strings, ["pt-PT", "pt-BR"], scales=(None, "short", "long"))),
        ("rules", sentence_records(rules_normalized, langs)),
        ("units", sentence_records(unit_texts, ["pt-PT", "pt-BR"])),
        ("nouns", sentence_records(common.unique(noun_texts()), ["pt-PT", "pt-BR"])),
        ("nonfinite", sentence_records(NONFINITE, ["pt-PT", "pt-BR"])),
        ("unicode", sentence_records(probes, ["pt-PT", "pt-BR"])),
        ("fuzz", sentence_records(fuzz, langs, scales=(None, "short"))),
    ]
