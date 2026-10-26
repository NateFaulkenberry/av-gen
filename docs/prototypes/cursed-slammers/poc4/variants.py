"""Cursed Slammers POC 4: four surface treatments on the same face, the same camera, lights and exposure. Each is ONE
material program on the face (a gltf node `program`, ADR-1572; up to 64 ops, ADR-1661). No post effect, no extra pass,
no temporal effect. Nothing is registered; the POC 1-3 harnesses are untouched (this file only reads them).

    python3 variants.py --face assets/cursed-slammers/cs-face-b0.glb --face-plain assets/cursed-slammers/cs-face-b0-notex.glb \
        --out <dir> --variant base|A|B|C|D --shot front|orbit|close|final [--period 12]

Variants:
  base  the scan as it is (no program): the reference
  A     holofoil engraving: relief lines that follow the face (phase = height + depth), grooves widening in shadow;
        silver ridges whose metal colour (F0) turns through a muted spectrum with the view angle and the face's depth;
        sparse angle-gated micro-sparkle on the ridges
  B     intaglio / guilloche: wavy relief lines that follow the face, a diagonal crosshatch only in shadow, black
        grooves against silver, no rainbow
  C     iridescent chrome: dark polished metal, F0 a muted spectrum by view angle and depth, Fresnel-weighted,
        roughness varied by a soft noise, no lines
  D     hybrid security foil: B's linework and crosshatch, a silver base, spectral colour only inside narrow bands
        that slide over the face with the view, a fine diffraction-grating phase in the colour, sparser sparkle

The lines follow the face because their phase is a function of the face's own coordinates: object-space height plus
object-space DEPTH (z toward the viewer), so on the brow, the muzzle and the cheeks they bend round the form like a
banknote engraver's relief lines. Groove width reads the shading: thicker where the surface turns away from the key
light. Every line set fades to its mean as it drops below about two pixels (the `footprint` input), so it cannot alias.
A program's base colour is multiplied by the asset's colour texture after the program runs; on the scan's near-black
texture no metal could read as silver. So A-D draw the same mesh with the colour texture removed (--face-plain,
tools/cursed_slammers/strip_color.py; the baked normal map is kept) and set every colour themselves. The base shot
keeps the textured scan as the reference.

Shots (identical lights and exposure in every one):
  front  stationary, frontal, the face filling about 90 % of the frame height
  orbit  a slow yaw orbit +-15 degrees round the face, once per --period seconds
  close  a close-up on the eyes and brow (stationary)
  final  the intended scene framing: full frame, brow to chin, with a slow +-6 degree drift
"""
import argparse
import math
import os
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../.."))
sys.path.insert(0, REPO)
from tools.sonic_vfx import kit  # noqa: E402

KEY_POS = (-1.3, 1.3, 1.9)
_n = math.sqrt(sum(c * c for c in KEY_POS))
LDIR = [c / _n for c in KEY_POS]             # toward the key light (it aims at the origin)


def C(dst, *v):
    v = list(v) + [v[-1]] * (4 - len(v)) if len(v) < 4 else list(v)
    return {"kind": "constant", "dst": dst, "constant": [float(x) for x in v]}


def op(kind, dst, a=None, b=None, c=None, value=None, k=None, k2=None, k3=None, k4=None, seed=None, inp=None):
    o = {"kind": kind, "dst": dst}
    if inp is not None:
        o["input"] = inp
    for key, val in (("srcA", a), ("srcB", b), ("srcC", c)):
        if val is not None:
            o[key] = val
    if value is not None:
        o["value"] = float(value)
    for key, val in (("constant", k), ("constant2", k2), ("constant3", k3), ("constant4", k4)):
        if val is not None:
            o[key] = [float(x) for x in val]
    if seed is not None:
        o["seed"] = int(seed)
    return o


def cosine01(dst, src):
    """0.5 + 0.5 cos(2 pi a.x) in every lane (a palette with equal channels)."""
    return op("palette", dst, a=src, value=0.0, k=[0.5, 0.5, 0.5, 0], k2=[0.5, 0.5, 0.5, 0], k3=[1, 1, 1, 0],
              k4=[0, 0, 0, 0])


