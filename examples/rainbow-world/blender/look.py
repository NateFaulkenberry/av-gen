"""Rainbow World: the shared look -- palette values, AV Gen material programs, scene environment and post settings.

Imported by the Stage 3 look-development builder and the world builder, so the look-dev scene and the world are lit
and coloured by exactly the same code. Pure Python (mathutils only for the vector helpers), no bpy.
"""
import json
import math

from mathutils import Vector

import rw_common as rw

LOOK = {
    "exposure": 0.2, "saturation": 1.15, "contrast": 1.14, "chroma": 0.65, "atmos": 0.42, "atmosDist": 3200.0, "atmosTint": rw.hexlin("#8FB4E8")[:3],
    "sun": 6.5, "sunColor": rw.hexlin("#FFD49A")[:3], "bounce": 1.6, "skyIntensity": 1.0, "haze": 0.2,
    "zenith": rw.hexlin("#0E36B2")[:3], "horizon": rw.hexlin("#3EAAE2")[:3], "ground": rw.hexlin("#B4B2DC")[:3],
    "meadow": [rw.hexlin("#2A6E2E")[:3], rw.hexlin("#4E9A2C")[:3], rw.hexlin("#6FB038")[:3], rw.hexlin("#9AC23A")[:3]],
    "shade": rw.hexlin("#155A5C")[:3],
    "cloud": [rw.hexlin("#6A5A9E")[:3], rw.hexlin("#A888BC")[:3], rw.hexlin("#FFC28C")[:3], rw.hexlin("#FFDFB8")[:3]],
    "cloudBounce": rw.hexlin("#F0A88C")[:3], "cloudGain": 0.92,
    "rainbowStrength": 2.0,
}


def load_look(path=None):
    if path:
        LOOK.update(json.load(open(path)))
    return LOOK


def R(v):
    return [round(float(x), 4) for x in v]


def av(v):
    return R(rw.to_avgen(v))


def point(p, q, s):
    a = rw.to_avgen(p)
    w, x, y, z = rw.quat_to_avgen(q)
    if not isinstance(s, (tuple, list)):
        s = (s, s, s)
    return [round(a[0], 3), round(a[1], 3), round(a[2], 3), round(x, 5), round(y, 5), round(z, 5), round(w, 5),
            round(s[0], 3), round(s[1], 3), round(s[2], 3)]



SUN = Vector((0.0, 1.0, 0.0))   # toward the sun, AV Gen frame (set by configure)
ranges = [(0.0, 0.0), (0.0, 0.0)]  # the band's two rim zones in UV.x (set by configure)


def configure(sun_avgen, rim_ranges):
    global SUN, ranges
    SUN = Vector(sun_avgen).normalized()
    ranges = list(rim_ranges)


def meadow_ops(dst, pos_reg=0, scratch=(1, 2, 5)):
    """The turf colour: designed patches (80 m and 15 m), an OKLab ramp through four greens. Shared by the ground
    and the grass so clumps match the turf they stand on."""
    a, b, c = scratch
    m = LOOK["meadow"]
    return [
        {"kind": "noise", "dst": a, "srcA": pos_reg, "value": 0.012, "seed": 11},
        {"kind": "noise", "dst": b, "srcA": pos_reg, "value": 0.07, "seed": 23},
        {"kind": "mix", "dst": a, "srcA": a, "srcB": b, "value": 0.35},
        {"kind": "remap", "dst": a, "srcA": a, "constant": [0.3, 0.72, 0.0, 1.0], "value": 1.0},
        {"kind": "oklabRamp", "dst": dst, "srcA": a, "value": 1.0, "constant": R(m[0]) + [1.0],
         "constant2": R(m[1]) + [1.0], "constant3": R(m[2]) + [1.0], "constant4": R(m[3]) + [1.0]},
    ]


