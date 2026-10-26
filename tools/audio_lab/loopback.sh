#!/bin/bash
# The Audio Response Lab on the LIVE path (ADR-1725): a fixture played into BlackHole, AV Gen capturing it live.
#
#   tools/audio_lab/loopback.sh <fixture-dir> <out-dir> [build-dir]
#
# ADR-1605's rig with a fixture instead of transients. AV Gen runs the lab project as `--live-profile --mode live`
# (the projection window at 1920x1080, the 30 fps target) with BlackHole as its live input, writing --sonic-live-log
# with every route final; once the log shows 15 s of frames, `avgen_sonic_probe file` plays the fixture's mix (mono)
# into BlackHole and logs the host time of its first sample. Then `lab.py loopback` joins the two logs on the host clock
# and scores the fixture against its manifest, as the offline and replay paths are scored.
#
# A real-device measurement (ADR-1550): opt-in, never part of a suite; needs the BlackHole 2ch driver; the whole run
# holds the GPU lock. Logs the power source (the owner's rule: every perf run says it).
set -u
FIXTURE=$(cd "$1" && pwd)
OUT=$2
BIN=${3:-build/release}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
cd "$ROOT"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
DUR=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['durationSeconds'])" "$FIXTURE/manifest.json")
MEASURE=$(python3 -c "print(int(float('$DUR') + 30))")
python3 - "$ROOT" "$OUT/project.json" <<'EOF'
import json, sys
root, out = sys.argv[1], sys.argv[2]
p = json.load(open(f"{root}/examples/audio-response-lab/audio-response-lab.json"))
p["assets"]["scene"]["path"] = f"{root}/examples/audio-response-lab/" + p["assets"]["scene"]["path"]
p["sonic"]["live"] = True  # live input: no audio file, the capture is the music
json.dump(p, open(out, "w"), indent=1)
EOF
SETTINGS="$HOME/Library/Application Support/avgen/avgen/settings.json"
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$OUT/settings.before.json"
cat > "$OUT/run.sh" <<EOF
cd "$ROOT"
"$BIN/src/avgen" --live-profile --mode live --project "$OUT/project.json" --target-fps 30 --size 1920x1080 \
    --quality auto --warmup 5 --measure $MEASURE --json "$OUT/profile.json" --trace "$OUT/profile.csv" \
    --live --input BlackHole --sonic-live-log "$OUT/live.csv" --trace-finals > "$OUT/editor.log" 2>&1 &
APP=\$!
for i in \$(seq 1 600); do
    [ -f "$OUT/live.csv" ] && [ \$(wc -l < "$OUT/live.csv") -gt 450 ] && break
    kill -0 \$APP 2>/dev/null || break
    sleep 1
done
"$BIN/tools/avgen_sonic_probe" file --file "$FIXTURE/mix.wav" --out "$OUT/probe.csv" --wav "$OUT/probe.wav" \
    --device BlackHole --lead-in 1 > "$OUT/probe.log" 2>&1
for i in \$(seq 1 240); do kill -0 \$APP 2>/dev/null || break; sleep 1; done
kill -TERM \$APP 2>/dev/null || true
wait \$APP 2>/dev/null || true
EOF
echo "== $(date +%T) $(pmset -g batt | head -1)" | tee "$OUT/conditions.log"
AVGEN_GPU_LOCK_TIMEOUT=${AVGEN_GPU_LOCK_TIMEOUT:-14400} "$ROOT/tools/gpu-lock.sh" bash "$OUT/run.sh"
echo "   done $(date +%T) $(pmset -g batt | head -1)" | tee -a "$OUT/conditions.log"
if [ -f "$OUT/settings.before.json" ] && ! cmp -s "$SETTINGS" "$OUT/settings.before.json"; then
    cp "$SETTINGS" "$OUT/settings.after-run.json"
    cp "$OUT/settings.before.json" "$SETTINGS"
    echo "   the run changed the settings file; put back"
fi
python3 tools/audio_lab/lab.py loopback "$FIXTURE" "$OUT"
