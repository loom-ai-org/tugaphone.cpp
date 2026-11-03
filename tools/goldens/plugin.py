"""Goldens of tugaphone.plugin: TugaphoneG2PPlugin and SilabificadorSyllabifier.

The entry-point registration has no C++ counterpart; the records cover the
behaviour of both classes.
"""
from orthography2ipa import WordContext

from tugaphone.plugin import SilabificadorSyllabifier, TugaphoneG2PPlugin

from . import common
from .lattice_core import gold_rows
from .package import ACCEPTANCE, OVERLAY

CONFIGS = [("pt-PT", "none"), ("pt-BR", "auto"), ("pt-AO", "en"), ("pt-UY", "es"), ("pt-PT-x-porto", "fr"),
           ("pt-BR", "bad"), ("xx", "none"), ("", "auto")]
MIXED = ["Fiz o download do software ontem.", "O croissant estava délicieux, merci.", "Vamos à fiesta con los amigos.",
         "the show must go on", "Ele ouve rock'n'roll e jazz.", "O weekend foi cool!", "", "x y z"]


def word_record(lang, contact, word, context_lang, has_context):
    plugin = TugaphoneG2PPlugin(lang, contact)
    context = WordContext(lang=context_lang) if has_context else None
    return ["transcribe_word", lang, contact, word, has_context, context_lang,
            common.call(plugin.transcribe_word, word, context)]


def corpora(repo):
    from tugaphone.accent import AccentOverlay
    rows = gold_rows(repo)
    gold = common.unique(w for w, _ in rows)
    tests = [s for s in common.test_strings(repo) if len(s) <= 200]
    syl = SilabificadorSyllabifier()
    out = [("constants", [
        ["language_codes", TugaphoneG2PPlugin().language_codes],
        ["syllabifier_language_codes", syl.language_codes],
        ["syllabifier_priority", syl.priority],
        ["defaults", [TugaphoneG2PPlugin().lang, TugaphoneG2PPlugin().contact]],
    ])]
    out.append(("syllabify", [["syllabify", w, lang, common.call(syl.syllabify, w, lang)]
                              for w in gold + common.UNICODE_PROBES for lang in (None,)] +
                [["syllabify", w, lang, common.call(syl.syllabify, w, lang)]
                 for w in gold[::50] for lang in ("pt-BR", "", "xx")]))
    texts = [ACCEPTANCE] + MIXED + tests[::10]
    out.append(("transcribe", [["transcribe", lang, contact, t,
                                common.call(TugaphoneG2PPlugin(lang, contact).transcribe, t)]
                               for lang, contact in CONFIGS for t in texts]))
    words = gold[::40] + ["show", "download", "fiesta", "bonjour", ""]
    out.append(("transcribe_word", [word_record(lang, contact, w, ctx, has)
                                    for lang, contact in CONFIGS[:5] for w in words
                                    for has, ctx in ((False, None), (True, None), (True, ""), (True, "pt-BR"),
                                                     (True, "pt-MZ"))]))
    accent_texts = [ACCEPTANCE, "o vinho verde", "a tia tinha dentes"]
    out.append(("force_accent", [
        ["force_accent", lang, t, lect, mode, base, overlay,
         common.call(TugaphoneG2PPlugin(lang).force_accent, t, lect, mode, base,
                     None if overlay is None else AccentOverlay.from_json(overlay))]
        for lang in ("pt-PT", "pt-BR") for t in accent_texts for lect in ("pt-BR", "pt-PT-x-porto")
        for mode in ("ipa", "respell", "x") for base in (None, "", "pt-AO") for overlay in (None, OVERLAY)]))
    return out
