"""STORM CELL (atmospheric, violent). A supercell walks across the night prairie: the bass winds its funnel, the snare
strikes, and the melody crawls as light inside the cloud.

Composition (SCENE-CATALOG.md #7): the camera stands in a wheat field at night, low. A tornado stands on the right
third a kilometre off, a dark column against the storm's clear slot (the pale green band of sky under the cloud that
real tornado photographs are silhouetted on), under a wall cloud that fills the top of the frame. A line of power
poles runs from the left foreground toward the funnel's foot; a farm's sodium light is the one warm point, on the left
third. The wheat's ears are silhouettes along the bottom of the frame against the band.

The light is the instrument: between strikes the storm is a silhouette; on a snare the whole prairie is lit for a
flicker; notes are lightning crawling inside the cloud at their place, left (low) to right (high).
"""
import math

from .. import kit, signals
from ..kit import R, M, hexrgb, scale3, lathe, SLOW, MEDIUM, FAST, HIT, SNAP, VERY_SLOW

ID = "storm-cell"
TITLE = "Storm Cell"

STORM = "#2c372f"
BRUISE = "#3b3046"
SODIUM = "#ffa24a"
BOLT = "#dfe9ff"
SLOT = "#93a27a"         # the clear slot under the cloud

CAM = (0.0, 1.65, 8.0)


def bearing(yaw_deg, dist, y=0.0):
    """A world point at bearing `yaw` (0 = straight ahead, -Z; positive right) and `dist` metres from the camera."""
    a = math.radians(yaw_deg)
    return [round(CAM[0] + dist * math.sin(a), 3), y, round(CAM[2] - dist * math.cos(a), 3)]


TORNADO = bearing(16.0, 1150.0)       # the funnel's foot, on the right third
FARM = bearing(-19.5, 430.0)          # the sodium light, on the left third
POLE_START = bearing(-36.0, 46.0)     # the nearest power pole
POLE_END = bearing(9.0, 760.0)        # the line runs toward the funnel's foot
CLOUD_Y = 780.0                       # the cloud base

DESIGN = {
    "category": "atmospheric",
    "thesis": "A supercell walks across the night prairie: the bass winds its funnel, the snare strikes, and the "
              "melody crawls as light inside the cloud.",
    "composition": {
        "background": "the wall cloud filling the top of the frame; the storm's clear slot, a pale green band, under it",
        "midground": "the tornado on the right third, a dark column on the band, its debris skirt; power poles "
                     "receding toward its foot",
        "foreground": "wheat ears in silhouette along the bottom of the frame, bending in the gusts",
        "focal": "the funnel where it meets the ground",
        "secondary": ["a farm's sodium light on the left third", "strikes", "light crawling in the cloud", "rain"],
        "atmosphere": "rain streaks and haze; the sodium light's halo in the rain",
        "post": "grain, a desaturated green-grey grade, exposure kicks on strikes",
        "camera": "low in the field, nearly still; a thunder nudge",
    },
    "palette": {"dominant": STORM, "secondary": BRUISE, "accent": SODIUM, "highlight": BOLT,
                "background_value": "dark, over a pale band",
                "saturation": "desaturated; the sodium light is the only warm colour"},
    "motion": {
        "very_slow": ["the funnel's drift", "the cloud's turning"],
        "medium": ["the wheat", "the rain", "the funnel's rotation (bass)"],
        "fast": ["light crawling in the cloud (notes)"],
        "extremely_fast": ["strikes (snare)", "thunder (kick)"],
    },
    "vocabulary": [
        ["bass", "response.bass", "the funnel winds: rotation, width and the debris skirt; the wind in the wheat"],
        ["kick", "response.kick", "thunder: the cloud flashes from within, a gust bends the field, the camera nudges"],
        ["snare", "response.snare", "a lightning strike under the cloud that lights the whole prairie and the rain"],
        ["hat", "response.hat", "rain: its density and streak length"],
        ["melodic", "response.note", "light crawls inside the cloud base at the pitch's place, low "
         "notes left, high notes right"],
        ["velocity", "notes.lastVelocity", "how bright a note's light and a strike are"],
        ["sustained", "response.sustain", "the cloud glows from within; the storm feeds"],
        ["density", "notes.density", "the storm's chaos: the funnel wobbles and the rain churns"],
        ["silence", "(no input)", "a dark, still storm on its pale band: the funnel turns slowly, the farm light burns"],
    ],
    "tier": "heavy: the haze march (40 steps live) and one tornado medium; a few thousand wheat instances",
}

