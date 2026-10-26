#!/usr/bin/env python3
"""Writes the Audio Response Lab's project and scene (examples/audio-response-lab/, ADR-1720).

    python3 tools/audio_lab/make_lab_scene.py            # rewrites both files from this script

The scene is a fixed diagnostic board, not art: dark neutral ground, unlit emissive boxes, one torus, text labels.
Every indicator is a ROUTE from a production signal to a parameter, so what the board shows is what the response
system published (the route chain is the identity unless a probe says otherwise). The layout table below is the one
source of truth: tools/audio_lab/compose.py reads the same `LAYOUT` (via the project's `audioLab` block) to know
where each indicator is on screen.

Panels (left to right, top then bottom):
  A  ENERGY   six meters: response.level, response.bass, audio.midLevel, audio.trebleLevel, response.sustain,
              response.onsetEnv. Fill height = the signal (0..1), unsmoothed.
  B  EVENTS   lamps: response.onset, response.low, beat.pulse, response.note (MIDI); three register bars
              response.onsetLow/Mid/High (the latest onset's register, held).
  D  BEAT     a dial: the needle is beat.phase, the outer tick beat.bar; bars for audio.tempoConfidence and beat.bpm.
  C  SOURCES  one column per conventional source key (drums, bass, synth, pad): an ACTIVE lamp over a red
              'no data' lamp (stem.<k>.active), a level bar (stem.<k>.level), an onset lamp (stem.<k>.onset); the
              drums column adds kick/snare/hat lamps (stem.drums.kick/snare/hat: a drums-role source only).
  E  PROBES   response-mapping probes with real chains: a sphere scaled by smoothed response.bass, a plate that
              flashes on response.onset (a 120 ms route envelope), a colour panel on sonic.brightness, a cube on
              stem.bass.level (smoothed), a cube on MIDI CC 1 (control.labCC1).
"""

import json
import math
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
OUT = REPO / "examples" / "audio-response-lab"

FONT = {"family": "Helvetica Neue", "weight": 0.5, "italic": False}
DIM = 0.10       # the baseline glow of every indicator's frame
LAMP_BASE = 0.07  # a lamp at rest: visible, unmistakably off

nodes = []
routes = []
layout = {}  # indicator name -> {"kind", "signal", "rect": [x0, y0, x1, y1] in world units}


def box(name, pos, size, colour, emissive, rot=(0.0, 0.0, 0.0)):
    nodes.append({"name": name, "kind": "procedural", "procedural": {
        "source": {"kind": "box", "size": list(size)},
        "distribution": {"kind": "single"},
        "distributionTransform": {"position": list(pos), "rotation": list(rot), "scale": [1.0, 1.0, 1.0]},
        "material": {"baseColor": [0, 0, 0], "emissiveColor": list(colour), "emissiveIntensity": emissive,
                     "roughness": 1.0, "metallic": 0.0}}})


def sphere(name, pos, radius, colour, emissive):
    nodes.append({"name": name, "kind": "procedural", "procedural": {
        "source": {"kind": "sphere", "radius": radius, "segments": 32, "rings": 16},
        "distribution": {"kind": "single"},
        "distributionTransform": {"position": list(pos), "rotation": [0, 0, 0], "scale": [1.0, 1.0, 1.0]},
        "material": {"baseColor": [0, 0, 0], "emissiveColor": list(colour), "emissiveIntensity": emissive,
                     "roughness": 1.0, "metallic": 0.0}}})


def torus(name, pos, major, minor, colour, emissive):
    nodes.append({"name": name, "kind": "procedural", "procedural": {
        "source": {"kind": "torus", "majorRadius": major, "minorRadius": minor, "majorSegments": 64,
                   "minorSegments": 8},
        "distribution": {"kind": "single"},
        "distributionTransform": {"position": list(pos), "rotation": [90.0, 0, 0], "scale": [1.0, 1.0, 1.0]},
        "material": {"baseColor": [0, 0, 0], "emissiveColor": list(colour), "emissiveIntensity": emissive,
                     "roughness": 1.0, "metallic": 0.0}}})


def text(name, pos, words, size=0.32, colour=(0.75, 0.75, 0.75), emissive=0.9, align=1):
    nodes.append({"name": name, "kind": "procedural", "procedural": {
        "source": {"kind": "text", "text": words, "textSize": size, "textDepth": 0.0, "textAlign": align,
                   "font": FONT},
        "distribution": {"kind": "single"},
        "distributionTransform": {"position": list(pos), "rotation": [0, 0, 0], "scale": [1.0, 1.0, 1.0]},
        "material": {"baseColor": [0, 0, 0], "emissiveColor": list(colour), "emissiveIntensity": emissive,
                     "roughness": 1.0, "metallic": 0.0}}})


