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
ZENITH = "#000004"          # deep indigo, nearly black
HORIZON = "#1d1640"         # the dusk band: muted violet
SKY_GROUND = "#05040f"
SHADOW = "#030210"          # facets facing away from the sky
SKYLIT = "#0c0a2a"          # facets facing up: blue-violet
CREST = "#6a62ae"           # the pale lavender on high, sky-facing facets (a hint of snow light, never white)
HAZE = "#4a3f8e"            # distance haze: the horizon's colour, a little darker
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
    ("Rose Lift", {"auroraLow": "#ffc0ec", "auroraMid": "#ff58c0", "auroraTop": "#8a3ae8",
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


HAZE_NEAR, HAZE_FAR, HAZE_CURVE = 20.0, 500.0, 0.7    # decametres (op constants clamp to +-1000): 400 m to 9 km
CREST_LO, CREST_HI = 110.0, 320.0                       # metres: where the pale crest light comes in
AURORA_WRAP_POWER = 3.0


def rock() -> Prog:
    """Every landform, unlit (28 ops). Facet colour = shadow + sky-facing blue-violet + aurora-facing aurora light x
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
      .op("constant", 4, constant=[1.1, 1.1, 1.1, 1.0], label="auroraGlow")                  # the glow (routes)
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
      .op("input", 6, input="materialEmission")                                                 # per-node accents
      .op("add", 3, srcA=3, srcB=6)
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
                "groundColor": hexrgb(SKY_GROUND), "haze": 0.2, "sunIntensity": 0.0, "intensity": 1.0,
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
                "appearance": {"edgeBrightness": 3.6, "emission": 0.6, "filaments": 2.5, "horizonGlow": 0.3,
                               "intensity": 3.6, "lowColor": c["auroraLow"], "midColor": c["auroraMid"],
                               "topColor": c["auroraTop"], "opacity": 0.85, "sparkle": 0.2},
                # ADR-939's own mix response off: every musical input comes through the stem routes (music.py)
                "audio": {"bass": 0.0, "beat": 0.0, "glints": 0.0, "high": 0.0, "lowMid": 0.0, "mid": 0.0,
                          "sensitivity": 0.0, "spectrumShape": 0.0},
                "rainbow": {"brightness": 1.0, "enabled": False, "hueOffset": 0.0, "saturation": 0.85, "scale": 1.1,
                            "speed": 0.0},
                "shape": dict({"anchor": "camera", "anchorPosition": [0.0, 0.0, 0.0], "baseHeight": 0.0,
                               "complexity": 50.0, "curtainCount": 3.0, "curtainHeight": 900.0,
                               "driftSpeed": 0.05, "flowSpeed": 0.015, "layerSpacing": 0.35, "radius": 6000.0,
                               "turbulence": 0.3, "verticalSpeed": 0.03, "waveAmplitude": 0.5, "waveScale": 4.0},
                              **RIBBONS)},
            # the ground term reaches lit PBR and water only; the land is unlit (its program carries the aurora
            # light), so this only tints the river's body a trace
            "ground": {"color": c["auroraMid"], "falloff": 2.0, "intensity": 0.05, "mode": "subtle", "radius": 600.0},
            "flow": {"field": "", "influence": 0.0}}


def stars_effect():
    return {"id": "stars", "name": "stars", "type": "stars", "owner": {"kind": "world"}, "enabled": True, "order": 0,
            "activation": "always", "style": "",
            "timing": {"delay": 0.0, "fadeIn": 0.0, "fadeOut": 0.0, "lifetime": 0.0, "repeatSeconds": 0.0,
                       "windowSeconds": 6.0, "windowStart": 0.0},
            "parameters": {"brightness": 0.7, "density": 0.3, "magnitudeSlope": 6.5, "colorSpread": 0.35,
                           "twinkle": 0.2, "twinkleRate": 1.2, "horizonFade": 0.7, "band": 0.15, "bandTilt": 25.0,
                           "daylight": 0.0}}


# ---- the river ---------------------------------------------------------------------------------------------------
WATER_NODE = "river"


def water():
    c = look_colors(REST_LOOK)
    return {
        "enabled": True,
        "shallowColor": hexrgb("#0a0a24"), "deepColor": hexrgb(WATER_DEEP), "shallow": 2.0, "clarity": 1.0,
        "edgeFade": 0.4, "maxOpacity": 0.98, "fresnel": 0.35, "reflection": 1.15,
        "reflectionTint": [0.82, 0.84, 1.0], "specular": 0.5, "roughness": 0.06,
        "ripple": 0.18, "rippleScale": 0.05, "rippleSpeed": 0.25, "chop": 0.0, "foam": 0.0, "refraction": 0.0,
        "sparkle": 0.0, "glow": 0.25, "glowColor": c["waterGlow"], "glowScale": 0.012, "glowCoverage": 0.45,
        "glowDepth": 0.2,
        # cinematic water (ADR-1286): SSR 16 steps x 1 ray for the banks and the near ranges; long reflections
        # streaked along the view; broad tint patches
        "ssr": 1.0, "ssrDistance": 2000.0, "ssrSteps": 16.0, "ssrGain": 1.1, "streak": 1.2, "reflectionRays": 1.0,
        "transmission": 0.3, "absorption": [2.0, 1.6, 1.0], "undulation": 0.5, "undulationScale": 0.012,
        "roughnessVariation": 0.4, "tintPatches": 0.0, "glints": 0.0,
    }


def water_node():
    """A terrain node that is only water: a flat bed under a sea at y = 0 (the land's GLBs make the river's banks, so
    there is nothing to fit). Coarse chunks; the bed is unlit and nearly black."""
    return {"name": WATER_NODE, "kind": "terrain", "position": [0.0, 0.0, 0.0],
            "world": {"name": "aurora-river", "seed": 1, "size": [3400.0, 3400.0], "baseHeight": -6.0,
                      "erosion": 0.0, "seaLevel": 0.0, "layers": [], "features": []},
            "terrain": {"chunkSize": 200.0, "resolution": 8, "lodLevels": 2, "lodDistance": 400.0,
                        "viewDistance": 2600.0, "shadowDistance": 0.0, "skirtDepth": 1.0, "groundMottle": False,
                        "groundGlow": 0.0, "water": water()},
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
    ]
    return {"states": states, "bindings": bindings, "position": float(REST_LOOK), "saturation": 1.0, "value": 1.0}
