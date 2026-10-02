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

    # ---- the gaze turns level ---------------------------------------------------------------------------------
    def _angular_gaze(self, shot, dt=0.1):
        """Pass 3's gaze re-keying (film3.Builder3._angular_gaze), with the direction interpolated as a heading and a
        pitch instead of along the great circle. The great circle between two headings far apart with the same slight
        downward pitch passes under the camera: a half-turn from a door to a bed slerped through the floor (the pass 4
        review found five such whip-downs: the bedroom, the bathroom, the study twice, the bar). Level turns take the
        shorter way round; a key pair more than 180 degrees apart needs a key between them to choose the side. Near
        the vertical (|pitch| > 80) the heading is meaningless and it slerps as before."""
        import math
        lk = shot["look_keys"]
        t0, t1 = shot["t0"], shot["t1"]
        eye_at = lambda tt: self.f.camera_at(min(max(tt, t0), t1 - 1e-4))[0]      # noqa: E731
        dirs = []
        for tk, p in lk:
            e = eye_at(tk)
            d = [a - b for a, b in zip(p, e)]
            L = math.sqrt(sum(v * v for v in d)) or 1.0
            dirs.append((tk, [v / L for v in d], L))

        def hp(d):
            return math.atan2(d[0], -d[2]), math.asin(max(-1.0, min(1.0, d[1])))

        def from_hp(h, pt):
            c = math.cos(pt)
            return [math.sin(h) * c, math.sin(pt), -math.cos(h) * c]

        out = []
        tt = t0
        while tt < t1 - 1e-6:
            if tt <= dirs[0][0]:
                _, d, L = dirs[0]
            elif tt >= dirs[-1][0]:
                _, d, L = dirs[-1]
            else:
                i = max(j for j in range(len(dirs)) if dirs[j][0] <= tt)
                (ta, da, La), (tb, db, Lb) = dirs[i], dirs[min(i + 1, len(dirs) - 1)]
                u = (tt - ta) / max(tb - ta, 1e-9)
                u = u * u * (3 - 2 * u)
                (ha, pa), (hb, pb) = hp(da), hp(db)
                if max(abs(pa), abs(pb)) > math.radians(80.0):
                    c = max(-1.0, min(1.0, sum(a * b for a, b in zip(da, db))))
                    om = math.acos(c)
                    if om < 1e-4:
                        d = da
                    else:
                        sa, sb = math.sin((1 - u) * om) / math.sin(om), math.sin(u * om) / math.sin(om)
                        d = [sa * a + sb * b for a, b in zip(da, db)]
                else:
                    dh = (hb - ha + math.pi) % (2.0 * math.pi) - math.pi
                    d = from_hp(ha + dh * u, pa + (pb - pa) * u)
                L = La + (Lb - La) * u
            e = eye_at(tt)
            out.append((round(tt, 4), tuple(a + b * L for a, b in zip(e, d))))
            tt += dt
        e = eye_at(t1 - 1e-4)
        _, d, L = dirs[-1]
        out.append((round(t1 - 2e-4, 4), tuple(a + b * L for a, b in zip(e, d))))
        shot["look_keys"] = out
        self.f._paths.pop(id(shot), None)

    # ---- words: a floor's clear area must be clear right down to the floor (a mat or a rug 2 cm high is not clear) ----
    def _surface_mask(self, m):
        if m["grid"] is None and m["key"] == "floor":
            grid = super()._surface_mask(m)
            import sdf_eval
            room = m["room"]
            objs = [o for o in room["objects"] if o[2] != "figure" and not o[0].endswith("Shell")]
            (a0, a1), (b0, b1) = m["a"], m["b"]
            step = m["step"]
            for i, row in enumerate(grid):
                for j, ok in enumerate(row):
                    if not ok:
                        continue
                    p = self._surface_point("floor", m["plane"], a0 + i * step, b0 + j * step)
                    for dd in (0.005, 0.02, 0.05):
                        q = [p[0], p[1] + dd, p[2]]
                        near = [o[1] for o in objs if all(o[3][c] - 0.05 <= q[c] <= o[4][c] + 0.05 for c in range(3))]
                        if near and min(sdf_eval.evaluate(tr, q) for tr in near) < 0.03:
                            row[j] = False
                            break
            m["grid"] = grid
            return grid
        return super()._surface_mask(m)

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

    def span_objects(self, spans):
        """{object name: (t0, t1)}: the object exists for the validator only in that span (its root is tagged a
        `structure` entity with the span, and every entity inside it gets the same span). For pieces that leave the
        film for good -- the faller after the intro, the roof and the ceiling that fly off at bar 66 -- which the
        static camera check would otherwise see at rest where the camera later passes."""
        def walk(n, t0, t1):
            if isinstance(n, dict):
                e = n.get("entity")
                if isinstance(e, dict):
                    e["t0"], e["t1"] = round(t0, 3), round(t1, 3)
                for c in n.get("children", []):
                    walk(c, t0, t1)
        for node in self.f.nodes:
            if node.get("kind") == "sdf" and node["name"] in spans:
                t0, t1 = spans[node["name"]]
                root = node["sdf"]["tree"]["root"]
                walk(root, t0, t1)
                if not isinstance(root.get("entity"), dict):
                    root["entity"] = {"category": "structure", "id": node["name"], "t0": round(t0, 3), "t1": round(t1, 3)}

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
