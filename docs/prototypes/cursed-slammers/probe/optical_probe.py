"""Cursed Slammers, THROWAWAY optical probe: four arms, each a 16 s slow side-to-side camera over the layered test
subject that make_probe_subject.py writes. Results and reading: 01-research-optical.md section 6.

    python3 optical_probe.py <dir-with-the-probe-glbs>
    tools/gpu-lock.sh build/release/src/avgen --project <dir>/P2.json --render <dir>/P2.mp4 --range 0:16 --fps 30

P1 depth only | P2 + optical surface (ridged lens normals, foil bars, tube glint, ridged back plate, sweep light)
P3 = P2 + an interlaced view flip on the head | P4 = P2 with a stepped (12-view) camera + frame-echo crosstalk

Layout (world units, +Y up, camera at +Z): bars and hands on the z = 0 plane, the head at z = -2.2 (the zero-parallax
"key plane": the camera always aims at it), the ridged back plate at z = -4.5, haze beyond. The camera sweeps x +-1.0 at
z = 4.0, so the near plane slides against the head one way and the back plate the other, as a lenticular print's layers
do around its key plane.
"""
import math
import os
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../.."))
sys.path.insert(0, REPO)
from tools.sonic_vfx import kit  # noqa: E402

OUT = os.path.abspath(sys.argv[1])
HEAD_Z = -2.2
HEAD = [0.0, 0.05, HEAD_Z]
CAM_Z = 4.0
PERIOD = 16.0
BARS_X = [-1.35, -0.47, 0.47, 1.35]
BAR_R = 0.055
HAND_S = 0.95


def ridge_normal_ops(dst, freq, tilt, first=0):
    """Vertical lenticules in world space: normal = (-tilt sin(2 pi x freq), 0, 1). Two ops."""
    return [
        {"kind": "worldProject", "dst": dst, "value": float(freq), "constant": [0.0, 0.0, 0.0, 0.0]},
        {"kind": "palette", "dst": dst, "srcA": dst, "constant": [0.0, 0.0, 1.0, 0.0],
         "constant2": [float(tilt), 0.0, 0.0, 0.0], "constant3": [1.0, 0.0, 0.0, 0.0],
         "constant4": [0.25, 0.0, 0.0, 0.0], "value": 0.0},
    ]


