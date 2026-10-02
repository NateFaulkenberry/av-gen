"""The signal classes every Sonic VFX scene speaks in (02-brief-vfx-expansion.md §9, §11, §12).

A scene names the brief's classes -- sustained, melodic, bass, kick, snare, hat -- and the distinct MIDI facts, never
raw bus ids. This table is the one place the classes meet the bus, so a scene written today keeps working when the
engineer's response model (VFX-ARCHITECTURE.md §1.1: `response.*`, the per-note `notes.*`) replaces a stand-in.

Two vocabularies:

  ENGINE = "response"  the engineer's response model (ADR-1060..1062): live kick/snare/hat, conditioned levels, the
                       per-note MIDI signals, and the live Effect Library trigger source `signal`.
  ENGINE = "adr1061"   4bc1a762: live kick/snare/hat (`audio.onsetLow/Mid/High`, ADR-1061) and the Effect Library's
                       live `signal` trigger source (ADR-1060), before the response model: levels and per-note facts
                       still fall back to the Sonic Character and the context.
  ENGINE = "legacy"    what fc99580d (the art agent's first pin) publishes: the Sonic Character, `notes.*` context,
                       `audio.*`. Hits fall back to the broadband onset, so drums read as generic attacks.

Set SONIC_VFX_ENGINE=adr1061 or legacy in the environment to build for an older pin (look development only).
"""
import os

ENGINE = os.environ.get("SONIC_VFX_ENGINE", "response")

# Every value is a bus signal name. `E` marks an event (a one-frame pulse whose value is the strength): route it through
# an envelope chain, or use its `Env` twin.
RESPONSE = {
    # ---- hits (events) and their shaped envelopes (0..1)
    "kick": "response.kick", "kickEnv": "response.kickEnv",
    "snare": "response.snare", "snareEnv": "response.snareEnv",
    "hat": "response.hat", "hatEnv": "response.hatEnv",
    "low": "response.low", "lowEnv": "response.lowEnv",          # any low attack: a kick or a bass note
    "onset": "response.onset", "onsetEnv": "response.onsetEnv",  # any attack, level-free
    "hatRate": "response.hatRate",                               # hats a second / 12: granular activity
    # ---- levels (0..1, conditioned by the Live panel's sensitivity, never auto-gained)
    "bass": "response.bass",              # 30-150 Hz, fast: displacement, pressure
    "level": "response.level",            # full band, fast
    "transient": "response.transient",    # how attacky the sound is now
    "sustain": "response.sustain",        # energy that stays: breathing, ambient light, slow growth
    "flux": "response.flux",              # change, turbulence
    "melodic": "response.melodic",        # the rate of melodic movement
    "pitchAny": "response.pitch",         # latest confident pitch (audio or MIDI), 0..1 over C1-C8
    "intensity": "response.intensity",    # a 12 s follower: macro dynamics
    # ---- MIDI: events
    "note": "response.note", "noteEnv": "response.noteEnv",  # note-on through the transient chain
    "noteOn": "notes.noteOn", "noteOff": "notes.noteOff",
    "release": "notes.release",           # E: a note-off whose strength is the note's duration
    "noteLow": "notes.low", "noteHigh": "notes.high",  # E: note-ons below / above the split key
    "phrase": "notes.phraseStart",
    # ---- MIDI: the latest note (per-note placement and strength)
    "lastPitch": "notes.lastPitch", "lastVelocity": "notes.lastVelocity",
    "interval": "notes.interval", "step": "notes.interval",  # signed: an octave = 1 (ADR-1062 has no notes.step)
    "held": "notes.held",                 # how long the longest sounding note has been held (log 0.05-4 s)
    "channel": "notes.channel",
    # ---- MIDI: the playing (context, 0..1)
    "pitch": "notes.pitch",               # pitch centre of the sounding / recent notes
    "polyphony": "notes.polyphony", "active": "notes.active", "density": "notes.density",
    "rhythm": "notes.rhythm", "velocity": "notes.velocity", "velocitySpread": "notes.velocitySpread",
    "range": "notes.range", "motion": "notes.motion", "direction": "notes.direction",
    "duration": "notes.duration", "legato": "notes.legato", "regularity": "notes.regularity",
    "chord": "notes.chord", "tension": "notes.tension", "repetition": "notes.repetition",
    "phraseShare": "notes.phrase",
    # ---- the sound's character (timbre: medium tier, and .slow for identity)
    "energy": "sonic.energy", "brightness": "sonic.brightness", "warmth": "sonic.warmth",
    "roughness": "sonic.roughness", "sharpness": "sonic.sharpness", "smoothness": "sonic.smoothness",
    "harmonicity": "sonic.harmonicity", "inharmonicity": "sonic.inharmonicity", "density_t": "sonic.density",
    "movement": "sonic.movement", "transientT": "sonic.transient",
    "energySlow": "sonic.energy.slow", "brightnessSlow": "sonic.brightness.slow",
    "warmthSlow": "sonic.warmth.slow", "roughnessSlow": "sonic.roughness.slow",
    # ---- the beat clock (live tracker or file grid)
    "beat": "beat.pulse", "beatPhase": "beat.phase", "bar": "beat.bar",
    "time": "time.seconds",
}

