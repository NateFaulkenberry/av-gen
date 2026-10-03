"""THE CORRUPTED CATHEDRAL (digital and glitch). A cathedral drawn in lines of light decays into data under the drums
and rebuilds itself on held chords.

Composition (SCENE-CATALOG.md #4): one-point perspective down the nave from the west door. Seven bays of piers and
pointed arcades recede to the rose window, which is the brightest thing in the frame, high on the axis. Two piers
frame the view close. Everything is drawn by the SDF line look (ADR-1047) in cold cyan; magenta appears only where
something breaks. Glitch is rationed to hits (Menkman: it must break something readable).
"""
from .. import kit, signals
from ..kit import (R, M, hexrgb, scale3, sd_union, sd_diff, sd_inter, sd_move, sd_rot, sd_box, sd_rbox, sd_cyl,
                   sd_torus, sd_repeat, sd_polar, sd_mirror, sd_voronoi, sd_warp, sd_twist, sd_plane, SLOW, MEDIUM,
                   FAST, HIT, SNAP)

ID = "corrupted-cathedral"
TITLE = "The Corrupted Cathedral"

BLACK = "#000000"
VIOLET = "#120a24"
CYAN = "#5ef2ff"
MAGENTA = "#ff2fd0"
WHITE = "#bff8ff"

DESIGN = {
    "category": "digital",
    "thesis": "A cathedral drawn in lines of light decays into data under the drums and rebuilds itself on held "
              "chords.",
    "composition": {
        "background": "the apse wall and its rose window far down the nave, the lines dimmed by distance and haze",
        "midground": "seven bays of piers, pointed arcades and transverse ribs, drawn in light",
        "foreground": "the first pair of piers framing the view, close and bright",
        "focal": "the rose window: the brightest thing in the frame, high on the axis",
        "secondary": ["the bay lights that flare by pitch", "floating data motes", "the floor's tile lines"],
        "atmosphere": "thin cold violet haze that swallows the far lines",
        "post": "data mosh, channel shift and lens fringing, rationed to hits; a scan sweep on phrases; bloom",
        "camera": "a slow walk up and back the axis (one-point perspective), 100 s",
    },
    "palette": {"dominant": BLACK, "secondary": VIOLET, "accent": MAGENTA, "highlight": WHITE,
                "background_value": "black",
                "saturation": "cyan lines everywhere at low value; the rose window and the corruption carry the "
                              "chroma"},
    "motion": {
        "very_slow": ["the walk", "the rose window turning"],
        "medium": ["the nave swaying (bass)", "reconstruction after a hit"],
        "fast": ["bay lights by pitch"],
        "extremely_fast": ["mosh blocks (snare)", "split and shock (kick)", "mote flicker (hat)"],
    },
    "vocabulary": [
        ["sustained", "response.sustain", "reconstruction: the lines sharpen and brighten, the corruption heals"],
        ["melodic", "response.note", "the bay at the pitch's place flares (low notes near the "
         "door, high notes near the apse)"],
        ["bass", "response.bass", "the structure sways (an SDF warp) and the lines thicken"],
        ["kick", "response.kick", "the image breaks for a moment: it splits along the nave, a shock runs from the rose"],
        ["snare", "response.snare", "frame corruption: mosh blocks and a magenta channel shift"],
        ["hat", "response.hat", "data motes flicker"],
        ["phrase", "notes.phrase", "a scan sweep crosses the frame and rebuilds it"],
        ["velocity", "notes.lastVelocity", "how hard a note flares its bay"],
        ["density", "notes.density", "fragmentation: more, smaller mosh blocks"],
        ["roughness", "sonic.roughness", "a rough sound leaves the frame corrupted: mosh blocks and sorted light"],
    ],
    "tier": "medium: one compiled SDF (the nave), haze march at 24 steps, mosh",
}

BAY = 6.0
N_BAYS = 7
# The nave's cost at 1080p (realtime tier, GPU ms; measured with tools/sonic_vfx/perf.py on 96bc0214): the one tree
# with a grooved SDF floor, a noise-warp sway and the rose inside it marched in 61 ms. The rose's twelve-fold polar
# tracery, evaluated at every step of every ray, was half of it; the floor's grazing grooves and the warp a quarter.
# All three switches on: 16 ms for both SDF objects together.
FLOOR_MESH = True       # the floor as a lit plane with a tile-line program instead of SDF grooves
SWAY_TWIST = True       # the bass's sway as a twist about the nave's axis instead of a noise warp
ROSE_SPLIT = True       # the rose window as its own small SDF object instead of part of the nave's tree
NAVE_MID_Z = -18.0       # bays at z = 0, -6, ..., -36
APSE_Z = -40.5
ROSE_Y = 12.0


