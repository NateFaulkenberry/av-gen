"""10. TALKING HEADS (docs/prototypes/talking-heads/00-brief.md; plan 03; the owner's notes 05 and 07).

Porcelain mannequin busts, suspended in a dark indigo ether, each SPEAKING ITS OWN CHANNEL like a noise gate (the owner,
stops 1-2): a head listens to one source -- a stem or a live input, found by role -- and when it sounds the head's neck
SNAPS back, its lips part and rays of light shoot from its mouth, as bright as the mouth is open; when it stops the neck
snaps forward, the mouth seals and the rays are gone. Between notes the heads look round the room, each on its own:
mechanical pans, pauses and snaps of the whole bust and of the neck (yaw and pitch), with eyes that dart ahead of each
turn like real eyes (saccades and fixations) and lids that blink. Each head has its own palette, and its secondary
effects -- shake, tear, ghosts, wash, its light on the mist -- are its own source's alone. The room has drifting mist,
motes at three depths and a slowly shifting colour.

One scene, N heads (`HEADS`): each head is one source spec, and everything a head owns -- its nodes, its gate, its
routes, its scan and its eyes -- is named by its prefix. The shipped project has three heads on Feline Footwear's three
stems (drums, bass, synth), found by ROLE (`role.<role>.*`, ADR-1630), so live inputs with roles replace the stems
unchanged. With no stems or inputs every head falls back to the mix's own band through a stricter, rate-limited path.
A single-head project is the same scene with one spec.

The articulated bust (tools/talking_heads/make_bust.py; anchors in meshes/th-bust.anchors.json):
  <p>-bust (body) > <p>-neck (twisted, then bent back or forward)
                  > <p>-pivot (pitch, on the bent neck's top) > <p>-head (yaw; its mouth a Hinge deformer, ADR-1672)
                                                                  > the mouth rays, the cavity, the ghosts
                                                                  > <p>-eyeL / <p>-eyeR (darting), their lids
Every route is same-frame (no macro.*). The rays' brightness and the mouth's angle are ONE route chain apart only in
their amounts (tools/talking_heads/ray_check.py proves they track on every frame); tools/talking_heads/isolation_check.py
proves each head's effects move only with its own source.
"""
import json
import math
import os

from .. import kit
from ..kit import R, M, hexrgb

ID = "talking-heads"
TITLE = "Talking Heads"
HERO_AT = 20.0

_A = json.load(open(os.path.join(kit.ABSTRACT_DIR, "meshes", "th-bust.anchors.json")))
NECK_FOOT = _A["body"]["neckFoot"]
HEAD_REST = _A["body"]["headRest"]
BEND_LEN = _A["neck"]["bendLength"]
HINGE = _A["head"]["mouthHinge"]               # the mouth's Hinge deformer (ADR-1672), head frame
EYE_L = _A["head"]["eyeLeft"]
EYE_R = _A["head"]["eyeRight"]
EYEBALL_L = _A["eyes"]["left"]
EYEBALL_R = _A["eyes"]["right"]
# the rays' origin, head frame: behind the lips, at the middle of the open "oh" (it opens downward from the slit, so
# an origin at the slit's height read as rays leaving the upper lip -- the owner's stop-5 screenshot)
MOUTH = [_A["head"]["mouth"][0], _A["head"]["mouth"][1] - 0.010, _A["head"]["mouth"][2] - 0.004]
LID_UPPER_CLOSED = _A["eyes"]["lidCloseUpperDeg"]  # the upper lid's turn about X to meet the lower (positive)
LID_LOWER_CLOSED = _A["eyes"]["lidCloseLowerDeg"]  # the lower lid's (applied negative)

# The palette (01-research-assets.md section 6).
INK = "#07051a"
VIOLET_DEEP = "#160a2e"
HAZE = "#2a2458"
MAGENTA = "#ff2bd6"
VIOLET = "#8a3bff"
CORE = "#ffe9f7"
BLUE = "#2b7bff"
PINK = "#ff6fb5"
PALE = "#f4f0ff"
IVORY = (0.86, 0.82, 0.74)

# The gate's pose when open: the neck's tilt back (degrees) and the mouth's opening (the Hinge's angle, radians: the
# lower lip and chin turn down about a pivot behind the mouth, the corners stay attached, the cheeks stretch). The head
# is placed on the bent neck's top exactly at this angle (neck_offset), and within a millimetre on the way there.
TILT_OPEN = 24.0
MOUTH_OPEN = 0.28
# The owner, stop 4: "reshape fully open mouth to more of an 'oh' sound / look rather than a half smile". The Hinge's
# purse (ADR-1672) draws the lips in, arches the upper lip and pushes both forward, in proportion to the opening.
MOUTH_PURSE = 2.0
# A step, then a short mechanical settle (the owner approved the snap at stop 2). The rays use the SAME chain.
SNAP = {"attackMs": 0.0, "decayMs": 0.0, "springHz": 11.0, "springDamping": 0.62}

# The busts are SLOTS, one per source role (ADR-1630: a source says what it is -- stems or live inputs, by key and
# role): drums, bass, synth, and a fourth for a vocal or any other source. A slot is shown while its role is active
# (`role.<role>.active`), so the scene has one to four busts, as many as there are assigned sources (fx1,
# 14-brief-secondary-fx.md section 6); with none at all the fourth slot is a hero on the mix. Each slot has:
#   its gate: `band` (the role band its level reads), `open`/`close`, `onsets` (open on the stem's own onsets too);
#   its look: a PALETTE (rays, wash, glow, the copies, its mist) and a PROFILE -- the secondary effects that make
#   a different kind of corrupted transmission (17-brief-signal-corruption.md): tracking, codec, broadcast, ghosting. `profile` is configurable per slot;
#   the defaults follow the role.
#   its scan's timing (rates in Hz and a seed).
# Thresholds were tuned on Feline Footwear's stems. The drums slot reads the drum stem's LOW band (the kick).
ICE = {"rays": ("#2b7bff", "#4fe3ff", "#e6f6ff"), "wash": ("#1b2cff", "#2b7bff", "#4fe3ff", "#c9f4ff"),
       "glow": "#4fe3ff", "ghosts": ("#4fe3ff", "#3a5bff"), "spill": "#3f8cff"}
ORCHID = {"rays": (MAGENTA, PINK, VIOLET), "wash": (VIOLET, MAGENTA, PINK, "#ffb0dc"),
          "glow": MAGENTA, "ghosts": (MAGENTA, VIOLET), "spill": "#ff2bd6"}
EMBER = {"rays": ("#ffb347", "#ff7a3d", "#ffe08a"), "wash": ("#ff4a2b", "#ff9a3d", "#ffd36b", "#fff1cf"),
         "glow": "#ffb347", "ghosts": ("#ff5a2b", "#5a8cff"), "spill": "#ff9a40"}
JADE = {"rays": ("#3dffc0", "#9affe0", "#2bd8ff"), "wash": ("#0f6b5a", "#22c49a", "#7dffd6", "#e0fff4"),
        "glow": "#3dffc0", "ghosts": ("#3dffc0", "#2bd8ff"), "spill": "#3dffc0"}
GREY = {"rays": ("#d8d8d8", "#bcbcbc", "#f0f0f0"), "wash": ("#6a6a6a", "#9a9a9a", "#c8c8c8", "#eeeeee"),
        "glow": "#cfcfcf", "ghosts": ("#d0d0d0", "#a8a8a8"), "spill": "#bdbdbd"}
PROFILES = ("tracking", "codec", "broadcast", "ghosting")
DEFAULT_PROFILE = "tracking"   # a slot whose profile is missing or unknown (the brief: a configurable default)
HEADS = [
    {"prefix": "th1", "roles": ["drums"], "band": "bass", "open": 0.60, "close": 0.45,
     "scan": (0.55, 0.13, 3), "palette": ICE, "profile": "tracking"},
    {"prefix": "th2", "roles": ["bass"], "band": "bass", "open": 0.55, "close": 0.42, "onsets": True,
     "scan": (0.48, 0.11, 7), "palette": ORCHID, "profile": "codec"},
    {"prefix": "th3", "roles": ["synth"], "band": "mid", "open": 0.80, "close": 0.65, "onsets": True, "onsetLevel": 0.8,
     "scan": (0.52, 0.17, 11), "palette": EMBER, "profile": "broadcast"},
    # the fourth: a vocal, else any other source; with NO source at all it is the hero on the mix (its rate-limited
    # mix path, `mix`)
    {"prefix": "th4", "roles": ["vocal", "other"], "band": "mid", "open": 0.70, "close": 0.55, "onsets": True,
     "mix": "audio.mid", "mixOpen": 0.75, "scan": (0.5, 0.15, 13), "palette": JADE, "profile": "ghosting"},
]
# The layout per bust count (15-fx-plan.md): LAYOUT[count][rank] = (x, z, heading, scale), the busts in slot order left
# to right. One: a hero with room round it. Two: an asymmetric pair. Three: the stop-5 stagger. Four: two forward, two
# back and turned in, a little smaller -- none hidden, none a thumbnail.
LAYOUT = {
    1: [(0.0, 0.30, 0.0, 1.22)],
    2: [(-0.40, 0.22, 14.0, 1.08), (0.46, -0.10, -18.0, 1.0)],
    3: [(-0.62, -0.15, 22.0, 1.0), (0.0, 0.25, 0.0, 1.0), (0.62, -0.15, -22.0, 1.0)],
    4: [(-0.76, -0.40, 26.0, 0.92), (-0.25, 0.18, 8.0, 0.95), (0.27, 0.04, -8.0, 0.95), (0.78, -0.44, -26.0, 0.92)],
}


def base_layout(i):
    """Slot i's resting place: its spot in the three-bust layout (the fourth slot: its four-bust spot). Routes move it
    from here to its place for the live count."""
    return LAYOUT[3][i] if i < 3 else LAYOUT[4][3]


ONSET_REFRACTORY_MS = 90.0   # an onset-gated head re-opens at most this often (no 16th-note chatter)
MIX_STRICT = 0.15        # the mix path's thresholds sit this much higher than the stem path's
MIX_PULSE_MS = 200.0     # a mix opening is a fixed pulse ...
MIX_REFRACTORY_MS = 450.0  # ... at most once per this long (07: "on a full mix we can limit the amount of times")

DESIGN = {
    "category": "surreal entity",
    "thesis": "Talking Heads: pale porcelain mannequin busts, each speaking its own channel like a noise gate -- on a "
              "note its neck snaps back, its lips part and rays of light shoot from its mouth; between notes they seal "
              "and look round the room, eyes darting ahead of each turn.",
    "composition": {
        "background": "a near-black indigo to deep violet gradient with a faint blue-purple haze",
        "midground": "three busts well apart in the dark, at different headings, among drifting mist",
        "foreground": "sparse slow motes at three depths",
        "focal": "whichever head is speaking",
        "secondary": ["the mouth rays sweeping the room", "darting eyes and blinks",
                      "each head's own shake, tear, ghosts and wash, in its own palette"],
        "atmosphere": "dark, suspended; drifting mist lit by the speaking heads; no floor, horizon or set",
        "post": "a restrained bloom; no screen-wide glitch (every tear is a head's own)",
        "camera": "a slow, restrained drift; never on the music",
    },
    "palette": {"dominant": INK, "secondary": HAZE, "accent": MAGENTA, "highlight": PALE,
                "background_value": "near black", "saturation": "pale porcelain against saturated emitted light"},
    "motion": {
        "very_slow": ["the camera's drift", "the room's colour", "the mist"],
        "medium": ["the scans and the neck's looks: pans, pauses, snaps"],
        "fast": ["the gates: the neck snaps, the lips part, the rays with the mouth", "saccades", "blinks"],
        "extremely_fast": ["a head's own tear on an opening"],
    },
    "vocabulary": [
        ["drums", "role.drums.bass", "head 1 speaks the kick"],
        ["bass", "role.bass.bass", "head 2 speaks the bass line"],
        ["synth", "role.synth.mid", "head 3 speaks the synth"],
        ["a mix", "response.lowEnv / audio.bass / audio.mid", "with no stems: each head on the mix's band, rate-limited"],
        ["silence", "(no input)", "the heads are shut and scan the room"],
    ],
    "tier": "light: three articulated busts (about 45k triangles each), rays and copies only on a speaking head",
}

INDEX_ENTRY = {
    "category": kit.ABSTRACT_CATEGORY,
    "description": "Play live audio by channel: up to four sources with the roles drums, bass, synth and vocal "
                   "(or any other), one mannequin bust each in engraved silver security foil -- one to four busts, as "
                   "many as there are sources, laid out for the count; a plain mix gives one. Each bust speaks its own "
                   "channel like a noise gate: on a note the neck snaps back, the lips part and rays of light shoot "
                   "from the mouth; between notes they look round the room, eyes darting. The foil's spectral bands "
                   "drift with its source's level, sweep on its onsets and shine while it speaks. Each source is a different corrupted "
                   "transmission: the drums suffer tracking failure (torn bands, static, channels jumping), the bass "
                   "codec collapse (displaced macroblocks that persist), the synth broadcast interference (static, "
                   "scanlines, drifting colour channels), a vocal or other source temporal ghosting. MIDI: CC 1 master, "
                   "CC 2 rays, CC 3 gate sensitivity, CC 4 effects, CC 5 tearing, CC 6 colour, CC 7 gain, CC 8 "
                   "palette, CC 9 head tilt (each rests at its middle and picks up); pad 36 holds every gate shut, 37 "
                   "holds them open, 39 bursts. Opening it turns live input on (View > Live); PageUp/PageDown step "
                   "through the Sonic Abstract set. docs/prototypes/talking-heads/.",
    "live": True,
    "name": kit.ABSTRACT_CATEGORY + " - " + TITLE,
    "project": "sonic-abstract/" + ID + ".json",
}


# ================================================================================================ material programs
class Ops:
    """A program's op list with names, so routes name an op rather than a hand-counted index (1-based paths)."""

    def __init__(self):
        self.ops, self.at = [], {}

    def add(self, label, kind, dst, **f):
        op = {"kind": kind, "dst": dst}
        op.update(f)
        self.ops.append(op)
        self.at[label] = len(self.ops)
        return op


