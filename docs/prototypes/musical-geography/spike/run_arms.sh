#!/bin/bash
# Phase 1 spike (throwaway): profile each arm at 1080p Ultra. Run from the repo root, under the GPU lock:
#   for n in 256 512 768; do python3 make_spike.py $n 1 1 $SPIKE/n${n}_sh; python3 make_spike.py $n 0 1 $SPIKE/n${n}_nosh; done
#   SPIKE=<dir> tools/gpu-lock.sh docs/prototypes/musical-geography/spike/run_arms.sh
S="${SPIKE:?set SPIKE to the directory make_spike.py wrote}"
for a in n256_sh n256_nosh n512_sh n512_nosh n768_sh n768_nosh; do
  ./build/release/src/avgen --live-profile --project $S/$a/spike.json --size 1920x1080 --quality ultra --no-audio \
    --warmup 3 --measure 5 --json $S/$a/rec.json --trace $S/$a/trace.csv --capture $S/$a/frame.png > $S/$a/out.txt 2>&1
  echo "$a rc=$?"
done