def nave():
    """The whole cathedral as one compiled SDF tree (under the 96-node limit)."""
    # piers: a column with a capital, mirrored across the nave and repeated down it
    pier = sd_union(sd_move((0.0, 8.0, 0.0), sd_rbox((0.55, 8.0, 0.55), 0.18)),
                    sd_move((0.0, 16.2, 0.0), sd_box((0.85, 0.22, 0.85))),
                    sd_move((0.0, 0.25, 0.0), sd_box((0.85, 0.25, 0.85))))
    piers = sd_move((0.0, 0.0, NAVE_MID_Z - BAY / 2.0),
                    sd_repeat((0.0, 0.0, BAY), sd_mirror((1, 0, 0), sd_move((6.0, 0.0, 0.0), pier)), count=3),
                    name="piers")
    # arcade walls: the wall above and between piers, pierced by a pointed arch
    arch_lens = sd_inter(sd_move((0.0, 8.6, 1.35), sd_rot((0, 0, 90), sd_cyl(4.1, 2.0))),
                         sd_move((0.0, 8.6, -1.35), sd_rot((0, 0, 90), sd_cyl(4.1, 2.0))))
    arch_void = sd_union(arch_lens, sd_move((0.0, 4.3, 0.0), sd_box((1.2, 4.3, 2.75))))
    wall = sd_diff(sd_move((0.0, 9.6, 0.0), sd_box((0.3, 9.6, 3.0))), arch_void)
    clerestory = sd_inter(sd_move((0.0, 15.2, 0.45), sd_rot((0, 0, 90), sd_cyl(1.5, 2.0))),
                          sd_move((0.0, 15.2, -0.45), sd_rot((0, 0, 90), sd_cyl(1.5, 2.0))))
    wall = sd_diff(wall, sd_union(clerestory, sd_move((0.0, 13.6, 0.0), sd_box((1.2, 1.6, 0.8)))))
    arcades = sd_move((0.0, 0.0, NAVE_MID_Z),
                      sd_repeat((0.0, 0.0, BAY), sd_mirror((1, 0, 0), sd_move((6.0, 0.0, 0.0), wall)), count=3),
                      name="arcades")
    # transverse ribs: a half ring across the nave at every pier
    rib = sd_move((0.0, 16.2, 0.0), sd_inter(sd_rot((90, 0, 0), sd_torus(6.0, 0.22)),
                                             sd_move((0.0, 3.5, 0.0), sd_box((7.0, 3.5, 1.0)))))
    ribs = sd_move((0.0, 0.0, NAVE_MID_Z - BAY / 2.0), sd_repeat((0.0, 0.0, BAY), rib, count=3), name="ribs")
    # the floor: a slab with tile grooves (the line look draws the joints)
    grooves = sd_repeat((1.5, 0.0, 1.5), sd_union(sd_box((0.025, 0.05, 0.8)), sd_box((0.8, 0.05, 0.025))))
    floor = sd_diff(sd_move((0.0, -0.25, -17.0), sd_box((7.5, 0.25, 24.0))), sd_move((0.0, 0.0, 0.0), grooves),
                    name="floor")
    # the apse wall and the rose window's tracery
    end_wall = sd_diff(sd_move((0.0, 11.0, APSE_Z), sd_box((7.5, 11.0, 0.4))),
                       sd_move((0.0, ROSE_Y, APSE_Z), sd_rot((90, 0, 0), sd_cyl(5.0, 2.0))), name="apse")
    spokes = sd_polar(12, sd_move((2.6, 0.0, 0.0), sd_box((2.3, 0.07, 0.07))))
    petals = sd_polar(12, sd_move((3.55, 0.0, 0.0), sd_torus(0.95, 0.06)))
    rings = sd_union(sd_rot((90, 0, 0), sd_torus(4.95, 0.14)), sd_rot((90, 0, 0), sd_torus(1.25, 0.1)))
    tracery = sd_move((0.0, ROSE_Y, APSE_Z), sd_union(sd_rot((90, 0, 0), sd_union(spokes, petals)), rings),
                      name="rose")
    glass = sd_move((0.0, ROSE_Y, APSE_Z - 0.3), sd_rot((90, 0, 0), sd_cyl(5.0, 0.04)), m=1)
    # (no Voronoi fracture round the whole nave: evaluated at every march step it cost most of a 92 ms SDF pass at
    #  1080p; the kick breaks the image with post instruments instead)
    parts = [piers, arcades, ribs, end_wall] + ([] if FLOOR_MESH else [floor]) + ([] if ROSE_SPLIT else [tracery])
    body = sd_union(*parts) if ROSE_SPLIT else sd_union(sd_union(*parts), glass)
    return sway(body)