def phase(dst, tmp, pos, wx, wy, wz):
    """dst.x(all lanes) = wx x + wy y + wz z of a position register (three lane-sums). Clobbers tmp."""
    return [C(tmp, wx, wy, wz, 0.0), op("multiply", dst, a=pos, b=tmp),
            op("swizzle", tmp, a=dst, k=[1, 1, 1, 1]), op("add", tmp, a=tmp, b=dst),
            op("swizzle", dst, a=dst, k=[2, 2, 2, 2]), op("add", dst, a=dst, b=tmp)]
    # note: after the first add, tmp.x = y + x; then dst = z + (x + y) in lane x (lanes y/z hold partial sums)


SHADOW_GAIN = 0.35


def shadow_ops(dst):
    """u = saturate(G (1 - N.L)) toward the key light (G = SHADOW_GAIN), broadcast: 0 facing the light, G at 90
    degrees. Grooves widen with it; the crosshatch appears where it passes 0.19 (N.L < 0.45)."""
    g = SHADOW_GAIN
    return [op("input", dst, inp="normal"),
            op("gradient", dst, a=dst, value=1.0, k=[-g * LDIR[0], -g * LDIR[1], -g * LDIR[2], g])]


def zphase(dst, tmp, pos, wz):
    """dst = wz * z, broadcast."""
    return [op("swizzle", dst, a=pos, k=[2, 2, 2, 2]), C(tmp, wz), op("multiply", dst, a=dst, b=tmp)]


def aa_fade(dst, freq):
    """saturate(1.5 - 3 freq footprint): 1 while a line period spans 6 pixels or more, 0 at 2 pixels (where it would
    alias). (A first version faded from the start, which left the grooves at 70 % depth: grey, not black.)"""
    return [op("input", dst, inp="footprint"), op("gradient", dst, a=dst, value=1.0, k=[-3.0 * freq, 0, 0, 1.5])]


def groove(dst, s, u, lo, hi):
    """A line mask from a phase: smoothstep(lo, hi, cos01(s) + u), u the shadow term (wider grooves in shadow)."""
    return [cosine01(dst, s), op("add", dst, a=dst, b=u), op("smoothstep", dst, a=dst, k=[lo, hi, 0, 0])]


def sparkle(dst, pos, fres, ridge_zero_tmp, cell, seed, edge):
    """Sparse angle-gated sparkle: each cell (quantize) flares when cos(2 pi (7 rnd + 6 fresnel)) peaks, so
    neighbouring cells flare at different angles and a flare lives over a narrow angle; x fresnel."""
    return [op("quantize", dst, a=pos, value=cell, k=[0, 0, 0, 0], seed=seed), op("swizzle", dst, a=dst, k=[3, 3, 3, 3]),
            C(ridge_zero_tmp, 6.0 / 7.0), op("multiply", ridge_zero_tmp, a=fres, b=ridge_zero_tmp),
            op("add", dst, a=dst, b=ridge_zero_tmp),
            op("palette", dst, a=dst, value=0.0, k=[0.5, 0.5, 0.5, 0], k2=[0.5, 0.5, 0.5, 0], k3=[7, 7, 7, 0],
               k4=[0, 0, 0, 0]),
            op("smoothstep", dst, a=dst, k=[edge, 1.0, 0, 0]), op("multiply", dst, a=dst, b=fres)]


def groove_occlusion(dst, g):
    """The grooves see almost none of the environment: occlusion = mix(1, 0.08, g). Without it a rough black groove
    still reflects the studio's bright band and reads lighter than shadowed silver (inverted engraving)."""
    return op("remap", dst, a=g, value=1.0, k=[0, 1, 1, 0.08])


def metal_rough(g, met, rough, r_ridge, r_groove):
    """metallic = 1 - g, roughness = mix(r_ridge, r_groove, g) then specular anti-aliasing."""
    return [op("remap", met, a=g, value=1.0, k=[0, 1, 1, 0]),
            op("remap", rough, a=g, value=1.0, k=[0, 1, r_ridge, r_groove]),
            op("roughnessFilter", rough, a=rough, value=1.0)]


SPECTRUM = dict(k=[0.62, 0.61, 0.66, 0], k2=[0.30, 0.30, 0.32, 0], k3=[1, 1, 1, 0], k4=[0.0, 0.33, 0.67, 0])
SPECTRUM_SOFT = dict(k=[0.66, 0.65, 0.69, 0], k2=[0.20, 0.20, 0.22, 0], k3=[1, 1, 1, 0], k4=[0.0, 0.33, 0.67, 0])
TEX_COMP = 1.0      # the variants draw the face with its colour texture removed (strip_color.py), so colours are absolute


