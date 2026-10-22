#!/bin/bash
# Living Prism audit, Sub-Agent A: baseline matrix. 4 scenes x {headless ultra, live auto} x 3 reps, interleaved.
cd /Users/natefaulkenberry/Documents/GitHub/av-gen-prism
OUT=~/Desktop/av-gen-review/80-living-prism-audit/A-data/baseline
mkdir -p $OUT
declare -a S=(rw:examples/rainbow-world/rainbow-world.json rift:examples/bioluminescent/rift-live.json mosh:examples/digital-mosh/digital-mosh-live.json eh:examples/sonic-vfx/event-horizon.json)
COMMON="--audio assets/audio/feline-footwear.wav --no-audio --target-fps 30 --size 1920x1080 --start 60 --warmup 5 --measure 30"
for rep in 1 2 3; do
 for mode in headless live; do
  for s in "${S[@]}"; do
   tag=${s%%:*}; proj=${s#*:}
   q=ultra; [ $mode = live ] && q=auto
   name=$tag-$mode-$q-r$rep
   echo "== $name $(date +%T) $(pmset -g batt | head -1) | $(uptime | sed 's/.*load/load/')" | tee -a $OUT/conditions.log
   AVGEN_GPU_LOCK_TIMEOUT=14400 tools/gpu-lock.sh ./build/render/src/avgen --live-profile --mode $mode --project $proj $COMMON --quality $q --json $OUT/$name.json --trace $OUT/$name.csv > $OUT/$name.log 2>&1
   echo "   exit $? $(date +%T) $(pmset -g batt | head -1)" | tee -a $OUT/conditions.log
  done
 done
done
echo DONE
