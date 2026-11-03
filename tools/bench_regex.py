"""Time and memory of the regular expression engine against Python's re.

Each case is one accent "transform" record: re.sub(pattern, "X", subject,
flags=re.I) over a subject of `unit` repeated `count` times. The script runs
the case in a fresh Python process and through tugaphone_parity in a fresh
C++ process, and prints the wall time and the peak RSS of each process. The
row "baseline" uses a pattern that never matches, so it shows the cost of
process start, data loading and golden parsing for the longest subject. Each
process runs `--repeat` times; the table shows the shortest time.

Usage, with the reference interpreter and a Release build:
    python -I tools/bench_regex.py <build dir> [--repeat <n>] [--only <case name> ...]
"""
import argparse
import os
import pathlib
import subprocess
import sys
import tempfile
import time

CASES = [
    # name, pattern, unit, count
    ("baseline", "x", "ab", 500000),
    ("a*", "a*", "a", 1000000),
    ("\\w+", "\\w+", "aé", 500000),
    ("(a|b)*", "(a|b)*", "ab", 500000),
    ("(?:(a)|b)*", "(?:(a)|b)*", "ab", 500000),
    ("(?:(?=(a|b))\\1)*", "(?:(?=(a|b))\\1)*", "ab", 500000),
    ("(?:a|b)*+", "(?:a|b)*+", "ab", 500000),
    ("(?=(a|b)*$) quadratic", "(?=(a|b)*$)", "ab", 2500),
    ("(a+)+c exponential", "(a+)+c", "a", 18),
]

PYTHON_RUN = """
import re, sys
pattern, unit, count = sys.argv[1], sys.argv[2], int(sys.argv[3])
re.sub(pattern, "X", unit * count, flags=re.I)
"""


def run(cmd, env=None):
    """(wall seconds, peak RSS in MB, stdout) of one child process."""
    t = time.perf_counter()
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env, text=True)
    out = p.stdout.read()
    _, status, usage = os.wait4(p.pid, 0)
    p.returncode = os.waitstatus_to_exitcode(status)
    if p.returncode:
        raise RuntimeError(f"{cmd[0]} exited {p.returncode}")
    return time.perf_counter() - t, usage.ru_maxrss / 1024, out


WRITE_GOLDEN = """
import gzip, json, re, sys
directory, pattern, unit, count = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
s = unit * count
record = ["transform", "regex", pattern, "X", True, s, re.sub(pattern, "X", s, flags=re.I)]
dumps = lambda v: json.dumps(v, ensure_ascii=False, separators=(",", ":"))
with gzip.open(directory + "/accent.jsonl.gz", "wt", encoding="utf-8") as f:
    f.write(dumps({"module": "accent", "count": 1}) + "\\n" + dumps(record) + "\\n")
"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("build")
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--only", nargs="*")
    args = parser.parse_args()
    repo = pathlib.Path(__file__).resolve().parent.parent
    env = dict(os.environ, TUGAPHONE_DATA_DIR=str(repo / "data"))
    binary = str(pathlib.Path(args.build).resolve() / "tugaphone_parity")
    print("| Case | Subject (code points) | Python s | C++ s | Python MB | C++ MB |")
    print("| --- | ---: | ---: | ---: | ---: | ---: |")
    for name, pattern, unit, count in CASES:
        if args.only and name not in args.only:
            continue
        with tempfile.TemporaryDirectory() as tmp:
            # A child writes the golden, so this process stays small: on Linux a
            # child's peak RSS counts the pages it held from its parent before exec.
            run([sys.executable, "-I", "-c", WRITE_GOLDEN, tmp, pattern, unit, str(count)])
            py = [run([sys.executable, "-I", "-c", PYTHON_RUN, pattern, unit, str(count)]) for _ in range(args.repeat)]
            cc = [run([binary, tmp, "accent"], env) for _ in range(args.repeat)]
        py_wall, py_mb = min(r[0] for r in py), max(r[1] for r in py)
        cc_wall, cc_mb, cc_out = min(r[0] for r in cc), max(r[1] for r in cc), cc[0][2]
        if "1 checked, 0 mismatches" not in cc_out:
            raise RuntimeError(f"{name}: C++ result differs from Python")
        print(f"| `{name}` | {len(unit) * count:,} | {py_wall:.2f} | {cc_wall:.2f} | {py_mb:.0f} | {cc_mb:.0f} |",
              flush=True)


if __name__ == "__main__":
    main()
