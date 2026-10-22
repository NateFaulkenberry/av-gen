#!/bin/bash
# Living Prism audit, Sub-Agent A: measured lever savings (the profiler's own interleaved A/B), headless Ultra 1080p.
cd /Users/natefaulkenberry/Documents/GitHub/av-gen-prism
OUT=~/Desktop/av-gen-review/80-living-prism-audit/A-data/verify
mkdir -p $OUT
A="--audio assets/audio/feline-footwear.wav --no-audio --size 1920x1080 --target-fps 30 --start 60 --warmup 5 --measure 10 --quality ultra --verify-candidates 5"
for s in rw:examples/rainbow-world/rainbow-world.json rift:examples/bioluminescent/rift-live.json mosh:examples/digital-mosh/digital-mosh-live.json eh:examples/sonic-vfx/event-horizon.json; do
 tag=${s%%:*}; proj=${s#*:}
 echo "== $tag $(date +%T) $(pmset -g batt | head -1) | $(uptime | sed 's/.*load/load/')" | tee -a $OUT/conditions.log
 AVGEN_GPU_LOCK_TIMEOUT=14400 tools/gpu-lock.sh ./build/render/src/avgen --live-profile --mode headless --project $proj $A --json $OUT/$tag.json > $OUT/$tag.log 2>&1
 echo "   exit $? $(date +%T) $(pmset -g batt | head -1)" | tee -a $OUT/conditions.log
done
echo DONE