def ellipsoid(o, label, dst, centre, radii, src=0):
    """dst = saturate(1 - sum(((p - c) / r)^2)): 1 at the centre, 0 on the ellipsoid. Four ops."""
    o.add(label + "C", "constant", dst, constant=[-centre[0], -centre[1], -centre[2], 0.0])
    o.add(label + "D", "add", dst, srcA=src, srcB=dst)
    o.add(label + "Q", "multiply", dst, srcA=dst, srcB=dst)
    o.add(label, "gradient", dst, srcA=dst, value=-1.0,
          constant=[1.0 / radii[0] ** 2, 1.0 / radii[1] ** 2, 1.0 / radii[2] ** 2, 1.0])


FAR = [9.0, 9.0, 9.0]
PROGRAM_OPS = {}   # program name -> its Ops (every piece's program has the same op list, so one index names an op)


def bust_program(name, mouth=FAR, eye_l=FAR, eye_r=FAR, wash=ORCHID["wash"], profile="tracking", accent="#ffffff"):
    """Porcelain, darkened sockets and the possession wash (concentrated at the mouth and eyes), then the PROFILE's own
    surface (15-fx-plan.md):
      tracking   hashed horizontal bands of animated static, opened on each hit (with the block tear)
      codec      macroblocks losing their image: flat smeared colour in a routed share of squares
      broadcast  animated static in drifting patches and line-by-line scanline shimmer in the palette's colour
      ghosting   nothing more: the identity is the copies
    Every pattern reads WORLD position, so the pieces of a bust meet without a seam. The masks are points in THIS
    piece's frame (FAR = the piece has none). Registers: r0 local position, r4 emission out, r5 wash colour, r7 base
    colour out; r1-r3, r6 scratch."""
    o = Ops()
    o.add("local", "input", 0, input="localPosition")
    ellipsoid(o, "mouth", 1, mouth, [0.030, 0.016, 0.030])
    ellipsoid(o, "eyeL", 2, [eye_l[0], eye_l[1], eye_l[2] - 0.004], [0.021, 0.012, 0.030])
    ellipsoid(o, "eyeR", 3, [eye_r[0], eye_r[1], eye_r[2] - 0.004], [0.021, 0.012, 0.030])
    o.add("eyes", "add", 2, srcA=2, srcB=3)
    o.add("ivory", "constant", 3, constant=list(IVORY) + [1.0])
    o.add("socketDark", "remap", 5, srcA=2, value=1.0, constant=[0.0, 1.0, 1.0, 0.6])
    o.add("socketMul", "multiply", 3, srcA=3, srcB=5)
    # the wash's colour and the signal bands read WORLD position: every piece of a bust has its own local origin, so a
    # pattern in local space breaks at each piece's edge (the owner, stop 4: "eliminate this seam")
    o.add("world", "input", 4, input="worldPosition")
    o.add("washNoise", "noise", 4, srcA=4, value=7.0, constant=[0.0, 0.0, 0.0, 0.0], seed=5)
    o.add("washSpread", "remap", 4, srcA=4, value=1.0, constant=[0.32, 0.68, 0.0, 1.0])
    o.add("washRamp", "oklabRamp", 5, srcA=4, value=1.0, constant=hexrgb(wash[0]) + [1.0],
          constant2=hexrgb(wash[1]) + [1.0], constant3=hexrgb(wash[2]) + [1.0], constant4=hexrgb(wash[3]) + [1.0])
    o.add("masks", "add", 6, srcA=1, srcB=2)
    o.add("washAmount", "remap", 6, srcA=6, value=1.0, constant=[0.0, 1.0, 0.0, 0.0])
    o.add("washBase", "mixBy", 7, srcA=3, srcB=5, srcC=6)
    o.add("washGlowAmt", "multiply", 4, srcA=5, srcB=6)
    o.add("washGlow", "constant", 6, constant=[0.35, 0.35, 0.35, 0.0])
    o.add("washGlowMul", "multiply", 4, srcA=4, srcB=6)
    profile_tail(o, profile, wash)
    assert len(o.ops) <= 48, (name, len(o.ops))
    PROGRAM_OPS[name] = o
    return {"name": name, "ops": o.ops, "baseColor": 7, "metallic": -1, "roughness": -1, "emission": 4,
            "emissionIntensity": 1.0, "opacity": -1}


# ================================================================================================ the foil (foil1)
# Cursed Slammers POC 4's variant D, the "hybrid security foil" (scene/cursed-slammers, poc4/variants.py; on this bust
# in POC 5), rebuilt for the bust as one program per piece. Its look:
# - engraved relief lines that follow the form (phase from height plus depth), grooves widening away from the key light;
# - a silver base;
# - spectral colour only inside narrow bands whose index runs with the view (fresnel) and the depth, plus a fine
#   grating phase;
# - sparse angle-gated micro-sparkle.
# The music moves the bands (here, not in POC 4).
#
# Fitting it on one head with an fx2 profile's tail (up to 20 ops) inside the 64-op ceiling (ADR-1661):
# - `objectProject` does POC 5's rescale and the piece's offset in one op;
# - an unsaturated `gradient` gives each phase in one op, scaled down and recovered by the palette's frequency;
# - the colour index is carried as t/8 + 0.5;
# - D's diagonal crosshatch in shadow is left out (12 ops).
#
# SEAMLESS: localPosition is the rest mesh's (procedural.wgsl, before the deformers). Each piece adds its rest offset
# in the bust's frame (body 0, neck NECK_FOOT, head HEAD_REST, a lid HEAD_REST + its eyeball), so the pieces form one
# continuous engraving at every joint. It stays glued to the material as the neck bends, the head turns, the jaw drops
# and fx2's blocks displace the surface.
#
# The SPARKLE flicker (POC 4's D shimmered at grazing angles, and its glints read as white dashes), fixed three ways:
# - WHICH cell flares no longer depends on the view. POC 4's flare phase was 7 x hash + 6 x fresnel, and the fresnel
#   varies across every cell on a curved surface, so nearly every cell held a thin stripe where it crossed its peak:
#   a dash, swimming as anything moved. Now the phase is hash + a twinkle phase the MUSIC advances (integrated level
#   and onsets), so a whole cell flashes for a moment as the phase passes it, and nothing moves with the view;
# - the angle only weights the flare's brightness, capped: clamp(fresnel, 0, FOIL_SPARK_CAP), then scaled;
# - the cells are 1.5 times bigger, and a cell fades out as it drops below about four pixels (the `footprint`
#   input). That is where cells crowd together at the silhouette, so near the silhouette the sparkle comes only
#   from cells still big enough on screen.
# Registers: r0 position then the groove mask (kept through the tail), r4 emission, r7 base colour; r1-r3, r5 and r6
# are scratch.
FOIL_UNIT = 0.45          # POC 5's bust rescale: about 100 engraved lines up the bust
FOIL_LINES_M = (95.0, 65.0)         # line phase per METRE of height and of depth: about 55 lines up the bust, 9 px
                                    # apart in the three-bust layout (POC 5's 100 per unit was ~120, under the
                                    # anti-aliasing fade's 6 px in the full scene: the lines vanished)
FOIL_METAL = 0.7                    # the ridges' metallic: all metal reflected only the dark violet room
FOIL_PHASE_DIV = 400.0              # the phase is carried /400 (+0.1) so the gradient never saturates
FOIL_SPARK_CELLS = 100.0            # sparkle cells per unit (POC 4: 150, sub-pixel at the silhouette)
FOIL_SPARK_CAP = 0.45               # the sparkle's angle weight stops growing here (POC 4: up to 1 at grazing)
FOIL_SPARK_EDGE = 0.9997            # cos peak a cell must pass to flare: about 1 % of cells at a time (POC 4's
                                    # 0.998 let ~3 % flare); routed down to ~2.7 % on hits
FOIL_TWINKLE = (0.05, 0.4)          # the twinkle phase's advance: per second at full level, and per onset push
FOIL_BAND = (0.68, 0.92)            # the band mask's smoothstep at rest (routed wider when the gate opens)
FOIL_SILVER = (0.86, 0.86, 0.88)
FOIL_INK = (0.008, 0.008, 0.008)
FOIL_SPECTRUM = dict(constant=[0.62, 0.61, 0.66, 0], constant2=[0.30, 0.30, 0.32, 0],
                     constant3=[8.0, 8.0, 8.0, 0], constant4=[0.0, 0.33, 0.67, 0])
_KEY_TO = [0.55, -0.55, -0.62]
_kn = math.sqrt(sum(c * c for c in _KEY_TO))
FOIL_LDIR = [-c / _kn for c in _KEY_TO]   # toward the key light
FOIL_SHADOW = 0.35


def foil_program(name, offset, profile=None, wash=None):
    """D's foil on one piece whose rest frame sits at `offset` in the bust's frame, with `profile`'s fx2 tail (None:
    a lid, no tail)."""
    u = 1.0 / FOIL_UNIT
    lines_per_m = FOIL_LINES_M[0] + 0.5 * FOIL_LINES_M[1]
    spark_per_m = FOIL_SPARK_CELLS * u
    g = FOIL_SHADOW
    o = Ops()
    o.add("fPos", "objectProject", 0, value=u, constant=[offset[0] * u, offset[1] * u, offset[2] * u, 0.0])
    o.add("fPhase", "gradient", 1, srcA=0, value=1.0,
          constant=[0.0, FOIL_LINES_M[0] * FOIL_UNIT / FOIL_PHASE_DIV, FOIL_LINES_M[1] * FOIL_UNIT / FOIL_PHASE_DIV,
                    0.1])
    o.add("fN", "input", 3, input="normal")
    o.add("fShade", "gradient", 3, srcA=3, value=1.0,
          constant=[-g * FOIL_LDIR[0], -g * FOIL_LDIR[1], -g * FOIL_LDIR[2], g])
    o.add("fLine", "palette", 2, srcA=1, value=0.0, constant=[0.5, 0.5, 0.5, 0], constant2=[0.5, 0.5, 0.5, 0],
          constant3=[FOIL_PHASE_DIV] * 3 + [0], constant4=[0, 0, 0, 0])
    o.add("fWiden", "add", 2, srcA=2, srcB=3)
    o.add("fGroove", "smoothstep", 2, srcA=2, constant=[0.72, 0.9, 0.0, 0.0])
    o.add("fFoot", "input", 5, input="footprint")
    o.add("fAA", "gradient", 3, srcA=5, value=1.0, constant=[-3.0 * lines_per_m, 0.0, 0.0, 1.5])
    o.add("fGrooveAA", "multiply", 2, srcA=2, srcB=3)                       # r2 = groove mask g
    o.add("fSparkAA", "gradient", 5, srcA=5, value=1.0, constant=[-4.0 * spark_per_m, 0.0, 0.0, 2.0])
    o.add("fFres", "fresnel", 3, value=1.0)
    o.add("fCell", "quantize", 1, srcA=0, value=FOIL_SPARK_CELLS, constant=[0.0, 0.0, 0.0, 0.0], seed=11)
    o.add("fCellR", "swizzle", 1, srcA=1, constant=[3.0, 3.0, 3.0, 3.0])
    o.add("fTwinkle", "constant", 6, constant=[0.0, 0.0, 0.0, 0.0])        # routed: the music's twinkle phase
    o.add("fCellA", "add", 1, srcA=1, srcB=6)
    o.add("fFlare", "palette", 1, srcA=1, value=0.0, constant=[0.5, 0.5, 0.5, 0], constant2=[0.5, 0.5, 0.5, 0],
          constant3=[7.0, 7.0, 7.0, 0], constant4=[0, 0, 0, 0])
    o.add("fSpark", "smoothstep", 1, srcA=1, constant=[FOIL_SPARK_EDGE, 1.0, 0.0, 0.0])   # routed down on hits
    o.add("fCap", "remap", 6, srcA=3, value=1.0, constant=[0.0, FOIL_SPARK_CAP, 0.0, 1.6])  # capped angle weight
    o.add("fSparkW", "multiply", 1, srcA=1, srcB=6)
    o.add("fSparkE", "multiply", 4, srcA=1, srcB=5)                         # r4 = sparkle (emission starts here)
    # the colour index u = t/8 + 0.5, t = 2.4 fresnel + 1.2 z' + a fine grating + the music's drift
    o.add("fDepth", "gradient", 1, srcA=0, value=1.0, constant=[0.0, 0.0, 1.2 / 8.0, 0.5])
    o.add("fView", "remap", 6, srcA=3, value=0.0, constant=[0.0, 1.0, 0.0, 2.4 / 8.0])
    o.add("fIdx", "add", 1, srcA=1, srcB=6)
    o.add("fGrate", "palette", 6, srcA=0, value=0.0, constant=[0, 0, 0, 0], constant2=[0.0075, 0.0075, 0.0075, 0],
          constant3=[320.0, 320.0, 320.0, 0], constant4=[0, 0, 0, 0])
    o.add("fIdxG", "add", 1, srcA=1, srcB=6)
    o.add("fDrift", "constant", 6, constant=[0.0, 0.0, 0.0, 0.0])           # routed: the music's drift and sweeps
    o.add("fIdxM", "add", 1, srcA=1, srcB=6)
    o.add("fBandC", "palette", 6, srcA=1, value=0.0, constant=[0.5, 0.5, 0.5, 0], constant2=[0.5, 0.5, 0.5, 0],
          constant3=[4.8, 4.8, 4.8, 0], constant4=[0, 0, 0, 0])
    o.add("fBand", "smoothstep", 6, srcA=6, constant=[FOIL_BAND[0], FOIL_BAND[1], 0.0, 0.0])  # routed wider
    o.add("fSpec", "palette", 5, srcA=1, value=0.0, **FOIL_SPECTRUM)
    o.add("fSilver", "constant", 7, constant=list(FOIL_SILVER) + [1.0])
    o.add("fFoilC", "mixBy", 7, srcA=7, srcB=5, srcC=6)
    o.add("fBandCol", "multiply", 5, srcA=5, srcB=6)
    o.add("fGlow", "constant", 6, constant=[FOIL_GLOW[0]] * 3 + [0.0])      # routed: the bands' light with the gate
    o.add("fBandL", "multiply", 5, srcA=5, srcB=6)
    o.add("fEmit", "add", 4, srcA=4, srcB=5)
    o.add("fInk", "constant", 5, constant=list(FOIL_INK) + [1.0])
    o.add("fInked", "mixBy", 7, srcA=7, srcB=5, srcC=2)
    o.add("fKeep", "swizzle", 0, srcA=2, constant=[0.0, 0.0, 0.0, 0.0])    # r0 = g, kept through the tail
    if profile is not None:
        profile_tail(o, profile, wash)
    o.add("fMetal", "remap", 1, srcA=0, value=1.0, constant=[0.0, 1.0, FOIL_METAL, 0.0])
    o.add("fRough", "remap", 2, srcA=0, value=1.0, constant=[0.0, 1.0, 0.34, 0.85])
    o.add("fRoughAA", "roughnessFilter", 2, srcA=2, value=1.0)
    o.add("fOcc", "remap", 3, srcA=0, value=1.0, constant=[0.0, 1.0, 1.0, 0.08])
    assert len(o.ops) <= 64, (name, len(o.ops))
    PROGRAM_OPS[name] = o
    return {"name": name, "ops": o.ops, "baseColor": 7, "metallic": 1, "roughness": 2, "emission": 4,
            "emissionIntensity": 1.0, "opacity": -1, "occlusion": 3}


