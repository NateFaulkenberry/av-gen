#!/usr/bin/env python3
"""The Sonic Live demo (ADR-1025; 01-brief-live.md PARTS 15-16: the live demo's art and the live art direction).

    python3 tools/sonic_live_project.py

Writes examples/sonic-garden/sonic-live.json and its scene, examples/sonic-garden/sonic-live.scene.json, from the
Sonic Garden master (examples/sonic-garden/sonic-garden.json and .scene.json), beside them so every relative path
still resolves. Re-run it after changing the master (tools/sonic_garden_look.py). The master and its review material
are not touched: the live demo diverges from the file-based garden only here.

What a live instrument cannot have is taken out: the audio file and the MIDI file (the sound and the notes come from
the live inputs), and the master's 21.5 s keyed push-in (a performance has no length). `sonic.live: true` makes the
live editor turn live input on when it opens the project.

What live play needs is put in (the live art pass; LIVE-ART-NOTES.md has the measurements):

  1. THE CHARACTER'S TIME CONSTANTS. The world's identity (the slow tier the families read) follows a new sound in
     about 2 s instead of 4-5, so changing the patch changes the world while you listen; the filter's channel (the
     medium tier of brightness) follows the cutoff in about 50 ms rising and 200 ms falling, where the master's took
     100 and 400. The Live panel's Smoothing multiplies all of these (1x = this tuning).
  2. BRIGHTNESS REACHES DOWN TO A BASS NOTE'S CLOSED FILTER. The master's brightness started at a 250 Hz centroid,
     so the dark half of a filter sweep on a low note (120 Hz - 1.2 kHz) read as no change at all. Here it starts at
     160 Hz, and the rolloff at 400 Hz.
  3. THE FAMILIES, RETUNED ON SYNTH PATCHES (the probe's pad, keys, pluck, lead, FM bell, distorted bass, noise
     perc, and the filter and drive ramps). The master's were tuned on four rendered files and read a warm pad and
     warm chords as glass and a distorted bass as glass: brightness led the crystalline family, and roughness had to
     be extreme to register. Now:
       organic      warm, smooth, dark, clean, harmonic (the melody's movement no longer counts against it)
       crystalline  bright and clean: brightness^1.8 x (1 - roughness)^3
       chaotic      roughness above about 0.15, led by roughness, with a little attack and unsteadiness
  4. MASS (heavy against light, which splits the chaotic family into its two faces) also reads the register: a
     distorted bass line is heavy (the pressure deep), a distorted high lead is light (the strike field).
  5. TWO CONTINUOUS CHANNELS, fast (tens to hundreds of ms), in every world, beside the slow identity:
       glow   the filter: the light, the colour and the fine detail open with the sound's brightness
       grit   distortion: roughness past a clean synth's (about 0.07) roughens and cracks the surfaces, sets the
              forms buzzing (a fast noise deformation: tendrils kink, petals crumple, rings go ragged, crystals
              warp) and throws off fragments -- the chaotic world's blades, in the current world's colour -- long
              before the world itself turns chaotic.
  6. THE REGISTER SPLIT for a keyboard (the master's was centred on its test phrase): high from about F3 to D5,
     low from A2 to D4.
  7. A NOTE IS A GESTURE, NOT A FLASH. The organic swell rises over 120-250 ms instead of snapping on; the rings of
     the observatory swing on a note and ring down; a note's light is held by the sound itself (the release: a pad's
     long tail keeps the garden lit, a pluck's dies with it); the heart light's per-note flash is smaller.
  8. RELEASE -> TRAILS: the temporal echo is small by default (the master's 0.5 ghosted the rippling tendrils) and
     grows with held, legato playing.
  9. THE WAITING WORLD: before any sound, a composed night -- a cold horizon line, the ground's veins breathing
     faintly -- instead of a grey plain.
 10. THE CAMERA drifts slowly (a 64 s loop, about 1.6 m of travel round the master's mid-move framing), so the
     world keeps its depth; it does not follow the music. Weight lowers it only in the heavy world.
"""
import copy
import json
import math
import os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GARDEN = os.path.join(REPO, "examples", "sonic-garden")
MASTER = os.path.join(GARDEN, "sonic-garden.json")
MASTER_SCENE = os.path.join(GARDEN, "sonic-garden.scene.json")
OUT = os.path.join(GARDEN, "sonic-live.json")
OUT_SCENE = os.path.join(GARDEN, "sonic-live.scene.json")

