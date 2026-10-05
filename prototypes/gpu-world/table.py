#!/usr/bin/env python3
"""table.py <run.jsonl> -- markdown table: per (n, arm), p50 over repeats of each run's p50; spread = min..max of run p50s."""
import json, sys, collections
rows = collections.defaultdict(list)
for line in open(sys.argv[1]):
    d = json.loads(line)
    if d["exit"] != 0 or not d["r"]:
        print("FAILED RUN:", line.strip()); continue
    r = d["r"]; rows[(r["n"], r["arm"])].append((r, d["load"]))
def med(xs):
    xs = sorted(xs); return xs[len(xs)//2] if len(xs) % 2 else 0.5*(xs[len(xs)//2-1]+xs[len(xs)//2])
def cell(runs, key, q="p50"):
    v = [r[key][q] for r, _ in runs if r[key]["n"] > 0]
    if not v: return "-"
    return f"{med(v):.3f}" + (f" ({min(v):.2f}-{max(v):.2f})" if len(v) > 1 else "")
print("| N | arm | visible | CPU frame ms | CPU update ms | upload ms | encode+submit ms | GPU frame ms | GPU compute ms | frame interval ms | upload B/frame | CPU state MB | footprint MB | RSS growth MB |")
print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
for (n, arm) in sorted(rows, key=lambda k: (k[0], ["cpu","cpumt","cpugrid","gpu"].index(k[1]))):
    runs = rows[(n, arm)]; r0 = runs[0][0]
    es = med([r["encode_ms"]["p50"] + r["submit_ms"]["p50"] for r, _ in runs])
    print(f"| {n:,} | {arm}{'' if arm!='cpumt' else '('+str(r0['threads'])+')'} | {r0['visible']:,} | {cell(runs,'cpu_ms')} | {cell(runs,'update_ms')} | {cell(runs,'upload_ms')} | {es:.3f} | {cell(runs,'gpu_frame_ms')} | {cell(runs,'gpu_compute_ms')} | {cell(runs,'interval_ms')} | {r0['upload_bytes_per_frame']:,} | {r0['cpu_state_bytes']/1048576:.1f} | {max(r['footprint_mb'] for r,_ in runs):.0f} | {max(r['footprint_growth_mb'] for r,_ in runs):.1f} |")
print()
print("loads:", sorted(set(l.split()[0] for rs in rows.values() for _, l in rs)))
