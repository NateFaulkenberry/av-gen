"""gate.py <tag> [size] : the corruption-off world, one still per tableau + flight frames, one gpu lock."""
import json, os, sys, subprocess, copy, math
R = "/Users/natefaulkenberry/Documents/GitHub/av-gen-mosh"
sys.path.insert(0, R + "/examples/digital-mosh")
import build, forms, tableaux as T
S = os.path.dirname(os.path.abspath(__file__))
tag = sys.argv[1]; size = sys.argv[2] if len(sys.argv) > 2 else "1280x720"
ONLY = sys.argv[3].split(",") if len(sys.argv) > 3 and sys.argv[3] else None
EXTRA = json.loads(sys.argv[4]) if len(sys.argv) > 4 else {}
lin = T.lin
SUN = build.norm([0.86, -0.16, 0.48])        # low, raking from the left
SKY = None

SKY = dict(zenith=lin(EXTRA.get("zenith", "#6f9aa3")), horizon=lin(EXTRA.get("horizon", "#efd9bd")), ground=lin(EXTRA.get("ground", "#41606a")),
           sun=build.light_of("#f1dcc0"))


def scene():
    olive = forms.Olive(seed=7)
    nodes = build.fields() + T.nodes(olive)
    nodes.append(build.clouds())
    cb = build.clouds(); cb["name"] = "cloudsBelow"
    for p in cb["procedural"]["distribution"]["points"]:
        p[1] = -p[1]
    nodes.append(cb)
    nodes.append(T.flight_node())
    sky = {"enabled": True, "background": True, "useKeyLight": False, "sunDirection": build.mul(SUN, -1.0),
           "zenithColor": SKY["zenith"], "horizonColor": SKY["horizon"], "groundColor": SKY["ground"],
           "sunColor": SKY["sun"], "haze": 0.22, "sunIntensity": 1.0, "sunSize": 0.016, "sunGlow": 0.35,
           "intensity": 0.75, "mirror": EXTRA.get("mirror", 0.72)}
    up = [SUN[0], -SUN[1], SUN[2]]
    return {"format": "avgen-scene", "version": 1, "name": "DIGITAL MOSH",
            "camera": {"mode": 1, "spline": "flight", "position": [60, 4, 250], "target": [0, 10, 0], "fov": 40},
            "environment": {"intensity": 0.12, "background": SKY["horizon"], "fogColor": SKY["horizon"],
                            "shadowRange": 260.0, "shadowCascades": 3,
                            "volumeDensity": EXTRA.get("vol", 0.00018), "volumeScattering": 0.9, "volumeAbsorption": 0.1,
                            "volumeAnisotropy": 0.3, "volumeSteps": 24, "volumeMaxDistance": 5000.0,
                            "fogHeight": 0.0, "fogHeightFalloff": 0.004, "fogSky": EXTRA.get("fogSky", 1.0), "fogSkyDistance": 4500.0,
                            "sky": sky},
            "lights": [
                {"name": "sun", "id": "sun", "type": "directional", "role": "key", "direction": SUN,
                 "color": SKY["sun"], "intensity": EXTRA.get("sunI", 10.0), "castsShadow": True, "shadowStrength": 1.0, "softness": 0.35,
                 "shadowBias": 0.012},
                # the sun off the mirror: it lights every underside, as a mirror does
                {"name": "mirrorSun", "id": "mirrorSun", "type": "directional", "role": "fill", "direction": up,
                 "color": SKY["sun"], "intensity": EXTRA.get("upI", 5.5), "castsShadow": False},
            ],
            "grids": [build.CONTAGION],
            "materialPrograms": T.programs(build.bark_program()),
            "nodes": nodes}

