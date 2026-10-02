"""LANTERN LAKE (atmospheric, warm). Each note releases a paper lantern from the surface of a still lake at dusk; the
melody becomes a drifting constellation, and the bass a slow swell beneath it.

Composition (SCENE-CATALOG.md #14): the camera sits low over the water at the end of a jetty. The far shore's hills
lie just above the frame's middle; everything above the waterline is mirrored below it. The engine has no planar
reflection, so the reflection is built: every lantern has a twin (the same system, seed and burst, falling where its
original rises), the hills and the jetty have mirrored copies, and a shimmer column below the waterline wobbles the
mirror world -- the bass swells it and the kick ripples it. Lanterns are released across the lake at the pitch's place
(low notes left), drift right on the evening air, and rise into the dusk.
"""
import math

from .. import kit, signals
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT, SNAP, VERY_SLOW

ID = "lantern-lake"
TITLE = "Lantern Lake"

INDIGO = "#141a3a"
SLATE = "#20283f"
AMBER = "#ffb050"

CAM = (1.2, 1.1, 7.0)
RELEASE_Z = -36.0          # where lanterns are released (out on the lake)
ZENITH, HORIZON = "#0a0e26", "#c76a5a"
SHORE_Z = -640.0

DESIGN = {
    "category": "atmospheric",
    "thesis": "Each note releases a paper lantern from the surface of a still lake at dusk; the melody becomes a "
              "drifting constellation, and the bass a slow swell beneath it.",
    "composition": {
        "background": "a dusk sky over a far shore of low dark hills",
        "midground": "the lanterns rising and drifting, doubled in the mirror of the lake",
        "foreground": "the end of a wooden jetty, its posts in the water, lower left",
        "focal": "the newest lantern, low over the water at the pitch's place",
        "secondary": ["the drifting constellation of older lanterns", "fireflies at the jetty on the hats",
                      "the reflection's swell"],
        "atmosphere": "a faint evening haze over the water",
        "post": "bloom round the lanterns, halation, a warm-on-indigo grade, fine grain",
        "camera": "low over the water; a slow push along the jetty",
    },
    "palette": {"dominant": INDIGO, "secondary": SLATE, "accent": "#0a0a0c", "highlight": AMBER,
                "background_value": "dark (dusk)",
                "saturation": "the lanterns are the only warm, saturated colour"},
    "motion": {
        "very_slow": ["the camera's push", "the lanterns' rise", "the sky"],
        "medium": ["the swell of the reflection (bass)", "the lanterns' drift"],
        "fast": ["a lantern's release (note)"],
        "extremely_fast": ["the kick's ripple", "fireflies (hat)"],
    },
    "vocabulary": [
        ["note", "response.note", "a lantern is released at the pitch's place across the lake (low "
         "notes left, high notes right)"],
        ["velocity", "notes.lastVelocity", "how bright the new lanterns burn"],
        ["chord", "notes.polyphony", "a cluster of lanterns at once"],
        ["sustained", "response.sustain", "the lanterns glow brighter and the dusk lingers"],
        ["bass", "response.bass", "a slow swell beneath: the reflection heaves"],
        ["kick", "response.kick", "a ripple runs through the reflection"],
        ["hat", "response.hat", "fireflies blink at the jetty"],
        ["silence", "(no input)", "a still lake: the old lanterns drift up into the dark, doubled below"],
    ],
    "tier": "light: two particle twins, a few procedural copies, one DF shimmer column; no march",
}


def hills(mirror):
    """The far shore: low hills, mirrored below the waterline for the reflection. A procedural cannot take a negative
    scale, so the twin is the same box turned 180 degrees about X (with a LOCAL-space ridgeline, that flips its
    height and its depth and keeps the ridge's left-to-right shape: a true mirror for a silhouette). One box, scaled
    along x, so the ridge has no seams."""
    mat = {"baseColor": scale3(hexrgb("#0d1022"), 0.45 if mirror else 1.0), "emissiveColor": [0, 0, 0],
           "emissiveIntensity": 0.0, "roughness": 1.0, "metallic": 0.0, "doubleSided": True}
    return dict(name="hillsM" if mirror else "hills",
                source={"kind": "box", "size": [1000.0, 24.0, 50.0], "subdivisions": 64}, material=mat,
                # the ridgeline: noise that fades to nothing at the box's foot (falloff from the local bottom), so the
                # shore meets the waterline flat and the twin meets it from below
                deformers=[{"kind": "noise", "amount": 16.0, "scale": 0.018, "speed": 0.0, "seed": 9,
                            "axisMask": [0, 1, 0], "axis": [0, 1, 0], "center": [0.0, -12.0, 0.0], "falloff": 24.0},
                           {"kind": "noise", "amount": 4.0, "scale": 0.09, "speed": 0.0, "seed": 10,
                            "axisMask": [0, 1, 0], "axis": [0, 1, 0], "center": [0.0, -12.0, 0.0], "falloff": 24.0}],
                transform={"position": [0.0, -12.0 if mirror else 12.0, SHORE_Z],
                           "rotation": [180.0 if mirror else 0.0, 0.0, 0.0], "scale": [4.5, 1.0, 1.0]})