# The camera's hold: the master's key at this second (the middle of its push-in).
HOLD_SECONDS = 10.75
RIG = "lightrig/SonicGarden"


def value_at(track, seconds):
    keys = sorted(track["keys"], key=lambda k: k["time"])
    for key in keys:
        if abs(key["time"] - seconds) < 1e-6:
            return key["value"]
    return min(keys, key=lambda k: abs(k["time"] - seconds))["value"]


# ------------------------------------------------------------------------------------------------ 1-2. the character
# Every dimension's slow tier (what the families read) follows in about 2 s; the medium tiers of the channels a
# player turns a knob on (brightness: the filter; roughness: the drive) are quicker.
LIVE_TIMES = {
    # energy keeps the master's 3 s slow tier: it decides when a rest becomes silence (the waiting world), and a
    # player's pause of a few seconds should keep the world
    "energy": {"slow": 3.0},
    "brightness": {"attack": 0.05, "release": 0.2, "slow": 2.0},
    "warmth": {"slow": 2.2},
    "roughness": {"attack": 0.05, "release": 0.25, "slow": 1.8},
    "sharpness": {"slow": 2.0},
    "smoothness": {"slow": 2.0},
    "harmonicity": {"slow": 2.2},
    "inharmonicity": {"slow": 2.2},
    "density": {"slow": 2.2},
    "complexity": {"slow": 2.2},
    "stability": {"slow": 2.2},
    "movement": {"slow": 2.0},
}
LIVE_BRIGHTNESS_TERMS = [
    {"feature": "centroid", "lo": 160, "hi": 4000, "log": True, "weight": 1.0},
    {"feature": "rolloff", "lo": 400, "hi": 10000, "log": True, "weight": 0.75},
    {"feature": "highRatio", "lo": -40, "hi": -6, "weight": 1.0},
]


def live_character(master_character):
    ch = copy.deepcopy(master_character or {})
    dims = ch.setdefault("dimensions", {})
    for name, times in LIVE_TIMES.items():
        dims.setdefault(name, {}).update(times)
    dims.setdefault("brightness", {})["terms"] = LIVE_BRIGHTNESS_TERMS
    return ch


# ------------------------------------------------------------------------------------------------ 3-6. the mappings
def M(name, inputs, combine="mean", bias=0.0, gain=1.0, curve=1.0, group=None):
    o = {"name": name, "combine": combine,
         "inputs": [dict({"signal": s, "weight": w}, **({"invert": True} if inv else {})) for s, w, inv in inputs]}
    if group:
        o["group"] = group
    if bias:
        o["bias"] = bias
    if gain != 1.0:
        o["gain"] = gain
    if curve != 1.0:
        o["curve"] = curve
    return o


