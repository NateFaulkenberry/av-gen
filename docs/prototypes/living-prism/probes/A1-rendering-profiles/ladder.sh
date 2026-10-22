#!/bin/bash
# Living Prism audit, Sub-Agent A: adaptive quality tests.
cd /Users/natefaulkenberry/Documents/GitHub/av-gen-prism
OUT=~/Desktop/av-gen-review/80-living-prism-audit/A-data/ladder
mkdir -p $OUT
A="--audio assets/audio/feline-footwear.wav --no-audio --size 1920x1080"
run() { name=$1; shift
  echo "== $name $(date +%T) $(pmset -g batt | head -1) | $(uptime | sed 's/.*load/load/')" | tee -a $OUT/conditions.log
  AVGEN_GPU_LOCK_TIMEOUT=14400 tools/gpu-lock.sh ./build/render/src/avgen --live-profile "$@" > $OUT/$name.log 2>&1
  echo "   exit $? $(date +%T) $(pmset -g batt | head -1)" | tee -a $OUT/conditions.log; }
# L1: a target the scene cannot hold at Ultra (60 fps on a 60 Hz display): does the ladder settle or oscillate? 90 s.
for r in 1 2; do
 run rw-live-60-r$r --mode live --project examples/rainbow-world/rainbow-world.json $A --target-fps 60 --start 60 --warmup 5 --measure 90 --quality auto --json $OUT/rw-live-60-r$r.json --trace $OUT/rw-live-60-r$r.csv
done
# L3: what each level does to the picture, same piece time, pinned levels, headless
for s in rw:examples/rainbow-world/rainbow-world.json rift:examples/bioluminescent/rift-live.json; do
 tag=${s%%:*}; proj=${s#*:}
 for q in ultra medium low emergency; do
  run $tag-pin-$q --mode headless --project $proj $A --target-fps 30 --start 75 --warmup 3 --measure 3 --quality $q --json $OUT/$tag-pin-$q.json --capture $OUT/$tag-pin-$q.png
 done
done
echo DONE