def route(source, target, op="replace", component=-1, amount=1.0, chain=None, optional=False):
    r = {"source": source, "target": target, "amount": amount, "op": op, "polarity": "unipolar",
         "component": component, "chain": chain or {}}
    if optional:
        r["optional"] = True
    routes.append(r)


def meter(name, x, y0, height, signal, colour, label, optional=False, width=0.9):
    """A bottom-anchored bar: fill height = signal * height, identity chain (scale and position both replaced)."""
    box(f"{name}Frame", (x, y0 + height / 2, -0.05), (width + 0.12, height + 0.12, 0.02), (0.5, 0.5, 0.5), DIM)
    for q in (0.25, 0.5, 0.75):
        box(f"{name}Tick{int(q * 100)}", (x, y0 + q * height, 0.06), (width + 0.3, 0.025, 0.02), (0.5, 0.5, 0.5), 0.12)
    box(f"{name}Fill", (x, y0, 0.0), (width, 1.0, 0.1), colour, 0.9)
    # Authored EMPTY: a signal the bus does not carry (no such source) leaves the route unbound and the meter at its
    # authored value, which must read as nothing, never as a stale or invented level.
    nodes[-1]["procedural"]["distributionTransform"]["scale"] = [1.0, 0.001, 1.0]
    remap = {"remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0, "clampEnabled": True, "clampMin": 0.0,
             "clampMax": 1.0}
    route(signal, f"procedural/{name}Fill/transform/scale", component=1, optional=optional,
          chain=dict(remap, remapOutMin=0.001, remapOutMax=height))
    route(signal, f"procedural/{name}Fill/transform/position", component=1, optional=optional,
          chain=dict(remap, remapOutMin=y0, remapOutMax=y0 + height / 2))
    text(f"{name}Label", (x, y0 - 0.45, 0.0), label, 0.28)
    layout[name] = {"kind": "meter", "signal": signal, "rect": [x - width / 2, y0, x + width / 2, y0 + height],
                    "param": f"procedural/{name}Fill/transform/scale", "component": 1, "scale": height}


def lamp(name, x, y, size, signal, colour, label, gain=0.9, optional=False, label_size=0.26):
    """A square that is dim at rest and bright on the frames its event (or value) is non-zero: added, no chain."""
    box(name, (x, y, 0.0), (size, size, 0.1), colour, LAMP_BASE)
    route(signal, f"procedural/{name}/material/emissive", op="add", amount=gain, optional=optional)
    if label:
        text(f"{name}Label", (x, y - size / 2 - 0.32, 0.0), label, label_size)
    layout[name] = {"kind": "lamp", "signal": signal, "rect": [x - size / 2, y - size / 2, x + size / 2, y + size / 2],
                    "param": f"procedural/{name}/material/emissive", "base": LAMP_BASE, "gain": gain}


def heading(name, x, y, words):
    text(name, (x, y, 0.0), words, 0.4, (0.95, 0.95, 0.95), 0.95, align=0)


# ---------------------------------------------------------------------------------------------------------------------
# A: ENERGY (top left)
heading("hA", -13.6, 7.0, "A  ENERGY")
for i, (sig, col, lab) in enumerate([
        ("response.level", (0.9, 0.9, 0.9), "LEVEL"), ("response.bass", (1.0, 0.45, 0.15), "LOW"),
        ("audio.midLevel", (0.95, 0.85, 0.2), "MID"), ("audio.trebleLevel", (0.3, 0.85, 1.0), "HIGH"),
        ("response.sustain", (0.6, 0.5, 1.0), "SUST"), ("response.onsetEnv", (1.0, 1.0, 0.55), "ENV")]):
    meter(f"m{lab.title()}", -13.0 + i * 1.35, 1.6, 4.6, sig, col, lab)

