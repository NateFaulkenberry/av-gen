"""Rainbow World: the shared look -- palette values, AV Gen material programs, scene environment and post settings.

Imported by the Stage 3 look-development builder and the world builder, so the look-dev scene and the world are lit
and coloured by exactly the same code. Pure Python (mathutils only for the vector helpers), no bpy.
"""
import json
import math

from mathutils import Vector

import rw_common as rw

LOOK = {
    "exposure": 0.2, "saturation": 1.15, "contrast": 1.14, "chroma": 0.65, "atmos": 0.3, "atmosDist": 3200.0, "atmosTint": rw.hexlin("#5FA2E6")[:3],
    "sun": 6.5, "sunColor": rw.hexlin("#FFD49A")[:3], "bounce": 1.6, "skyIntensity": 1.0, "haze": 0.08,
    "zenith": rw.hexlin("#0E36B2")[:3], "horizon": rw.hexlin("#3EAAE2")[:3], "ground": rw.hexlin("#B4B2DC")[:3],
    "meadow": [rw.hexlin("#2A6E2E")[:3], rw.hexlin("#4E9A2C")[:3], rw.hexlin("#6FB038")[:3], rw.hexlin("#9AC23A")[:3]],
    "shade": rw.hexlin("#155A5C")[:3],
    "cloud": [rw.hexlin("#4A50AC")[:3], rw.hexlin("#8A8ED6")[:3], rw.hexlin("#F4CFA4")[:3], rw.hexlin("#FFEAD0")[:3]],
    "cloudBounce": rw.hexlin("#8478CC")[:3], "cloudGain": 0.88,
    "rainbowStrength": 2.0,
}


# ---- Rainbow World 2: the music's ops (16-effect-mapping.md) --------------------------------------------------------
# Effect ops carry a "_role"; `finish` strips it and records the op's 1-based index per program in FX, so the project's
# routes name "material/<program>/op/<index>/<kind>/..." without counting ops by hand.
FX = {}
RING = {"tail": 4.0, "lead": 1.0, "r0": 10.0}         # the kick ring's shape, metres (its radius is routed)
RING_COLOUR = rw.hexlin("#FFE6A6")[:3]                # a warm white-gold: light, not a hue of its own
GLINT_COLOUR = rw.hexlin("#FFF4D8")[:3]


def finish(prog):
    roles = {}
    for i, op in enumerate(prog["ops"]):
        r = op.pop("_role", None)
        if r:
            roles[r] = i + 1
    FX[prog["name"]] = roles
    return prog


def ring_ops(out, a, b, c):
    """The kick ring (drums): a thin shell of light on every surface at the ring's radius from the eye. Camera distance
    is the metric, so the ring is a sphere about the camera and follows whatever face it crosses -- the world's gravity
    turns, so a ground-plane ring would be wrong on most of it. Its radius rides beat.phase (it starts on the beat and
    is gone before the next); its gain is the drum stem's low band. `out` = ring radiance; uses a, b, c as scratch."""
    return [
        {"kind": "input", "dst": a, "input": "cameraDistance"},
        {"kind": "remap", "dst": b, "srcA": a, "constant": [6.0, 10.0, 0.0, 1.0], "value": 1.0, "_role": "ring_in"},
        {"kind": "remap", "dst": a, "srcA": a, "constant": [11.0, 10.0, 0.0, 1.0], "value": 1.0, "_role": "ring_out"},
        {"kind": "multiply", "dst": a, "srcA": a, "srcB": b},
        {"kind": "constant", "dst": c, "constant": [0.0, 0.0, 0.0, 0.0], "_role": "ring_gain"},
        {"kind": "constant", "dst": b, "constant": R(RING_COLOUR) + [1.0], "_role": "ring_colour"},
        {"kind": "multiply", "dst": b, "srcA": b, "srcB": c},
        {"kind": "multiply", "dst": out, "srcA": a, "srcB": b},
    ]


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


def ground_program(name="rw_ground", rim_hex="#76665C"):
    """The band's ground. `rim_hex` colours the rim core between the rocks; the lip variant (rw_ground_lip) is moss,
    for the chunks where the ride swings over a rim: a vegetated lip, not bare rock (Stage 5)."""
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
        {"kind": "constant", "dst": 5, "constant": R(rw.hexlin(rim_hex)[:3]) + [1.0]},  # the rim core
        {"kind": "mixBy", "dst": 3, "srcA": 3, "srcB": 5, "srcC": 1},
    ]
    ops.insert(1, {"kind": "constant", "dst": 7, "constant": [-1.0, -1.0, -1.0, -1.0]})
    ops += ring_ops(5, 1, 2, 4)  # Rainbow World 2: r1, r2, r4 are free once the colour is in r3
    return finish({"name": name, "baseColor": 3, "metallic": -1, "roughness": -1, "emission": 5,
                   "emissionIntensity": 1.0, "opacity": -1, "ops": ops})


