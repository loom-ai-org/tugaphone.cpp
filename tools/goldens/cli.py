"""Goldens of the tugaphone command line (python -m tugaphone).

Each "cli" record runs the reference interpreter on one argument list, with
the working directory at the root of this repository and COLUMNS set to a
value or unset, and holds standard output, standard error and the exit status.
tests/parity/cli.cpp runs the C++ executable on the same arguments and
compares the bytes. An argument that is not UTF-8 is written as
{"hex": "<bytes>"}. When Python ends with a traceback, the record says so: the
frames name Python source files, so the record holds the last line of the
traceback, and the C++ side must print that line alone. This repository reads
as "<root>" in that line. A UserWarning is recorded as its category and text
alone, for the same reason.

An optional last item of a "cli" record changes how the process runs:
"env" sets variables, or removes those set to null; "stdout" names a file to
write standard output to, such as /dev/full; "pty" runs standard output on a
pseudo-terminal of that many columns, in raw mode.

The other records run in the process and hold the last line Python prints for
an uncaught exception, or "" when there is none: "overlay_json" loads an
overlay from JSON text, "decode" decodes bytes as UTF-8, and
"transform_error" applies a regex transform. The patterns hold no complete
\\N{...} escape, whose name lookup the C++ engine does not port.
"""
import fcntl
import os
import pathlib
import pty
import random
import re
import struct
import subprocess
import sys
import termios
import traceback
import tty

from tugaphone.accent import AccentOverlay, Transform
from tugaphone.registry import list_dialects

from .package import ACCEPTANCE

ROOT = pathlib.Path(__file__).resolve().parents[2]

HELP = [[], ["-h"], ["--help"], ["--he"], ["-h", "list"], ["phonemize", "-h"], ["phonemize", "--help"],
        ["force-accent", "-h"], ["force-accent", "-h", "x"], ["list", "-h"], ["list", "--h"]]
ERRORS = [
    [ACCEPTANCE, "--lect", "pt-BR"], ["phonemize"], ["force-accent", "x"], ["--lect", "pt-BR", "phonemize", "x"],
    ["phonemize", "x", "--l", "pt-BR"], ["force-accent", "x", "--lect", "pt-BR", "--mode", "foo"], ["phonemize", "-x"],
    ["phon", "x"], ["phonemize", "x", "--lect"], ["-x"], ["--help=foo"], ["-hfoo"], ["-h=x"], ["list", "--x"],
    ["phonemize", "--lect=pt-BR"], ["force-accent", "x", "--lect"], ["list", "a", "b"], ["-"], ["-5"], ["--=x"],
    ["force-accent", "--mode", "respell"], ["force-accent", "x", "--lect", "pt-BR", "--mode=ipa", "--mod", "x"],
    ["force-accent", "x", "--lect", "pt-BR", "--overlay"], ["phonemize", "x", "y"], ["phonemize", "x", "--lang"],
    ["phonemize", ACCEPTANCE, "--lect", "pt-BR", "--contact", "en"], ["phonemize", "x", "--contact=auto"],
    ["phonemize", "x", "--", "y"], ["force-accent", "x", "--lect", "pt-BR", "--mode", "IPA"],
    ["force-accent", "x", "--lect", "pt-BR", "--mode", ""], ["phonemize", "x", "--lect", "a", "--lect"],
    ["phonemize", "x", "-l", "pt-BR"], ["phonemize", "x", "--lect", "pt-BR", "-hx"], ["list", "-hx"],
    ["force-accent", "x", "--lect", "pt-BR", "--base"], ["phonemize", "x", "---lect", "pt-BR"],
    ["phonemize", "x", "--lect", "--", "pt-BR"], ["phonemize", "x", "-h=1"], ["Phonemize", "x"],
    ["phonemize", "x y", "--lect", "-5"], ["phonemize", "x", "--lect", "-x"], ["force-accent", "x", "--l", "pt-BR"],
]
TEXTS = ["o gato dorme", "", "-5", "--", "-", "x y", "O comboio chegou à estação às 16:30.", "dois irmãos",
         "Ça va? Tudo bem.", "1234567", "-1.5", "-.5", "Vou pôr a manteiga no frigorífico."]