# Replacements for the master's garden mappings (by name), and new ones (appended). Products are ANDs; a mapping is
# x * gain + bias, clamped, then raised to the curve.
LIVE_GARDEN = [
    # warm AND smooth AND dark AND clean AND harmonic. The master's steadiness term (stability^0.5) separated its
    # file pad from its file bell; live, any melody moves the centroid, so a pad played as a line read unsteady and
    # lost the garden to the glass. Here steadiness is a hint (^0.1).
    M("organic", [("sonic.warmth.slow", 0.7, False), ("sonic.smoothness.slow", 0.5, False),
                  ("sonic.brightness.slow", 1.4, True), ("sonic.roughness.slow", 2.5, True),
                  ("sonic.inharmonicity.slow", 1.0, True), ("sonic.stability.slow", 0.1, False)],
      "product", group="family"),
    # bright AND clean. The master's bright^0.5 x inharmonic^0.5 made any moderately bright saw glass (a warm pad,
    # a distorted bass); brightness^1.8 keeps the warm sounds out, and (1 - roughness)^3 hands a rough bright sound
    # to the chaotic family instead.
    M("crystalline", [("sonic.brightness.slow", 1.8, False), ("sonic.roughness.slow", 3.0, True)],
      "product", group="family"),
    # rough, led by roughness: a clean synth sits at 0.05-0.1, a fully open saw or a single driven note at 0.2-0.3,
    # a drive after the filter or a driven chord at 0.3-0.4, noise at 0.9. The threshold sits between the second
    # and third: the top of a filter sweep is bright glass (buzzing, through grit), a driven chord or riff is the
    # chaotic world. The master's mean of roughness, sharpness and unsteadiness gave a legato pad a third of the
    # chaotic weight (its note rises count as attack); here attack and unsteadiness are 0.3 each against
    # roughness's 3.
    M("chaotic", [("sonic.roughness.slow", 3.0, False), ("sonic.sharpness.slow", 0.3, False),
                  ("sonic.smoothness.slow", 0.3, True)], "mean", -0.55, 3.0, group="family"),
    # heavy: loud AND full AND pitched AND low. The register term is what makes a distorted bass line the pressure
    # deep and a distorted high lead the strike field (notes.pitch holds the last note through rests).
    # (Loudness counts only a little -- energy^0.5 -- so a heavy world does not turn light in a rest.)
    M("mass", [("sonic.energy.slow", 0.5, False), ("sonic.density.slow", 2.0, False),
               ("sonic.harmonicity.slow", 3.0, False), ("notes.pitch", 1.5, True)], "product", -0.3, 5.0, 1.5),
    # the filter: the sound's brightness alone, medium tier, over its whole live range (on a held A2 swept from
    # 120 Hz to 9 kHz: 0.1 by 350 Hz, 0.3 by 1 kHz, 0.55 by 2 kHz, 0.8 at 9 kHz)
    M("glow", [("sonic.brightness", 1.0, False)], "mean", -0.03, 1.25, 0.8),
    # distortion: roughness that is not a bell's (the roughness dimension also counts inharmonic partials, so an FM
    # bell reads 0.25 rough; x (1 - inharmonicity)^2 keeps it clean), while the sound sounds (x energy^0.4: it
    # falls with the note instead of holding through the rest, as the gated roughness does). A clean synth reads
    # 0-0.1, a raspy open saw 0.3-0.4, a drive after the filter 1.
    M("grit", [("sonic.roughness", 1.0, False), ("sonic.energy", 0.4, False), ("sonic.inharmonicity", 2.0, True)],
      "product", -0.3, 6.5),
    # register for a keyboard: notes.pitch is 0 at C1 and 1 at C8 (84 semitones). high runs 0 -> 1 from F3 to D5
    # (MIDI 53-74), low 1 -> 0 from A2 to D4 (45-62), so a chord in the middle of the keyboard is mostly low, a
    # melody over middle C climbs from one to the other.
    M("high", [("notes.pitch", 1.0, False)], "mean", -1.4, 4.0),
    M("low", [("notes.pitch", 1.0, True)], "mean", -2.7, 4.95),
]
# The second source reads this frame's garden outputs.
LIVE_WORLD = [
    # fragments: the chaotic world's blades, or distortion's shards in any world (x 0.55: fragments, not a world)
    M("fragments", [("visual.chaotic", 1.0, False), ("visual.grit", 0.55, False)], "max"),
]
FAMILY_SHARPNESS = 3.0


# ------------------------------------------------------------------------------------------------ routes
def R(src, target, amount, op="add", comp=None, depth=None, **chain):
    r = {"source": src, "target": target, "amount": amount, "op": op, "polarity": "unipolar"}
    if comp is not None:
        r["component"] = comp
    if depth:
        r["depthSource"] = depth
    if chain:
        r["chain"] = chain
    return r


def palette(target, colors, **chain):
    out = []
    for fam, rgb in colors.items():
        for c, v in enumerate(rgb):
            if abs(v) > 1e-6:
                out.append(R("visual." + fam, target, v, comp=c, **chain))
    return out


def scalar(target, values, **chain):
    return [R("visual." + fam, target, v, **chain) for fam, v in values.items() if abs(v) > 1e-9]