def programs(s, flip=False):
    # ---- skin: lens ridges in the specular only; roughness stretched along the ridges
    ops = ridge_normal_ops(0, 64.0, 0.22)
    ops += [
        {"kind": "constant", "dst": 1, "constant": [0.30, 0.30, 0.30, 0.30]},
        {"kind": "anisotropy", "dst": 1, "srcA": 1, "value": 0.85, "constant": [0.0, 1.0, 0.0, 0.0]},
        {"kind": "roughnessFilter", "dst": 1, "srcA": 1, "value": 1.0},
    ]
    prog = {"name": "cs_skin", "ops": ops, "normal": 0, "roughness": 1, "baseColor": -1, "metallic": -1,
            "emission": -1, "emissionIntensity": 1.0, "opacity": -1}
    if flip:
        # the interlaced flip: t = smoothstep(saturate(viewDir.x * 2.2 + 0.5 + stripe)), stripe = 0.18 cos(2 pi x 38)
        ops += [
            {"kind": "input", "dst": 2, "input": "viewDirection"},
            {"kind": "worldProject", "dst": 3, "value": 38.0, "constant": [0.0, 0.0, 0.0, 0.0]},
            {"kind": "palette", "dst": 3, "srcA": 3, "constant": [0.0, 0.0, 0.0, 0.0],
             "constant2": [0.18, 0.0, 0.0, 0.0], "constant3": [1.0, 0.0, 0.0, 0.0],
             "constant4": [0.0, 0.0, 0.0, 0.0], "value": 0.0},
            {"kind": "add", "dst": 2, "srcA": 2, "srcB": 3},
            {"kind": "gradient", "dst": 2, "srcA": 2, "value": 2.2, "constant": [1.0, 0.0, 0.0, 0.5]},
            {"kind": "smoothstep", "dst": 2, "srcA": 2, "constant": [0.42, 0.58, 0.0, 0.0]},
            # look A: the skin (white x the material colour); look B: a pale silver negative with a violet glow
            {"kind": "constant", "dst": 3, "constant": [0.035, 0.03, 0.028, 1.0]},
            {"kind": "constant", "dst": 4, "constant": [0.55, 0.56, 0.62, 1.0]},
            {"kind": "mixBy", "dst": 5, "srcA": 3, "srcB": 4, "srcC": 2},
            {"kind": "constant", "dst": 6, "constant": [0.30, 0.16, 0.85, 1.0]},
            {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 2},
        ]
        prog.update({"ops": ops, "baseColor": 5, "emission": 6, "emissionIntensity": 0.6})
    s.program(prog)
    # ---- the bars: polished foil metal, a muted thin-film tint that turns with the view, lens ridges across
    ops = [
        {"kind": "input", "dst": 0, "input": "viewDirection"},
        {"kind": "input", "dst": 1, "input": "worldPosition"},
        {"kind": "constant", "dst": 2, "constant": [1.0, 0.0, 0.0, 0.0]},
        {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 2},
        {"kind": "constant", "dst": 2, "constant": [0.0, 0.35, 0.0, 0.0]},
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 2},
        {"kind": "add", "dst": 0, "srcA": 0, "srcB": 1},
        {"kind": "gradient", "dst": 0, "srcA": 0, "value": 1.0, "constant": [1.0, 1.0, 0.0, 0.0]},
        # a muted cosine rainbow (Quilez) around silver: F0 tinted violet / teal / bronze as the angle turns
        {"kind": "palette", "dst": 3, "srcA": 0, "constant": [0.72, 0.70, 0.78, 0.0],
         "constant2": [0.16, 0.14, 0.20, 0.0], "constant3": [1.6, 1.6, 1.6, 0.0],
         "constant4": [0.0, 0.33, 0.67, 0.0], "value": 0.0},
        {"kind": "constant", "dst": 4, "constant": [1.0, 1.0, 1.0, 1.0]},
        {"kind": "constant", "dst": 5, "constant": [0.12, 0.12, 0.12, 0.12]},
        {"kind": "anisotropy", "dst": 5, "srcA": 5, "value": 0.9, "constant": [0.0, 1.0, 0.0, 0.0]},
        {"kind": "roughnessFilter", "dst": 5, "srcA": 5, "value": 1.0},
    ]
    s.program({"name": "cs_bar", "ops": ops, "baseColor": 3, "metallic": 4, "roughness": 5, "normal": -1,
               "emission": -1, "emissionIntensity": 1.0, "opacity": -1})
    # ---- the back plate: the print's own substrate, near black, glossy lens ridges (this is the Ether)
    ops = ridge_normal_ops(0, 22.0, 0.8)
    ops += [
        {"kind": "constant", "dst": 1, "constant": [0.05, 0.045, 0.06, 1.0]},
        {"kind": "constant", "dst": 2, "constant": [0.10, 0.10, 0.10, 0.10]},
        {"kind": "anisotropy", "dst": 2, "srcA": 2, "value": 0.9, "constant": [0.0, 1.0, 0.0, 0.0]},
        {"kind": "roughnessFilter", "dst": 2, "srcA": 2, "value": 1.0},
        {"kind": "constant", "dst": 3, "constant": [1.0, 1.0, 1.0, 1.0]},
    ]
    s.program({"name": "cs_plate", "ops": ops, "normal": 0, "baseColor": 1, "roughness": 2, "metallic": 3,
               "emission": -1, "emissionIntensity": 1.0, "opacity": -1})


def subject(s, optical):
    skin = {"program": "cs_skin"} if optical else {}
    for name, glb, pos, rot, sc in (
            ("head", "probe-head.glb", HEAD, (0, 0, 0), 2.25),
            ("eyeL", "probe-eye.glb", [HEAD[0] - 0.165 * 2.25, HEAD[1] + 0.06 * 2.25, HEAD_Z + 0.30 * 2.25],
             (0, 0, 0), 2.25),
            ("eyeR", "probe-eye.glb", [HEAD[0] + 0.165 * 2.25, HEAD[1] + 0.06 * 2.25, HEAD_Z + 0.30 * 2.25],
             (0, 0, 0), 2.25),
            ("handL", "probe-hand.glb", [BARS_X[1], -0.12, 0.0], (0, 0, 0), HAND_S),
            ("handR", "probe-hand-r.glb", [BARS_X[2], -0.05, 0.0], (0, 0, 0), HAND_S)):
        n = {"name": name, "kind": "gltf", "asset": os.path.join(OUT, glb), "position": [float(v) for v in pos],
             "rotation": [float(v) for v in rot], "scale": [float(sc)] * 3 if not isinstance(sc, list) else sc,
             "castsShadow": False}
        if optical and not name.startswith("eye"):
            n.update(skin)
        s.nodes.append(n)
        s.entity(name)
    bar_mat = {"baseColor": [0.6, 0.6, 0.62], "roughness": 0.18, "metallic": 1.0, "emissiveColor": [0, 0, 0],
               "emissiveIntensity": 0.0}
    if optical:
        bar_mat["program"] = "cs_bar"
    s.proc("bars", {"kind": "cylinder", "radius": BAR_R, "height": 6.0, "radialSegments": 32, "caps": False},
           distribution={"kind": "linear", "count": len(BARS_X), "start": [BARS_X[0], 0.0, 0.0],
                         "end": [BARS_X[-1], 0.0, 0.0]},
           material=bar_mat)
    plate_mat = {"baseColor": [0.012, 0.01, 0.016], "roughness": 0.3, "metallic": 0.4, "emissiveColor": [0, 0, 0],
                 "emissiveIntensity": 0.0}
    if optical:
        plate_mat["program"] = "cs_plate"
    s.proc("plate", {"kind": "box", "size": [16.0, 10.0, 0.05]}, material=plate_mat,
           transform={"position": [0.0, 0.0, -4.5], "rotation": [0, 0, 0], "scale": [1, 1, 1]})


