"""5. ORGANIC DIGITAL GARDEN: LANTERN REEF (04-brief-abstract-direction.md, direction 5; ABSTRACT-PLAN.md section 5).

A macro photograph of an alien meadow at twilight: a tall lantern flower in focus on the left third (a curving stalk, a
bell of twelve translucent coral petals round a glowing heart), curling tentacle vines, a cluster of polka-dot
mushrooms, out-of-focus leaves framing the foreground, giant jelly-trees softly blurred behind, spores drifting.

Grammar: organisms are a handful of primitives repeated radially, as Haeckel drew them (petals are elongated spheres on
a ring, stalks and tentacles tapering tubes, bells flattened spheres, gills rings, caps polka-dotted). Everything glows
from its rim (a fresnel program over the material's own emission: one program colours every organism), so it reads
as translucent jelly. Depth of field does the rest.
"""
import math

from .. import kit
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT, VERY_SLOW

ID = "organic-garden"
TITLE = "Organic Digital Garden"

DARK = "#031a1c"
TEAL = "#06302f"
VIOLET = "#2a1648"
CORAL = "#ff6f61"
PEACH = "#ffb38a"
LIME = "#c6ff5e"
MINT = "#bfffe8"
CYAN = "#5ee8ff"
ORCHID = "#d27bff"

FLOWER = (-0.75, 0.0, 0.0)     # the lantern flower's foot
BELL = (-0.62, 2.05, 0.08)     # its bell's centre

DESIGN = {
    "category": "organic",
    "thesis": "Lantern Reef: a macro view into a twilight meadow of translucent organisms that breathe, bloom and "
              "sway with the music; the notes open flowers, the kick blooms the lantern, held sound makes it grow.",
    "composition": {
        "background": "giant jelly-trees trailing tendrils, softly blurred, in a teal-to-violet dusk",
        "midground": "tentacle vines and a cluster of polka-dot mushrooms",
        "foreground": "dark out-of-focus leaves framing the bottom corners; spore bokeh",
        "focal": "the lantern flower on the left third: twelve coral petals round a peach heart",
        "secondary": ["the mushrooms", "the jelly-trees", "the spores"],
        "atmosphere": "a teal haze; depth of field",
        "post": "bloom on the rims, macro depth of field, grain",
        "camera": "a slow macro slide past the flower",
    },
    "palette": {"dominant": TEAL, "secondary": VIOLET, "accent": CORAL, "highlight": LIME,
                "background_value": "dark", "saturation": "glow from within: coral, peach, lime, mint"},
    "motion": {
        "very_slow": ["the camera's slide", "growth"],
        "medium": ["breathing", "the sway"],
        "fast": ["blooms on notes"],
        "extremely_fast": ["spore puffs on the snare"],
    },
    "vocabulary": [
        ["bass", "response.bass", "the garden breathes: bells inflate"],
        ["kick", "response.kick", "the lantern blooms open and closes again"],
        ["snare", "response.snare", "the mushrooms puff spores"],
        ["hat", "response.hat", "cilia shimmer; spores sparkle"],
        ["mids", "audio.mid", "the current: the tentacles sway"],
        ["sustained", "response.sustain", "the flower grows taller"],
        ["note", "notes.lastPitch", "a flower opens at the pitch's place"],
        ["silence", "(no input)", "the meadow sways gently and glows"],
    ],
    "tier": "medium: about 25 procedural nodes with one fresnel program, macro depth of field, spores",
}


def jelly_program():
    """Emission = the material's own emission x (dim core + bright rim): one program for every organism."""
    return {
        "name": "ogJelly",
        "ops": [
            {"kind": "fresnel", "dst": 0, "value": 2.2},
            {"kind": "remap", "dst": 0, "srcA": 0, "value": 1, "constant": [0.0, 1.0, 0.16, 1.25]},
            {"kind": "input", "dst": 1, "input": "materialEmission"},
            {"kind": "multiply", "dst": 2, "srcA": 1, "srcB": 0},
        ],
        "baseColor": -1, "metallic": -1, "roughness": -1, "emission": 2, "emissionIntensity": 1.0, "opacity": -1,
    }