# Notes light the cloud base at seven places, low notes left: (bearing degrees, pitch centre on notes.lastPitch)
CLOUD_LIGHTS = [(-30.0, 0.30), (-18.0, 0.36), (-6.0, 0.42), (5.0, 0.48), (14.0, 0.54), (24.0, 0.60), (34.0, 0.66)]


def ground_program():
    """Prairie at night: dark soil and stubble, a slow variation, nothing that reads as a pattern."""
    return {
        "name": "scGround",
        "ops": [
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            {"kind": "noise", "dst": 1, "srcA": 0, "value": 0.08, "seed": 4},
            {"kind": "microDetail", "dst": 2, "srcA": 0, "value": 2.5, "seed": 5},
            {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},
            {"kind": "ramp", "dst": 3, "srcA": 1, "constant": hexrgb("#141611") + [1.0],
             "constant2": hexrgb("#262a1d") + [1.0], "constant3": hexrgb("#3a3a26") + [1.0]},
        ],
        "baseColor": 3, "metallic": -1, "roughness": -1, "emission": -1, "emissionIntensity": 1.0, "opacity": -1,
    }


def pole_line():
    """Pole positions along the line, 52 m apart, each with a tiny lean and its crossarm across the line."""
    dx, dz = POLE_END[0] - POLE_START[0], POLE_END[2] - POLE_START[2]
    length = math.hypot(dx, dz)
    n = int(length // 52.0) + 1
    ux, uz = dx / length, dz / length
    out = []
    for i in range(n):
        x, z = POLE_START[0] + ux * 52.0 * i, POLE_START[2] + uz * 52.0 * i
        out.append((x, z))
    yaw = math.degrees(math.atan2(ux, uz))
    return out, yaw, (ux, uz)


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.55, "transient": 0.6, "sustain": 0.5, "attack": 0.9, "release": 1.2}
    s.environment = {
        "intensity": 0.6, "background": hexrgb("#0b0e0c"), "fogColor": hexrgb("#3d4636"),
        # the march carries the tornado alone (volumeMaxDistance 0, ADR-705); the air's haze is the surface pass's
        "volumeDensity": 0.00045, "volumeMaxDistance": 0.0, "volumeAnisotropy": 0.05, "volumeNoise": 0.0,
        "volumeLocalLights": 1.0, "volumeShadowStrength": 0.0, "fogSky": 1.0, "fogSkyDistance": 1800.0,
        "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": hexrgb("#050706"), "horizonColor": hexrgb("#a8b48a"),
                "groundColor": hexrgb("#0c0e0b"), "haze": 0.05, "sunIntensity": 0.0, "sunColor": [1, 1, 1],
                "sunSize": 0.01, "sunGlow": 0.1, "intensity": 0.8, "background": True, "useKeyLight": False},
    }
    s.composition = {
        "focalPoints": [{"name": "funnel", "position": [TORNADO[0], 60.0, TORNADO[2]], "radius": 80.0,
                         "weight": 1.0},
                        {"name": "farm", "position": [FARM[0], 6.0, FARM[2]], "radius": 10.0, "weight": 0.4}],
        "layers": [
            {"name": "field", "start": 0.0, "end": 40.0, "contrast": 1.0, "saturation": 0.8},
            {"name": "prairie", "start": 40.0, "end": 900.0, "contrast": 0.85, "saturation": 0.7},
            {"name": "storm", "start": 900.0, "end": 9000.0, "contrast": 0.75, "saturation": 0.6},
        ],
    }
    s.program(ground_program())

    # ---- the prairie (a procedural's size is at most 1000 m: the ground is scaled)
    s.proc("ground", {"kind": "box", "size": [1000.0, 0.4, 1000.0], "subdivisions": 2},
           material={"baseColor": [1, 1, 1], "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0, "roughness": 0.9,
                     "metallic": 0.0, "program": "scGround"},
           transform={"position": [0.0, -0.2, -2500.0], "rotation": [0, 0, 0], "scale": [8.0, 1.0, 8.0]})

    # ---- the tornado: a classic cone on the right third, under a broad wall cloud
    s.effect("storm", "tornado", ("world",), parameters={
        "base": list(TORNADO), "height": CLOUD_Y, "radiusBottom": 36.0, "radiusMid": 50.0, "radiusTop": 85.0,
        "taper": 1.5, "shellWidth": 0.24, "shellGain": 1.0, "coreRadius": 0.55, "coreDensity": 0.4,
        "edgeSoft": 0.35, "wallCloudGain": 0.9, "cloudWidth": 5.0, "cloudHeight": 0.16, "cloudDensity": 1.1,
        "touchdown": 1.0, "footSoft": 0.04, "skirtWidth": 2.6, "skirtHeight": 0.09, "skirtDensity": 0.9,
        "skirtFlare": 0.7, "stripeCount": 4.0, "stripePitch": 3.4, "stripeDepth": 0.35, "stripeHarmonic": 0.4,
        "suctionCount": 0.0, "suctionStrength": 0.0, "suctionRadius": 1.0, "suctionWidth": 0.35,
        "suctionSpeed": 0.9, "cloudAmount": 0.65, "macroAmp": 1.0, "mesoAmp": 0.5, "microAmp": 0.25,
        "detailContrast": 1.6, "detailScale": 2.4, "climbRate": 0.07, "erosion": 1.2, "edgeWidth": 0.6,
        "circulation": 700.0, "coreRadiusMetres": 0.0, "inflow": 0.25, "lift": 1.0, "rotationBottom": 1.0,
        "rotationTop": 0.55, "rotationCurve": 1.0, "wobbleAmount": 22.0, "wobbleSpeed": 0.12, "lean": [-60.0, 0.0],
        # dense and dark: mostly absorbing (a low scattering), so the funnel is a silhouette on the clear slot
        "density": 0.09, "emission": 0.0, "scattering": 0.18,
        "colorThin": hexrgb("#2a302a"), "colorThick": hexrgb("#050605")})

    # ---- the power line: poles receding toward the funnel's foot, three wires sagging between the nearest
    poles, yaw, (ux, uz) = pole_line()
    wood = {"baseColor": hexrgb("#1b1712"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0, "roughness": 0.9,
            "metallic": 0.0}
    for i, (x, z) in enumerate(poles):
        lean = ((i * 37) % 7 - 3) * 0.6
        s.proc("pole%d" % i, {"kind": "cylinder", "radius": 0.14, "height": 10.5, "segments": 8},
               material=wood, transform={"position": [x, 5.25, z], "rotation": [lean, yaw, 0.0],
                                         "scale": [1, 1, 1]})
        s.proc("arm%d" % i, {"kind": "box", "size": [2.6, 0.14, 0.14], "subdivisions": 1}, material=wood,
               transform={"position": [x, 9.8, z], "rotation": [0.0, yaw, lean * 1.5], "scale": [1, 1, 1]})
    nwire = min(6, len(poles))
    px, pz = -uz, ux                                  # across the line
    for w, off in enumerate((-1.15, 0.0, 1.15)):
        pts = []
        for i in range(nwire):
            x, z = poles[i]
            pts.append({"position": [x + px * off, 9.9 if off else 10.55, z + pz * off], "scale": 1.0, "roll": 0.0})
            if i + 1 < nwire:
                x2, z2 = poles[i + 1]
                pts.append({"position": [(x + x2) * 0.5 + px * off, (9.9 if off else 10.55) - 1.4,
                                         (z + z2) * 0.5 + pz * off], "scale": 1.0, "roll": 0.0})
        s.proc("wire%d" % w, {"kind": "tube", "tubeRadius": 0.03, "tubeTaper": 1.0, "tubeSides": 4,
                              "tubeSegments": 200, "tubeTwist": 0.0, "tubeCaps": False,
                              "curve": {"kind": "catmullRom", "generator": "points", "points": pts,
                                        "samplesPerSegment": 8}},
               material={"baseColor": hexrgb("#0c0c0c"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                         "roughness": 0.6, "metallic": 0.0})

    # ---- the farm: a house, a barn and a pole light (sodium), the one warm point
    dark = {"baseColor": hexrgb("#15130f"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0, "roughness": 0.9,
            "metallic": 0.0}
    s.proc("house", {"kind": "box", "size": [9.0, 5.0, 7.0], "subdivisions": 1}, material=dark,
           transform={"position": [FARM[0] - 14.0, 2.5, FARM[2] - 6.0], "rotation": [0, 24, 0], "scale": [1, 1, 1]})
    s.proc("roof", {"kind": "box", "size": [6.4, 6.4, 7.2], "subdivisions": 1}, material=dark,   # a gable
           transform={"position": [FARM[0] - 14.0, 5.0, FARM[2] - 6.0], "rotation": [0, 24, 45],
                      "scale": [1, 1, 1]})
    s.proc("barn", {"kind": "box", "size": [14.0, 8.0, 10.0], "subdivisions": 1}, material=dark,
           transform={"position": [FARM[0] + 14.0, 4.0, FARM[2] - 14.0], "rotation": [0, 18, 0], "scale": [1, 1, 1]})
    s.proc("lampPost", {"kind": "cylinder", "radius": 0.12, "height": 8.0, "segments": 6}, material=dark,
           transform={"position": [FARM[0], 4.0, FARM[2]], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    s.proc("lamp", {"kind": "sphere", "radius": 0.45, "segments": 16, "rings": 8},
           material={"baseColor": [0, 0, 0], "emissiveColor": hexrgb(SODIUM), "emissiveIntensity": 60.0,
                     "roughness": 1.0, "metallic": 0.0},
           transform={"position": [FARM[0], 8.2, FARM[2]], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    s.light("sodium", "point", position=[FARM[0], 8.0, FARM[2]], color=hexrgb(SODIUM), intensity=5000.0,
            range=90.0, radius=0.5, castsShadow=False, volumetric=1.0)

    # ---- the wheat: ears on stalks, a field before the camera, bending in the wind
    stalk = lathe([(0.0, 0.007), (0.78, 0.006), (0.84, 0.017), (0.93, 0.016), (0.99, 0.004)], sides=5, samples=2)
    s.proc("wheat", stalk,
           distribution={"kind": "grid", "gridCount": [100, 1, 44], "gridSpacing": [0.22, 1.0, 0.24]},
           material={"baseColor": hexrgb("#3a3626"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                     "roughness": 0.85, "metallic": 0.0},
           variation={"seed": 11, "position": [0.1, 0.0, 0.11], "rotation": [0.12, 3.14, 0.12],
                      "scale": [0.0, 0.18, 0.0], "uniformScale": 0.1},
           deformers=[{"kind": "bend", "amount": 0.25, "axis": [0, 1, 0], "displacementAxis": [1.0, 0.0, 0.35],
                       "center": [0, 0, 0], "falloff": 0.0},
                      {"kind": "noise", "amount": 0.14, "scale": 0.35, "speed": 0.7, "seed": 3,
                       "axisMask": [1.0, 0.0, 0.6], "axis": [0, 1, 0], "center": [0, 0, 0], "falloff": 1.0,
                       "space": "world"}],
           transform={"position": [CAM[0] + 0.4, 0.0, CAM[2] - 7.8], "rotation": [0, 0, 0], "scale": [1, 1, 1]})

    # ---- rain: long thin streaks round the camera, slanting with the wind
    s.particles("rain", capacity=24000, seed=31, shape="box", position=[CAM[0] + 2.0, 9.0, CAM[2] - 12.0],
                extent=[18.0, 1.0, 16.0], direction=[0.22, -1.0, -0.08], spawnRate=1600.0, lifetimeMin=0.7,
                lifetimeMax=0.9, spread=0.02, speedMin=11.0, speedMax=13.0, gravity=[0, -2.0, 0], drag=0.0,
                sizeStart=0.006, sizeEnd=0.006, colorStart=hexrgb("#c9d6c8") + [0.22],
                colorEnd=hexrgb("#c9d6c8") + [0.16], emissive=0.35, blend="additive", velocityStretch=1.0,
                stretchMax=0.9)

    # ---- light: a cold back light from the clear slot (it rims the funnel and the cloud's underside)
    s.light("slot", "directional", direction=_toward(16.0, 4.0), color=hexrgb("#b8c8a0"), intensity=1.1,
            castsShadow=False)
    # the cloud's inner lights, one per place (notes), plus the kick's thunder glow
    for k, (yaw_deg, _) in enumerate(CLOUD_LIGHTS):
        p = bearing(yaw_deg, 1250.0, CLOUD_Y - 40.0)
        s.light("cloud%d" % k, "point", position=p, color=hexrgb("#cfd8ff"), intensity=0.0, range=520.0,
                radius=40.0, castsShadow=False, volumetric=1.0)
    s.light("thunder", "point", position=bearing(6.0, 1400.0, CLOUD_Y + 60.0), color=hexrgb("#c8d0ff"),
            intensity=0.0, range=900.0, radius=80.0, castsShadow=False, volumetric=1.0)

    # ---- lightning: the snare strikes under the cloud, scattered round the funnel
    s.effect("strike", "lightning", ("world",),
             trigger={"source": "signal", "name": signals.S("snare"), "threshold": 0.25},
             parameters={"jaggedness": 0.3, "branchProbability": 0.55, "branchDecay": 0.5, "depth": 7,
                         "height": CLOUD_Y - 30.0, "lean": 120.0, "scatter": 650.0,
                         "coreColor": hexrgb(BOLT), "coreIntensity": 90.0, "coreWidth": 1.6,
                         "glowColor": hexrgb("#8fa8ff"), "glowIntensity": 2.0, "glowWidth": 22.0,
                         "flashIntensity": 4.0e6, "flashRange": 2600.0, "flashFog": 0.9, "leader": 0.05,
                         "restrikes": 2.0, "strokeDuration": 0.3, "afterglow": 0.3, "seed": 7.0,
                         "source": {"kind": "world", "position": [TORNADO[0] - 220.0, 0.0, TORNADO[2] + 120.0]}})

    # ---- camera: low in the field; the horizon on the lower third, the funnel on the right third
    s.camera["fov"] = 50.0
    s.drift_camera(centre=CAM, target=(CAM[0] + 0.0, CAM[1] + 15.2, CAM[2] - 90.0), period=80.0,
                   amp=(0.08, 0.02, 0.05), tamp=(0.2, 0.08, 0.0))

    # ---- the instrument ---------------------------------------------------------------------------------------------
    # bass: the funnel winds (rotation and width), the debris skirt thickens, the wind leans on the wheat
    s.route(R("bass", "fx/storm/circulation", 900.0, attackMs=50, decayMs=1400),
            R("bass", "fx/storm/radiusBottom", 22.0, attackMs=50, decayMs=1600),
            R("bass", "fx/storm/skirtDensity", 1.2, attackMs=50, decayMs=900),
            R("bass", "procedural/wheat/deform/1/amount", 0.25, attackMs=50, decayMs=900))
    # density: the storm's chaos
    s.route(R("density", "fx/storm/wobbleAmount", 40.0, **SLOW),
            R("density", "particles/rain/turbulence", 1.5, **SLOW))
    # sustain: the cloud glows from within; the storm feeds
    s.route(R("sustain", "fx/storm/emission", 0.03, **SLOW),
            R("sustain", "lights/thunder/intensity", 4.0e5, **SLOW))
    # kick: thunder -- the cloud flashes from inside, a gust bends the field, the camera nudges
    s.route(R("kick", "lights/thunder/intensity", 3.0e6, attackMs=0, decayMs=380),
            R("kick", "procedural/wheat/deform/1/amount", 0.35, attackMs=40, decayMs=700),
            R("kick", "procedural/wheat/deform/2/amount", 0.12, attackMs=40, decayMs=900),
            R("kick", "post/lens/chromaticAberration", 0.01, attackMs=0, decayMs=150))
    # snare: the strike (its own trigger) lights the sky, the rain and the exposure for a flicker
    # (not the sky's intensity: every change of the procedural sky rebuilds its lighting cube, 7-8 ms a time)
    s.route(R("snare", "lights/slot/intensity", 6.0, attackMs=0, decayMs=220),
            R("snare", "particles/rain/emissive", 4.0, attackMs=0, decayMs=200),
            R("snare", "camera/exposure/compensation", 0.6, attackMs=0, decayMs=260))
    # hat: rain -- density and streak length
    s.route(R("hatRate", "particles/rain/spawnRate", 5000.0, **MEDIUM),
            R("hat", "particles/rain/burst", 900.0, attackMs=0, decayMs=40),
            R("hatRate", "particles/rain/stretch", 0.8, **MEDIUM))
    # melodic: each note lights the cloud base at its place (low notes left), its brightness the velocity
    s.places("cl", "lastPitch", [c for _, c in CLOUD_LIGHTS], 0.045, event="noteOn")
    for k in range(len(CLOUD_LIGHTS)):
        s.route(R("visual.clHit%d" % k, "lights/cloud%d/intensity" % k, 2.2e6, depth="lastVelocity", attackMs=0,
                  decayMs=420))
    s.route(R("noteEnv", "post/bloom/intensity", 0.06, attackMs=0, decayMs=300))

    s.params_({
        "scene/volumeSteps": 40, "scene/volumeJitter": 0.5,
        "post/bloom/intensity": 0.12, "post/bloom/threshold": 1.2,
        "post/output/vignette": 0.42, "post/output/grain": 0.035, "post/grade/saturation": 0.78,
        "post/grade/contrast": 1.08, "post/grade/temperature": -0.04, "post/grade/tint": -0.05,
        "post/lens/chromaticAberration": 0.002, "post/tonemap/operator": 3,
        "camera/lens/focalLength": 24.0, "camera/exposure/compensation": 0.0,
    })

    # ---- the evaluator's screen regions, projected through the camera at t = 0
    s.region_points("funnel", [[TORNADO[0] - 90.0, 0.0, TORNADO[2]], [TORNADO[0] + 90.0, 0.0, TORNADO[2]],
                               [TORNADO[0] - 90.0, 420.0, TORNADO[2]], [TORNADO[0] + 90.0, 420.0, TORNADO[2]]])
    s.region_points("cloud", [bearing(-40.0, 1250.0, CLOUD_Y), bearing(40.0, 1250.0, CLOUD_Y),
                              bearing(-40.0, 1250.0, CLOUD_Y + 300.0), bearing(40.0, 1250.0, CLOUD_Y + 300.0)])
    s.region("farm", centre=[FARM[0], 6.0, FARM[2]], radius=25.0)
    s.region("field", box=[0.0, 0.78, 1.0, 1.0])
    return s


def _toward(yaw_deg, el_deg):
    """The direction a light TRAVELS when its source is at bearing `yaw` (0 = -Z, positive right), `el` degrees up."""
    y, e = math.radians(yaw_deg), math.radians(el_deg)
    return [round(-math.sin(y) * math.cos(e), 5), round(-math.sin(e), 5), round(math.cos(y) * math.cos(e), 5)]
