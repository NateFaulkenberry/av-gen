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


HAZE_NEAR, HAZE_FAR, HAZE_CURVE = 30.0, 950.0, 0.55    # decametres (op constants clamp to +-1000): 400 m to 9 km
CREST_LO, CREST_HI = 110.0, 320.0                       # metres: where the pale crest light comes in
AURORA_WRAP_POWER = 4.0
EDGE_GLOW = 0.5   # the edges' resting glow (Gate B); the music adds to it (music.py)


def rock() -> Prog:
    """Every landform, unlit (45 ops; the material-op ceiling is 48). Facet colour = shadow + sky-facing blue-violet + aurora-facing aurora light x
    glow; high sky-facing facets pale to lavender; then the distance haze."""
    p = Prog("abRock")
    c4 = lambda h: hexrgb(h) + [1.0]   # noqa: E731
    lit = look_colors(REST_LOOK)["facetLit"] + [1.0]
    (p.op("input", 0, input="worldPosition")
      .op("input", 1, input="normal")
      .op("input", 2, input="cameraDistance")
      .op("constant", 3, constant=[0.1, 0.1, 0.1, 0.1])
      .op("multiply", 2, srcA=2, srcB=3)                                                        # decametres
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
      .op("multiply", 5, srcA=5, srcB=4)
      .op("constant", 6, constant=c4(CREST), label="crest")
      .op("mixBy", 3, srcA=3, srcB=6, srcC=5)
      .op("fresnel", 6, value=6.0)                                                              # glassy rim
      .op("constant", 7, constant=c4(RIM) , label="rim")                                        # palette
      .op("multiply", 6, srcA=6, srcB=7)
      .op("add", 3, srcA=3, srcB=6)
      # Gate B, the ridge edges: TEXCOORD_0 = (metres to this facet's chosen crease, that crease's importance), baked by
      # world.edge_uvs; a ~2 px anti-aliased line (metres over the pixel footprint), faded out by 4.5 km
      .op("input", 0, input="uv")
      .op("input", 1, input="footprint")
      .op("power", 1, srcA=1, value=-1.0)
      .op("multiply", 5, srcA=0, srcB=1)
      .op("smoothstep", 5, srcA=5, constant=[1.6, 0.0, 0.0, 0.0])
      .op("swizzle", 5, srcA=5, constant=[0.0, 0.0, 0.0, 0.0])
      .op("swizzle", 6, srcA=0, constant=[1.0, 1.0, 1.0, 1.0])
      .op("multiply", 5, srcA=5, srcB=6)
      .op("smoothstep", 6, srcA=2, constant=[450.0, 40.0, 0.0, 0.0])
      .op("multiply", 5, srcA=5, srcB=6)
      .op("constant", 6, constant=lit, label="edgeColor")                                       # palette
      .op("multiply", 5, srcA=5, srcB=6)
      .op("constant", 6, constant=[EDGE_GLOW, EDGE_GLOW, EDGE_GLOW, 1.0], label="edgeGlow")   # routes; 0 = off
      .op("multiply", 5, srcA=5, srcB=6)
      .op("add", 3, srcA=3, srcB=5)
      .op("constant", 6, constant=c4(HAZE), label="haze")
      .op("smoothstep", 7, srcA=2, constant=[HAZE_NEAR, HAZE_FAR, 0.0, 0.0])
      .op("power", 7, srcA=7, value=HAZE_CURVE)
      .op("mixBy", 3, srcA=3, srcB=6, srcC=7))
    return p


ROCK = rock()


def rock_program():
    return {"name": ROCK.name, "ops": ROCK.ops, "baseColor": -1, "metallic": -1, "roughness": -1,
            "emission": 3, "emissionIntensity": 1.0, "opacity": -1}


