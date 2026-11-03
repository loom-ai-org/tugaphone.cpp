"""Generate src/unicode_norm_tables.inc from the running Python's Unicode database.

The tables hold what tools/gen_unicode_tables.py leaves out:
- the canonical decompositions and combining classes that
  unicodedata.normalize("NFD"/"NFC") and unicodedata.combining use,
- the canonical composition pairs of NFC,
- the simple case mappings and the extra case equivalences that the re
  module uses for IGNORECASE (_sre.unicode_tolower, _sre.unicode_iscased,
  re._casefix._EXTRA_CASES),
- str.isprintable, which repr() of a str uses, and the code points
  str.isidentifier() accepts first and after the first.

Before it writes anything, the script runs the same normalization algorithm in
Python over the tables and checks it against unicodedata for every code point
and for a seeded set of sequences.

Usage: python -I tools/gen_unicode_norm_tables.py > src/unicode_norm_tables.inc
"""
import random
import sys
import unicodedata

import _sre
from re._casefix import _EXTRA_CASES

MAX = 0x110000
S_BASE, L_BASE, V_BASE, T_BASE = 0xAC00, 0x1100, 0x1161, 0x11A7
L_COUNT, V_COUNT, T_COUNT = 19, 21, 28
N_COUNT = V_COUNT * T_COUNT
S_COUNT = L_COUNT * N_COUNT


def surrogate(cp):
    return 0xD800 <= cp <= 0xDFFF


def hangul(cp):
    return S_BASE <= cp < S_BASE + S_COUNT


POINTS = [cp for cp in range(MAX) if not surrogate(cp)]
CCC = {cp: unicodedata.combining(chr(cp)) for cp in POINTS if unicodedata.combining(chr(cp))}
DECOMP = {}
for cp in POINTS:
    if hangul(cp):
        continue
    full = unicodedata.normalize("NFD", chr(cp))
    if full != chr(cp):
        DECOMP[cp] = [ord(c) for c in full]
PAIRS = {}
for cp in POINTS:
    raw = unicodedata.decomposition(chr(cp))
    if not raw or raw.startswith("<") or hangul(cp):
        continue
    parts = [int(x, 16) for x in raw.split()]
    if len(parts) == 2 and unicodedata.normalize("NFC", chr(cp)) == chr(cp) and CCC.get(parts[0], 0) == 0:
        PAIRS[(parts[0], parts[1])] = cp


