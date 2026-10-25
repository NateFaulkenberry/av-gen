"""Aurora Borealis: the art direction -- the palette states, the land's painter's program, the sky, the aurora, the
water and the post. Everything that decides how the scene looks, in one place; build.py assembles it.

The method is Glass Caldera's (examples/sonic-abstract/digital-alpine): no lights, no shadows, no fog volume. The land
is unlit and coloured by a short program from its facet normals; distance haze is painted in the program in the sky's
own colour. What is new is that the light comes from the aurora: facets that face the aurora's side of the sky take
the aurora's low colour, scaled by the same value that drives the aurora's brightness, so the land brightens with it.

One palette state (look.palette) drives the aurora's three colours, the land's aurora-lit facet colour and the water's
glow and flow-streak colours, so a palette change moves sky, land and river together.
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
from sonic_vfx.kit import hexrgb  # noqa: E402

# ---- the fixed colours (sRGB hex; the engine takes linear) ----------------------------------------------------------
ZENITH = "#04061c"          # deep indigo, nearly black
HORIZON = "#2a2466"         # the dusk band: muted violet
SKY_GROUND = "#05040f"
SHADOW = "#02020c"          # facets facing away from the sky
SKYLIT = "#0c0a2a"          # facets facing up: blue-violet
CREST = "#6a62ae"
RIM = "#8a70ff"              # the glass rim: facets turned edge-on to the eye catch the aurora's colour           # the pale lavender on high, sky-facing facets (a hint of snow light, never white)
HAZE = "#3e3a8c"            # distance haze: the horizon's colour, a little darker
WATER_DEEP = "#03030c"

# where the aurora light comes from: up, and toward the side of the sky the ribbons favour (world -z, +x)
AURORA_DIR = (0.55, 0.38, -0.74)

# ---- the palette: the curated states the music blends between (music.py drives palette/position along them) --------
# roles: auroraLow (the bright lower rims), auroraMid (the body), auroraTop (the fading tops), facetLit (the land's
# aurora-lit facets), waterGlow (the river's under-glow and its first streak colour)
LOOKS = [
    ("Dusk Indigo", {"auroraLow": "#8a6cff", "auroraMid": "#4636c8", "auroraTop": "#20186e",
                     "facetLit": "#6a54d8", "waterGlow": "#3a2c90"}),
    ("Violet Veil", {"auroraLow": "#d58cff", "auroraMid": "#7a48f0", "auroraTop": "#2b2aa8",
                     "facetLit": "#a070f0", "waterGlow": "#6a40c0"}),
    ("Electric", {"auroraLow": "#ff6ad0", "auroraMid": "#3f7cff", "auroraTop": "#5a2ad0",
                  "facetLit": "#7a8cff", "waterGlow": "#4060e0"}),
    ("Rose Lift", {"auroraLow": "#ff6fd0", "auroraMid": "#ff58c0", "auroraTop": "#8a3ae8",
                   "facetLit": "#e080d8", "waterGlow": "#d060b0"}),
]
REST_LOOK = 1                # the colours authored into the effect and program (the palette replaces them)
REST_POSITION = 0.6          # palette/position with no music: between dusk indigo and the violet veil


def look_colors(i):
    return {k: hexrgb(v) for k, v in LOOKS[i][1].items()}


# ---- the land's program ------------------------------------------------------------------------------------------
class Prog:
    """A material program built op by op, with labels, so routes and palette bindings name ops by label, not by a
    hand-counted index."""

    def __init__(self, name):
        self.name = name
        self.ops = []
        self.labels = {}

    def op(self, kind, dst, label=None, **kw):
        o = {"kind": kind, "dst": dst}
        o.update(kw)
        self.ops.append(o)
        if label:
            self.labels[label] = len(self.ops)
        return self

    def path(self, label, field="constant"):
        return "material/%s/op/%d/%s/%s" % (self.name, self.labels[label], self.ops[self.labels[label] - 1]["kind"],
                                            field)


HAZE_NEAR, HAZE_FAR, HAZE_CURVE = 300.0, 9500.0, 0.55   # metres: the haze comes in from 300 m to 9.5 km
LINE_FADE_NEAR, LINE_FADE_FAR = 400.0, 4500.0          # metres: the crease lines (and their glints) fade out by 4.5 km
CREST_LO, CREST_HI = 110.0, 320.0                       # metres: where the pale crest light comes in
AURORA_WRAP_POWER = 4.0
EDGE_GLOW = 0.5   # the edges' resting glow (Gate B)

# pass 3: the glints (22-research-pass3-scene.md s5). A comb over each crease's random value v (baked in TEXCOORD_0.y)
# picks a deterministic subset; its smoothstep rests above 1 (nothing lit) and an instrument's envelope, routed onto
# its constant, lowers it by up to GATE_DROP: a hit lights a subset, a louder one more of it, more fully.
GATE_REST = [1.05, 1.10, 0.0, 0.0]
GATE_DROP = 0.16
# (label, teeth, glint line width in px, colour, intensity, plane fill). Pass 5 (the owner: "the Moog and Bass are not
# translating well enough onto the mountains ... try to illuminate more of the mountains or find a way to make edges
# brighter thicker or more prominent"): each instrument lights its picked creases as wide lines AND fills the facets on
# either side of them (`fill`, a share of the line's brightness), so a note lights planes, not hairlines; the whole
# bank's ridgelines (the resting edge lines) glow with the instrument's sustained level (music.py routes it onto
# `edgeGlow`); and the travelling band of pass 4 is gone -- every note lights its subset at once, everywhere on its bank.
GLINTS = {
    "L": [("moog", 3.0, 7.0, "#ff9fdc", 6.0, 0.24)],     # pale pink: the Moog
    "R": [("bass", 2.0, 5.0, "#6a52ff", 2.2, 0.9)],     # deep blue-violet: the bass (planes first, lines on them)
}


def rock(side) -> Prog:
    """Every landform, unlit; one program per bank (`side` "L" or "R", pass 3). Facet colour = shadow + sky-facing
    blue-violet + aurora-facing aurora light x glow; high sky-facing facets pale to lavender (and flare with a crash);
    the glass rim; the crease lines; this bank's instrument glints; then the distance haze. At most 64 ops (the
    ceiling, ADR-1661)."""
    p = Prog("abRock" + side)
    c4 = lambda h: hexrgb(h) + [1.0]   # noqa: E731
    lit = look_colors(REST_LOOK)["facetLit"] + [1.0]
    (p.op("input", 0, input="worldPosition")
      .op("input", 1, input="normal")
      .op("input", 2, input="cameraDistance")                                                   # metres
      .op("gradient", 3, srcA=1, constant=list(AURORA_DIR) + [0.0], value=1.0, label="auroraDir")
      .op("power", 3, srcA=3, value=AURORA_WRAP_POWER)
      .op("constant", 4, constant=lit, label="facetLit")                                        # palette
      .op("multiply", 3, srcA=3, srcB=4)
      .op("constant", 4, constant=[1.5, 1.5, 1.5, 1.0], label="auroraGlow")                  # the glow (routes)
      .op("multiply", 3, srcA=3, srcB=4)
      .op("gradient", 4, srcA=1, constant=[0.0, 1.0, 0.0, 0.0], value=1.0)                      # sky-facing
      .op("constant", 5, constant=c4(SKYLIT), label="skyLit")
      .op("multiply", 5, srcA=4, srcB=5)
      .op("add", 3, srcA=3, srcB=5)
      .op("constant", 5, constant=c4(SHADOW), label="shadow")
      .op("add", 3, srcA=3, srcB=5)                                                             # the facet
      .op("swizzle", 5, srcA=0, constant=[1.0, 1.0, 1.0, 1.0])                                  # y
      .op("smoothstep", 5, srcA=5, constant=[CREST_LO, CREST_HI, 0.0, 0.0])
      .op("multiply", 5, srcA=5, srcB=4)                                                        # the crest mask
      .op("constant", 6, constant=c4(CREST), label="crest")
      .op("mixBy", 3, srcA=3, srcB=6, srcC=5)
      # pass 3: a crash's flash on the high sky-facing facets (crashGlow routes this up from 0; white-violet)
      .op("constant", 6, constant=[0.0, 0.0, 0.0, 0.0], label="crashCrest")
      .op("multiply", 6, srcA=6, srcB=5)
      .op("add", 3, srcA=3, srcB=6)
      .op("fresnel", 6, value=6.0)                                                              # glassy rim
      .op("constant", 7, constant=c4(RIM), label="rim")                                         # palette
      .op("multiply", 6, srcA=6, srcB=7)
      .op("add", 3, srcA=3, srcB=6)
      # the ridge edges: TEXCOORD_0 = (metres to this facet's chosen crease over its importance, the crease's random
      # value), baked by world.edge_uvs; a ~2 px anti-aliased line (metres over the pixel footprint)
      .op("input", 0, input="uv")
      .op("input", 1, input="footprint")
      # floored before the reciprocal: a zero footprint made inf, u = 0 times inf made NaN, and the bloom spread one
      # NaN pixel over the whole frame (two black frames in ten seconds of the first Gate B build)
      .op("constant", 6, constant=[1.0e-3, 1.0e-3, 1.0e-3, 1.0e-3])
      .op("add", 1, srcA=1, srcB=6)
      .op("power", 1, srcA=1, value=-1.0)
      .op("multiply", 5, srcA=0, srcB=1)
      .op("smoothstep", 5, srcA=5, constant=[1.6, 0.0, 0.0, 0.0])
      .op("swizzle", 5, srcA=5, constant=[0.0, 0.0, 0.0, 0.0])
      # the lines' distance fade, linear in metres, kept in r4 for the glints too
      .op("gradient", 4, srcA=2, constant=[1.0, 0.0, 0.0, LINE_FADE_FAR / (LINE_FADE_FAR - LINE_FADE_NEAR)],
          value=-1.0 / (LINE_FADE_FAR - LINE_FADE_NEAR))
      .op("multiply", 5, srcA=5, srcB=4)
      .op("constant", 6, constant=lit, label="edgeColor")                                       # palette
      .op("multiply", 5, srcA=5, srcB=6)
      .op("constant", 6, constant=[EDGE_GLOW, EDGE_GLOW, EDGE_GLOW, 1.0], label="edgeGlow")   # routes; 0 = off
      .op("multiply", 5, srcA=5, srcB=6)
      .op("add", 3, srcA=3, srcB=5))
    for label, teeth, width, color, gain, fill in GLINTS[side]:
        # the subset: a comb over each crease's random value v (TEXCOORD_0.y); its gate rests closed and the note and
        # the sustained level, routed onto its constant, open it (louder: more of the bank, brighter), r7
        (p.op("swizzle", 7, srcA=0, constant=[1.0, 1.0, 1.0, 1.0])                               # v
          .op("palette", 7, srcA=7, value=0.0, label=label, constant=[0.5, 0.5, 0.5, 0.0],
              constant2=[0.5, 0.5, 0.5, 0.0], constant3=[teeth, teeth, teeth, 0.0], constant4=[0.0, 0.0, 0.0, 0.0])
          .op("smoothstep", 7, srcA=7, constant=list(GATE_REST), label=label + "Gate")
          .op("multiply", 7, srcA=7, srcB=4))                                                     # the distance fade
        # a wide line on the picked creases (routing `<label>Line` swells it) plus the fill on their facets, r6
        (p.op("multiply", 6, srcA=0, srcB=1)
          .op("smoothstep", 6, srcA=6, constant=[width, 0.0, 0.0, 0.0], label=label + "Line")
          .op("swizzle", 6, srcA=6, constant=[0.0, 0.0, 0.0, 0.0])
          .op("constant", 5, constant=[fill, fill, fill, 0.0], label=label + "Fill")
          .op("add", 6, srcA=6, srcB=5)
          .op("multiply", 6, srcA=6, srcB=7)
          .op("constant", 7, constant=[round(v * gain, 4) for v in hexrgb(color)] + [1.0], label=label + "Color")
          .op("multiply", 6, srcA=6, srcB=7)
          .op("add", 3, srcA=3, srcB=6))
    (p.op("constant", 6, constant=c4(HAZE), label="haze")
      .op("gradient", 7, srcA=2, constant=[1.0, 0.0, 0.0, -HAZE_NEAR / (HAZE_FAR - HAZE_NEAR)],
          value=1.0 / (HAZE_FAR - HAZE_NEAR))
      .op("power", 7, srcA=7, value=HAZE_CURVE)
      .op("mixBy", 3, srcA=3, srcB=6, srcC=7))
    assert len(p.ops) <= 64, (side, len(p.ops))
    return p


ROCK_L = rock("L")
ROCK_R = rock("R")
ROCKS = [ROCK_L, ROCK_R]


def rock_program(prog):
    return {"name": prog.name, "ops": prog.ops, "baseColor": -1, "metallic": -1, "roughness": -1,
            "emission": 3, "emissionIntensity": 1.0, "opacity": -1}


def rock_material(prog):
    return {"baseColor": [0.0, 0.0, 0.0], "emissiveColor": [0.0, 0.0, 0.0], "emissiveIntensity": 0.0,
            "roughness": 1.0, "metallic": 0.0, "unlit": True, "program": prog.name}


def unshadowed_key():
    """Authoring a light turns the default (shadowed) key off; nothing here is lit, so it casts nothing."""
    return {"id": "key", "name": "key", "type": "directional", "direction": [-0.4, -1.0, -0.35],
            "color": [1.0, 0.97, 0.92], "intensity": 3.0, "temperature": 5600.0, "softness": 1.0,
            "castsShadow": False, "contactShadow": False}


# ---- sky and environment -----------------------------------------------------------------------------------------
def environment():
    return {
        "intensity": 0.0, "background": hexrgb(HORIZON), "fogColor": hexrgb(HAZE), "volumeDensity": 0.0,
        "skyIntensity": 1.0,
        # the depth prepass saves more overdraw than it costs (01 s3.4); AO is pure cost in an unlit scene
        "depthPrepass": True, "ambientOcclusion": False, "contactShadows": False,
        "sky": {"enabled": True, "zenithColor": hexrgb(ZENITH), "horizonColor": hexrgb(HORIZON),
                "groundColor": hexrgb(SKY_GROUND), "haze": 0.35, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }


# ---- the aurora ----------------------------------------------------------------------------------------------------
RIBBONS = {"ribbons": True, "ribbonAltitude": 1500.0, "ribbonScale": 4500.0, "ribbonWidth": 0.045, "ribbonSwirl": 1.9,
           "ribbonEdge": 0.3, "ribbonSteps": 48.0, "ribbonDistance": 24000.0, "ribbonCoverage": 0.75,
           # E3's travelling kick front, gentler than its default 2x so the kick stays under the flash budget
           "pulseGain": 1.0, "pulseThicken": 0.3}


def aurora_effect():
    c = look_colors(REST_LOOK)
    return {"id": "aurora", "name": "aurora", "type": "aurora", "owner": {"kind": "world"}, "enabled": True,
            "order": 0, "activation": "always", "style": "",
            "timing": {"delay": 0.0, "fadeIn": 0.0, "fadeOut": 0.0, "lifetime": 0.0, "repeatSeconds": 0.0,
                       "windowSeconds": 6.0, "windowStart": 0.0},
            "parameters": {
                "appearance": {"edgeBrightness": 5.0, "emission": 0.6, "filaments": 5.0, "horizonGlow": 0.55,
                               "intensity": 3.0, "lowColor": c["auroraLow"], "midColor": c["auroraMid"],
                               "topColor": c["auroraTop"], "opacity": 0.85, "sparkle": 0.2},
                # ADR-939's own mix response off: every musical input comes through the stem routes (music.py)
                "audio": {"bass": 0.0, "beat": 0.0, "glints": 0.0, "high": 0.0, "lowMid": 0.0, "mid": 0.0,
                          "sensitivity": 0.0, "spectrumShape": 0.0},
                "rainbow": {"brightness": 1.0, "enabled": False, "hueOffset": 0.0, "saturation": 0.85, "scale": 1.1,
                            "speed": 0.0},
                "shape": dict({"anchor": "camera", "anchorPosition": [0.0, 0.0, 0.0], "baseHeight": 0.0,
                               "complexity": 90.0, "curtainCount": 3.0, "curtainHeight": 900.0,
                               "driftSpeed": 0.05, "flowSpeed": 0.015, "layerSpacing": 0.35, "radius": 6000.0,
                               "turbulence": 0.3, "verticalSpeed": 0.03, "waveAmplitude": 0.5, "waveScale": 4.0},
                              **RIBBONS)},
            # the ground term reaches lit PBR and water only; the land is unlit (its program carries the aurora
            # light), so this only tints the river's body a trace
            "ground": {"color": c["auroraMid"], "falloff": 2.0, "intensity": 0.05, "mode": "subtle", "radius": 600.0},
            "flow": {"field": "", "influence": 0.0}}


def _timing(**kw):
    t = {"delay": 0.0, "fadeIn": 0.0, "fadeOut": 0.0, "lifetime": 0.0, "repeatSeconds": 0.0, "windowSeconds": 6.0,
         "windowStart": 0.0}
    t.update(kw)
    return t


# ---- the fireballs (pass 4, ADR-1663; pass 5, ADR-1665: "make the fireball look more like a realistic fireball" -- a
# blackbody-coloured flame billboard of domain-warped fbm tongues with a sooty fringe, a smoke trail and embers): the owner replaced the shooting stars with "a dynamic ball of light ... it should
# be able to throw lots of light onto the geometry below", then: "make them appear as though they are shooting from
# behind the camera over the valley and change them to big glowing vibrant fireballs". Launched from behind and a little
# above the camera, each enters the frame from its near edge, close and large, and arcs away over the valley toward the
# far mountains, shrinking; each crash its own heading (`spread`), lighting land, water and riverbed as it flies. Fire
# orange: the aurora's complement, so it reads as the most vibrant thing in the frame. One instance per slot of the
# pool of two (music.py). (launch, target) in the camera's frame: right, up, forward (m)
BALLS = {"ballA": ((18.0, 22.0, -35.0), (-150.0, 230.0, 2600.0)),
         "ballB": ((-18.0, 26.0, -30.0), (150.0, 250.0, 2500.0))}
BALL_COLOR = "#ffa060"       # the light it casts: warm fire
BALL_INTENSITY = 0.6         # crashSize adds up to 0.6 (a phrase-start crash throws a brighter fireball)


def ball_effect(name):
    (l0, l1) = BALLS[name]
    return {"id": name, "name": name, "type": "groundPulse", "owner": {"kind": "world"}, "enabled": True, "order": 0,
            "activation": "trigger", "trigger": {"source": "signal", "name": "signal.%sEv" % name, "threshold": 0.0},
            # it ignites over 0.6 s as it comes over the camera: large but dim while close, at full fire as it flies away
            # (brighter sooner, a ball passing overhead swung the frame by 0.1-0.18 in 267 ms: the flash gate)
            "style": "", "timing": _timing(fadeIn=0.6, fadeOut=0.8, lifetime=2.2),
            "parameters": {
                "appearance": {"color": hexrgb(BALL_COLOR), "edgeColor": [1.0, 0.86, 0.55], "edgeIntensity": 5.0,
                               "intensity": BALL_INTENSITY, "rainbow": False, "rainbowBrightness": 1.0,
                               "rainbowSaturation": 0.85, "rainbowScale": 0.025, "rainbowSpeed": 0.35, "width": 1.0},
                "propagation": {"kind": "ball", "direction": "explicit", "beamRadius": 0.0, "startOffset": 0.0,
                                "speed": 60.0, "range": 220.0, "frontWidth": 14.0, "trailLength": 46.0,
                                "falloff": 1.6, "verticalExtent": 18.0, "verticalGrowth": 0.0, "ringCount": 0.0,
                                "forwardWeight": 1.0, "targetWeight": 0.0, "velocityWeight": 0.0,
                                "explicitDirection": [0.0, 0.0, -1.0],
                                "path": {"ease": 0.35},
                                "ball": {"launch": list(l0), "target": list(l1), "travel": 1.4, "arc": 120.0,
                                         "lightRadius": 150.0, "range": 1100.0, "coreSize": 9.0, "haloSize": 30.0,
                                         "coronaColor": [1.0, 0.36, 0.06], "coronaIntensity": 2.0,
                                         "coronaSize": 85.0, "flicker": 0.8, "trail": 220.0, "spread": 900.0,
                                         # ADR-1665 (pass 5): the flame billboard, sooty, shedding embers -- the
                                         # most convincing of the three looks compared (41-research-pass5-fireball.md)
                                         "look": 1, "smoke": 0.7, "embers": 0.8}},
                "response": {"emissive": 0.0, "foliage": 1.0, "ground": 1.0, "surface": 1.0},
                "source": {"kind": "camera", "groundOffset": 0.0},
                "sparkle": {"density": 0.9, "enabled": False, "fadeDistance": 85.0, "intensity": 1.8, "seed": 1,
                            "size": 0.3, "speed": 0.6}}}


def ball_effects():
    return [ball_effect(n) for n in BALLS]


# ---- the river light (pass 3, 20b-brief-river-light.md; ADR-1662's path wave on the water's own `river` feature) --
# Kick: one hot core; snare: one core too (owner, pass 4: the twin "looks like two beams"), told apart by its colour,
# its lane and its faster, shorter timing. Each hit is its own front fading on its own envelope
# (an ADR-981 trigger with a lifetime), so a beam finishes before the next hit and overlap is a fading one. The light
# sits under the reflection, ripples the mirror behind it, and lights the facets of the banks, the slopes and the
# riverbed (BED) by their orientation. Colours from the scene's own family (owner, 2026-10-24: no amber or cyan).
BEAM_PAIRS = {   # (kick, snare)
    "magenta-lilac": ("#ff2f92", "#e2d2ff"),
    "rose-orchid": ("#e8346c", "#cf7cff"),
    "fuchsia-lavender": ("#ff3cd0", "#ab9bff"),
}
BEAM_PAIR = os.environ.get("AB_BEAM_PAIR", "magenta-lilac")
CAM_H_WATER = float(os.environ.get("AB_CAM_H", "110"))   # the camera's height above the water (build.CAM_POS)
# pass 5: the lake is 2 km deep, so the light must show from close in -- it launches 70 m out and fades in over 150 m
# (the river's 160 m and 500 m left only the last strip by the far shore lit)
START_OFFSET = float(os.environ.get("AB_START", "70"))
NEAR_FADE = float(os.environ.get("AB_NEARFADE", "150"))
BED_VARIANT = os.environ.get("AB_BED", "coarse")


def _whiter(hexcol, k):
    c = hexrgb(hexcol)
    return [round(v + (1.0 - v) * k, 4) for v in c]


def river_beam(name, signal, color, *, intensity, edge, travel, rng, ease, lifetime, fade_out, core, twin, halo,
               wake, wake_growth, disturb, land, bed, reach, reach_height, front_width, lane=0.0, lane_jitter=0.0,
               splash=0.0, near_wake=0.0):
    return {"id": name, "name": name, "type": "groundPulse", "owner": {"kind": "world"}, "enabled": True, "order": 0,
            "activation": "trigger", "trigger": {"source": "signal", "name": signal, "threshold": 0.0},
            "style": "", "timing": _timing(fadeIn=0.0, fadeOut=fade_out, lifetime=lifetime),
            "parameters": {
                "appearance": {"color": hexrgb(color), "edgeColor": _whiter(color, 0.6), "edgeIntensity": edge,
                               "intensity": intensity, "rainbow": False, "rainbowBrightness": 1.0,
                               "rainbowSaturation": 0.85, "rainbowScale": 0.025, "rainbowSpeed": 0.35, "width": 1.0},
                "propagation": {"kind": "path", "direction": "explicit", "beamRadius": 0.0,
                                "startOffset": START_OFFSET, "speed": round(rng / travel, 2), "range": rng,
                                "frontWidth": front_width, "trailLength": wake, "falloff": 1.6,
                                "verticalExtent": 40.0, "verticalGrowth": 0.0, "ringCount": 0.0,
                                "forwardWeight": 1.0, "targetWeight": 0.0, "velocityWeight": 0.0,
                                "explicitDirection": [0.0, 0.0, -1.0],
                                "path": {"node": WATER_NODE, "feature": "river", "ease": ease,
                                         "wakeGrowth": wake_growth, "coreWidth": core, "coreOffset": twin,
                                         "haloWidth": halo, "reach": reach, "reachHeight": reach_height,
                                         "land": land, "bed": bed, "embed": 0.625, "disturb": disturb,
                                         "nearFade": NEAR_FADE, "lane": lane, "laneJitter": lane_jitter,
                                         "splash": splash, "nearWake": near_wake}},
                "response": {"emissive": 0.0, "foliage": 1.0, "ground": 1.0, "surface": 1.0},
                "source": {"kind": "camera", "groundOffset": CAM_H_WATER},
                "sparkle": {"density": 0.9, "enabled": False, "fadeDistance": 85.0, "intensity": 1.8, "seed": 1,
                            "size": 0.3, "speed": 0.6}}}


# brief s12: kick and snare colour and travel; ripple strength (`disturb`)
# Pass 4 (owner): three lanes -- kick left of the centre, snare right, toms anywhere, each hit jittered -- a splash
# where each launches and a heavier wake near the camera; the toms a third colour. Pass 5, the lake: the lanes run from
# just in front of the camera straight out across the lake to the far shore (about 2 km), 150 m either side of its axis
# for the kick and snare, the toms anywhere across it; and (the owner: "tone down the amount of light that gets throw
# onto the mountains from the underwater snare / kick / tom lights") the light on the land (`land`) is a quarter of
# pass 4's, a restrained accent, while the light in the water and on the bed is unchanged. The cores and halos are half
# as wide again as the river's: the camera stands 110 m up and sees them across the lake at 0.3-2 km.
KICK = dict(intensity=4.0, edge=7.0, travel=2.0, rng=1950.0, ease=0.6, lifetime=2.0, fade_out=0.8, core=9.0,
            twin=0.0, halo=52.0, wake=260.0, wake_growth=0.35, disturb=0.045, land=0.09, bed=3.5, reach=700.0,
            reach_height=250.0, front_width=14.0, lane=-150.0, lane_jitter=50.0, splash=0.035, near_wake=0.8)
SNARE = dict(intensity=3.6, edge=7.0, travel=1.4, rng=1900.0, ease=0.6, lifetime=1.3, fade_out=0.6, core=6.0,
             twin=0.0, halo=40.0, wake=160.0, wake_growth=0.22, disturb=0.035, land=0.06, bed=2.8, reach=600.0,
             reach_height=200.0, front_width=9.0, lane=150.0, lane_jitter=50.0, splash=0.025, near_wake=0.6)
TOMS = dict(intensity=3.8, edge=6.0, travel=1.7, rng=1920.0, ease=0.6, lifetime=1.6, fade_out=0.7, core=7.0,
            twin=0.0, halo=45.0, wake=200.0, wake_growth=0.3, disturb=0.04, land=0.075, bed=3.0, reach=650.0,
            reach_height=220.0, front_width=12.0, lane=0.0, lane_jitter=220.0, splash=0.03, near_wake=0.6)
TOMS_COLOR = "#8a5cff"   # electric violet: the third colour, between the magenta kick and the lilac snare


def river_effects(pair=None):
    k, sn = BEAM_PAIRS[pair or BEAM_PAIR]
    return [river_beam("riverKick", "signal.kickBeamEv", k, **KICK),
            river_beam("riverSnare", "signal.snareBeamEv", sn, **SNARE),
            river_beam("riverToms", "signal.tomHitEv", TOMS_COLOR, **TOMS)]


BED_SHELF = "#1c1858"   # pass 5: the lakebed's own faint blue-violet where it is shallow
BED_SHELF_DEPTH = (-38.0, -3.0)   # metres: it fades to the body's black between these depths


def bed_program():
    """The lakebed: the river body's near black, plus (pass 5, "make the pond appear deeper") a faint blue-violet that
    fades with depth -- the shelves round the shores show through the water, the middle drops away into black, so the
    lake reads as a volume. The light in the water (ADR-1662's land term) reveals its planes as before."""
    c4 = lambda h: hexrgb(h) + [1.0]   # noqa: E731
    return {"name": "abBed", "ops": [
        {"kind": "input", "dst": 0, "input": "worldPosition"},
        {"kind": "swizzle", "dst": 0, "srcA": 0, "constant": [1.0, 1.0, 1.0, 1.0]},
        {"kind": "smoothstep", "dst": 0, "srcA": 0, "constant": [BED_SHELF_DEPTH[0], BED_SHELF_DEPTH[1], 0.0, 0.0]},
        {"kind": "power", "dst": 0, "srcA": 0, "value": 1.6},
        {"kind": "constant", "dst": 1, "constant": c4(BED_SHELF)},
        {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 1},
        {"kind": "constant", "dst": 1, "constant": c4("#010106")},
        {"kind": "add", "dst": 0, "srcA": 0, "srcB": 1}],
            "baseColor": -1, "metallic": -1, "roughness": -1, "emission": 0, "emissionIntensity": 1.0, "opacity": -1}


def fish_program():
    """The fish: near black, so a fish shows only when a pulse's light in the water passes over it."""
    return {"name": "abFish", "ops": [{"kind": "constant", "dst": 0, "constant": hexrgb("#010106") + [1.0]}],
            "baseColor": -1, "metallic": -1, "roughness": -1, "emission": 0, "emissionIntensity": 1.0, "opacity": -1}


def fish_node():
    """The fish (pass 4-5), in their own near-black program: lit only by the pulses' light in the water."""
    return {"name": "fish", "kind": "procedural", "procedural": {
        "source": {"kind": "mesh", "asset": "ab-fish.glb"}, "distribution": {"kind": "single"},
        "material": {"baseColor": [0.0, 0.0, 0.0], "emissiveColor": [0.0, 0.0, 0.0], "emissiveIntensity": 0.0,
                     "roughness": 1.0, "metallic": 0.0, "unlit": True, "program": "abFish"},
        "castsShadow": False}}


def bed_node():
    return {"name": "riverbed", "kind": "procedural", "procedural": {
        "source": {"kind": "mesh", "asset": "ab-bed-%s.glb" % BED_VARIANT}, "distribution": {"kind": "single"},
        "material": {"baseColor": [0.0, 0.0, 0.0], "emissiveColor": [0.0, 0.0, 0.0], "emissiveIntensity": 0.0,
                     "roughness": 1.0, "metallic": 0.0, "unlit": True, "program": "abBed"},
        "castsShadow": False}}


def music_routes(R):
    """The routes that name this module's program ops (music.py adds them to its own)."""
    out = []
    for prog in ROCKS:
        out += [R("signal.auroraLight", prog.path("auroraGlow"), 0.75),
                # a crash flares the high sky-facing facets, both banks (white-violet: the route adds to every channel)
                R("signal.crashGlow", prog.path("crashCrest"), 0.2)]
    L, Rt = ROCK_L, ROCK_R
    out += [
        # LEFT, the Moog (pass 5): a fresh subset on every note (random.moogPick, the stem's onsets); every note the
        # stem plays -- its onsets and the softer ones its onset strength shows (music.py moogStrike) -- opens the
        # gate, sized by the stem's level, and swells the lines; the sustained level holds it open as long as a note
        # rings and lets it close as the note decays (moogHold), and lights every ridgeline on the bank with it
        R("random.moogPick", L.path("moog", "value"), 1.0),
        R("signal.moogStrike", L.path("moogGate"), -0.26),
        R("signal.moogHold", L.path("moogGate"), -0.24),
        R("signal.moogStrike", L.path("moogLine"), 3.0),
        R("signal.moogHold", L.path("edgeGlow"), 1.6),
        # RIGHT, the bass (pass 4-5; the toms and tone block before): the same on the right bank, its planes first.
        # The tone block is unassigned; the toms keep their lake lane
        R("random.bassPick", Rt.path("bass", "value"), 1.0),
        R("signal.bassStrike", Rt.path("bassGate"), -0.2),
        R("signal.bassHold", Rt.path("bassGate"), -0.2),
        R("signal.bassStrike", Rt.path("bassLine"), 2.0),
        R("signal.bassHold", Rt.path("edgeGlow"), 1.3),
    ]
    return out


def stars_effect():
    return {"id": "stars", "name": "stars", "type": "stars", "owner": {"kind": "world"}, "enabled": True, "order": 0,
            "activation": "always", "style": "",
            "timing": {"delay": 0.0, "fadeIn": 0.0, "fadeOut": 0.0, "lifetime": 0.0, "repeatSeconds": 0.0,
                       "windowSeconds": 6.0, "windowStart": 0.0},
            "parameters": {"brightness": 1.4, "density": 0.03, "magnitudeSlope": 5.0, "colorSpread": 0.45,
                           "twinkle": 0.25, "twinkleRate": 1.2, "horizonFade": 0.45, "band": 1.4, "bandTilt": 1.0,
                           "daylight": 0.0}}


# ---- the river ---------------------------------------------------------------------------------------------------
WATER_NODE = "river"


def water():
    """A long exposure: no ripple or chop, so the surface is glass; no SSR march (its one jittered ray, spread by the
    view streak, drew a lattice of dark dashes over the water; measured), the sky mirror alone reads the rendered frame
    -- sky, aurora and the ranges beyond half its reach -- and the streak smears it along the view; reflections stretched along the view; fine lanes
    of light drawn out along the current (ADR-1643 flow streaks) in the palette's aurora colours; and the sky mirror
    (ADR-1650, E1), so the aurora itself is smeared into the river."""
    c = look_colors(REST_LOOK)
    return {
        "enabled": True,
        "shallowColor": hexrgb("#0a0a24"), "deepColor": hexrgb(WATER_DEEP), "shallow": 2.0, "clarity": 1.0,
        "edgeFade": 0.4, "maxOpacity": 0.98, "fresnel": 0.35, "reflection": 1.1,
        "reflectionTint": [0.85, 0.86, 1.0], "specular": 0.3, "roughness": 0.03,
        "ripple": 0.01, "rippleScale": 0.03, "rippleSpeed": 1.0, "chop": 0.0, "foam": 0.0, "refraction": 1.5,
        "sparkle": 0.0, "glow": 0.15, "glowColor": c["waterGlow"], "glowScale": 0.012, "glowCoverage": 0.45,
        "glowDepth": 0.2,
        "ssr": 0.0, "ssrDistance": 700.0, "skyMirror": 1.0, "ssrSteps": 16.0, "ssrGain": 1.0, "streak": 2.0,
        # pass 3: the bed is seen through the water (dimmed with depth, wobbled by the beam's ripples via the refraction)
        # pass 5 ("make the pond appear deeper"): twice pass 4's absorption, so the bed darkens with depth -- the shelf
        # shows near the shores and fades into a dark middle -- and the water reads as a volume
        "reflectionRays": 1.0, "transmission": 0.7, "absorption": [0.075, 0.06, 0.042], "undulation": 0.03,
        "undulationScale": 0.012, "roughnessVariation": 0.0, "tintPatches": 0.0, "glints": 0.0,
        "flowStreaks": 1.2, "streakLines": 600.0, "streakLength": 50.0, "streakDensity": 0.45, "streakSpeed": 1.8,
        "streakHighlight": 1.0, "streakColor1": c["auroraLow"], "streakColor2": c["auroraMid"],
        "streakColor3": c["waterGlow"],
    }


def river_feature():
    """Pass 5: the lake's axis, the line the light in the water runs along (each beam on its lane beside it) and the
    flow the streaks follow -- from behind the camera straight out to the far shore. Wide enough to cover the lake, so
    the streaks lie across all of it, drifting slowly away from the camera."""
    import world
    z0, z1 = 400.0, world.LAKE_Z - world.LAKE_B - 200.0
    pts = [[0.0, 0.0, round(z0 + (z1 - z0) * i / 32, 2)] for i in range(33)]
    return {"name": "river", "kind": "river", "path": pts, "width": 1200.0, "amplitude": 0.0, "falloff": 1.0,
            "flatten": 0.0, "roughness": 0.0, "water": True, "waterDepth": 0.0, "smoothing": 0}


def water_node():
    """A terrain node that is only water: a flat bed under a sea at y = 0 (the land's GLBs make the shores, so there is
    nothing to fit). Pass 5: a rectangle over the lake whose edges lie under the shore band. Coarse chunks; the bed is
    unlit and nearly black."""
    return {"name": WATER_NODE, "kind": "terrain", "position": [0.0, 0.0, -1000.0],
            "world": {"name": "aurora-lake", "seed": 1, "size": [2000.0, 3200.0], "baseHeight": -70.0,
                      "erosion": 0.0, "seaLevel": 0.0, "layers": [], "features": [river_feature()]},
            "terrain": {"chunkSize": 400.0, "resolution": 8, "lodLevels": 2, "lodDistance": 400.0,
                        "viewDistance": 6000.0, "shadowDistance": 0.0, "skirtDepth": 1.0, "groundMottle": False,
                        "groundGlow": 0.0, "water": water(),
                        # a lake: a slow drift away from the camera, so the streaks barely move
                        "flow": {"speedOverride": 0.8, "bankShear": 0.5, "meander": 0.12, "turbulence": 0.3}},
            "material": {"baseColor": [0.0, 0.0, 0.0], "emissiveColor": hexrgb("#020208"), "emissiveIntensity": 1.0,
                         "roughness": 1.0, "metallic": 0.0, "unlit": True}}


# ---- post ----------------------------------------------------------------------------------------------------------
def parameters():
    return {
        "post/bloom/enabled": True, "post/bloom/intensity": 0.3, "post/bloom/threshold": 1.1,
        "post/bloom/emissionWeight": 0.8, "post/tonemap/operator": 4, "post/tonemap/chroma-retention": 0.65,
        "post/output/vignette": 0.32, "post/output/grain": 0.01,
        "camera/exposure/mode": 0, "camera/exposure/compensation": 0.0,
        "camera/lens/useExplicitFov": False, "camera/lens/focalLength": 24.0,
        "temporal/echo/enabled": False, "temporal/mosh/enabled": False,
    }


# ---- the palette ---------------------------------------------------------------------------------------------------
def palette():
    states = [{"name": n, "colors": {k: hexrgb(v) for k, v in roles.items()}} for n, roles in LOOKS]
    w = "nodes/%s/water/" % WATER_NODE
    bindings = [
        {"role": "auroraLow", "target": "fx/aurora/lowColor"},
        {"role": "auroraMid", "target": "fx/aurora/midColor"},
        {"role": "auroraTop", "target": "fx/aurora/topColor"},
        *[{"role": "facetLit", "target": prog.path("facetLit")} for prog in ROCKS],
        {"role": "waterGlow", "target": w + "glowColor"},
        {"role": "auroraLow", "target": w + "streaks/color1"},
        {"role": "auroraMid", "target": w + "streaks/color2"},
        {"role": "waterGlow", "target": w + "streaks/color3"},
        *[{"role": "auroraLow", "target": prog.path("rim"), "gain": 0.1} for prog in ROCKS],
        *[{"role": "auroraLow", "target": prog.path("edgeColor"), "gain": 0.8} for prog in ROCKS],
    ]
    return {"states": states, "bindings": bindings, "position": float(REST_LOOK), "saturation": 1.0, "value": 1.0}
