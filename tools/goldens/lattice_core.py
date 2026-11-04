"""Goldens of tugaphone.lattice_core: the engine and its layers.

Run it with TUGAPHONE_LEXICON_DIR pointing at a scratch directory, so the
lexicon TSVs are not written to the user's cache.
"""
import os
import random
import subprocess
import sys
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


def engine_lattice(text, lect):
    def run():
        return [[w.surface, w.ipa] for w in lc.engine(lect).sentence_lattice(text).words]
    return ["engine_lattice", text, lect, common.call(run)]


def engine_features(text, lect):
    def run():
        return [[f.word, [[g.grapheme, g.top1_ipa] for g in f.graphemes]] for f in lc.engine(lect).features(text)]
    return ["engine_features", text, lect, common.call(run)]


CACHE_ENVS = [
    {"HOME": ""},
    {"HOME": "/"},
    {"HOME": "/home/x//"},
    {"HOME": "/home/x", "XDG_CACHE_HOME": ""},
    {"HOME": "/home/x", "XDG_CACHE_HOME": "/c/"},
    {"HOME": "/home/x", "XDG_CACHE_HOME": "rel//c"},
    {"HOME": "/home/x", "TUGAPHONE_LEXICON_DIR": ""},
    {"HOME": "/home/x", "TUGAPHONE_LEXICON_DIR": "/a//b/./c/"},
    {"HOME": "/home/x", "TUGAPHONE_LEXICON_DIR": "~/lex"},
    {"HOME": "/home/x", "TUGAPHONE_LEXICON_DIR": "//srv/lex"},
    {"HOME": "/home/x", "TUGAPHONE_LEXICON_DIR": "///srv/lex"},
    {"HOME": "/home/x", "TUGAPHONE_LEXICON_DIR": "a/../b"},
]


def cache_dir_records():
    """_LEXICON_CACHE as a fresh interpreter computes it under each environment."""
    out = []
    for env in CACHE_ENVS:
        full = {k: v for k, v in os.environ.items()
                if k not in ("HOME", "XDG_CACHE_HOME", "TUGAPHONE_LEXICON_DIR")}
        full.update(env)
        value = subprocess.run([sys.executable, "-I", "-c",
                                "from tugaphone import lattice_core as lc; print(lc._LEXICON_CACHE, end='')"],
                               env=full, check=True, capture_output=True, text=True).stdout
        out.append(["lexicon_cache_dir", env, value])
    return out


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
        out.append(["write_lexicon_error", "xyz", common.call(lc._write_lexicon, "xyz", lc.Path(tmp) / "xyz.tsv")])
        blocker = lc.Path(tmp) / "file"
        blocker.write_text("x", encoding="utf-8")
        out.append(["write_lexicon_error", "under_file",
                    common.call(lc._write_lexicon, "lbx", blocker / "sub" / "x.tsv")])
        stamp_dir = lc.Path(tmp) / "stamp_dir"
        stamp_dir.mkdir()
        (stamp_dir / "x.tsv").write_bytes(b"a\tb\n")
        (stamp_dir / "x.version").mkdir()
        out.append(["is_stale", "stamp_is_dir", lc._is_stale(stamp_dir / "x.tsv")])
        out.append(["write_lexicon_error", "stamp_is_dir",
                    common.call(lc._write_lexicon, "lbx", stamp_dir / "x.tsv")])
    return out


def stamp_bytes_records():
    """_is_stale on stamps given as bytes, most of them not UTF-8.

    The record holds the stamp as hex. A decode error is recorded with its
    message, which names the byte range and the reason.
    """
    version = lc._tugalex_version().encode()
    stamps = [version, b"\xef\xbb\xbf" + version, b"0", b"", b"\x00" + version, version + b"\x80",
              version + b"\n\xff\n", b" \xa0" + version, version[:3] + b"\xe4" + version[3:],
              b"\xff", b"\x80", b"\xbf", b"\xc0\xaf", b"\xc1\xbf", b"\xc2", b"\xc2\x41", b"\xdf\xc0",
              b"\xe0\x80\x80", b"\xe0\x9f\xbf", b"\xe0\xa0", b"\xe0\xa0\x41", b"\xe1\x80\xc0",
              b"\xed\x9f\xbf", b"\xed\xa0\x80", b"\xef\xbf", b"\xf0\x80\x80\x80", b"\xf0\x8f\xbf\xbf",
              b"\xf0\x90", b"\xf0\x90\x80", b"\xf0\x90\x80\x41", b"\xf0\x90\x41", b"\xf4\x8f\xbf",
              b"\xf4\x90\x80\x80", b"\xf5\x80\x80\x80", b"\xf8\x88\x80\x80\x80", b"\xfe\xff"]
    pool = [0x00, 0x30, 0x41, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xC1, 0xC2, 0xDF, 0xE0, 0xE1,
            0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF3, 0xF4, 0xF5, 0xFF]
    rng = random.Random(20261103)
    stamps += [bytes(rng.choice(pool) for _ in range(rng.randint(1, 8))) for _ in range(300)]
    out = []
    with tempfile.TemporaryDirectory() as tmp:
        path = lc.Path(tmp) / "x.tsv"
        path.write_bytes(b"a\tb\n")
        for stamp in stamps:
            lc._stamp_path(path).write_bytes(stamp)
            try:
                result = lc._is_stale(path)
            except UnicodeDecodeError as exc:
                result = {"err": type(exc).__name__, "msg": str(exc)}
            out.append(["is_stale_bytes", stamp.hex(), result])
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
    norm = lc._normalizer
    out.append(("normalizer", [["normalize", s, lect, common.call(norm(lect), s)]
                               for s in tests + texts + numeral_homographs for lect in ("pt-PT", "pt-BR", "pt-AO")]))
    out.append(("relayout", [["relayout", s, lc._relayout(s)] for s in relayout_inputs()]))
    paths = ["pt-PT.tsv", "/a/b/pt-PT.tsv", "x", "/a.b/c", ".hidden", "a.", "dir/a.b.c", "", "/", "a/.tsv",
             "a.tsv.tmp", "pt-PT-x-lisbon.tsv", "a/b.tsv/", "a/", "a/b.c/", ".", "./x.tsv", "a//b.tsv",
             "a/./b.tsv", "//a/b.tsv", "///a/b.tsv", "a/../b.tsv", "..", "a/..", "./", "a/b/.", "~/x.tsv"]
    out.append(("stamp_path", [["stamp_path", p, common.call(lambda: str(lc._stamp_path(lc.Path(p))))]
                               for p in paths]))
    out.append(("lexicon", lexicon_records()))
    out.append(("stamp_bytes", stamp_bytes_records()))
    out.append(("lexicon_cache_dir", cache_dir_records()))
    out.append(("validate_contact", [["validate_contact", c, common.call(lc._validate_contact, c)]
                                     for c in contacts + ["auto", "es", "fr", "en"]]))
    alphabet = "abcdefghijlmnopqrstuvxzçãõáéíóúâêôàü AEOÇ-.,!?;:'123º%€"
    fuzz = common.fuzz_strings(20261104, 1500, alphabet, 24)
    out.append(("fuzz", [phon(s, lects[i % len(lects)]) for i, s in enumerate(fuzz)]))
    out.append(("unicode", [phon(p, lang) for p in common.UNICODE_PROBES for lang in ("pt-PT", "pt-BR")]))
    return out
