#!/usr/bin/env bash
# Runs the CPU suite under a binary name that another agent's `pkill -f 'avgen_tests ~\[gpu\]'`
# cannot match. Two hazards this exists for, both met on the raytrace branch:
#
#   1. A sibling worktree running that pkill terminates THIS worktree's run too. Catch2 then prints
#      a plausible partial summary with a fake FAILED and no "with expansion" line -- once reported
#      as a phantom 878-case regression before the exit code 143 was noticed.
#   2. On Apple silicon a plain `cp` of a Mach-O invalidates its ad-hoc code signature and the
#      kernel SIGKILLs the copy with NO output at all (exit 137). Re-signing is not optional.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$root/build/release/tests/avgen_tests"
dst="$root/build/release/tests/avgen_tests_raytrace"
[ -x "$src" ] || { echo "no test binary at $src" >&2; exit 1; }
cp "$src" "$dst"
codesign -f -s - "$dst" 2>/dev/null || true
exec "$dst" "$@"
