#!/bin/sh
# make difftest must fail when the comparisons fail, or cannot even be listed: run
# `make difftest-run` with a fake uv that stands in for subtest.py. Needs no game files.
#   tests/difftest_runner.sh [source folder]
src=${1:-.}
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT
fails=0

# a fake `uv run subtest.py ...`: MODE=fail-one fails routine beta; MODE=fail-list cannot list;
# MODE=empty lists nothing; MODE=pass passes all
cat > "$tmp/uv" <<'EOF'
#!/bin/sh
shift; shift
case "$MODE:$1" in
  fail-list:--list) echo "Traceback: no module" >&2; exit 1 ;;
  empty:--list) exit 0 ;;
  *:--list) printf 'alpha\nbeta\n' ;;
  fail-one:beta) echo "beta: 240 states (2 fuzzed per state), 3 differ"; exit 1 ;;
  *) echo "$1: 240 states (2 fuzzed per state), 0 differ" ;;
esac
EOF
chmod +x "$tmp/uv"

check() { # MODE, the make status wanted (0 or 1)
    MODE=$1 make -s -C "$src" difftest-run UV="$tmp/uv" > "$tmp/out" 2>&1
    got=$?
    [ "$got" -ne 0 ] && got=1
    if [ "$got" != "$2" ]; then
        echo "FAIL: $1 gave status $got, want $2:"
        cat "$tmp/out"
        fails=$((fails + 1))
    fi
}

check pass 0
check fail-one 1
check fail-list 1
check empty 1
[ "$fails" = 0 ] && echo "difftest runner: failures fail" || echo "difftest runner: FAILED"
[ "$fails" = 0 ]
