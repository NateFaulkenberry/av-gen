"""Cursed Slammers look-development study: ONE head (the H1 scan, prepared by tools/cursed_slammers/prep_face.py)
under the lenticular treatment, judged on a slow side-to-side camera. No bars, hands, cycle, audio or animation (the
owner, 2026-10-25: "just try rendering this head with lenticular effects"). Not registered anywhere.

    python3 face_study.py --face <cs-face.glb> --out <dir> [--period 16]
    tools/gpu-lock.sh build/release/src/avgen --project <dir>/V2.json --render <dir>/V2.mp4 --range 0:16 --fps 30 \
        --tier high --codec h264

Variants (each a project + scene in --out):
  V0 plain    the lit head over the dark substrate, no optics (the reference)
  V1 ridges   lens ridges in the highlights only: a faint ridge comb on the skin, a strong one on the substrate,
              a travelling sweep light
  V2 flip     V1 + the interlaced, view-dependent flip: from one side the head is embossed into the substrate
              (near black, glossy, ridged: it shows only as glints); from the other it is the lit ape. Between, the
              two looks interleave in strips and the boundary sweeps across the face as the camera moves
  V3 foil     V2 with the embossed look in muted foil metal (silver F0 cycling cold violet / teal / bronze with the
              view angle) and a foil substrate

Layout (world units, +Y up, camera at +Z): the face at the origin, its relief exaggerated in depth (scale z 1.5),
the ridged substrate at z = -1.4. The camera sweeps x +-0.40 at z = 2.0, always aimed at the face.
"""
import argparse
import math
import os
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../.."))
sys.path.insert(0, REPO)
from tools.sonic_vfx import kit  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--face", required=True)
ap.add_argument("--out", required=True)
ap.add_argument("--period", type=float, default=16.0)
ap.add_argument("--depth", type=float, default=1.5, help="relief exaggeration (face scale z)")
args = ap.parse_args()
FACE = os.path.abspath(args.face)
OUT = os.path.abspath(args.out)
PERIOD = args.period
TARGET = [0.0, 0.02, 0.0]
CAM_Z = 2.0
SWEEP = 0.40
PLATE_Z = -1.4

RIDGE_FREQ = 70.0          # lenticules per world unit (about 120 across the frame at the face)
RIDGE_FREQ_PLATE = 32.0


def ridge(dst, freq):
    """(-sin(2 pi x freq), 0, 1): a vertical lenticule's normal, unit tilt."""
    return [
        {"kind": "worldProject", "dst": dst, "value": float(freq), "constant": [0.0, 0.0, 0.0, 0.0]},
        {"kind": "palette", "dst": dst, "srcA": dst, "constant": [0.0, 0.0, 1.0, 0.0],
         "constant2": [1.0, 0.0, 0.0, 0.0], "constant3": [1.0, 0.0, 0.0, 0.0],
         "constant4": [0.25, 0.0, 0.0, 0.0], "value": 0.0},
    ]


def foil_f0(dst, tmp):
    """A muted cosine rainbow around silver, indexed by the view angle and height: a metal's F0."""
    return [
        {"kind": "input", "dst": dst, "input": "viewDirection"},
        {"kind": "input", "dst": tmp, "input": "worldPosition"},
        {"kind": "constant", "dst": tmp + 1, "constant": [0.0, 0.6, 0.0, 0.0]},
        {"kind": "multiply", "dst": tmp, "srcA": tmp, "srcB": tmp + 1},
        {"kind": "add", "dst": dst, "srcA": dst, "srcB": tmp},
        {"kind": "gradient", "dst": dst, "srcA": dst, "value": 1.2, "constant": [1.0, 1.0, 0.0, 0.0]},
        {"kind": "palette", "dst": dst, "srcA": dst, "constant": [0.62, 0.60, 0.68, 0.0],
         "constant2": [0.16, 0.14, 0.20, 0.0], "constant3": [1.4, 1.4, 1.4, 0.0],
         "constant4": [0.0, 0.33, 0.67, 0.0], "value": 0.0},
    ]


