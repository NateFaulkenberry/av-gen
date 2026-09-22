#!/bin/bash
# Run a test binary and print the THREE numbers a guard needs, plus the exit code from the binary
# itself. `docs/testing.md` is why each one is here.
#
#   tools/suite-guards.sh ./build/release/tests/avgen_tests
#   tools/suite-guards.sh ./build/release/tests/avgen_render_tests '[determinism],[examples]' 44
#
# WHY THREE NUMBERS AND NOT TWO. A guard that reads the verdict and the failures cannot tell a
# clean run from one that skipped half its cases: a GPU context that fails to create makes a case
# SKIP, and a skipped case is not a passed one. The skip count is the field that distinguishes
# them, and it is a thing to READ rather than to assume (entry 29).
#
# WHY THE EXPECTED COUNT IS AN ARGUMENT. A Catch2 filter is comma-separated, so a test name
# containing a comma splits into two specs that match nothing, and the filter silently selects a
# SUBSET as if it were the set. **A selector that cannot report a miss will report a subset as if
# it were the set.** Passing the number you expect turns that into a failure before the run starts
# rather than a quiet under-measurement you never see.
#
# WHY IT GREPS FOR TWO VERDICT LINES. Catch2 prints `test cases: ...` when something fails and
# `All tests passed (N assertions in M test cases)` when nothing does, so a guard looking only for
# the first reads ZERO verdict lines on a perfect run and cannot tell "perfect" from "never ran".
set -u
BIN=${1:?usage: suite-guards.sh <binary> [filter] [expected-case-count]}
FILTER=${2:-}
EXPECT=${3:-}

if [ -n "$EXPECT" ]; then
    # Parsed by MATCHING THE SENTENCE rather than by taking the last line: Catch2 emits a trailing
    # blank line, so `tail -1` reads "" and a naive `grep -oE '^[0-9]+'` then reports 0 -- which
    # would refuse every correct filter and, worse, is a selector that cannot report a miss
    # failing in exactly the way this argument exists to catch. Found by self-testing the tool
    # against the case it was written for.
    if [ -n "$FILTER" ]; then
        GOT=$("$BIN" --list-tests "$FILTER" 2>/dev/null | grep -oE '^[0-9]+ matching test case' | grep -oE '^[0-9]+')
    else
        GOT=$("$BIN" --list-tests 2>/dev/null | grep -oE '^[0-9]+ test case' | grep -oE '^[0-9]+')
    fi
    GOT=${GOT:-0}
    echo "selected: $GOT (expected $EXPECT)"
    if [ "$GOT" != "$EXPECT" ]; then
        echo "REFUSING TO RUN: the filter selected $GOT cases and you expected $EXPECT." >&2
        echo "A Catch2 spec is comma-separated; a test name containing a comma splits into two" >&2
        echo "specs that match nothing. Use a trailing * for any name with punctuation." >&2
        exit 2
    fi
fi

LOG=$(mktemp -t avgen_guards)
if [ -n "$FILTER" ]; then "$BIN" "$FILTER" > "$LOG" 2>&1; else "$BIN" > "$LOG" 2>&1; fi
CODE=$?   # the BINARY's own, captured immediately and in this shell (entry 18)

echo "exit code (from the binary): $CODE"
echo "verdict lines: $(grep -cE '^test cases:|^All tests passed' "$LOG")"
grep -E '^test cases:|^All tests passed' "$LOG"
SKIPPED=$(grep -oE "[0-9]+ skipped" "$LOG" | head -1); echo "skipped: ${SKIPPED:-0 (none reported)}"
echo "FAILED blocks: $(grep -c ': FAILED' "$LOG")"
grep -oE '[a-z_]+\.cpp:[0-9]+: FAILED' "$LOG" | sort -u | head -10
echo "crash text: $(grep -cE 'Bus error|Segmentation fault|syntax error' "$LOG")"
echo "log: $LOG"
exit $CODE
