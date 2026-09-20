#!/usr/bin/env python3
"""Read an --ab --bench-json record and report the delta on per-block GPU MINIMA.

ADR-170: minima over repeats, never means. The A/B run's own headline is a median, which is the
right statistic on a quiet machine and the wrong one on a shared one -- this machine runs several
agents, and a CPU suite in a sibling worktree saturates the unified memory bus that the GPU shares,
which inflates every median while leaving the least-disturbed frame in each block alone.

So this takes the minimum frame of each block, then the minimum over blocks per arm, and subtracts.
It also prints the per-block minima so that a run where even the minima drifted is visible rather
than averaged away.
"""
import json, sys

def main(path):
    d = json.load(open(path))
    arms = {}
    for rec in d.get("records", []):
        arm = rec.get("conditions", {}).get("arm") or "?"
        arms.setdefault(arm, []).append(rec["gpuMs"]["min"])
    if len(arms) != 2:
        print(f"{path}: expected two arms, found {sorted(arms)}")
        return 1
    (an, av), (bn, bv) = sorted(arms.items(), key=lambda kv: -min(kv[1]))
    print(f"  {an:<18} min {min(av):7.3f}  blocks {' '.join(f'{x:.2f}' for x in sorted(av))}")
    print(f"  {bn:<18} min {min(bv):7.3f}  blocks {' '.join(f'{x:.2f}' for x in sorted(bv))}")
    # The spread of the minima is the honest floor: if the least-disturbed frame of each block
    # varies, the machine moved under the measurement and the delta is worth that much less.
    spread = max(max(av) - min(av), max(bv) - min(bv))
    print(f"  delta on minima  {min(av) - min(bv):+7.3f} ms   (worst spread within an arm {spread:.3f} ms)")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
