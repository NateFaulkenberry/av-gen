"""All You Got, art pass 4: the film's builder (pass 3's Builder3, extended; 04-art-pass-4.md governs).

What pass 4 adds:
  * build -> lock (PART 3 and PART 15): a structural element moves only during its construction event and is still
    afterwards. `lock()` writes such an event as keys (a fast ease into each new value, then a hold), and
    `audit_structure()` refuses any route that keeps a structural transform moving: a beat pulse, a wave or a phase
    into a wall's, a building's or a piece of furniture's transform. What may keep modulating is named in LIVING
    (lights, atmosphere, steam, signage, traffic, walkers, fans, clocks, the camera's energy, post effects);
  * one-off events (the BIG CLAPs' corruptions, a landing, a collapse) are keyed or fired by a clap channel with a
    release, so they end at rest: they are not continuous modulation;
  * `opens()` records when a room has opened enough for the camera to enter it (the film validator's `opensAt`).
"""

from __future__ import annotations

import re

import film3 as F3
import pass2_grid as G

t = G.t
BEAT1 = G.BAR1 / 4.0
BEAT2 = G.BAR2 / 4.0

# Sources that never stop: the grid's divisions (pulses, waves, phases) and the music's measured energy.
CONTINUOUS_SOURCE = re.compile(r"^(grid\.song\.(quarter|eighth|sixteenth|half|bar|whole)(\.wave|\.phase)?|audio\..*)$")
# Transform-like targets: an object's transform, a node's placement or shape.
STRUCTURAL_TARGET = re.compile(r"^(sdf/[^/]+/transform/(position|scale|rotation)|sdf/[^/]+/node/[^/]+/(translation|rotation|scale|size))$")
# What may keep moving (PART 15's continuous modulation): named nodes of living things, by substring.
LIVING = ("Fan", "Globe", "Clock", "Curtain", "walk", "Walk", "traffic", "Traffic", "car", "Car", "steam", "Steam",
          "sign", "Sign", "ringLowRot", "ringHighRot", "sf0", "sf1", "sf2", "sf3", "sf4", "flag", "Flag", "gest",
          "Gest", "arm", "Arm", "head", "Head", "jog", "Jog", "bus", "Bus", "pend", "Pend")


def is_living(target: str) -> bool:
    m = re.match(r"^sdf/([^/]+)/node/([^/]+)/", target)
    if m:
        return any(k in m.group(2) for k in LIVING) or any(k in m.group(1) for k in ("walk", "Walk", "traffic", "Traffic", "crowd", "Crowd"))
    m = re.match(r"^sdf/([^/]+)/transform/", target)
    if m:
        return any(k in m.group(1) for k in ("walk", "Walk", "traffic", "Traffic", "crowd", "Crowd", "car", "Car", "bus", "Bus"))
    return False


class Builder4(F3.Builder3):
    def __init__(self, film, add_world, palette_index):
        super().__init__(film, add_world, palette_index)
        self.opened = {}       # room id -> the time it has opened (the camera may enter after it)

    # ---- build -> lock -----------------------------------------------------------------------------------
    def lock(self, target, events, start, ease=0.06, kind="easeOut", mode="replace", component=-1):
        """A structural element's construction as keys: `start` is its value before the first event; each event
        (time, value) eases into the value over `ease` seconds ending on the event's instant + ease, and the value
        then HOLDS until the next event (nothing moves between events)."""
        keys = [(0.0, start, "step")]
        prev = start
        for tt, v in sorted(events, key=lambda e: e[0]):
            keys.append((tt - 0.001, prev, kind))
            keys.append((tt + ease, v, "step"))
            prev = v
        self.f.track(target, keys, mode=mode, component=component)
        return keys

    def opens(self, room_id, tt):
        self.opened[room_id] = round(tt, 3)

    # ---- the audit -------------------------------------------------------------------------------------------
    def audit_structure(self):
        """PART 15: no continuous modulation on a structural transform. Returns the routes it found (raises if any
        is not a living thing's)."""
        bad = []
        for r in self.f.routes:
            if CONTINUOUS_SOURCE.match(r["source"]) and STRUCTURAL_TARGET.match(r["target"]) and not is_living(r["target"]):
                bad.append(f"{r['source']} -> {r['target']}")
        if bad:
            raise RuntimeError("continuous modulation on structural transforms (PART 15):\n  " + "\n  ".join(bad))
        return bad

    def stamp_openings(self):
        """Write each room's `opensAt` into its entity (the film validator: entering a room before it has opened)."""
        def walk(n):
            if isinstance(n, dict):
                e = n.get("entity")
                if isinstance(e, dict) and e.get("category") == "room" and e.get("id") in self.opened:
                    e["opensAt"] = self.opened[e["id"]]
                for c in n.get("children", []):
                    walk(c)
        for node in self.f.nodes:
            if node.get("kind") == "sdf":
                walk(node["sdf"]["tree"]["root"])
