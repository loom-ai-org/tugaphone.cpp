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
    env "$var=/nonexistent-tugaphone" "$cli" phonemize "tenho 2 casas" > "$scratch/out" 2> "$scratch/err"
    status=$?
    lines=$(wc -l < "$scratch/err")
    if [ "$status" != 1 ] || [ "$lines" != 1 ] || ! grep -Eq '^[A-Za-z.]*Error: ' "$scratch/err"; then
        echo "FAIL $var unset: status $status, $lines stderr lines:"
        cat "$scratch/err"
        failed=1
    fi
done

# A lexicon TSV made by hand reads as Python reads it: a key keeps its case, and
# a line without two fields does not count. An exception wins over a patch for
# the same word, and a word in the patches alone reaches the output. The
# expected line is the output of the Python tugaphone of PARITY.md with the
# same TSV files.
data=$(mktemp -d "$scratch/data-XXXXXX")
cp -R "$root/data/." "$data"
printf 'Gato\tˈmjaw\ncão\tˈbɐ̃w\tx\nsolo\npato\tˈkwak\r\nrato\tˈʃiʃ\n' > "$data/o2i-lexicons/pt-PT/infopedia.tsv"
printf 'rato\tˈpɐtʃ\nmato\tˈmmm\n' > "$data/o2i-lexicons/patches/pt-PT/infopedia.tsv"
out=$(TUGAPHONE_DATA_DIR="$data" "$cli" phonemize --lect pt-PT "o Gato, o gato, o cão, o solo, o pato, o rato e o mato")
if [ "$out" != "u ˈɡatu u ˈɡatu u ˈkɐ̃w̃ u ˈsɔlu u ˈkwak u ˈʃiz i u ˈmmm" ]; then
    echo "FAIL hand-made lexicon TSV: $out"
    failed=1
fi

# A lect that takes a lexicon fails with LookupError when the data directory
# holds no file or two files for it. Python raises LookupError there too, but
# its text differs; PARITY.md lists the difference. A lect with no lexicon
# still reads.
mv "$data/o2i-lexicons/pt-PT/infopedia.tsv" "$data/infopedia.tsv"
expected="LookupError: no installed package supplies a lexicon file for 'pt-PT'"
for files in 0 2; do
    if [ "$files" = 2 ]; then
        cp "$data/infopedia.tsv" "$data/o2i-lexicons/pt-PT/a.tsv"
        cp "$data/infopedia.tsv" "$data/o2i-lexicons/pt-PT/b.tsv"
    fi
    TUGAPHONE_DATA_DIR="$data" "$cli" phonemize --lect pt-PT "o gato" > "$scratch/out" 2> "$scratch/err"
    status=$?
    if [ "$status" != 1 ] || [ "$(cat "$scratch/err")" != "$expected" ]; then
        echo "FAIL $files lexicon files: status $status, stderr:"
        cat "$scratch/err"
        failed=1
    fi
done
out=$(TUGAPHONE_DATA_DIR="$data" "$cli" phonemize --lect pt-AO "o gato")
if [ "$out" != "ʊ ˈɡatʊ" ]; then
    echo "FAIL pt-AO without the pt-PT lexicon: $out"
    failed=1
fi

# The patch file is required too. With the exception file back and no patch
# file, the lect fails with LookupError, as orthography2ipa.packaged_lexicon
# with patches=True does. Python adds the list of installed codes.
rm -f "$data/o2i-lexicons/pt-PT/a.tsv" "$data/o2i-lexicons/pt-PT/b.tsv"
mv "$data/infopedia.tsv" "$data/o2i-lexicons/pt-PT/infopedia.tsv"
mv "$data/o2i-lexicons/patches/pt-PT/infopedia.tsv" "$data/patch.tsv"
TUGAPHONE_DATA_DIR="$data" "$cli" phonemize --lect pt-PT "o gato" > "$scratch/out" 2> "$scratch/err"
status=$?
if [ "$status" != 1 ] || [ "$(cat "$scratch/err")" != "LookupError: no installed package supplies a patch file for 'pt-PT'" ]; then
    echo "FAIL no patch file: status $status, stderr:"
    cat "$scratch/err"
    failed=1
fi

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

[ "$failed" = 0 ] && echo "cli_process: 8 of 8 checks pass"
exit "$failed"
