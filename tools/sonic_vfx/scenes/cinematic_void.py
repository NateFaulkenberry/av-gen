"""8. ABSTRACT CINEMATIC VOID: THE GATE (04-brief-abstract-direction.md, direction 8; ABSTRACT-PLAN.md section 8).

One colossal object, one horizon, one light. A ring 300 m across, built of 64 dark segments, stands on the horizon of a
mirror plane with a low sun exactly inside it; amber haze; a single tiny figure on the lower third gives the scale;
letterboxed 2.39:1. The camera is a slow, majestic push.

Grammar: almost nothing, at enormous scale. Atmosphere, light and the camera carry the image. The ring is a
`radial` distribution of box segments (its count and gaps are structural instruments). There is no planar reflection
in the engine, so the mirror is built: the ring and the figure have twins below the horizon, and the sky's lower
hemisphere is the plane.

Letterbox: the engine's `post/display/letterbox` (ADR-1075), 2.39:1.
"""
import math

from .. import kit
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT, VERY_SLOW

ID = "cinematic-void"
TITLE = "Abstract Cinematic Void"

TEAL = "#3a8aa0"
TEAL_MID = "#1d4a52"
AMBER = "#ff9a3c"
AMBER_PALE = "#ffcf86"
LIGHT = "#fff4dc"
SIL = "#0a0a0c"

RING_C = (0.0, 103.0, -950.0)    # 6.2 degrees up from the camera
RING_R = 100.0                    # 6 degrees: it stands on the horizon, inside the letterbox
SEGMENTS = 64
FOCAL = 50.0
PUSH = 120.0                       # seconds for the push
CAM0 = (0.0, 1.8, 70.0)
CAM1 = (0.0, 1.8, 10.0)
FIGURE = (7.5, 0.0, -32.0)
HAZE_W = 0.07                      # the sky's haze width: amber in a narrow band at the horizon, teal above
HORIZON_Y = CAM1[1]                # a backdrop's top edge at eye height reads as the horizon at any distance
MIRROR_Z = -1700.0
MIRROR_H = 900.0                   # (a procedural box is at most 1000 m: the width comes from the transform's scale)
SUN_D = 1500.0                     # the geometric sun's distance (behind the ring, before the mirror's backdrop)
SUN_R = 140.0                      # its disc's radius: the glow fades to the sky's own colour at the rim
TWIN_SUN_R = 160.0

DESIGN = {
    "category": "cinematic",
    "thesis": "The Gate: a colossal segmented ring on the horizon of a mirror plane, a low sun burning inside it, "
              "amber haze, one tiny figure. The music breathes the haze and the light, parts the ring's segments as it "
              "builds and lights the segment at the pitch's angle.",
    "composition": {
        "background": "a teal sky grading to amber at the horizon; the sun inside the ring",
        "midground": "the ring and its reflection, a dark silhouette with seams of light",
        "foreground": "one figure on the lower third, and its reflection",
        "focal": "the sun inside the ring, on the frame's vertical centre line",
        "secondary": ["the figure", "the reflection", "the dust in the light"],
        "atmosphere": "amber haze lit from behind the ring",
        "post": "letterbox 2.39:1, halation, grain, a slow bloom",
        "camera": "a slow straight push toward the ring, low (1.8 m)",
    },
    "palette": {"dominant": TEAL, "secondary": AMBER, "accent": AMBER_PALE, "highlight": LIGHT,
                "background_value": "mid-dark", "saturation": "amber against teal; a black silhouette"},
    "motion": {
        "very_slow": ["the push", "the ring's turn"],
        "medium": ["the haze breathing", "the segments parting"],
        "fast": ["a segment lighting on a note"],
        "extremely_fast": ["a pulse of light round the seams on the kick"],
    },
    "vocabulary": [
        ["bass", "response.bass", "the air breathes; the sun swells"],
        ["kick", "response.kick", "a pulse of light round the ring's segments"],
        ["snare", "response.snare", "dust bursts in the light"],
        ["hat", "response.hat", "the dust glitters"],
        ["mids", "audio.mid", "the ring turns slowly"],
        ["brightness", "sonic.brightness.slow", "the grade: amber to rose and teal"],
        ["sustained", "response.sustain", "the sun brightens"],
        ["beat", "beat.pulse", "the ring breathes its radius"],
        ["intensity", "response.intensity", "STRUCTURE: the ring parts, its segments drifting apart as the piece "
         "builds"],
        ["note", "notes.lastPitch", "the segment at the note's angle lights: a beacon round the ring"],
        ["chord", "notes.polyphony", "STRUCTURE: the segment count"],
        ["held", "notes.held", "the segments drift outward"],
        ["mod wheel", "control.modwheel", "the sun's height inside the ring"],
        ["silence", "(no input)", "the slow push; the ring stands"],
    ],
    "tier": "medium: 64 instanced segments twice, a haze march, a few particles",
}


