"""Goldens of the tugaphone package: TugaPhonemizer and the top-level re-exports.

A phonemize_sentence record holds one text and one lect with the output of
each contact mode, in the order of MODES.
"""
import json
import warnings

import tugaphone
from tugaphone import TugaPhonemizer
from tugaphone import accent, lattice_core, registry

from . import abbreviation_corpus, common
from .lattice_core import gold_rows

MODES = ("none", "en", "es", "fr", "auto")
NATIONAL = ["pt-PT", "pt-BR", "pt-AO", "pt-MZ", "pt-TL", "pt-PT-x-porto", "pt-UY"]
ACCEPTANCE = "Tenho 23 anos, Sr. Silva; li o Cap. XII."
OVERLAY = json.dumps({"name": "tweaks", "transforms": [
    {"kind": "regex", "pattern": "ʃ$", "replacement": "s", "stage": "ipa"},
    {"kind": "word", "pattern": "vinho", "replacement": "binho", "stage": "text"}]})


def sentence(ph, text, lang):
    return ["phonemize_sentence", text, lang, [common.call(ph.phonemize_sentence, text, lang, m) for m in MODES]]


def check_reexports():
    """The names __init__ re-exports are the module objects themselves."""
    assert tugaphone.phonemize is lattice_core.phonemize
    assert tugaphone.list_dialects is registry.list_dialects
    assert tugaphone.resolve_lect is registry.resolve_lect
    for name in ("force_accent", "respell", "respell_word", "AccentOverlay", "Transform", "RespellRule",
                 "DEFAULT_RESPELL_RULES"):
        assert getattr(tugaphone, name) is getattr(accent, name), name


def regional(ph, text, lang, contact, value):
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", DeprecationWarning)
        out = common.call(ph.phonemize_sentence, text, lang, contact, value)
    return ["regional_dialect", text, lang, contact, value, out]


def corpora(repo):
    check_reexports()
    ph = TugaPhonemizer()
    lects = registry.list_dialects()
    rows = gold_rows(repo)
    gold = common.unique(w for w, _ in rows)
    tests = [s for s in common.test_strings(repo) if len(s) <= 200]
    out = [("constants", [
        ["__version__", tugaphone.__version__],
        ["list_dialects", tugaphone.list_dialects()],
        ["DEFAULT_RESPELL_RULES", [[r.name, r.pattern, r.replacement, r.note] for r in tugaphone.DEFAULT_RESPELL_RULES]],
    ])]
    out.append(("acceptance", [sentence(ph, ACCEPTANCE, lang) for lang in lects]))
    out.append(("gold", [sentence(ph, w, lect) for lect in lects for w in gold]))
    out.append(("gold_rows", [sentence(ph, w, lang) for w, lang in rows]))
    out.append(("tests", [sentence(ph, s, lang) for s in tests for lang in NATIONAL]))
    out.append(("abbreviations", [sentence(ph, s, lang) for s in abbreviation_corpus.sentences(repo)
                                  for lang in NATIONAL]))
    codes = ["", "pt", "PT-pt", "pt-pt-x-azores", "pt-br-x-rio", "pt-PT-x-unknown", "en-US", "pt_BR", "xx",
             "ext-PT-x-barrancos", "pt-BR-x-sp", "PT_br", " pt-BR ", "pt-PT-x-porto"] + lects
    out.append(("supported", [["is_supported", c, common.call(TugaPhonemizer.is_supported, c)] for c in codes] +
                [["resolve_lect", c, common.call(tugaphone.resolve_lect, c)] for c in codes]))
    out.append(("contact", [["phonemize", ACCEPTANCE, lang, c, common.call(tugaphone.phonemize, ACCEPTANCE, lang, c)]
                            for lang in ("pt-PT", "pt-BR") for c in ("none", "", "x", "NONE", "auto ", "de", "en")]))
    out.append(("regional_dialect", [regional(ph, t, lang, c, value)
                                     for t in (ACCEPTANCE, "O gato dorme.")
                                     for lang in ("pt-PT", "pt-BR")
                                     for c in ("none", "auto")
                                     for value in (None, "", "pt-PT-x-porto", "lisbon")]))
    texts = [ACCEPTANCE, "o vinho verde", "a tia tinha dentes", "O gato dorme.", "dois irmãos", ""]
    out.append(("force_accent", [
        ["force_accent", t, lect, mode, base, overlay,
         common.call(TugaPhonemizer.force_accent, t, lect, mode, base,
                     None if overlay is None else tugaphone.AccentOverlay.from_json(overlay))]
        for t in texts for lect in ("pt-BR", "pt-PT-x-porto", "pt-AO") for mode in ("ipa", "respell", "IPA")
        for base in ("pt-PT", "pt-BR") for overlay in (None, OVERLAY)]))
    words = gold[::150]
    out.append(("respell", [["respell_word", w, lect, base, common.call(tugaphone.respell_word, w, lect, base)]
                            for w in words for lect in ("pt-BR", "pt-PT-x-porto") for base in ("pt-PT", "pt-BR")] +
                [["respell", t, lect, common.call(tugaphone.respell, t, lect)]
                 for t in texts for lect in ("pt-BR", "pt-PT-x-porto")]))
    return out