def sway(body):
    """The bass's sway round a tree: a twist about the nave's vertical axis (the vault swings, the floor stays; one
    rotation a step) or the original noise warp. The rose window's own object takes the same sway, so it stays in its
    wall."""
    if SWAY_TWIST:
        return sd_move((0.0, 0.0, NAVE_MID_Z), sd_twist(0.0, sd_move((0.0, 0.0, -NAVE_MID_Z), body), name="sway"))
    return sd_warp(0.0, 0.07, body, gain=(1.0, 0.25, 1.0), seed=3, name="sway")


def rose_window():
    """The rose window's tracery and glass alone, for its own SDF object: a march over a small rect instead of
    every ray of the nave evaluating twelve-fold polar tracery at every step."""
    spokes = sd_polar(12, sd_move((2.6, 0.0, 0.0), sd_box((2.3, 0.07, 0.07))))
    petals = sd_polar(12, sd_move((3.55, 0.0, 0.0), sd_torus(0.95, 0.06)))
    rings = sd_union(sd_rot((90, 0, 0), sd_torus(4.95, 0.14)), sd_rot((90, 0, 0), sd_torus(1.25, 0.1)))
    tracery = sd_move((0.0, ROSE_Y, APSE_Z), sd_union(sd_rot((90, 0, 0), sd_union(spokes, petals)), rings),
                      name="rose")
    glass = sd_move((0.0, ROSE_Y, APSE_Z - 0.3), sd_rot((90, 0, 0), sd_cyl(5.0, 0.04)), m=1)
    return sway(sd_union(tracery, glass))


