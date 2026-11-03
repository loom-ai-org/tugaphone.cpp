"""Goldens of the tugaphone command line (python -m tugaphone).

Each record runs the reference interpreter on one argument list, with the
working directory at the root of this repository and COLUMNS set to a value
or unset, and holds standard output, standard error and the exit status.
tests/parity/cli.cpp runs the C++ executable on the same arguments and
compares the bytes. When Python ends with a traceback, the record says so: the
frames name Python source files, so the C++ side must give the same output,
the same status and an error line with the same exception class. Such a record
holds only the class, because the frames and the message name local paths.
"""
import os
import pathlib
import subprocess
import sys

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
RUNS = (
    [["phonemize", ACCEPTANCE, "--lect", lect] for lect in list_dialects()]
    + [["phonemize", ACCEPTANCE], ["phonemize", ACCEPTANCE, "--lang", "pt-BR"], ["phonemize", ACCEPTANCE, "--le=pt-AO"],
       ["phonemize", "--lect", "pt-BR", ACCEPTANCE], ["phonemize", "--lect=pt-BR", "--", ACCEPTANCE],
       ["phonemize", ACCEPTANCE, "--lect", "PT_br"], ["phonemize", ACCEPTANCE, "--lect", ""],
       ["phonemize", ACCEPTANCE, "--lect", "xx"], ["phonemize", ACCEPTANCE, "--lect", "pt-pt-x-azores"],
       ["phonemize", ACCEPTANCE, "--lect", "pt-BR", "--lect", "pt-AO"]]
    + [["phonemize", "--", t] for t in TEXTS] + [["phonemize", t, "--lect", "pt-BR"] for t in TEXTS if t[:1] != "-"]
    + [["force-accent", t, "--lect", lect, "--mode", mode] + base
       for t in ("o vinho verde", "a tia", ACCEPTANCE) for lect in ("pt-BR", "pt-PT-x-porto", "pt-AO")
       for mode in ("ipa", "respell") for base in ([], ["--base", "pt-BR"])]
    + [["force-accent", "o vinho verde", "--lect", "pt-PT-x-porto", "--mode", mode, "--overlay", path]
       for mode in ("ipa", "respell")
       for path in ("tests/data/cli/tweaks.json", "tests/data/cli/crlf.json", "", "tests/data/cli/missing.json",
                    "tests/data/cli/broken.json", "tests/data/cli/latin1.json", "tests/data/cli/bad-kind.json",
                    "tests/data/cli")]
    + [["force-accent", "o vinho", "--lang", "pt-BR", "--mo", "respell", "--ba", "pt-AO"],
       ["force-accent", "--mode=respell", "--lect=pt-BR", "--", "-o vinho"], ["list"], ["list", "--"]]
)
COLUMNS = [None, "40", "120", "20", "5", "1", "0", "abc", " 60 ", "-3", "1_00"]


def run(args, columns):
    env = {k: v for k, v in os.environ.items() if k != "COLUMNS"}
    if columns is not None:
        env["COLUMNS"] = columns
    p = subprocess.run([sys.executable, "-I", "-m", "tugaphone", *args], capture_output=True, cwd=ROOT, env=env)
    out, err = p.stdout.decode("utf-8"), p.stderr.decode("utf-8")
    traceback = err.startswith("Traceback (most recent call last):\n")
    if traceback:  # the frames and the message name local paths: keep the exception class only
        err = err.rstrip("\n").splitlines()[-1].split(":")[0]
    if p.returncode == 0 and err:
        raise SystemExit(f"python wrote to stderr on success, a warning that names a path: {args} {err!r}")
    return ["cli", args, columns, out, err, p.returncode, traceback]


def corpora(repo):
    del repo
    out = [("help", [run(a, c) for a in HELP for c in COLUMNS])]
    out.append(("errors", [run(a, c) for a in ERRORS for c in (None, "30")]))
    out.append(("runs", [run(a, None) for a in RUNS]))
    return out