def ground_program():
    (r0a, r0b), (r1a, r1b) = ranges
    e = 0.004
    ops = [{"kind": "input", "dst": 0, "input": "worldPosition"}]
    ops += meadow_ops(3)
    ops += [
        # cavities into cool teal shade
        {"kind": "input", "dst": 4, "input": "cavity"},
        {"kind": "remap", "dst": 4, "srcA": 4, "constant": [0.0, 0.6, 0.0, 0.75], "value": 1.0},
        {"kind": "constant", "dst": 5, "constant": R(LOOK["shade"]) + [1.0]},
        {"kind": "mixBy", "dst": 3, "srcA": 3, "srcB": 5, "srcC": 4},
        # turf: a fine value breakup at the scale of grass tufts, fading with distance (never aliases)
        {"kind": "microDetail", "dst": 2, "srcA": 0, "value": 1.6, "seed": 7},
        {"kind": "remap", "dst": 2, "srcA": 2, "constant": [0.3, 0.7, 0.8, 1.12], "value": 1.0},
        {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 2},
        # the rims: the crevices between the rocks, a dark cool stone
        {"kind": "input", "dst": 6, "input": "uv"},
        {"kind": "smoothstep", "dst": 1, "srcA": 6, "constant": [r0a, r0a + e, 0, 0]},
        {"kind": "smoothstep", "dst": 2, "srcA": 6, "constant": [r0b - e, r0b, 0, 0]},
        {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 7},  # r7 = -1 (set below)
        {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},
        {"kind": "smoothstep", "dst": 2, "srcA": 6, "constant": [r1a, r1a + e, 0, 0]},
        {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},
        {"kind": "smoothstep", "dst": 2, "srcA": 6, "constant": [r1b - e, r1b, 0, 0]},
        {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 7},
        {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},
        {"kind": "constant", "dst": 5, "constant": R(rw.hexlin("#76665C")[:3]) + [1.0]},  # the rim core: a warm mid stone
        {"kind": "mixBy", "dst": 3, "srcA": 3, "srcB": 5, "srcC": 1},
    ]
    ops.insert(1, {"kind": "constant", "dst": 7, "constant": [-1.0, -1.0, -1.0, -1.0]})
    return {"name": "rw_ground", "baseColor": 3, "metallic": -1, "roughness": -1, "emission": -1,
            "emissionIntensity": 1.0, "opacity": -1, "ops": ops}


def grass_program():
    ops = [{"kind": "input", "dst": 0, "input": "worldPosition"}]
    ops += meadow_ops(3)
    ops += [
        {"kind": "input", "dst": 4, "input": "objectPosition"},
        {"kind": "gradient", "dst": 4, "srcA": 4, "constant": [0.0, 2.2, 0.0, 0.0], "value": 1.0},
        {"kind": "constant", "dst": 5, "constant": R(LOOK["meadow"][3]) + [1.0]},
        {"kind": "mixBy", "dst": 3, "srcA": 3, "srcB": 5, "srcC": 4},
    ]
    return {"name": "rw_grass", "baseColor": 3, "metallic": -1, "roughness": -1, "emission": -1,
            "emissionIntensity": 1.0, "opacity": -1, "ops": ops}


def canopy_program():
    return {"name": "rw_canopy", "baseColor": 3, "metallic": -1, "roughness": -1, "emission": -1,
            "emissionIntensity": 1.0, "opacity": -1, "ops": [
                {"kind": "input", "dst": 0, "input": "objectPosition"},
                {"kind": "gradient", "dst": 1, "srcA": 0, "constant": [0.0, 1.0 / 14.0, 0.0, -0.75], "value": 1.0},
                {"kind": "input", "dst": 4, "input": "worldPosition"},
                {"kind": "noise", "dst": 2, "srcA": 4, "value": 0.35, "seed": 3},
                {"kind": "mix", "dst": 1, "srcA": 1, "srcB": 2, "value": 0.35},
                {"kind": "oklabRamp", "dst": 3, "srcA": 1, "value": 1.0,
                 "constant": R(rw.P["canopy_core"][:3]) + [1.0], "constant2": R(rw.hexlin("#3A7A3A")[:3]) + [1.0],
                 "constant3": R(rw.P["canopy_lit"][:3]) + [1.0], "constant4": R(rw.hexlin("#9BD24C")[:3]) + [1.0]}]}


def rock_program():
    return {"name": "rw_rock", "baseColor": 3, "metallic": -1, "roughness": -1, "emission": -1,
            "emissionIntensity": 1.0, "opacity": -1, "ops": [
                {"kind": "input", "dst": 0, "input": "worldPosition"},
                {"kind": "noise", "dst": 1, "srcA": 0, "value": 0.2, "seed": 5},
                {"kind": "oklabRamp", "dst": 3, "srcA": 1, "value": 1.0,
                 "constant": R(rw.hexlin("#8C7C78")[:3]) + [1.0], "constant2": R(rw.hexlin("#A88566")[:3]) + [1.0],
                 "constant3": R(rw.hexlin("#BE9A72")[:3]) + [1.0], "constant4": R(rw.hexlin("#C9B08C")[:3]) + [1.0]},
                {"kind": "input", "dst": 4, "input": "objectPosition"},
                {"kind": "noise", "dst": 2, "srcA": 0, "value": 0.9, "seed": 9},
                {"kind": "mix", "dst": 4, "srcA": 4, "srcB": 2, "value": 0.25},
                {"kind": "swizzle", "dst": 4, "srcA": 4, "constant": [1, 1, 1, 1]},
                {"kind": "smoothstep", "dst": 4, "srcA": 4, "constant": [0.5, 0.62, 0, 0]},
                {"kind": "constant", "dst": 5, "constant": R(rw.P["olive"][:3]) + [1.0]},
                {"kind": "mixBy", "dst": 3, "srcA": 3, "srcB": 5, "srcC": 4},
                {"kind": "input", "dst": 6, "input": "cavity"},
                {"kind": "constant", "dst": 5, "constant": R(rw.P["stone_shadow"][:3]) + [1.0]},
                {"kind": "remap", "dst": 6, "srcA": 6, "constant": [0.0, 0.5, 0.0, 0.7], "value": 1.0},
                {"kind": "mixBy", "dst": 3, "srcA": 3, "srcB": 5, "srcC": 6}]}


