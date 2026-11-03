"""Write data/dialects.json from the reference tugaphone.dialects module.

The file holds the literal default of every DialectInventory field that
__post_init__ fills from a literal, the ARCHAIC_WORDS set, the AO1990
instance arguments, and the entries each national subclass adds to the
AO1990 tables. The fields that __post_init__ derives from other fields are
left out: the C++ code derives them in the same way.

The script checks its own output. It rebuilds each subclass table from
AO1990 and the added entries, and asserts that the result is the Python
value, in the Python order. It asserts that every other literal field of a
subclass is the AO1990 value.

Run it with the reference interpreter:
    python -I tools/gen_dialect_data.py data/dialects.json
"""
import dataclasses
import json
import sys
import typing

from tugaphone import dialects
from importlib.metadata import version

# Fields that __post_init__ computes from other fields, and the string fields
# that keep their dataclass defaults.
DERIVED = {"PRIMARY_STRESS_MARKERS", "SECONDARY_STRESS_MARKERS", "ALL_VOWEL_CHARS", "VOWEL_HEIGHT",
           "PUNCT2IPA", "DIPHTHONG2IPA", "GRAPHEME_INVENTORY", "IRREGULAR_WORDS"}

# The tables each subclass passes to DialectInventory.__init__ as {**AO1990.X, ...}.
MERGES = {
    "EuropeanPortuguese": ("FALLING_NASAL_DIPHTHONGS", "TRIPHTHONG2IPA"),
    "BrazilianPortuguese": ("DIGRAPH2IPA", "DEFAULT_CHAR2PHONEMES"),
    "AngolanPortuguese": ("DIGRAPH2IPA", "DEFAULT_CHAR2PHONEMES"),
    "MozambicanPortuguese": ("DIGRAPH2IPA", "DEFAULT_CHAR2PHONEMES"),
    "TimoresePortuguese": ("DIGRAPH2IPA", "DEFAULT_CHAR2PHONEMES"),
}


def kind(field):
    t = field.type
    if t is str:
        return "str"
    origin, args = typing.get_origin(t), typing.get_args(t)
    if origin is set:
        return "set"
    if origin is list:
        return "list"
    if origin is dict:
        return "dict_of_sets" if typing.get_origin(args[1]) is set else "dict"
    raise TypeError(f"{field.name}: {t}")


def encode(value):
    if isinstance(value, (set, frozenset)):
        return sorted(value)
    if isinstance(value, dict):
        return {k: encode(v) for k, v in value.items()}
    return value


def main(out_path):
    fields = dataclasses.fields(dialects.DialectInventory)
    base = dialects.DialectInventory()
    ao = dialects.AO1990
    defaults = {}
    for f in fields:
        if kind(f) == "str" or f.name in DERIVED:
            continue
        value = getattr(base, f.name)
        assert value, f"{f.name} has no literal default"
        defaults[f.name] = encode(value)

    classes = {}
    for name, merged in MERGES.items():
        inst = getattr(dialects, name)(IRREGULAR_WORDS={"_": "_"})
        adds = {}
        for f in fields:
            if kind(f) == "str" or f.name in DERIVED:
                continue
            got, parent = getattr(inst, f.name), getattr(ao, f.name)
            if f.name in merged:
                add = {k: v for k, v in got.items() if k not in parent or parent[k] != v}
                rebuilt = {**parent, **add}
                assert list(rebuilt.items()) == list(got.items()), (name, f.name)
                adds[f.name] = add
            else:
                assert got == parent, (name, f.name)
        classes[name] = adds

    doc = {
        "tugaphone": version("tugaphone"),
        "fields": [{"name": f.name, "kind": kind(f)} for f in fields],
        "defaults": defaults,
        "ARCHAIC_WORDS": sorted(base.ARCHAIC_WORDS),
        "AO1990": {"dialect_code": ao.dialect_code, "IRREGULAR_WORDS": ao.IRREGULAR_WORDS},
        "merges": classes,
    }
    with open(out_path, "w", encoding="utf-8") as fh:
        json.dump(doc, fh, ensure_ascii=False, indent=1)
        fh.write("\n")
    print(f"{len(fields)} fields, {len(defaults)} literal defaults, {len(classes)} subclasses -> {out_path}")


if __name__ == "__main__":
    main(sys.argv[1])
