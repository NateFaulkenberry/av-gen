"""All You Got, art pass 2: the film, section by section (PASS2-PLAN.md §5-7), on film2.py's machinery.

Every visual response to the music is authored on the owner's grid:
  * pulses come from the beat grid source (ADR-1045): `grid.song.quarter|eighth|...`, gated per section by
    events with `until` (a gate is a route's depthSource);
  * every BIG CLAP is its own channel (`grid.song.cNN`) driving its own targets: no two are the same effect;
  * camera breathing (ADR-1048) is routed from `grid.song.quarter.wave` and keyed per section by
    `camera/breath/amount`;
  * words are extruded glyphs in the rooms (ADR-1046, tools/liminal_text.py), timed from pass2_grid.py.
"""

from __future__ import annotations

import math

import intro2 as IN
import kit as K
import outdoor2 as OD
import pass2_grid as G
import rooms2 as RM
from film2 import Film, mix, mul, srgb

t = G.t
BEAT1 = G.BAR1 / 4.0
BEAT2 = G.BAR2 / 4.0


def bl(bar):
    return G.beat_len(bar)


class Builder:
    def __init__(self, film: Film, add_world, palette_index):
        self.f = film
        self.add_world = add_world
        self.PI = palette_index
        self.worlds = {}
        self.words = []
        self.pal = []          # palette/position keys
        self.val = [(0.0, 1.0)]
        self.sat = [(0.0, 1.0)]
        self.breath = [(0.0, 0.0)]
        self.expo = []
        self.bloom = []
        self.hue = []
        self.chroma = []
        self.cuts = []
        self.edge_tracks = {}  # object -> keys of edge intensity base

    # ---- worlds ---------------------------------------------------------------------------------------
    def world(self, key, room, **kw):
        if key not in self.worlds:
            names, lights = self.add_world(self.f, room, **kw)
            self.worlds[key] = {"names": names, "lights": lights, "room": room}
        return self.worlds[key]

    def nodes(self, *keys, extra=()):
        out, lights = [], []
        for k in keys:
            out += self.worlds[k]["names"]
            lights += self.worlds[k]["lights"]
        return out + list(extra), lights

    # ---- shots ----------------------------------------------------------------------------------------
    def shot(self, name, t0, t1, eye, look, keys=(), extra=(), **kw):
        nodes, lights = self.nodes(*keys, extra=extra)
        self.f.shot(name, t0, t1, eye, look, nodes=nodes, lights=lights, **kw)
        self.cuts.append(t0)

    # ---- the beat grid ------------------------------------------------------------------------------
    def gate(self, name, t0, t1, fade=0.05):
        """A span of the song as a 0..1 signal (`grid.song.<name>`), for a route's depthSource."""
        self.f.event(name, time=t0, hold=max(t1 - t0 - fade, 0.0), attack=fade, release=fade, curve="linear")
        return f"grid.song.{name}"

    def clap(self, cid, t_at, release=0.6, attack=0.0, hold=0.0, curve="exp", strength=1.0):
        self.f.event(cid, time=t_at, attack=attack, hold=hold, release=release, curve=curve, strength=strength)
        return f"grid.song.{cid}"

    def pulse(self, target, amount, div="quarter", gate=None, component=-1, polarity="unipolar", **chain):
        self.f.route(f"grid.song.{div}", target, amount, component=component, depth=gate, polarity=polarity, **chain)

    # ---- words --------------------------------------------------------------------------------------
    def word(self, text, t0, t1, pos, normal, height, style="pop", role="word", tilt=0.0, intensity=3.2, name=None,
             tin=0.1, tout=0.2, **kw):
        e = {"text": text, "t0": t0, "t1": t1, "position": list(map(float, pos)), "normal": list(map(float, normal)),
             "height": height, "style": style, "role": role, "tilt": tilt, "intensity": intensity, "in": tin,
             "out": tout, "font": kw.pop("font", {"family": "Helvetica Neue", "weight": 0.7})}
        if name:
            e["name"] = name
        e.update(kw)
        self.words.append(e)
        return e

    # ---- palette ------------------------------------------------------------------------------------
    def palette_at(self, time, name, ramp=0.0):
        idx = float(self.PI[name])
        if ramp > 0 and self.pal:
            self.pal.append((time, self.pal[-1][1], "smooth"))
            self.pal.append((time + ramp, idx, "step"))
        else:
            self.pal.append((time, idx, "step"))