def foil_pieces(P):
    """The pieces drawn in foil and their programs: (program, offset in the bust frame, takes the profile's tail)."""
    return [(P + "-body", [0.0, 0.0, 0.0], True), (P + "-neck", list(NECK_FOOT), True),
            (P + "-head", list(HEAD_REST), True)]


def bust_progs(P):
    """The programs carrying the profile's tail and the per-head looks (wash or foil bands) on head P."""
    return ([P + "-body", P + "-neck", P + "-head"] if FOIL else [P + "-body", P + "-head"])


def lid_progs(P):
    """One foil program for all four lids of a head: the line phase reads only height and depth, and the two eyes
    differ only in x, so one offset (the left eyeball's) serves both (a 33rd program would not be uploaded)."""
    return [P + "-lid"] if FOIL else []


FOIL = True   # the bust's material: D's foil (foil1) or fx2's porcelain (build(foil=False))


def profile_tail(o, profile, wash):
    """The fx2 profile's own surface, appended to a bust program (porcelain or foil). Reads nothing the base left but
    r4 (emission, added to) and r7 (base colour, mixed into); r1, r2, r3 and r6 are its scratch. r0 and r5 are left
    alone (the foil keeps its groove mask in r0)."""
    if profile == "tracking":
        # TRACKING FAILURE's static: hashed horizontal bands (a routed threshold opens them on each hit) filled with
        # animated monochrome noise -- the signal dropping out in strips, not lines drawn over the head
        o.add("bandTime", "input", 1, input="time")
        o.add("bandRate", "constant", 2, constant=[0.0, 0.0, 14.0, 0.0])
        o.add("bandEpoch", "multiply", 1, srcA=1, srcB=2)
        o.add("bandWorld", "input", 3, input="worldPosition")
        o.add("bandY", "constant", 2, constant=[0.0, 1.0, 0.0, 0.0])
        o.add("bandYMul", "multiply", 2, srcA=3, srcB=2)
        o.add("bandCoord", "add", 1, srcA=1, srcB=2)
        o.add("bandCell", "quantize", 1, srcA=1, value=70.0, constant=[0.0, 0.0, 0.0, 0.0], seed=9)
        o.add("bandHash", "swizzle", 1, srcA=1, constant=[3.0, 3.0, 3.0, 3.0])
        o.add("bands", "threshold", 1, srcA=1, value=1.01)  # 1.01 = no band; routes lower it
        o.add("staticT", "input", 2, input="time")
        o.add("staticV", "constant", 6, constant=[17.0, 23.0, 41.0, 0.0])
        o.add("staticTm", "multiply", 2, srcA=2, srcB=6)
        o.add("staticP", "add", 2, srcA=3, srcB=2)
        o.add("staticN", "noise", 2, srcA=2, value=900.0, constant=[0.0, 0.0, 0.0, 0.0], seed=19)
        o.add("bandStatic", "multiply", 1, srcA=1, srcB=2)
        o.add("bandGain", "constant", 2, constant=[2.4, 2.4, 2.4, 0.0])
        o.add("bandGainMul", "multiply", 1, srcA=1, srcB=2)
        o.add("bandAdd", "add", 4, srcA=4, srcB=1)
    elif profile == "codec":
        # CODEC COLLAPSE's macroblocks: the surface quantised into squares; a routed share of them (rand >= threshold)
        # lose their image -- flat colour from a per-block ramp, streaked by per-row hashes (a smear), faintly lit
        o.add("cW", "input", 1, input="worldPosition")
        o.add("cQ", "quantize", 2, srcA=1, value=CODEC_CELLS, constant=[0.0, 0.0, 0.0, 0.0], seed=27)
        o.add("cRand", "swizzle", 3, srcA=2, constant=[3.0, 3.0, 3.0, 3.0])
        o.add("cMask", "threshold", 3, srcA=3, value=1.01)  # routed: 1 - the share of corrupt blocks
        o.add("cN", "noise", 6, srcA=2, value=3.1, constant=[0.0, 0.0, 0.0, 0.0], seed=29)
        o.add("cCol", "oklabRamp", 6, srcA=6, value=1.0, constant=hexrgb(wash[0]) + [1.0],
              constant2=hexrgb(wash[1]) + [1.0], constant3=hexrgb(wash[2]) + [1.0], constant4=hexrgb(wash[3]) + [1.0])
        o.add("cRowY", "constant", 2, constant=[0.0, 1.0, 0.0, 0.0])
        o.add("cRowP", "multiply", 1, srcA=1, srcB=2)
        o.add("cRow", "quantize", 1, srcA=1, value=160.0, constant=[0.0, 0.0, 0.0, 0.0], seed=31)
        o.add("cRowR", "swizzle", 1, srcA=1, constant=[3.0, 3.0, 3.0, 3.0])
        o.add("cStreak", "remap", 1, srcA=1, value=1.0, constant=[0.0, 1.0, 0.55, 1.3])
        o.add("cBlock", "multiply", 6, srcA=6, srcB=1)
        o.add("cMix", "mixBy", 7, srcA=7, srcB=6, srcC=3)
        o.add("cGlow", "constant", 1, constant=[0.25, 0.25, 0.25, 0.0])
        o.add("cGlowM", "multiply", 6, srcA=6, srcB=1)
        o.add("cGlowMask", "multiply", 6, srcA=6, srcB=3)
        o.add("cAdd", "add", 4, srcA=4, srcB=6)
    elif profile == "broadcast":
        # BROADCAST INTERFERENCE: animated static in slowly drifting patches (their threshold routed: density), and
        # scanlines that flicker line by line (routed), over the porcelain; the colour separation is the rim copies'
        o.add("bW", "input", 1, input="worldPosition")
        # the patches drift with the held level: the noise's offset (k) is integrated from it by routes, not a time op
        o.add("pN", "noise", 3, srcA=1, value=4.0, constant=[0.0, 0.0, 0.0, 0.0], seed=41)
        o.add("pSpread", "remap", 3, srcA=3, value=1.0, constant=[0.36, 0.64, 0.0, 1.0])   # fbm sits near 0.5
        o.add("slArea", "smoothstep", 6, srcA=3, constant=[PATCH_OFF, PATCH_OFF + 0.3, 0.0, 0.0])  # routed down
        o.add("patch", "smoothstep", 3, srcA=3, constant=[PATCH_OFF, PATCH_OFF + 0.06, 0.0, 0.0])  # routed down
        # the static: fine noise whose offset runs on (integrated by routes from the level and the bursts, far more
        # than a grain a frame), so it boils wherever a patch shows
        o.add("bN", "noise", 2, srcA=1, value=1100.0, constant=[0.0, 0.0, 0.0, 0.0], seed=37)
        o.add("bOver", "mixBy", 7, srcA=7, srcB=2, srcC=3)   # the static OBSCURES the surface inside a patch ...
        o.add("bStatic", "multiply", 2, srcA=2, srcB=3)
        o.add("bGain", "constant", 3, constant=[0.7, 0.7, 0.7, 0.0])
        o.add("bLit", "multiply", 2, srcA=2, srcB=3)
        o.add("bAdd", "add", 4, srcA=4, srcB=2)              # ... and glows a little
        o.add("slY", "constant", 2, constant=[0.0, 1.0, 0.0, 0.0])
        o.add("slP", "multiply", 1, srcA=1, srcB=2)
        o.add("sl", "quantize", 1, srcA=1, value=320.0, constant=[0.0, 0.0, 0.0, 0.0], seed=43)
        o.add("slR", "swizzle", 1, srcA=1, constant=[3.0, 3.0, 3.0, 3.0])
        o.add("slOn", "threshold", 1, srcA=1, value=1.01)    # routed: lines lit where the hash passes
        o.add("slCol", "constant", 2, constant=[0.0, 0.0, 0.0, 0.0])    # routed: the lines' colour x gain
        o.add("slLit", "multiply", 1, srcA=1, srcB=2)
        o.add("slIn", "multiply", 1, srcA=1, srcB=6)         # only round the patches (never stripes over the whole bust)
        o.add("slAdd", "add", 4, srcA=4, srcB=1)


CODEC_CELLS = 14.0  # the codec's macroblocks per metre (about 7 cm squares)
PATCH_OFF = 1.1     # the static patches' threshold at rest: above the remapped noise (no patch); routes lower it


def ghost_program(name, colour, floor=0.0):
    """A copy of the head (fx2). `floor` 0: RIM ONLY -- the opacity is the fresnel itself (power 4), so the copy is
    nothing face-on and a coloured edge at its silhouette (a colour channel out of register). `floor` > 0: a VHS ghost --
    a faint whole copy (that much opacity face-on) with the same coloured rim. The body is the copy's own colour, never
    black (a dark copy read as a drop shadow). The copy fades out round its mouth: it is the head at rest, its lips
    closed, and the slit's grazing faces caught the rim light as a bright dash beside the real, open mouth. The
    opacity's gain (constant op 4) is routed from zero."""
    o = Ops()
    o.add("fres", "fresnel", 0, value=4.0)
    o.add("col", "constant", 1, constant=hexrgb(colour, 2.2) + [1.0])
    o.add("rim", "multiply", 2, srcA=0, srcB=1)
    o.add("gain", "constant", 3, constant=[0.0, 0.0, 0.0, 0.0])
    assert o.at["gain"] == 4   # the routes name op 4
    o.add("floor", "remap", 6, srcA=0, value=1.0, constant=[0.0, 1.0, floor, 1.0])
    o.add("opac", "multiply", 3, srcA=3, srcB=6)
    o.add("local", "input", 4, input="localPosition")
    ellipsoid(o, "mouth", 6, MOUTH, [0.045, 0.026, 0.06], src=4)
    o.add("noMouth", "remap", 6, srcA=6, value=1.0, constant=[0.0, 0.35, 1.0, 0.0])
    o.add("opacM", "multiply", 3, srcA=3, srcB=6)
    o.add("lit", "multiply", 2, srcA=2, srcB=3)
    o.add("base", "constant", 5, constant=hexrgb(colour, 0.6) + [1.0])
    return {"name": name, "ops": o.ops, "baseColor": 5, "metallic": -1, "roughness": -1, "emission": 2,
            "emissionIntensity": 1.0, "opacity": 3}


# ================================================================================================ the mouth rays
# Rays only (the owner, D5 at stop 2: "try rays only and make them fade as fast as the mouth animation closes").
# Jaw frame: the face looks down +Z; uneven angles, lengths and weights so they never read as debug lines. Short
# enough that the outer heads' rays stay in the frame.
MOUTH_RAYS = [
    {"yaw": -16.0, "pitch": 14.0, "angle": 1.6, "length": 1.55, "aperture": 0.003},
    {"yaw": 6.0, "pitch": -8.0, "angle": 1.0, "length": 1.85, "aperture": 0.003},
    {"yaw": 30.0, "pitch": 20.0, "angle": 2.1, "length": 1.35, "aperture": 0.004},
    {"yaw": 38.0, "pitch": -4.0, "angle": 1.2, "length": 1.70, "aperture": 0.003},
    {"yaw": 18.0, "pitch": 30.0, "angle": 0.8, "length": 1.45, "aperture": 0.003},
    {"yaw": -2.0, "pitch": 7.0, "angle": 1.4, "length": 1.95, "aperture": 0.003},
]
RAY_WEIGHT = [0.55, 0.35, 0.8, 0.45, 0.3, 0.65]
RAY_BRIGHTNESS = 4.2     # a ray's intensity at a fully open mouth, times its weight (foil1, the owner: "strengthen
                         # brightness of light beams from mouth"; it was 2.6)


def neck_offset(deg):
    """Where the bent neck carries the head's origin at a tilt back of `deg` (applyBend: R = F / theta), in the
    neck's frame: (dy, dz) with +Z the face's way. Exact at the open pose; the routes scale it linearly."""
    th = math.radians(deg)
    if th < 1e-9:
        return 0.0, 0.0
    R_ = BEND_LEN / th
    return R_ * math.sin(th) - BEND_LEN, -R_ * (1.0 - math.cos(th))


