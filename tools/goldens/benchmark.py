"""Goldens of scripts/benchmark.py, the gold benchmark of the reference tag.

The script is loaded from the tag with `git show`, so the working tree of the
Python repository does not matter. The gold fixtures are the copies in
tests/data/gold of this repository; the dumper checks that they are equal to
the fixtures of the tag.

A word record holds one fixture word with its rules-only transcription, its
edit distance, its stress verdict and its confusion pairs. A main record runs
main() with one argument list in a scratch repository root and holds standard
output, standard error, the exit status and the two reports. When main()
raises, Python prints a traceback that names its own files, so the record
holds the last line of the traceback and says so, as the command-line records
do. The scratch root and this repository read as "<root>" in that line. An
optional last item sets the scratch root up: {"benchmarks_file": true} puts a
regular file where main() makes the benchmarks directory.
"""
import contextlib
import io
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import traceback
import types

from . import common

ROOT = pathlib.Path(__file__).resolve().parents[2]
GOLD = ROOT / "tests" / "data" / "gold"

# Arguments of main(). --refresh-fixtures downloads the gold and is not run.
RUNS = [
    [], ["--dialect", "pt-BR"], ["--dialect", "pt-PT-x-lisbon", "--limit", "50"], ["--limit", "5"],
    ["--limit", "0"], ["--limit", "-3"], ["--limit", "-999999"], ["--limit", "99999999999999999999999"],
    ["--dialect", "pt-TL", "--limit", "-998"], ["--dialect", "xx"], ["--dialect", ""], ["--dialect", "", "--limit", "2"],
    ["--limit", "abc"], ["--limit", ""], ["--limit", " 7 "], ["--limit", "1_0"], ["--limit", "+4"], ["--limit"],
    ["--dialect"], ["-h"], ["--help"], ["--he"], ["--x"], ["x"], ["--dia=pt-AO", "--lim=3"],
    ["--dialect=pt-MZ", "--dialect", "pt-AO", "--limit", "1"], ["--li", "2", "--di", "pt-BR"],
    ["--dialect", "/dev/null", "--limit", "3"], ["--dialect", "../gold/pt-BR", "--limit", "2"],
]
# Runs in a scratch root where "benchmarks" is a regular file.
BENCHMARKS_FILE_RUNS = [[], ["--dialect", "pt-BR"]]
COLUMNS = [None, "30", "200"]

# Strings for the scoring helpers: stress marks, joiners, ties, syllabic and
# non-syllabic diacritics, decomposed vowels, spaces, and long strings that
# switch on the "popular element" rule of difflib (200 characters or more).
IPA_PROBES = [
    "", " ", "ˈ", "ˌ", "ˈˈ", "a", "ˈa", "ˌa", "ˈkaza", "kaˈza", "ˈka.za", "ka‿za", "t͡ʃa", "t͜ʃa", "ˈpaj̯", "ˈpa i̯",
    "ˈkasɐ ˈbo", "m̩", "ˈm̍", "ẽ", "ẽ", "ˈɐ̃w̃", "ɐ̃ˈw̃", "a·b|c", "ˈpɾɐ.ˈtu", "ˌpɾɐˈtu", "ˈa ˈb", "ˈtʃ",
    "kʰ", "ˈxɔ̃", "ɾˈa", "ˈjɛ", "uˈi", "ˈ ", "ab ˈ", "ˈpaɪ̯", "ˈɐj̃", "ˈoʊ̯",
]
LONG = ["ab" * 120, "ab" * 100 + "c" + "ab" * 100, "a" * 250, "a" * 199 + "b", "ˈa" * 130, "xyz" * 90 + "a" * 3,
        "ɐ" * 210 + "ˈ" + "ɐ" * 5]


def load_script(repo):
    source = subprocess.run(["git", "-C", repo, "show", f"{common.TAG}:scripts/benchmark.py"], check=True,
                            capture_output=True, text=True).stdout
    module = types.ModuleType("benchmark")
    module.__file__ = str(ROOT / "scripts" / "benchmark.py")
    saved = list(sys.path)
    exec(compile(source, "benchmark.py", "exec"), module.__dict__)
    sys.path[:] = saved
    return module


def check_fixtures(repo, bench):
    for dialect in bench.DIALECT_TO_REGION:
        tagged = subprocess.run(["git", "-C", repo, "show", f"{common.TAG}:tests/data/gold/{dialect}.tsv"],
                                check=True, capture_output=True).stdout
        assert (GOLD / f"{dialect}.tsv").read_bytes() == tagged, dialect


def word_records(bench):
    out = []
    for dialect in sorted(bench.DIALECT_TO_REGION):
        golds = bench.load_fixture(dialect)
        for word in sorted(golds):
            hyp = common.call(bench.rules_only_ipa, word, dialect)
            if isinstance(hyp, dict):
                out.append(["word", dialect, word, golds[word], hyp])
                continue
            score = list(bench.score_word(hyp, golds[word]))
            out.append(["word", dialect, word, golds[word],
                         [hyp, score, bench.score_stress(word, hyp, golds[word]), confusions(bench, hyp, golds[word])]])
    return out