# B: EVENTS (top middle)
heading("hB", -4.6, 7.0, "B  EVENTS")
lamp("lOnset", -3.6, 5.2, 1.5, "response.onset", (1.0, 1.0, 0.6), "ONSET", gain=0.9)
lamp("lLow", -1.6, 5.2, 1.5, "response.low", (1.0, 0.45, 0.15), "LOW HIT", gain=0.9)
lamp("lBeat", 0.4, 5.2, 1.5, "beat.pulse", (1.0, 0.3, 0.9), "BEAT", gain=0.9)
lamp("lNote", 2.2, 5.2, 1.0, "response.note", (0.4, 1.0, 0.5), "MIDI NOTE", gain=0.9, label_size=0.22)
for i, (sig, col, lab) in enumerate([("response.onsetLow", (1.0, 0.45, 0.15), "REG L"),
                                     ("response.onsetMid", (0.95, 0.85, 0.2), "REG M"),
                                     ("response.onsetHigh", (0.3, 0.85, 1.0), "REG H")]):
    meter(f"r{lab[-1]}", -3.6 + i * 1.2, 1.6, 2.2, sig, col, lab, width=0.7)

# D: BEAT (top right)
heading("hD", 5.0, 7.0, "D  BEAT")
DIAL = (8.2, 3.9)
torus("dialRing", (DIAL[0], DIAL[1], -0.1), 2.3, 0.05, (0.5, 0.5, 0.5), 0.6)
for k in range(4):  # quarter ticks; 12 o'clock is phase 0
    a = math.radians(90 - 90 * k)
    box(f"dialTick{k}", (DIAL[0] + 2.3 * math.cos(a), DIAL[1] + 2.3 * math.sin(a), 0.0), (0.12, 0.12, 0.05),
        (0.9, 0.9, 0.9), 0.6)
box("dialHub", (DIAL[0], DIAL[1], 0.05), (0.25, 0.25, 0.05), (0.9, 0.9, 0.9), 0.6)
# The hand: one sphere on a radial distribution of one, its angle the route. 12 o'clock is phase 0 and it turns
# clockwise (radial angles are counter-clockwise from +X, so angle = pi/2 - 2 pi phase). beat.bar does the same on an
# outer ring with a smaller grey marker.
def orbiter(name, radius, size, colour, emissive, signal):
    nodes.append({"name": name, "kind": "procedural", "procedural": {
        "source": {"kind": "sphere", "radius": size, "segments": 16, "rings": 8},
        "distribution": {"kind": "radial", "count": 1, "radius": radius, "plane": "xy", "startAngle": math.pi / 2,
                         "endAngle": math.pi / 2 + 2 * math.pi, "center": [DIAL[0], DIAL[1], 0.0]},
        "material": {"baseColor": [0, 0, 0], "emissiveColor": list(colour), "emissiveIntensity": emissive,
                     "roughness": 1.0, "metallic": 0.0}}})
    route(signal, f"procedural/{name}/distribution/startAngle", chain={
        "remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0, "remapOutMin": math.pi / 2,
        "remapOutMax": math.pi / 2 - 2 * math.pi})


orbiter("dialNeedle", 2.3, 0.32, (1.0, 0.3, 0.9), 0.95, "beat.phase")
orbiter("dialBar", 2.85, 0.16, (0.7, 0.7, 0.7), 0.8, "beat.bar")
layout["dialNeedle"] = {"kind": "dial", "signal": "beat.phase", "centre": list(DIAL), "radius": 2.3,
                        "param": "procedural/dialNeedle/distribution/startAngle"}
text("dialLabel", (DIAL[0], DIAL[1] - 3.0, 0.0), "PHASE (pink)  BAR (grey)", 0.24)
meter("bConf", 11.6, 1.6, 4.6, "audio.tempoConfidence", (1.0, 0.3, 0.9), "CONF", width=0.6)
meter("bBpm", 12.8, 1.6, 4.6, "beat.bpm", (0.7, 0.7, 0.7), "BPM/300", width=0.6)
# beat.bpm is 0..300 on the bus: the meter's remap reads it as 0..300
for r in routes:
    if r["source"] == "beat.bpm":
        r["chain"]["remapInMax"] = 300.0
        r["chain"]["clampMax"] = 300.0
layout["bBpm"]["range"] = 300.0

# C: SOURCES (bottom left)
heading("hC", -13.6, -0.6, "C  SOURCES")
SOURCES = [("drums", (1.0, 0.55, 0.2)), ("bass", (0.35, 0.6, 1.0)), ("synth", (0.4, 1.0, 0.5)),
           ("pad", (0.75, 0.5, 1.0))]
