"""Goldens of tugaphone.lattice_core: the engine and its layers."""
import os
import subprocess
import tempfile

from orthography2ipa import G2P, clear_lexicons, register_lexicon
from tugaphone import accent
from tugaphone import lattice_core as lc
from tugaphone import registry

from . import common

NATIONAL = ["pt-PT", "pt-BR", "pt-AO", "pt-MZ", "pt-TL", "pt-PT-x-porto", "pt-UY"]


def phon(text, lang, contact="none"):
    return ["phonemize", text, lang, contact, common.call(lc.phonemize, text, lang, contact)]


def gold_rows(repo):
    """(word, lect) for each row of the gold TSVs, the lect named by the file."""
    names = subprocess.run(["git", "-C", repo, "ls-tree", "-r", "--name-only", common.TAG, "tests/data/gold"],
                           check=True, capture_output=True, text=True).stdout.split()
    rows = []
    for name in sorted(names):
        lang = name.rsplit("/", 1)[1][:-len(".tsv")]
        text = subprocess.run(["git", "-C", repo, "show", f"{common.TAG}:{name}"], check=True,
                              capture_output=True, text=True).stdout
        for line in text.splitlines():
            if line and not line.startswith("#"):
                rows.append((line.split("\t")[0], lang))
    return rows


def fake_contact(token, lang):
    """The contact transcriber of the runs corpus; the C++ test has the same one."""
    return "" if token.startswith("x") else f"<{lang}:{token}>"


def runs_record(lect, runs):
    saved = lc.split_runs, lc.transcribe_contact
    lc.split_runs = lambda text, contact, default_side: [tuple(r) for r in runs]
    lc.transcribe_contact = fake_contact
    try:
        return ["runs", lect, runs, common.call(lc.phonemize, "ignored", lect, "en")]
    finally:
        lc.split_runs, lc.transcribe_contact = saved


def engine_lattice(text, lect):
    def run():
        return [[w.surface, w.ipa] for w in lc.engine(lect).sentence_lattice(text).words]
    return ["engine_lattice", text, lect, common.call(run)]


def engine_features(text, lect):
    def run():
        return [[f.word, [[g.grapheme, g.top1_ipa] for g in f.graphemes]] for f in lc.engine(lect).features(text)]
    return ["engine_features", text, lect, common.call(run)]


def lexicon_words():
    """Each word of the packaged lexicons, sorted."""
    return sorted({w for lect in sorted(registry.PACKAGED_LEXICON_LECTS) for w in lc.engine(lect).pipeline.lexicon})


def process_lexicon(lect, word):
    """A lexicon registered for the process reaches a plain G2P and not the engine."""
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, f"{lect}.tsv")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(f"{word}\tˈzzz\n")
        register_lexicon(lect, path)
        lc.clear_caches()
        try:
            return ["process_lexicon", lect, word, [G2P(lect).transcribe_word(word), lc.phonemize(word, lect)]]
        finally:
            clear_lexicons()
            lc.clear_caches()