SLOW = {"attackMs": 500, "decayMs": 1000}   # the master's 800/1600: with the 2 s slow tier, a world in ~3 s
GLOW = {"attackMs": 60, "decayMs": 250}     # the filter channel's routes (the master's 150/500)
GRIT = {"attackMs": 40, "decayMs": 300}     # the distortion channel's routes


def retime(route):
    """The master's route timing, for live play."""
    ch = route.get("chain")
    if not ch:
        return route
    if ch.get("attackMs") == 800 and ch.get("decayMs") == 1600:  # SLOW and GROW: the family blends
        ch["attackMs"], ch["decayMs"] = SLOW["attackMs"], SLOW["decayMs"]
    if route["source"] == "visual.glow" and ch.get("attackMs") == 150:
        ch["attackMs"], ch["decayMs"] = GLOW["attackMs"], GLOW["decayMs"]
    return route


def drop(route):
    """Master routes the live demo replaces (the replacements are in live_routes)."""
    s, t = route["source"], route["target"]
    if s == "visual.mass" and t in ("camera/position", "camera/target"):
        return True  # weight lowers the camera only in the heavy world (a chord is dense, not heavy)
    if s == "visual.chaotic" and t == "procedural/shards/source/scale":
        return True  # the blades grow from `fragments` (chaotic, or grit in any world)
    if s == "notes.noteOn" and route.get("depthSource") in ("visual.organic", "visual.organicHi", "visual.organicLo") \
            and t.endswith("material/emissive"):
        return True  # the organic swell: re-authored with an attack
    if s == "notes.noteOn" and t == "lights/heart/intensity" and route.get("depthSource") == "visual.crystalline":
        return True  # a smaller per-note flash
    if s == "notes.noteOn" and t == "lights/note/intensity":
        return True  # re-authored below
    if t in ("temporal/echo/strength", "temporal/echo/decay"):
        return True  # trails: small by default, grown by legato playing
    if s == "visual.lift":
        return True  # re-spanned for a keyboard
    if s == "notes.pitch" and t == "lights/note/position":
        return True
    return False