def edge_fade(dst, colour):
    """Darken the scan's cut edges (grazing to the camera: high Fresnel) so the mask sinks into the dark instead of
    showing a rim: colour *= 1 - smoothstep(0.45, 0.9, fresnel^1)."""
    return [
        {"kind": "fresnel", "dst": dst, "value": 1.0},
        {"kind": "smoothstep", "dst": dst, "srcA": dst, "constant": [0.45, 0.9, 0.0, 0.0]},
        {"kind": "constant", "dst": dst + 1, "constant": [1.0, 1.0, 1.0, 1.0]},
        {"kind": "mixBy", "dst": dst, "srcA": dst + 1, "srcB": dst + 2, "srcC": dst},
        {"kind": "multiply", "dst": colour, "srcA": colour, "srcB": dst},
    ]


def skin_program(variant):
    """The head. Registers: r0 ridge normal, r1 t (0 = embossed look, 1 = lit look), r2..r7 scratch / outputs.
    (edge_fade uses r5..r7; r7 is never written, so it reads zero.)"""
    if variant == "V0":
        ops = [{"kind": "constant", "dst": 4, "constant": [0.75, 0.72, 0.70, 1.0]}] + edge_fade(5, 4)
        return {"name": "cs_face", "ops": ops, "normal": -1, "roughness": -1, "baseColor": 4, "metallic": -1,
                "emission": -1, "emissionIntensity": 1.0, "opacity": -1}
    ops = ridge(0, RIDGE_FREQ)
    if variant == "V1":
        # ridges in the highlights only: a small tilt, anisotropic roughness along the lenticules
        ops += [
            {"kind": "constant", "dst": 2, "constant": [0.12, 1.0, 1.0, 0.0]},
            {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 2},
            {"kind": "constant", "dst": 3, "constant": [0.38, 0.38, 0.38, 0.38]},
            {"kind": "anisotropy", "dst": 3, "srcA": 3, "value": 0.8, "constant": [0.0, 1.0, 0.0, 0.0]},
            {"kind": "roughnessFilter", "dst": 3, "srcA": 3, "value": 1.0},
            {"kind": "constant", "dst": 4, "constant": [0.75, 0.72, 0.70, 1.0]},
        ] + edge_fade(5, 4)
        return {"name": "cs_face", "ops": ops, "normal": 0, "roughness": 3, "baseColor": 4, "metallic": -1,
                "emission": -1, "emissionIntensity": 1.0, "opacity": -1}
    # V2 / V3: the interlaced flip. t = smoothstep(0.4, 0.6, saturate(2.6 viewDir.x + 0.5 + 0.2 cos(2 pi x f)))
    ops += [
        {"kind": "input", "dst": 1, "input": "viewDirection"},
        {"kind": "worldProject", "dst": 2, "value": RIDGE_FREQ, "constant": [0.0, 0.0, 0.0, 0.0]},
        {"kind": "palette", "dst": 2, "srcA": 2, "constant": [0.0, 0.0, 0.0, 0.0],
         "constant2": [0.20, 0.0, 0.0, 0.0], "constant3": [1.0, 0.0, 0.0, 0.0],
         "constant4": [0.0, 0.0, 0.0, 0.0], "value": 0.0},
        {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},
        {"kind": "gradient", "dst": 1, "srcA": 1, "value": 2.6, "constant": [1.0, 0.0, 0.0, 0.5]},
        {"kind": "smoothstep", "dst": 1, "srcA": 1, "constant": [0.40, 0.60, 0.0, 0.0]},
        # ridge tilt: 0.65 embossed, 0.16 lit
        {"kind": "constant", "dst": 2, "constant": [0.65, 1.0, 1.0, 0.0]},
        {"kind": "constant", "dst": 3, "constant": [0.12, 1.0, 1.0, 0.0]},
        {"kind": "mixBy", "dst": 2, "srcA": 2, "srcB": 3, "srcC": 1},
        {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 2},
        # roughness: 0.16 embossed (glossy lens sheet), 0.40 lit skin; anisotropic along the lenticules
        {"kind": "constant", "dst": 3, "constant": [0.16, 0.16, 0.16, 0.16]},
        {"kind": "constant", "dst": 4, "constant": [0.40, 0.40, 0.40, 0.40]},
        {"kind": "mixBy", "dst": 3, "srcA": 3, "srcB": 4, "srcC": 1},
        {"kind": "anisotropy", "dst": 3, "srcA": 3, "value": 0.8, "constant": [0.0, 1.0, 0.0, 0.0]},
        {"kind": "roughnessFilter", "dst": 3, "srcA": 3, "value": 1.0},
    ]
    if variant == "V2":
        # base colour: embossed = near black (x the scan texture), lit = the scan texture slightly graded down
        ops += [
            {"kind": "constant", "dst": 4, "constant": [0.03, 0.028, 0.034, 1.0]},
            {"kind": "constant", "dst": 5, "constant": [0.75, 0.72, 0.70, 1.0]},
            {"kind": "mixBy", "dst": 4, "srcA": 4, "srcB": 5, "srcC": 1},
            # metallic: embossed 0.6 (a glossy dark sheet), lit 0
            {"kind": "constant", "dst": 5, "constant": [0.6, 0.6, 0.6, 0.6]},
            {"kind": "constant", "dst": 6, "constant": [0.0, 0.0, 0.0, 0.0]},
            {"kind": "mixBy", "dst": 5, "srcA": 5, "srcB": 6, "srcC": 1},
        ]
    else:
        # V3: embossed = foil metal (F0 from the muted rainbow, darkened); lit = the scan
        ops += foil_f0(4, 5)
        ops += [
            {"kind": "constant", "dst": 5, "constant": [0.6, 0.6, 0.6, 1.0]},
            {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 5},
            {"kind": "constant", "dst": 5, "constant": [0.75, 0.72, 0.70, 1.0]},
            {"kind": "mixBy", "dst": 4, "srcA": 4, "srcB": 5, "srcC": 1},
            {"kind": "constant", "dst": 5, "constant": [1.0, 1.0, 1.0, 1.0]},
            {"kind": "constant", "dst": 6, "constant": [0.0, 0.0, 0.0, 0.0]},
            {"kind": "mixBy", "dst": 5, "srcA": 5, "srcB": 6, "srcC": 1},
        ]
    # edge fade on the colour with r6 (fade) and r7 (ones); r2 is free again and zero-filled first
    ops += [
        {"kind": "fresnel", "dst": 6, "value": 1.0},
        {"kind": "smoothstep", "dst": 6, "srcA": 6, "constant": [0.45, 0.9, 0.0, 0.0]},
        {"kind": "constant", "dst": 7, "constant": [1.0, 1.0, 1.0, 1.0]},
        {"kind": "constant", "dst": 2, "constant": [0.0, 0.0, 0.0, 0.0]},
        {"kind": "mixBy", "dst": 6, "srcA": 7, "srcB": 2, "srcC": 6},
        {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 6},
    ]
    return {"name": "cs_face", "ops": ops, "normal": 0, "roughness": 3, "baseColor": 4, "metallic": 5,
            "emission": -1, "emissionIntensity": 1.0, "opacity": -1}


