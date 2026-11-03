#!/bin/sh
# Process-level behaviour of the executables that the goldens cannot hold.
# Usage: cli_process.sh <tugaphone> <tugaphone_benchmark> <source root> <scratch dir>
set -u
cli=$1 bench=$2 root=$3 scratch=$4
mkdir -p "$scratch"
failed=0

# Without its data, the executable prints one error line and exits with 1,
# as Python does for an uncaught exception. Python ships its data, so no
# golden holds this case.
for var in TUGALEX_DATA_DIR BIFONIA_DATA_DIR; do
    lexicon=$(mktemp -d "$scratch/lexicon-XXXXXX")
    env "$var=/nonexistent-tugaphone" TUGAPHONE_LEXICON_DIR="$lexicon" "$cli" phonemize "o gato" \
        > "$scratch/out" 2> "$scratch/err"
    status=$?
    lines=$(wc -l < "$scratch/err")
    if [ "$status" != 1 ] || [ "$lines" != 1 ] || ! grep -Eq '^[A-Za-z.]*Error: ' "$scratch/err"; then
        echo "FAIL $var unset: status $status, $lines stderr lines:"
        cat "$scratch/err"
        failed=1
    fi
done

# A failed write to stdout: python scripts/benchmark.py exits with 120.
TUGAPHONE_BENCHMARK_ROOT="$root" "$bench" --dialect pt-PT --limit 1 > /dev/full 2> "$scratch/err"
status=$?
expected="Exception ignored in: <_io.TextIOWrapper name='<stdout>' mode='w' encoding='utf-8'>
OSError: [Errno 28] No space left on device"
if [ "$status" != 120 ] || [ "$(cat "$scratch/err")" != "$expected" ]; then
    echo "FAIL benchmark > /dev/full: status $status, stderr:"
    cat "$scratch/err"
    failed=1
fi
[ "$failed" = 0 ] && echo "cli_process: 3 of 3 checks pass"
exit "$failed"
