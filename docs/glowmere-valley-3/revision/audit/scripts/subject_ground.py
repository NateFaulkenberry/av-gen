"""Split each followed subject's drawn Y into terrain-under-feet and the liveliness stride bob."""
import json, math, pathlib, re, subprocess, sys
import numpy as np
HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import rigs as R
WT = R.WT
TOOL = WT / "build/release/tools/avgen_world_preview"
WORLD = WT / "build/gv3/world-33541923bb9d94a1.json"

def probe(points):
    out = []
    for i in range(0, len(points), 1500):
        chunk = points[i:i + 1500]
        args = [a for (x, z) in chunk for a in (f"{x:.3f}", f"{z:.3f}")]
        res = subprocess.run([str(TOOL), str(WORLD), str(HERE / "probe.png"), "16"] + args,
                             capture_output=True, text=True)
        found = re.findall(r"probe \([^)]*\): height (-?[\d.]+) .*? water (\S+)", res.stdout)
        assert len(found) == len(chunk)
        out += [float(h) for h, w in found]
    return np.array(out)

LIVE = {}
for e in R.SCENE["entities"]:
    for b in e.get("behaviors", []):
        if b["kind"] == "liveliness":
            LIVE[e["name"]] = b
rows = {}
for r in R.rigs():
    name = r["follow"]
    if not name or name not in R.CAST["entities"] or r["end"] - r["start"] < 2.0:
        continue
    ts, P, spd, act = R.track_of(name)
    m = (ts >= r["start"] - 0.5) & (ts < r["end"])
    t, Pm, sp = ts[m], P[m], spd[m]
    h = probe([(float(x), float(z)) for x, z in zip(Pm[:, 0], Pm[:, 2])])
    resid = Pm[:, 1] - h            # bob + grounding band/filter error
    lv = LIVE[name]
    stride = lv.get("stride", 1.6); bounce = lv.get("bounce", 0.0); rate = lv.get("bounceRate", 0.9)
    walk = sp > 2.5
    f_bob = 2 * (3.069 / stride) * rate
    amp_pp = bounce * min(3.069 / stride, 2.0)
    n = len(t)
    spec = np.abs(np.fft.rfft((resid - resid.mean()) * np.hanning(n))) ** 2
    fr = np.fft.rfftfreq(n, 0.05)
    # terrain relief under the feet, high-passed (what grounding passes through to the node)
    def lp(x, s=6):
        rr = int(4 * s); k = np.exp(-0.5 * (np.arange(-rr, rr + 1) / s) ** 2); k /= k.sum()
        pad = np.pad(x, (rr, rr), mode="reflect", reflect_type="odd") if len(x) > rr else np.pad(x, (rr, rr), mode="edge")
        return np.convolve(pad, k, mode="same")[rr:-rr]
    terr_hf = h - lp(h)
    bob_hf = resid - lp(resid)
    rows[r["slug"]] = dict(subject=name, walk_frac=float(walk.mean()),
                           resid_min_cm=100 * float(resid[walk].min()) if walk.any() else None,
                           resid_max_cm=100 * float(resid[walk].max()) if walk.any() else None,
                           resid_fdom=float(fr[int(np.argmax(spec[1:]) + 1)]),
                           model_f_bob=f_bob, model_pp_cm=100 * amp_pp,
                           terrainHF_cm=100 * float(np.sqrt(np.mean(terr_hf ** 2))),
                           bobHF_cm=100 * float(np.sqrt(np.mean(bob_hf ** 2))))
    print(r["slug"], json.dumps({k: (round(v, 3) if isinstance(v, float) else v) for k, v in rows[r["slug"]].items()}))
(HERE / "subject_ground.json").write_text(json.dumps(rows, indent=1))