# What the first pin can offer instead. A hit with no class falls back to the broadband onset (drums read as attacks
# without telling kick from hat); a per-note fact falls back to the context's.
LEGACY_OVERRIDES = {
    "kick": "audio.onset", "kickEnv": "audio.onset",
    "snare": "audio.onset", "snareEnv": "audio.onset",
    "hat": "audio.onset", "hatEnv": "audio.onset",
    "low": "audio.onset", "lowEnv": "audio.onset",
    "onset": "audio.onset", "onsetEnv": "audio.onset",
    "hatRate": "audio.onsetRate",
    "bass": "audio.bass", "level": "audio.rms", "transient": "sonic.transient",
    "sustain": "sonic.energy.slow", "flux": "audio.spectralFlux", "melodic": "notes.motion",
    "pitchAny": "notes.pitch", "intensity": "sonic.energy.slow",
    "note": "notes.noteOn", "noteEnv": "notes.noteOn", "release": "notes.noteOff",
    "noteLow": "notes.noteOn", "noteHigh": "notes.noteOn",
    "lastPitch": "notes.pitch", "lastVelocity": "notes.velocity", "interval": "notes.motion",
    "step": "notes.direction", "held": "notes.duration", "channel": "notes.velocity",
    "velocitySpread": "notes.velocity",
}

# 4bc1a762 (ADR-1060/1061): the drum classes are real and live; everything else as legacy.
ADR1061_OVERRIDES = dict(LEGACY_OVERRIDES)
ADR1061_OVERRIDES.update({
    "kick": "audio.onsetLow", "kickEnv": "audio.onsetLow",
    "snare": "audio.onsetMid", "snareEnv": "audio.onsetMid",
    "hat": "audio.onsetHigh", "hatEnv": "audio.onsetHigh",
})

# Events: route them through an envelope stage (or an attack/decay chain with attack 0).
EVENTS = {"kick", "snare", "hat", "low", "onset", "note", "noteOn", "noteOff", "release", "noteLow", "noteHigh",
          "phrase", "beat", "transientT"}


def S(name):
    """The bus id for a signal class or MIDI fact."""
    if ENGINE == "legacy" and name in LEGACY_OVERRIDES:
        return LEGACY_OVERRIDES[name]
    if ENGINE == "adr1061" and name in ADR1061_OVERRIDES:
        return ADR1061_OVERRIDES[name]
    if name in RESPONSE:
        return RESPONSE[name]
    if "." in name:  # already a bus id (visual.*, fx.*, macro.*)
        return name
    raise KeyError(f"unknown signal class {name!r}")


def is_event(name):
    return name in EVENTS


def live_triggers_available():
    """The Effect Library's `signal` trigger source (VFX-ARCHITECTURE.md §1.3) exists only in the response engine."""
    return ENGINE in ("response", "adr1061")