for i, (key, col) in enumerate(SOURCES):
    x = -12.6 + i * 2.6
    text(f"s{key}Name", (x + 0.3, -1.45, 0.0), key.upper(), 0.3, col, 0.95)
    # 'no data' underneath (red), 'active' on top (green): a source that is absent or inactive shows red.
    box(f"s{key}NoData", (x - 0.45, -2.2, -0.02), (0.55, 0.55, 0.05), (1.0, 0.12, 0.1), 0.6)
    lamp(f"s{key}Active", x - 0.45, -2.2, 0.55, f"stem.{key}.active", (0.2, 1.0, 0.3), "", gain=0.9, optional=True)
    nodes[-1]["procedural"]["distributionTransform"]["position"][2] = 0.03
    nodes[-1]["procedural"]["material"]["emissiveIntensity"] = 0.0
    layout[f"s{key}Active"]["base"] = 0.0
    # Hidden while the source is inactive or absent, so the red lamp behind it shows; an optional route on a signal
    # the bus does not carry (no such source configured) never runs, so the lamp keeps its authored `visible: false`.
    nodes[-1]["procedural"]["visible"] = False  # the procedural's own flag (its parameter), not the node's
    route(f"stem.{key}.active", f"procedural/s{key}Active/visible", optional=True,
          chain={"threshold": "binary", "thresholdLevel": 0.5})
    meter(f"s{key}Level", x + 0.55, -6.6, 3.6, f"stem.{key}.level", col, "LVL", optional=True, width=0.55)
    lamp(f"s{key}Onset", x - 0.45, -3.4, 0.55, f"stem.{key}.onset", col, "ON", gain=0.9, optional=True,
         label_size=0.2)
for j, cls in enumerate(("kick", "snare", "hat")):
    lamp(f"sDrums{cls.title()}", -13.05, -4.5 - j * 0.85, 0.45, f"stem.drums.{cls}", (1.0, 0.55, 0.2), "",
         gain=0.9, optional=True)
    text(f"sDrums{cls.title()}Label", (-13.45, -4.62 - j * 0.85, 0.0), cls[0].upper(), 0.24, align=2)

# E: PROBES (bottom right)
heading("hE", -2.2, -0.6, "E  PROBES (real route chains)")
sphere("pBass", (-0.6, -3.6, 0.0), 1.0, (1.0, 0.45, 0.15), 0.9)
route("response.bass", "procedural/pBass/transform/scale", chain={
    "attackMs": 30, "decayMs": 300, "remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0,
    "remapOutMin": 0.25, "remapOutMax": 1.4, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0})
text("pBassLabel", (-0.6, -5.6, 0.0), "BASS SPHERE", 0.24)
layout["pBass"] = {"kind": "probe", "signal": "response.bass", "param": "procedural/pBass/transform/scale",
                   "component": 0, "rect": [-2.0, -5.0, 0.8, -2.2]}
box("pFlash", (2.4, -3.6, 0.0), (2.0, 2.0, 0.1), (1.0, 1.0, 1.0), 0.02)
route("response.onset", "procedural/pFlash/material/emissive", op="add", amount=0.95, chain={
    "envelope": "peakhold", "envelopeHoldMs": 0, "envelopeFallPerSecond": 8.33})  # back to rest in 120 ms
text("pFlashLabel", (2.4, -5.0, 0.0), "ONSET FLASH 120ms", 0.24)
layout["pFlash"] = {"kind": "probe", "signal": "response.onset", "param": "procedural/pFlash/material/emissive",
                    "rect": [1.4, -4.6, 3.4, -2.6]}
