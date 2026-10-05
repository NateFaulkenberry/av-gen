#!/usr/bin/env python3
"""table_rep.py <p4-rep.jsonl> -- Phase 4 prototype table: per (size, rep), medians of the per-run p50s."""
import json, sys, collections
rows = collections.defaultdict(list)
for line in open(sys.argv[1]):
    d = json.loads(line)
    r = d["r"]
    if not r: print("FAILED RUN", line.strip()); continue
    rows[(r["size"], r["rep"])].append(r)
def med(v): v = sorted(v); n = len(v); return v[n//2] if n % 2 else 0.5*(v[n//2-1]+v[n//2])
def c(runs, k):
    v = [r[k]["p50"] for r in runs if k in r]
    return "-" if not v else f"{med(v):.3f}" + (f" ({min(v):.2f}-{max(v):.2f})" if len(v) > 1 else "")
print("| world side m | rep | records stored | record MB (CPU) | GPU buffers MB | expand (flatten) ms | upload ms | threads/frame | visible (grass, reeds, mush, spires) | CPU frame ms | GPU compute ms | GPU draw ms | GPU frame ms | footprint ready MB |")
print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
for (size, rep) in sorted(rows, key=lambda k: (k[0], k[1])):
    runs = rows[(size, rep)]; r0 = runs[0]
    if "error" in r0:
        print(f"| {size:,.0f} | {rep} | {r0['records']:,} | {r0['record_bytes']/1048576:.0f} | - | {r0['expand_ms']:.0f} | - | - | **{r0['error']}** | | | | | |"); continue
    print(f"| {size:,.0f} | {rep} | {r0['records']:,} | {r0['cpu_record_bytes']/1048576:.1f} | {r0['gpu_buffer_bytes']/1048576:.1f} | {med([r['expand_ms'] for r in runs]):.0f} | {med([r['record_upload_ms'] for r in runs]):.1f} | {r0['threads_per_frame']:,} | {r0['visible']} | {c(runs,'cpu_ms')} | {c(runs,'gpu_compute_ms')} | {c(runs,'gpu_draw_ms')} | {c(runs,'gpu_frame_ms')} | {max(r['footprint_ready_mb'] for r in runs):.0f} |")