def decompose(s):
    out = []
    for ch in s:
        cp = ord(ch)
        if hangul(cp):
            s_index = cp - S_BASE
            out += [L_BASE + s_index // N_COUNT, V_BASE + (s_index % N_COUNT) // T_COUNT]
            if s_index % T_COUNT:
                out.append(T_BASE + s_index % T_COUNT)
        else:
            out += DECOMP.get(cp, [cp])
    # canonical ordering: a stable sort of each run of non-starters
    i = 0
    while i < len(out):
        if CCC.get(out[i], 0) == 0:
            i += 1
            continue
        j = i
        while j < len(out) and CCC.get(out[j], 0) != 0:
            j += 1
        out[i:j] = sorted(out[i:j], key=lambda c: CCC.get(c, 0))
        i = j
    return out


def compose(cps):
    if not cps:
        return []
    out = list(cps)
    starter = 0 if CCC.get(out[0], 0) == 0 else None
    last_ccc = -1 if starter is not None else 256
    k = 1
    for i in range(1, len(out)):
        c = out[i]
        ccc = CCC.get(c, 0)
        composite = None
        if starter is not None and (last_ccc == -1 or last_ccc < ccc):
            first = out[starter]
            if L_BASE <= first < L_BASE + L_COUNT and V_BASE <= c < V_BASE + V_COUNT and last_ccc == -1:
                composite = S_BASE + ((first - L_BASE) * V_COUNT + (c - V_BASE)) * T_COUNT
            elif hangul(first) and (first - S_BASE) % T_COUNT == 0 and T_BASE < c < T_BASE + T_COUNT \
                    and last_ccc == -1:
                composite = first + (c - T_BASE)
            else:
                composite = PAIRS.get((first, c))
        if composite is not None:
            out[starter] = composite
            continue
        if ccc == 0:
            starter = k
            last_ccc = -1
        else:
            last_ccc = ccc
        out[k] = c
        k += 1
    return out[:k]


def nfd(s):
    return "".join(map(chr, decompose(s)))


def nfc(s):
    return "".join(map(chr, compose(decompose(s))))


def self_check():
    for cp in POINTS:
        c = chr(cp)
        assert nfd(c) == unicodedata.normalize("NFD", c), hex(cp)
        assert nfc(c) == unicodedata.normalize("NFC", c), hex(cp)
    rng = random.Random(7)
    pool = sorted(set(list(CCC)[:400] + [k[0] for k in PAIRS][:600] + [k[1] for k in PAIRS] +
                      list(range(0x1100, 0x1113)) + list(range(0x1161, 0x1176)) + list(range(0x11A8, 0x11C3)) +
                      list(range(0xAC00, 0xAC40)) + list(range(0x41, 0x7B)) + list(DECOMP)[:800]))
    for _ in range(200000):
        s = "".join(chr(rng.choice(pool)) for _ in range(rng.randint(1, 6)))
        assert nfd(s) == unicodedata.normalize("NFD", s), [hex(ord(x)) for x in s]
        assert nfc(s) == unicodedata.normalize("NFC", s), [hex(ord(x)) for x in s]


def ranges(predicate):
    out, start = [], None
    for cp in range(MAX + 1):
        hit = cp < MAX and predicate(cp)
        if hit and start is None:
            start = cp
        elif not hit and start is not None:
            out.append((start, cp - 1))
            start = None
    return out


def main():
    self_check()
    w = sys.stdout.write
    w("// Generated by tools/gen_unicode_norm_tables.py; do not edit.\n")
    w(f"// Python {sys.version.split()[0]}, Unicode {unicodedata.unidata_version}.\n\n")

    w(f"static const uint32_t kCombiningClass[{len(CCC)}][2] = {{\n")
    for cp in sorted(CCC):
        w(f"  {{0x{cp:X}, {CCC[cp]}}},\n")
    w("};\n\n")

    pool = []
    entries = []
    for cp in sorted(DECOMP):
        entries.append((cp, len(pool), len(DECOMP[cp])))
        pool += DECOMP[cp]
    w(f"static const char32_t kDecompPool[{len(pool)}] = {{\n")
    for i in range(0, len(pool), 12):
        w("  " + ", ".join(f"0x{c:X}" for c in pool[i:i + 12]) + ",\n")
    w("};\n\n")
    w(f"static const uint32_t kDecomp[{len(entries)}][3] = {{\n")
    for cp, offset, n in entries:
        w(f"  {{0x{cp:X}, {offset}, {n}}},\n")
    w("};\n\n")

    w(f"static const char32_t kCompose[{len(PAIRS)}][3] = {{\n")
    for (a, b), cp in sorted(PAIRS.items()):
        w(f"  {{0x{a:X}, 0x{b:X}, 0x{cp:X}}},\n")
    w("};\n\n")

    lower = [(cp, _sre.unicode_tolower(cp)) for cp in POINTS if _sre.unicode_tolower(cp) != cp]
    w(f"static const char32_t kSimpleLower[{len(lower)}][2] = {{\n")
    for cp, lo in lower:
        w(f"  {{0x{cp:X}, 0x{lo:X}}},\n")
    w("};\n\n")
    # re's RANGE_UNI_IGNORE upper-cases with the simple mapping; the full
    # mapping of a lower-cased code point is one code point wherever re uses it.
    upper = [(cp, ord(chr(cp).upper())) for cp in POINTS
             if len(chr(cp).upper()) == 1 and chr(cp).upper() != chr(cp)]
    w(f"static const char32_t kSimpleUpper[{len(upper)}][2] = {{\n")
    for cp, up in upper:
        w(f"  {{0x{cp:X}, 0x{up:X}}},\n")
    w("};\n\n")

    cased = ranges(lambda cp: not surrogate(cp) and _sre.unicode_iscased(cp))
    w(f"static const char32_t kSreCased[{len(cased)}][2] = {{\n")
    for lo, hi in cased:
        w(f"  {{0x{lo:X}, 0x{hi:X}}},\n")
    w("};\n\n")

    fixes = sorted(_EXTRA_CASES.items())
    w(f"static const char32_t kCaseFix[{len(fixes)}][3] = {{\n")
    for cp, others in fixes:
        assert len(others) <= 2
        padded = list(others) + [0] * (2 - len(others))
        w(f"  {{0x{cp:X}, 0x{padded[0]:X}, 0x{padded[1]:X}}},\n")
    w("};\n\n")

    for name, pred in (("kXidStart", lambda c: c.isidentifier()),
                       ("kXidContinue", lambda c: ("a" + c).isidentifier())):
        rs = ranges(lambda cp: not surrogate(cp) and pred(chr(cp)))
        w(f"static const char32_t {name}[{len(rs)}][2] = {{\n")
        for lo, hi in rs:
            w(f"  {{0x{lo:X}, 0x{hi:X}}},\n")
        w("};\n\n")

    printable = ranges(lambda cp: not surrogate(cp) and chr(cp).isprintable())
    w(f"static const char32_t kPrintable[{len(printable)}][2] = {{\n")
    for lo, hi in printable:
        w(f"  {{0x{lo:X}, 0x{hi:X}}},\n")
    w("};\n")


if __name__ == "__main__":
    main()
