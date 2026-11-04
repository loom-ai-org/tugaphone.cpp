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
# Python's sys.stdout keeps up to 8192 bytes before it writes them. A longer
# output reaches the descriptor inside print(): the write error is then an
# uncaught OSError with its own errno, and the status is 1.
text="O comboio das 16:30 chegou com 23 passageiros e o Sr. Silva leu o Cap. XII."
long=$text
i=1
while [ "$i" -lt 200 ]; do long="$long $text"; i=$((i + 1)); done

# Run "$@" with stdout on a pipe whose read end is closed before the command
# starts. Writes "<status>" to $scratch/status and stderr to $scratch/err.
closed_pipe() {
    rm -f "$scratch/closed"
    {
        n=0
        while [ ! -e "$scratch/closed" ] && [ "$n" -lt 100 ]; do sleep 0.1; n=$((n + 1)); done
        "$@" 2> "$scratch/err"
        echo $? > "$scratch/status"
    } | {
        exec 0<&-
        : > "$scratch/closed"
    }
}

check() {
    label=$1 want_status=$2 want_err=$3 status=$4
    if [ "$status" != "$want_status" ] || [ "$(cat "$scratch/err")" != "$want_err" ]; then
        echo "FAIL $label: status $status, stderr:"
        cat "$scratch/err"
        failed=1
    fi
}

"$cli" phonemize "$long" --lect pt-PT > /dev/full 2> "$scratch/err"
check "long phonemize > /dev/full" 1 "OSError: [Errno 28] No space left on device" $?

closed_pipe "$cli" phonemize "$long" --lect pt-PT
check "long phonemize | closed pipe" 1 "BrokenPipeError: [Errno 32] Broken pipe" "$(cat "$scratch/status")"

# A short output fails at the final flush, as for /dev/full above. Python
# ignores SIGPIPE, so the write gives EPIPE and the process does not die by
# the signal.
closed_pipe "$cli" list
check "list | closed pipe" 120 "Exception ignored in: <_io.TextIOWrapper name='<stdout>' mode='w' encoding='utf-8'>
BrokenPipeError: [Errno 32] Broken pipe" "$(cat "$scratch/status")"

[ "$failed" = 0 ] && echo "cli_process: 6 of 6 checks pass"
exit "$failed"