def live_routes():
    out = []
    # ---- 4. the camera: weight lowers it only in the heavy world; the blades grow from `fragments`
    out.append(R("visual.tectonic", "camera/position", -0.9, comp=1, **SLOW))
    out.append(R("visual.tectonic", "camera/target", 0.4, comp=1, **SLOW))
    out.append(R("visual.fragments", "procedural/shards/source/scale", 1.0, op="multiply", gain=1.25, offset=-0.25,
                 clampEnabled=True, clampMin=0.0, clampMax=1.0, attackMs=250, decayMs=900))

    # ---- 5a. glow: the filter opens the world's light and detail (on top of the master's glow routes)
    out.append(R("visual.glow", "procedural/hero/material/emissive", 0.35, depth="visual.crystalline", **GLOW))
    out.append(R("visual.glow", "procedural/gills/material/emissive", 0.6, depth="visual.organic", **GLOW))
    out.append(R("visual.glow", "procedural/buds/material/emissive", 0.8, depth="visual.organic", **GLOW))
    for n in ("halos", "meridians"):
        out.append(R("visual.glow", "procedural/%s/material/emissive" % n, 0.5, depth="visual.crystalline", **GLOW))
    out.append(R("visual.glow", "lights/heart/intensity", 3.0, **GLOW))
    out.append(R("visual.glow", "procedural/seed/deform/1/amount", 0.04, **GLOW))  # the seed's skin wrinkles finer
    # the colour: a closed filter is ember, an open one white-gold (organic) or white (crystalline)
    out += palette("lights/heart/color", {"glow": (0.0, 0.25, 0.45)}, **GLOW)
    out.append(R("visual.glow", "post/grade/temperature", -0.12, **GLOW))
    out.append(R("visual.glow", "post/bloom/intensity", 0.25, **GLOW))
    out.append(R("visual.glow", "particles/spores/spawnRate", 40.0, depth="visual.organic", **GLOW))

    # ---- 5b. grit: distortion roughens, sets the forms buzzing, lights the cracks, throws off fragments (every
    # deformer named here is added by the live scene at amount 0, so a clean sound is the master's look)
    for node, k, amount in (("seed", 1, 0.22), ("hero", 1, 0.35), ("stalks", 2, 0.16), ("petals", 4, 0.07),
                            ("outerpetals", 4, 0.09), ("caps", 2, 0.06), ("facets", 1, 0.22), ("prisms", 1, 0.16),
                            ("spires", 1, 0.12), ("halos", 1, 0.12), ("meridians", 1, 0.12)):
        out.append(R("visual.grit", "procedural/%s/deform/%d/amount" % (node, k), amount, **GRIT))
    for node in ("seed", "hero", "facets", "petals", "outerpetals", "stalks"):
        out.append(R("visual.grit", "procedural/%s/material/roughness" % node, 0.35, **GRIT))
    out.append(R("visual.grit", "procedural/ground/material/emissive", 0.12, **GRIT))
    out.append(R("visual.grit", "procedural/hero/material/emissive", 0.4, **GRIT))
    out.append(R("visual.grit", "procedural/facets/material/emissive", 0.3, depth="visual.crystalline", **GRIT))
    out.append(R("visual.grit", "procedural/seed/material/emissive", 0.4, depth="visual.organic", **GRIT))
    out.append(R("visual.grit", "procedural/shards/material/emissive", 0.6, **GRIT))
    out.append(R("visual.grit", "procedural/shards/deform/1/amount", 0.35, **GRIT))
    out.append(R("visual.grit", "particles/sparks/spawnRate", 60.0, **GRIT))
    out.append(R("visual.grit", "particles/spores/turbulence", 2.5, **GRIT))
    out.append(R("visual.grit", "particles/glints/speed", 3.0, **GRIT))
    # and the image hardens a little: contrast, and a trace of colour fringing at the edges
    out.append(R("visual.grit", "post/grade/contrast", 0.12, **GRIT))
    out.append(R("visual.grit", "post/lens/chromaticAberration", 0.25, **GRIT))

    # ---- 7. a note is a gesture. The organic swell rises (120-250 ms) ...
    for tgt, amount, depth, a, d in (("seed", 0.8, "visual.organic", 150, 1400),
                                     ("stalks", 1.6, "visual.organicHi", 120, 1500),
                                     ("gills", 1.4, "visual.organicLo", 180, 1700),
                                     ("caps", 0.8, "visual.organicLo", 180, 1700),
                                     ("petals", 0.7, "visual.organic", 250, 1500)):
        out.append(R("notes.noteOn", "procedural/%s/material/emissive" % tgt, amount, depth=depth, attackMs=a,
                     decayMs=d))
    # ... and the sound holds the light up while it sounds and lets it go with its own release (energy's medium
    # tier falls about 0.3 s after the sound does): a pad's tail keeps the garden lit, a pluck's dies with it
    for tgt, amount in (("seed", 0.6), ("stalks", 0.35), ("petals", 0.3), ("outerpetals", 0.25), ("gills", 0.5)):
        out.append(R("visual.energy", "procedural/%s/material/emissive" % tgt, amount, depth="visual.organic",
                     attackMs=80, decayMs=400))
    for tgt, amount in (("prisms", 0.25), ("spires", 0.3), ("facets", 0.25)):
        out.append(R("visual.energy", "procedural/%s/material/emissive" % tgt, amount, depth="visual.crystalline",
                     attackMs=40, decayMs=300))
    # the lotus opens a little on each note and closes again: it breathes with the playing
    out.append(R("notes.noteOn", "procedural/petals/deform/1/amount", -0.08, depth="visual.organic", attackMs=220,
                 decayMs=1300))
    out.append(R("notes.noteOn", "procedural/outerpetals/deform/1/amount", -0.07, depth="visual.organic",
                 attackMs=260, decayMs=1500))
    # the observatory's rings swing on a note and ring down (high notes the ecliptic, low the meridians)
    out.append(R("notes.noteOn", "procedural/halos/transform/rotation", 9.0, comp=0, depth="visual.crystalHi",
                 attackMs=50, decayMs=900))
    out.append(R("notes.noteOn", "procedural/meridians/transform/rotation", -8.0, comp=2, depth="visual.crystalLo",
                 attackMs=50, decayMs=900))
    out.append(R("notes.noteOn", "lights/heart/intensity", 3.5, depth="visual.crystalline", attackMs=0,
                 decayMs=240))
    # the note light: where the melody is, in the world's colour, answering in the world's tempo
    out.append(R("notes.pitch", "lights/note/position", 4.0, comp=1, gain=2.2, offset=-0.55, clampEnabled=True,
                 clampMin=0.0, clampMax=1.0, attackMs=60, decayMs=60))
    out.append(R("notes.noteOn", "lights/note/intensity", 6.0, depth="visual.organic", attackMs=90, decayMs=1200))
    out.append(R("notes.noteOn", "lights/note/intensity", 8.0, depth="visual.crystalline", attackMs=0,
                 decayMs=220))
    out.append(R("visual.energy", "lights/note/intensity", 3.0, depth="visual.organic", attackMs=80, decayMs=400))

    # ---- pitch -> height: the core, the gem, the rings and the heart follow the melody's register, spanned for a
    # keyboard (pitch 0.25 -> 0 m, 0.70 -> 1.1 m: A2 to A5)
    for n in ("procedural/hero/transform/position", "procedural/seed/transform/position",
              "procedural/facets/transform/position", "procedural/halos/transform/position",
              "procedural/meridians/transform/position", "lights/heart/position"):
        out.append(R("visual.lift", n, 1.1, comp=1, gain=2.2, offset=-0.55, clampEnabled=True, clampMin=0.0,
                     clampMax=1.0, attackMs=180, decayMs=400))

    # ---- 8. release -> trails: small by default, grown by held, legato playing
    out += scalar("temporal/echo/strength", {"organic": 0.15, "crystalline": 0.08}, **SLOW)
    out += scalar("temporal/echo/decay", {"organic": 0.8, "crystalline": 0.55}, **SLOW)
    out.append(R("visual.sustain", "temporal/echo/strength", 0.3, attackMs=600, decayMs=1200))

    # ---- 9. the waiting world (silence): a cold night, a horizon line, the ground's veins breathing faintly
    quiet = {"attackMs": 300, "decayMs": 1500}
    out += palette("env/sky/horizonColor", {"silence": (0.006, 0.009, 0.02)}, **quiet)
    out += palette("env/sky/zenithColor", {"silence": (0.0008, 0.001, 0.003)}, **quiet)
    out += palette("scene/fogColor", {"silence": (0.003, 0.004, 0.008)}, **quiet)
    out += palette(RIG + "/ambientColor", {"silence": (0.05, 0.07, 0.16)}, **quiet)
    out += palette("procedural/ground/material/emissiveColor", {"silence": (0.35, 0.55, 1.0)}, **quiet)
    out.append(R("visual.silence", "procedural/ground/material/emissive", 0.05, **quiet))
    out.append(R("visual.silence", RIG + "/key/intensity", -0.7, **quiet))
    out.append(R("visual.silence", RIG + "/rim/intensity", 0.3, **quiet))
    out.append(R("visual.silence", RIG + "/rim/temperature", 6000.0, **quiet))
    return out


