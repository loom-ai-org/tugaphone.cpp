"""Goldens of tugaphone.tokenizer: the rules-only IPA path and the token features.

A dialect is named by a spec string that the C++ parity test builds the same
way: "<Class>" or "<Class>:<dialect_code>".
"""
import subprocess
import zlib

from tugaphone import dialects
from tugaphone import tokenizer as tk

from . import common

DIALECTS = [
    "EuropeanPortuguese", "BrazilianPortuguese", "AngolanPortuguese", "MozambicanPortuguese", "TimoresePortuguese",
    "LisbonPortuguese", "RioJaneiroPortuguese", "SaoPauloPortuguese", "DialectInventory",
    "EuropeanPortuguese:pt-PT-x-porto", "BrazilianPortuguese:pt-BR-x-sp", "EuropeanPortuguese:xx-YY",
    "EuropeanPortuguese:en-US",
]
NATIONAL = DIALECTS[:5]

_CACHE = {}


def dialect(spec):
    if spec not in _CACHE:
        name, _, code = spec.partition(":")
        cls = getattr(dialects, name)
        _CACHE[spec] = cls(dialect_code=code) if code else cls()
    return _CACHE[spec]


def features_json(feats):
    return common.dumps(feats)


def crc(text):
    return zlib.crc32(text.encode("utf-8"))


def opt_repr(token):
    return None if token is None else token.surface


def char_props(c):
    return [c.surface, c.char_idx, c.idx_in_word, c.idx_in_sentence, c.prefix, c.suffix, opt_repr(c.prev_char),
            opt_repr(c.next_char), c.parent_syllable, c.is_nucleus, repr(c), c == c.surface]


def grapheme_props(g):
    return [g.surface, g.grapheme_idx, g.syllable_idx, g.idx_in_word, g.idx_in_sentence, g.prefix, g.suffix,
            opt_repr(g.prev_grapheme), opt_repr(g.next_grapheme), g.parent_syllable, repr(g),
            [char_props(c) for c in g.characters]]


def word_props(w):
    return [w.surface, w.word_idx, w.chunk_idx, w.normalized, w.syllables, w.normalized_syllables,
            w.idx_in_sentence, opt_repr(w.prev_word), opt_repr(w.next_word), [x.surface for x in w.hyphen_group],
            w.is_function_word, w.is_hyphen_clitic, w.stress_token, repr(w),
            [grapheme_props(g) for g in w.graphemes]]


def sentence_record(spec, text, mode):
    """mode "ipa": the IPA and a CRC of the features JSON; "props": also the
    repr and a CRC of every token property; "full": the features JSON and the
    token properties themselves."""
    def run():
        s = tk.Sentence(text, dialect=dialect(spec))
        out = [s.normalized, s.n_words, s.ipa]
        feats = features_json(s.features)
        out.append(feats if mode == "full" else [crc(feats), len(feats)])
        if mode != "ipa":
            out.append(repr(s))
            props = [word_props(w) for w in s.words]
            if mode == "full":
                out.append(props)
            else:
                text_props = common.dumps(props)
                out.append([crc(text_props), len(text_props)])
        return out
    return ["sentence", spec, text, mode, common.call(run)]


def mode_for(text, limit):
    return "full" if len(text) <= limit else "props"


def gold_words(repo):
    names = subprocess.run(["git", "-C", repo, "ls-tree", "-r", "--name-only", common.TAG, "tests/data/gold"],
                           check=True, capture_output=True, text=True).stdout.split()
    words = []
    for name in sorted(names):
        text = subprocess.run(["git", "-C", repo, "show", f"{common.TAG}:{name}"], check=True,
                              capture_output=True, text=True).stdout
        for line in text.splitlines():
            if line and not line.startswith("#"):
                words.append(line.split("\t")[0])
    return words


def custom_word_record(spec, surface, syllables, chunk_idx):
    """A WordToken built with syllables the Sentence path never passes."""
    def run():
        w = tk.WordToken(surface, 0, syllables=list(syllables), dialect=dialect(spec), chunk_idx=chunk_idx)
        return [w.ipa, features_json(w.features), word_props(w), w.stress_pattern, w.stressed_syllable_idx]
    return ["word", spec, surface, syllables, chunk_idx, common.call(run)]