def cloud_program():
    """Painted clouds (unlit):
    - a wrapped N.L ramp (blue-violet core -> warm white), broken by a billow-scale noise so lobes read inside the
      mass;
    - a warm underside from the cloud sea;
    - a silver rim;
    - the silhouette dissolved toward the sky's horizon colour at grazing angles, so the edge is soft, not a cut-out."""
    c = LOOK["cloud"]
    g = LOOK["cloudGain"]
    return {"name": "rw_cloud", "baseColor": 3, "metallic": -1, "roughness": -1, "emission": -1,
            "emissionIntensity": 1.0, "opacity": -1, "ops": [
                {"kind": "constant", "dst": 6, "constant": [0.3, 0.28, 0.25, 1.0]},
                {"kind": "input", "dst": 0, "input": "normal"},
                {"kind": "gradient", "dst": 1, "srcA": 0, "constant": R(SUN) + [0.5], "value": 0.5},
                {"kind": "input", "dst": 7, "input": "worldPosition"},
                {"kind": "noise", "dst": 2, "srcA": 7, "value": 0.018, "seed": 4},
                {"kind": "remap", "dst": 2, "srcA": 2, "constant": [0.3, 0.7, -0.18, 0.18], "value": 1.0},
                {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},
                {"kind": "remap", "dst": 1, "srcA": 1, "constant": [0.0, 1.0, 0.0, 1.0], "value": 1.0},
                {"kind": "power", "dst": 1, "srcA": 1, "value": 0.85},
                {"kind": "oklabRamp", "dst": 3, "srcA": 1, "value": 1.0, "constant": R([v * g for v in c[0]]) + [1.0],
                 "constant2": R([v * g for v in c[1]]) + [1.0], "constant3": R([v * g for v in c[2]]) + [1.0],
                 "constant4": R([v * g for v in c[3]]) + [1.0]},
                {"kind": "gradient", "dst": 2, "srcA": 0, "constant": [0.0, -1.0, 0.0, 0.0], "value": 1.0},
                {"kind": "remap", "dst": 2, "srcA": 2, "constant": [0.2, 1.0, 0.0, 0.55], "value": 1.0},
                {"kind": "constant", "dst": 5, "constant": R([v * g for v in LOOK["cloudBounce"]]) + [1.0]},
                {"kind": "mixBy", "dst": 3, "srcA": 3, "srcB": 5, "srcC": 2},
                {"kind": "fresnel", "dst": 4, "value": 3.0},
                {"kind": "multiply", "dst": 2, "srcA": 4, "srcB": 6},
                {"kind": "add", "dst": 3, "srcA": 3, "srcB": 2},
                {"kind": "fresnel", "dst": 4, "value": 6.0},
                {"kind": "remap", "dst": 4, "srcA": 4, "constant": [0.0, 1.0, 0.0, 0.75], "value": 1.0},
                {"kind": "constant", "dst": 5, "constant": R(LOOK["horizon"]) + [1.0]},
                {"kind": "mixBy", "dst": 3, "srcA": 3, "srcB": 5, "srcC": 4}]}


def flower_program():
    """Heads in the accent palette by instance (magenta, coral, lilac, buttercup), stems green below the head line."""
    return {"name": "rw_flower", "baseColor": 3, "metallic": -1, "roughness": -1, "emission": -1,
            "emissionIntensity": 1.0, "opacity": -1, "ops": [
                {"kind": "input", "dst": 0, "input": "instanceRandom"},
                {"kind": "oklabRamp", "dst": 3, "srcA": 0, "value": 1.0, "constant": R(rw.P["magenta"][:3]) + [1.0],
                 "constant2": R(rw.P["coral"][:3]) + [1.0], "constant3": R(rw.P["lilac"][:3]) + [1.0],
                 "constant4": R(rw.P["buttercup"][:3]) + [1.0]},
                {"kind": "input", "dst": 1, "input": "objectPosition"},
                {"kind": "swizzle", "dst": 1, "srcA": 1, "constant": [1, 1, 1, 1]},
                {"kind": "smoothstep", "dst": 1, "srcA": 1, "constant": [0.3, 0.34, 0, 0]},
                {"kind": "constant", "dst": 2, "constant": R(rw.P["meadow"][:3]) + [1.0]},
                {"kind": "mixBy", "dst": 3, "srcA": 2, "srcB": 3, "srcC": 1}]}