OVERLAYS = sorted(str(p.relative_to(ROOT)) for p in (ROOT / "tests" / "data" / "cli" / "overlays").iterdir())
RUNS = (
    [["phonemize", ACCEPTANCE, "--lect", lect] for lect in list_dialects()]
    + [["phonemize", ACCEPTANCE], ["phonemize", ACCEPTANCE, "--lang", "pt-BR"], ["phonemize", ACCEPTANCE, "--le=pt-AO"],
       ["phonemize", "--lect", "pt-BR", ACCEPTANCE], ["phonemize", "--lect=pt-BR", "--", ACCEPTANCE],
       ["phonemize", ACCEPTANCE, "--lect", "PT_br"], ["phonemize", ACCEPTANCE, "--lect", ""],
       ["phonemize", ACCEPTANCE, "--lect", "xx"], ["phonemize", ACCEPTANCE, "--lect", "pt-pt-x-azores"],
       ["phonemize", ACCEPTANCE, "--lect", "pt-BR", "--lect", "pt-AO"],
       ["phonemize", ACCEPTANCE, "--lect", "pt-BR-x-unknown"], ["phonemize", ACCEPTANCE, "--lect", "ext-PT-x-barranco"],
       ["phonemize", ACCEPTANCE, "--lect", "pt-PT-x-medieval"], ["phonemize", ACCEPTANCE, "--lect", "roa-x-galaico-pt"],
       ["phonemize", ACCEPTANCE, "--lect", "en-US"], ["phonemize", ACCEPTANCE, "--lect", "it's"],
       ["force-accent", "o vinho", "--lect", "xx"], ["force-accent", "o vinho", "--lect", "xx", "--base", "yy"],
       ["force-accent", "o vinho", "--lect", "xx", "--base", "xx"],
       ["force-accent", "o vinho", "--lect", "xx", "--base", "xx", "--mode", "respell"]]
    + [["phonemize", "--", t] for t in TEXTS] + [["phonemize", t, "--lect", "pt-BR"] for t in TEXTS if t[:1] != "-"]
    + [["force-accent", t, "--lect", lect, "--mode", mode] + base
       for t in ("o vinho verde", "a tia", ACCEPTANCE) for lect in ("pt-BR", "pt-PT-x-porto", "pt-AO")
       for mode in ("ipa", "respell") for base in ([], ["--base", "pt-BR"])]
    + [["force-accent", "o vinho verde", "--lect", "pt-PT-x-porto", "--mode", mode, "--overlay", path]
       for mode in ("ipa", "respell")
       for path in ["tests/data/cli/tweaks.json", "tests/data/cli/crlf.json", "", "tests/data/cli/missing.json",
                    "tests/data/cli/broken.json", "tests/data/cli/latin1.json", "tests/data/cli/bad-kind.json",
                    "tests/data/cli", "/dev/null"] + OVERLAYS]
    + [["force-accent", "o vinho", "--lang", "pt-BR", "--mo", "respell", "--ba", "pt-AO"],
       ["force-accent", "--mode=respell", "--lect=pt-BR", "--", "-o vinho"], ["list"], ["list", "--"]]
)
COLUMNS = [None, "40", "120", "20", "5", "1", "0", "abc", " 60 ", "-3", "1_00", "9223372036854775807",
           "9223372036854775808", "99999999999999999999", "-99999999999999999999"]

