#!/usr/bin/env python3
"""Spatial lyric typography for AV Gen projects (ADR-1046), as data.

A word in the world is a procedural node whose source is `{"kind": "text"}`: extruded glyph geometry from the
platform's fonts, lit, fogged and depth-tested like any mesh, facing +Z with its back at z = 0. This module turns
a list of word entries into the three things a project needs, so ~300 lyric entries are one function call:

    from liminal_text import place_words
    out = place_words(entries, grid="song")
    scene["nodes"] += out["nodes"]                      # one node per entry (addressable: nodes/<name>/...)
    beatgrid_settings["events"] += out["events"]        # one envelope channel per entry, on the beat grid
    project["routes"] += out["routes"]                  # the animate-in/out treatment
    palette["bindings"] += out["bindings"]              # colour roles
    for chapter, names in out["chapters"].items():      # journey chapters show their own words only
        ...append names to that chapter's "nodes"...

An entry (only `text`, a time and `position` are required):

    {"name": "lig_let_1",          # optional; default w<index>_<TEXT>
     "text": "LET",
     "t0": 93.5, "t1": 94.6,       # appear / disappear, seconds (or "at"/"until": "letgo:1:1" on the grid)
     "position": [x, y, z],        # where the text's centre sits (world metres, the chapter's frame)
     "normal": [nx, ny, nz],       # the wall's outward normal: the text faces it, upright (or "rotation": [deg x, y, z])
     "tilt": 0,                    # degrees, a turn in the wall's plane (quirky layouts)
     "height": 0.6,                # cap height in metres (the em is height / 0.72)
     "depth": 0.08,                # extrusion in em
     "font": {"family": "Helvetica Neue", "weight": 0.6},
     "role": "accent",             # palette role bound to the emission colour; or "color": [r, g, b]
     "intensity": 3.0,             # emission strength
     "fill": [0.02, 0.02, 0.02],   # base (lit) colour: dark, so the word reads as light
     "style": "pop",               # pop | rise | flash | flicker | cut  (how it arrives and leaves)
     "in": 0.12, "out": 0.25,      # seconds of the arrival and the departure
     "chapter": "rooms"}           # optional: the journey chapter whose nodes list it joins

A word is drawn only while its envelope is above zero (a route multiplies `nodes/<name>/visible`): the renderer
draws at most 256 procedural objects at once, so 300 words are fine as long as no more than ~250 are up together.

Every word is addressable afterwards: `nodes/<name>/scale|position|rotation|visible|emissiveBoost` and
`procedural/<name>/material/emissive|emissiveColor|baseColor`, and the grid signal `grid.<grid>.<name>` is its
envelope (1 while it is shown). Key or route any of them on top.

Styles (all driven by the word's own envelope, so they are seek-exact and authored, never random):
    pop     scale 0 -> 1 through a lightly underdamped spring (a small overshoot), back to 0 on the way out
    rise    scales in while rising `rise` metres (default 0.25) into place
    flash   appears at once with an emission spike that settles over `in`
    flicker appears flickering on the sixteenth-note pulse while it arrives, then holds
    cut     on and off with no transition
"""

from __future__ import annotations

import math
import re

CAP_HEIGHT_EM = 0.72  # cap height of a typical sans, in em


def _matmul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def _rot(axis, deg):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    if axis == 0:
        return [[1, 0, 0], [0, c, -s], [0, s, c]]
    if axis == 1:
        return [[c, 0, s], [0, 1, 0], [-s, 0, c]]
    return [[c, -s, 0], [s, c, 0], [0, 0, 1]]


def _euler_from_normal(normal, tilt_deg=0.0):
    """Euler degrees for a node's "rotation" turning the text's +Z onto `normal`, upright, then turned `tilt`
    degrees in the wall's own plane. AV Gen's node rotation is glm's quat(euler): R = Rz(z) Ry(y) Rx(x)."""
    nx, ny, nz = normal
    n = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
    nx, ny, nz = nx / n, ny / n, nz / n
    pitch = -math.degrees(math.asin(max(-1.0, min(1.0, ny))))
    yaw = math.degrees(math.atan2(nx, nz)) if abs(ny) < 0.9999 else 0.0
    m = _matmul(_matmul(_rot(1, yaw), _rot(0, pitch)), _rot(2, tilt_deg))
    # Decompose m = Rz(z) Ry(y) Rx(x).
    y = -math.asin(max(-1.0, min(1.0, m[2][0])))
    if abs(m[2][0]) < 0.99999:
        x = math.atan2(m[2][1], m[2][2])
        z = math.atan2(m[1][0], m[0][0])
    else:  # gimbal: fold everything into x
        x = math.atan2(-m[1][2], m[1][1])
        z = 0.0
    return [math.degrees(x), math.degrees(y), math.degrees(z)]


def _channel(name):
    c = re.sub(r"[^A-Za-z0-9_]", "_", name)
    return c