def dots_program():
    """Polka dots on a jelly cap: the fresnel jelly plus bright dots (thresholded 3D Worley on object position)."""
    return {
        "name": "ogDots",
        "ops": [
            {"kind": "fresnel", "dst": 0, "value": 2.0},
            {"kind": "remap", "dst": 0, "srcA": 0, "value": 1, "constant": [0.0, 1.0, 0.2, 1.1]},
            {"kind": "input", "dst": 1, "input": "materialEmission"},
            {"kind": "multiply", "dst": 2, "srcA": 1, "srcB": 0},
            {"kind": "input", "dst": 3, "input": "localPosition"},
            {"kind": "voronoi", "dst": 4, "srcA": 3, "value": 7.0, "seed": 4},
            {"kind": "smoothstep", "dst": 4, "srcA": 4, "constant": [0.34, 0.26, 0.0, 0.0]},
            {"kind": "constant", "dst": 5, "constant": hexrgb("#f6ffd8", 2.4) + [1.0]},
            {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 4},
            {"kind": "add", "dst": 2, "srcA": 2, "srcB": 5},
        ],
        "baseColor": -1, "metallic": -1, "roughness": -1, "emission": 2, "emissionIntensity": 1.0, "opacity": -1,
    }


def jelly(hexc, intensity, base="#06100f", program="ogJelly"):
    return {"baseColor": hexrgb(base), "emissiveColor": hexrgb(hexc), "emissiveIntensity": float(intensity),
            "roughness": 0.45, "metallic": 0.0, "program": program}


def tube(points, radius, taper=0.5, sides=10, segments=40):
    return {"kind": "tube", "tubeRadius": float(radius), "tubeTaper": float(taper), "tubeSides": int(sides),
            "tubeSegments": int(segments), "tubeTwist": 0.0, "tubeCaps": True,
            "curve": {"kind": "catmullRom", "generator": "points", "samplesPerSegment": 10,
                      "points": [{"position": [float(v) for v in p]} for p in points]}}