# Arguments that are not UTF-8: Python decodes them with surrogateescape.
BYTE_ARGS = [
    [b"\xff"], [b"phon\xe9mize", b"x"], [b"phonemize", b"\xe2\x82"], [b"phonemize", b"x", b"--l\xff"],
    [b"force-accent", b"x", b"--mode", b"ip\xc3"], [b"phonemize", b"o gato \xff", b"--lect", b"pt-BR"],
    [b"list", b"\xed\xa0\x80"], [b"phonemize", b"x", b"--lect", b"pt-BR", b"--\xf0\x90\x80"],
    [b"phonemize", b"x", b"--lect", b"pt-\xff"],
]
# Runs that fail outside argparse, and runs with a changed environment.
FAILURES = [
    (["phonemize", "<phoneme>jogo"], None), (["phonemize", "o gato</phoneme>"], None),
    (["force-accent", "<phoneme>jogo", "--lect", "pt-BR"], None),
    (["phonemize", "<phoneme>jogo", "--lect", "xx"], None),
    (["phonemize", "o gato"], {"env": {"TUGAPHONE_LEXICON_DIR": "/dev/null/lexicon"}}),
    (["phonemize", "o gato"], {"env": {"XDG_CACHE_HOME": None, "HOME": "/nonexistent-tugaphone"}}),
    (["list"], {"stdout": "/dev/full"}), (["phonemize", "o gato", "--lect", "pt-BR"], {"stdout": "/dev/full"}),
    (["-h"], {"stdout": "/dev/full"}), (["phon"], {"stdout": "/dev/full"}),
    (["-h"], {"pty": 50}), (["phonemize", "-h"], {"pty": 120}), (["force-accent", "--help"], {"pty": 33}),
    (["list"], {"pty": 70}),
]


# A warning names a Python source file and a source line, so the record holds
# its category and text, which is what the C++ side prints.
WARNING = re.compile(r"^.*:\d+: UserWarning: (.*)\n  .*\n", re.M)
WARNED = re.compile(r"^UserWarning: .*\n", re.M)


def local(line):
    return line.replace(str(ROOT), "<root>")


def run(args, columns, options=None):
    env = {k: v for k, v in os.environ.items() if k != "COLUMNS"}
    if columns is not None:
        env["COLUMNS"] = columns
    for name, value in ((options or {}).get("env") or {}).items():
        if value is None:
            env.pop(name, None)
        else:
            env[name] = value
    argv = [sys.executable, "-I", "-m", "tugaphone", *args]
    if options and "pty" in options:
        master, slave = pty.openpty()
        tty.setraw(slave)
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, options["pty"], 0, 0))
        p = subprocess.Popen(argv, stdout=slave, stderr=subprocess.PIPE, cwd=ROOT, env=env)
        os.close(slave)
        stdout = b""
        while True:
            try:
                chunk = os.read(master, 65536)
            except OSError:  # EIO once the terminal has no writer
                break
            if not chunk:
                break
            stdout += chunk
        os.close(master)
        stderr = p.stderr.read()
        p.stderr.close()
        returncode = p.wait()
    elif options and "stdout" in options:
        with open(options["stdout"], "wb") as sink:
            p = subprocess.run(argv, stdout=sink, stderr=subprocess.PIPE, cwd=ROOT, env=env)
        stdout, stderr, returncode = b"", p.stderr, p.returncode
    else:
        p = subprocess.run(argv, capture_output=True, cwd=ROOT, env=env)
        stdout, stderr, returncode = p.stdout, p.stderr, p.returncode
    out, err = stdout.decode("utf-8"), stderr.decode("utf-8")
    err = WARNING.sub(r"UserWarning: \1\n", err)
    traceback_at = err.find("Traceback (most recent call last):\n")
    traceback_seen = traceback_at >= 0
    if traceback_seen:  # the frames name Python files: keep the last line
        err = err[:traceback_at] + local(err.rstrip("\n").splitlines()[-1])
    if returncode == 0 and WARNED.sub("", err):
        raise SystemExit(f"python wrote to stderr on success, a warning that names a path: {args} {err!r}")
    record_args = [{"hex": a.hex()} if isinstance(a, bytes) and not is_utf8(a) else
                   a.decode("utf-8") if isinstance(a, bytes) else a for a in args]
    record = ["cli", record_args, columns, out, err, returncode, traceback_seen]
    return record + [options] if options else record


def is_utf8(b):
    try:
        b.decode("utf-8")
        return True
    except UnicodeDecodeError:
        return False