def place_words(entries, grid="song", default_font=None, palette_target="emissiveColor"):
    font0 = default_font or {"family": "Helvetica Neue", "weight": 0.6}
    nodes, events, routes, bindings = [], [], [], []
    chapters = {}
    for i, e in enumerate(entries):
        text = e["text"]
        name = e.get("name") or f"w{i:03d}_{_channel(text)[:16]}"
        name = _channel(name)
        channel = name
        height = float(e.get("height", 0.5))
        size = height / CAP_HEIGHT_EM
        style = e.get("style", "pop")
        t_in = float(e.get("in", 0.12))
        t_out = float(e.get("out", 0.2))
        if "rotation" in e:
            rot = list(e["rotation"])
        else:
            rot = _euler_from_normal(e.get("normal", [0, 0, 1]), float(e.get("tilt", 0.0)))
        pos = list(e["position"])
        rise = float(e.get("rise", 0.25))
        if style == "rise":
            pos = [pos[0], pos[1] - rise, pos[2]]
        color = e.get("color", [1.0, 1.0, 1.0])
        node = {
            "kind": "procedural",
            "name": name,
            "position": pos,
            "rotation": rot,
            "procedural": {
                "source": {"kind": "text", "text": text, "textSize": size, "textDepth": float(e.get("depth", 0.08)),
                           "font": e.get("font", font0)},
                "material": {"baseColor": e.get("fill", [0.02, 0.02, 0.02]), "emissiveColor": color,
                             "emissiveIntensity": float(e.get("intensity", 3.0)),
                             "roughness": float(e.get("roughness", 0.7))},
            },
        }
        nodes.append(node)
        # The word's envelope on the beat grid: rises over `in` into t0 is wrong for a word (it must not
        # appear before it is sung), so the attack is zero and the arrival is shaped by the routes below.
        ev = {"channel": channel, "units": "seconds", "attack": 0.0, "release": t_out, "curve": "smooth"}
        if "at" in e:
            ev["at"] = e["at"]
            if "until" in e:
                ev["until"] = e["until"]
            else:
                ev["hold"] = float(e.get("hold", 0.5))
        else:
            ev["time"] = float(e["t0"])
            ev["hold"] = max(float(e.get("t1", e["t0"] + 0.5)) - float(e["t0"]), 0.0)
        if "strength" in e:
            ev["strength"] = e["strength"]
        events.append(ev)
        sig = f"grid.{grid}.{channel}"
        # Hidden means hidden: a word whose envelope is zero is not drawn (the renderer draws at most 256
        # procedural objects at once, and a zero scale still counts). Bools read >= 0.5 as true, so a large
        # amount keeps the word visible through the whole tail of its departure.
        routes.append({"source": sig, "target": f"nodes/{name}/visible", "op": "multiply", "amount": 1000.0})
        scale_route = {"source": sig, "target": f"nodes/{name}/scale", "op": "multiply", "amount": 1.0}
        if style == "pop":
            # A spring at ~1/(in) Hz with damping 0.45 overshoots by about 20 % and settles.
            scale_route["chain"] = {"springHz": max(1.0 / max(t_in, 0.03), 0.5), "springDamping": 0.45}
            routes.append(scale_route)
        elif style == "rise":
            scale_route["chain"] = {"springHz": max(0.6 / max(t_in, 0.03), 0.5), "springDamping": 0.9}
            routes.append(scale_route)
            routes.append({"source": sig, "target": f"nodes/{name}/position", "component": 1, "op": "add",
                           "amount": rise, "chain": {"springHz": max(0.6 / max(t_in, 0.03), 0.5), "springDamping": 0.9}})
        elif style == "flash":
            routes.append(scale_route)
            routes.append({"source": sig, "target": f"nodes/{name}/emissiveBoost", "op": "add",
                           "amount": float(e.get("flash", 6.0)), "chain": {"attackMs": 0, "decayMs": t_in * 1000.0}})
        elif style == "flicker":
            routes.append(scale_route)
            routes.append({"source": f"grid.{grid}.sixteenth", "target": f"nodes/{name}/emissiveBoost",
                           "op": "add", "amount": float(e.get("flash", 3.0)), "depthSource": sig})
        else:  # cut
            routes.append(scale_route)
        if "role" in e:
            bindings.append({"role": e["role"], "target": f"procedural/{name}/material/{palette_target}"})
        if "chapter" in e:
            chapters.setdefault(e["chapter"], []).append(name)
    return {"nodes": nodes, "events": events, "routes": routes, "bindings": bindings, "chapters": chapters}


if __name__ == "__main__":
    import json
    demo = place_words([
        {"text": "LET", "t0": 1.0, "t1": 2.0, "position": [0, 1.5, -3], "normal": [0, 0, 1], "height": 0.6},
        {"text": "IT", "t0": 1.25, "t1": 2.0, "position": [-1.9, 1.2, -2], "normal": [1, 0, 0], "style": "rise"},
        {"text": "GO", "t0": 1.5, "t1": 2.0, "position": [1, 0.01, -2], "normal": [0, 1, 0], "tilt": 12,
         "style": "flash", "role": "accent"},
    ])
    print(json.dumps(demo, indent=1))
