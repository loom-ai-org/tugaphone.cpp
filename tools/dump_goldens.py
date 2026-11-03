"""Dump the reference tugaphone's output as the C++ port's parity goldens.

Each module listed in MODULES has a dumper at tools/goldens/<module>.py with
a corpora(repo) function. This script writes tests/goldens/<module>.jsonl.gz
for each one. tests/parity.cpp recomputes every record in C++ and compares
the bytes.

Usage, with the reference interpreter and a tugaphone checkout that has the
reference tag:
    python -I tools/dump_goldens.py --out-dir tests/goldens --tugaphone-repo <tugaphone> [module ...]
"""
import argparse
import importlib
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from goldens import common  # noqa: E402

MODULES = [
    "registry",
    "dialects",
    "text_normalization",
    "number_utils",
    "tokenizer",
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--tugaphone-repo", required=True)
    parser.add_argument("modules", nargs="*", default=MODULES)
    args = parser.parse_args()
    total = 0
    for name in args.modules:
        module = importlib.import_module(f"goldens.{name}")
        total += common.write(str(pathlib.Path(args.out_dir) / f"{name}.jsonl.gz"), name,
                              module.corpora(args.tugaphone_repo))
    print(f"total: {total} records", file=sys.stderr)


if __name__ == "__main__":
    main()
