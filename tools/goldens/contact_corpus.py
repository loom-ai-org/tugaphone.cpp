"""Corpora shared by the langdetect, codeswitch and accent dumpers.

The words come from the reference tests, the gold TSVs, the keep-list, the
loanword lexicon, the contact stopword lists and the four model
vocabularies. The mixed sentences interleave Portuguese gold words with
contact words, seeded so every run builds the same set.
"""
import random
import subprocess

from tugaphone import codeswitch, langdetect

from . import common


def gold_rows(repo):
    """[(lect, word)] for every data row of tests/data/gold/*.tsv at the tag."""
    names = subprocess.run(["git", "-C", repo, "ls-tree", "--name-only", common.TAG, "tests/data/gold/"],
                           check=True, capture_output=True, text=True).stdout.split()
    rows = []
    for name in sorted(names):
        lect = name.rsplit("/", 1)[1][:-len(".tsv")]
        text = subprocess.run(["git", "-C", repo, "show", f"{common.TAG}:{name}"], check=True,
                              capture_output=True, text=True).stdout
        for line in text.splitlines():
            if line and not line.startswith("#"):
                rows.append((lect, line.split("\t")[0]))
    return rows


def vocab_tokens():
    """The tokens of the four bundled models, in model order."""
    det = langdetect.get_detector()
    out = []
    for mc in det._models.values():
        out.extend(mc.vocab.id2tok)
    return common.unique(out)


def contact_words():
    words = sorted(codeswitch._loanwords())
    words += sorted(codeswitch._SPANISH_STOPWORDS) + sorted(codeswitch._FRENCH_STOPWORDS)
    words += sorted(codeswitch._ENGLISH_STOPWORDS)
    words += ["mañana", "crème", "naïve", "york", "playback", "wi-fi", "plaza", "libro", "general", "hotel",
              "radio", "menu", "theater", "whisky", "Œuvre", "façade", "jalapeño", "smoothie", "showroom",
              "über", "señor", "garçon", "rendez-vous", "déjà", "Ångström"]
    return common.unique(words)


# Words with an apostrophe: English contractions and possessives, French
# elisions and Portuguese compounds, with the ASCII and the U+2019 form.
APOSTROPHE_BASE = [
    "don't", "it's", "can't", "won't", "isn't", "I'm", "we're", "you'll", "she'd", "they've", "let's", "that's",
    "what's", "there's", "couldn't", "wouldn't", "shouldn't", "doesn't", "didn't", "haven't", "hasn't", "aren't",
    "wasn't", "weren't", "he's", "I've", "I'd", "we'll", "they're", "you're", "y'all", "'tis", "o'clock",
    "rock'n'roll", "McDonald's", "O'Neill", "Levi's", "Macy's", "children's", "people's", "rock'", "'90s", "Shaq'",
    "ab''cd", "'", "''", "a'b", "rock`n`roll", "l'homme", "aujourd'hui", "c'est", "qu'il", "j'ai", "d'Artagnan",
    "l'été", "n'est", "s'il", "jusqu'à", "presqu'île", "d'água", "copo-d'água", "gota-d'água", "pau-d'arco",
    "Sant'Ana",
]


def apostrophe_words():
    out = []
    for w in APOSTROPHE_BASE:
        out += [w, w.replace("'", "\u2019"), w.upper(), w.capitalize()]
    return common.unique(out)


def apostrophe_sentences():
    return ["I don't know", "o show do McDonald's", "it's rock'n'roll baby", "c'est la vie", "às 5 o'clock",
            "eles can't stop", "don\u2019t stop", "o copo-d'água e o pau-d'arco", "O'Neill e McDonald's",
            "we're live no Porto", "the people's choice award", "l'homme qu'il aime"]


def keep_variants():
    keep = sorted(langdetect.PORTUGUESE_KEEP)
    out = []
    for w in keep:
        out += [w, langdetect.fold_diacritics(w), w.upper(), w.capitalize(), w + ".", "«" + w + "»"]
    return common.unique(out)


def words(repo, gold=True):
    """Single tokens for the word-level functions."""
    tests = [s for s in common.test_strings(repo) if s and len(s) <= 30 and " " not in s]
    out = tests + keep_variants() + contact_words()
    if gold:
        out += [w for _, w in gold_rows(repo)]
    out += vocab_tokens()
    out += [p for p in common.UNICODE_PROBES]
    alphabet = "aeiouãõáéíóúâêôàçbcdfghjklmnpqrstvwxyzñèëïîûùœæ-'.ΣσςİıẞßĶ́̃"
    out += common.fuzz_strings(303, 1500, alphabet, 12)
    vocab = vocab_tokens()
    rng = random.Random(304)
    out += ["".join(rng.choice(vocab[1:]) for _ in range(rng.randint(1, 8))) for _ in range(800)]
    return common.unique(out)


def sentences(repo):
    """Mixed-language sentences: test sentences and seeded mixes."""
    tests = [s for s in common.test_strings(repo) if " " in s and len(s) <= 120]
    pt = sorted({w for _, w in gold_rows(repo)})
    contact = contact_words()
    keep = sorted(langdetect.PORTUGUESE_KEEP)
    rng = random.Random(305)
    mixes = []
    for _ in range(1200):
        parts = []
        for _ in range(rng.randint(1, 9)):
            pool = rng.choice((pt, contact, keep, keep))
            word = rng.choice(pool)
            if rng.random() < 0.15:
                word = word.upper() if rng.random() < 0.5 else word.capitalize()
            if rng.random() < 0.1:
                word += rng.choice([".", ",", "!", "?", ";", ":", "…"])
            parts.append(word)
        sep = rng.choice([" ", " ", " ", "  ", "\t", " ", " ", "\n"])
        mixes.append(sep.join(parts))
    return common.unique(tests + mixes + ["", " ", "\t\n"])