def jetty(mirror):
    """A low jetty from just in front of the camera out into the lake, lower left: planks on posts that stand from the
    waterline to just above the deck (so the twin's posts end at the waterline too)."""
    sy = -1.0 if mirror else 1.0
    wood = {"baseColor": scale3(hexrgb("#1a1410"), 0.5 if mirror else 1.0), "emissiveColor": [0, 0, 0],
            "emissiveIntensity": 0.0, "roughness": 0.85, "metallic": 0.0, "doubleSided": True}
    deck_y = 0.45
    x0, z0, z1 = -2.4, 5.0, -16.0
    tag = "M" if mirror else ""
    parts = [dict(name="planks" + tag, source={"kind": "box", "size": [1.8, 0.05, 0.22], "subdivisions": 1},
                  distribution={"kind": "linear", "count": 64, "start": [x0, deck_y * sy, z0],
                                "end": [x0, deck_y * sy, z1]},
                  variation={"seed": 5, "position": [0.03, 0.01, 0.0], "rotation": [0.0, 0.02, 0.01],
                             "scale": [0.04, 0.0, 0.0]},
                  material=wood)]
    for side, seed in ((-0.85, 6), (0.85, 7)):
        parts.append(dict(name=("postsL" if side < 0 else "postsR") + tag,
                          source={"kind": "cylinder", "radius": 0.07, "height": 0.6, "segments": 8},
                          distribution={"kind": "linear", "count": 8, "start": [x0 + side, 0.3 * sy, z0],
                                        "end": [x0 + side, 0.3 * sy, z1]},
                          variation={"seed": seed, "rotation": [0.03, 0.0, 0.03]}, material=wood))
    return parts