def corpora(repo):
    lects = registry.list_dialects()
    rows = gold_rows(repo)
    gold = common.unique(w for w, _ in rows)
    tests = [s for s in common.test_strings(repo) if len(s) <= 200]
    out = [("constants", [["VALID_CONTACT", list(lc._VALID_CONTACT)]])]
    out.append(("gold", [phon(w, lect) for lect in lects for w in gold]))
    out.append(("gold_rows", [phon(w, lang) for w, lang in rows]))
    out.append(("tests", [phon(s, lang) for s in tests for lang in NATIONAL]))
    langs = ["", "pt", "PT-pt", "pt-pt-x-azores", "pt-br-x-rio", "pt-PT-x-unknown", "en-US", "pt_BR", "xx",
             "pt-BR-x-sao-paulo", "pt-pt-x-norte", "ext-PT-x-barrancos", "roa-x-galaicopt", "pt-PT-x-medieval"]
    texts = ["Tenho 23 anos, Sr. Silva; li o Cap. XII.", "O gato dorme.", "dois irmãos", "menstruação e osos",
             "Vou pôr a manteiga no frigorífico.", "O comboio chegou à estação às 16:30."]
    out.append(("langs", [phon(t, lang) for t in texts for lang in langs]))
    contacts = ["none", "", "x", "NONE", " none", "None", "auto ", "EN", "de"]
    out.append(("contact", [phon(t, "pt-PT", c) for t in texts[:2] for c in contacts]))
    mixed = ["Fiz o download do software ontem.", "O croissant estava délicieux, merci.",
             "Vamos à fiesta con los amigos.", "the show must go on", "Ele ouve rock'n'roll e jazz.",
             "Gosto de hambúrguer e de pizza.", "O weekend foi cool!", "dois irmãos no shopping",
             "Ça va? Tudo bem.", "", "x y z", "O e-mail do CEO chegou às 9h."]
    out.append(("contact_modes", [phon(t, lang, c) for t in mixed + tests[:60]
                                  for lang in ("pt-PT", "pt-BR", "pt-UY", "pt-AO")
                                  for c in ("auto", "es", "fr", "en")]))
    run_sets = [
        [[None, "o"], [None, "gato"]],
        [[None, "o"], ["en", "computer"], [None, "dorme"]],
        [["en", "hello"], ["fr", "bonjour"]],
        [["es", "xhola"], [None, "dois"], [None, "irmãos"], ["en", "xx"]],
        [],
        [[None, ""], [None, ""]],
        [[None, "as"], [None, "aves"], ["en", "show"], [None, "os"], [None, "homens"]],
        [["en", "x"]],
    ]
    words = gold[::300]
    out.append(("accent_default",
                [["respell_word", w, lect, common.call(accent.respell_word, w, lect)]
                 for w in words for lect in ("pt-BR", "pt-PT-x-porto")] +
                [["force_accent", s, lect, mode, common.call(accent.force_accent, s, lect, mode)]
                 for s in texts + mixed[:4] for lect in ("pt-BR", "pt-AO", "pt-PT-x-acores")
                 for mode in ("ipa", "respell")]))
    out.append(("runs", [runs_record(lect, r) for lect in ("pt-PT", "pt-BR", "pt-UY") for r in run_sets]))
    # A numeral next to a heterophonic homograph: the numbers stage must run
    # before the homograph marks, or the marks read the digits' neighbours.
    numeral_homographs = [
        "Vi 3 colheres e 1 colher.", "2 jogos e o 1º jogo", "eu jogo 2 vezes", "a sede 5 e o governo 3",
        "O 1º olho e o 2º olho.", "Comprei 1 colher e 2 garfos.", "Fiz 1 acordo com 3 pessoas.",
        "O jogo 1 e o jogo 2.", "Há 23 jogos e 1 almoço.", "Eu molho 2 pães no molho.",
        "O 3º gosto é o melhor.", "Às 9h o 1º jogo; às 10h o 2º.", "1 jogo", "jogo 1", "o 2º sede",
        "Ele almoça às 13h e eu almoço às 12h.",
    ]
    out.append(("numeral_homographs",
                [phon(s, lect) for s in numeral_homographs for lect in lects] +
                [phon(s, lang, "auto") for s in numeral_homographs for lang in ("pt-PT", "pt-BR")]))
    engine_texts = texts + numeral_homographs + [
        "Eu jogo o jogo.", "O Sr. Silva", "Tenho 23 anos e o jogo.", "A Dra. Ana tem 2 filhos.",
        "O jogo começa às 16:30.", "No séc. XX havia 1000 jogos.", "", "  ", "olho, molho e jogo",
    ]
    out.append(("engine", [rec(t, lect) for t in engine_texts for lect in NATIONAL
                           for rec in (engine_lattice, engine_features)]))
    out.append(("expander", [["expand", s, lect, common.call(lc._expander(lect), s)]
                             for s in tests + texts + numeral_homographs for lect in ("pt-PT", "pt-BR", "pt-AO")]))
    # Forced words: expand rewrites each plain run alone, and mark reads the
    # whole sentence, with each forced word as spelled.
    markup = [
        '<phoneme ph="ˈʒoɡu">jogo</phoneme> o jogo 2 vezes.',
        'Eu <phoneme ph="ˈʒɔɡu">jogo</phoneme> e tu jogas.',
        'O <phoneme ph="ˈmɔʎu">molho</phoneme> e o molho.',
        'Tenho <phoneme ph="ˈviɲtɨ">23</phoneme> anos e 23 jogos.',
        'O Sr. <phoneme ph="ˈsiɫvɐ">Silva</phoneme> leu o Cap. XII.',
        'A sede <phoneme ph="ˈsedɨ">sede</phoneme> sede.',
        '<phoneme ph="u">o</phoneme> <phoneme ph="ˈɡatu">gato</phoneme>',
        'Às 9h o <phoneme ph="ˈʒoɡu">jogo</phoneme> do 1º jogo.',
        '2 <phoneme ph="ʒɔɡuʃ">jogos</phoneme>',
        'colher <phoneme ph="ʎ">x</phoneme> colher',
    ]
    out.append(("markup", [phon(s, lect) for s in markup for lect in NATIONAL] +
                [rec(s, lect) for s in markup for lect in ("pt-PT", "pt-BR")
                 for rec in (engine_lattice, engine_features)]))
    packaged = lexicon_words()
    out.append(("engine_lexicon", [["engine_lexicon", lect, sorted(map(list, lc.engine(lect).pipeline.lexicon.items()))]
                                   for lect in lects]))
    out.append(("lexicon_words", [phon(w, lect) for w in packaged for lect in NATIONAL + ["pt-PT-x-lisbon", "pt-BR-x-rj"]] +
                [phon(f"o {w} e a {w}.", lect) for w in packaged for lect in ("pt-PT", "pt-BR")]))
    out.append(("plain_engine", [["plain_engine", lect, w, common.call(G2P(lect).transcribe_word, w)]
                                 for w in packaged for lect in ("pt-PT", "pt-BR")]))
    out.append(("process_lexicon", [process_lexicon(lect, w) for lect in ("pt-PT", "pt-BR", "pt-AO", "pt-PT-x-porto")
                                    for w in ("gato", "basebol", "táxi")]))
    out.append(("validate_contact", [["validate_contact", c, common.call(lc._validate_contact, c)]
                                     for c in contacts + ["auto", "es", "fr", "en"]]))
    alphabet = "abcdefghijlmnopqrstuvxzçãõáéíóúâêôàü AEOÇ-.,!?;:'123º%€"
    fuzz = common.fuzz_strings(20261104, 1500, alphabet, 24)
    out.append(("fuzz", [phon(s, lects[i % len(lects)]) for i, s in enumerate(fuzz)]))
    out.append(("unicode", [phon(p, lang) for p in common.UNICODE_PROBES for lang in ("pt-PT", "pt-BR")]))
    return out