# =============================================================================================================

def seed_state_keys(b: Builder):
    """The intro's evolution (owner bars 0-8): one authored state per quarter note, landing on the beat with a
    fast ease (every beat 'one step of evolution'); the stabs on bars 1 and 3 get the biggest steps."""
    f = b.f
    O = "seed"
    # (beat time, state) -- a state is a dict of named node values; values hold until the next state
    st = []

    def S_(bar, beat, **v):
        st.append((t(bar, beat), v))
    W = IN.wire_keys
    zero3 = [0.0, 0.0, 0.0]
    # the count-in: a cursor blinking on each beat (a 6 cm cube)
    for i in range(4):
        S_(0, 1 + i, core=[0.03, 0.03, 0.03])
        st.append((t(0, 1 + i) + 0.22, {"core": [0.0, 0.0, 0.0]}))
    S_(1, 1.0, core=[0.5, 0.5, 0.5], ray=[3.2, 0.012, 0.012], rayAt=[3.4, 0, 0])
    S_(1, 2.0, core=[0.3, 0.3, 0.3], ray=[0.0, 0.0, 0.0], coreRot=[0, 0, 0])
    S_(1, 3.0, core=[0.62, 0.62, 0.62], coreRot=[0, 45, 0])
    S_(1, 4.0, core=[0.4, 0.4, 0.4], coreRot=[45, 45, 0])
    S_(2, 1.0, frame=0.85, core=[0.45, 0.45, 0.45], coreRot=[0, 0, 0])
    S_(2, 2.0, core=[0.0, 0.0, 0.0], cell=[0.16, 0.16, 0.16], cells=[0.5, 0.5, 0.5])
    S_(2, 3.0, cell=[0.14, 0.14, 0.14], cells=[0.62, 0.62, 0.62], frameRot=[0, 30, 0])
    S_(2, 4.0, cell=[0.09, 0.09, 0.09], cells=[0.78, 0.78, 0.78], frameRot=[0, 60, 0])
    S_(3, 1.0, ringAt=[2.0, 0, 0], ringCube=[0.12, 0.12, 0.12], frame=1.15, frameRot=[0, 90, 0])
    S_(3, 2.0, ringRot=[0, 45, 0], cellsRot=[0, 45, 0])
    S_(3, 3.0, frame=1.4, ringRot=[0, 90, 0], cell=[0.12, 0.12, 0.12], cells=[0.55, 0.55, 0.55])
    S_(3, 4.0, ringRot=[0, 135, 0], cell=[0.0, 0.0, 0.0], core=[0.35, 0.35, 0.35])
    S_(4, 1.0, core=[0.0, 0.0, 0.0], inner=0.55, frame=1.1, ringRot=[0, 180, 0])
    S_(4, 2.0, inner=0.7, innerRot=[0, 45, 0], ringRot=[0, 225, 0])
    S_(4, 3.0, ray=[2.6, 0.01, 0.01], rayAt=[2.8, 0, 0], rayRot=[90, 30, 0], ringRot=[0, 270, 0])
    S_(4, 4.0, ray=[0.0, 0.0, 0.0], inner=0.4, frame=0.8, ringRot=[0, 315, 0])
    # bars 5-8: the bass enters; the floors stack up through the sustained note; the ring steps on eighths
    for bar in range(5, 9):
        k = bar - 5
        S_(bar, 1.0, floors=[0.0, 0.42 - 0.04 * k, 0.0], floor=[0.9 + 0.12 * k, 0.02, 0.9 + 0.12 * k],
           frame=1.1 + 0.1 * k, inner=0.55 + 0.04 * k, core=[0.0, 0.0, 0.0])
        S_(bar, 2.0, cell=[0.06, 0.06, 0.06], cells=[0.45 + 0.05 * k] * 3, innerRot=[0, 45 * (k + 1), 0])
        S_(bar, 3.0, frameRot=[0, 45 * k + 22.5, 0], cell=[0.0, 0.0, 0.0])
        S_(bar, 4.0, inner=0.65 + 0.04 * k, frame=1.25 + 0.1 * k)
    # the end: the world collapses back into the cursor, which blinks twice and goes out (255-258 s)
    st.append((255.0, {"core": [0.6, 0.6, 0.6], "frame": 0.0, "inner": 0.0, "cell": [0.0, 0.0, 0.0], "ray": [0.0, 0.0, 0.0],
                       "ringCube": [0.0, 0.0, 0.0], "floor": [0.0, 0.0, 0.0], "coreRot": [0.0, 0.0, 0.0]}))
    for tt, v in ((255.55, 0.03), (256.1, 0.0), (256.5, 0.03), (256.9, 0.0), (257.3, 0.03), (257.65, 0.0)):
        st.append((tt, {"core": [v, v, v]}))
    # ring: one eighth of a turn on every eighth note through bars 5-8 (a clock hand)
    ring = []
    for i in range(32):
        bar, beat = 5 + i // 8, 1 + (i % 8) * 0.5
        ring.append((t(bar, beat), [0.0, 315.0 + 45.0 * (i + 1), 0.0]))
    # write each named node's keys: a fast ease into the beat's value (60 ms), held until the next state
    names = {}
    for tt, v in st:
        for k_, val in v.items():
            names.setdefault(k_, []).append((tt, val))
    for k_, ks in names.items():
        if k_ in ("frame", "inner"):
            # a wire cube: four boxes keyed together
            for suffix in ("", "X", "Y", "Z"):
                keys = []
                prev = None
                for tt, h in ks:
                    vals = W(h, 0.03 if k_ == "frame" else 0.02)[suffix] if h > 0 else [0.0, 0.0, 0.0]
                    if prev is not None:
                        keys.append((tt - 0.001, prev, "easeOut"))
                    keys.append((tt + 0.06, vals, "step"))
                    prev = vals
                f.track(f"sdf/{O}/node/{k_}{suffix}/size", [(0.0, [0.0, 0.0, 0.0], "step")] + keys)
            continue
        field = {"core": "size", "cell": "size", "ray": "size", "ringCube": "size", "floor": "size",
                 "cells": "size", "floors": "size", "rayAt": "translation", "ringAt": "translation",
                 "coreRot": "rotation", "frameRot": "rotation", "innerRot": "rotation", "ringRot": "rotation",
                 "rayRot": "rotation", "cellsRot": "rotation"}[k_]
        keys = []
        prev = None
        init = {"rayRot": [90.0, 0.0, 0.0], "cells": [0.5, 0.5, 0.5], "floors": [0.0, 0.6, 0.0]}.get(k_, [0.0, 0.0, 0.0])
        for tt, val in ks:
            if prev is not None:
                keys.append((tt - 0.001, prev, "easeOut"))
            keys.append((tt + 0.06, val, "step"))
            prev = val
        if k_ == "ringRot":
            keys += [(tt - 0.001, keys[-1][1], "easeOut") for tt, _ in ring[:1]]
            for tt, val in ring:
                keys.append((tt - 0.001, keys[-1][1], "easeOut"))
                keys.append((tt + 0.05, val, "step"))
        keys.sort(key=lambda k: k[0])
        f.track(f"sdf/{O}/node/{k_}/{field}", [(0.0, init, "step")] + keys)