def mirror_program():
    """The lake's mirror of the sky, painted on an unlit plane below the reflected world: the sky's own gradient,
    flipped (the horizon colour at grazing, the zenith's when looking down), weighted by water's Fresnel (a mirror
    at grazing, dark underfoot), broken into long horizontal ripple bands."""
    return {
        "name": "llMirror",
        "ops": [
            {"kind": "input", "dst": 0, "input": "viewDirection"},
            {"kind": "swizzle", "dst": 0, "srcA": 0, "constant": [1.0, 1.0, 1.0, 1.0]},             # its y
            {"kind": "remap", "dst": 1, "srcA": 0, "value": 1, "constant": [0.0, 0.3, 0.0, 1.0]},    # 0 grazing
            {"kind": "ramp", "dst": 2, "srcA": 1, "constant": hexrgb(HORIZON, 0.95) + [1.0],
             "constant2": hexrgb("#4a3050", 0.6) + [1.0], "constant3": hexrgb(ZENITH, 0.5) + [1.0]},
            {"kind": "remap", "dst": 3, "srcA": 0, "value": 1, "constant": [0.0, 0.45, 1.0, 0.08]},  # Fresnel
            {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 3},
            {"kind": "input", "dst": 4, "input": "worldPosition"},
            {"kind": "constant", "dst": 5, "constant": [0.03, 0.0, 1.3, 0.0]},                     # long bands
            {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 5},
            {"kind": "noise", "dst": 4, "srcA": 4, "value": 1.0, "seed": 5},
            {"kind": "remap", "dst": 4, "srcA": 4, "value": 1, "constant": [0.3, 0.7, 0.82, 1.12]},
            {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 4},
            {"kind": "constant", "dst": 6, "constant": [0.0, 0.0, 0.0, 1.0]},
        ],
        "baseColor": 6, "metallic": -1, "roughness": -1, "emission": 2, "emissionIntensity": 1.0, "opacity": -1,
    }


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.45, "sustain": 0.6, "attack": 1.3, "release": 1.7}
    s.environment = {
        "intensity": 0.6, "background": hexrgb(INDIGO), "fogColor": hexrgb("#2a2c4a"),
        "volumeDensity": 0.00035, "volumeMaxDistance": 40.0, "volumeNoise": 0.0, "fogSky": 1.0,
        "fogSkyDistance": 900.0, "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": hexrgb("#0a0e26"), "horizonColor": hexrgb("#c76a5a"),
                "groundColor": hexrgb("#8a4a48"), "haze": 0.12, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    s.composition = {
        "focalPoints": [{"name": "release", "position": [0.0, 2.0, RELEASE_Z], "radius": 12.0, "weight": 1.0}],
        "layers": [
            {"name": "jetty", "start": 0.0, "end": 20.0, "contrast": 1.0, "saturation": 0.9},
            {"name": "lake", "start": 20.0, "end": 400.0, "contrast": 0.9, "saturation": 0.9},
            {"name": "shore", "start": 400.0, "end": 9000.0, "contrast": 0.7, "saturation": 0.7},
        ],
    }
    # the mirror: the sky's gradient on an unlit plane below everything the lake reflects
    s.program(mirror_program())
    s.proc("mirror", {"kind": "box", "size": [1000.0, 0.2, 1000.0], "subdivisions": 1},
           material={"baseColor": [0, 0, 0], "emissiveColor": [0, 0, 0], "emissiveIntensity": 1.0, "roughness": 1.0,
                     "metallic": 0.0, "unlit": True, "program": "llMirror"},
           transform={"position": [0.0, -40.0, -3400.0], "rotation": [0, 0, 0], "scale": [8.0, 1.0, 8.0]})
    for mirror in (False, True):
        h = hills(mirror)
        s.proc(h["name"], h["source"], material=h["material"], deformers=h["deformers"], transform=h["transform"])
    for mirror in (False, True):
        for p in jetty(mirror):
            s.proc(p["name"], p["source"], distribution=p["distribution"], material=p["material"],
                   variation=p.get("variation"))

    # ---- the lanterns and their twins (same seed, same bursts; the twin falls where the lantern rises)
    common = dict(capacity=2400, seed=19, shape="box", extent=[1.5, 0.0, 6.0], spawnRate=0.0, lifetimeMin=24.0,
                  lifetimeMax=34.0, spread=0.18, speedMin=0.9, speedMax=1.4, drag=0.04, turbulence=0.04,
                  turbulenceScale=0.15, sizeStart=0.45, sizeEnd=0.36, blend="additive")
    s.particles("lanterns", position=[0.0, 0.3, RELEASE_Z], direction=[0.12, 1.0, 0.0], gravity=[0.06, 0.02, 0.0],
                colorStart=hexrgb("#ffb050") + [1.0], colorEnd=hexrgb("#ff7a2a") + [0.0], emissive=2.6, **common)
    s.particles("twins", position=[0.0, -0.3, RELEASE_Z], direction=[0.12, -1.0, 0.0], gravity=[0.06, -0.02, 0.0],
                colorStart=hexrgb("#ff9a50") + [0.7], colorEnd=hexrgb("#c86a2a") + [0.0], emissive=1.8, **common)
    # fireflies at the jetty (hats)
    s.particles("fireflies", capacity=400, seed=23, shape="box", position=[-1.0, 1.4, 6.0], extent=[3.0, 0.8, 5.0],
                direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=0.4, lifetimeMax=1.0, spread=1.0, speedMin=0.05,
                speedMax=0.2, gravity=[0, 0, 0], drag=0.8, turbulence=0.3, turbulenceScale=0.8, sizeStart=0.02,
                sizeEnd=0.0, colorStart=hexrgb("#d8ff7a") + [1.0], colorEnd=hexrgb("#ffd25a") + [0.0], emissive=10.0,
                blend="additive")

    # ---- the water: a shimmer column under the waterline wobbles the mirror world (bass swells, kick ripples)
    s.effect("swell", "heatShimmer", ("world",), parameters={
        "strength": 0.08, "radius": 900.0, "height": 60.0, "scale": 0.9, "riseSpeed": 0.4, "chroma": 0.0,
        "churn": 0.4, "edgeSoftness": 0.2, "heightFalloff": 0.2, "thicknessRef": 60.0, "fadeDistance": 0.0,
        "offsetX": CAM[0], "offsetY": -60.0, "offsetZ": CAM[2] - 300.0})

    # ---- light: dusk sky light, and a warm glow round the release point (the lanterns light the water's air)
    s.light("dusk", "directional", direction=[0.2, -0.3, 0.93], color=hexrgb("#c08070"), intensity=0.1,
            castsShadow=False)
    s.light("glow", "point", position=[0.0, 1.5, RELEASE_Z], color=hexrgb(AMBER), intensity=0.0, range=30.0,
            radius=4.0, castsShadow=False, volumetric=0.6)

    # ---- camera: low over the water; a slow push along the jetty
    s.camera["fov"] = 45.0
    s.track("camera/position", [{"time": 0.0, "value": list(CAM), "interp": "smooth"},
                                {"time": 60.0, "value": [CAM[0], CAM[1] + 0.05, CAM[2] - 2.5], "interp": "smooth"},
                                {"time": 120.0, "value": list(CAM), "interp": "smooth"}], loop=120.0)
    s.track("camera/target", [{"time": 0.0, "value": [4.0, 8.0, -80.0], "interp": "smooth"},
                              {"time": 60.0, "value": [5.0, 8.2, -80.0], "interp": "smooth"},
                              {"time": 120.0, "value": [4.0, 8.0, -80.0], "interp": "smooth"}], loop=120.0)
    s.camera = {"mode": 1, "position": list(CAM), "target": [4.0, 8.0, -80.0], "fov": 45.0, "orbitSpeed": 0.0}

    # ---- the instrument ---------------------------------------------------------------------------------------------
    # notes: a lantern (and its twin) at the pitch's place across the lake; a chord releases a cluster
    s.map(M("lakeX", [("lastPitch", 1.0)], "mean", -0.3 / 0.4, 1.0 / 0.4),
          M("cluster", [("noteOn", 1.0), ("polyphony", 1.0)], "min"))
    for sysname in ("lanterns", "twins"):
        s.route(R("visual.lakeX", "particles/%s/position" % sysname, 36.0, comp=0, offset=-0.5, attackMs=0,
                  decayMs=0),
                R("noteOn", "particles/%s/burst" % sysname, 1.0, threshold="binary", thresholdLevel=0.01),
                R("visual.cluster", "particles/%s/burst" % sysname, 8.0, attackMs=0, decayMs=0),
                R("lastVelocity", "particles/%s/emissive" % sysname, 2.0 if sysname == "lanterns" else 1.3,
                  **MEDIUM),
                R("sustain", "particles/%s/emissive" % sysname, 1.6 if sysname == "lanterns" else 0.7, **SLOW))
    s.route(R("noteEnv", "lights/glow/intensity", 260.0, attackMs=0, decayMs=900),
            R("sustain", "lights/glow/intensity", 120.0, **SLOW))
    # bass: the swell; kick: a ripple through the reflection
    s.route(R("bass", "fx/swell/strength", 0.35, attackMs=50, decayMs=1800),
            R("kick", "fx/swell/strength", 0.5, attackMs=0, decayMs=500),
            R("kick", "fx/swell/scale", 0.6, attackMs=0, decayMs=500))
    # hat: fireflies
    s.route(R("hat", "particles/fireflies/burst", 8.0, attackMs=0, decayMs=40),
            R("hatRate", "particles/fireflies/spawnRate", 30.0, **MEDIUM),
            R("sustain", "post/bloom/intensity", 0.1, **SLOW),
            R("kick", "post/lens/chromaticAberration", 0.006, attackMs=0, decayMs=120))

    s.params_({
        "post/bloom/intensity": 0.35, "post/bloom/threshold": 0.9, "post/halation/enabled": True,
        "post/halation/intensity": 0.08, "post/halation/warmth": 0.8, "post/output/vignette": 0.4,
        "post/output/grain": 0.016, "post/grade/contrast": 1.05, "post/lens/chromaticAberration": 0.002,
        "post/tonemap/operator": 3, "camera/lens/focalLength": 28.0,
    })

    # ---- the evaluator's screen regions, projected through the camera at t = 0
    s.region_points("lanterns", [[-18.0, 0.0, RELEASE_Z], [18.0, 0.0, RELEASE_Z], [-18.0, 14.0, RELEASE_Z],
                                 [18.0, 14.0, RELEASE_Z]])
    s.region_points("reflection", [[-18.0, 0.0, RELEASE_Z], [18.0, 0.0, RELEASE_Z], [-18.0, -14.0, RELEASE_Z],
                                   [18.0, -14.0, RELEASE_Z]])
    s.region("jetty", box=[0.0, 0.62, 0.35, 1.0])
    return s
