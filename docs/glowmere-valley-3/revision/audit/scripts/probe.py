import json, re, subprocess, pathlib
HERE = pathlib.Path(__file__).resolve().parent
WT = pathlib.Path("/Users/natefaulkenberry/Documents/GitHub/av-gen-gv3")
TOOL = WT / "build/release/tools/avgen_world_preview"
WORLD = WT / "build/gv3/world-33541923bb9d94a1.json"
pts = json.loads((HERE / "probe_points.json").read_text())
out = {}
for i in range(0, len(pts), 1500):
    chunk = pts[i:i + 1500]
    args = [a for (x, z) in chunk for a in (f"{x:.3f}", f"{z:.3f}")]
    res = subprocess.run([str(TOOL), str(WORLD), str(HERE / "probe.png"), "16"] + args,
                         capture_output=True, text=True)
    found = re.findall(r"probe \([^)]*\): height (-?[\d.]+) .*? water (\S+)", res.stdout)
    assert len(found) == len(chunk), (len(found), len(chunk), res.stderr[-500:])
    for (x, z), (h, w) in zip(chunk, found):
        out[f"{x:.3f},{z:.3f}"] = [float(h), None if w == "none" else float(w)]
(HERE / "heights.json").write_text(json.dumps(out))
print("probed", len(out))