box("pColour", (5.4, -3.6, 0.0), (2.0, 2.0, 0.1), (0.1, 0.2, 1.0), 0.9)
route("sonic.brightness", "procedural/pColour/material/emissiveColor", component=0, chain={
    "remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0, "remapOutMin": 0.05, "remapOutMax": 1.0,
    "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0})
route("sonic.brightness", "procedural/pColour/material/emissiveColor", component=2, chain={
    "remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0, "remapOutMin": 1.0, "remapOutMax": 0.05,
    "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0})
text("pColourLabel", (5.4, -5.0, 0.0), "BRIGHTNESS COLOUR", 0.24)
layout["pColour"] = {"kind": "probe", "signal": "sonic.brightness",
                     "param": "procedural/pColour/material/emissiveColor", "component": 0,
                     "rect": [4.4, -4.6, 6.4, -2.6]}
box("pStem", (8.4, -3.6, 0.0), (1.0, 1.0, 0.1), (0.35, 0.6, 1.0), 0.9)
nodes[-1]["procedural"]["distributionTransform"]["scale"] = [0.2, 0.2, 1.0]  # authored at its rest: no stem, no size
route("stem.bass.level", "procedural/pStem/transform/scale", optional=True, chain={
    "attackMs": 30, "decayMs": 300, "remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0,
    "remapOutMin": 0.2, "remapOutMax": 1.8, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0})
text("pStemLabel", (8.4, -5.0, 0.0), "BASS STEM CUBE", 0.24)
layout["pStem"] = {"kind": "probe", "signal": "stem.bass.level", "param": "procedural/pStem/transform/scale",
                   "component": 0, "rect": [7.4, -4.6, 9.4, -2.6]}
box("pMidi", (11.6, -3.6, 0.0), (1.4, 1.4, 0.1), (0.4, 1.0, 0.5), 0.05)
route("control.labCC1", "procedural/pMidi/material/emissive", op="add", amount=0.9, optional=True)
route("control.labCC1", "procedural/pMidi/transform/rotation", component=2, optional=True, chain={
    "remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0, "remapOutMin": 0.0, "remapOutMax": -90.0})
text("pMidiLabel", (11.6, -5.0, 0.0), "MIDI CC1", 0.24)
layout["pMidi"] = {"kind": "probe", "signal": "control.labCC1", "param": "procedural/pMidi/material/emissive",
                   "rect": [10.9, -4.3, 12.3, -2.9]}

text("title", (13.6, -7.3, 0.0), "AUDIO RESPONSE LAB  diagnostic", 0.26, (0.5, 0.5, 0.5), 0.8, align=2)

CAMERA = {"position": [0.0, 0.0, 34.0], "target": [0.0, 0.0, 0.0], "fov": 27.0}

# Cheap by construction (brief §2): every object is unlit (ADR-1673's slim fragment path) and casts no shadow
# (ADR-1545). Nothing here is lit, so neither changes a pixel; the first live profile measured shadows at 12.7 ms
# of a 19.4 ms GPU frame before this.
for n in nodes:
    n["procedural"]["castsShadow"] = False
    n["procedural"]["material"]["unlit"] = True

scene = {
    "format": "avgen-scene", "version": 1, "name": "audio-response-lab",
    "camera": {"mode": 1, "position": CAMERA["position"], "target": CAMERA["target"], "fov": CAMERA["fov"],
               "orbitSpeed": 0.0},
    "environment": {"intensity": 0.0, "background": [0.006, 0.0065, 0.007], "fogColor": [0, 0, 0],
                    "volumeDensity": 0.0, "skyIntensity": 0.0, "sky": {"enabled": False, "background": False},
                    # nothing is lit: occlusion and contact shadows would only cost (6.7 ms of AO measured)
                    "ambientOcclusion": False, "contactShadows": False},
    "nodes": nodes,
}

project = {
    "format": "avgen-project", "version": 4,
    "app": {"name": "Audio Response Lab"},
    "assets": {"scene": {"kind": "composition", "path": "audio-response-lab.scene.json"}},
    "sonic": {"live": True},
    "parameters": {
        "post/bloom/enabled": False, "post/tonemap/operator": 4, "post/output/vignette": 0.0, "post/output/grain": 0.0,
        "camera/exposure/mode": 0, "camera/exposure/compensation": 0.0, "temporal/echo/enabled": False,
        "temporal/mosh/enabled": False, "post/grade/contrast": 1.0, "post/grade/saturation": 1.0,
        "camera/position": CAMERA["position"], "camera/target": CAMERA["target"],
    },
    "control": {"midi": {"enabled": True, "filter": "*", "bindings": [
        {"signal": "labCC1", "source": "*", "channel": -1, "kind": "cc", "number": 1}]}},
    "sources": [],
    "routes": routes,
    "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video", "path": "renders/audio-response-lab.mp4"},
    # Not read by the engine: where each indicator is, for tools/audio_lab (the composer and the pixel checks).
    "audioLab": {"camera": CAMERA, "layout": layout},
}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "audio-response-lab.scene.json").write_text(json.dumps(scene, indent=1) + "\n")
    (OUT / "audio-response-lab.json").write_text(json.dumps(project, indent=1) + "\n")
    print(f"make_lab_scene: {len(nodes)} nodes, {len(routes)} routes -> {OUT}")


if __name__ == "__main__":
    main()