def last_line(fn, *args):
    try:
        fn(*args)
    except Exception as exc:  # the line an uncaught exception ends with
        return traceback.format_exception_only(exc)[-1].rstrip("\n")
    return ""


JSON_SEEDS = [
    '{"name": "tweaks", "transforms": [{"kind": "regex", "pattern": "ʎ", "replacement": "j", "stage": "ipa"}]}',
    '{"name":"t","transforms":[{"kind":"word","pattern":"\\u0076inho","replacement":"\\ud83d\\ude00","stage":"text"}]}',
    '[1, 2.5e3, -0, true, false, null, NaN, -Infinity, "a\\"b\\\\c\\/\\b\\f\\n\\r\\t"]',
    '{"a": {"b": [{}, []]}, "c": "é😀"}',
]
JSON_CASES = [
    "", " ", "\n\n  x", "﻿{}", " ﻿{}", "{", "}", "[", "]", "[1,]", "[,1]", "{,}", '{"a" 1}', '{"a":}',
    '{"a":1,}', '{"a":1 "b":2}', '{1:2}', "{'a':1}", '{"a":1}x', '{"a":1}\n\n x', "[1]\n,", "-", "-x", "--1", "01",
    "1.", "1.e5", "1e", "1e+", ".5", "+1", "0x10", "NaNx", "nul", "nulll", "tru", "fals", "Infinit", "-Inf",
    "-Infinity", "-Infinityx", '"abc', '"a\nb"', '"a\tb"', '"a\x00b"', '"a\x1fb"', '"\x7f"', '"\\x"', '"\\',
    '"\\"', '"\\u12"', '"\\u1234', '"\\u1234"', '"\\u12345', '"\\u12g4"', '"\\ud800"', '"\\ud800\\u0041"',
    '"\\ud800\\udc00"', '"\\ud800\\udc0"', '"\\ud800\\udc00', '"\\ud800\\uzzzz"', '"\\ud800\\', '"\\ud800\\x"',
    '"\\udc00\\ud800"', '"é\\q"', '"😀\\q"', "é", '"é" é', "\n[\n1\n,\n]", "[1\n2]", '{"a":\n\n}', '{"é":1,"😀"}',
    '[[[[]]]', '[[[[]]]]]', '{"transforms": 5}', '{"transforms": null}', '{"transforms": true}',
    '{"transforms": 2.5}', '{"transforms": "ab"}', '{"transforms": {"a": 1}}', '{"transforms": [1]}',
    '{"transforms": [null]}', '{"transforms": [[]]}', '{"transforms": [{"kind": 1}]}', '"x"', "1", "2.5", "true",
    "[]", "{}", '{"name": {"a": [1, 2]}, "transforms": []}', '{"name": [1, "a", null], "transforms": ""}',
]


def json_fuzz():
    rng = random.Random(4721)
    alphabet = list('{}[]",:\\u0123456789abcdefxyzNInlr-+.eE \t\n\r') + ["\x00", "\x1f", "é", "😀", "﻿"]
    out = []
    for seed in JSON_SEEDS:
        out += [seed[:i] for i in range(len(seed) + 1)]
        for _ in range(400):
            i = rng.randrange(len(seed) + 1)
            op = rng.randrange(3)
            c = rng.choice(alphabet)
            out.append(seed[:i] + c + seed[i:] if op == 0 else seed[:i] + c + seed[i + 1:] if op == 1
                       else seed[:i] + seed[i + 1:])
    return out


BYTE_ALPHABET = [0x41, 0x0A, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xC1, 0xC2, 0xDF, 0xE0, 0xE1, 0xED, 0xEF,
                 0xF0, 0xF4, 0xF5, 0xFF]