def manual_sentence_record(spec, surfaces):
    """A Sentence given its words: each word keeps its own dialect."""
    def run():
        words = [tk.WordToken(s, i, dialect=dialect(spec), chunk_idx=0 if i < 2 else None)
                 for i, s in enumerate(surfaces)]
        s = tk.Sentence(" ".join(surfaces), words=words, dialect=dialect("BrazilianPortuguese"))
        return [s.ipa, features_json(s.features), [word_props(w) for w in s.words]]
    return ["manual", spec, surfaces, common.call(run)]


def grapheme_record(surface, grapheme_idx, syllable_idx):
    """A GraphemeToken with no parent word."""
    def run():
        g = tk.GraphemeToken(surface, grapheme_idx, syllable_idx)
        return [g.normalized, g.n_chars, g.prefix, g.suffix, opt_repr(g.prev_grapheme), opt_repr(g.next_grapheme),
                g.parent_syllable, common.call(lambda: g.is_vowel_grapheme), common.call(lambda: g.ipa),
                common.call(lambda: g.is_archaism), common.call(lambda: [char_props(c) for c in g.characters])]
    return ["grapheme", surface, grapheme_idx, syllable_idx, common.call(run)]


def char_record(surface):
    g = tk.GraphemeToken("a", 0, 0)
    return ["char", surface, common.call(lambda: char_props(tk.CharToken(surface, 0, g)))]


def functions(test_strings):
    out = [["PRONOUNCED_QU_GU_U", sorted(tk.PRONOUNCED_QU_GU_U)]]
    words = test_strings + [w.upper() for w in tk.PRONOUNCED_QU_GU_U] + [
        "aguentou", "aguentamos", "frequentes", "frequentemente", "linguistas", "pinguins", "tranquila",
        "tranquilos", "equinas", "", "AGUENT", "frequentar", "arguiu", "argui", "Ubiquidades", "cinquentas"]
    for w in common.unique(words):
        out.append(["pronounces_qu_gu_u", w, common.call(tk.pronounces_qu_gu_u, w)])
    contexts = [("h", "", "oje", "hoje"), ("u", "q", "ero", "quero"), ("u", "q", "ino", "equino"),
                ("u", "g", "ê", "aguê"), ("u", "Q", "Ero", "QUERO"), ("u", "", "e", "ue"), ("u", "q", "", "qu"),
                ("p", "m", "ção", "assumpção"), ("p", "m", "to", "peremptório"), ("p", "m", "ção", "sumpção"),
                ("p", "m", "tuoso", "sumptuoso"), ("p", "am", "ção", "assumpção"), ("H", "x", "", "x"),
                ("U", "ag", "ente", "aguentar"), ("u", "g", "íneo", "sanguíneo"), ("", "", "", ""),
                ("p", "m", "ç", "mpç"), ("u", "aq", "i", "aqui")]
    for spec in ("EuropeanPortuguese", "BrazilianPortuguese", "DialectInventory"):
        for g, before, after, word in contexts:
            out.append(["is_grapheme_silent", spec, g, before, after, word,
                        common.call(tk.is_grapheme_silent, g, before, after, word, dialect(spec))])
    sylls = [("café", ["ca", "fé"]), ("casa", ["ca", "sa"]), ("falar", ["fa", "lar"]), ("", []),
             ("a", ["a"]), ("xyz", ["x", "y", "z"]), ("rapidamente", ["ra", "pi", "da", "men", "te"]),
             ("órfão", ["ór", "fão"]), ("jacaré", ["ja", "ca", "ré"]), ("TÁXI", ["TÁ", "XI"]),
             ("amor", ["a", "mor"]), ("ímpar", ["ím", "par"]), ("bambu", ["bam", "bu"]), ("rubi", ["ru", "bi"]),
             ("câmara", ["câ", "ma", "ra"]), ("x", ["", ""]), ("mãe", ["mãe"]), ("órgãos", ["ór", "gãos"])]
    for spec in DIALECTS:
        for word, syl in sylls:
            out.append(["detect_stress_position", spec, word, syl,
                        common.call(tk.detect_stress_position, word, syl, dialect(spec))])
    for spec in DIALECTS:
        out.append(["demonstrate", spec, "O cão comeu o pão.", demonstrate("O cão comeu o pão.", spec)])
    return out