VIEWS = {
    # painted compositions: the horizon off centre, the idea off centre, a great deal of emptiness
    "A-mirror": ([40, 2.2, 150], [-46, 6.0, 74], 30),
    "B-eye": ([96, 4.0, 118], [-8, 16, -6], 30),
    "B-eye-high": ([200, 48, 30], [-20, 2, -30], 30),
    "C-hanging": ([-130, 10, -40], [-250, 66, -175], 46),
    "C-under": ([-215, 42, -150], [-260, 62, -185], 70),
    "D-door": ([128, 4.0, -18], [176, 4.0, -68], 40),
    "E-stair": ([-20, 5, 300], [110, 38, 150], 42),
    "F-colossus": ([150, 8, 120], [520, 120, 410], 40),
}

def project(name, cam):
    p = json.load(open(R + "/examples/digital-mosh/digital-mosh.json"))
    p["assets"]["scene"]["path"] = f"{S}/{tag}.scene.json"
    p["assets"]["audio"]["path"] = R + "/assets/audio/feline-footwear.wav"
    p["states"] = {"initial": "x", "states": []}
    p["presets"] = []; p["routes"] = []
    par = {k: v for k, v in p["parameters"].items() if k.startswith("post/") or k.startswith("camera/exposure")}
    for k in list(par):
        if any(x in k for x in ("glitch", "pixel", "poster", "sort", "split")):
            par.pop(k)
    par.update(cam)
    par.update(EXTRA.get("params", {}))
    p["parameters"] = par
    pp = f"{S}/{tag}-{name}.json"; json.dump(p, open(pp, "w")); return pp

json.dump(scene(), open(f"{S}/{tag}.scene.json", "w"))
L = len(T.FLIGHT)
lines = ["#!/bin/bash", f"cd {R}"]
jobs = []
for name, (e, t, fov) in VIEWS.items():
    jobs.append((name, project(name, {"camera/mode": 1, "camera/position": e, "camera/target": t, "camera/fov": fov})))
for i, st in enumerate([0.08, 0.31, 0.55, 0.72]):
    jobs.append((f"fly{i}", project(f"fly{i}", {"camera/mode": 2, "camera/splineT": st, "camera/lookAhead": 34.0,
                                                 "camera/splineBank": 10.0, "camera/fov": 42})))
jobs = [j for j in jobs if ONLY is None or j[0] in ONLY]
for name, pp in jobs:
    d = f"{S}/r-{tag}-{name}"
    lines += [f"rm -rf {d}; mkdir -p {d}",
              f"./build/release/src/avgen --headless --project {pp} --render {d} --format png --range 2:2.02 --fps 50 --size {size} > {d}.log 2>&1",
              f"f=$(ls {d}/*.png 2>/dev/null | head -1); [ -n \"$f\" ] && mv \"$f\" {S}/{tag}-{name}.png && echo ok {name} || echo FAIL {name}"]
open(f"{S}/gate-{tag}.sh", "w").write("\n".join(lines) + "\n"); os.chmod(f"{S}/gate-{tag}.sh", 0o755)
r = subprocess.run([R + "/tools/gpu-lock.sh", f"{S}/gate-{tag}.sh"], capture_output=True, text=True, env=dict(os.environ, AVGEN_GPU_LOCK_TIMEOUT="14400"))
print(r.stdout[-2000:])
from PIL import Image, ImageDraw
names = [n for n, _ in jobs if os.path.exists(f"{S}/{tag}-{n}.png")]
ims = [Image.open(f"{S}/{tag}-{n}.png").resize((640, 360)) for n in names]
cols = 3; rows = (len(ims) + cols - 1) // cols
sh = Image.new("RGB", (640 * cols, 360 * rows))
for i, (im, n) in enumerate(zip(ims, names)):
    sh.paste(im, ((i % cols) * 640, (i // cols) * 360)); ImageDraw.Draw(sh).text(((i % cols) * 640 + 6, (i // cols) * 360 + 4), n, fill=(255, 255, 255))
sh.save(f"{S}/sheet-{tag}.png"); print("sheet", names)