def plate_program(variant):
    ops = ridge(0, RIDGE_FREQ_PLATE)
    ops += [
        {"kind": "constant", "dst": 2, "constant": [0.75, 1.0, 1.0, 0.0]},
        {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 2},
        {"kind": "constant", "dst": 2, "constant": [0.09, 0.09, 0.09, 0.09]},
        {"kind": "anisotropy", "dst": 2, "srcA": 2, "value": 0.9, "constant": [0.0, 1.0, 0.0, 0.0]},
        {"kind": "roughnessFilter", "dst": 2, "srcA": 2, "value": 1.0},
        {"kind": "constant", "dst": 3, "constant": [1.0, 1.0, 1.0, 1.0]},
    ]
    if variant == "V3":
        ops += foil_f0(1, 4)
        ops += [{"kind": "constant", "dst": 4, "constant": [0.10, 0.10, 0.10, 1.0]},
                {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 4}]
    else:
        ops += [{"kind": "constant", "dst": 1, "constant": [0.05, 0.046, 0.06, 1.0]}]
    return {"name": "cs_plate", "ops": ops, "normal": 0, "baseColor": 1, "roughness": 2, "metallic": 3,
            "emission": -1, "emissionIntensity": 1.0, "opacity": -1}


def build(variant):
    s = kit.Scene(variant, "Cursed Slammers face study " + variant, {"thesis": "look-development study"})
    s.environment = {"intensity": 0.015, "background": [0.002, 0.0016, 0.004], "fogColor": [0.008, 0.006, 0.016],
                     "volumeDensity": 0.0, "skyIntensity": 1.0,
                     "sky": {"enabled": True, "zenithColor": [0.003, 0.002, 0.006],
                             "horizonColor": [0.005, 0.0035, 0.010], "groundColor": [0.002, 0.0015, 0.004],
                             "haze": 0.2, "sunIntensity": 0.0, "intensity": 1.0, "background": True,
                             "useKeyLight": False}}
    s.params_({"camera/lens/focalLength": 50.0, "post/bloom/intensity": 0.35, "post/bloom/threshold": 1.0,
               "post/output/vignette": 0.55, "post/output/grain": 0.015})
    # key: high camera-left, grazing (the brow shades the eyes); rim: cold violet from behind right;
    # catch: a small light just above the lens so the eyes keep a catchlight as the camera moves
    s.light("key", "spot", position=[-1.5, 1.5, 1.5], direction=[0.55, -0.5, -0.65], color=[1.0, 0.93, 0.86],
            intensity=10.0, range=8.0, innerCone=16.0, outerCone=30.0, castsShadow=False, contactShadow=False)
    s.light("rim", "directional", direction=[-0.4, -0.75, 0.5], color=[0.55, 0.45, 1.0], intensity=0.7,
            castsShadow=False, contactShadow=False)
    s.light("catch", "point", position=[0.0, 0.35, CAM_Z - 0.4], color=[0.9, 0.92, 1.0], intensity=0.6, range=4.0,
            castsShadow=False, contactShadow=False)
    if variant != "V0":
        # the sweep: a narrow spot travelling across in front: the ridge combs slide over the face and the substrate
        s.light("sweep", "spot", position=[-2.0, 0.4, 1.2], direction=[0.0, -0.1, -1.0], color=[0.85, 0.86, 1.0],
                intensity=14.0, range=8.0, innerCone=20.0, outerCone=45.0, castsShadow=False, contactShadow=False)
        s.track("lights/sweep/position", [
            {"time": 0.0, "value": [-2.0, 0.4, 1.2], "interp": "smooth"},
            {"time": PERIOD * 0.5, "value": [2.0, 0.4, 1.2], "interp": "smooth"},
            {"time": PERIOD, "value": [-2.0, 0.4, 1.2], "interp": "smooth"}], loop=PERIOD)
    prog = skin_program(variant)
    s.program(prog)
    s.program(plate_program(variant))
    face = {"name": "face", "kind": "gltf", "asset": FACE, "position": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0],
            "scale": [1.0, 1.0, float(args.depth)], "castsShadow": False}
    if prog:
        face["program"] = "cs_face"
    s.nodes.append(face)
    s.entity("face")
    s.proc("plate", {"kind": "box", "size": [12.0, 8.0, 0.05]},
           material={"baseColor": [0.05, 0.046, 0.06], "roughness": 0.1, "metallic": 1.0, "emissiveColor": [0, 0, 0],
                     "emissiveIntensity": 0.0, "program": "cs_plate"},
           transform={"position": [0.0, 0.0, PLATE_Z], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    # the camera: one slow lateral sweep per period, aimed at the face
    kp = []
    n = 48
    for i in range(n + 1):
        ph = i / n
        kp.append({"time": round(PERIOD * ph, 4),
                   "value": [round(-SWEEP * math.cos(2 * math.pi * ph), 4), 0.06, CAM_Z], "interp": "smooth"})
    s.track("camera/position", kp, loop=PERIOD)
    s.track("camera/target", [{"time": 0.0, "value": TARGET, "interp": "smooth"},
                              {"time": PERIOD, "value": TARGET, "interp": "smooth"}], loop=PERIOD)
    s.camera = {"mode": 1, "position": [-SWEEP, 0.06, CAM_Z], "target": TARGET, "fov": 27.0, "orbitSpeed": 0.0}
    s.write(OUT)
    print(variant, "->", os.path.join(OUT, variant + ".json"))


for v in ("V0", "V1", "V2", "V3"):
    build(v)