def build(film: Film, add_world, palette_index, grid_settings):
    b = Builder(film, add_world, palette_index)
    f = film

    # ---- worlds ---------------------------------------------------------------------------------------
    seed_room = {"objects": [("seed", IN.seed(), "wall", (-8, -6, -8), (8, 6, 8), {"edge_pixels": 2.6, "own_line": True})],
                 "lights": []}
    b.world("seed", seed_room)
    liv = RM.living_room()
    b.world("liv", liv)
    house = {"objects": [("houseParts", IN.house_parts(), "furn2", (-14, -6, -6), (14, 12, 16), {"max_distance": 80.0}),
                         ("houseTree", IN.house_tree(), "furn", (1.5, -0.1, 1.2), (5.8, 4.5, 5.6), {"max_distance": 80.0}),
                         ("houseGround", IN.ground_plane(), "wall", (-40, -1, -40), (40, 0.2, 40),
                          {"max_distance": 80.0, "edge_pixels": 1.6})],
             "lights": []}
    b.world("house", house)
    bed = RM.bedroom()
    b.world("bed", bed)
    kit_ = RM.kitchen()
    b.world("kit", kit_)
    hall = RM.hallway()
    b.world("hall", hall)

    # ---- the palette's slow voice -------------------------------------------------------------------
    b.palette_at(0.0, "P0boot")
    b.palette_at(t(9), "P1compile")
    b.palette_at(t(17), "P2allyougot")
    b.palette_at(t(25), "P3night")
    b.palette_at(t(32, 4), "P4ember", ramp=BEAT1)          # C04: the palette turn, a wipe over one beat

    # ---- camera breathing: routed from the quarter's smooth wave, amount keyed per section ----------
    f.route("grid.song.quarter.wave", "camera/breath/forward", 0.07)
    f.route("grid.song.quarter.wave", "camera/breath/fov", -2.4)
    f.route("grid.song.eighth.wave", "camera/breath/lift", 0.012)
    b.breath += [(t(1) - 0.01, 0.0), (t(1), 1.0), (t(5), 1.0), (t(9), 0.7), (t(16, 4), 0.0), (t(17), 0.6), (t(19), 0.25),
                 (t(24, 4), 0.12)]

    # =========================================================================================================
    # COUNT-IN AND INTRO A-B (bars 0-8): the seed in the void
    # =========================================================================================================
    b.shot("seed", 0.0, t(9), [[0.0, 0.25, 6.2], [0.0, 0.3, 5.4]], (0.0, 0.0, 0.0), keys=("seed",),
           moves=[(0.0, 0.0), (t(5), 0.25), (t(9), 1.0)], fov=[(0.0, 46.0), (t(5), 46.0), (t(9), 52.0)])
    seed_state_keys(b)
    # the seed's lines: one colour per quarter note (magenta, cyan, white, electric blue), each beat a flash
    cols = [srgb("#FF2BD6"), srgb("#22E6FF"), srgb("#FFFFFF"), srgb("#3B5BFF")]
    ek = [(0.0, [1.0, 1.0, 1.0], "step")]
    for i in range(32):
        bar, beat = 1 + i // 4, 1 + i % 4
        ek.append((t(bar, beat), cols[i % 4], "step"))
    f.track("sdf/seed/look/edge/color", ek)
    g_intro = b.gate("gIntro", t(1), t(9))
    g_bass = b.gate("gBass", t(5), t(17))
    b.pulse("sdf/seed/look/edge/intensity", 6.0, "quarter", g_intro)
    b.pulse("sdf/seed/look/edge/pixels", 2.0, "quarter", g_intro)
    b.pulse("sdf/seed/look/edge/intensity", 2.5, "eighth", g_bass)
    f.track("sdf/seed/look/edge/intensity", [(0.0, 3.0), (t(1), 3.0), (t(5), 4.0), (t(9), 4.0)])
    # the splash: a burst of light and colour out of black
    c = b.clap("splash", t(1), release=1.6)
    f.route(c, "camera/exposure/compensation", 2.2)
    f.route(c, "post/bloom/intensity", 1.6)
    f.route(c, "post/lens/chromaticAberration", 0.6)
    f.route(c, "palette/saturation", 0.6)
    # the stabs on bars 1 and 3, and every bar's downbeat in bars 5-8, as smaller hits
    for bar in (3, 5, 6, 7, 8):
        c = b.clap(f"stab{bar}", t(bar), release=0.7)
        f.route(c, "camera/exposure/compensation", 0.9)
        f.route(c, "post/bloom/intensity", 0.6)

    # =========================================================================================================
    # INTRO C (bars 9-16): the house assembles around the living room; the camera pushes in; the gap at 16.4
    # =========================================================================================================
    b.shot("house", t(9), t(17), [[0.4, 2.6, 19.0], [0.0, 2.2, 13.0], [-0.4, 1.75, 7.0], [-0.6, 1.55, 3.4], [-0.6, 1.52, 2.62]],
           (-0.6, 1.4, 0.0), keys=("house", "liv"),
           moves=[(t(9), 0.0), (t(13), 0.32), (t(16, 4), 1.0), (t(17), 1.0)], ease_kind="in",
           fov=[(t(9), 50.0), (t(13), 54.0), (t(16, 3.5), 64.0), (t(17), 64.0)])
    # the parts fly in, one per eighth note from bar 9, faster each bar
    order = ["path", "porch", "fenceL", "fenceR", "chimney", "roofL", "roofR"]
    slots = [t(9), t(10), t(10, 3), t(11), t(11, 3), t(12), t(12, 2)]
    for (name, _b, off), tt in zip([p for p in IN.HOUSE_PARTS if p[0] in order], slots):
        pass
    by_name = {p[0]: p for p in IN.HOUSE_PARTS}
    for name, tt in zip(order, slots):
        off = list(by_name[name][2])
        f.track(f"sdf/houseParts/node/{name}/translation", [(0.0, off, "step"), (tt - 0.001, off, "easeOut"),
                                                            (tt + 0.35, [0.0, 0.0, 0.0], "step")])
    # the living room's walls draw themselves: their lines rise from nothing over bars 9-11
    f.track("sdf/livShell/look/edge/intensity", [(0.0, 0.0), (t(9), 0.0), (t(11), 4.0), (t(16, 4) - 0.01, 4.0),
                                                 (t(16, 4), 0.8, "step"), (t(17), 4.0)])
    f.track("sdf/houseTree/node/hTree/scale", [(0.0, 0.0, "step"), (t(13) - 0.001, 0.0, "easeOut"), (t(16), 1.0, "step")])
    g_riser = b.gate("gRiser", t(9), t(16, 4))
    b.pulse("sdf/houseParts/look/edge/intensity", 5.0, "eighth", g_riser)
    b.pulse("sdf/houseGround/look/edge/intensity", 2.0, "quarter", g_riser)
    b.pulse("sdf/livShell/look/edge/pixels", 1.5, "quarter", g_riser)
    # the 'all you got' chop, once a bar from bar 13: the windows flash
    for bar in range(13, 17):
        c = b.clap(f"chop{bar}", t(bar), release=0.9)
        f.route(c, "sdf/livShell/surface/7/emission", 6.0)
    # the gap (16.4): everything dims for one beat
    b.val += [(t(16, 4) - 0.02, 1.0, "step"), (t(16, 4), 0.25, "step"), (t(17), 1.0, "step")]

    # =========================================================================================================
    # FIRST RELEASE (bars 17-24): the impact; the living room; C01 colour explosion; C02 walls blow out
    # =========================================================================================================
    A = liv["anchors"]
    b.shot("rel1", t(17), t(19), [[-0.6, 1.55, 2.05], [-0.45, 1.5, 1.2], [-0.15, 1.45, 0.75]], (-1.6, 0.7, -0.9),
           keys=("liv",), moves=[(t(17), 0.0), (t(19), 1.0)], ease_kind="out", fov=[(t(17), 78.0), (t(17, 3), 64.0), (t(19), 62.0)])
    b.shot("rel2", t(19), t(21), [[1.9, 1.45, 1.75], [1.6, 1.45, 0.6], [1.5, 1.4, -0.4]], (-1.95, 0.85, -0.4),
           keys=("liv",), fov=58.0)
    b.shot("rel3", t(21), t(23), [[-1.0, 1.5, -1.5], [-0.4, 1.5, -1.2], [0.3, 1.5, -1.0]], (2.2, 0.85, 0.2), keys=("liv",), fov=60.0)
    b.shot("rel4", t(23), t(25), [[0.8, 1.35, 1.4], [0.2, 1.3, 1.25]], (-2.4, 1.55, -0.1), keys=("liv",), fov=56.0,
           moves=[(t(23), 0.0), (t(24, 4), 1.0), (t(25), 1.0)])
    # the impact: everything lands. A white flash, the furniture drops in from above with an overshoot
    c = b.clap("impact", t(17), release=2.0)
    f.route(c, "camera/exposure/compensation", 2.5)
    f.route(c, "post/bloom/intensity", 2.0)
    f.route(c, "post/lens/chromaticAberration", 0.8)
    for o in ("livShell", "livSofa", "livMedia", "livShelf", "livMan"):
        f.route(c, f"sdf/{o}/look/edge/intensity", 10.0)
    for node, h in (("livCouch", 1.6), ("livTable", 2.2), ("livTV", 1.4), ("livArm", 1.9), ("livShelfAt", 1.2),
                    ("livPlant", 2.4), ("livLampAt", 1.8), ("livManAt", 2.0)):
        obj = {"livCouch": "livSofa", "livTable": "livSofa", "livLampAt": "livSofa", "livTV": "livMedia",
               "livArm": "livMedia", "livShelfAt": "livShelf", "livPlant": "livShelf", "livManAt": "livMan"}[node]
        base = _node_translation(liv, obj, node)
        up = [base[0], base[1] + h, base[2]]
        f.track(f"sdf/{obj}/node/{node}/translation", [(0.0, up, "step"), (t(17) - 0.001, up, "easeIn"),
                                                       (t(17) + 0.16, [base[0], base[1] - 0.04, base[2]], "easeOut"),
                                                       (t(17) + 0.3, base, "step")])
    # the quarter pulse in the house: lines, the lamp, the furniture hops; strong at first, settling
    g_rel = b.gate("gRel", t(17), t(24, 4))
    f.track("sources/song/pulseDecay", [(0.0, 0.3), (t(17), 0.42), (t(25), 0.3), (t(49, 4), 0.3), (t(50), 0.22), (t(58), 0.16),
                                        (t(66), 0.3)])
    for o, amt in (("livShell", 3.0), ("livSofa", 4.0), ("livMedia", 4.0), ("livShelf", 4.0)):
        b.pulse(f"sdf/{o}/look/edge/intensity", amt, "quarter", g_rel)
    f.route("grid.song.quarter", "lights/livLamp/intensity", 3.0, depth=g_rel)
    for node, obj, h in (("livTable", "livSofa", 0.035), ("livArm", "livMedia", 0.04), ("livPlant", "livShelf", 0.05)):
        f.route("grid.song.quarter", f"sdf/{obj}/node/{node}/translation", h, component=1, depth=g_rel)
    # ALL YOU GOT on the TV, once a bar with the chop (17-24), and the room's lines carry the downbeat
    for bar in range(17, 25):
        b.word("ALL YOU GOT", t(bar) + 0.02, t(bar, 3.5), (2.075, 0.79, -0.17), (-1, 0, 0), 0.075, style="flash",
               role="screen", intensity=5.0, tin=0.2, tout=0.15, depth=0.04)
    # C01 (20.4): a colour explosion -- the whole wheel in 250 ms -- and the lamp bursts
    c = b.clap("c01", t(20, 4), release=0.9)
    f.route(c, "post/grade/hueShift", 3.1)
    f.route(c, "palette/saturation", 0.8)
    f.route(c, "lights/livLamp/intensity", 25.0)
    f.route(c, "post/bloom/intensity", 1.2)
    # C02 (24.4): the walls blow outward into the black, the furniture lifts; the cut lands in the bedroom
    c = b.clap("c02", t(24, 4), release=0.5, attack=BEAT1 * 0.25)
    f.route(c, "post/bloom/intensity", 1.5)
    f.route(c, "camera/exposure/compensation", 1.0)
    for o in ("livShell", "livCeil"):
        f.track(f"sdf/{o}/transform/scale", [(0.0, [1.0, 1.0, 1.0], "step"), (t(24, 4) - 0.002, [1.0, 1.0, 1.0], "easeOut"),
                                             (t(24, 4) + 0.35, [2.2, 1.5, 2.2], "easeIn"), (t(25) - 0.002, [3.4, 1.9, 3.4], "step"),
                                             (t(25), [1.0, 1.0, 1.0], "step")])
    for o in ("livSofa", "livMedia", "livShelf", "livMan"):
        f.track(f"sdf/{o}/transform/position", [(0.0, [0.0, 0.0, 0.0], "step"), (t(24, 4) - 0.002, [0.0, 0.0, 0.0], "easeOut"),
                                                (t(25) - 0.002, [0.0, 0.9, 0.0], "step"), (t(25), [0.0, 0.0, 0.0], "step")])

    # =========================================================================================================
    # VERSE 1 (bars 25-40): the night apartment; a room every four bars; words on the walls
    # =========================================================================================================
    BA = bed["anchors"]
    b.shot("v1bed", t(25), t(28, 4), [[1.25, 1.6, 1.75], [1.0, 1.58, 1.25], [0.65, 1.56, 0.95]], (-0.45, 1.55, -1.75),
           keys=("bed",), fov=60.0)
    b.word("HOW LITTLE DO I KNOW?", 55.3, t(28, 4), (-0.3, 2.22, -1.79), (0, 0, 1), 0.2, style="rise", role="word",
           tin=0.5, tout=0.3)
    b.word("HOW LITTLE", 57.4, t(28, 4), (-1.99, 2.05, 1.0), (1, 0, 0), 0.13, style="flicker", role="accent", tilt=-6)
    b.word("DO I KNOW?", 59.5, t(28, 4), (0.2, 2.59, -0.4), (0, -1, 0), 0.16, style="pop", role="word", tilt=180)
    b.word("BREATHE AND GROW", 61.5, t(28, 4), (1.99, 1.45, 0.55), (-1, 0, 0), 0.09, style="rise", role="accent")
    # C03 (28.4): the bedside lamp flares and floods the room white; inside the flash, the living room
    c = b.clap("c03", t(28, 4), release=1.1, attack=BEAT1 * 0.35)
    f.route(c, "camera/exposure/compensation", 2.6)
    f.route(c, "lights/bedLamp/intensity", 40.0)
    f.route(c, "post/bloom/intensity", 1.8)
    b.shot("v1liv", t(28, 4), t(33), [[2.0, 1.5, 1.8], [1.6, 1.46, 1.35], [1.25, 1.42, 1.0]], (-1.6, 0.85, -0.6),
           keys=("liv",), fov=58.0)
    b.word("CAN YOU TELL ME", 68.4, t(32, 3), (2.075, 0.86, -0.17), (-1, 0, 0), 0.055, style="flicker", role="screen",
           intensity=5.0, depth=0.04)
    b.word("IT'S FINE THOUGH?", 69.4, t(32, 3), (2.075, 0.72, -0.17), (-1, 0, 0), 0.055, style="flicker", role="screen",
           intensity=5.0, depth=0.04)
    b.word("FEEL AND GROW", 70.4, t(32, 4), (-0.2, 2.15, -2.19), (0, 0, 1), 0.14, style="rise", role="accent")
    # C04 (32.4): the palette turn -- the colour wipes from blue to crimson over one beat, and stays
    c = b.clap("c04", t(32, 4), release=0.8)
    f.route(c, "post/bloom/intensity", 0.9)
    f.route(c, "lights/livLamp/intensity", 12.0)
    b.shot("v1kit", t(33), t(37), [[1.75, 1.5, 1.6], [1.4, 1.48, 1.3], [1.0, 1.45, 1.15]], (-0.4, 1.0, -0.7), keys=("kit",),
           fov=60.0)
    KA = kit_["anchors"]
    for i, (wd, x) in enumerate((("COME ON,", -0.05), ("TELL ME", 0.35), ("WHAT YOU", 0.75))):
        b.word(wd, 74.5 + 0.35 * i, t(36, 4), (x, 1.83, -1.79 + 0.33), (0, 0, 1), 0.06, style="pop", role="word")
    b.word("WANNA", 75.6, t(36, 4), (1.55, 1.25, -1.79 + 0.68), (0, 0, 1), 0.1, style="flicker", role="accent")
    b.word("MAYBE CAUSE A LITTLE DRAMA", 76.3, t(36, 4), (1.55, 0.75, -1.79 + 0.66), (0, 0, 1), 0.035, style="pop",
           role="word", tilt=90)
    b.word("LET IT SHOW", 78.5, t(36, 4), (-0.7, 1.62, -1.78), (0, 0, 1), 0.13, style="rise", role="accent")
    # C05 (36.4): levitation -- plate, cup, chairs, kettle jump and hang, then drift down over the next bar
    c = b.clap("c05", t(36, 4), release=0.4)
    f.route(c, "post/bloom/intensity", 1.0)
    f.route(c, "camera/exposure/compensation", 1.0)
    for node, obj, h in (("kitPlate", "kitTable", 0.7), ("kitChairA", "kitTable", 0.55), ("kitChairB", "kitTable", 0.6),
                         ("kitKettle", "kitCounter", 0.8), ("kitTableAt", "kitTable", 0.35)):
        base = _node_translation(kit_, obj, node)
        upk = [base[0], base[1] + h, base[2]]
        f.track(f"sdf/{obj}/node/{node}/translation", [(0.0, base, "step"), (t(36, 4) - 0.001, base, "easeOut"),
                                                       (t(36, 4) + 0.12, upk, "smooth"), (t(37, 3), upk, "easeIn"),
                                                       (t(37, 4) + 0.3, base, "step")])
    b.shot("v1hall", t(37), t(41), [[0.0, 1.6, 0.6], [0.05, 1.55, -0.8], [0.0, 1.45, -2.55]], (0.0, 0.9, -4.3), keys=("hall",),
           fov=[(t(37), 62.0), (t(40, 4), 58.0), (t(41), 58.0)])
    HA = hall["anchors"]
    b.word("CAN YOU TELL ME IT'S FINE?", 81.1, t(38, 3), (0.73, 1.5, -1.4), (-1, 0, 0), 0.05, style="flicker",
           role="screen", intensity=4.0)
    b.word("PEACE OF MIND", 85.1, t(40, 4), (-0.74, 1.95, -2.2), (1, 0, 0), 0.12, style="rise", role="accent")
    words8 = ["IT'S", "STEPS", "IN", "A", "PROCESS,", "LET", "IT", "GO"]
    for i, wd in enumerate(words8):
        z = HA["foot"] - i * HA["run"]
        y = i * HA["rise"] + HA["rise"] / 2
        b.word(wd, 87.5 + i * 0.3, t(40, 4), (0.0, y, z + 0.003), (0, 0, 1), 0.1, style="pop", role="word")
    # C06 (40.4): the hallway collapses inward; the lines flare; then black
    c = b.clap("c06", t(40, 4), release=0.55)
    f.route(c, "post/bloom/intensity", 2.0)
    f.route(c, "camera/exposure/compensation", 1.8)
    f.route(c, "post/lens/distortion", -0.6)
    f.route("grid.song.quarter", "lights/kitPend/intensity", 3.0, depth=b.gate("gV1k", t(33), t(37)))
    g_v1 = b.gate("gV1", t(25), t(40, 4))
    for o in ("bedShell", "bedFurn", "livShell", "livSofa", "livMedia", "livShelf", "kitShell", "kitCounter", "kitTable",
              "hallShell", "hallFurn"):
        b.pulse(f"sdf/{o}/look/edge/intensity", 1.6, "quarter", g_v1)
    f.route("grid.song.quarter", "lights/bedLamp/intensity", 1.5, depth=g_v1)
    f.route("grid.song.quarter", "lights/livLamp/intensity", 1.5, depth=g_v1)
    # the clock hands tick on the quarter (a sixth of a turn each beat, a hand that keeps time)
    for clock_obj, clock in (("livMedia", "livClock"), ("kitShell", "kitClock")):
        f.route("grid.song.bar.phase", f"sdf/{clock_obj}/node/{clock}M/rotation", -360.0, component=2)
    import film_build2
    film_build2.build_part2(b, film.end)
    return b


def _node_translation(room, obj_name, node_name):
    """The translation of a named node in one of a room's objects (its rest value)."""
    tree = next(o[1] for o in room["objects"] if o[0] == obj_name)

    def find(n):
        if n.get("name") == node_name:
            return n
        for c in n.get("children", []):
            r = find(c)
            if r:
                return r
        return None
    n = find(tree)
    assert n is not None, (obj_name, node_name)
    return list(n.get("translation", [0.0, 0.0, 0.0]))
