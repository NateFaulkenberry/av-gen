# Summarise avgen --live-profile JSON records as one markdown table row each (stems-multichannel M0/M8).
#   python3 tools/stems_bench/summarize.py build/bench/m0/*.json
import json, sys
print("| run | scene | level (end) | frame p50 / p99 ms | cpu work p50 / p99 ms | gpu p50 / p95 / p99 ms | misses / frames |")
print("|---|---|---|---|---|---|---|")
for p in sys.argv[1:]:
    d = json.load(open(p)); f = d['frame']; c = d['conditions']
    g = f['gpuMs']; fm = f['frameMs']; cw = f['cpuWorkMs']; b = f['budget']
    print(f"| {p.split('/')[-1]} | {c['scene'].split('/')[-1]} | {c['quality']['level']} | {fm['p50']:.1f} / {fm['p99']:.1f} | {cw['p50']:.2f} / {cw['p99']:.2f} | {g['p50']:.1f} / {g['p95']:.1f} / {g['p99']:.1f} | {b['deadlineMisses']} / {b['frames']} |")