def byte_cases():
    out = [bytes([a]) for a in BYTE_ALPHABET] + [bytes([a, b]) for a in BYTE_ALPHABET for b in BYTE_ALPHABET]
    out += [bytes([a, b, c]) for a in BYTE_ALPHABET[8:] for b in BYTE_ALPHABET[2:8] for c in BYTE_ALPHABET]
    rng = random.Random(9055)
    out += [bytes(rng.choice(BYTE_ALPHABET) for _ in range(rng.randint(0, 9))) for _ in range(3000)]
    out += ["olá 😀".encode() + bytes([b]) for b in (0x80, 0xC3, 0xF0)]
    return out


REGEX_ERRORS = [
    "(", ")", "a)", "(?", "(?P", "(?P<", "(?P<a", "(?P<>x)", "(?P<1a>x)", "(?P<a>x)(?P<a>y)", "(?P=", "(?P=a)",
    "(?P=a", "(?P<a>(?P=a))", "(?Px)", "(?#", "(?#abc", "(?<", "(?<x)", "(?<=", "(?<=a", "(?=a", "(?!a", "(?(",
    "(?()a)", "(?(a)b)", "(?(0)a)", "(?(1)a)", "(?(1a)a)", "(a)(?(1)b|c|d)", "(?(99999999999999)a)", "(?i", "(?i-",
    "(?i-i:a)", "(?L)", "(?au:a)", "(?-u:a)", "(?z)", "(?iz)", "(?i-z:a)", "(?t)", "(?-t:a)", "(?i-:a)", "(?i-a",
    "(?x)a(?i)b", "a(?i)b", "(?i)(?m)", "(?", "(?@)", "[", "[a", "[]", "[^", "[a-", "[z-a]", "[\\d-z]", "[a-\\d]",
    "[\\w-\\d]", "*", "+", "?", "a**", "a*+*", "^*", "x{2,1}", "a{3,2}", "\\", "a\\", "\\8", "\\1", "(a)\\2",
    "(a\\1)", "\\400", "\\777", "\\x", "\\x1", "\\xg", "\\u12", "\\U1234", "\\U00110000", "\\q", "\\c", "[\\q]",
    "[\\8]", "[\\x1]", "[\\u12]", "[\\400]", "(?<=a+)b", "(?<=a|bc)d", "(?<=(a)\\1)b", "(a)(?<=\\1)",
    "(?<=(?P<x>a)(?P=x))b", "abc)", "((a)", "(?:a", "(?>a", "\\N", "\\N{", "\\N{}", "x\\N{a",
]
REPLACEMENTS = [
    "\\g", "\\g<", "\\g<a", "\\g<>", "\\g<1a>", "\\g<a>", "\\g<5>", "\\g<99999999999999999999>", "\\g<0001>x\\2",
    "\\3", "\\9", "\\400", "\\q", "a\\", "x\\g<b", "\n\\5", "a\nb\\q",
]


def message_records():
    out = []
    for text in JSON_CASES + json_fuzz():
        out.append(["overlay_json", text, last_line(AccentOverlay.from_json, text)])
    for b in byte_cases():
        out.append(["decode", b.hex(), last_line(b.decode, "utf-8")])
    patterns = REGEX_ERRORS + ["a\n" + p for p in REGEX_ERRORS[::4]] + ["(?x)a\n# c\n" + p for p in REGEX_ERRORS[1::7]]
    for p in patterns:
        out.append(["transform_error", p, "x", last_line(Transform(kind="regex", pattern=p, replacement="x").apply,
                                                         "abc vinho")])
    for r in REPLACEMENTS:
        out.append(["transform_error", "(v)(i)", r, last_line(Transform(kind="regex", pattern="(v)(i)", replacement=r)
                                                              .apply, "abc vinho")])
    return out


def corpora(repo):
    del repo
    out = [("help", [run(a, c) for a in HELP for c in COLUMNS])]
    out.append(("errors", [run(a, c) for a in ERRORS for c in (None, "30")]))
    out.append(("runs", [run(a, None) for a in RUNS]))
    out.append(("bytes", [run(a, None) for a in BYTE_ARGS]))
    out.append(("failures", [run(a, None, o) for a, o in FAILURES]))
    out.append(("messages", message_records()))
    return out