def confusions(bench, hyp, golds):
    confusion = {}
    bench._count_confusions(hyp, golds, confusion)
    return [[a, b, c] for (a, b), c in confusion.items()]


def probe_records(bench):
    import difflib
    out = []
    strings = IPA_PROBES + LONG
    for s in strings:
        out.append(["normalize", s, common.call(bench.normalize, s)])
        out.append(["segments", s, common.call(lambda: list(bench._segments(s)))])
        out.append(["stress_eligible", s, common.call(bench.stress_eligible, s)])
    for a in strings:
        for b in strings:
            out.append(["levenshtein", a, b, bench._levenshtein(a, b)])
            out.append(["opcodes", a, b, [list(op) for op in difflib.SequenceMatcher(None, a, b).get_opcodes()]])
            out.append(["stress_hit", a, b, common.call(bench.stress_hit, a, b)])
            out.append(["align", a, b, common.call(
                lambda: [list(kv) for kv in sorted(bench._align(bench._segments(a)[0], bench._segments(b)[0]).items())])])
        out.append(["score_word", a, strings[:6], common.call(lambda: list(bench.score_word(a, strings[:6])))])
        for word, golds in (("palavra", strings[2:9]), ("duas palavras", ["ˈduɐʃ pɐˈlavɾɐʃ"]), ("x", [])):
            out.append(["score_stress", word, a, golds, common.call(bench.score_stress, word, a, golds)])
        out.append(["confusions", a, strings[30:36], common.call(lambda: confusions(bench, a, strings[30:36]))])
    for hits, n in [(0, 0), (5, 19), (19, 20), (20, 20), (0, 20), (1, 3), (993, 1000), (2, 3 * 10 ** 6),
                    (1, 7 * 10 ** 4)]:
        acc = bench.stress_accuracy(hits, n)
        out.append(["stress_accuracy", hits, n, None if acc is None else repr(acc)])
    out.append(["DIALECT_TO_REGION", list(bench.DIALECT_TO_REGION.items())])
    out.append(["constants", [bench.FIXTURE_WORDS, bench.SAMPLE_SEED, bench.HARNESS_VERSION, bench.STRESS_MIN_WORDS]])
    return out


def score_records(bench):
    out = []
    for dialect in sorted(bench.DIALECT_TO_REGION) + ["xx"]:
        for limit in (None, 0, 1, 7, -1, -999, -1001, 1001):
            row = common.call(bench.score_dialect, dialect, limit)
            out.append(["score_dialect", dialect, limit,
                        row if isinstance(row, dict) and "err" in row else json.dumps(row, ensure_ascii=False)])
    return out


def run_main(bench, args, columns, options=None):
    with tempfile.TemporaryDirectory(prefix="tugaphone-bench-") as root:
        os.symlink(ROOT / "tests", os.path.join(root, "tests"))
        bench.FIXTURE_DIR = str(GOLD)
        bench.RESULTS_JSON = os.path.join(root, "benchmarks", "results.json")
        bench.SCOREBOARD_MD = os.path.join(root, "docs", "scoreboard.md")
        os.mkdir(os.path.join(root, "docs"))
        if options and options.get("benchmarks_file"):
            pathlib.Path(root, "benchmarks").write_text("", encoding="utf-8")
        saved_argv, saved_columns = sys.argv, os.environ.pop("COLUMNS", None)
        if columns is not None:
            os.environ["COLUMNS"] = columns
        out, err = io.StringIO(), io.StringIO()
        status, is_traceback = 0, False
        sys.argv = ["benchmark.py", *args]
        try:
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                bench.main()
        except SystemExit as exc:
            status = exc.code if isinstance(exc.code, int) else (0 if exc.code is None else 1)
        except Exception:  # the uncaught exception ends the process: status 1 and a traceback
            status, is_traceback = 1, True
            line = traceback.format_exc().splitlines()[-1]
            err.write(line.replace(root, "<root>").replace(str(ROOT), "<root>"))
        finally:
            sys.argv = saved_argv
            os.environ.pop("COLUMNS", None)
            if saved_columns is not None:
                os.environ["COLUMNS"] = saved_columns

        def report(path):
            return pathlib.Path(path).read_text(encoding="utf-8") if os.path.exists(path) else None

        record = ["main", args, columns, out.getvalue(), err.getvalue(), status, report(bench.RESULTS_JSON),
                  report(bench.SCOREBOARD_MD), is_traceback]
        return record + [options] if options else record


def corpora(repo):
    bench = load_script(repo)
    check_fixtures(repo, bench)
    out = [("probes", probe_records(bench)), ("words", word_records(bench)), ("score_dialect", score_records(bench))]
    out.append(("main", [run_main(bench, a, c) for a in RUNS for c in (COLUMNS if a[:1] in (["-h"], ["--help"])
                                                                         else [None])]
                + [run_main(bench, a, None, {"benchmarks_file": True}) for a in BENCHMARKS_FILE_RUNS]))
    return out
