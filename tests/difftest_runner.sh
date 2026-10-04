#!/bin/sh
# make difftest must fail when the comparisons fail, or cannot even be listed: run
# `make difftest-run` with a fake uv that stands in for subtest.py. Needs no game files.
#   tests/difftest_runner.sh [source folder]
src=${1:-.}
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT
fails=0

# a fake `uv run subtest.py ...`: MODE=fail-one fails routine beta; MODE=fail-list cannot list;
# MODE=empty lists nothing; MODE=pass passes all; MODE=order fails unless the build has finished
# (the stub cmake's mark) and the comparisons are given that build's tool
cat > "$tmp/uv" <<'EOF'
#!/bin/sh
shift; shift
if [ "$MODE" = order ]; then
  [ -f "$MARK" ] || { echo "compared before the build finished" >&2; exit 1; }
  [ "$1" = --list ] || [ "$3" = "$TOOL" ] || { echo "compared with $3, not $TOOL" >&2; exit 1; }
fi
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

# make -j2 difftest: the comparisons only once the build is done, with its ep_subsys (a stub
# cmake that takes a second to build)
printf '#!/bin/sh\ncase "$1" in --build) sleep 1; touch "%s" ;; esac\n' "$tmp/built" > "$tmp/cmake"
chmod +x "$tmp/cmake"
MODE=order MARK="$tmp/built" TOOL="$tmp/b/ep_subsys" \
    make -s -j2 -C "$src" difftest UV="$tmp/uv" CMAKE="$tmp/cmake" BUILD="$tmp/b" > "$tmp/out" 2>&1
if [ $? != 0 ]; then
    echo "FAIL: make -j2 difftest compared too early or with another build's tool:"
    cat "$tmp/out"
    fails=$((fails + 1))
fi
[ "$fails" = 0 ] && echo "difftest runner: failures fail" || echo "difftest runner: FAILED"
[ "$fails" = 0 ]
