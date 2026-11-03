"""Goldens of tugaphone.codeswitch: the orthographic heuristic, the
nativization tables, the per-token routing under both classifiers (the Markov
detector and the orthographic fallback), split_runs over mixed-language
sentences, and the contact transcription through orthography2ipa."""
import contextlib
import random

import tugaphone.langdetect as langdetect
from tugaphone import codeswitch

from . import common, contact_corpus

SIDES = ["en", "es", "fr", "pt", "xx"]
CONTACTS = ["none", "auto", "es", "fr", "en", "xx"]


class Fallback:
    """Route through the orthographic fallback, as the tests' fixture does."""

    def __enter__(self):
        self.saved = langdetect.get_detector
        langdetect.get_detector = lambda: None

    def __exit__(self, *exc):
        langdetect.get_detector = self.saved


def ipa_strings():
    keys = []
    for table in (codeswitch._NATIVIZE_ROMANCE, codeswitch._NATIVIZE_ENGLISH):
        keys += list(table) + list(table.values())
    keys += list(codeswitch._DROP) + ["a", "e", "i", "o", "u", "p", "t", "k", "s", "z", "ʃ", "ʒ", "̃", "ˈ",
                                      "j", "w", "dʒ", "tʃ", " ", "-"]
    rng = random.Random(911)
    fuzz = ["".join(rng.choice(keys) for _ in range(rng.randint(0, 10))) for _ in range(3000)]
    return common.unique(keys + fuzz + common.UNICODE_PROBES)


def corpora(repo):
    words = contact_corpus.words(repo)
    sentences = contact_corpus.sentences(repo)
    consts = [["loanwords", sorted(codeswitch._loanwords())]]
    heuristic = []
    for w in words:
        core = codeswitch._strip(w)
        heuristic.append(["token", w, core, codeswitch.is_known_loanword(w), codeswitch.is_contact_word(w),
                          codeswitch._has_english_signal(w), codeswitch._has_english_signal(core)])
        for side in SIDES:
            heuristic.append(["contact_language", w, side, codeswitch.contact_language(w, side)])
    nativize = []
    for ipa in ipa_strings():
        nativize.append(["nativize", ipa, False, codeswitch._nativize(ipa)])
        nativize.append(["nativize", ipa, True, codeswitch._nativize(ipa, english=True)])
    routing = []
    for path in ("detector", "fallback"):
        ctx = Fallback() if path == "fallback" else contextlib.nullcontext()
        with ctx:
            for w in words[::2]:
                for side in SIDES[:3]:
                    routing.append(["classify", w, side, path, codeswitch._classify(w, side)])
                routing.append(["is_contact", w, path, codeswitch._is_contact(w)])
    runs = []
    for path in ("detector", "fallback"):
        ctx = Fallback() if path == "fallback" else contextlib.nullcontext()
        with ctx:
            for i, s in enumerate(sentences):
                contacts = CONTACTS if i % 4 == 0 else ["auto", CONTACTS[i % len(CONTACTS)]]
                for contact in common.unique(contacts):
                    side = SIDES[i % 3]
                    runs.append(["split_runs", s, contact, side, path,
                                 [list(r) for r in codeswitch.split_runs(s, contact, default_side=side)]])
    transcribe = []
    contact_words = contact_corpus.contact_words()
    tests = [s for s in common.test_strings(repo) if s and " " not in s and len(s) <= 20]
    for w in common.unique(contact_words + tests[:150]):
        for contact in ("es", "fr", "en"):
            transcribe.append(["transcribe_contact", w, contact, common.call(codeswitch.transcribe_contact, w, contact)])
    transcribe.append(["transcribe_contact", "hello", "xx", common.call(codeswitch.transcribe_contact, "hello", "xx")])
    apostrophe = []
    for w in contact_corpus.apostrophe_words():
        for contact in ("es", "fr", "en"):
            apostrophe.append(["transcribe_contact", w, contact, common.call(codeswitch.transcribe_contact, w, contact)])
        for path in ("detector", "fallback"):
            ctx = Fallback() if path == "fallback" else contextlib.nullcontext()
            with ctx:
                apostrophe.append(["classify", w, "en", path, codeswitch._classify(w, "en")])
    # split_runs as callers reach it: the bundled detector, chosen inside split_runs.
    public = []
    for s in contact_corpus.apostrophe_sentences() + sentences[:200]:
        for contact in CONTACTS:
            public.append(["split_runs_public", s, contact, "en",
                           [list(r) for r in codeswitch.split_runs(s, contact, default_side="en")]])
    return [("constants", consts), ("heuristic", heuristic), ("nativize", nativize), ("routing", routing),
            ("split_runs", runs), ("transcribe_contact", transcribe), ("apostrophe", apostrophe),
            ("split_runs_public", public)]