def node_points(name, asset, points, material, **extra):
    p = {"source": {"kind": "mesh", "asset": asset}, "distribution": {"kind": "points", "points": points},
         "material": material}
    p.update(extra)
    return {"name": name, "kind": "procedural", "procedural": p}




def tinted_canopy_program(name, stops, height=14.0, offset=-0.75):
    """A canopy ramp by height in the tree (core -> skin), with a little world noise -- the Great Tree's program with
    the tree's own colours: the Weeper's limes, the Blossom Puff's pinks."""
    return {"name": name, "baseColor": 3, "metallic": -1, "roughness": -1, "emission": -1,
            "emissionIntensity": 1.0, "opacity": -1, "ops": [
                {"kind": "input", "dst": 0, "input": "objectPosition"},
                {"kind": "gradient", "dst": 1, "srcA": 0, "constant": [0.0, 1.0 / height, 0.0, offset], "value": 1.0},
                {"kind": "input", "dst": 4, "input": "worldPosition"},
                {"kind": "noise", "dst": 2, "srcA": 4, "value": 0.35, "seed": 3},
                {"kind": "mix", "dst": 1, "srcA": 1, "srcB": 2, "value": 0.35},
                {"kind": "oklabRamp", "dst": 3, "srcA": 1, "value": 1.0, "constant": R(stops[0]) + [1.0],
                 "constant2": R(stops[1]) + [1.0], "constant3": R(stops[2]) + [1.0], "constant4": R(stops[3]) + [1.0]}]}


def all_programs():
    return [ground_program(), grass_program(), canopy_program(), rock_program(), cloud_program(), flower_program(),
            tinted_canopy_program("rw_weeper", [rw.hexlin("#3C6E22")[:3], rw.hexlin("#6E9E2A")[:3],
                                                rw.hexlin("#A6C83A")[:3], rw.hexlin("#D2E25A")[:3]], height=12.0, offset=0.0),
            tinted_canopy_program("rw_blossom", [rw.hexlin("#8A3C78")[:3], rw.hexlin("#C9619E")[:3],
                                                 rw.hexlin("#EE94C0")[:3], rw.hexlin("#FFD0E4")[:3]], height=10.0, offset=-1.0),
            tinted_canopy_program("rw_pine", [rw.hexlin("#173F2E")[:3], rw.hexlin("#245C3C")[:3],
                                              rw.hexlin("#3F7E46")[:3], rw.hexlin("#6FA552")[:3]], height=24.0, offset=-0.2)]


def rim_ranges(band):
    """The band's two rim zones as UV.x ranges (profile fractions), for the ground program's crevice colour."""
    rim = [k for k in range(band.NP) if abs(band.PROFILE[k].x) > 0.86 * band.HALF_W]
    out, start = [], rim[0]
    for a, b in zip(rim, rim[1:] + [None]):
        if b is None or b != a + 1:
            out.append((start / band.NP, (a + 1) / band.NP))
            start = b
    assert len(out) == 2, out
    return out


def environment(sun_avgen, bounce_dir_avgen):
    return {
        "intensity": 1.0, "skyIntensity": LOOK["skyIntensity"], "shadowRange": 240.0, "ambientOcclusion": True,
        "contactShadows": False, "depthPrepass": True, "fogColor": R(LOOK["horizon"]), "volumeDensity": 0.0,
        "sky": {"enabled": True, "background": True, "useKeyLight": True, "zenithColor": R(LOOK["zenith"]),
                "horizonColor": R(LOOK["horizon"]), "groundColor": R(LOOK["ground"]), "haze": LOOK["haze"],
                "sunIntensity": 6.0, "intensity": 1.0}}, [
        {"name": "sun", "type": "directional", "direction": R(-Vector(sun_avgen).normalized()), "color": R(LOOK["sunColor"]),
         "intensity": LOOK["sun"], "castsShadow": True, "contactShadow": False, "shadowBias": 0.1},
        {"name": "cloudsea", "type": "directional", "direction": R(Vector(bounce_dir_avgen).normalized()),
         "color": R(rw.P["cloud_bounce"][:3]), "intensity": LOOK["bounce"], "castsShadow": False}]


def post_parameters():
    return {"camera/exposure/compensation": LOOK["exposure"], "post/grade/saturation": LOOK["saturation"],
            "post/grade/contrast": LOOK["contrast"], "post/tonemap/chroma-retention": LOOK["chroma"],
            "post/look/atmospheric": LOOK["atmos"], "post/look/atmosphericDistance": LOOK["atmosDist"],
            "post/look/atmosphericTint": R(LOOK["atmosTint"]),
            "post/bloom/intensity": 0.18, "post/bloom/threshold": 1.2, "post/output/antialias": 0.8}
