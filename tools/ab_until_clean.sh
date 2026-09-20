#!/usr/bin/env bash
# Run one --ab arm until its per-block GPU MINIMA are stable, then report the delta.
#
# ADR-170 says minima over repeats. This machine is shared by several agents, and a CPU suite in a
# sibling worktree saturates the unified memory bus the GPU shares -- which inflates medians AND,
# when it is bad enough, the minima too. `ab_minima.py` prints the spread of the minima within an
# arm, so a disturbed run announces itself: this retries until that spread is under a threshold
# rather than averaging a disturbed run into a clean one.
set -u
PROJECT="$1"; ARM="$2"; OUT="$3"; MAXSPREAD="${4:-1.0}"; TRIES="${5:-6}"
for try in $(seq 1 "$TRIES"); do
    ./tools/gpu-lock.sh ./build/release/src/avgen --project "$PROJECT" --headless \
        --ab "$ARM" --ab-blocks 4 --frames 200 --size 1920x1080 \
        --bench-json "$OUT" > "${OUT%.json}.log" 2>&1
    line=$(python3 tools/ab_minima.py "$OUT" 2>/dev/null | tail -1)
    spread=$(printf '%s' "$line" | sed -n 's/.*arm \([0-9.]*\) ms.*/\1/p')
    if [ -n "$spread" ] && awk "BEGIN{exit !($spread < $MAXSPREAD)}"; then
        echo "$line   [try $try, clean]"
        exit 0
    fi
    echo "  try $try discarded: $line" >&2
    sleep 20
done
echo "$line   [NOT CLEAN after $TRIES tries -- the machine never held still]"