def grass_program():
    ops = [{"kind": "input", "dst": 0, "input": "worldPosition"}]
    ops += meadow_ops(3)
    ops += [
        {"kind": "input", "dst": 4, "input": "objectPosition"},
        {"kind": "gradient", "dst": 4, "srcA": 4, "constant": [0.0, 2.2, 0.0, 0.0], "value": 1.0},
        {"kind": "constant", "dst": 5, "constant": R(LOOK["meadow"][3]) + [1.0]},
        {"kind": "mixBy", "dst": 3, "srcA": 3, "srcB": 5, "srcC": 4},
    ]
    ops += ring_ops(5, 1, 2, 6)
    ops += [
        # hat glints: the tips of a few tufts flash. The same tufts each time, but at 30 m/s the field in view is
        # always new, so the subset never reads as fixed. The threshold lowers with the hats (more tufts), the gain
        # carries the decay. r4 is the tuft's height gradient (0 root .. 1 tip).
        {"kind": "input", "dst": 1, "input": "instanceRandom"},
        {"kind": "swizzle", "dst": 1, "srcA": 1, "constant": [2, 2, 2, 2]},
        {"kind": "threshold", "dst": 1, "srcA": 1, "value": 1.01, "_role": "glint_threshold"},
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 4},
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 4},
        {"kind": "constant", "dst": 2, "constant": [0.0, 0.0, 0.0, 0.0], "_role": "glint_gain"},
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 2},
        {"kind": "constant", "dst": 2, "constant": R(GLINT_COLOUR) + [1.0]},
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 2},
        {"kind": "add", "dst": 5, "srcA": 5, "srcB": 1},
    ]
    return finish({"name": "rw_grass", "baseColor": 3, "metallic": -1, "roughness": -1, "emission": 5,
                   "emissionIntensity": 1.0, "opacity": -1, "ops": ops})


def canopy_program():
    return {"name": "rw_canopy", "baseColor": 3, "metallic": -1, "roughness": -1, "emission": -1,
            "emissionIntensity": 1.0, "opacity": -1, "ops": [
                {"kind": "input", "dst": 0, "input": "objectPosition"},
                {"kind": "gradient", "dst": 1, "srcA": 0, "constant": [0.0, 1.0 / 14.0, 0.0, -0.75], "value": 1.0},
                {"kind": "input", "dst": 4, "input": "worldPosition"},
                {"kind": "noise", "dst": 2, "srcA": 4, "value": 0.35, "seed": 3},
                {"kind": "mix", "dst": 1, "srcA": 1, "srcB": 2, "value": 0.35},
                {"kind": "oklabRamp", "dst": 3, "srcA": 1, "value": 1.0,
                 # Stage 7, against the Blender preview: the AV Gen pads read cooler and darker than the Cycles
                 # tree; the two upper stops are warmer and lighter so the lit tops come out yellow-green
                 "constant": R(rw.hexlin("#2A6034")[:3]) + [1.0], "constant2": R(rw.hexlin("#5A9A3C")[:3]) + [1.0],
                 "constant3": R(rw.hexlin("#9ACC44")[:3]) + [1.0], "constant4": R(rw.hexlin("#CCE85A")[:3]) + [1.0]}]}


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
                # a second, broad billow octave (~150 m): a mass seen close and sunlit still breaks into lobes
                # with shaded clefts instead of reading as one flat cream shape
                {"kind": "noise", "dst": 2, "srcA": 7, "value": 0.0065, "seed": 11},
                {"kind": "remap", "dst": 2, "srcA": 2, "constant": [0.3, 0.7, -0.3, 0.12], "value": 1.0},
                {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},
                {"kind": "remap", "dst": 1, "srcA": 1, "constant": [0.0, 1.0, 0.0, 1.0], "value": 1.0},
                {"kind": "power", "dst": 1, "srcA": 1, "value": 0.85},
                # only the most sun-facing lobes reach the top stop: a near, sunlit mass keeps a gradient
                {"kind": "remap", "dst": 1, "srcA": 1, "constant": [0.0, 1.0, 0.0, 0.86], "value": 1.0},
                {"kind": "oklabRamp", "dst": 3, "srcA": 1, "value": 1.0, "constant": R([v * g for v in c[0]]) + [1.0],
                 "constant2": R([v * g for v in c[1]]) + [1.0], "constant3": R([v * g for v in c[2]]) + [1.0],
                 "constant4": R([v * g for v in c[3]]) + [1.0]},
                {"kind": "gradient", "dst": 2, "srcA": 0, "constant": [0.0, -1.0, 0.0, 0.0], "value": 1.0},
                {"kind": "remap", "dst": 2, "srcA": 2, "constant": [0.1, 1.0, 0.0, 0.7], "value": 1.0},
                {"kind": "constant", "dst": 5, "constant": R([v * g for v in LOOK["cloudBounce"]]) + [1.0]},
                {"kind": "mixBy", "dst": 3, "srcA": 3, "srcB": 5, "srcC": 2},
                {"kind": "fresnel", "dst": 4, "value": 3.0},
                {"kind": "multiply", "dst": 2, "srcA": 4, "srcB": 6},
                {"kind": "add", "dst": 3, "srcA": 3, "srcB": 2},
                {"kind": "fresnel", "dst": 4, "value": 6.0},
                {"kind": "remap", "dst": 4, "srcA": 4, "constant": [0.0, 1.0, 0.0, 0.75], "value": 1.0},
                {"kind": "constant", "dst": 5, "constant": R(LOOK["horizon"]) + [1.0]},
                {"kind": "mixBy", "dst": 3, "srcA": 3, "srcB": 5, "srcC": 4},
                # Rainbow World 2: the synth's light on the clouds (1 = as built; routed up by the synth)
                {"kind": "constant", "dst": 5, "constant": [1.0, 1.0, 1.0, 1.0], "_role": "cloud_glow"},
                {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 5}]}