def floor_program():
    """The nave's floor as a lit plane: dark stone, its tile joints drawn as thin cyan lines (cosines of x and z from
    the palette op, thresholded), fading with distance before they could alias."""
    def cos_lines(dst, src):
        return {"kind": "palette", "dst": dst, "srcA": src, "constant": [0.0, 0.0, 0.0, 0.0],
                "constant2": [1.0, 1.0, 1.0, 0.0], "constant3": [1.0 / 1.5, 1.0 / 1.5, 1.0 / 1.5, 0.0],
                "constant4": [0.0, 0.0, 0.0, 0.0]}
    return {
        "name": "ccFloor",
        "ops": [
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            cos_lines(1, 0),                                                                  # cos(2 pi x / 1.5)
            {"kind": "swizzle", "dst": 2, "srcA": 0, "constant": [2.0, 2.0, 2.0, 2.0]},
            cos_lines(2, 2),                                                                  # cos(2 pi z / 1.5)
            {"kind": "smoothstep", "dst": 1, "srcA": 1, "constant": [0.985, 0.999, 0.0, 0.0]},
            {"kind": "smoothstep", "dst": 2, "srcA": 2, "constant": [0.985, 0.999, 0.0, 0.0]},
            {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},
            {"kind": "remap", "dst": 1, "srcA": 1, "value": 1, "constant": [0.0, 1.0, 0.0, 1.0]},
            {"kind": "input", "dst": 3, "input": "cameraDistance"},
            {"kind": "remap", "dst": 3, "srcA": 3, "value": 1, "constant": [6.0, 40.0, 1.0, 0.15]},
            {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 3},
            {"kind": "constant", "dst": 4, "constant": hexrgb(CYAN, 1.6) + [0.0]},
            {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 1},
            {"kind": "constant", "dst": 5, "constant": hexrgb("#0b0d12") + [1.0]},
        ],
        "baseColor": 5, "metallic": -1, "roughness": -1, "emission": 4, "emissionIntensity": 1.0, "opacity": -1,
    }


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.environment = {
        "intensity": 0.0, "background": hexrgb("#000000"), "fogColor": scale3(hexrgb(VIOLET), 0.6),
        "volumeDensity": 0.0035, "volumeMaxDistance": 70.0, "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": [0.0, 0.0, 0.0], "horizonColor": scale3(hexrgb(VIOLET), 0.15),
                "groundColor": [0.0, 0.0, 0.0], "haze": 0.0, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    s.composition = {
        "focalPoints": [{"name": "rose", "position": [0.0, ROSE_Y, APSE_Z], "radius": 5.0, "weight": 1.0}],
        "layers": [
            {"name": "door", "start": 0.0, "end": 12.0, "contrast": 1.0, "saturation": 1.0},
            {"name": "nave", "start": 12.0, "end": 36.0, "contrast": 0.95, "saturation": 0.95},
            {"name": "apse", "start": 36.0, "end": 200.0, "contrast": 1.0, "saturation": 1.1},
        ],
    }
    surfaces = [
        {"color": scale3(hexrgb("#0b0d12"), 1.0), "edge": hexrgb(CYAN, 0.9)},                      # stone
        {"color": [0.0, 0.0, 0.0], "emission": hexrgb("#7a2cff", 1.4), "edge": hexrgb(WHITE, 1.0)},  # rose glass
    ]
    if FLOOR_MESH:
        s.program(floor_program())
        s.proc("floor", {"kind": "box", "size": [15.0, 0.5, 48.0], "subdivisions": 1},
               material={"baseColor": hexrgb("#0b0d12"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 1.0,
                         "roughness": 0.35, "metallic": 0.0, "program": "ccFloor"},
               transform={"position": [0.0, -0.25, -17.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    s.sdf("nave", nave(), (-9.0, 0.0 if FLOOR_MESH else -0.6, -42.0), (9.0, 23.5, 4.0), surfaces=surfaces,
          material={"baseColor": [1, 1, 1], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0,
                    "roughness": 0.7, "metallic": 0.0},
          look={"aoStrength": 0.3, "aoDistance": 0.8, "edgeIntensity": 2.6, "edgeWidth": 0.02,
                "edgeColor": hexrgb(CYAN), "edgePixels": 1.5, "edgeSoftness": 0.25, "edgeThreshold": 0.03},
          max_steps=100, epsilon=0.0015, step_scale=0.9, max_distance=80.0)
    if ROSE_SPLIT:
        s.sdf("rose", rose_window(), (-7.0, 5.5, -41.6), (7.0, 18.5, -39.4), surfaces=surfaces,
              material={"baseColor": [1, 1, 1], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0,
                        "roughness": 0.7, "metallic": 0.0},
              look={"aoStrength": 0.3, "aoDistance": 0.8, "edgeIntensity": 2.6, "edgeWidth": 0.02,
                    "edgeColor": hexrgb(CYAN), "edgePixels": 1.5, "edgeSoftness": 0.25, "edgeThreshold": 0.03},
              max_steps=64, epsilon=0.0015, step_scale=0.9, max_distance=80.0)

    # the bay lights: one per bay, high in the arcade, dark until a note lands there
    for k in range(N_BAYS):
        s.light("bay%d" % k, "point", position=[0.0, 7.5, -BAY * k], color=hexrgb("#9af4ff"), intensity=0.0,
                range=11.0, radius=1.0, castsShadow=False, volumetric=0.8)
    # the rose window lights the apse and the haze before it
    s.light("rose", "point", position=[0.0, ROSE_Y, APSE_Z + 1.5], color=hexrgb("#a070ff"), intensity=120.0,
            range=26.0, radius=3.0, castsShadow=False, volumetric=0.9)
    s.light("fill", "directional", direction=[0.1, -1.0, -0.4], color=hexrgb("#3a4a70"), intensity=0.03,
            castsShadow=False)

    # data motes: tiny square-ish points hanging in the nave, flickering on hats
    s.particles("motes", capacity=4000, seed=17, shape="box", position=[0.0, 7.0, -18.0], extent=[5.5, 6.5, 20.0],
                direction=[0, 1, 0], spawnRate=60.0, lifetimeMin=4.0, lifetimeMax=8.0, spread=1.0, speedMin=0.0,
                speedMax=0.03, gravity=[0, 0.02, 0], drag=0.5, turbulence=0.05, sizeStart=0.03, sizeEnd=0.03,
                colorStart=hexrgb(CYAN) + [0.6], colorEnd=hexrgb(CYAN) + [0.0], emissive=1.2, blend="additive",
                pulseRate=0.0, pulseDepth=0.9, pulseSync=0.0, pulseSharpness=6.0)
    s.particles("shards", capacity=3000, seed=19, shape="box", position=[0.0, 8.0, -14.0], extent=[5.0, 6.0, 14.0],
                direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=0.25, lifetimeMax=0.7, spread=1.0, speedMin=0.5,
                speedMax=2.5, gravity=[0, -2.0, 0], drag=1.2, sizeStart=0.035, sizeEnd=0.0,
                colorStart=hexrgb(MAGENTA) + [1.0], colorEnd=hexrgb(CYAN) + [0.0], emissive=6.0, blend="additive")

    # ---- camera: down the axis from the west door, walking slowly up and back
    s.camera["fov"] = 50.0
    keys_p, keys_t = [], []
    for i in range(9):
        u = i / 8.0
        import math
        z = 5.0 - 7.0 * (0.5 - 0.5 * math.cos(2 * math.pi * u))
        x = 0.55 * math.sin(2 * math.pi * u + 0.7)
        keys_p.append({"time": round(100.0 * u, 3), "value": [round(x, 3), 4.4, round(z, 3)], "interp": "smooth"})
        keys_t.append({"time": round(100.0 * u, 3), "value": [round(0.2 * math.sin(2 * math.pi * u), 3), 10.2,
                                                              APSE_Z], "interp": "smooth"})
    s.track("camera/position", keys_p, loop=100.0)
    s.track("camera/target", keys_t, loop=100.0)
    s.camera = {"mode": 1, "position": keys_p[0]["value"], "target": keys_t[0]["value"], "fov": 50.0,
                "orbitSpeed": 0.0}

    # ---- the instrument ---------------------------------------------------------------------------------------------
    # bays along the nave: low notes near the door, high notes near the apse
    centres = [0.32 + 0.045 * k for k in range(N_BAYS)]
    s.places("bay", "lastPitch", centres, 0.045, event="noteOn")
    s.map(M("heal", [("sustain", 1.0), ("held", 0.6)], "max"))
    s.map(M("frag", [("density", 1.0), ("rhythm", 0.5)], "max"))
    s.map(M("grit", [("roughness", 1.0), ("energy", 0.4), ("inharmonicity", 2.0, True)], "product", -0.3, 6.5))

    # sustained: reconstruction -- sharper, brighter lines; the corruption heals
    objs = ("nave", "rose") if ROSE_SPLIT else ("nave",)
    for obj in objs:
        s.route(R("visual.heal", "sdf/%s/look/edge/intensity" % obj, 2.4, **SLOW),
                R("visual.heal", "sdf/%s/look/edge/softness" % obj, -0.15, **SLOW))
    s.route(R("visual.heal", "lights/rose/intensity", 260.0, **SLOW),
            R("visual.heal", "sdf/%s/surface/1/emission" % objs[-1], 0.8, comp=2, **SLOW))
    # melodic: the note's bay flares
    for k in range(N_BAYS):
        s.route(R("visual.bayHit%d" % k, "lights/bay%d/intensity" % k, 900.0, attackMs=0, decayMs=650))
    # bass: the nave sways and the lines thicken
    # (the twist's amount is radians per metre of height: 0.004 swings the vault's crown ~5 degrees)
    for obj in objs:
        s.route(R("bass", "sdf/%s/node/sway/amount" % obj, 0.004 if SWAY_TWIST else 0.35, attackMs=50,
                  decayMs=900),
                R("bass", "sdf/%s/look/edge/pixels" % obj, 1.2, attackMs=50, decayMs=600))
    # kick: the image breaks for a moment -- it splits along the nave's axis and a shock runs out from the rose
    s.route(R("kickEnv", "post/split/amount", 14.0, attackMs=0, decayMs=0),
            R("kickEnv", "post/shock/amount", 40.0, attackMs=0, decayMs=0),
            R("kickEnv", "post/shock/radius", -1.3, op="replace", offset=-1.0, attackMs=0, decayMs=0),
            R("kick", "post/lens/chromaticAberration", 0.05, attackMs=0, decayMs=140))
    # snare: frame corruption -- mosh blocks and a channel shift; denser playing breaks it into smaller blocks
    s.route(R("snare", "temporal/mosh/amount", 0.42, attackMs=0, decayMs=240),
            R("snare", "temporal/mosh/shift", 6.0, attackMs=0, decayMs=200),
            R("snare", "particles/shards/burst", 60.0, attackMs=0, decayMs=40),
            R("visual.frag", "temporal/mosh/block", -18.0, **MEDIUM),
            R("visual.frag", "temporal/mosh/amount", 0.06, **MEDIUM),
            # ...and the frame tears: row bands torn sideways, a few blocks swapped (ADR-1065)
            R("snareEnv", "post/glitch/tear", 0.5, attackMs=0, decayMs=0),
            R("snareEnv", "post/glitch/amount", 0.14, attackMs=0, decayMs=0))
    # hat: the motes flicker
    s.route(R("hat", "particles/motes/emissive", 7.0, attackMs=0, decayMs=70))
    # phrase: a scan sweeps the frame (progress 0 -> 1 as the envelope falls), in the accent colour
    s.route(R("phrase", "post/sweep/progress", 1.0, op="replace", envelope="linearfall", envelopeFallPerSecond=0.9,
              remapEnabled=True, remapInMin=0.0, remapInMax=1.0, remapOutMin=1.0, remapOutMax=0.0),
            R("phrase", "post/sweep/intensity", 0.6, attackMs=0, decayMs=1100))
    # roughness: permanent decay -- and the glass's light drips down the frame as sorted pixels; healing stops it
    s.route(R("visual.grit", "temporal/mosh/amount", 0.12, **FAST),
            R("visual.grit", "post/sort/amount", 0.75, **MEDIUM),
            R("visual.heal", "post/sort/amount", -0.5, **SLOW))
    s.route(R("visual.heal", "post/bloom/intensity", 0.1, **SLOW))

    rose_uv = [round(v, 3) for v in s.project([0.0, ROSE_Y, APSE_Z])[:2]]
    s.params_({
        "scene/volumeSteps": 16,          # (24 cost 15.7 ms of the frame; the thin haze holds at 16)
        "post/shock/amount": 0.0, "post/shock/radius": 1.3, "post/shock/width": 0.09, "post/shock/chroma": 0.6,
        "post/shock/centerX": rose_uv[0], "post/shock/centerY": rose_uv[1],
        "temporal/mosh/enabled": True, "temporal/mosh/amount": 0.0, "temporal/mosh/shift": 0.0,
        "temporal/mosh/block": 36.0, "temporal/mosh/smear": 26.0, "temporal/mosh/frames": 10.0,
        "temporal/mosh/rate": 14.0,
        "post/sweep/angle": 0.35, "post/sweep/width": 0.12, "post/sweep/span": 0.4, "post/sweep/hue": 0.85,
        "post/sweep/wash": 0.25, "post/sweep/trail": 0.4, "post/sweep/intensity": 0.0, "post/sweep/progress": 1.0,
        "post/bloom/intensity": 0.5, "post/bloom/threshold": 0.8, "post/bloom/emissionWeight": 0.9,
        "post/lens/chromaticAberration": 0.02, "post/output/vignette": 0.45, "post/output/grain": 0.02,
        "post/grade/contrast": 1.12, "camera/lens/focalLength": 24.0, "camera/exposure/compensation": 0.0,
        "post/split/amount": 0.0, "post/split/angle": 90.0, "post/split/spectral": 0.6,
        "post/glitch/amount": 0.0, "post/glitch/tear": 0.0, "post/glitch/tearShift": 70.0,
        "post/glitch/block": 40.0, "post/glitch/rate": 14.0, "post/glitch/swap": 0.5, "post/glitch/drift": 30.0,
        "post/sort/amount": 0.0, "post/sort/threshold": 0.9, "post/sort/length": 140.0, "post/sort/angle": 90.0,
    })
    # ---- the evaluator's screen regions, projected through the camera at t = 0, and the performer's baseline
    s.region("rose", centre=[0.0, ROSE_Y, APSE_Z], radius=4.5)
    s.region_points("nave", [[-6.0, 15.0, -BAY], [6.0, 15.0, -BAY], [-6.0, 15.0, APSE_Z + 6.0],
                             [6.0, 15.0, APSE_Z + 6.0], [-6.0, 0.0, -BAY], [6.0, 0.0, -BAY]])
    s.region("floor", box=[0.1, 0.72, 0.9, 1.0])
    s.response = {"sensitivity": 0.5, "transient": 0.6, "sustain": 0.5, "attack": 0.8, "release": 0.9}
    return s
