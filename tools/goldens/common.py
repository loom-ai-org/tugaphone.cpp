"""Shared pieces of the golden dumpers.

A golden file is tests/goldens/<module>.jsonl.gz. Its first line is a header
with the reference versions, the record count and the count per corpus. Each
other line is one record: [function, arguments..., result], written as compact
JSON. An exception is recorded as {"err": "<class name>"}, because the
exception class is part of the contract. A float is recorded as its repr.
"""
import ast
import gzip
import importlib.metadata
import io
import json
import random
import subprocess
import sys

# linguatag and langcodes decide how orthography2ipa resolves a language code.
REFERENCE = ("tugaphone", "tugalex", "orthography2ipa", "o2i-lexicons", "silabificador", "bifonia",
             "ovos-number-parser", "markovonnx", "linguatag", "langcodes")
TAG = "1.6.2a1"

# Strings that exercise the code-point layer: case mappings that change the
# length, final sigma, non-ASCII digits and spaces, combining marks, astral
# characters, and the characters the rules look for.
UNICODE_PROBES = [
    "", " ", "\t\n", " ", " ", "　", "​", "ß", "ẞ", "ŉ", "ǅ", "ǈ", "ﬁ", "İ", "ı", "Σ", "ΑΣ",
    "ΟΔΟΣ ΑΣ", "ς", "Å", "Ω", "K", "é", "ão", "ɐ̃", "٣", "३", "੩", "１２", "²", "³", "½", "Ⅻ", "ⅻ",
    "𝟗", "𐒡", "😀", "€", "£", "º", "ª", "…", "–", "—", "−", "×", "“", "”", "¿", "¡", "�", "\x00", "\x7f",
    "\u0085", " ", " ", "\U0001d7ce\U0001d7cf",
]


def dumps(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


def call(fn, *args, **kwargs):
    try:
        return fn(*args, **kwargs)
    except Exception as exc:  # the exception class is part of the contract
        return {"err": type(exc).__name__}


def versions():
    return {name: importlib.metadata.version(name) for name in REFERENCE}


def test_sources(repo):
    """{file name: source} of every tests/*.py of the reference tag."""
    names = subprocess.run(["git", "-C", repo, "ls-tree", "--name-only", TAG, "tests/"], check=True,
                           capture_output=True, text=True).stdout.split()
    out = {}
    for name in names:
        if name.endswith(".py"):
            out[name] = subprocess.run(["git", "-C", repo, "show", f"{TAG}:{name}"], check=True,
                                       capture_output=True, text=True).stdout
    return out


def test_strings(repo):
    """Every string literal of the reference tests, in first-seen order."""
    seen = {}
    for name, source in sorted(test_sources(repo).items()):
        for node in ast.walk(ast.parse(source)):
            if isinstance(node, ast.Constant) and isinstance(node.value, str):
                seen.setdefault(node.value, None)
    return list(seen)


def fuzz_strings(seed, count, alphabet, max_len=12):
    rng = random.Random(seed)
    return ["".join(rng.choice(alphabet) for _ in range(rng.randint(0, max_len))) for _ in range(count)]


def unique(items):
    return list(dict.fromkeys(items))


def write(path, module, corpora):
    """corpora: [(corpus name, [record, ...])]. Prints N in and N out per corpus."""
    lines = []
    counts = {}
    for name, records in corpora:
        counts[name] = len(records)
        lines.extend(dumps(r) for r in records)
        print(f"  {module}/{name}: {len(records)} in, {len(records)} out", file=sys.stderr)
    header = {"module": module, "versions": versions(), "corpora": counts, "count": len(lines)}
    raw = io.BytesIO()
    with gzip.GzipFile(fileobj=raw, mode="wb", mtime=0, filename="") as gz:
        gz.write((dumps(header) + "\n").encode("utf-8"))
        for line in lines:
            gz.write((line + "\n").encode("utf-8"))
    with open(path, "wb") as fh:
        fh.write(raw.getvalue())
    print(f"{path}: {len(lines)} records", file=sys.stderr)
    return len(lines)