def program_A():
    F_Y, F_Z = 100.0, 70.0
    o = [op("input", 0, inp="localPosition")]
    o += phase(1, 2, 0, 0.0, F_Y, F_Z)                                  # r1 = relief-line phase
    o += shadow_ops(3)                                                  # r3 = shadow term
    o += groove(2, 1, 3, 0.74, 0.9)                                     # r2 = groove mask
    o += aa_fade(4, F_Y + 0.5 * F_Z)
    o += [op("multiply", 2, a=2, b=4)]
    # spectral F0: t = 2 fresnel + 1.5 z
    o += [op("fresnel", 3, value=1.0)]                                  # r3 = fresnel
    o += zphase(5, 4, 0, 1.5)
    o += [C(6, 2.0), op("multiply", 6, a=3, b=6), op("add", 5, a=5, b=6),
          op("palette", 5, a=5, value=0.0, **SPECTRUM_SOFT),
          C(6, TEX_COMP, TEX_COMP, TEX_COMP, 1.0), op("multiply", 5, a=5, b=6),
          C(6, 0.02, 0.02, 0.022, 1.0), op("mixBy", 5, a=5, b=6, c=2)]  # r5 = base colour (grooves dark)
    o += metal_rough(2, 6, 7, 0.3, 0.75)
    # micro-sparkle on the ridges, coloured by the ridge's own spectrum
    o += sparkle(0, 0, 3, 4, 220.0, 5, 0.997)
    o += [op("remap", 4, a=2, value=1.0, k=[0, 1, 1, 0]), op("multiply", 0, a=0, b=4), op("multiply", 0, a=0, b=5)]
    o += [groove_occlusion(4, 2)]
    return {"name": "poc4", "ops": o, "baseColor": 5, "metallic": 6, "roughness": 7, "emission": 0,
            "emissionIntensity": 1.2, "normal": -1, "opacity": -1, "occlusion": 4}


def lines_B(o, wavy=True):
    """B's linework into r2 (0 silver .. 1 groove). Uses r0 (position), r1, r3 (shade), r4..r6 scratch."""
    F_Y, F_Z, F_X = 100.0, 70.0, 1.6
    o += phase(1, 2, 0, 0.0, F_Y, F_Z)
    if wavy:   # guilloche: the relief lines ripple gently across the face
        o += [op("swizzle", 4, a=0, k=[0, 0, 0, 0]), op("palette", 4, a=4, value=0.0, k=[0, 0, 0, 0],
                                                       k2=[0.6, 0.6, 0.6, 0], k3=[F_X, F_X, F_X, 0], k4=[0, 0, 0, 0]),
              op("add", 1, a=1, b=4)]
    o += shadow_ops(3)
    o += groove(2, 1, 3, 0.72, 0.9)
    # crosshatch: a diagonal family, only where the face turns from the light (N.L below about 0.45)
    o += phase(5, 4, 0, 85.0, 85.0, 50.0)
    o += [cosine01(5, 5), op("smoothstep", 5, a=5, k=[0.72, 0.88, 0, 0]),
          op("smoothstep", 6, a=3, k=[0.19, 0.28, 0, 0]), op("multiply", 5, a=5, b=6),
          op("add", 2, a=2, b=5), op("remap", 2, a=2, value=1.0, k=[0, 1, 0, 1])]
    o += aa_fade(4, F_Y + 0.5 * F_Z)
    o += [op("multiply", 2, a=2, b=4)]
    return o


def program_B():
    o = [op("input", 0, inp="localPosition")]
    o = lines_B(o)
    o += [C(5, 0.85 * TEX_COMP, 0.85 * TEX_COMP, 0.86 * TEX_COMP, 1.0), C(6, 0.008, 0.008, 0.008, 1.0),
          op("mixBy", 5, a=5, b=6, c=2)]
    o += metal_rough(2, 6, 7, 0.3, 0.85)
    o += [groove_occlusion(4, 2)]
    return {"name": "poc4", "ops": o, "baseColor": 5, "metallic": 6, "roughness": 7, "emission": -1,
            "emissionIntensity": 1.0, "normal": -1, "opacity": -1, "occlusion": 4}