def rock_material():
    return {"baseColor": [0.0, 0.0, 0.0], "emissiveColor": [0.0, 0.0, 0.0], "emissiveIntensity": 0.0,
            "roughness": 1.0, "metallic": 0.0, "unlit": True, "program": ROCK.name}


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
RIBBONS = {"ribbons": True, "ribbonAltitude": 600.0, "ribbonScale": 4500.0, "ribbonWidth": 0.045, "ribbonSwirl": 1.9,
           "ribbonEdge": 0.3, "ribbonSteps": 48.0, "ribbonDistance": 24000.0, "ribbonCoverage": 1.0}


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


def river_pulse(slot, signal, speed=750.0, intensity=0.8, name=None):
    """Gate D: a front of light that leaves the camera and runs down the river (research A s5.3): a directional
    groundPulse from the camera at the water's height, a few metres tall, so it lights the water and the first metres
    of the shore slope wherever the river goes. Triggered by its slot of the ring of two (music.py)."""
    c = look_colors(REST_LOOK)
    return {"id": name or ("riverPulse" + slot), "name": name or ("riverPulse" + slot), "type": "groundPulse",
            "owner": {"kind": "world"}, "enabled": True, "order": 0, "activation": "trigger",
            "trigger": {"source": "signal", "name": signal, "threshold": 0.5}, "style": "",
            "timing": _timing(fadeOut=0.6, lifetime=4.0),
            "parameters": {
                "appearance": {"color": c["waterGlow"], "edgeColor": c["auroraLow"], "edgeIntensity": 1.6,
                               "intensity": intensity, "rainbow": False, "rainbowBrightness": 1.0,
                               "rainbowSaturation": 0.85, "rainbowScale": 0.025, "rainbowSpeed": 0.35, "width": 1.0},
                "propagation": {"kind": "directional", "direction": "cameraVelocity", "beamRadius": 350.0,
                                "startOffset": 120.0, "speed": speed, "range": 3000.0, "frontWidth": 90.0,
                                "trailLength": 500.0, "falloff": 1.6, "verticalExtent": 5.0, "verticalGrowth": 0.004,
                                "ringCount": 0.0, "forwardWeight": 1.0, "targetWeight": 0.0, "velocityWeight": 1.0,
                                "explicitDirection": [0.0, 0.0, -1.0]},
                "response": {"emissive": 1.0, "foliage": 1.0, "ground": 1.0, "surface": 1.0},
                "source": {"kind": "camera", "groundOffset": CAM_H_WATER},
                "sparkle": {"density": 0.9, "enabled": False, "fadeDistance": 85.0, "intensity": 1.8, "seed": 1,
                            "size": 0.3, "speed": 0.6}}}


CAM_H_WATER = 65.0   # the camera's height above the water (build.CAM_H): the pulse's origin sits on the water


