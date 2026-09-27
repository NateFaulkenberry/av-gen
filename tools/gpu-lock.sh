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
# BEFORE YOU READ THE RESULT: docs/testing.md, "Forty-one ways a green suite has lied".
#
# It is here because it is not findable from anywhere else. Two agents in one night each
# walked into a hazard that section already described accurately -- one of them by name,
# naming the exact test file -- and neither knew the document existed. This is the one file
# every agent doing GPU work reads the top of, so the pointer lives here.
#
# THE ONE SENTENCE, and it is narrower than "read the exit code":
#
#     Read the exit code the BINARY returned, not the one the tooling reports about it.
#
# This file is a wrapper. Its exit status is a report ABOUT your run and has been wrong in
# both directions in one session -- failure reported for a process that was still alive and
# holding this lock, success reported for a run whose own summary said `1 failed`. Capture
# `$?` immediately after the binary, in the same shell, and read it TOGETHER with the
# summary: an exit code with no summary is a crash, a summary that disagrees with the exit
# code is a tooling fault, and neither is a pass.
#
# AND THE CPU SUITE COUNTS AS GPU WORK. `avgen_tests` encodes video through the same system
# frameworks this suite renders with. Run concurrently on 2026-09-21 the CPU suite died with
# SIGTRAP inside CVPixelBufferPoolCreatePixelBuffer and the render suite reported a DETERMINISM
# failure -- one project, two fresh engines, two different sequence hashes. Run one after the
# other, both are clean. Serialise them; a run taken while the other suite is up is not evidence,
# whichever way it came out. docs/testing.md 29.
#
# AND THE INPUTS ARE LIVE. `ShaderLibrary` loads `.wgsl` from the SOURCE TREE when a case asks
# for a module, so editing a shader while a suite is running mixes two versions into one run --
# which comes back green and means nothing. Same for scenes and assets. A C++ edit is harmless
# (the binary is linked); anything read from disk is not. docs/testing.md entry 25.
#
# Doing that is load-bearing wherever it already happens, and is written down almost
# nowhere -- a later cleanup that wraps an invocation "for consistency" removes the only
# reason the result can be trusted, silently. That is why this paragraph is here and not
# only one link away.
#
# The rest, for families A and B. Not
# the summary, not a `grep -c FAILED`, and never a pipeline's `$?` -- that is the last
# command's, usually `grep`, and grep is delighted to find nothing. A crashed run prints no
# verdict line at all, so a failure grep reports success on it.
#
# Family C the exit code CANNOT catch: the run is honest, exit 0 is correct, and the
# thing you measured is not the thing you meant -- a filter that excluded nothing, a
# census that matched the wrong noun, a probe aimed where the knob does nothing. Check
# what your scan MATCHED, not just how many. An expected number is the one to verify.
#
# NEVER EDIT THIS FILE WHILE ANY INSTANCE OF IT IS RUNNING. bash reads a script
# incrementally from a byte offset, so inserting even a COMMENT shifts every byte below
# it and a running instance resumes mid-token. That wedged this lock for twenty minutes
# on 2026-09-20: a corrupted instance won the `mkdir` and died before writing `pid`,
# leaving an ownerless directory that the stale-reclaim below cannot see, because its
# own test is `[ -f "$LOCK/pid" ]`. Land changes here only when the lock is free and
# `ps -Ao pid,args | grep gpu-lock` is empty.
#
# And if you kill a run: `trap` only fires for the process that installed it. Signalling
# this wrapper leaves the test binary alive and this lock held, which is invisible from
# both ends. Check `pgrep -fl avgen_render_tests` and the lock's `pid` file afterwards.
#
# `mkdir` is atomic on every filesystem here, which `[ -e ]` plus `touch` is not.
#
# THE PID IS WRITTEN WHOLE, AND A HOLDER RELEASES ONLY ITS OWN LOCK. On 2026-09-27 at 13:00:24
# a waiter read `pid` in the instant between `echo` creating the file and filling it, took
# `kill -0 ""` for a dead holder, and reclaimed a live lock. Every holder's trap then removed
# whatever lock was there, so each job that ended let the next waiter in beside a job still
# running, for as long as the queue lasted. A 4K cost measurement was taken on a shared GPU
# before anyone noticed. So the pid now arrives by `mv`, a pid-less lock is reclaimed only once
# it has been ownerless for longer than any writer takes (the 2026-09-20 wedge above), and the
# trap removes the lock only while its `pid` is still this process's.
set -uo pipefail

LOCK="${TMPDIR:-/tmp}/avgen-gpu.lock"
WAITED=0
MAX_WAIT="${AVGEN_GPU_LOCK_TIMEOUT:-3600}"

UNOWNED_GRACE=30

while ! mkdir "$LOCK" 2>/dev/null; do
    holder=$(cat "$LOCK/pid" 2>/dev/null)
    if [ -n "$holder" ]; then
        if ! kill -0 "$holder" 2>/dev/null; then
            # The holder died without releasing. Reclaim rather than wait out the timeout -- the
            # lock that dead pid still owns, not one another waiter has reclaimed since.
            if [ "$(cat "$LOCK/pid" 2>/dev/null)" = "$holder" ]; then
                echo "gpu-lock: stale lock from pid $holder, reclaiming" >&2
                rm -rf "$LOCK"
            fi
            continue
        fi
    elif [ -d "$LOCK" ]; then
        # No pid: a winner between its `mkdir` and its first write, or one that died there.
        # Only time tells them apart.
        age=$(( $(date +%s) - $(stat -f %m "$LOCK" 2>/dev/null || date +%s) ))
        if [ "$age" -gt "$UNOWNED_GRACE" ]; then
            echo "gpu-lock: a lock with no pid for ${age}s, reclaiming" >&2
            rm -rf "$LOCK"
            continue
        fi
    fi
    if [ "$WAITED" -ge "$MAX_WAIT" ]; then
        echo "gpu-lock: timed out after ${MAX_WAIT}s waiting for $LOCK" >&2
        exit 75
    fi
    [ $((WAITED % 60)) -eq 0 ] && echo "gpu-lock: waiting for the GPU (${WAITED}s)..." >&2
    sleep 5
    WAITED=$((WAITED + 5))
done

echo $$ > "$LOCK/pid.$$" && mv "$LOCK/pid.$$" "$LOCK/pid"
echo "${AVGEN_AGENT:-$(basename "$PWD")}" > "$LOCK/holder"
trap '[ "$(cat "$LOCK/pid" 2>/dev/null)" = "$$" ] && rm -rf "$LOCK"' EXIT INT TERM

"$@"
