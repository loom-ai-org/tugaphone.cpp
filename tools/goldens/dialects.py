"""Goldens of tugaphone.dialects.

A record builds one inventory from a spec ({"class", "dialect_code",
"IRREGULAR_WORDS", "kwargs"}; an absent key is not passed) and writes every
dataclass field in order. Sets are written sorted, VOWEL_HEIGHT is written
sorted because its order follows set hash order, and IRREGULAR_WORDS is
written as its size and the FNV-1a 64 hash of "key\\tvalue\\n" over its items
in order.
"""
import dataclasses

from tugaphone import dialects

from . import common

CLASSES = ["DialectInventory", "EuropeanPortuguese", "LisbonPortuguese", "BrazilianPortuguese",
           "RioJaneiroPortuguese", "SaoPauloPortuguese", "AngolanPortuguese", "MozambicanPortuguese",
           "TimoresePortuguese"]
NATIONAL = ["EuropeanPortuguese", "BrazilianPortuguese", "AngolanPortuguese", "MozambicanPortuguese",
            "TimoresePortuguese"]


def fnv1a64(items):
    h = 0xcbf29ce484222325
    for k, v in items:
        for b in f"{k}\t{v}\n".encode("utf-8"):
            h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return f"{h:016x}"


def encode_field(name, value):
    if name == "IRREGULAR_WORDS":
        return [len(value), fnv1a64(value.items())]
    if name == "VOWEL_HEIGHT":
        return sorted(value.items())
    if isinstance(value, set):
        return sorted(value)
    if isinstance(value, dict):
        return [[k, sorted(v) if isinstance(v, set) else v] for k, v in value.items()]
    return value


def encode(inv):
    fields = [[f.name, encode_field(f.name, getattr(inv, f.name))] for f in dataclasses.fields(inv)]
    return [type(inv).__name__, fields, sorted(inv.ARCHAIC_WORDS)]


def build(spec):
    cls = getattr(dialects, spec["class"])
    kwargs = dict(spec.get("kwargs", {}))
    for name, value in kwargs.items():
        kind = {f.name: f.type for f in dataclasses.fields(dialects.DialectInventory)}[name]
        if "Set" in str(kind) and "Dict" not in str(kind):
            kwargs[name] = set(value)
        elif "Dict[str, typing.Set" in str(kind):
            kwargs[name] = {k: set(v) for k, v in value.items()}
    for key in ("dialect_code", "IRREGULAR_WORDS"):
        if key in spec:
            kwargs[key] = spec[key]
    return cls(**kwargs)


def specs():
    out = [{"class": c} for c in CLASSES]
    base_kwargs = [
        {"VOWEL_CHARS": ["a", "y"]}, {"ACUTE_VOWEL_CHARS": ["ý"]}, {"TILDE_VOWEL_CHARS": []},
        {"HIATUS_TOKEN": "|"}, {"HIATUS_TOKEN": ""}, {"PRIMARY_STRESS_TOKEN": "'", "HIATUS_TOKEN": ".."},
        {"CLOSED_VOWELS": ["x", "i"]}, {"OPEN_VOWELS": ["i"]}, {"SEMI_OPEN_VOWELS": ["ɐ̃"]},
        {"RISING_ORAL_DIPHTHONGS": {"ab": "cd", "ef": "cd"}}, {"FALLING_NASAL_DIPHTHONGS": {"x": "y"}},
        {"GRAPHEME_INVENTORY": ["z"]}, {"PUNCT2IPA": {"!": "?"}}, {"VOWEL_HEIGHT": {"a": "b"}},
        {"ARCHAIC_MUTE_P": {"pt": ["a", "b"]}}, {"HIATUS_PREFIXES": ["ultra", "ü"]},
        {"PRIMARY_STRESS_MARKERS": ["q"]}, {"ALL_VOWEL_CHARS": ["aa", "ê"]}, {"DIPHTHONG2IPA": {"k": "v"}},
        {"DIGRAPH2IPA": {"zz": "z"}}, {"PUNCT_CHARS": ["!"]}, {"dialect_code": ""}, {"dialect_code": "xx"},
        {"IRREGULAR_WORDS": {"b": "B", "a": "A"}},
    ]
    out += [{"class": "DialectInventory", "kwargs": k} for k in base_kwargs]
    for c in NATIONAL:
        out += [
            {"class": c, "dialect_code": ""}, {"class": c, "dialect_code": "pt-XX-x-test"},
            {"class": c, "dialect_code": None}, {"class": c, "IRREGULAR_WORDS": {}},
            {"class": c, "IRREGULAR_WORDS": None}, {"class": c, "IRREGULAR_WORDS": {"z": "y", "a": "b"}},
            {"class": c, "kwargs": {"PUNCT_CHARS": ["!"]}}, {"class": c, "kwargs": {"HIATUS_TOKEN": "~"}},
            {"class": c, "kwargs": {"VOWEL_CHARS": []}},
            {"class": c, "kwargs": {"DIGRAPH2IPA": {}}}, {"class": c, "kwargs": {"TRIPHTHONG2IPA": {"a": "b"}}},
            {"class": c, "kwargs": {"FALLING_NASAL_DIPHTHONGS": {}}},
            {"class": c, "kwargs": {"DEFAULT_CHAR2PHONEMES": {"a": "b"}}},
        ]
    return out


def corpora(repo):
    del repo
    built = [["inventory", s, common.call(lambda s=s: encode(build(s)))] for s in specs()]
    ao = [["AO1990", encode(dialects.AO1990)]]
    pairs = [({"class": a}, {"class": b}) for a in CLASSES for b in CLASSES]
    pairs += [({"class": "EuropeanPortuguese"}, {"class": "EuropeanPortuguese", "dialect_code": "pt-PT"}),
              ({"class": "DialectInventory"}, {"class": "DialectInventory", "kwargs": {"VOWEL_CHARS": ["a"]}}),
              ({"class": "DialectInventory", "kwargs": {"RISING_ORAL_DIPHTHONGS": {"a": "b", "c": "d"}}},
               {"class": "DialectInventory", "kwargs": {"RISING_ORAL_DIPHTHONGS": {"c": "d", "a": "b"}}}),
              ({"class": "AngolanPortuguese", "IRREGULAR_WORDS": {}},
               {"class": "AngolanPortuguese", "IRREGULAR_WORDS": None})]
    eq = [["eq", a, b, common.call(lambda a=a, b=b: build(a) == build(b))] for a, b in pairs]
    return [("inventories", built), ("ao1990", ao), ("equality", eq)]
