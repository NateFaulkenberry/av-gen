#!/usr/bin/env bash
# The live performance regression check (ADR-1608, T9): measure the reference live scenes and judge them against
# docs/perf/live-baseline.json. Exit 0 pass, 1 fail (a regression or a lost threshold), 2 nothing to judge.
#
#   tools/ci/live-perf.sh [--out <dir>] [--bin <dir>] [--reps <n>] [--scenes a,b] [--check-only]
#
# Local, on the owner's machine, on AC power: not a hosted job, and not on the parked self-hosted runner either
# (docs/development/gpu-ci-private-assets.md). It needs what neither repository may hold: assets/audio/
# feline-footwear.wav (the analysis track) and the generated scenes (examples/rainbow-world/build.py,
# examples/bioluminescent/build.py). Every avgen run takes tools/gpu-lock.sh itself; about 20 minutes for 3 reps.
# Change the baseline only on purpose: `python3 tools/live_perf_baseline.py write --out <a run you reviewed>`.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"
out="ci-results/live-perf"; bin="build/release"; reps=3; scenes=""; check_only=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        --out) out="$2"; shift ;;
        --bin) bin="$2"; shift ;;
        --reps) reps="$2"; shift ;;
        --scenes) scenes="$2"; shift ;;
        --check-only) check_only=true ;;
        *) echo "live-perf: unknown argument '$1'" >&2; exit 2 ;;
    esac
    shift
done
if ! pmset -g batt | head -1 | grep -q "AC Power"; then
    echo "live-perf: not on AC power ($(pmset -g batt | head -1)); timings on battery are not evidence" >&2
    exit 2
fi
if [[ ! -f assets/audio/feline-footwear.wav ]]; then
    echo "live-perf: assets/audio/feline-footwear.wav is missing (local only, never committed)" >&2
    exit 2
fi
if [[ "$check_only" == false ]]; then
    rm -rf "$out"
    python3 tools/live_perf_baseline.py run --out "$out" --bin "$bin" --reps "$reps" ${scenes:+--scenes "$scenes"} \
        || echo "live-perf: a run exited non-zero (the check below fails it)"
fi
set +e
python3 tools/live_perf_baseline.py check --out "$out" ${scenes:+--scenes "$scenes"} | tee "$out/check.txt"
rc=${PIPESTATUS[0]}
echo "live-perf: exit $rc"
exit "$rc"
