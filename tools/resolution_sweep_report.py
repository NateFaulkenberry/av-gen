#!/usr/bin/env python3
"""Summarise a tools/resolution_sweep.sh directory: cost per pass against pixel count.

ADR-170: the minimum of each run and then the minimum over repeats, never the mean. The medians
are printed beside them so a reader can see how much the machine moved; if the minima and the
medians tell different stories, the medians are the ones that are wrong.

The last column is the one the whole experiment is for: the local log-log slope of GPU cost
against pixel count. 1.0 is a perfectly fill-bound frame -- halve the pixels, halve the time.
0.0 is a frame whose cost does not depend on resolution at all, and for which dynamic resolution
buys exactly nothing.
"""
import json, glob, math, os, sys, statistics, collections

def main(d):
    runs = collections.defaultdict(list)
    passes = collections.defaultdict(lambda: collections.defaultdict(list))
    for f in sorted(glob.glob(os.path.join(d, "*.json"))):
        rec = json.load(open(f))["records"][0]
        c = rec["conditions"]
        px = c["width"] * c["height"] / 1e6
        key = (round(px, 4), c["width"], c["height"])
        runs[key].append((rec["gpuMs"]["min"], rec["gpuMs"]["p50"],
                          rec["wallMs"]["min"], rec["wallMs"]["p50"],
                          rec["cpuFrameMs"]["p50"] if "cpuFrameMs" in rec else float("nan")))
        for p in rec.get("gpuPassMedianMs", []):
            passes[key][p["label"]].append(p["medianMs"])
    if not runs:
        print(f"no records under {d}")
        return 1
    keys = sorted(runs, reverse=True)
    print(f"{'target':>12} {'Mpx':>6} {'n':>2} {'gpu min':>8} {'spread':>7} {'gpu p50':>8} "
          f"{'wall min':>9} {'wall p50':>9} {'cpu p50':>8} {'slope':>6}")
    prev = None
    for k in keys:
        px, w, h = k
        v = runs[k]
        gmin = min(x[0] for x in v)
        spread = max(x[0] for x in v) - gmin
        gmed = statistics.median(x[1] for x in v)
        wmin = min(x[2] for x in v)
        wmed = statistics.median(x[3] for x in v)
        cmed = statistics.median(x[4] for x in v)
        slope = ""
        if prev is not None:
            ppx, pg = prev
            if ppx > 0 and px > 0 and pg > 0 and gmin > 0 and ppx != px:
                slope = f"{math.log(pg / gmin) / math.log(ppx / px):6.2f}"
        prev = (px, gmin)
        print(f"{w:>5}x{h:<6} {px:6.2f} {len(v):>2} {gmin:8.2f} {spread:7.2f} {gmed:8.2f} "
              f"{wmin:9.2f} {wmed:9.2f} {cmed:8.2f} {slope:>6}")

    # Per pass: the minimum over repeats at the largest and smallest target, and the slope between.
    big, small = keys[0], keys[-1]
    labels = sorted(set(passes[big]) | set(passes[small]),
                    key=lambda L: -min(passes[big].get(L, [0])))
    print(f"\nper-pass GPU median (minimum over repeats), {big[1]}x{big[2]} vs {small[1]}x{small[2]}:")
    print(f"{'pass':<22} {'big ms':>8} {'small ms':>9} {'saved':>7} {'slope':>6}")
    ratio = math.log(big[0] / small[0])
    for L in labels:
        a = min(passes[big].get(L, [0.0])) or 0.0
        b = min(passes[small].get(L, [0.0])) or 0.0
        s = f"{math.log(a / b) / ratio:6.2f}" if a > 0 and b > 0 else "     -"
        print(f"{L:<22} {a:8.2f} {b:9.2f} {a - b:7.2f} {s:>6}")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
