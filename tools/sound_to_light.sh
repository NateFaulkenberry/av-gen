#!/bin/bash
# Sound to light (ADR-1605): a live run of a project on BlackHole loopback, the probe's transients played into it.
#
#   [EXTRA_ARGS="--live-vsync-align off"] tools/sound_to_light.sh <project.json> <out-dir> [build-dir] [probe-period]
#
# A real-device measurement (ADR-1550): opt-in, never part of a suite; needs the BlackHole 2ch driver and holds the GPU
# lock for the whole run. AV Gen runs the project as `--live-profile --mode live` does (the projection window at
# 1920x1080, the 30 fps target, the Auto ladder) with BlackHole as its live input (channel 2 also as the drum stem),
# writing --sonic-live-log with every route final; the probe starts once the log shows 15 s of frames.
#
# With a period the probe opens BlackHole first, at that buffer, and AV Gen's capture joins a running device (ADR-1550's
# courtesy decides its buffer; the editor log says what it got). Without one AV Gen opens BlackHole first (128 frames
# when it is idle) and the probe joins with the courteous size. Core Audio keeps a buffer size per process, and the
# analysis corrects for the probe's own period (sound_to_light.py). The app's settings file is put back if a run
# changed it. Results: result.txt / result.json in <out-dir>; pool runs with `sound_to_light.py analyse <dir>...`.
set -u
PROJECT=$1
OUT=$2
BIN=${3:-build/release}
PERIOD=${4:-}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
cd "$ROOT"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
TWIN=$(python3 tools/sound_to_light.py prepare "$PROJECT") || exit 1
SETTINGS="$HOME/Library/Application Support/avgen/avgen/settings.json"
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$OUT/settings.before.json"

if [ -n "$PERIOD" ]; then
    PROBE_FIRST=1
    LEADIN=60   # the probe holds BlackHole while AV Gen loads; its scenario starts after the lead-in
else
    PROBE_FIRST=0
    LEADIN=1
fi
cat > "$OUT/run.sh" <<EOF
cd "$ROOT"
probe() {
    "$BIN/tools/avgen_sonic_probe" transients --out "$OUT/probe.csv" --wav "$OUT/probe.wav" --device BlackHole \
        --lead-in $LEADIN ${PERIOD:+--period $PERIOD} > "$OUT/probe.log" 2>&1
}
if [ $PROBE_FIRST = 1 ]; then probe & PROBE=\$!; sleep 2; fi
"$BIN/src/avgen" --live-profile --mode live --project "$TWIN" --target-fps 30 --size 1920x1080 --quality auto \
    --warmup 5 --measure 90 --json "$OUT/profile.json" --trace "$OUT/profile.csv" \
    --live --input BlackHole --input-sources drums@2:drums --sonic-live-log "$OUT/live.csv" --trace-finals ${EXTRA_ARGS:-} \
    > "$OUT/editor.log" 2>&1 &
APP=\$!
if [ $PROBE_FIRST = 0 ]; then
    for i in \$(seq 1 600); do   # up to 10 minutes for a load and a cold shader cache
        [ -f "$OUT/live.csv" ] && [ \$(wc -l < "$OUT/live.csv") -gt 450 ] && break
        kill -0 \$APP 2>/dev/null || break
        sleep 1
    done
    probe
else
    wait \$PROBE
fi
# the profile ends on its own after its measured window (which outlasts the probe) and writes its record
for i in \$(seq 1 180); do kill -0 \$APP 2>/dev/null || break; sleep 1; done
kill -TERM \$APP 2>/dev/null || true
wait \$APP 2>/dev/null || true
EOF
echo "== $(date +%T) $(pmset -g batt | head -1)" | tee "$OUT/conditions.log"
AVGEN_GPU_LOCK_TIMEOUT=${AVGEN_GPU_LOCK_TIMEOUT:-14400} "$ROOT/tools/gpu-lock.sh" bash "$OUT/run.sh"
echo "   done $(date +%T) $(pmset -g batt | head -1)" | tee -a "$OUT/conditions.log"
rm -f "$TWIN"
if [ -f "$OUT/settings.before.json" ] && ! cmp -s "$SETTINGS" "$OUT/settings.before.json"; then
    cp "$SETTINGS" "$OUT/settings.after-run.json"
    cp "$OUT/settings.before.json" "$SETTINGS"
    echo "   the run changed the settings file; put back (the changed copy: $OUT/settings.after-run.json)"
fi
grep -E "probe:" "$OUT/probe.log" | tee -a "$OUT/conditions.log"
python3 tools/sound_to_light.py analyse "$OUT" --json "$OUT/result.json" | tee "$OUT/result.txt"