def program_C():
    o = [op("input", 0, inp="localPosition"), op("fresnel", 3, value=1.0)]
    o += [op("noise", 1, a=0, value=6.0, seed=9)]                       # r1 = soft irregularity 0..1
    o += zphase(5, 4, 0, 1.5)
    o += [C(6, 1.8), op("multiply", 6, a=3, b=6), op("add", 5, a=5, b=6),
          C(6, 0.12), op("multiply", 6, a=1, b=6), op("add", 5, a=5, b=6),
          op("palette", 5, a=5, value=0.0, **SPECTRUM),
          # dark metal face-on, brighter toward grazing: F0 x mix(0.35, 1, fresnel), x texture compensation
          C(6, 0.35 * TEX_COMP), C(7, 1.0 * TEX_COMP), op("mixBy", 6, a=6, b=7, c=3), op("multiply", 5, a=5, b=6),
          C(6, 1.0),
          C(7, 0.22), op("multiply", 7, a=1, b=7), C(4, 0.16), op("add", 7, a=7, b=4),
          op("roughnessFilter", 7, a=7, value=1.0)]
    return {"name": "poc4", "ops": o, "baseColor": 5, "metallic": 6, "roughness": 7, "emission": -1,
            "emissionIntensity": 1.0, "normal": -1, "opacity": -1}


def program_D():
    o = [op("input", 0, inp="localPosition")]
    o = lines_B(o, wavy=True)                                            # r2 = groove mask
    o += [op("fresnel", 3, value=1.0)]
    # spectral colour t = 2.4 fresnel + 1.2 z + 0.06 cos(2 pi 320 x) (a fine grating phase)
    o += zphase(5, 4, 0, 1.2)
    o += [C(6, 2.4), op("multiply", 6, a=3, b=6), op("add", 5, a=5, b=6),
          op("swizzle", 4, a=0, k=[0, 0, 0, 0]),
          op("palette", 4, a=4, value=0.0, k=[0, 0, 0, 0], k2=[0.06, 0.06, 0.06, 0], k3=[320, 320, 320, 0],
             k4=[0, 0, 0, 0]),
          op("add", 5, a=5, b=4)]
    # the band mask: spectral only where cos01(0.6 t) is near its peak; elsewhere silver
    o += [op("palette", 6, a=5, value=0.0, k=[0.5, 0.5, 0.5, 0], k2=[0.5, 0.5, 0.5, 0], k3=[0.6, 0.6, 0.6, 0],
             k4=[0, 0, 0, 0]),
          op("smoothstep", 6, a=6, k=[0.68, 0.92, 0, 0]),
          op("palette", 5, a=5, value=0.0, **SPECTRUM),
          C(7, 0.86, 0.86, 0.88, 1.0), op("mixBy", 5, a=7, b=5, c=6),
          C(7, TEX_COMP, TEX_COMP, TEX_COMP, 1.0), op("multiply", 5, a=5, b=7),
          C(7, 0.008, 0.008, 0.008, 1.0), op("mixBy", 5, a=5, b=7, c=2)]
    o += metal_rough(2, 6, 7, 0.28, 0.85)
    # sparser sparkle than A, at grazing (not masked to ridges: grooves are near-black, a flare there reads as a
    # flake in the ink)
    o += sparkle(0, 0, 3, 4, 150.0, 11, 0.998)
    o += [groove_occlusion(4, 2)]
    return {"name": "poc4", "ops": o, "baseColor": 5, "metallic": 6, "roughness": 7, "emission": 0,
            "emissionIntensity": 1.5, "normal": -1, "opacity": -1, "occlusion": 4}


PROGRAMS = {"A": program_A, "B": program_B, "C": program_C, "D": program_D}

DIST = 2.2


def shot_camera(s, shot, period):
    if shot == "front":
        s.camera = {"mode": 1, "position": [0.0, 0.0, DIST], "target": [0, 0, 0], "fov": 27.0, "orbitSpeed": 0.0}
        return
    if shot == "close":
        tgt = [0.0, 0.19, 0.12]
        s.camera = {"mode": 1, "position": [0.0, 0.22, 1.05], "target": tgt, "fov": 27.0, "orbitSpeed": 0.0}
        return
    if shot == "orbit":
        amp, dist, tgt = 15.0, DIST, [0.0, 0.0, 0.0]
    else:  # final: full frame brow to chin, a slow drift
        amp, dist, tgt = 6.0, 1.75, [0.0, 0.02, 0.0]
    keys = []
    for i in range(65):
        a = math.radians(amp) * -math.cos(2 * math.pi * i / 64)
        keys.append({"time": round(period * i / 64, 4),
                     "value": [round(dist * math.sin(a), 5), tgt[1], round(dist * math.cos(a), 5)],
                     "interp": "smooth"})
    s.track("camera/position", keys, loop=period)
    s.track("camera/target", [{"time": 0.0, "value": tgt, "interp": "linear"},
                              {"time": period, "value": tgt, "interp": "linear"}], loop=period)
    s.camera = {"mode": 1, "position": keys[0]["value"], "target": tgt, "fov": 27.0, "orbitSpeed": 0.0}


