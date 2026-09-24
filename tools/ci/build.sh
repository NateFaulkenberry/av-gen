#!/bin/bash
# Configure and build one CMake preset on a CI runner, and record what was built with what.
#
#   tools/ci/build.sh <preset> <out-dir> [extra cmake -D args...]
#
# Uses the repository's own presets (CMakePresets.json); the only thing CI adds is ccache as the
# compiler launcher, so a warm runner does not recompile unchanged translation units. Writes:
#
#   <out-dir>/configure.log, <out-dir>/build.log   full logs (uploaded as the build-logs artifact)
#   <out-dir>/meta.json                             toolchain, timings, cache state (read by the report)
#
# Every exit status is read off the command itself, never off a pipe (docs/testing.md entry 31):
# `set -o pipefail` plus PIPESTATUS, because `cmake --build ... | tee` would otherwise report tee's 0.
set -uo pipefail
PRESET=${1:?usage: build.sh <preset> <out-dir> [cmake args...]}
OUT=${2:?usage: build.sh <preset> <out-dir> [cmake args...]}
shift 2
mkdir -p "$OUT"

LAUNCHER=()
if command -v ccache >/dev/null; then
    LAUNCHER=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
              -DCMAKE_OBJC_COMPILER_LAUNCHER=ccache -DCMAKE_OBJCXX_COMPILER_LAUNCHER=ccache)
    ccache --zero-stats >/dev/null
fi

t0=$(date +%s)
echo "::group::configure ($PRESET)"
cmake --preset "$PRESET" "${LAUNCHER[@]}" "$@" 2>&1 | tee "$OUT/configure.log"
configure_rc=${PIPESTATUS[0]}
echo "::endgroup::"
t1=$(date +%s)

build_rc=1
if [ "$configure_rc" -eq 0 ]; then
    echo "::group::build ($PRESET) -- warnings are in the build-logs artifact"
    # -k 0: keep going after the first failed translation unit, so one run reports every compile
    # error instead of one per push.
    cmake --build --preset "$PRESET" -- -k 0 2>&1 | tee "$OUT/build.log"
    build_rc=${PIPESTATUS[0]}
    echo "::endgroup::"
fi
t2=$(date +%s)

status=pass
[ "$configure_rc" -ne 0 ] && status="configure failed"
[ "$configure_rc" -eq 0 ] && [ "$build_rc" -ne 0 ] && status="build failed"

ccache_line="not used"
if [ ${#LAUNCHER[@]} -gt 0 ]; then
    echo "::group::ccache statistics"
    ccache --show-stats -v || true
    echo "::endgroup::"
    ccache_line=$(ccache --print-stats 2>/dev/null | awk -F'\t' '
        $1=="direct_cache_hit"||$1=="preprocessed_cache_hit"{h+=$2}
        $1=="cache_miss"{m=$2} END{printf "%d hits / %d misses", h, m}')
fi

python3 - "$OUT/meta.json" <<PY
import json, subprocess, sys
def sh(c):
    try: return subprocess.run(c, shell=True, capture_output=True, text=True).stdout.strip()
    except Exception: return "?"
meta = {"build": {
    "status": "$status", "preset": "$PRESET",
    "runner": "${RUNNER_NAME:-local} (${ImageOS:-?} image ${ImageVersion:-?})",
    "macos": sh("sw_vers -productVersion"), "arch": sh("uname -m"),
    "xcode": sh("xcodebuild -version | head -1 | cut -d' ' -f2"), "sdk": sh("xcrun --show-sdk-version"),
    "compiler": sh("clang --version | head -1"), "cmake": sh("cmake --version | head -1 | cut -d' ' -f3"),
    "configure_seconds": $((t1 - t0)), "build_seconds": $((t2 - t1)),
    "ccache": "$ccache_line", "cpm_cache": "${CPM_CACHE_STATE:-unknown}",
    "warnings": int(sh("grep -c 'warning:' '$OUT/build.log' 2>/dev/null") or 0),
}}
json.dump(meta, open(sys.argv[1], "w"), indent=2)
print(json.dumps(meta, indent=2))
PY

if [ "$status" != pass ]; then
    # Actionable, not "exit code 1": the first compiler/linker errors, as annotations and in the
    # step summary. The full log is in the build-logs artifact.
    LOG="$OUT/build.log"; [ "$configure_rc" -ne 0 ] && LOG="$OUT/configure.log"
    {
        echo "## Build: FAIL ($status, preset \`$PRESET\`)"
        echo
        echo '```'
        grep -E 'error:|error [A-Z][0-9]+|CMake Error|undefined symbol|ld: |FAILED: ' "$LOG" | grep -v 'warning:' | head -40
        echo '```'
    } >> "${GITHUB_STEP_SUMMARY:-/dev/stdout}"
    grep -E '^[^ ]+:[0-9]+:[0-9]+: error: ' "$LOG" | head -20 | while IFS= read -r line; do
        f=${line%%:*}; rest=${line#*:}; l=${rest%%:*}
        echo "::error file=${f#"${GITHUB_WORKSPACE:-}/"},line=$l,title=compile error::${line#*error: }"
    done
    exit 1
fi
echo "configure $((t1 - t0)) s, build $((t2 - t1)) s, ccache: $ccache_line"
