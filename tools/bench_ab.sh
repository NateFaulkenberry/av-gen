#!/bin/sh
# Interleaved per-frame benchmark (ADR-051).
#
# Two things this does that the old whole-process timing did not, both learned the hard way:
#
#  1. It measures frames, not the process. `/usr/bin/time` on one run charges glTF decode, scatter
#     placement and terrain build to the frames. Eleven scatter layers take two seconds longer to
#     load than none, which over a 100-frame run is 20 ms a frame of nothing -- almost exactly the
#     "1.5 ms per scatter layer" that was believed to be a draw cost. avgen now reports the median
#     of its own per-frame wall clock, after a warm-up, which cannot contain anything that happens
#     once.
#  2. It interleaves. The machine drifts and other work lands on it; two sequential runs of one
#     binary have been measured 11 ms apart. Every configuration is run once per round, the rounds
#     repeat, and the median over rounds is the answer.
#
# A binary may be given as `path` or `path@shaderdir`. Shaders are read from the working tree at
# run time, so two binaries whose shaders differ cannot be compared without pinning each to its own
# copy -- the baseline otherwise runs the new shader, which is how an afternoon was spent chasing a
# rendering bug that did not exist.
#
# Scenes are the variants tools/make_bench_scenes.py writes into examples/world/_bench.
#
#   tools/bench_ab.sh REPS SIZE FRAMES "binA[@dir] binB[@dir] ..." "scene1 scene2 ..."
set -e
REPS=${1:-5}; SIZE=${2:-2880x1800}; FRAMES=${3:-80}; BINS=${4:?binaries}; SCENES=${5:?scenes}
[ -d examples/world/_bench ] || { echo "run tools/make_bench_scenes.py first"; exit 1; }
OUT=$(mktemp -d); : > "$OUT/raw"
for r in $(seq 1 "$REPS"); do
  for s in $SCENES; do
    for b in $BINS; do
      case "$b" in *@*) BIN=${b%%@*}; export AVGEN_SHADER_DIR=${b##*@};; *) BIN=$b; unset AVGEN_SHADER_DIR;; esac
      M=$("$BIN" --headless --composition "examples/world/_bench/$s.scene.json" --frames "$FRAMES" \
            --fps 30 --size "$SIZE" --log info 2>&1 |
          sed -nE 's/.*frame wall clock.*median ([0-9.]+) ms.*/\1/p')
      echo "$b $s ${M:-nan}" >> "$OUT/raw"
    done
  done
done
python3 - "$OUT/raw" <<'PY'
import sys, statistics, collections
ms = collections.defaultdict(list)
for line in open(sys.argv[1]):
    b, s, v = line.split()
    ms[(b, s)].append(float(v))
print(f"{'binary':30s} {'scene':6s} {'ms/frame':>9s}   samples")
for k, v in sorted(ms.items()):
    print(f"{k[0][-30:]:30s} {k[1]:6s} {statistics.median(v):9.2f}   {v}")
PY
rm -rf "$OUT"