FLOOR_STOPS = ("#1A3AA8", "#4A78CE", "#DCBE9A", "#F4E2C8")  # deeper than the clouds, so the heads on it read


def cloud_floor_program(base_y, top_h=650.0):
    """The cloud sea's floor (unlit). Seen from the ride it filled half of a crossing's frame as one flat cream wall,
    because every up-facing billow took the ramp's top stop. Here height leads: the deep troughs between billows are
    blue-violet (R1's shadowed cloud valleys), the flanks lavender, and only the heads catch the white-gold light, with
    the sun's N.L and a billow noise breaking it up. The far floor still dissolves to the horizon colour."""
    g = LOOK["cloudGain"]
    stops = [rw.hexlin(h)[:3] for h in FLOOR_STOPS]
    return {"name": "rw_cloudfloor", "baseColor": 3, "metallic": -1, "roughness": -1, "emission": -1,
            "emissionIntensity": 1.0, "opacity": -1, "ops": [
                {"kind": "input", "dst": 7, "input": "worldPosition"},
                {"kind": "swizzle", "dst": 1, "srcA": 7, "constant": [1, 1, 1, 1]},
                {"kind": "remap", "dst": 1, "srcA": 1, "constant": [base_y + 40.0, base_y + top_h, 0.0, 1.0], "value": 1.0},
                {"kind": "power", "dst": 1, "srcA": 1, "value": 0.7},
                {"kind": "input", "dst": 0, "input": "normal"},
                {"kind": "gradient", "dst": 2, "srcA": 0, "constant": R(SUN) + [0.5], "value": 0.5},
                {"kind": "remap", "dst": 2, "srcA": 2, "constant": [0.0, 1.0, -0.3, 0.3], "value": 1.0},
                {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},
                {"kind": "noise", "dst": 2, "srcA": 7, "value": 0.0022, "seed": 9},
                {"kind": "remap", "dst": 2, "srcA": 2, "constant": [0.3, 0.7, -0.28, 0.28], "value": 1.0},
                {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},
                {"kind": "remap", "dst": 1, "srcA": 1, "constant": [0.0, 1.0, 0.0, 1.0], "value": 1.0},
                {"kind": "oklabRamp", "dst": 3, "srcA": 1, "value": 1.0, "constant": R([v * g for v in stops[0]]) + [1.0],
                 "constant2": R([v * g for v in stops[1]]) + [1.0], "constant3": R([v * g for v in stops[2]]) + [1.0],
                 "constant4": R([v * g for v in stops[3]]) + [1.0]},
                {"kind": "fresnel", "dst": 4, "value": 8.0},
                {"kind": "remap", "dst": 4, "srcA": 4, "constant": [0.0, 1.0, 0.0, 0.4], "value": 1.0},
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


def rainbow_program(name, reverse=False, alpha_peak=0.66, strength=2.0, bands=5.0, flow_speed=0.18):
    """The arch as a program (Rainbow World 2), so light can move along it. The Stage 2-8 arch was a glTF with a
    baked spectral emissive texture and alpha (engine_assets.rainbow_textures); this rebuilds the same bow from the
    tube's UVs -- u across (0 inner, 1 outer), v along (0 and 1 are the feet) -- and adds what moves:
      flow   bright bands drifting along the arch, always on, their depth set by the synth (`flow_gain`);
      fronts on the backbeat a pair of fronts runs up from both feet to the crown in one beat (`front_*`, beat.phase),
             their brightness the drum stem's clap band (`front_gain`);
      gain   the arch's own brightness (`arch_gain`): the synth's level, the phrase and the drop.
    The spectrum is two four-stop OKLab ramps (violet-blue-cyan-green, green-yellow-orange-red), so it never passes
    through grey; every moving term multiplies that spectrum, so a band is the bow brighter, never another hue."""
    sp = [(t, c) for t, c in rw.SPECTRUM]

    def at(t):
        for (t0, c0), (t1, c1) in zip(sp, sp[1:]):
            if t <= t1:
                f = (t - t0) / max(1e-9, t1 - t0)
                return [c0[i] + (c1[i] - c0[i]) * f for i in range(3)]
        return list(sp[-1][1])
    lo = [R(at(x)) + [1.0] for x in (0.0, 0.17, 0.33, 0.5)]
    hi = [R(at(x)) + [1.0] for x in (0.5, 0.67, 0.83, 1.0)]
    if reverse:
        lo, hi = [c for c in reversed(hi)], [c for c in reversed(lo)]
    ops = [
        {"kind": "input", "dst": 0, "input": "uv"},
        {"kind": "swizzle", "dst": 1, "srcA": 0, "constant": [0, 0, 0, 0]},
        {"kind": "remap", "dst": 2, "srcA": 1, "constant": [0.0, 0.5, 0.0, 1.0], "value": 1.0},
        {"kind": "oklabRamp", "dst": 3, "srcA": 2, "value": 1.0, "constant": lo[0], "constant2": lo[1],
         "constant3": lo[2], "constant4": lo[3]},
        {"kind": "remap", "dst": 2, "srcA": 1, "constant": [0.5, 1.0, 0.0, 1.0], "value": 1.0},
        {"kind": "oklabRamp", "dst": 4, "srcA": 2, "value": 1.0, "constant": hi[0], "constant2": hi[1],
         "constant3": hi[2], "constant4": hi[3]},
        {"kind": "threshold", "dst": 2, "srcA": 1, "value": 0.5},
        {"kind": "mixBy", "dst": 3, "srcA": 3, "srcB": 4, "srcC": 2},
        # coverage across the tube (soft both sides) and at the feet (they dissolve into the meadow)
        {"kind": "remap", "dst": 2, "srcA": 1, "constant": [0.02, 0.26, 0.0, alpha_peak], "value": 1.0},
        {"kind": "remap", "dst": 4, "srcA": 1, "constant": [0.98, 0.74, 0.0, 1.0], "value": 1.0},
        {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 4},
        {"kind": "swizzle", "dst": 4, "srcA": 0, "constant": [1, 1, 1, 1]},
        {"kind": "remap", "dst": 5, "srcA": 4, "constant": [0.0, 0.1, 0.0, 1.0], "value": 1.0},
        {"kind": "remap", "dst": 6, "srcA": 4, "constant": [1.0, 0.9, 0.0, 1.0], "value": 1.0},
        {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 6},
        {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 5},
        # flow: cos bands along v drifting with the transport's time
        {"kind": "remap", "dst": 5, "srcA": 4, "constant": [0.0, 1.0, 0.0, bands], "value": 0.0},
        {"kind": "input", "dst": 6, "input": "time"},
        {"kind": "remap", "dst": 6, "srcA": 6, "constant": [0.0, 1.0, 0.0, -flow_speed], "value": 0.0},
        {"kind": "add", "dst": 5, "srcA": 5, "srcB": 6},
        {"kind": "palette", "dst": 5, "srcA": 5, "value": 0.0, "constant": [0.5, 0.5, 0.5, 0.0],
         "constant2": [0.5, 0.5, 0.5, 0.0], "constant3": [1.0, 1.0, 1.0, 0.0], "constant4": [0.0, 0.0, 0.0, 0.0]},
        {"kind": "power", "dst": 5, "srcA": 5, "value": 6.0},
        {"kind": "constant", "dst": 6, "constant": [0.0, 0.0, 0.0, 0.0], "_role": "flow_gain"},
        {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 6},
        # the backbeat fronts: left foot (v = 0) up to the crown, right foot (v = 1) down to it; a sharp lead, a tail
        {"kind": "remap", "dst": 6, "srcA": 4, "constant": [-0.08, 0.0, 0.0, 1.0], "value": 1.0, "_role": "front_l_in"},
        {"kind": "remap", "dst": 7, "srcA": 4, "constant": [0.015, 0.0, 0.0, 1.0], "value": 1.0, "_role": "front_l_out"},
        {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 7},
        {"kind": "remap", "dst": 7, "srcA": 4, "constant": [0.985, 1.0, 0.0, 1.0], "value": 1.0, "_role": "front_r_in"},
        {"kind": "remap", "dst": 1, "srcA": 4, "constant": [1.08, 1.0, 0.0, 1.0], "value": 1.0, "_role": "front_r_out"},
        {"kind": "multiply", "dst": 7, "srcA": 7, "srcB": 1},
        {"kind": "add", "dst": 6, "srcA": 6, "srcB": 7},
        {"kind": "constant", "dst": 7, "constant": [0.0, 0.0, 0.0, 0.0], "_role": "front_gain"},
        {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 7},
        {"kind": "add", "dst": 5, "srcA": 5, "srcB": 6},
        {"kind": "constant", "dst": 6, "constant": [strength, strength, strength, 0.0], "_role": "arch_gain"},
        {"kind": "add", "dst": 5, "srcA": 5, "srcB": 6},
        {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 5},
    ]
    return finish({"name": name, "baseColor": -1, "metallic": -1, "roughness": -1, "emission": 3,
                   "emissionIntensity": 1.0, "opacity": 2, "ops": ops})


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
    return [rainbow_program("rw_rainbow"), rainbow_program("rw_rainbow2", reverse=True, alpha_peak=0.22, strength=1.0),
            ground_program(), ground_program("rw_ground_lip", "#4C7A2E"), grass_program(), canopy_program(), rock_program(), finish(cloud_program()), flower_program(),
            tinted_canopy_program("rw_weeper", [rw.hexlin("#5E9426")[:3], rw.hexlin("#8EBE2E")[:3],
                                                rw.hexlin("#BEDC44")[:3], rw.hexlin("#E2EE6A")[:3]], height=12.0, offset=0.0),
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
        "intensity": 1.0, "skyIntensity": LOOK["skyIntensity"], "shadowRange": 240.0, "shadowCascades": 2, "ambientOcclusion": False,
        "contactShadows": False, "depthPrepass": True, "fogColor": R(LOOK["horizon"]), "volumeDensity": 0.0,
        "sky": {"enabled": True, "background": True, "useKeyLight": True, "zenithColor": R(LOOK["zenith"]),
                "horizonColor": R(LOOK["horizon"]), "groundColor": R(LOOK["ground"]), "haze": LOOK["haze"],
                "sunIntensity": 6.0, "sunGlow": 0.32, "intensity": 1.0}}, [
        {"name": "sun", "type": "directional", "direction": R(-Vector(sun_avgen).normalized()), "color": R(LOOK["sunColor"]),
         "intensity": LOOK["sun"], "castsShadow": True, "contactShadow": False, "shadowBias": 0.25},
        {"name": "cloudsea", "type": "directional", "direction": R(Vector(bounce_dir_avgen).normalized()),
         "color": R(rw.P["cloud_bounce"][:3]), "intensity": LOOK["bounce"], "castsShadow": False}]


def post_parameters():
    return {"camera/exposure/compensation": LOOK["exposure"], "post/grade/saturation": LOOK["saturation"],
            "post/grade/contrast": LOOK["contrast"], "post/tonemap/chroma-retention": LOOK["chroma"],
            "post/look/atmospheric": LOOK["atmos"], "post/look/atmosphericDistance": LOOK["atmosDist"],
            "post/look/atmosphericTint": R(LOOK["atmosTint"]),
            "post/grade/lift": [-0.006, -0.006, -0.004],
            "post/bloom/intensity": 0.18, "post/bloom/threshold": 1.2, "post/output/antialias": 0.8}