def comet_effect(start_az=30.0, end_az=-35.0):
    """Gate F: one comet, triggered by signal.comet (about five a song, never two alive), crossing above the aurora's
    band in the palette's lavender, violet and magenta (research A s7.3)."""
    return {"id": "comet", "name": "comet", "type": "comet", "owner": {"kind": "world"}, "enabled": True, "order": 0,
            "activation": "trigger", "trigger": {"source": "signal", "name": "signal.comet", "threshold": 0.5},
            "style": "", "timing": _timing(fadeIn=0.4, fadeOut=1.5, lifetime=9.0),
            "ground": {"color": hexrgb("#b38cff"), "falloff": 2.0, "intensity": 0.4, "mode": "subtle",
                       "radius": 600.0},
            "parameters": {
                "appearance": {"coreColor": hexrgb("#f4e8ff"), "coreIntensity": 16.0, "flowSpeed": 0.35,
                               "haloColor": hexrgb("#b38cff"), "haloIntensity": 2.0, "haloSize": 120.0,
                               "headSize": 24.0, "tailColor": hexrgb("#d36cff"), "tailFalloff": 1.8,
                               "tailIntensity": 4.5, "tailLength": 2600.0, "tailWidth": 60.0, "wispAmount": 50.0,
                               "wispScale": 0.002},
                "path": {"acceleration": 0.0, "anchor": "camera", "anchorPosition": [0.0, 0.0, 0.0],
                         "arcLift": 200.0, "curvature": 0.0, "distance": 5000.0, "endAzimuth": end_az,
                         "endElevation": 13.0, "speedScale": 1.0, "startAzimuth": start_az, "startElevation": 30.0,
                         "travelSeconds": 6.5},
                "rainbow": {"brightness": 1.0, "enabled": False, "hueOffset": 0.0, "saturation": 0.85, "scale": 1.1,
                            "speed": 0.2},
                "sparkle": {"density": 0.01, "enabled": True, "fadeDistance": 85.0, "intensity": 5.0, "seed": 1,
                            "size": 0.13, "speed": 1.3}}}


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
        "ripple": 0.01, "rippleScale": 0.03, "rippleSpeed": 1.0, "chop": 0.0, "foam": 0.0, "refraction": 0.0,
        "sparkle": 0.0, "glow": 0.15, "glowColor": c["waterGlow"], "glowScale": 0.012, "glowCoverage": 0.45,
        "glowDepth": 0.2,
        "ssr": 0.0, "ssrDistance": 700.0, "skyMirror": 1.0, "ssrSteps": 16.0, "ssrGain": 1.0, "streak": 2.0,
        "reflectionRays": 1.0, "transmission": 0.3, "absorption": [2.0, 1.6, 1.0], "undulation": 0.03,
        "undulationScale": 0.012, "roughnessVariation": 0.0, "tintPatches": 0.0, "glints": 0.0,
        "flowStreaks": 1.2, "streakLines": 600.0, "streakLength": 50.0, "streakDensity": 0.45, "streakSpeed": 1.8,
        "streakHighlight": 1.0, "streakColor1": c["auroraLow"], "streakColor2": c["auroraMid"],
        "streakColor3": c["waterGlow"],
    }


def river_feature():
    """The river's course for the water's flow (the streaks run along it): the centreline, with no cut (the land is
    the GLB banks) and its surface at the sea's level."""
    import world
    pts = [world.centre(world.TAU * i / 255) for i in range(256)]   # 256 points at most
    return {"name": "river", "kind": "river", "path": [[round(p[0], 2), 0.0, round(p[2], 2)] for p in pts],
            "width": 200.0, "amplitude": 0.0, "falloff": 1.0, "flatten": 0.0, "roughness": 0.0, "water": True,
            "waterDepth": 0.0, "smoothing": 0}


def water_node():
    """A terrain node that is only water: a flat bed under a sea at y = 0 (the land's GLBs make the river's banks, so
    there is nothing to fit). Coarse chunks; the bed is unlit and nearly black."""
    return {"name": WATER_NODE, "kind": "terrain", "position": [0.0, 0.0, 0.0],
            "world": {"name": "aurora-river", "seed": 1, "size": [8400.0, 8400.0], "baseHeight": -6.0,
                      "erosion": 0.0, "seaLevel": 0.0, "layers": [], "features": [river_feature()]},
            "terrain": {"chunkSize": 400.0, "resolution": 8, "lodLevels": 2, "lodDistance": 400.0,
                        "viewDistance": 6000.0, "shadowDistance": 0.0, "skirtDepth": 1.0, "groundMottle": False,
                        "groundGlow": 0.0, "water": water(),
                        # the river's real speed (it was about 0.02 m/s, so nothing in the water moved: 11 s4)
                        "flow": {"speedOverride": 3.0, "bankShear": 0.5, "meander": 0.12, "turbulence": 0.3}},
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
        {"role": "facetLit", "target": ROCK.path("facetLit")},
        {"role": "waterGlow", "target": w + "glowColor"},
        {"role": "auroraLow", "target": w + "streaks/color1"},
        {"role": "auroraMid", "target": w + "streaks/color2"},
        {"role": "waterGlow", "target": w + "streaks/color3"},
        {"role": "auroraLow", "target": ROCK.path("rim"), "gain": 0.1},
        {"role": "auroraLow", "target": ROCK.path("edgeColor"), "gain": 0.8},
    ]
    return {"states": states, "bindings": bindings, "position": float(REST_LOOK), "saturation": 1.0, "value": 1.0}