def build(a):
    name = "%s-%s" % (a.variant, a.shot)
    s = kit.Scene(name, "Cursed Slammers POC4 " + name, {"thesis": "surface treatment study"})
    # identical in every variant: a dark studio (a dim sky with a soft brighter horizon band for the metals to
    # reflect), the same three lights, the same exposure and a conservative bloom
    s.environment = {"intensity": 0.5, "background": [0.006, 0.006, 0.007], "fogColor": [0.0, 0.0, 0.0],
                     "volumeDensity": 0.0, "skyIntensity": 1.0,
                     "sky": {"enabled": True, "zenithColor": [0.10, 0.10, 0.11],
                             "horizonColor": [0.45, 0.45, 0.48], "groundColor": [0.004, 0.004, 0.005],
                             "haze": 0.0, "sunIntensity": 0.0, "intensity": 1.0, "background": True,
                             "useKeyLight": False}}
    s.params_({"camera/lens/focalLength": 50.0, "post/bloom/intensity": 0.2, "post/bloom/threshold": 1.4,
               "post/output/vignette": 0.25, "post/output/grain": 0.0, "camera/exposure/compensation": 0.0})
    s.light("key", "spot", position=list(KEY_POS), direction=[0.5, -0.45, -0.74], color=[1.0, 0.95, 0.9],
            intensity=22.0, range=9.0, innerCone=18.0, outerCone=34.0, castsShadow=False, contactShadow=False)
    s.light("fill", "point", position=[1.6, -0.4, 1.8], color=[0.85, 0.88, 1.0], intensity=1.6, range=8.0,
            castsShadow=False, contactShadow=False)
    s.light("rim", "directional", direction=[-0.3, -0.6, 0.74], color=[0.9, 0.9, 1.0], intensity=0.9,
            castsShadow=False, contactShadow=False)
    s.program({"name": "bg", "ops": [
        op("input", 0, inp="worldPosition"), op("noise", 1, a=0, value=1.4, seed=3),
        op("remap", 1, a=1, value=1.0, k=[0.2, 0.8, 0.5, 1.5]), C(2, 0.03, 0.03, 0.034, 1.0),
        op("multiply", 2, a=2, b=1)],
        "baseColor": 2, "metallic": -1, "roughness": -1, "emission": -1, "emissionIntensity": 1.0, "opacity": -1})
    s.proc("backdrop", {"kind": "box", "size": [12.0, 8.0, 0.05]},
           material={"baseColor": [0.03, 0.03, 0.034], "roughness": 0.9, "metallic": 0.0, "emissiveColor": [0, 0, 0],
                     "emissiveIntensity": 0.0, "program": "bg"},
           transform={"position": [0.0, 0.0, -1.6], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    face_glb = a.face if a.variant == "base" else (a.face_plain or a.face)
    face = {"name": "face", "kind": "gltf", "asset": os.path.abspath(face_glb), "position": [0.0, 0.0, 0.0],
            "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0], "castsShadow": False}
    if a.variant != "base":
        prog = PROGRAMS[a.variant]()
        n = len(prog["ops"])
        assert n <= 64, (a.variant, n)
        s.program(prog)
        face["program"] = "poc4"
    s.nodes.append(face)
    s.entity("face")
    if a.back:
        s.nodes.append({"name": "back", "kind": "gltf", "asset": os.path.abspath(a.back), "position": [0, 0, 0],
                        "rotation": [0, 0, 0], "scale": [1, 1, 1], "castsShadow": False})
    shot_camera(s, a.shot, a.period)
    s.write(a.out)
    print(os.path.join(a.out, name + ".json"), "ops", len(s.programs[-1]["ops"]) if a.variant != "base" else 0)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--face", required=True)
    ap.add_argument("--face-plain", default="", help="the same mesh without its colour texture, for A-D")
    ap.add_argument("--back", default="")
    ap.add_argument("--out", required=True)
    ap.add_argument("--variant", choices=["base", "A", "B", "C", "D"], required=True)
    ap.add_argument("--shot", choices=["front", "orbit", "close", "final"], required=True)
    ap.add_argument("--period", type=float, default=12.0)
    a = ap.parse_args()
    a.out = os.path.abspath(a.out)
    build(a)
