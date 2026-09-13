#!/bin/bash
# One GPU, several agents. Serialises anything that touches it.
#
# Two agents running GPU work at once do not go faster -- they contend for one device -- and they
# corrupt each other's results: measured on 2026-09-13, two concurrent test binaries produced 145
# differing channels between two draws of one FrameTime plus four checkpoint mismatches, in runs that
# were individually clean. A GPU result taken without this lock is not evidence.
#
#   tools/gpu-lock.sh ./build/release/tests/avgen_render_tests "[water]"
#   tools/gpu-lock.sh ./build/release/src/avgen --headless --frames 120 ...
#
# `mkdir` is atomic on every filesystem here, which `[ -e ]` plus `touch` is not.
set -uo pipefail

LOCK="${TMPDIR:-/tmp}/avgen-gpu.lock"
WAITED=0
MAX_WAIT="${AVGEN_GPU_LOCK_TIMEOUT:-3600}"

while ! mkdir "$LOCK" 2>/dev/null; do
    if [ -f "$LOCK/pid" ] && ! kill -0 "$(cat "$LOCK/pid" 2>/dev/null)" 2>/dev/null; then
        # The holder died without releasing. Reclaim rather than wait out the timeout.
        echo "gpu-lock: stale lock from pid $(cat "$LOCK/pid" 2>/dev/null), reclaiming" >&2
        rm -rf "$LOCK"
        continue
    fi
    if [ "$WAITED" -ge "$MAX_WAIT" ]; then
        echo "gpu-lock: timed out after ${MAX_WAIT}s waiting for $LOCK" >&2
        exit 75
    fi
    [ $((WAITED % 60)) -eq 0 ] && echo "gpu-lock: waiting for the GPU (${WAITED}s)..." >&2
    sleep 5
    WAITED=$((WAITED + 5))
done

echo $$ > "$LOCK/pid"
echo "${AVGEN_AGENT:-$(basename "$PWD")}" > "$LOCK/holder"
trap 'rm -rf "$LOCK"' EXIT INT TERM

"$@"