# ================================================================================================ the scene
def build(heads=None, neutral=False, foil=True):
    """`heads`: the slots (default HEADS, all four). `neutral`: every slot's palette grey -- the brief's test that a
    source is recognisable from its secondary effects alone (with the rays knob at 0)."""
    global FOIL
    FOIL = foil   # the bust's material for this build (module state: head() and routes() read it)
    heads = [dict(h) for h in (heads or HEADS)]
    for h in heads:
        if h.get("profile") not in PROFILES:
            h["profile"] = DEFAULT_PROFILE
    if neutral:
        for h in heads:
            h["palette"] = GREY
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.5, "attack": 1.0, "release": 1.0}
    s.environment = {
        "intensity": 0.04, "background": hexrgb(INK), "fogColor": hexrgb(HAZE, 0.25 * SKY_DIM),
        "volumeDensity": 0.0, "volumeMaxDistance": 0.0, "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": hexrgb(VIOLET_DEEP, 0.5 * SKY_DIM),
                "horizonColor": hexrgb(VIOLET_DEEP, 0.32 * SKY_DIM), "groundColor": hexrgb(VIOLET_DEEP, 0.3 * SKY_DIM),
                "haze": 0.25, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    s.light("key", "directional", direction=[0.55, -0.55, -0.62], color=hexrgb("#ffe2c0"), intensity=3.4,
            castsShadow=False, contactShadow=False)
    s.light("rim", "directional", direction=[-0.45, -0.25, 0.85], color=hexrgb("#8a6cff"), intensity=2.2,
            castsShadow=False, contactShadow=False)
    s.light("fill", "directional", direction=[-0.6, 0.2, -0.75], color=hexrgb("#5a50c8"), intensity=0.25,
            castsShadow=False, contactShadow=False)
    # bloom from 1.2 (was 0.9): the brighter rays bloom; the foil's specular ridges, bands and glints stay crisp
    s.params_({"camera/lens/focalLength": 50.0, "post/bloom/intensity": 0.42, "post/bloom/threshold": 1.2,
               "post/output/vignette": 0.45, "post/output/grain": 0.012, "temporal/echo/enabled": False})
    s.camera["fov"] = 40.0
    # the camera frames every layout (one to four busts, LAYOUT); the busts move, it does not
    s.drift_camera(centre=[-0.28, 0.46, 2.2], target=[0.0, 0.33, -0.05], period=90.0, amp=(0.05, 0.015, 0.03),
                   tamp=(0.008, 0.005, 0.0), steps=8)
    control(s)
    atmosphere(s)
    background(s)
    layout_signals(s, heads)
    for i, spec in enumerate(heads):
        head(s, spec, i)
    layout_routes(s, heads)
    return s


# ================================================================================================ the atmosphere
# The owner, stop 4: "add more atmosphere to the talking heads background too, mist, particles, ambient tones - it
# should be dynamic too". All of it is the room's, not a head's: it drifts on its own clocks, swells a little with the
# whole mix's energy, and takes each head's colour only through that head's own light (`<p>-spill`, in head()).
MIST = "#3a2f7a"
MIST_DEEP = "#1a1240"


def atmosphere(s):
    # the mist: large, soft, slow sprites (depth-faded where they cross a bust), a few dozen of them drifting through
    # and behind the heads -- banks of haze at the cost of a particle draw. (The environment's marched air looked
    # right and cost 13-16 ms a frame at 1080p; a placed fog bank drew nothing at this scale. See 09-stop4.md.)
    s.particles("mist", capacity=70, seed=53, shape="box", position=[0.0, 0.45, -0.7], extent=[3.4, 0.9, 1.6],
                direction=[1.0, 0.1, 0.0], spawnRate=3.0, lifetimeMin=18.0, lifetimeMax=30.0, spread=0.6,
                speedMin=0.01, speedMax=0.035, gravity=[0, 0.0, 0], drag=0.05, turbulence=0.01,
                turbulenceScale=0.3, sizeStart=0.55, sizeEnd=0.95, colorStart=hexrgb(MIST) + [0.16],
                colorEnd=hexrgb(MIST_DEEP) + [0.0], emissive=1.3 * MIST_DIM, blend="additive", softness=0.25)
    # the motes, at three depths: far (fine and many, behind the heads), mid (among them), near (few, larger, slow,
    # between the camera and the heads); each its own clock
    for name, cap, seed, pos, ext, rate, size, alpha, emis in (
            ("motes", 500, 17, [0.0, 0.45, 0.0], [3.6, 1.6, 2.4], 8.0, 0.0035, 0.6, 1.4),
            ("motesNear", 90, 43, [0.0, 0.5, 1.7], [2.6, 1.4, 1.0], 1.6, 0.0065, 0.35, 1.2)):
        s.particles(name, capacity=cap, seed=seed, shape="box", position=pos, extent=ext,
                    direction=[0.0, 1.0, 0.0], spawnRate=rate, lifetimeMin=12.0, lifetimeMax=22.0, spread=1.0,
                    speedMin=0.004, speedMax=0.018, gravity=[0, 0.002, 0], drag=0.3, turbulence=0.025,
                    turbulenceScale=0.5, sizeStart=size, sizeEnd=size * 0.6, colorStart=hexrgb(PALE) + [alpha],
                    colorEnd=hexrgb(VIOLET) + [0.0], emissive=emis, blend="additive")
    # the room's tone: slow colour gradients through the sky (zenith and horizon on their own smooth noises,
    # minutes long; visual, not audio)
    for name, rate, seed in (("toneA", 0.021, 71), ("toneB", 0.033, 73), ("toneC", 0.027, 79)):
        s.sources.append({"kind": "noise", "name": name, "settings": {"seed": seed}})
        s.params_({"sources/%s/rate" % name: rate, "sources/%s/smoothness" % name: 1.0})
    s.route(R("noise.toneC", "particles/mist/emissive", 0.35 * MIST_DIM, polarity="bipolar"),
            R("noise.toneA", "env/sky/zenithColor", 0.035 * SKY_DIM, comp=2, polarity="bipolar"),
            R("noise.toneA", "env/sky/zenithColor", -0.015 * SKY_DIM, comp=0, polarity="bipolar"),
            R("noise.toneB", "env/sky/horizonColor", 0.03 * SKY_DIM, comp=0, polarity="bipolar"),
            R("noise.toneB", "env/sky/horizonColor", 0.012 * SKY_DIM, comp=1, polarity="bipolar"),
            R("noise.toneC", "env/sky/horizonColor", 0.03 * SKY_DIM, comp=2, polarity="bipolar"),
            # the ground moves with the horizon, so no horizon line ever shows (the room has none)
            R("noise.toneB", "env/sky/groundColor", 0.03 * SKY_DIM, comp=0, polarity="bipolar"),
            R("noise.toneB", "env/sky/groundColor", 0.012 * SKY_DIM, comp=1, polarity="bipolar"),
            R("noise.toneC", "env/sky/groundColor", 0.03 * SKY_DIM, comp=2, polarity="bipolar"),
            )
    # ... and the whole mix's energy swells the mist and the bloom a little (seconds, not beats)
    s.route(R("response.intensity", "particles/mist/emissive", 0.8 * MIST_DIM, attackMs=1500.0, decayMs=4000.0),
            R("response.intensity", "post/bloom/intensity", 0.08, attackMs=1500.0, decayMs=4000.0))


def copy_names(spec):
    """The copies a profile has (head(), routes()): tracking, broadcast and ghosting have two; codec none."""
    return ("A", "B") if spec.get("profile", "tracking") in ("tracking", "broadcast", "ghosting") else ()


def head(s, spec, slot):
    P = spec["prefix"]
    pal = spec.get("palette", ORCHID)
    prof = spec.get("profile", "tracking")
    bx, bz, bh, bs = base_layout(slot)
    # the profile's world-space block corruption (ADR-1674), applied with identical values to every piece of the bust
    # and to the face's pieces (one cell grid, so no join steps and nothing on the face detaches):
    #   tracking -- flat full-width bands, displaced sideways on each hit (the tracking tear);
    #   codec    -- macroblocks, a share displaced, persisting, reshuffled on strong onsets (the codec collapse).
    # Each has a still band at the mouth (`speed`): the rays ride the head's node, which no deformer moves, so the
    # mouth barely moves while the forehead and chest break up. Its centre follows the bust's place (layout_routes).
    still = {"speed": STILL_BAND, "center": [bx, MOUTH_HEIGHT * bs, bz], "axis": [0.0, 1.0, 0.0], "space": "world"}
    tracking = dict(still, kind="blocks", amount=0.0, frequency=42.0, scale=60.0, falloff=0.0, phase=0.0,
                    displacementAxis=[1.0, 0.0, 0.0], seed=slot + 1)
    codec = dict(still, kind="blocks", amount=0.0, frequency=9.0, scale=0.85, falloff=0.0, phase=0.0,
                 displacementAxis=[1.0, 0.0, 0.3], seed=slot + 11)
    surface = {"tracking": [tracking], "codec": [codec]}.get(prof, [])
    # The neck: a TWIST (the swivel's yaw, about the straight neck), then two one-sided BENDS -- back (the gate's snap and
    # looking up) and forward (looking down). One side at a time, each cut to exactly 0 at rest: a Bend at a curvature
    # near 0 loses its normals to the GPU sine's precision, and a bipolar pitch would pass through it.
    twist = {"kind": "twist", "amount": 0.0, "axis": [0.0, 1.0, 0.0], "center": [0.0, 0.0, 0.0], "speed": 0.0,
             "space": "local"}
    bend_back = {"kind": "bend", "amount": 0.0, "axis": [0.0, 1.0, 0.0], "displacementAxis": [0.0, 0.0, -1.0],
                 "center": [0.0, 0.0, 0.0], "falloff": BEND_LEN, "space": "local"}
    bend_fwd = dict(bend_back, displacementAxis=[0.0, 0.0, 1.0])
    hinge = {"kind": "hinge", "amount": 0.0, "space": "local", "axis": HINGE["axis"], "center": HINGE["center"],
             "displacementAxis": HINGE["displacementAxis"], "phase": HINGE["phase"], "falloff": HINGE["falloff"],
             "frequency": HINGE["frequency"], "speed": HINGE["speed"], "scale": HINGE["scale"],
             "tension": MOUTH_PURSE}
    # The head hangs from a PIVOT at the neck's top: the pivot takes the pitch (and its place on the bent neck), the
    # head the yaw, so the head turns Rx(pitch) Ry(yaw) -- the order the neck's twist-then-bend turns its top ring.
    s.nodes.append({"name": P + "-pivot", "kind": "group", "position": [float(v) for v in HEAD_REST],
                    "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0], "parent": P + "-bust"})
    pieces = [  # node, mesh, program (its masks in its own frame), parent, position, rotation, deformers
        (P + "-bust", "th-body", P + "-body", None, [bx, 0.0, bz], [0.0, bh, 0.0], surface),
        (P + "-neck", "th-neck", P + "-neck" if FOIL else P + "-body", P + "-bust", NECK_FOOT, [0.0, 0.0, 0.0],
         [twist, bend_back, bend_fwd] + surface),
        (P + "-head", "th-head", P + "-head", P + "-pivot", [0.0, 0.0, 0.0], [0.0, 0.0, 0.0], [hinge] + surface),
    ]
    accent = pal["glow"]
    if FOIL:
        for prog, off, _ in foil_pieces(P):
            s.program(foil_program(prog, off, prof, pal["wash"]))
        s.program(foil_program(P + "-lid", [HEAD_REST[k] + EYEBALL_L[k] for k in range(3)]))
    else:
        s.program(bust_program(P + "-body", wash=pal["wash"], profile=prof, accent=accent))
        s.program(bust_program(P + "-head", mouth=MOUTH, eye_l=EYE_L, eye_r=EYE_R, wash=pal["wash"], profile=prof,
                               accent=accent))
    for node, mesh, prog, parent, pos, rot, deformers in pieces:
        s.proc(node, {"kind": "mesh", "asset": "meshes/%s.glb" % mesh},
               material={"baseColor": list(IVORY), "roughness": 0.36, "metallic": 0.0, "program": prog},
               deformers=[dict(d) for d in deformers], position=pos, rotation=rot,
               scale=[bs] * 3 if node == P + "-bust" else [1.0, 1.0, 1.0], parent=parent, extra={"castsShadow": False})
        s.entity(node)
    # the bust's pivot node goes before its children in the list (it was appended before the pieces, but its parent,
    # the bust, is a piece): move it to just after the bust
    piv = next(n for n in s.nodes if n["name"] == P + "-pivot")
    s.nodes.remove(piv)
    s.nodes.insert(next(i for i, n in enumerate(s.nodes) if n["name"] == P + "-bust") + 1, piv)
    # the mouth's dark interior (seen only through the parted lips), the eyes and their lids: procedural pieces with the
    # head's own surface deformers, the same values and routes (face_pieces), so whatever moves the face carries them
    # with it (fx1, the owner: "the eyes look like they are not apart of the head", "the mouth doesnt bend with it")
    for side, at in (("L", EYEBALL_L), ("R", EYEBALL_R)):
        # the eye's darting turn is its group's; the white and the iris ride it
        s.nodes.append({"name": "%s-eye%s" % (P, side), "kind": "group", "position": [float(v) for v in at],
                        "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0], "parent": P + "-head"})
    for name, mesh, parent, pos, mat in face_pieces(P):
        mat = dict(mat)
        if FOIL and "-lid" in name:
            mat["program"] = P + "-lid"
        s.proc(name, {"kind": "mesh", "asset": "meshes/%s.glb" % mesh}, material=mat,
               deformers=[dict(d) for d in surface], position=pos, rotation=[0.0, 0.0, 0.0], scale=[1.0, 1.0, 1.0],
               parent=parent, extra={"castsShadow": False})
    # the copies: images of the head that the profile pulls out of register (gltf, alpha-blended). They are whole
    # frames of the head at rest -- they do not open their mouths or break into blocks (a procedural piece ignores a
    # program's opacity and drew them solid) -- which is what a lingering or misregistered frame is.
    #   tracking, broadcast -- two RIM copies (no body: nothing face-on), red and cyan or red and blue: the colour
    #     channels separating at the silhouette -- a flash on a hit, a slow drift with a held note
    #   ghosting -- two VHS ghosts: faint whole copies (an opacity floor) with a coloured rim, offset and fading
    copies = {"tracking": [("A", "#ff2a3a", 0.0), ("B", "#2affe8", 0.0)],
              "broadcast": [("A", "#ff3060", 0.0), ("B", "#3a6bff", 0.0)],
              "ghosting": [("A", pal["ghosts"][0], 0.5), ("B", pal["ghosts"][1], 0.34)]}.get(prof, [])
    for cp, colour, floor in copies:
        s.program(ghost_program("%s-ghost%s" % (P, cp), colour, floor))
        s.nodes.append({"name": "%s-ghost%sh" % (P, cp), "kind": "gltf", "asset": "meshes/th-head-ghost.glb",
                        "position": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [0.001] * 3,
                        "parent": P + "-head", "castsShadow": False, "program": "%s-ghost%s" % (P, cp)})
    cols = pal["rays"]
    rays = s.emitter_fan(P + "-mouth", P + "-head", MOUTH,
                         [dict(r, color=hexrgb(cols[i % len(cols)])) for i, r in enumerate(MOUTH_RAYS)])
    if prof == "tracking":
        s.effect(P + "-shake", "shake", ("entity", P + "-bust"),
                 parameters={"trauma": 0.0, "amplitude": 0.004, "rotation": 1.0, "frequency": 17.0, "decay": 0.0,
                             "seed": 7.0})
    # the glow on EVERY piece, with identical values and routes: a glow on the head alone stopped in a line at the neck
    for part, piece in (("H", "head"), ("N", "neck"), ("B", "bust")):
        s.effect("%s-glow%s" % (P, part), "glow", ("entity", "%s-%s" % (P, piece)),
                 parameters={"gain": 1.0, "tint": hexrgb(pal["glow"]), "glow": 0.0, "rim": 0.05,
                             "rimColor": hexrgb("#7a6cff"), "rimPower": 3.5, "spill": False, "recolour": 0.0})
    # the halo: the mist behind this head, in its own colour, dark until its gate opens (its place follows the layout)
    hd = math.radians(bh)
    s.particles(P + "-mist", capacity=36, seed=61 + int(P[2:]), shape="box",
                position=[bx - 0.3 * math.sin(hd), 0.45, bz - 0.35], extent=[0.5, 0.35, 0.2],
                direction=[0.0, 1.0, 0.0], spawnRate=1.8, lifetimeMin=14.0, lifetimeMax=22.0, spread=1.0,
                speedMin=0.005, speedMax=0.02, gravity=[0, 0.0, 0], drag=0.1, turbulence=0.01,
                turbulenceScale=0.3, sizeStart=0.45, sizeEnd=0.8, colorStart=hexrgb(pal["spill"]) + [0.07],
                colorEnd=hexrgb(pal["spill"]) + [0.0], emissive=0.0, blend="additive", softness=0.25)
    gate(s, spec)
    scan_and_eyes(s, spec)
    blinks(s, spec)
    routes(s, spec, rays)


STILL_BAND = 0.07   # the block corruption ramps from nothing at the mouth to full this far above and below it
MOUTH_HEIGHT = 0.31 # the mouth's height in the bust's frame at scale 1 (the still band's centre)
EYE_WHITE = {"baseColor": [0.86, 0.84, 0.80], "roughness": 0.25, "metallic": 0.0}
EYE_IRIS = {"baseColor": [0.03, 0.025, 0.04], "roughness": 0.3, "metallic": 0.0}
LID = {"baseColor": list(IVORY), "roughness": 0.36, "metallic": 0.0}
CAVITY = {"baseColor": [0.012, 0.010, 0.012], "roughness": 0.85, "metallic": 0.0}


def face_pieces(P):
    """The pieces on the face that must move with it: (node, mesh, parent, position, material)."""
    return [(P + "-cavity", "th-mouth-cavity", P + "-head", [0.0, 0.0, 0.0], CAVITY),
            (P + "-eyeLw", "th-eye-white", P + "-eyeL", [0.0, 0.0, 0.0], EYE_WHITE),
            (P + "-eyeLi", "th-eye-iris", P + "-eyeL", [0.0, 0.0, 0.0], EYE_IRIS),
            (P + "-eyeRw", "th-eye-white", P + "-eyeR", [0.0, 0.0, 0.0], EYE_WHITE),
            (P + "-eyeRi", "th-eye-iris", P + "-eyeR", [0.0, 0.0, 0.0], EYE_IRIS),
            (P + "-lidUL", "th-lid-upper", P + "-head", EYEBALL_L, LID),
            (P + "-lidLL", "th-lid-lower", P + "-head", EYEBALL_L, LID),
            (P + "-lidUR", "th-lid-upper", P + "-head", EYEBALL_R, LID),
            (P + "-lidLR", "th-lid-lower", P + "-head", EYEBALL_R, LID)]


BLINK_CLOSE_MS = 35.0    # a lid shuts in about this long ...
BLINK_HOLD_MS = 25.0     # ... stays shut this long ...
BLINK_OPEN_PER_S = 11.0  # ... and opens at this rate (about 90 ms): a blink of about 100-150 ms in all
BLINK_MIN_MS = 2000.0    # never two blinks closer than this
LID_FOLLOW = 0.32        # the upper lid follows a downward glance by this share of the closed angle per unit noise


def blinks(s, spec):
    """The lids (the owner: "add eyelids to busts too"): porcelain shells round each eye, turned about the eye's centre,
    so they can never cut into the eyeball. Both eyes blink together, irregularly: two smooth noises at
    incommensurate rates, each blink on a noise's upward crossing, at least BLINK_MIN_MS apart -- measured on Feline
    Footwear, intervals of 2.0-8.8 s (median 3.4), each blink 133 ms, each head its own. The upper lid also follows the eyes' downward glances (the same saccade noise, the same
    spring), so a look down half-lowers it. The upper lid's turn is clamped at closed, so a blink during a downward
    glance seals and never overshoots."""
    P = spec["prefix"]
    seed = spec.get("scan", (0.4, 0.13, 1))[2]
    for name, r, sd in (("%sBlinkA" % P, 1.1, seed + 601), ("%sBlinkB" % P, 1.7, seed + 709)):
        s.sources.append({"kind": "noise", "name": name, "settings": {"seed": sd}})
        s.params_({"sources/%s/rate" % name: r, "sources/%s/smoothness" % name: 1.0})
    sac = {"springHz": 14.0, "springDamping": 0.78}
    s.sources += [
        sig("%sBlink" % P, [{"source": "noise.%sBlinkA" % P}, {"source": "noise.%sBlinkB" % P}], combine="max",
            chain={"threshold": "binary", "thresholdLevel": 0.7, "envelope": "peakhold",
                   "envelopeHoldMs": BLINK_HOLD_MS, "envelopeFallPerSecond": BLINK_OPEN_PER_S, "envelopeRearm": 0.5,
                   "envelopeRefractoryMs": BLINK_MIN_MS}),
        sig("%sLidU" % P, [{"source": "signal.%sBlink" % P, "chain": {"attackMs": BLINK_CLOSE_MS, "decayMs": 0.0}},
                           {"source": "noise.%sSacP" % P, "amount": LID_FOLLOW, "chain": dict(sac)},
                           {"constant": -0.2 * LID_FOLLOW}],
            combine="sum", chain={"clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0}),
        sig("%sLidL" % P, [{"source": "signal.%sBlink" % P, "chain": {"attackMs": BLINK_CLOSE_MS, "decayMs": 0.0}}]),
    ]
    for side in ("L", "R"):
        s.route(R("signal.%sLidU" % P, "nodes/%s-lidU%s/rotation" % (P, side), LID_UPPER_CLOSED, comp=0),
                R("signal.%sLidL" % P, "nodes/%s-lidL%s/rotation" % (P, side), -LID_LOWER_CLOSED, comp=0))


# ================================================================================================ the background
# The owner's background brief (10-brief-background.md; the plan is 11-background-plan.md): a surreal signal dimension
# behind the heads. Two warped membranes carry the colour fields and topographic contour lines; a few partial rings,
# a bent frame and two far bands give depth; sparse filaments hang behind the busts. All of it answers the WHOLE mix
# (the heads answer their own sources): low energy waves the membranes, a bass hit ripples them and lights their
# contours, the mids move the colour, the highs light the filaments, a strong transient tears the membranes for an
# instant, sustained energy brightens the fields -- and the waves, the drift and the colour travel on their own clocks
# when the music is quiet.
FIELD = ("#1a1050", "#5a1fb0", "#e0249a", "#2b6bff")     # indigo, violet, magenta, electric blue
FIELD_B = ("#22104a", "#8a2be2", "#ff4f8b", "#5a3cff")   # the near membrane: violet, pink, blue
BG_PROGRAM_OPS = {}
# The membranes' size: what a sheet costs is the share of the frame it covers (about 4 ms for a frame-filling sheet
# at 1080p before its program), so they leave dark sky round them -- which also shows their shape.
BG_SCALE = {"thbg-memA": 0.72, "thbg-memB": 0.6}


# foil1, the owner: "darken the background". One factor on every light of the background (the membranes' colour
# fields, their contour lines, the arcs, the bands, the filaments) -- their constants AND the routes that light them,
# and their dark bodies too (drawn unlit, a body shows its base colour as it is: that, not the glow, set most of a
# membrane's brightness),
# so the motion and the reactivity are unchanged, only dimmer; one on the sky (its colours, its slow tone routes and
# the fog's colour, which hazes the far membranes);
# one on the room's mist, and one on each head's halo (the faces are lit by the key light, not by these).
BG_DIM = 0.25
SKY_DIM = 0.5
MIST_DIM = 0.35
HALO_DIM = 0.3    # each head's halo (the mist behind it in its colour, lit while it speaks): with the room mist, most of
                  # the frame's violet-pink wash


def membrane_program(name, ramp, field_scale=0.16):
    """A membrane's light: a dark surface whose colour field -- an OKLab ramp over a slow diagonal gradient through
    world space, shifted by routes -- glows only in its bright bands. Eight ops: every op of a program costs about
    0.2 ms a frame over a frame-filling sheet at 1080p, so the contour lines are geometry (`-lines` nodes, lit by
    glow_program), not per-pixel arithmetic. Registers: r0 world/base, r5 field, r6 colour, r7 emission."""
    o = Ops()
    o.add("world", "input", 0, input="worldPosition")
    o.add("field", "gradient", 5, srcA=0, value=field_scale, constant=[0.8, 0.45, 0.3, 0.5])
    o.add("fieldShift", "constant", 6, constant=[0.0, 0.0, 0.0, 0.0])
    o.add("fieldShifted", "add", 5, srcA=5, srcB=6)
    o.add("fieldColour", "oklabRamp", 6, srcA=5, value=1.0, constant=hexrgb(ramp[0]) + [1.0],
          constant2=hexrgb(ramp[1]) + [1.0], constant3=hexrgb(ramp[2]) + [1.0], constant4=hexrgb(ramp[3]) + [1.0])
    o.add("fieldGlow", "constant", 7, constant=[0.05 * BG_DIM] * 3 + [0.0])
    o.add("fieldLit", "multiply", 7, srcA=6, srcB=7)
    o.add("base", "constant", 0, constant=[0.012 * BG_DIM, 0.009 * BG_DIM, 0.022 * BG_DIM, 1.0])
    BG_PROGRAM_OPS[name] = o
    return {"name": name, "ops": o.ops, "baseColor": 0, "metallic": -1, "roughness": -1, "emission": 7,
            "emissionIntensity": 1.0, "opacity": -1}


def glow_program(name, colour, gain):
    """A structure's or a filament's light: one colour at a routed gain, on a dark body."""
    o = Ops()
    o.add("colour", "constant", 0, constant=hexrgb(colour) + [1.0])
    o.add("gain", "constant", 1, constant=[gain * BG_DIM] * 3 + [0.0])
    o.add("lit", "multiply", 2, srcA=0, srcB=1)
    o.add("base", "constant", 3, constant=[0.01 * BG_DIM, 0.008 * BG_DIM, 0.02 * BG_DIM, 1.0])
    BG_PROGRAM_OPS[name] = o
    return {"name": name, "ops": o.ops, "baseColor": 3, "metallic": -1, "roughness": -1, "emission": 2,
            "emissionIntensity": 1.0, "opacity": -1}


def background(s):
    sheet = lambda freq, axis, speed, amp: {"kind": "sine", "amount": amp, "frequency": freq, "speed": speed,
                                            "phase": 0.0, "axis": axis, "displacementAxis": [0.0, 0.0, 1.0],
                                            "space": "local"}
    tear = {"kind": "sine", "amount": 0.0, "frequency": 90.0, "speed": 0.0, "phase": 0.0, "axis": [0.0, 1.0, 0.0],
            "displacementAxis": [1.0, 0.0, 0.0], "space": "world"}
    drift = {"kind": "noise", "amount": 0.12, "scale": 0.35, "speed": 0.04, "space": "local"}
    s.program(membrane_program("thbg-memA", FIELD))
    s.program(membrane_program("thbg-memB", FIELD_B, field_scale=0.22))
    s.program(glow_program("thbg-memA-lines", "#7a5cff", 0.10))
    s.program(glow_program("thbg-memB-lines", "#ff5fb0", 0.10))
    s.program(glow_program("thbg-arcs", "#8f7dff", 0.22))
    s.program(glow_program("thbg-bands", "#d0309a", 0.22))
    s.program(glow_program("thbg-fil", "#5f8cff", 0.10))
    # deformers: [slow waves (independent), bass waves (low energy), ripple (bass hits), the tear, the slow drift];
    # a sheet's lines carry the same stack and the same routes, so they ride its waves
    MEM_A = [sheet(0.9, [1.0, 0.35, 0.0], 0.35, 0.10), sheet(1.6, [0.6, 1.0, 0.0], 0.0, 0.0),
             sheet(3.2, [1.0, -0.2, 0.0], 0.0, 0.0), tear, drift]
    MEM_B = [sheet(1.3, [1.0, -0.4, 0.0], -0.5, 0.06), sheet(2.2, [0.4, 1.0, 0.0], 0.0, 0.0),
             sheet(4.0, [1.0, 0.3, 0.0], 0.0, 0.0), tear, drift]
    pieces = [
        ("thbg-memA", "th-bg-membrane-a", [0.5, 0.2, -4.6], [-28.0, 18.0, 6.0], MEM_A),
        ("thbg-memA-lines", "th-bg-membrane-a-lines", [0.5, 0.2, -4.6], [-28.0, 18.0, 6.0], MEM_A),
        ("thbg-memB", "th-bg-membrane-b", [-1.7, 0.75, -2.3], [12.0, 40.0, -14.0], MEM_B),
        ("thbg-memB-lines", "th-bg-membrane-b-lines", [-1.7, 0.75, -2.3], [12.0, 40.0, -14.0], MEM_B),
        ("thbg-arcs", "th-bg-arcs", [0.4, 0.9, -3.4], [0.0, -6.0, 0.0], [sheet(1.1, [1.0, 0.0, 0.0], 0.25, 0.03)]),
        ("thbg-bands", "th-bg-bands", [0.0, 0.5, -5.2], [0.0, 0.0, -4.0], [sheet(0.7, [1.0, 0.2, 0.0], 0.15, 0.10)]),
        ("thbg-fil", "th-bg-filaments", [0.0, 0.25, -1.3], [0.0, 0.0, 0.0],
         [sheet(2.5, [1.0, 0.0, 0.0], 0.6, 0.015), sheet(6.0, [1.0, 0.0, 0.0], 2.0, 0.0)]),
    ]
    for node, mesh, pos, rot, deformers in pieces:
        s.proc(node, {"kind": "mesh", "asset": "meshes/%s.glb" % mesh},
               # unlit: the background is all emission, and the shared PBR shading cost ~5 ms over a frame-filling sheet
               material={"baseColor": [0.012 * BG_DIM, 0.009 * BG_DIM, 0.022 * BG_DIM], "roughness": 0.9,
                         "metallic": 0.0, "program": node,
                         "unlit": True},
               deformers=[dict(d) for d in deformers], position=pos, rotation=rot,
               scale=[BG_SCALE.get(node.replace("-lines", ""), 1.0)] * 3, extra={"castsShadow": False})
    # a constant op's value: material/<program>/op/<n>/constant/constant
    op = lambda prog, label, kind: "material/%s/op/%d/%s/constant" % (prog, BG_PROGRAM_OPS[prog].at[label], kind)
    LOW = {"attackMs": 400.0, "decayMs": 1400.0}      # low energy, sustained
    HIT = {"attackMs": 0.0, "decayMs": 700.0}         # a bass hit
    STRONG = {"threshold": "gate", "thresholdLevel": 0.7, "attackMs": 0.0, "decayMs": 140.0}
    for base, wave, ripple, rip_phase in (("thbg-memA", 0.16, 0.06, 9.0), ("thbg-memB", 0.10, 0.045, 12.0)):
        for node in (base, base + "-lines"):
            d = lambda n, leaf: "procedural/%s/deform/%d/%s" % (node, n, leaf)
            s.route(R("audio.bass", d(2, "amount"), wave, **LOW),                  # low energy: big waves
                    R("audio.bass", d(2, "phase"), 2.5, integrate=True, **LOW),    # ... that travel while it lasts
                    R("response.lowEnv", d(3, "amount"), ripple, **HIT),           # a bass hit: a ripple ...
                    R("response.lowEnv", d(3, "phase"), rip_phase, integrate=True, **HIT),   # ... running across it
                    R("response.onsetEnv", d(4, "amount"), 0.035, **STRONG),       # a strong transient: it tears
                    R("response.onsetEnv", d(4, "phase"), 30.0, integrate=True, attackMs=0.0, decayMs=140.0))
        node = base
        # a bass hit lights the contour lines
        s.route(R("response.lowEnv", op(base + "-lines", "gain", "constant"), 1.4 * BG_DIM, comp=c, **HIT)
                for c in range(3))
        s.route(R("audio.mid", op(node, "fieldShift", "constant"), 0.45, comp=c, attackMs=900.0, decayMs=2600.0)
                for c in range(3))
        s.route(R("response.intensity", op(node, "fieldGlow", "constant"), 0.14 * BG_DIM, comp=c, attackMs=3000.0,
                  decayMs=6000.0) for c in range(3))
    # the slow, independent colour: the field's shift also wanders on its own minute-long noise
    s.sources.append({"kind": "noise", "name": "bgTone", "settings": {"seed": 83}})
    s.params_({"sources/bgTone/rate": 0.017, "sources/bgTone/smoothness": 1.0})
    for node in ("thbg-memA", "thbg-memB"):
        s.route(R("noise.bgTone", op(node, "fieldShift", "constant"), 0.3, comp=c, polarity="bipolar")
                for c in range(3))
    # the structures flare on strong transients; the filaments on the highs (and quiver)
    s.route(R("response.onsetEnv", op("thbg-arcs", "gain", "constant"), 1.2 * BG_DIM, comp=c, **STRONG)
            for c in range(3))
    s.route(R("audio.mid", op("thbg-bands", "gain", "constant"), 0.25 * BG_DIM, comp=c, attackMs=900.0,
              decayMs=2600.0) for c in range(3))
    s.route(R("audio.treble", op("thbg-fil", "gain", "constant"), 1.4 * BG_DIM, comp=c, attackMs=20.0,
              decayMs=260.0) for c in range(3))
    s.route(R("audio.treble", "procedural/thbg-fil/deform/2/amount", 0.012, attackMs=20.0, decayMs=260.0))


# ================================================================================================ 1-4 busts
ALL_ROLES = ("drums", "bass", "synth", "vocal", "other")
LAYOUT_GLIDE = {"attackMs": 700.0, "decayMs": 700.0}   # a bust glides to its new place when the count changes


def layout_signals(s, heads):
    """Which busts are shown, and where (fx1, 14-brief-secondary-fx.md sections 6-7). From the sources' own flags
    (`role.<role>.active`, stems or live inputs):
      thNoSources      1 while no source of any role is assigned (the fourth slot is then the hero on the mix)
      <p>Active        the slot's role (or one of its roles) is assigned -- or, for the mix slot, no source is
      thCount          how many slots are active (0..4; never more: one slot per role)
      thEq<c>          thCount == c;  <p>R<r>  the slot's rank among the active slots == r (the slots in order)
      visual.<p>L<c><r> the product: this slot is the rank-r bust of a c-bust layout
    An inactive slot has every indicator 0: it shrinks to nothing and its gate reads silence (its role's signals are
    inactive), so nothing of it is left on screen or on the bus."""
    binary = lambda lv: {"threshold": "binary", "thresholdLevel": lv}
    s.sources.append(sig("thNoSources", [{"constant": 1.0}] + [{"source": "role.%s.active" % r, "amount": -1.0}
                                                               for r in ALL_ROLES],
                         combine="sum", chain={"clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0}))
    for spec in heads:
        P = spec["prefix"]
        ins = [{"source": "role.%s.active" % r} for r in spec["roles"]]
        if spec.get("mix"):
            ins.append({"source": "signal.thNoSources"})
        s.sources += [sig(P + "Active", ins, combine="max"),
                      sig(P + "Inactive", [{"constant": 1.0}, {"source": "signal.%sActive" % P, "amount": -1.0}],
                          combine="sum")]
    s.sources.append(sig("thCount", [{"source": "signal.%sActive" % h["prefix"]} for h in heads], combine="sum"))
    for c in range(1, len(heads) + 2):
        s.sources.append(sig("thGe%d" % c, [{"source": "signal.thCount", "chain": binary(c - 0.5)}]))
    for c in range(1, len(heads) + 1):
        s.sources.append(sig("thEq%d" % c, [{"source": "signal.thGe%d" % c},
                                            {"source": "signal.thGe%d" % (c + 1), "amount": -1.0}], combine="sum"))
    for i, spec in enumerate(heads):
        P = spec["prefix"]
        before = [{"source": "signal.%sActive" % heads[j]["prefix"]} for j in range(i)]
        s.sources.append(sig(P + "Rank", before or [{"constant": 0.0}], combine="sum"))
        for r in range(1, i + 2):
            s.sources.append(sig("%sRge%d" % (P, r), [{"source": "signal.%sRank" % P, "chain": binary(r - 0.5)}]))
        for r in range(0, i + 1):
            lo = [{"constant": 1.0}] if r == 0 else [{"source": "signal.%sRge%d" % (P, r)}]
            s.sources.append(sig("%sR%d" % (P, r), lo + [{"source": "signal.%sRge%d" % (P, r + 1), "amount": -1.0}],
                                 combine="sum"))
        for c in range(1, len(heads) + 1):
            for r in range(0, min(i, c - 1) + 1):
                s.map(M("%sL%d%d" % (P, c, r), [("signal.thEq%d" % c, 1.0), ("signal.%sR%d" % (P, r), 1.0),
                                                 ("signal.%sActive" % P, 1.0)], "product"))


def layout_routes(s, heads):
    """Each bust to its place for the live count: position, heading and size as offsets from its resting place
    (base_layout), and the halo (and the grains) with it; an inactive bust shrinks to nothing."""
    for i, spec in enumerate(heads):
        P = spec["prefix"]
        bx, bz, bh, bs = base_layout(i)
        node = "nodes/%s-bust/" % P
        for c in range(1, len(heads) + 1):
            for r in range(0, min(i, c - 1) + 1):
                x, z, h, sc = LAYOUT[c][r]
                ind = "visual.%sL%d%d" % (P, c, r)
                hd, bhd = math.radians(h), math.radians(bh)
                if abs(x - bx) > 1e-6:
                    s.route(R(ind, node + "position", x - bx, comp=0, **LAYOUT_GLIDE))
                if abs(z - bz) > 1e-6:
                    s.route(R(ind, node + "position", z - bz, comp=2, **LAYOUT_GLIDE))
                if abs(h - bh) > 1e-6:
                    s.route(R(ind, node + "rotation", h - bh, comp=1, **LAYOUT_GLIDE))
                if abs(sc - bs) > 1e-6:
                    s.route(*[R(ind, node + "scale", sc - bs, comp=k, **LAYOUT_GLIDE) for k in range(3)])
                # the halo behind the head, and the grains at it, follow
                mx = (x - 0.3 * math.sin(hd)) - (bx - 0.3 * math.sin(bhd))
                mz = (z - 0.35) - (bz - 0.35)
                s.route(R(ind, "particles/%s-mist/position" % P, mx, comp=0, **LAYOUT_GLIDE),
                        R(ind, "particles/%s-mist/position" % P, mz, comp=2, **LAYOUT_GLIDE),
                        R(ind, "particles/%s-mist/position" % P, 0.45 * (sc - bs), comp=1, **LAYOUT_GLIDE))
                if spec.get("profile") in ("tracking", "codec"):
                    # the block deformer's still band (at the mouth) moves with the bust
                    for pn in ["bust", "neck", "head"] + [f[0][len(P) + 1:] for f in face_pieces(P)]:
                        d = {"bust": 1, "neck": 4, "head": 2}.get(pn, 1)
                        cp = "procedural/%s-%s/deform/%d/center" % (P, pn, d)
                        s.route(R(ind, cp, x - bx, comp=0, **LAYOUT_GLIDE),
                                R(ind, cp, MOUTH_HEIGHT * (sc - bs), comp=1, **LAYOUT_GLIDE),
                                R(ind, cp, z - bz, comp=2, **LAYOUT_GLIDE))
        # an inactive bust: shrunk to nothing (it grows back in when its source arrives)
        s.route(*[R("signal.%sInactive" % P, node + "scale", -0.999 * bs, comp=k, attackMs=150.0, decayMs=500.0)
                  for k in range(3)])


# ================================================================================================ the instrument
def sig(name, inputs, combine="firstActive", chain=None, event=False):
    st = {"combine": combine, "inputs": inputs}
    if chain:
        st["chain"] = chain
    if event:
        st["event"] = True
    return {"kind": "signal", "name": name, "settings": st}


CHANNELS = [("master", 1), ("beams", 2), ("sensitivity", 3), ("copies", 4), ("tear", 5), ("chroma", 6), ("gain", 7),
            ("palette", 8), ("tilt", 9)]
PADS = [("padShut", 36), ("padOpen", 37), ("padBurst", 39)]


def control(s):
    s.control = {"midi": {"enabled": True, "filter": "*", "pickup": True,
                          "bindings": [{"source": "*", "channel": -1, "kind": "cc", "number": n, "signal": name}
                                       for name, n in CHANNELS]
                          + [{"source": "*", "channel": -1, "kind": "noteEvent", "number": n, "signal": name}
                             for name, n in PADS]},
                 "channels": [{"name": name, "rest": 0.0 if name == "palette" else 0.5, "pickup": True}
                              for name, _ in CHANNELS]}
    # the knobs as gates: master x the feature's own knob, gain 2, so the rests (0.5 x 0.5) read 0.5; each gated route
    # maps 0..1 onto depth 0..2 (below), so at rest a route adds its own amount and a knob turned fully up doubles it
    s.map(*[M(name + "Gate", [("control.master", 1.0), ("control." + name, 1.0)], "product", gain=2.0)
            for name in ("beams", "copies", "tear", "chroma")])
    s.map(M("tiltGate", [("control.tilt", 1.0)], "mean"))
    s.sources.append(sig("padShutHold", [{"source": "control.padShut", "chain": {
        "envelope": "peakhold", "envelopeHoldMs": 4000.0, "envelopeFallPerSecond": 1000.0}}]))


def gate(s, spec):
    """One head's noise gate (05 section 3, 07). Two paths, one gate:

    THE CHANNEL (stems, or live inputs with the head's role) -- the feel the owner approved at stop 2:
      <p>Level   role.<role>.<band>, live only while that role is active; shifted by CC 3 (sensitivity)
      <p>Rise    Level less its 150 ms follower; <p>Trig a one-frame latch on a sharp rise (a re-attack)
      Schmitt    opens at `open`, stays open above `close` (through last frame's visual.<p>GateKeep), held ~33 ms
      <p>Chomp   Trig while already open: one frame shut, so every note is its own snap
    THE MIX (no channel of that role): the mix's own band, MIX_STRICT higher, as a fixed MIX_PULSE_MS opening at most
      once per MIX_REFRACTORY_MS (07: "limit the amount of times it happens").

      <p>Gate      the max of the two; visual.<p>GateLive = Gate x (1 - Chomp) x (1 - the shut pad): what the head does
      <p>Open      a one-shot on each rise of Live: each opening and each re-opening after a chomp (the flashes)
    Pad 37 forces every gate open, pad 36 shut."""
    P, roles, band = spec["prefix"], spec["roles"], spec["band"]
    L, LS, RS, TR, GR, GT, PV, CH, OP, OM, ML, MG = ("%s%s" % (P, n) for n in (
        "Level", "LevelS", "Rise", "Trig", "GateRaw", "Gate", "Prev", "Chomp", "Open", "OnMix", "MixLevel", "MixGate"))
    ON = P + "Onset"   # the slot's onset strength: the first active of its roles
    sens = {"source": "control.sensitivity", "polarity": "bipolar", "amount": 0.16}   # up = more sensitive
    binary = lambda lv: {"threshold": "binary", "thresholdLevel": lv}
    s.sources += [
        sig(L, [{"source": "role.%s.%s" % (r, band)} for r in roles]),
        sig(ON, [{"source": "role.%s.onsetStrength" % r} for r in roles]),
        sig(LS, [{"source": "signal." + L}, sens], combine="sum"),
        sig(RS, [{"source": "signal." + LS}, {"source": "signal." + LS, "amount": -1.0,
                                              "chain": {"attackMs": 150.0, "decayMs": 150.0}}], combine="sum"),
        sig(TR, [{"source": "signal." + RS, "chain": dict(binary(0.20), envelope="peakhold", envelopeHoldMs=0.0,
                                                          envelopeFallPerSecond=30.0, envelopeRearm=0.5,
                                                          envelopeRefractoryMs=33.0)}]
            + ([{"source": "signal." + ON, "chain": dict(binary(spec.get("onsetLevel", 1.0)),
                                                         envelope="peakhold", envelopeHoldMs=0.0,
                                                                 envelopeFallPerSecond=30.0, envelopeRearm=0.5,
                                                                 envelopeRefractoryMs=ONSET_REFRACTORY_MS)}]
               if spec.get("onsets") else []), combine="max"),
        sig(PV, [{"source": "visual.%sKeep" % GT}]),                      # one-frame memory
    ]
    if spec.get("mix"):
        # the mix path (the hero with no sources): on only while NO source of any role is assigned (layout())
        s.sources += [
            sig(ML, [{"source": spec["mix"]}, sens], combine="sum"),
            sig(MG, [{"source": "signal." + ML, "gate": "signal.thNoSources",
                      "chain": dict(binary(spec.get("mixOpen", spec["open"] + MIX_STRICT)), envelope="peakhold",
                                    envelopeHoldMs=MIX_PULSE_MS, envelopeFallPerSecond=1000.0, envelopeRearm=0.5,
                                    envelopeRefractoryMs=MIX_REFRACTORY_MS)}], combine="max"),
        ]
    s.sources += [
        sig(GR, [{"source": "signal." + LS, "chain": binary(spec["open"])},
                 {"source": "signal." + PV}]
            # ONSETS (stop 5, the synth: "i think it needs to be more sensitive"): the stem's own note onsets open the
            # gate for a moment, the level keeps it open while the note rings, and an onset while open is a chomp --
            # so legato notes, which never fall below the level's threshold between them, each read as their own
            + ([{"source": "signal." + ON, "chain": dict(binary(spec.get("onsetLevel", 1.0)),
                                                         envelope="peakhold",
                                                         envelopeHoldMs=spec.get("onsetHoldMs", 140.0),
                                                                         envelopeFallPerSecond=1000.0,
                                                                         envelopeRearm=0.5,
                                                                         envelopeRefractoryMs=ONSET_REFRACTORY_MS)}]
               if spec.get("onsets") else [])
            + ([{"source": "signal." + MG}] if spec.get("mix") else []) + [
                 {"source": "control.padOpen", "chain": {"envelope": "peakhold", "envelopeHoldMs": 600.0,
                                                         "envelopeFallPerSecond": 1000.0}}],
            combine="max", chain={"attackMs": 0.0, "decayMs": 36.0}),
        sig(GT, [{"source": "signal." + GR, "chain": binary(0.4)}]),
        sig(GT + "Keep", [{"source": "signal." + LS, "chain": binary(spec["close"])}]),
        sig(CH, [{"source": "signal." + TR, "gate": "signal." + PV}]),
    ]
    # what the head does: Gate AND NOT Chomp AND NOT the shut pad (all 0 or 1: Gate - Chomp - Shut, clamped), then the POSE --
    # that through the snap's spring, in a signal, so every pose route and the rays read one value. Values under 0.002
    # are cut to exactly 0 by each route (a Bend deformer at a curvature of 1e-5 loses its normals to float precision:
    # the neck went dark while a spring's tail was still settling).
    s.sources += [
        sig(P + "Live", [{"source": "signal." + GT}, {"source": "signal." + CH, "amount": -1.0},
                         {"source": "signal.padShutHold", "amount": -1.0}],
            combine="sum", chain={"clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0}),
        sig(P + "Pose", [{"source": "signal.%sLive" % P, "chain": dict(SNAP)}]),
        # the flash: a one-shot on each rise of what the head DOES (an opening, and the re-opening after each chomp),
        # so a head held shut (pad 36) or silent flashes nothing
        sig(OP, [{"source": "signal.%sLive" % P, "chain": {"envelope": "peakhold", "envelopeHoldMs": 0.0,
                                                            "envelopeFallPerSecond": 3.0, "envelopeRearm": 0.5,
                                                            "envelopeRefractoryMs": 30.0}}]),
    ]
    s.map(M(GT + "Keep", [("signal.%sKeep" % GT, 1.0), ("signal." + GT, 1.0)], "min"))


SCAN_RANGE = 26.0       # degrees either side of a head's resting heading: the whole bust's turn ...
SCAN_SNAP = 10.0        # ... its occasional snap ...
SCAN_PAN = {"springHz": 0.9, "springDamping": 1.0}     # ... and a pan: about 0.6-1.2 s, mechanical, no overshoot
# The neck's swivel (the owner, stop 4: "making their necks swivel so they can also look up down left right at the
# neck too"): its own mechanical pans, pauses and snaps in two axes, clamped, layered on the gate's snap back.
NECK_YAW = 30.0         # degrees either side
NECK_UP = 10.0          # looking up (the neck bends back) ...
NECK_DOWN = 14.0        # ... and down (it bends forward)
NECK_FULL = 36.0        # the most the neck bends back: the gate's 24 plus looking up 10 (and margin)
NECK_PAN = {"springHz": 1.3, "springDamping": 1.0}
NECK_LAG_MS = 220.0     # the neck sets off this much behind the eyes (07: eyes lead, the neck follows a beat later)


def scan_and_eyes(s, spec):
    """Where a head looks, in three layers, each with its own clocks and seed:

    THE SCAN (07: "scanning the room", readable): the whole bust's yaw. A STEPPED noise (a new heading every 1/rate
      seconds, held) through a critically damped spring -- a pan -- plus a second stepped noise with no follower: the
      occasional snap.
    THE NECK (stop 4): yaw and pitch at the neck, the same mechanics: stepped targets, held, through a lag and a
      critically damped spring. Yaw twists the neck (its base stays put on the body), pitch bends it -- one-sided bends,
      back or forward, so the bend is exactly 0 at rest -- and the head rides the neck's top through its pivot.
    THE EYES (07: darting, then "make the movements look more realistic"): conjugate, in the head's frame, through a
      spring with a slight overshoot (saccades accelerate and settle in about 40-70 ms):
      - they LEAD: (target - current) of the scan AND of the neck, so they arrive before the head does;
      - saccades: two stepped noises at incommensurate rates (fixations of about 0.3-1.2 s), mostly small moves;
      - downward glances: a stepped pitch biased down (07: "eyes can look down though");
      - micro-drift during a fixation: a slow smooth noise of under a degree.
    Kept within about +-27 degrees yaw and -4..+16 pitch so the iris never sinks into the socket."""
    P = spec["prefix"]
    rate, snap_rate, seed = spec.get("scan", (0.4, 0.13, 1))
    noises = [("%sScanT" % P, rate, 0.0, seed), ("%sScanSnap" % P, snap_rate, 0.0, seed + 101),
              ("%sSacA" % P, 1.35, 0.0, seed + 211), ("%sSacB" % P, 2.15, 0.0, seed + 307),
              ("%sSacP" % P, 0.85, 0.0, seed + 401), ("%sDrift" % P, 0.6, 1.0, seed + 503),
              ("%sNeckYT" % P, rate * 1.3, 0.0, seed + 809), ("%sNeckPT" % P, rate * 0.9, 0.0, seed + 907)]
    for name, r, smooth, sd in noises:
        s.sources.append({"kind": "noise", "name": name, "settings": {"seed": sd}})
        s.params_({"sources/%s/rate" % name: r, "sources/%s/smoothness" % name: smooth})
    neck = dict(NECK_PAN, attackMs=NECK_LAG_MS, decayMs=NECK_LAG_MS)
    # the neck's look, in units of NECK_FULL (pitch, up positive) and NECK_YAW (yaw): -1..1 targets, then the lag and
    # the pan. The pitch's target spans -NECK_DOWN..+NECK_UP: bipolar x (UP + DOWN) / 2, shifted by (UP - DOWN) / 2.
    half = 0.5 * (NECK_UP + NECK_DOWN) / NECK_FULL
    shift = 0.5 * (NECK_UP - NECK_DOWN) / NECK_FULL
    s.sources += [
        sig("%sScanNow" % P, [{"source": "noise.%sScanT" % P, "chain": dict(SCAN_PAN)}]),
        sig("%sLead" % P, [{"source": "noise.%sScanT" % P}, {"source": "signal.%sScanNow" % P, "amount": -1.0}],
            combine="sum"),
        sig("%sNeckYTarget" % P, [{"source": "noise.%sNeckYT" % P, "polarity": "bipolar"}]),
        sig("%sNeckY" % P, [{"source": "signal.%sNeckYTarget" % P, "chain": dict(neck)}]),
        sig("%sNeckPTarget" % P, [{"source": "noise.%sNeckPT" % P, "polarity": "bipolar", "amount": half},
                                  {"constant": shift}], combine="sum"),
        sig("%sNeckP" % P, [{"source": "signal.%sNeckPTarget" % P, "chain": dict(neck)}]),
        sig("%sNeckLeadY" % P, [{"source": "signal.%sNeckYTarget" % P},
                                {"source": "signal.%sNeckY" % P, "amount": -1.0}], combine="sum"),
        sig("%sNeckLeadP" % P, [{"source": "signal.%sNeckPTarget" % P},
                                {"source": "signal.%sNeckP" % P, "amount": -1.0}], combine="sum"),
    ]
    bust = "nodes/%s-bust/rotation" % P
    s.route(R("signal.%sScanNow" % P, bust, SCAN_RANGE, comp=1, polarity="bipolar"),
            R("noise.%sScanSnap" % P, bust, SCAN_SNAP, comp=1, polarity="bipolar"))
    sac = {"springHz": 14.0, "springDamping": 0.78}
    for eye in ("eyeL", "eyeR"):
        rot = "nodes/%s-%s/rotation" % (P, eye)
        s.route(R("signal.%sLead" % P, rot, 10.0, comp=1, **sac),                       # lead the scan
                R("signal.%sNeckLeadY" % P, rot, 7.0, comp=1, **sac),                  # ... and the neck's turn
                R("signal.%sNeckLeadP" % P, rot, -0.25 * NECK_FULL, comp=0, **sac),    # ... and its nod (up = -)
                R("noise.%sSacA" % P, rot, 8.0, comp=1, polarity="bipolar", **sac),  # saccades
                R("noise.%sSacB" % P, rot, 3.0, comp=1, polarity="bipolar", **sac),  # small ones
                R("noise.%sSacP" % P, rot, 20.0, comp=0, offset=-0.2, **sac),           # down glances, -4..+16
                R("noise.%sDrift" % P, rot, 1.4, comp=1, polarity="bipolar"),           # fixation drift
                R("noise.%sDrift" % P, rot, 1.0, comp=0, polarity="bipolar"))


FOIL_DRIFT = 0.012      # the bands' drift, in u (t/8) per second at full level: about a band every 17 s
FOIL_SWEEP = 0.8        # an onset's push, integrated over its ~125 ms envelope: about half a band per hit
FOIL_GLOW = (0.12, 0.9) # the bands' own light: at rest, and added while the gate is open


def foil_routes(s, spec, progs, opath, LIVE, OPEN, LEVEL, ONSET, K):
    """The music on the foil, head by head, from this head's own source only (the same signals as its gate):
    - the bands DRIFT with the level (integrated: they keep the place they reached when the source stops);
    - an onset SWEEPS them on (an event envelope, integrated: a push, never a jump);
    - the gate WIDENS the bands and makes them shine while it is open (smoothed both ways: no flicker);
    - a hit lifts the sparkle a little (more cells pass the flare threshold, for ~300 ms)."""
    P = spec["prefix"]
    sweep = "signal.%sSweep" % P
    s.sources.append(sig(P + "Sweep", [{"source": ONSET, "chain": {
        "threshold": "binary", "thresholdLevel": 0.7, "envelope": "peakhold", "envelopeHoldMs": 0.0,
        "envelopeFallPerSecond": 4.0, "envelopeRearm": 0.5, "envelopeRefractoryMs": 120.0}}]))
    for prog in progs:
        s.route(R(LEVEL, opath(prog, "fDrift", "constant"), FOIL_DRIFT, comp=0, integrate=True, attackMs=400,
                  decayMs=1500),
                R(sweep, opath(prog, "fDrift", "constant"), FOIL_SWEEP, comp=0, integrate=True),
                R(LIVE, opath(prog, "fBand", "smoothstep"), -0.12, comp=0, attackMs=120, decayMs=700),
                R(LIVE, opath(prog, "fBand", "smoothstep"), -0.03, comp=1, attackMs=120, decayMs=700),
                R(OPEN, opath(prog, "fSpark", "smoothstep"), -0.0015, comp=0, attackMs=0, decayMs=300),
                R(LEVEL, opath(prog, "fTwinkle", "constant"), FOIL_TWINKLE[0], comp=0, integrate=True, attackMs=200,
                  decayMs=800),
                R(sweep, opath(prog, "fTwinkle", "constant"), FOIL_TWINKLE[1], comp=0, integrate=True))
        for c in range(3):
            s.route(R(LIVE, opath(prog, "fGlow", "constant"), FOIL_GLOW[1], comp=c, depth=K("chroma"), attackMs=120,
                      decayMs=700))

def routes(s, spec, rays):
    P = spec["prefix"]
    G = "signal.%sPose" % P                # the gate's pose, 0..1 through the snap (the shut pad applied)
    LIVE = "signal.%sLive" % P             # the gate itself, 0 or 1
    SNAP_CUT = {"threshold": "gate", "thresholdLevel": 0.002}   # exact 0 at rest (see gate())
    OPEN = "signal.%sOpen" % P             # a flash on each opening
    K = lambda f: "visual.%sGate" % f      # a knob's gate
    fx = lambda i, leaf: "fx/%s/%s" % (i, leaf)
    clamp = lambda hi: {"clampEnabled": True, "clampMin": 0.0, "clampMax": hi}
    # ---- the neck: the gate's snap back plus the look's pitch, as ONE angle in units of NECK_FULL (back positive),
    # split into its back and forward halves (each 0..1, one of them 0); the tilt knob (CC 9) scales the snap
    s.sources += [
        sig(P + "Theta", [{"source": G, "amount": TILT_OPEN / NECK_FULL, "depthSource": "visual.tiltGate",
                           "depthMax": 2.0},
                          {"source": "signal.%sNeckP" % P}], combine="sum"),
        sig(P + "Back", [{"source": "signal.%sTheta" % P}], chain=clamp(1.0)),
        sig(P + "Fwd", [{"source": "signal.%sTheta" % P, "amount": -1.0}], chain=clamp(1.0)),
    ]
    s.map(M(P + "Back2", [("signal.%sBack" % P, 1.0)], "max", curve=2.0),
          M(P + "Fwd2", [("signal.%sFwd" % P, 1.0)], "max", curve=2.0))
    dy, dz = neck_offset(NECK_FULL)
    k = math.radians(NECK_FULL) / BEND_LEN
    piv = "nodes/%s-pivot/" % P
    for half, sgn in (("Back", 1.0), ("Fwd", -1.0)):
        src = "signal.%s%s" % (P, half)
        s.route(R(src, "procedural/%s-neck/deform/%d/amount" % (P, 2 if half == "Back" else 3), k, **SNAP_CUT),
                R(src, piv + "rotation", -sgn * NECK_FULL, comp=0, **SNAP_CUT),
                R(src, piv + "position", sgn * dz, comp=2, **SNAP_CUT),
                R("visual.%s%s2" % (P, half), piv + "position", dy, comp=1))
    # ... and its yaw: the twist (radians per metre up the neck, so its top turns by the angle) and the head with it
    s.route(R("signal.%sNeckY" % P, "procedural/%s-neck/deform/1/amount" % P, math.radians(NECK_YAW) / BEND_LEN),
            R("signal.%sNeckY" % P, "nodes/%s-head/rotation" % P, NECK_YAW, comp=1))
    # ---- the mouth: the head's Hinge opens with the gate's pose
    s.route(R(G, "procedural/%s-head/deform/1/amount" % P, MOUTH_OPEN, **SNAP_CUT))
    # ---- the rays: a direct function of the mouth's opening (the owner: "mouth fully closed = rays off / fully open =
    # rays fully bright"). The SAME signal (the pose, settle included) and the same route chain as the mouth's route
    # above, so ray = weight x RAY_BRIGHTNESS x (hinge angle / MOUTH_OPEN) on every frame. CC 2 scales it.
    for i, r in enumerate(rays):
        s.route(R(G, fx(r, "intensity"), RAY_BRIGHTNESS * RAY_WEIGHT[i], depth=K("beams"), **SNAP_CUT))
    # ---- its light on the mist: the same pose, in its own colour
    s.route(R(G, "particles/%s-mist/emissive" % P, 1.4 * HALO_DIM, depth=K("chroma"), attackMs=0, decayMs=350))
    # ---- the secondary effects: this head's PROFILE (17-brief-signal-corruption.md), every one on this head's own signals only
    prof = spec.get("profile", "tracking")
    pal = spec.get("palette", ORCHID)
    LEVEL = "signal.%sLevelS" % P          # the slot's level (its band, the sensitivity knob applied)
    ONSET = "signal.%sOnset" % P           # its onset strength
    surf = {"bust": 1, "neck": 4, "head": 2}   # the world-space block deformer: each piece's last
    face = [f[0][len(P) + 1:] for f in face_pieces(P)]   # the face's pieces carry it too (their only deformer)
    surf.update({f: 1 for f in face})
    blocks = ["procedural/%s-%s/deform/%d/" % (P, n, surf[n]) for n in surf]
    progs = bust_progs(P)
    opath = lambda prog, label, kind, field="constant": "material/%s/op/%d/%s/%s" % (
        prog, PROGRAM_OPS[prog].at[label], kind, field)
    colour = hexrgb(pal["glow"])
    ghost = lambda cp: "nodes/%s-ghost%sh/" % (P, cp)
    gain = lambda cp: "material/%s-ghost%s/op/4/constant/constant" % (P, cp)

    def event(name, level, fall, refractory, hold=0.0):
        """A one-shot envelope on this slot's onsets: jumps to 1 when the onset strength passes `level`, falls at
        `fall` per second; `refractory` ms before it can fire again."""
        s.sources.append(sig(P + name, [{"source": ONSET, "chain": {
            "threshold": "binary", "thresholdLevel": level, "envelope": "peakhold", "envelopeHoldMs": hold,
            "envelopeFallPerSecond": fall, "envelopeRearm": 0.5, "envelopeRefractoryMs": refractory}}]))
        return "signal." + P + name

    for cp in ("A", "B"):
        if prof in ("tracking", "broadcast", "ghosting"):
            s.route(*[R(LIVE, ghost(cp) + "scale", 1.004 if prof != "ghosting" else 1.0, comp=c, attackMs=0,
                        decayMs=600) for c in range(3)])
    if prof == "tracking":
        # TRACKING FAILURE (default: drums): violent and brief. Each hit tears the bust into full-width bands thrown
        # sideways (a share of the bands, a fresh pattern every few frames while it lasts), strips of the surface drop
        # out to static, the red and cyan channels jump out of register, and the bust shakes -- all gone in ~150 ms.
        hit = event("Hit", 0.6, 7.0, 60.0)
        s.route(R(OPEN, "fx/%s-shake/trauma" % P, 0.6, depth=K("copies"), attackMs=0, decayMs=140, **clamp(1.0)))
        for d in blocks:
            s.route(R(hit, d + "amount", 0.045, depth=K("tear"), attackMs=0, decayMs=90),
                    R(hit, d + "falloff", 0.55, attackMs=0, decayMs=120),
                    R(hit, d + "phase", 24.0, integrate=True, attackMs=0, decayMs=150),
                    R("control.padBurst", d + "amount", 0.06, attackMs=0, decayMs=150),
                    R("control.padBurst", d + "falloff", 0.6, attackMs=0, decayMs=150))
        for prog in progs:
            s.route(R(hit, opath(prog, "bands", "threshold", "value"), -0.3, depth=K("tear"), attackMs=0,
                      decayMs=120))
        for cp, sgn in (("A", 1.0), ("B", -1.0)):
            s.route(R(hit, gain(cp), 0.9, comp=0, depth=K("copies"), attackMs=0, decayMs=160),
                    R(hit, ghost(cp) + "position", 0.016 * sgn, comp=0, depth=K("copies"), attackMs=0, decayMs=110),
                    R(hit, ghost(cp) + "position", 0.003 * sgn, comp=1, depth=K("copies"), attackMs=0, decayMs=110))
    elif prof == "codec":
        # CODEC COLLAPSE (default: bass): the image reconstructed wrongly. The slot's smoothed level (the bass band)
        # sets how many macroblocks are displaced and how far, slow to rise and slower to clear; a strong onset is a
        # reconstruction failure -- a burst of more and larger displacement, a new block pattern, and blocks that lose
        # their image to flat smeared colour, decaying over ~2 s. Between notes the blocks settle back into the bust.
        crash = event("Crash", 1.3, 0.5, 500.0)
        jump = event("Jump", 1.3, 12.0, 500.0)       # ~80 ms: one reshuffle of the block pattern per crash
        for d in blocks:
            s.route(R(LEVEL, d + "amount", 0.022, depth=K("tear"), attackMs=350, decayMs=2500),
                    R(crash, d + "amount", 0.03, depth=K("tear"), attackMs=0, decayMs=1800),
                    R(LEVEL, d + "falloff", 0.28, attackMs=500, decayMs=3000),
                    R(crash, d + "falloff", 0.3, attackMs=0, decayMs=2000),
                    R(jump, d + "phase", 14.0, integrate=True, attackMs=0, decayMs=0),
                    R("control.padBurst", d + "amount", 0.05, attackMs=0, decayMs=900),
                    R("control.padBurst", d + "falloff", 0.5, attackMs=0, decayMs=900))
        for prog in progs:
            s.route(R(LEVEL, opath(prog, "cMask", "threshold", "value"), -0.12, depth=K("copies"), attackMs=500,
                      decayMs=3000),
                    R(crash, opath(prog, "cMask", "threshold", "value"), -0.25, depth=K("copies"), attackMs=0,
                      decayMs=2000))
    elif prof == "broadcast":
        # BROADCAST INTERFERENCE (default: synth): analog and slow. Patches of animated static open over the bust as
        # the level holds (their threshold drifting down over about a second), scanlines in the palette's colour
        # shimmer on, and the red and blue channels drift apart at the silhouette; an onset is a burst of all three.
        burst = event("Burst", 1.0, 3.0, 150.0, hold=40.0)
        for prog in progs:
            for c in (0, 1):
                for lab, lv, bu in (("patch", -0.6, -0.3), ("slArea", -0.62, -0.3)):
                    s.route(R(LEVEL, opath(prog, lab, "smoothstep"), lv, comp=c, depth=K("copies"),
                              attackMs=900, decayMs=2200),
                            R(burst, opath(prog, lab, "smoothstep"), bu, comp=c, depth=K("copies"), attackMs=0,
                              decayMs=500))
            for c, k in enumerate((0.9, 1.7, 0.4)):
                s.route(R(LEVEL, opath(prog, "bN", "noise"), 40.0 * k, comp=c, integrate=True),
                        R(burst, opath(prog, "bN", "noise"), 60.0 * k, comp=c, integrate=True))
            s.route(R(LEVEL, opath(prog, "pN", "noise"), 0.12, comp=1, integrate=True, attackMs=300, decayMs=1500),
                    R(LEVEL, opath(prog, "pN", "noise"), 0.05, comp=0, integrate=True, attackMs=300, decayMs=1500))
            s.route(R(LEVEL, opath(prog, "slOn", "threshold", "value"), -0.55, attackMs=700, decayMs=1800),
                    R(burst, opath(prog, "slOn", "threshold", "value"), -0.3, attackMs=0, decayMs=400))
            for c in range(3):
                s.route(R(LEVEL, opath(prog, "slCol", "constant"), 1.1 * colour[c], comp=c, depth=K("chroma"),
                          attackMs=600, decayMs=1800),
                        R(burst, opath(prog, "slCol", "constant"), 1.0 * colour[c], comp=c, depth=K("chroma"),
                          attackMs=0, decayMs=400))
        for cp, sgn in (("A", 1.0), ("B", -1.0)):
            s.route(R(LEVEL, gain(cp), 0.55, comp=0, depth=K("copies"), attackMs=700, decayMs=1800),
                    R(burst, gain(cp), 0.45, comp=0, depth=K("copies"), attackMs=0, decayMs=400),
                    R(LEVEL, ghost(cp) + "position", 0.008 * sgn, comp=0, depth=K("copies"), attackMs=1200,
                      decayMs=2000),
                    R(burst, ghost(cp) + "position", 0.012 * sgn, comp=0, depth=K("copies"), attackMs=0,
                      decayMs=500))
    elif prof == "ghosting":
        # TEMPORAL GHOSTING (default: the fourth source): two lingering frames. Faint whole copies of the head sit a
        # little out of place while the source sounds (each its own opacity and colour); an onset throws them apart and
        # they drift back over about a second. The bust itself stays clean beneath them.
        sep = event("Sep", 0.8, 1.1, 250.0, hold=60.0)
        for cp, dx, dy, yaw, op_ in (("A", 0.045, 0.012, 7.0, 0.75), ("B", -0.03, -0.006, -5.0, 0.55)):
            s.route(R(LEVEL, gain(cp), op_, comp=0, depth=K("copies"), attackMs=200, decayMs=1600),
                    R(sep, gain(cp), 0.35, comp=0, depth=K("copies"), attackMs=0, decayMs=900),
                    R(LEVEL, ghost(cp) + "position", 0.25 * dx, comp=0, depth=K("copies"), attackMs=800,
                      decayMs=1600),
                    R(sep, ghost(cp) + "position", dx, comp=0, depth=K("copies")),
                    R(sep, ghost(cp) + "position", dy, comp=1, depth=K("copies")),
                    R(sep, ghost(cp) + "rotation", yaw, comp=1, depth=K("copies")))
    if FOIL:
        foil_routes(s, spec, progs + lid_progs(P), opath, LIVE, OPEN, LEVEL, ONSET, K)
    else:
        # every profile: its own wash over the mouth and eyes while it speaks, and a glow on each opening (all pieces)
        for prog in progs:
            s.route(R(LIVE, opath(prog, "washAmount", "remap"), 0.45, comp=3, depth=K("chroma"), attackMs=0,
                      decayMs=0),
                    R(OPEN, opath(prog, "washAmount", "remap"), 0.3, comp=3, depth=K("chroma"), attackMs=0,
                      decayMs=300),
                    R(LEVEL, opath(prog, "washSpread", "remap"), -0.12, comp=2, attackMs=600, decayMs=2500),
                    R("control.palette", opath(prog, "washSpread", "remap"), -0.6, comp=2))
    for part in ("H", "N", "B"):
        s.route(R(OPEN, "fx/%s-glow%s/glow" % (P, part), 0.22, depth=K("chroma"), attackMs=0, decayMs=200))
    # ---- global gain (CC 7, bipolar about its rest): once, on the first head
    if P == "th1":
        s.route(R("control.gain", "camera/exposure/compensation", 1.5, polarity="bipolar"),
                R("control.gain", "post/bloom/intensity", 0.2, polarity="bipolar"))
    for r in s.routes:
        ds = r.get("depthSource", "")
        if ds.startswith("visual.") and ds.endswith("Gate") and "depthMax" not in r:
            r["depthMax"] = 2.0   # knob gates (and tiltGate: rest 0.5 -> depth 1, the authored amount)