def demonstrate(text, spec):
    import contextlib
    import io
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        tk.demonstrate_transcription(text, dialect(spec))
    return buf.getvalue()


def corpora(repo):
    tests = [s for s in common.test_strings(repo) if len(s) <= 200]
    gold = common.unique(gold_words(repo))
    out = [("functions", functions(tests))]

    out.append(("tests_full", [sentence_record(spec, s, mode_for(s, 12)) for s in tests for spec in NATIONAL]))
    out.append(("tests_ipa", [sentence_record(spec, s, "ipa") for s in tests for spec in DIALECTS[5:]]))
    out.append(("gold", [sentence_record(spec, w, "ipa") for w in gold for spec in DIALECTS]))
    out.append(("gold_full", [sentence_record(spec, w, "full" if i % 4 == 0 else "props")
                              for i, w in enumerate(gold[::20]) for spec in NATIONAL]))
    out.append(("unicode", [sentence_record(spec, p, "full") for p in common.UNICODE_PROBES + [
        "a" + p + "a" for p in common.UNICODE_PROBES] for spec in NATIONAL[:2]]))
    alphabet = "abcdefghijlmnopqrstuvxzçãõáéíóúâêôàüèìòùẽĩũkwyAÉÇ -.,!?'1234ºª"
    out.append(("fuzz", [sentence_record(spec, s, mode_for(s, 6))
                         for s in common.fuzz_strings(20261103, 1500, alphabet, 14)
                         for spec in (NATIONAL[(len(s) + ord(s[0]) if s else 0) % 5],)]))

    custom = []
    shapes = [("carro", ["car", "ro"]), ("carro", ["ca", "rro"]), ("carro", ["c", "a", "r", "r", "o"]),
              ("pássaro", ["pás", "sa", "ro"]), ("bairro", ["bair", "ro"]), ("casa", ["casa", "", "x"]),
              ("guerra", ["guer", "ra"]), ("que", ["que"]), ("aquele", ["a", "que", "le"]), ("ab", ["", "ab"]),
              ("chamo", ["cha", "mo"]), ("me", ["me"]), ("rr", ["r", "r"]), ("ssff", ["s", "sf", "f"]),
              ("pé", ["p", "é", "z"]), ("Ação", ["A", "ção"]), ("muito", ["mui", "to"])]
    for spec in ("EuropeanPortuguese", "BrazilianPortuguese", "EuropeanPortuguese:xx-YY"):
        for surface, syl in shapes:
            for chunk in (None, 3):
                custom.append(custom_word_record(spec, surface, syl, chunk))
    for spec in NATIONAL:
        for surfaces in (["chamo", "me", "João"], ["dá", "lo", "já"], ["o", "o", "o"], ["pé", "de", "meia"]):
            custom.append(manual_sentence_record(spec, surfaces))
    for surface, gi, si in [("ch", 0, 0), ("ue", 1, 0), ("", 0, 0), ("ãe", 2, -1), ("İ", 0, 0), ("ph", 0, 0),
                            ("ü", 0, 0), ("que", 3, 1)]:
        custom.append(grapheme_record(surface, gi, si))
    for surface in ("a", "", "ab", "İ", " ", "ß", "😀"):
        custom.append(char_record(surface))
    # Diaeresis vowels are vowels only through NORMALIZED_VOWELS.
    umlaut = ["asüa", "osöa", "esäe", "isëo", "üsa", "ösar", "lingüiça", "agüentar", "Müller", "pÿsa", "tranqüilo",
              "freqüência", "rüa", "cïrco", "säo"]
    out.append(("constructed", custom + [sentence_record(spec, w, "full") for w in umlaut for spec in NATIONAL]))
    return out
