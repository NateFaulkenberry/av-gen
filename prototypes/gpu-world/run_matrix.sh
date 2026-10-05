#!/bin/bash
# run_matrix.sh <out.jsonl> <scenario> <mesh> <"sizes"> [extra args...]
# One run per (repeat, size, arm), each under tools/gpu-lock.sh; arm order alternates per repeat.
out=$1; scen=$2; mesh=$3; sizes=$4; shift 4
cd "$(dirname "$0")/../.."
BIN=./build/release/prototypes/gpu-world/avgen_gpu_world_bench
for rep in 1 2; do
  if [ $rep = 1 ]; then arms="cpu cpumt cpugrid gpu"; else arms="gpu cpugrid cpumt cpu"; fi
  for n in $sizes; do
    for arm in $arms; do
      # CPU timings are only evidence on a quiet machine: wait for the 1-minute load to fall under 4 (12-core machine).
      while [ "$(sysctl -n vm.loadavg | awk '{print ($2 < 4.0)}')" != 1 ]; do sleep 15; done
      raw=$(tools/gpu-lock.sh $BIN --arm $arm --n $n --scenario $scen --mesh $mesh "$@" 2>>"$out.err")
      code=$?
      line=$(printf '%s\n' "$raw" | grep '^{' | tail -1)
      echo "{\"rep\":$rep,\"exit\":$code,\"load\":\"$(uptime | sed 's/.*averages*: //')\",\"r\":${line:-null}}" >> "$out"
      echo "rep $rep $scen $mesh n=$n $arm exit $code"
    done
  done
done
