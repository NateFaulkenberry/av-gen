#!/bin/bash
# run_rep.sh <out.jsonl> <shot> -- Phase 4 matrix: both representations over world sizes, two repeats, under the lock.
out=$1; shot=$2
cd "$(dirname "$0")/../.."
BIN=./build/release/prototypes/gpu-world/avgen_gpu_world_rep
for rep in 1 2; do
  for size in 250 500 1000 2000 4000 16000 1000000; do
    for arm in compact expanded; do
      if [ $arm = expanded ] && [ $size -gt 4000 ]; then continue; fi
      while [ "$(sysctl -n vm.loadavg | awk '{print ($2 < 4.0)}')" != 1 ]; do sleep 15; done
      raw=$(tools/gpu-lock.sh $BIN --rep $arm --size $size --shot $shot 2>>"$out.err")
      code=$?
      line=$(printf '%s\n' "$raw" | grep '^{' | tail -1)
      echo "{\"rep\":$rep,\"exit\":$code,\"load\":\"$(sysctl -n vm.loadavg | awk '{print $2}')\",\"r\":${line:-null}}" >> "$out"
      echo "rep $rep size $size $arm exit $code"
    done
  done
done
