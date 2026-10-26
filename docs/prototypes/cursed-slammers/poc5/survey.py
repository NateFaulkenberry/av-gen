"""Cursed Slammers POC 5: one pass of POC 4's four surface treatments (A holofoil engraving, B intaglio, C iridescent
chrome, D hybrid security foil) on other subjects: three existing models and four procedural objects. The programs
are POC 4's, unchanged except one prepended rescale of the object-space position (so about 100 engraved lines span
each subject whatever its size). One camera, lighting and exposure per subject; a slow +-20 degree orbit.

    python3 survey.py --out <dir>          # writes <subject>-<variant>.json for every subject and variant

Nothing is registered. The existing models: the Talking Heads bust (ours, committed), and two third-party assets
used LOCALLY only (assets/farm/horse.glb, assets/musicians/astronaut_keys.glb: never committed, never copied).
"""
import argparse
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "../../../.."))
sys.path.insert(0, REPO)
sys.path.insert(0, os.path.join(HERE, "..", "poc4"))
from tools.sonic_vfx import kit  # noqa: E402
from tools.sonic_vfx.kit import sd_cyl, sd_move, sd_rbox, sd_rot, sd_sdiff, sd_smooth, sd_sphere, sd_torus, sd_union  # noqa: E402,E501
import variants as V  # noqa: E402

MESHES = os.path.join(REPO, "examples", "sonic-abstract", "meshes")
ASSETS = os.path.join(REPO, "assets")


def rescaled(prog, unit):
    """POC 4's program with the object-space position divided by `unit` first (one constant, one multiply)."""
    ops = list(prog["ops"])
    assert ops[0]["kind"] == "input" and ops[0]["input"] == "localPosition" and ops[0]["dst"] == 0
    k = 1.0 / unit
    ops[1:1] = [V.C(1, k, k, k, 1.0), V.op("multiply", 0, a=0, b=1)]
    assert len(ops) <= 64, len(ops)
    p = dict(prog)
    p["ops"] = ops
    return p


def medallion():
    disc = sd_rot([90, 0, 0], sd_cyl(0.5, 0.06))
    rim = sd_torus(0.47, 0.03)                                        # in the XY plane after the rotation below
    rim = sd_rot([90, 0, 0], rim)
    boss = sd_move([0, 0, 0.0], sd_sphere(0.15))
    # six arms placed explicitly (one rotation about the view axis each)
    arms = [sd_rot([0, 0, 60.0 * i], sd_move([0, 0.27, 0.035], sd_rbox([0.035, 0.09, 0.02], 0.012))) for i in range(6)]
    return sd_smooth(0.02, disc, rim, boss, sd_union(*arms))


def mask():
    cranium = sd_sphere(0.42)
    brow = sd_move([0, 0.06, 0.28], sd_rbox([0.30, 0.05, 0.10], 0.05))
    cheeks = sd_move([0, -0.12, 0.22], sd_rbox([0.28, 0.10, 0.12], 0.08))
    jaw = sd_move([0, -0.32, 0.12], sd_rbox([0.20, 0.10, 0.16], 0.08))
    face = sd_smooth(0.08, cranium, brow, cheeks, jaw)
    sockets = sd_union(sd_move([0.15, -0.02, 0.36], sd_sphere(0.11)), sd_move([-0.15, -0.02, 0.36], sd_sphere(0.11)))
    nose = sd_move([0, -0.16, 0.40], sd_sphere(0.06))
    return sd_sdiff(0.03, face, sockets, nose)


# name: (kind, spec, unit, camera distance, target, why)
SUBJECTS = {
    "bust": ("bust", None, 0.45, 1.25, [0.0, 0.27, 0.0],
             "Talking Heads porcelain bust (ours): smooth, clean, untextured, a strong classical silhouette"),
    "horse": ("gltf", ("farm/horse.glb", [0, -90, 0], 1.0), 1.6, 4.8, [0.0, 0.95, 0.0],
              "farm horse (third-party, local only): an animal silhouette in profile, low-poly and stylised"),
    "astronaut": ("gltf", ("musicians/astronaut_keys.glb", [0, 0, 0], 1.0), 1.4, 3.1, [0.0, 0.95, 0.0],
                  "astronaut (third-party, local only): untextured hard-surface suit and helmet, readable forms"),
    "medallion": ("sdf", (medallion(), [-0.56, -0.56, -0.2], [0.56, 0.56, 0.2], 160), 1.0, 2.1, [0, 0, 0],
                  "procedural medallion: a disc, a rim, a domed boss and a six-point star in relief"),
    "torus": ("proc", ({"kind": "torus", "majorRadius": 0.34, "minorRadius": 0.15, "majorSegments": 96,
                        "minorSegments": 32}, [70, 0, 0]), 1.0, 2.0, [0, 0, 0],
              "procedural torus: continuous curvature in both directions, a ring or a link"),
    "gem": ("proc", ({"kind": "sphere", "radius": 0.45, "segments": 10, "rings": 6}, [15, 20, 0]), 1.0, 2.1,
            [0, 0, 0], "procedural low-segment sphere: a faceted gem or crystal"),
    "mask": ("sdf", (mask(), [-0.5, -0.5, -0.5], [0.5, 0.5, 0.55], 128), 1.0, 2.1, [0, -0.02, 0],
             "procedural mask: cranium, brow, cheeks and jaw blended, the eye sockets and nose cut away"),
}
VARIANTS = ["base", "A", "B", "C", "D"]