# ------------------------------------------------------------------------------------------------ the live scene
# Deformers the live demo adds (all at amount 0: a clean sound is the master's look; grit drives them). Each is a
# fast noise, so a distorted sound sets the forms buzzing rather than bending them.
def buzz(scale, speed, seed):
    return {"kind": "noise", "amount": 0.0, "scale": scale, "speed": speed, "seed": seed}


LIVE_DEFORMERS = {
    # node: deformers appended (their 1-based index is what the grit routes name)
    "stalks": [buzz(2.6, 3.5, 61)],          # deform/2: the tendrils kink
    "petals": [buzz(3.0, 2.5, 62)],          # deform/4: the petals crumple at the edges
    "outerpetals": [buzz(2.6, 2.5, 63)],     # deform/4
    "caps": [buzz(2.2, 2.0, 64)],            # deform/2
    "facets": [buzz(0.9, 1.8, 65)],          # deform/1: the gem warps
    "prisms": [buzz(1.1, 2.2, 66)],          # deform/1
    "spires": [buzz(1.3, 2.2, 67)],          # deform/1
    "halos": [buzz(1.6, 2.8, 68)],           # deform/1: the rings go ragged
    "meridians": [buzz(1.6, 2.8, 69)],       # deform/1
}


def live_scene():
    with open(MASTER_SCENE) as f:
        s = json.load(f)
    s["name"] = "sonic-live"
    seen = set()
    for node in s["nodes"]:
        extra = LIVE_DEFORMERS.get(node["name"])
        if extra:
            p = node["procedural"]
            p["deformers"] = list(p.get("deformers", [])) + copy.deepcopy(extra)
            seen.add(node["name"])
    missing = set(LIVE_DEFORMERS) - seen
    if missing:
        raise SystemExit(f"the master scene has no node(s) {sorted(missing)}")
    return s


