"""8. ABSTRACT CINEMATIC VOID: THE GATE (04-brief-abstract-direction.md, direction 8; ABSTRACT-PLAN.md section 8).

One colossal object, one horizon, one light. A ring 300 m across, built of 64 dark segments, stands on the horizon of a
mirror plane with a low sun exactly inside it; amber haze; a single tiny figure on the lower third gives the scale;
letterboxed 2.39:1. The camera is a slow, majestic push.

Grammar: almost nothing, at enormous scale. Atmosphere, light and the camera carry the image. The ring is a
`radial` distribution of box segments (its count and gaps are structural instruments). There is no planar reflection
in the engine, so the mirror is built: the ring and the figure have twins below the horizon, and the sky's lower
hemisphere is the plane.

Letterbox: two black unlit bars ride the camera's straight push on the same track (the engine has no output letterbox;
noted for the coordinator).
"""
import math

from .. import kit
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT, VERY_SLOW

ID = "cinematic-void"
TITLE = "Abstract Cinematic Void"

TEAL = "#0e2a33"
TEAL_MID = "#1d4a52"
AMBER = "#ff9a3c"
AMBER_PALE = "#ffcf86"
LIGHT = "#fff4dc"
SIL = "#0a0a0c"

RING_C = (0.0, 128.0, -950.0)
RING_R = 150.0
SEGMENTS = 64
FOCAL = 50.0
PUSH = 120.0                       # seconds for the push
CAM0 = (0.0, 1.8, 70.0)
CAM1 = (0.0, 1.8, 10.0)
FIGURE = (7.5, 0.0, -32.0)

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
        ["bass", "response.bass", "the haze breathes; the sun's glow swells"],
        ["kick", "response.kick", "a pulse of light runs round the ring's seams"],
        ["snare", "response.snare", "dust bursts in the light"],
        ["mids", "audio.mid", "the ring turns slowly"],
        ["sustained", "response.sustain", "the sun brightens"],
        ["intensity", "response.intensity", "the ring's segments part as the piece builds"],
        ["chord", "notes.polyphony", "the segment count"],
        ["note", "notes.lastPitch", "the segment at the pitch's angle lights"],
        ["silence", "(no input)", "the push; the ring stands"],
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
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.6, "attack": 1.2, "release": 1.5}
    elev = math.atan2(RING_C[1] - CAM1[1], -(RING_C[2] - CAM1[2]))
    sun_dir = [0.0, -math.sin(elev), math.cos(elev)]          # travelling toward the camera, slightly down
    s.environment = {
        "intensity": 0.35, "background": hexrgb(TEAL), "fogColor": hexrgb("#c46a2a"), "volumeDensity": 0.0016,
        "volumeMaxDistance": 1400.0, "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": hexrgb(TEAL), "horizonColor": hexrgb(AMBER),
                "groundColor": hexrgb("#3a1e12"), "haze": 0.22, "sunIntensity": 26.0, "sunGlow": 0.06,
                "intensity": 1.0, "background": True, "useKeyLight": True},
    }
    s.light("sun", "directional", direction=sun_dir, color=hexrgb(AMBER_PALE), intensity=6.0, castsShadow=False,
            volumetric=1.0)
    s.light("fill", "directional", direction=[0.2, -0.3, -1.0], color=hexrgb("#4a8a96"), intensity=0.25,
            castsShadow=False)

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
    look = [0.0, (RING_C[1] - CAM1[1]) * 0.35, RING_C[2] - CAM1[2]]
    n = math.sqrt(sum(c * c for c in look))
    look = [c / n for c in look]
    t0 = [CAM0[k] + look[k] * 100.0 for k in range(3)]
    t1 = [CAM1[k] + look[k] * 100.0 for k in range(3)]
    s.track("camera/position", [{"time": 0.0, "value": list(CAM0), "interp": "smooth"},
                                {"time": PUSH, "value": list(CAM1), "interp": "smooth"}], loop=PUSH)
    s.track("camera/target", [{"time": 0.0, "value": t0, "interp": "smooth"},
                              {"time": PUSH, "value": t1, "interp": "smooth"}], loop=PUSH)
    s.camera = {"mode": 1, "position": list(CAM0), "target": t0, "fov": 30.0, "orbitSpeed": 0.0}
    # the bars: 1 m in front of the lens, covering the top and bottom (2.39:1 inside 16:9)
    fy = math.tan(math.atan(12.0 / FOCAL))       # half-height at 1 m
    bar_h = fy * (1.0 - (16.0 / 9.0) / 2.39)     # each bar's height at 1 m
    fwd = look
    right = [1.0, 0.0, 0.0]
    up = [right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2],
          right[0] * fwd[1] - right[1] * fwd[0]]
    up = [-c for c in up] if up[1] < 0 else up
    pitch = math.degrees(math.atan2(fwd[1], -fwd[2]))
    for name, sign in (("barTop", 1.0), ("barBottom", -1.0)):
        off = [fwd[k] * 1.0 + up[k] * sign * (fy - bar_h * 0.5) for k in range(3)]
        keys = [{"time": 0.0, "value": [CAM0[k] + off[k] for k in range(3)], "interp": "smooth"},
                {"time": PUSH, "value": [CAM1[k] + off[k] for k in range(3)], "interp": "smooth"}]
        s.proc(name, {"kind": "box", "size": [4.0, bar_h * 1.02, 0.01], "subdivisions": 1},
               material={"baseColor": [0, 0, 0], "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                         "roughness": 1.0, "metallic": 0.0, "unlit": True},
               transform={"position": keys[0]["value"], "rotation": [pitch, 0.0, 0.0], "scale": [1, 1, 1]})
        s.track("procedural/%s/transform/position" % name, keys, loop=PUSH)

    s.params_({"camera/lens/focalLength": FOCAL, "post/bloom/intensity": 0.45, "post/bloom/threshold": 1.2,
               "post/bloom/emissionWeight": 0.8, "post/halation/enabled": True, "post/halation/intensity": 0.25,
               "post/halation/warmth": 0.8, "post/output/vignette": 0.45, "post/output/grain": 0.03,
               "post/tonemap/operator": 3, "scene/volumeAnisotropy": 0.72, "scene/volumeSteps": 24})
    instrument(s)
    s.region("ring", centre=list(RING_C), radius=RING_R)
    s.region("figure", centre=[FIGURE[0], 0.9, FIGURE[2]], radius=2.0)
    return s