def build(out, subject, variant, period):
    kind, spec, unit, dist, tgt, _ = SUBJECTS[subject]
    name = "%s-%s" % (subject, variant)
    s = kit.Scene(name, "Cursed Slammers POC5 " + name, {"thesis": "surface treatment survey"})
    s.environment = {"intensity": 0.5, "background": [0.006, 0.006, 0.007], "fogColor": [0.0, 0.0, 0.0],
                     "volumeDensity": 0.0, "skyIntensity": 1.0,
                     "sky": {"enabled": True, "zenithColor": [0.10, 0.10, 0.11], "horizonColor": [0.45, 0.45, 0.48],
                             "groundColor": [0.004, 0.004, 0.005], "haze": 0.0, "sunIntensity": 0.0,
                             "intensity": 1.0, "background": True, "useKeyLight": False}}
    s.params_({"camera/lens/focalLength": 50.0, "post/bloom/intensity": 0.2, "post/bloom/threshold": 1.4,
               "post/output/vignette": 0.25, "post/output/grain": 0.0, "camera/exposure/compensation": 0.0})
    sc = dist / 2.2                                                     # lights scale with the framing
    s.light("key", "spot", position=[tgt[0] - 1.3 * sc, tgt[1] + 1.3 * sc, 1.9 * sc], direction=[0.5, -0.45, -0.74],
            color=[1.0, 0.95, 0.9], intensity=22.0 * sc * sc, range=9.0 * sc, innerCone=18.0, outerCone=34.0,
            castsShadow=False, contactShadow=False)
    s.light("fill", "point", position=[tgt[0] + 1.6 * sc, tgt[1] - 0.4 * sc, 1.8 * sc], color=[0.85, 0.88, 1.0],
            intensity=1.6 * sc * sc, range=8.0 * sc, castsShadow=False, contactShadow=False)
    s.light("rim", "directional", direction=[-0.3, -0.6, 0.74], color=[0.9, 0.9, 1.0], intensity=0.9,
            castsShadow=False, contactShadow=False)
    s.program({"name": "bg", "ops": [
        V.op("input", 0, inp="worldPosition"), V.op("noise", 1, a=0, value=1.4 / sc, seed=3),
        V.op("remap", 1, a=1, value=1.0, k=[0.2, 0.8, 0.5, 1.5]), V.C(2, 0.03, 0.03, 0.034, 1.0),
        V.op("multiply", 2, a=2, b=1)],
        "baseColor": 2, "metallic": -1, "roughness": -1, "emission": -1, "emissionIntensity": 1.0, "opacity": -1})
    s.proc("backdrop", {"kind": "box", "size": [12.0 * sc, 8.0 * sc, 0.05]},
           material={"baseColor": [0.03, 0.03, 0.034], "roughness": 0.9, "metallic": 0.0, "emissiveColor": [0, 0, 0],
                     "emissiveIntensity": 0.0, "program": "bg"},
           transform={"position": [tgt[0], tgt[1], -1.6 * sc], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    prog = None
    if variant != "base":
        prog = rescaled(V.PROGRAMS[variant](), unit)
        s.program(prog)
    plain = {"baseColor": [0.6, 0.6, 0.62], "roughness": 0.45, "metallic": 0.0, "emissiveColor": [0, 0, 0],
             "emissiveIntensity": 0.0}

    def gl(nm, path, pos, rot=(0, 0, 0), scale=1.0):
        n = {"name": nm, "kind": "gltf", "asset": path, "position": [float(c) for c in pos],
             "rotation": [float(c) for c in rot], "scale": [float(scale)] * 3, "castsShadow": False}
        if prog:
            n["program"] = "poc4"
        s.nodes.append(n)
        s.entity(nm)

    if kind == "bust":
        gl("body", os.path.join(MESHES, "th-body.glb"), (0, 0, 0))
        gl("neck", os.path.join(MESHES, "th-neck.glb"), (0.0, 0.162, -0.0214))
        gl("head", os.path.join(MESHES, "th-head.glb"), (0.0, 0.237, -0.0214))
    elif kind == "gltf":
        path, rot, scale = spec
        gl("subject", os.path.join(ASSETS, path), (0, 0, 0), rot, scale)
    elif kind == "proc":
        src, rot = spec
        mat = dict(plain)
        if prog:
            mat["program"] = "poc4"
        s.proc("subject", src, material=mat, transform={"position": [0, 0, 0], "rotation": rot, "scale": [1, 1, 1]})
    else:
        root, bmin, bmax, res = spec
        mat = dict(plain)
        if prog:
            mat["program"] = "poc4"
        s.sdf("subject", root, bmin, bmax, material=mat, mesh=res)
    keys = []
    for i in range(65):
        a = math.radians(20.0) * -math.cos(2 * math.pi * i / 64)
        keys.append({"time": round(period * i / 64, 4),
                     "value": [round(tgt[0] + dist * math.sin(a), 5), tgt[1], round(dist * math.cos(a), 5)],
                     "interp": "smooth"})
    s.track("camera/position", keys, loop=period)
    s.track("camera/target", [{"time": 0.0, "value": tgt, "interp": "linear"},
                              {"time": period, "value": tgt, "interp": "linear"}], loop=period)
    s.camera = {"mode": 1, "position": keys[0]["value"], "target": tgt, "fov": 27.0, "orbitSpeed": 0.0}
    s.write(out)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--period", type=float, default=8.0)
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    for subj in SUBJECTS:
        for v in VARIANTS:
            build(out, subj, v, a.period)
    for subj, t in SUBJECTS.items():
        print("%-10s %s" % (subj, t[5]))
