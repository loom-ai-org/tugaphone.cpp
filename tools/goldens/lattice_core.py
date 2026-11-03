"""Goldens of tugaphone.lattice_core: the engine and its layers.

Run it with TUGAPHONE_LEXICON_DIR pointing at a scratch directory, so the
lexicon TSVs are not written to the user's cache.
"""
import os
import subprocess
import tempfile
import zlib

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


def crc(data):
    return zlib.crc32(data)


def lexicon_records():
    out = []
    regions = sorted(set(registry._LEXICON_REGION.values()))
    with tempfile.TemporaryDirectory() as tmp:
        for region in regions:
            path = os.path.join(tmp, f"parity-{region}.tsv")
            lc._write_lexicon(region, lc.Path(path))
            data = open(path, "rb").read()
            stamp = open(lc._stamp_path(lc.Path(path)), "rb").read().decode()
            out.append(["write_lexicon", region, crc(data), len(data), data.count(b"\n"), stamp])
        scenarios = [("missing", None, None), ("empty", b"", None), ("no_stamp", b"a\tb\n", None),
                     ("stamp", b"a\tb\n", lc._tugalex_version()),
                     ("stamp_spaces", b"a\tb\n", " " + lc._tugalex_version() + "\n"),
                     ("stamp_other", b"a\tb\n", "0"), ("empty_stamped", b"", lc._tugalex_version())]
        for name, body, stamp in scenarios:
            path = lc.Path(tmp) / f"{name}.tsv"
            if body is not None:
                path.write_bytes(body)
            if stamp is not None:
                lc._stamp_path(path).write_text(stamp, encoding="utf-8")
            out.append(["is_stale", name, lc._is_stale(path)])
    return out


def relayout_inputs():
    lex = lc.LEXICON.get_ipa_map(region="lbx")
    words = sorted(lex)
    sample = [lex[w] for w in words[::97]]
    edges = ["", "·", "ˈ", "ˌ", "gˈa·tʊ", "ˈa·ˈb", "aˌb·cˈd", "a·b·c", "ˈˈa", "xˈyˌz", "··", "aˈ·ˌb", "ˈ·ˌ",
             "pɐ·ɾɐˈ·ðɐ"]
    return common.unique(sample + edges)


def corpora(repo):
    lects = registry.list_dialects()
    rows = gold_rows(repo)
    gold = common.unique(w for w, _ in rows)
    tests = [s for s in common.test_strings(repo) if len(s) <= 200]
    out = [("constants", [["tugalex_version", lc._tugalex_version()], ["VALID_CONTACT", list(lc._VALID_CONTACT)]])]
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
    norm = lc._normalizer
    out.append(("normalizer", [["normalize", s, lect, common.call(norm(lect), s)]
                               for s in tests + texts for lect in ("pt-PT", "pt-BR", "pt-AO")]))
    out.append(("relayout", [["relayout", s, lc._relayout(s)] for s in relayout_inputs()]))
    paths = ["pt-PT.tsv", "/a/b/pt-PT.tsv", "x", "/a.b/c", ".hidden", "a.", "dir/a.b.c", "", "/", "a/.tsv",
             "a.tsv.tmp", "pt-PT-x-lisbon.tsv"]
    out.append(("stamp_path", [["stamp_path", p, common.call(lambda: str(lc._stamp_path(lc.Path(p))))]
                               for p in paths if p]))
    out.append(("lexicon", lexicon_records()))
    out.append(("validate_contact", [["validate_contact", c, common.call(lc._validate_contact, c)]
                                     for c in contacts + ["auto", "es", "fr", "en"]]))
    alphabet = "abcdefghijlmnopqrstuvxzçãõáéíóúâêôàü AEOÇ-.,!?;:'123º%€"
    fuzz = common.fuzz_strings(20261104, 1500, alphabet, 24)
    out.append(("fuzz", [phon(s, lects[i % len(lects)]) for i, s in enumerate(fuzz)]))
    out.append(("unicode", [phon(p, lang) for p in common.UNICODE_PROBES for lang in ("pt-PT", "pt-BR")]))
    return out