def at(x, y, z, rot=(0, 0, 0), sc=(1, 1, 1)):
    return {"position": [float(x), float(y), float(z)], "rotation": [float(v) for v in rot],
            "scale": [float(v) for v in sc]}


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.6, "attack": 1.2, "release": 1.4}
    s.environment = {
        "intensity": 0.15, "background": hexrgb(DARK), "fogColor": hexrgb("#0a2f33"), "volumeDensity": 0.05,
        "volumeMaxDistance": 0.0, "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": hexrgb("#100a24"), "horizonColor": hexrgb("#0d3a3c"),
                "groundColor": hexrgb(DARK), "haze": 0.7, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    s.program(jelly_program())
    s.program(dots_program())

    # ---- the ground: a dark teal floor
    s.proc("ground", {"kind": "box", "size": [60.0, 0.2, 60.0], "subdivisions": 1},
           material={"baseColor": hexrgb("#082322"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                     "roughness": 0.9, "metallic": 0.0},
           transform=at(0.0, -0.1, -10.0))

    # ---- the lantern flower: a curving stalk, a bell of twelve drooping petals, a heart
    s.proc("stalk", tube([FLOWER, (-0.85, 0.7, 0.02), (-0.7, 1.45, 0.06), BELL], 0.045, taper=0.55),
           material=jelly(MINT, 0.6))
    s.proc("bell", {"kind": "sphere", "radius": 0.42, "segments": 16, "rings": 10},
           distribution={"kind": "radial", "count": 12, "radius": 0.2, "plane": "xz", "orientation": "outward",
                         "center": [0.0, 0.0, 0.0]},
           material=jelly(CORAL, 2.2),
           transform=at(BELL[0], BELL[1] + 0.06, BELL[2]),
           extra={"sourceTransform": {"position": [0.0, 0.0, 0.0], "rotation": [52.0, 0.0, 0.0],
                                      "scale": [0.34, 0.13, 1.0]}})
    s.proc("heart", {"kind": "sphere", "radius": 0.15, "segments": 16, "rings": 10},
           material=jelly(PEACH, 6.0), transform=at(BELL[0], BELL[1] - 0.06, BELL[2]))
    s.light("heartLight", "point", position=[BELL[0], BELL[1] - 0.15, BELL[2]], color=hexrgb(PEACH),
            intensity=6.0, range=6.0, castsShadow=False)

    # ---- tentacle vines: curling tubes round the flower, swaying in the current
    vines = [((-1.8, 0.0, -0.6), (-1.6, 0.6, -0.4), (-1.95, 1.2, -0.5), (-1.55, 1.7, -0.2)),
             ((0.35, 0.0, -0.8), (0.55, 0.5, -0.7), (0.3, 1.0, -0.9), (0.62, 1.35, -0.6)),
             ((-0.2, 0.0, 0.9), (-0.05, 0.35, 1.0), (-0.3, 0.7, 0.95), (-0.1, 0.95, 1.1)),
             ((1.4, 0.0, -1.6), (1.6, 0.7, -1.4), (1.3, 1.4, -1.7), (1.7, 2.0, -1.5)),
             ((-2.6, 0.0, 0.4), (-2.4, 0.4, 0.5), (-2.75, 0.85, 0.35), (-2.5, 1.15, 0.6))]
    for k, pts in enumerate(vines):
        s.proc("vine%d" % k, tube(pts, 0.05 - 0.004 * k, taper=0.1, sides=8, segments=36),
               material=jelly([LIME, MINT, LIME, CYAN, MINT][k], 1.3),
               deformers=[{"kind": "sine", "amount": 0.08, "frequency": 1.7, "speed": 0.9 + 0.13 * k, "phase": k,
                           "axis": [0.0, 1.0, 0.0], "displacementAxis": [1.0, 0.0, 0.3], "falloff": 1.4,
                           "center": [0.0, 0.0, 0.0], "space": "local"}])
        tip = pts[-1]
        s.proc("vineTip%d" % k, {"kind": "sphere", "radius": 0.05, "segments": 10, "rings": 6},
               material=jelly(PEACH, 5.0), transform=at(*tip))

    # ---- a cluster of polka-dot mushrooms on the right (stems and caps share a distribution and seed)
    shroom_dist = {"kind": "spiral", "count": 7, "radius": 0.25, "radiusGrowth": 0.55, "turns": 1.15,
                   "plane": "xz", "center": [0.0, 0.0, 0.0]}
    shroom_var = {"seed": 31, "randomScale": [0.0, 0.0, 0.0], "uniformScale": 0.45, "randomRotation": [0.12, 3.1, 0.12]}
    shroom_at = at(1.25, 0.0, 0.35)
    s.proc("stems", {"kind": "cylinder", "radius": 0.07, "height": 0.62, "radialSegments": 12, "caps": True},
           distribution=shroom_dist, variation=shroom_var, material=jelly(MINT, 0.9), transform=shroom_at,
           extra={"sourceTransform": {"position": [0.0, 0.31, 0.0]}})
    s.proc("caps", {"kind": "sphere", "radius": 0.3, "segments": 20, "rings": 12},
           distribution=shroom_dist, variation=shroom_var, material=jelly(LIME, 1.2, program="ogDots"),
           transform=shroom_at, extra={"sourceTransform": {"position": [0.0, 0.64, 0.0], "scale": [1.0, 0.55, 1.0]}})

    # ---- jelly-trees behind: big umbrella bells trailing tendrils (blurred by the depth of field)
    for k, (x, z, h, r, col) in enumerate(((-4.5, -7.5, 4.2, 1.7, ORCHID), (3.2, -10.0, 5.4, 2.2, CYAN),
                                           (-9.0, -14.0, 6.0, 2.6, MINT), (8.5, -16.0, 4.8, 2.0, ORCHID))):
        s.proc("jtStalk%d" % k, tube([(x, 0, z), (x + 0.3, h * 0.5, z), (x - 0.2, h, z)], 0.12, taper=0.6),
               material=jelly(col, 0.5))
        s.proc("jtBell%d" % k, {"kind": "sphere", "radius": r, "segments": 24, "rings": 12},
               material=jelly(col, 1.6), transform=at(x - 0.2, h, z, sc=(1.0, 0.5, 1.0)))
        s.proc("jtTendrils%d" % k, tube([(0, 0, 0), (0.05, -0.8, 0.0), (-0.05, -1.7, 0.02), (0.04, -2.6, 0.0)],
                                        0.035, taper=0.2, sides=6, segments=20),
               distribution={"kind": "radial", "count": 14, "radius": r * 0.82, "plane": "xz",
                             "orientation": "outward"},
               material=jelly(col, 1.1), transform=at(x - 0.2, h - 0.1, z),
               deformers=[{"kind": "sine", "amount": 0.12, "frequency": 1.2, "speed": 0.7 + 0.1 * k, "phase": 0.0,
                           "axis": [0.0, 1.0, 0.0], "displacementAxis": [1.0, 0.0, 0.5], "space": "world"}])

    # ---- out-of-focus leaves framing the foreground: dark blades with a faint rim
    for k, (x, y, z, ry, rz) in enumerate(((-1.6, 0.35, 2.7, 30.0, 25.0), (1.9, 0.25, 2.5, -25.0, -30.0),
                                           (0.9, 0.12, 3.2, 10.0, -10.0))):
        s.proc("leaf%d" % k, {"kind": "sphere", "radius": 0.9, "segments": 16, "rings": 8},
               material=jelly("#1d6b55", 0.5, base="#020807"), transform=at(x, y, z, rot=(70.0, ry, rz),
                                                                             sc=(0.38, 1.0, 0.06)))

    # ---- spores drifting through the meadow
    s.particles("spores", capacity=2500, seed=9, shape="box", position=[0.0, 1.5, -1.5], extent=[4.5, 1.5, 4.0],
                direction=[0, 1, 0], spawnRate=260.0, lifetimeMin=6.0, lifetimeMax=10.0, spread=1.0, speedMin=0.02,
                speedMax=0.08, gravity=[0, 0.02, 0], drag=0.3, turbulence=0.12, turbulenceScale=0.6,
                turbulenceSpeed=0.15, sizeStart=0.018, sizeEnd=0.012, sizeVariance=0.6, sizeSkew=2.0,
                colorStart=hexrgb(PEACH) + [0.9], colorEnd=hexrgb(MINT) + [0.0], emissive=3.0, blend="additive",
                pulseRate=0.6, pulseDepth=0.5)

    # ---- a cool rim light from behind and a little ambient
    s.light("rim", "directional", direction=[0.25, -0.35, 0.9], color=hexrgb("#7a8cff"), intensity=0.9,
            castsShadow=False)

    # ---- camera: a macro slide past the flower; focus on the bell
    cam = [0.55, 1.15, 4.1]
    tgt = [-0.55, 1.62, 0.0]
    focus = math.dist(cam, BELL)
    s.params_({"camera/lens/focalLength": 55.0, "post/bloom/intensity": 0.6, "post/bloom/threshold": 0.7,
               "post/bloom/emissionWeight": 1.0, "post/output/vignette": 0.5, "post/output/grain": 0.02,
               "post/tonemap/operator": 3, "post/dof/enabled": True, "post/dof/physical": True,
               "post/dof/maxRadius": 18.0, "camera/lens/aperture": 1.6, "camera/focus/mode": 0,
               "camera/lens/focusDistance": round(focus, 3)})
    s.camera = {"mode": 1, "position": cam, "target": tgt, "fov": 30.0, "orbitSpeed": 0.0}
    s.drift_camera(cam, tgt, period=48.0, amp=(0.35, 0.08, 0.15), tamp=(0.12, 0.05, 0.0))
    s.region("flower", centre=list(BELL), radius=0.7)
    s.region("mushrooms", centre=[1.25, 0.6, 0.35], radius=0.9)
    return s
