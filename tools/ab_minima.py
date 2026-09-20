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
    # How much to trust it, and the statistic here is deliberately NOT max - min over the blocks.
    #
    # Taking the minimum over blocks means contention can only ever make an arm look SLOWER: a
    # disturbed block raises its own minimum and is then discarded by the outer `min`. So one bad
    # block among four does not corrupt the answer, and a spread that max-minus-min reports as
    # 18 ms can sit on top of three blocks that agree to 0.06. That happened, and reading the
    # spread as "this run is worthless" threw away a good measurement.
    #
    # What does matter is whether the LOWEST TWO blocks of each arm agree. If they do, the floor
    # was reached twice independently and is real; if only one block is low, it might itself be an
    # artefact and there is nothing to corroborate it.
    def floorGap(v):
        lo = sorted(v)
        return lo[1] - lo[0] if len(lo) > 1 else float("inf")
    gap = max(floorGap(av), floorGap(bv))
    verdict = "corroborated" if gap < 0.35 else "NOT CORROBORATED -- the floor was reached once"
    print(f"  delta on minima  {min(av) - min(bv):+7.3f} ms   "
          f"(lowest two blocks agree to {gap:.3f} ms -- {verdict})")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