def ring(s, name, centre):
    """The ring: 64 segments on a radial distribution facing the camera. Radially symmetric, so its mirror image in the
    plane is the same ring at the mirrored height (a procedural's scale cannot be negative)."""
    seg_len = 2.0 * math.pi * RING_R / SEGMENTS * 0.9
    s.proc(name, {"kind": "box", "size": [seg_len, 16.0, 11.0], "subdivisions": 1, "bevel": 0.6, "bevelSegments": 2},
           distribution={"kind": "radial", "count": SEGMENTS, "radius": RING_R, "plane": "xy",
                         "orientation": "outward"},
           material={"baseColor": hexrgb("#16161a"), "emissiveColor": hexrgb(AMBER_PALE), "emissiveIntensity": 0.0,
                     "roughness": 0.45, "metallic": 0.35},
           transform={"position": list(centre), "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]})


def sky_mix(angle):
    """The sky's horizon share at an elevation (environment.wgsl skyRadiance): exp(-sin(angle) / haze)."""
    return math.exp(-math.sin(angle) / HAZE_W)


def sky_colour(angle, dim=1.0):
    m = sky_mix(angle)
    return [round((z * (1.0 - m) + h * m) * dim, 5) for z, h in zip(hexrgb(TEAL), hexrgb(AMBER))]


def gradient_ramp_ops(y0, y1, angles, dim):
    """r1 = t over worldPosition.y from y0 (t 0) to y1 (t 1); r2 = the sky's colour at the three angles (a ramp)."""
    stops = [sky_colour(a, dim) + [1.0] for a in angles]
    return [
        {"kind": "input", "dst": 0, "input": "worldPosition"},
        {"kind": "gradient", "dst": 1, "srcA": 0, "constant": [0.0, 1.0 / (y1 - y0), 0.0, -y0 / (y1 - y0)],
         "value": 1.0},
        {"kind": "ramp", "dst": 2, "srcA": 1, "constant": stops[0], "constant2": stops[1], "constant3": stops[2]},
    ]


def glow_ops(radius, colour, power):
    """r3 = a radial glow (1 - r/R)^power in the disc's own plane (a cylinder's local XZ)."""
    return [
        {"kind": "input", "dst": 4, "input": "localPosition"},
        {"kind": "multiply", "dst": 5, "srcA": 4, "srcB": 4},
        {"kind": "gradient", "dst": 5, "srcA": 5, "constant": [1.0, 0.0, 1.0, 0.0], "value": 1.0 / (radius * radius)},
        {"kind": "power", "dst": 5, "srcA": 5, "value": 0.5},
        {"kind": "remap", "dst": 5, "srcA": 5, "value": 1, "constant": [0.0, 1.0, 1.0, 0.0]},
        {"kind": "power", "dst": 5, "srcA": 5, "value": power},
        {"kind": "constant", "dst": 6, "constant": colour + [1.0]},
        {"kind": "multiply", "dst": 3, "srcA": 6, "srcB": 5},
    ]


def mirror_program(twin_y):
    """The water: the sky's gradient at the mirrored angle (t = depth below the horizon over MIRROR_H), x 0.82 for
    water's reflectance, plus the sun's reflection as a radial glow round (0, twin_y) on the backdrop. One surface: a
    separate glow disc 1700 m out leaked the cleared background through slivers of its triangle fan (its depth prepass
    and colour pass disagreed at that range)."""
    dist = CAM1[2] - MIRROR_Z
    angles = [math.atan2(t * MIRROR_H, dist) for t in (0.0, 0.5, 1.0)]
    ops = gradient_ramp_ops(HORIZON_Y, HORIZON_Y - MIRROR_H, angles, 0.82)
    ops += [
        {"kind": "constant", "dst": 4, "constant": [0.0, -twin_y, 0.0, 0.0]},
        {"kind": "add", "dst": 4, "srcA": 0, "srcB": 4},                         # p - centre
        {"kind": "multiply", "dst": 5, "srcA": 4, "srcB": 4},
        {"kind": "gradient", "dst": 5, "srcA": 5, "constant": [1.0, 1.0, 0.0, 0.0],
         "value": 1.0 / (TWIN_SUN_R * TWIN_SUN_R)},
        {"kind": "power", "dst": 5, "srcA": 5, "value": 0.5},
        {"kind": "remap", "dst": 5, "srcA": 5, "value": 1, "constant": [0.0, 1.0, 1.0, 0.0]},
        {"kind": "power", "dst": 6, "srcA": 5, "value": 2.4},
        {"kind": "constant", "dst": 7, "constant": hexrgb(AMBER_PALE, 2.0) + [1.0]},
        {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 7},
        {"kind": "power", "dst": 5, "srcA": 5, "value": 8.0},
        {"kind": "constant", "dst": 7, "constant": hexrgb(LIGHT, 9.0) + [1.0]},
        {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 7},
        {"kind": "add", "dst": 6, "srcA": 6, "srcB": 5},
        {"kind": "add", "dst": 7, "srcA": 2, "srcB": 6},
    ]
    return {"name": "cvMirror", "ops": ops, "baseColor": -1, "metallic": -1, "roughness": -1, "emission": 7,
            "emissionIntensity": 1.0, "opacity": -1}


def sun_program(sun_y):
    """The sun itself (the sky's own disc renders blocky from the lighting cube): the sky's gradient where it stands
    (three stops round its elevation) plus a core and a glow, fading to the sky's own colour at the disc's rim."""
    d = SUN_D
    y0, y1 = sun_y - SUN_R, sun_y + SUN_R
    angles = [math.atan2(y - HORIZON_Y, d) for y in (y0, sun_y, y1)]
    ops = gradient_ramp_ops(y0, y1, angles, 1.0)
    # a white-gold core and a wide amber halo from one radial falloff (r5 = 1 - r/R)
    ops += glow_ops(SUN_R, hexrgb(AMBER_PALE, 2.4), 2.2)
    ops += [{"kind": "power", "dst": 6, "srcA": 5, "value": 7.0},
            {"kind": "constant", "dst": 7, "constant": hexrgb(LIGHT, 26.0) + [1.0]},
            {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 7},
            {"kind": "add", "dst": 3, "srcA": 3, "srcB": 6},
            {"kind": "add", "dst": 7, "srcA": 2, "srcB": 3}]
    return {"name": "cvSun", "ops": ops, "baseColor": -1, "metallic": -1, "roughness": -1, "emission": 7,
            "emissionIntensity": 1.0, "opacity": -1}


def instrument(s):
    """The modulation map (ABSTRACT-PLAN.md section 8). Restraint: the void moves slowly, light and air carry it."""
    P = "procedural/%s/"
    # ---- BASS: the haze breathes; the sun's glow swells
    s.route(R("bass", "scene/volumeDensity", 0.0009, attackMs=80, decayMs=700),
            R("bass", "lights/sun/intensity", 2.5, attackMs=60, decayMs=600))
    # ---- KICK: a pulse of light round the ring's seams (its segments glow amber and fade)
    for node in ("ring", "ringTwin"):
        s.route(R("kick", P % node + "material/emissive", 3.5, attackMs=0, decayMs=600))
    # ---- SNARE: dust bursts in the light
    s.route(R("snare", "particles/dust/burst", 500.0, threshold="binary", thresholdLevel=0.05))
    # ---- HIGHS: the dust glitters
    s.route(R("audio.treble", "particles/dust/emissive", 2.5, attackMs=20, decayMs=250),
            R("hat", "particles/dust/emissive", 1.5, attackMs=0, decayMs=90))
    # ---- MIDS: the ring turns slowly about its axis (integrated)
    for node in ("ring", "ringTwin", "beacon", "beaconTwin"):
        s.route(R("audio.mid", P % node + "transform/rotation", 4.0 if "Twin" not in node else -4.0, comp=2,
                  integrate=True, attackMs=300, decayMs=1500))
    # ---- CENTROID: the grade -- amber for a dark timbre, rose and teal for a bright one
    s.route(R("brightnessSlow", "env/sky/horizonColor", -0.35, comp=1, **SLOW),
            R("brightnessSlow", "env/sky/horizonColor", 0.25, comp=2, **SLOW),
            R("brightnessSlow", "post/grade/temperature", -0.25, **SLOW))
    # ---- SUSTAIN: the sun inside the ring brightens
    s.route(R("sustain", "lights/sun/intensity", 5.0, **SLOW))
    # ---- TEMPO: the ring breathes its radius on the beat (a few metres at 300)
    for node in ("ring", "ringTwin"):
        s.route(R("beat", P % node + "distribution/radius", 2.5, attackMs=0, decayMs=400))
    # ---- INTENSITY: STRUCTURE -- the ring parts: its segments move apart as the piece builds
    for node in ("ring", "ringTwin", "beacon", "beaconTwin"):
        s.route(R("intensity", P % node + "distribution/radius", 22.0, **VERY_SLOW))
    # ---- MIDI: the segment at the pitch's angle lights (a beacon round the ring), as bright as the velocity;
    # STRUCTURE: a chord sets the segment count; held notes drift the segments outward
    for node in ("beacon", "beaconTwin"):
        s.route(R("lastPitch", P % node + "distribution/startAngle", 2.0 * math.pi * (1 if "Twin" not in node else -1),
                  springHz=1.2, springDamping=0.85),
                R("noteEnv", P % node + "material/emissive", 9.0, depth="lastVelocity", attackMs=0, decayMs=900))
    for node in ("ring", "ringTwin"):
        s.route(R("polyphony", P % node + "distribution/count", 64.0, attackMs=0, decayMs=1500),
                R("held", P % node + "distribution/radius", 9.0, **MEDIUM))
    # ---- MOD WHEEL: the sun's height inside the ring (a sunrise by hand)
    s.route(R(s.modwheel(), "lights/sun/elevation", 6.0, attackMs=100, decayMs=100))


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.6, "attack": 1.2, "release": 1.5,
                  "floorDb": -44.0, "rangeDb": 42.0}     # mastered music does not saturate the levels
    elev = math.atan2(RING_C[1] - CAM1[1], -(RING_C[2] - CAM1[2]))
    sun_dir = [0.0, -math.sin(elev), math.cos(elev)]          # travelling toward the camera, slightly down
    # the air: almost none. In-scatter toward a low sun floods a long march (the whole frame went amber at 1400 m), and
    # beyond the march the same density is analytic SURFACE fog, which turned the ring at 960 m into fog colour while
    # the sky behind it kept its gradient. The distance is the sky's own gradient; the glow is the sun's disc.
    s.environment = {
        "intensity": 0.2, "background": hexrgb(TEAL), "fogColor": hexrgb("#c47a3c"), "volumeDensity": 0.00012,
        "volumeMaxDistance": 70.0, "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": hexrgb(TEAL), "horizonColor": hexrgb(AMBER),
                "groundColor": hexrgb(AMBER), "haze": HAZE_W, "sunIntensity": 0.0, "sunGlow": 0.045,
                "intensity": 1.0, "background": True, "useKeyLight": True},
    }
    s.light("sun", "directional", direction=sun_dir, color=hexrgb(AMBER_PALE), intensity=6.0, castsShadow=False,
            volumetric=0.25)
    s.light("fill", "directional", direction=[0.2, -0.3, -1.0], color=hexrgb("#4a8a96"), intensity=0.25,
            castsShadow=False)

    # ---- the mirror: the engine has no planar reflection and its sky is one flat colour below the horizon, so the
    # still water is BUILT: a backdrop below the horizon whose emission is the sky's own gradient mirrored (the
    # sky's formula, mix(zenith, horizon, exp(-sin(angle) / haze)), sampled at the depression angle and dimmed to
    # water's reflectance), the twin sun on it, the twin ring and figure in front of it
    twin_y = HORIZON_Y - (CAM1[2] - MIRROR_Z) * math.tan(elev)
    s.program(mirror_program(twin_y))
    s.proc("mirror", {"kind": "box", "size": [1000.0, MIRROR_H, 2.0], "subdivisions": 1},
           material={"baseColor": [0, 0, 0], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0, "roughness": 1.0,
                     "metallic": 0.0, "unlit": True, "program": "cvMirror"},
           transform={"position": [0.0, HORIZON_Y - MIRROR_H * 0.5, MIRROR_Z], "rotation": [0, 0, 0],
                      "scale": [7.0, 1, 1]})
    sun_pos = [CAM1[0], HORIZON_Y + SUN_D * math.tan(elev), CAM1[2] - SUN_D]
    s.program(sun_program(sun_pos[1]))
    s.proc("sunDisc", {"kind": "cylinder", "radius": SUN_R, "height": 1.0, "radialSegments": 96, "caps": True},
           material={"baseColor": [0, 0, 0], "emissiveColor": hexrgb(LIGHT), "emissiveIntensity": 1.0,
                     "roughness": 1.0, "metallic": 0.0, "unlit": True, "program": "cvSun"},
           transform={"position": sun_pos, "rotation": [90.0, 0.0, 0.0], "scale": [1, 1, 1]})

    ring(s, "ring", RING_C)
    ring(s, "ringTwin", (RING_C[0], -RING_C[1], RING_C[2]))

    # the beacon: one segment that the notes place round the ring (a radial distribution of one, its start angle
    # routed), and its twin
    seg_len = 2.0 * math.pi * RING_R / SEGMENTS * 0.9
    for name, cy in (("beacon", RING_C[1]), ("beaconTwin", -RING_C[1])):
        s.proc(name, {"kind": "box", "size": [seg_len * 0.92, 17.0, 11.6], "subdivisions": 1},
               distribution={"kind": "radial", "count": 1, "radius": RING_R, "plane": "xy", "orientation": "outward",
                             "startAngle": 1.5708, "endAngle": 1.5708 + 6.2832},
               material={"baseColor": hexrgb("#16161a"), "emissiveColor": hexrgb(LIGHT), "emissiveIntensity": 0.0,
                         "roughness": 0.45, "metallic": 0.35},
               transform={"position": [RING_C[0], cy, RING_C[2] + 0.4], "rotation": [0.0, 0.0, 0.0],
                          "scale": [1.0, 1.0, 1.0]})

    # the figure (a simple silhouette) and its twin
    for name, sign in (("figure", 1.0), ("figureTwin", -1.0)):
        s.proc(name + "Body", {"kind": "cylinder", "radius": 0.24, "height": 1.35, "radialSegments": 12,
                               "caps": True, "bevel": 0.12},
               material={"baseColor": hexrgb(SIL), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                         "roughness": 1.0, "metallic": 0.0},
               transform={"position": [FIGURE[0], sign * 0.78, FIGURE[2]], "rotation": [0, 0, 0],
                          "scale": [1.0, 1.0, 0.7]})
        s.proc(name + "Head", {"kind": "sphere", "radius": 0.13, "segments": 12, "rings": 8},
               material={"baseColor": hexrgb(SIL), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                         "roughness": 1.0, "metallic": 0.0},
               transform={"position": [FIGURE[0], sign * 1.6, FIGURE[2]], "rotation": [0, 0, 0], "scale": [1, 1, 1]})

    # dust in the light near the camera's path
    s.particles("dust", capacity=3000, seed=13, shape="box", position=[0.0, 4.0, 20.0], extent=[30.0, 6.0, 40.0],
                direction=[0, 1, 0], spawnRate=280.0, lifetimeMin=8.0, lifetimeMax=12.0, spread=1.0,
                speedMin=0.02, speedMax=0.1, gravity=[0, -0.01, 0], drag=0.2, turbulence=0.1, turbulenceScale=0.2,
                sizeStart=0.03, sizeEnd=0.03, sizeVariance=0.6, sizeSkew=2.0,
                colorStart=hexrgb(AMBER_PALE) + [0.6], colorEnd=hexrgb(AMBER_PALE) + [0.0], emissive=0.6,
                blend="additive", scatterStrength=6.0, scatterAnisotropy=0.8)

    # the push: position and target move together (a constant direction), the letterbox bars ride it
    look = [0.0, math.tan(math.radians(3.24)), -1.0]     # 3.24 degrees up: the horizon on the lower third
    n = math.sqrt(sum(c * c for c in look))
    look = [c / n for c in look]
    t0 = [CAM0[k] + look[k] * 100.0 for k in range(3)]
    t1 = [CAM1[k] + look[k] * 100.0 for k in range(3)]
    s.track("camera/position", [{"time": 0.0, "value": list(CAM0), "interp": "smooth"},
                                {"time": PUSH, "value": list(CAM1), "interp": "smooth"}], loop=PUSH)
    s.track("camera/target", [{"time": 0.0, "value": t0, "interp": "smooth"},
                              {"time": PUSH, "value": t1, "interp": "smooth"}], loop=PUSH)
    s.camera = {"mode": 1, "position": list(CAM0), "target": t0, "fov": 30.0, "orbitSpeed": 0.0}
    # the letterbox (ADR-1075): 2.39:1 inside any frame
    s.params_({"post/display/letterbox": 2.39, "post/display/letterboxAmount": 1.0})
    s.params_({"camera/lens/focalLength": FOCAL, "post/bloom/intensity": 0.45, "post/bloom/threshold": 1.2,
               "post/bloom/emissionWeight": 0.8, "post/halation/enabled": False,
               "camera/exposure/compensation": -0.6, "post/output/vignette": 0.45, "post/output/grain": 0.03,
               "post/tonemap/operator": 3, "scene/volumeAnisotropy": 0.72, "scene/volumeSteps": 24})
    instrument(s)
    s.region("ring", centre=list(RING_C), radius=RING_R)
    s.region("figure", centre=[FIGURE[0], 0.9, FIGURE[2]], radius=2.0)
    return s
