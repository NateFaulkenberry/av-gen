#!/bin/bash
# run_conventional.sh <outdir> -- the conventional meadow: production's real flatten (CPU, two repeats) and
# production's real render path (avgen --live-profile, headless, Ultra, under the GPU lock).
out=$1; mkdir -p "$out"
cd "$(dirname "$0")/../../.."
P=$PWD/prototypes/gpu-world/phase4-conventional
quiet() { while [ "$(sysctl -n vm.loadavg | awk '{print ($2 < 4.0)}')" != 1 ]; do sleep 15; done; }
for rep in 1 2; do
  for size in 250 500 1000 2000; do
    for v in "" "-terrain-only"; do
      quiet
      ./build/release/prototypes/gpu-world/avgen_gpu_world_flatten_probe $P/meadow-$size$v.scene.json 2>>"$out/flatten.err" | sed "s/^{/{\"rep\":$rep,\"load\":\"$(sysctl -n vm.loadavg | awk '{print $2}')\",/" >> "$out/flatten.jsonl"
      echo "flatten rep $rep $size$v exit ${PIPESTATUS[0]}"
    done
  done
done
for rep in 1 2; do
  for size in 250 500 1000 2000; do
    for v in "" "-terrain-only"; do
      quiet
      tools/gpu-lock.sh ./build/release/src/avgen --live-profile --project $P/meadow-$size$v.json --quality ultra --start 5 \
        --json "$out/live-$size$v-$rep.json" > "$out/live-$size$v-$rep.txt" 2>>"$out/live.err"
      echo "live rep $rep $size$v exit $?"
    done
  done
done