# ------------------------------------------------------------------------------------------------ 10. the camera
def camera_drift(hold_pos, hold_tgt):
    """A slow loop round the hold: 64 s, about 1.6 m of travel, the target swaying a few centimetres."""
    period = 64.0
    keys_p, keys_t = [], []
    for i in range(9):
        t = period * i / 8.0
        a = 2.0 * math.pi * i / 8.0
        dp = (0.8 * math.sin(a), 0.12 * math.sin(2.0 * a), 0.55 * (1.0 - math.cos(a)) - 0.3)
        dt = (0.12 * math.sin(a + 0.6), 0.05 * math.sin(2.0 * a + 1.0), 0.0)
        keys_p.append({"time": t, "value": [round(hold_pos[k] + dp[k], 4) for k in range(3)], "interp": "smooth"})
        keys_t.append({"time": t, "value": [round(hold_tgt[k] + dt[k], 4) for k in range(3)], "interp": "smooth"})
    return keys_p, keys_t, period


def main():
    with open(MASTER) as f:
        doc = json.load(f)
    live = copy.deepcopy(doc)
    live["app"] = {"name": "Sonic Live"}
    assets = live.setdefault("assets", {})
    assets.pop("audio", None)
    assets["scene"] = {"kind": "composition", "path": os.path.basename(OUT_SCENE)}
    sonic = live.setdefault("sonic", {})
    sonic.pop("notes", None)
    sonic["live"] = True
    sonic["character"] = live_character(sonic.get("character"))

    # the interpreter: the master's mappings with the live replacements, and the live additions appended
    for src in live["sources"]:
        maps = src["settings"]["mappings"]
        repl = LIVE_GARDEN if src["name"] == "garden" else LIVE_WORLD if src["name"] == "world" else []
        by_name = {m["name"]: m for m in repl}
        maps[:] = [by_name.pop(m["name"], m) for m in maps] + list(by_name.values())
        if src["name"] == "garden":
            src["settings"].setdefault("groups", {})["family"] = {"sharpness": FAMILY_SHARPNESS}
    routes = [retime(copy.deepcopy(r)) for r in live["routes"] if not drop(r)]
    routes += live_routes()
    live["routes"] = routes

    # the camera: a slow drift round the master's mid-move framing
    tracks = live.get("timeline", {}).get("tracks", [])
    hold = {t["target"]: value_at(t, HOLD_SECONDS) for t in tracks
            if t.get("target") in ("camera/position", "camera/target")}
    kp, kt, period = camera_drift(hold["camera/position"], hold["camera/target"])
    for track in tracks:
        if track.get("target") == "camera/position":
            track["keys"], track["loopLength"] = kp, period
        elif track.get("target") == "camera/target":
            track["keys"], track["loopLength"] = kt, period
    live.setdefault("render", {})["path"] = "renders/sonic-live"

    with open(OUT_SCENE, "w") as f:
        json.dump(live_scene(), f, indent=1)
        f.write("\n")
    with open(OUT, "w") as f:
        json.dump(live, f, indent=1)
        f.write("\n")
    print(f"wrote {os.path.relpath(OUT, REPO)} ({len(routes)} routes) and {os.path.relpath(OUT_SCENE, REPO)}")


if __name__ == "__main__":
    main()