def base(sid, optical, flip=False, stepped=False):
    s = kit.Scene(sid, "Cursed Slammers probe " + sid, {"thesis": "optical probe"})
    s.environment = {"intensity": 0.02, "background": [0.002, 0.0016, 0.004], "fogColor": [0.010, 0.008, 0.02],
                     "volumeDensity": 0.0015, "volumeMaxDistance": 30.0, "volumeLocalLights": 1.0,
                     "skyIntensity": 1.0,
                     "sky": {"enabled": True, "zenithColor": [0.003, 0.002, 0.006],
                             "horizonColor": [0.006, 0.004, 0.012], "groundColor": [0.002, 0.0015, 0.004],
                             "haze": 0.2, "sunIntensity": 0.0, "intensity": 1.0, "background": True,
                             "useKeyLight": False}}
    s.params_({"camera/lens/focalLength": 50.0, "post/bloom/intensity": 0.4, "post/bloom/threshold": 1.0,
               "post/output/vignette": 0.45, "post/output/grain": 0.015})
    # key: high camera-left, warm-neutral, reveals the brow and muzzle; rim: cold violet from behind right
    s.light("key", "spot", position=[-2.2, 2.6, 1.6], direction=[0.45, -0.5, -0.75], color=[1.0, 0.92, 0.85],
            intensity=35.0, range=14.0, innerCone=14.0, outerCone=24.0, castsShadow=False, contactShadow=False)
    s.light("rim", "directional", direction=[-0.6, -0.25, 0.75], color=[0.55, 0.45, 1.0], intensity=1.6,
            castsShadow=False, contactShadow=False)
    if optical:
        # the sweep: a thin tube light travelling across in front of the bars: thin vertical glints on the bars,
        # a comb of ridge glints sliding over the skin and the plate
        s.light("sweep", "spot", position=[-3.0, 0.6, 1.6], direction=[0.0, -0.1, -1.0], color=[0.85, 0.85, 1.0],
                intensity=30.0, range=12.0, innerCone=30.0, outerCone=60.0, castsShadow=False, contactShadow=False)
        s.track("lights/sweep/position", [
            {"time": 0.0, "value": [-3.0, 0.6, 1.6], "interp": "smooth"},
            {"time": PERIOD / 2, "value": [3.0, 0.6, 1.6], "interp": "smooth"},
            {"time": PERIOD, "value": [-3.0, 0.6, 1.6], "interp": "smooth"}], loop=PERIOD)
        programs(s, flip)
    subject(s, optical)
    # the camera: a slow lateral sweep aimed at the head (the key plane)
    steps = 48
    kp, kt = [], []
    if stepped:
        views = 12
        for i in range(views * 2 + 1):
            # hold each view, then a short ease to the next: a lenticular print's discrete views
            ph = i / (views * 2)
            x = -1.0 * math.cos(2 * math.pi * ph)
            t0 = PERIOD * ph
            kp.append({"time": round(t0, 4), "value": [round(x, 4), 0.1, CAM_Z], "interp": "step"})
        s.params_({"temporal/echo/enabled": True, "temporal/echo/frames": 3.0, "temporal/echo/strength": 0.45})
    else:
        for i in range(steps + 1):
            ph = i / steps
            kp.append({"time": round(PERIOD * ph, 4), "value": [round(-1.0 * math.cos(2 * math.pi * ph), 4), 0.1, CAM_Z],
                       "interp": "smooth"})
    kt = [{"time": 0.0, "value": HEAD, "interp": "smooth"}, {"time": PERIOD, "value": HEAD, "interp": "smooth"}]
    s.track("camera/position", kp, loop=PERIOD)
    s.track("camera/target", kt, loop=PERIOD)
    s.camera = {"mode": 1, "position": [-1.0, 0.1, CAM_Z], "target": HEAD, "fov": 27.0, "orbitSpeed": 0.0}
    s.write(OUT)
    print(sid, len(s.nodes), "nodes", len(s.programs), "programs")


base("P1", optical=False)
base("P2", optical=True)
base("P3", optical=True, flip=True)
base("P4", optical=True, stepped=True)
