#!/bin/bash
# Checks tools/gpu-lock.sh against the ways it has failed. Everything runs in a private TMPDIR, so the
# real lock is never touched and the check is safe while other agents hold the GPU:
#
#   tools/gpu-lock-selftest.sh [path/to/gpu-lock.sh]      (about 20 s; exit 0 when every check passes)
#
# 2026-09-20: a winner that died between its `mkdir` and its pid write left a lock nobody reclaimed.
# 2026-09-27: a waiter that read `pid` between its creation and its write took a live lock for a dead
# one, and every holder's trap removed whatever lock was there, so one overlap cascaded down the queue.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
LOCKSH="${1:-$HERE/gpu-lock.sh}"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
L="$T/avgen-gpu.lock"
fail=0
check() {
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got $2, want $3"; fail=1; fi
}

# A fresh lock whose pid is still being written is waited for, not reclaimed.
mkdir "$L"; : > "$L/pid"
AVGEN_GPU_LOCK_TIMEOUT=5 TMPDIR="$T" bash "$LOCKSH" true 2>/dev/null
check "an empty pid on a fresh lock is waited for" "$?" 75

# A lock with no pid for longer than any writer takes is reclaimed.
rm -rf "$L"; mkdir "$L"; touch -t "$(date -v-2M +%Y%m%d%H%M.%S)" "$L"
AVGEN_GPU_LOCK_TIMEOUT=5 TMPDIR="$T" bash "$LOCKSH" true 2>/dev/null
check "a lock with no pid for two minutes is reclaimed" "$?" 0

# A dead holder's lock is reclaimed.
rm -rf "$L"; mkdir "$L"; echo 999999 > "$L/pid"
AVGEN_GPU_LOCK_TIMEOUT=5 TMPDIR="$T" bash "$LOCKSH" true 2>/dev/null
check "a dead holder's lock is reclaimed" "$?" 0

# A holder whose lock has changed hands leaves the new owner's lock where it is.
rm -rf "$L"
TMPDIR="$T" bash "$LOCKSH" sleep 2 2>/dev/null & holder=$!
sleep 1; sleep 30 & other=$!
echo "$other" > "$L/pid"
wait "$holder"
check "a holder leaves a lock that is no longer its own" "$(cat "$L/pid" 2>/dev/null)" "$other"
kill "$other" 2>/dev/null; wait "$other" 2>/dev/null

# Two started together run one after the other, and the lock is free afterwards.
rm -rf "$L"; : > "$T/intervals"
for _ in 1 2; do
    TMPDIR="$T" bash "$LOCKSH" python3 -c "import time; a = time.time(); time.sleep(1.5); open('$T/intervals', 'a').write(f'{a} {time.time()}\n')" 2>/dev/null &
done
wait
order=$(python3 -c "
spans = sorted(tuple(map(float, line.split())) for line in open('$T/intervals') if line.strip())
print('serial' if len(spans) == 2 and spans[0][1] <= spans[1][0] else 'overlap')")
check "two holders run one after the other" "$order" serial
check "the lock is free afterwards" "$([ -d "$L" ] && echo held || echo free)" free

exit "$fail"
