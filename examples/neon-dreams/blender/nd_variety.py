#!/usr/bin/env python3
"""Neon Dreams it12 (owner msg 170: "need to add posing variety to aliens in this scene"): who does what.

    /usr/bin/python3 examples/neon-dreams/blender/nd_variety.py [--report]

Rewrites art/population.json in place (idempotent: every figure's original facing and position are kept in
`facing0_deg` / `pos0`, and the same seed gives the same cast). It needs art/poses.json (blender/nd_poses.py: the
clips the cast carries) and, for the rail leaners, art/rails.json (blender/nd_rail_probe.py). Plain Python.

What it writes on every spot and rider:
  nd_clip      the clip it loops: the pack's, a mirrored copy `*_m`, or an authored `ND_*` (art/poses.json; these
               last two are in assets/aliens/<cast>-nd.glb, made by blender/nd_poses.py)
  nd_rate      clip seconds per timeline second (never under 1)
  nd_phase     its clip second at timeline second 0
  nd_grid      "kick" (clip second 0 falls on a kick), "off" (half a kick late) or "free"
  nd_cues      [{t, clip, rate, offset, blend, stock}] later changes of clip, in time order (a crash's cheer, a wave at
               the passing lens, and the way back to its loop): sequence clip cues, each looping until the next;
               `offset` is the clip second meant at t, `stock` the pack's clip to use without the -nd files
  anim_clip / anim_rate / anim_phase   the same three for the pack's own files (the engine's override names, 3d7d4d1a):
               the nearest stock clip, so a build without the -nd files still gets a varied cast
  look_weight  how much of its crowd's head-turn it takes (0 for those talking to a neighbour)
  sway         false where the clip already carries the beat (no crowd-sway layer)
  scale        its size (0.70-0.84 for the children at the front of the band's audience, 0.90-1.10 for the rest)
  facing_deg / forward   turned a little off the cast's original (`facing0_deg`), by what its activity allows
and on every path: `pause_clips` (one per walker: what it does at a path's end instead of Idle), `stop_clip`.
Run it LAST: nd_band.py and nd_reframe.py rewrite spots and would drop these fields.

How the choice is made. Each activity has a palette of clip families with weights; figures are taken in order of how
large the lens sees them, and each takes the family that scores best after its neighbours' choices are counted
against it (the same family within reach is all but forbidden, the same silhouette is marked down) and the palette's
shares are kept. A one-sided clip takes the mirror of its nearest same-family neighbour. Clips that carry the beat
keep it: rate 1 on the kick grid, their section of the crowd on the kick or half a kick late, each a few hundredths
of a second off like people; the rest run free at 1.00-1.15.
"""
from __future__ import annotations

import collections
import json
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.environ.get("ND_ART", os.path.join(HERE, "..", "art"))   # ND_ART: a scratch copy (stills before a commit)
AUDIO = os.path.join(HERE, "..", "audio")
BEAT = 60.0 / 171.4
KICK = 2 * BEAT
SEED = 170

# silhouette classes: two neighbours of one class read alike even on different clips
SIL = {"ND_Arms_up": "up", "ND_Wave": "up", "ND_Cheer": "up", "ND_Jump": "up", "ND_Stretch": "up", "ND_Hello": "oneup",
       "ND_Fist_pump": "oneup", "ND_Look_up": "oneup", "ND_Rock": "oneup", "ND_Clap": "front", "ND_Clap_b": "front", "ND_Film": "front",
       "ND_Point": "front", "ND_Fish": "front", "ND_Groove": "front", "ND_Talk_a": "front", "ND_Nod": "fold",
       "ND_Watch": "fold", "ND_Stand": "low", "ND_Sway": "low", "ND_Twist": "low", "ND_Talk_b": "hip", "ND_Hips": "hip",
       "Idle": "stride", "Idle_turn": "stride", "Fight_idle": "crouch", "Crazy": "crouch", "ND_Headbang": "crouch",
       "ND_Squat": "down", "ND_Sit": "down", "Take_from_floor": "down", "ND_Lean": "lean", "Button_push": "stride",
       "Take_from_table": "stride"}
# the clips that carry the beat (rate on the grid); the others run free
ON_GRID = {"ND_Clap", "ND_Clap_b", "ND_Arms_up", "ND_Fist_pump", "ND_Wave", "ND_Sway", "ND_Nod", "ND_Jump", "ND_Groove",
           "ND_Twist", "ND_Headbang", "ND_Point", "ND_Cheer", "Fight_idle", "Crazy", "Jumping", "Fight_Jab", "ND_Rock"}
# where the -nd cast files are missing: the nearest thing the pack has
STOCK = {"ND_Clap": "Fight_idle", "ND_Clap_b": "Fight_idle", "ND_Arms_up": "Fight_idle", "ND_Fist_pump": "Fight_Jab",
         "ND_Wave": "Idle_turn", "ND_Sway": "Fight_idle", "ND_Nod": "Idle", "ND_Jump": "Jumping", "ND_Groove": "Fight_idle",
         "ND_Twist": "Fight_idle", "ND_Headbang": "Fight_idle", "ND_Rock": "Fight_idle", "Crazy": "Fight_idle",
         "Crazy_m": "Fight_idle_m", "ND_Point": "Button_push", "ND_Film": "Take_from_table",
         "ND_Cheer": "Jumping", "ND_Stand": "Idle", "ND_Talk_a": "Idle_turn", "ND_Talk_b": "Idle", "ND_Watch": "Idle",
         "ND_Hips": "Idle_turn", "ND_Look_up": "Idle_turn", "ND_Stretch": "Idle_turn", "ND_Hello": "Button_push",
         "ND_Lean": "Idle", "ND_Sit": "Idle", "ND_Squat": "Take_from_floor", "ND_Fish": "Take_from_table"}

# ---- palettes: clip family -> weight
BAND = {
    "front": {"ND_Jump": 3, "ND_Arms_up": 3, "ND_Fist_pump": 3, "ND_Headbang": 2, "Crazy": 1.5, "ND_Groove": 2, "ND_Clap": 2,
              "ND_Cheer": 1, "ND_Twist": 1.5, "ND_Wave": 1.5, "Fight_idle": 1, "ND_Point": 0.7, "ND_Film": 0.8},
    "mid": {"ND_Clap": 3, "ND_Clap_b": 2.5, "ND_Wave": 2.5, "ND_Sway": 3, "ND_Groove": 2, "ND_Twist": 1.5, "ND_Arms_up": 2,
            "ND_Fist_pump": 2, "ND_Jump": 1, "ND_Point": 1.5, "ND_Film": 2, "ND_Nod": 2, "Fight_idle": 1, "ND_Headbang": 0.7},
    "back": {"ND_Nod": 3, "ND_Watch": 2.5, "ND_Sway": 2.5, "ND_Film": 2, "ND_Clap_b": 2, "ND_Clap": 1.5, "ND_Stand": 2,
             "ND_Hips": 1.5, "ND_Point": 1.5, "Idle": 1, "Idle_turn": 1, "ND_Wave": 1, "ND_Stretch": 0.3, "ND_Groove": 1},
    "kid": {"ND_Jump": 3, "ND_Arms_up": 2.5, "ND_Cheer": 2, "ND_Clap": 2, "ND_Groove": 1.5, "ND_Wave": 1.5, "ND_Twist": 1},
}
# what a figure may do when the crash lands (47.15 s), by row; None = carries on
REACT = {
    "front": {"ND_Cheer": 3, "ND_Jump": 3, "ND_Arms_up": 2, "Crazy": 2.5, "ND_Fist_pump": 1.5, "ND_Clap": 1, None: 3.5},
    "mid": {"ND_Cheer": 3, "ND_Jump": 2.5, "ND_Arms_up": 2.5, "Crazy": 1.5, "ND_Clap": 2, "ND_Fist_pump": 1.5, "ND_Wave": 1, None: 5.5},
    "back": {"ND_Clap": 3, "ND_Clap_b": 1.5, "ND_Arms_up": 2, "ND_Cheer": 2, "ND_Wave": 1, "ND_Fist_pump": 1, None: 6},
}
OTHERS = {"ND_Talk_b": 2.4, "ND_Watch": 1.7, "ND_Stand": 2.0, "ND_Hips": 1.6, "ND_Nod": 1.2, "Idle": 1.4, "Idle_turn": 1.4,
          "ND_Sit": 0.5, "ND_Squat": 0.35, "ND_Point": 0.9, "ND_Stretch": 0.4, "ND_Look_up": 0.6, "ND_Film": 0.5, "ND_Sway": 0.5,
          "ND_Fist_pump": 0.25, "ND_Clap_b": 0.25}
TOWN = {
    "gather": dict(OTHERS, ND_Talk_a=1.0),
    "look_sea": {"ND_Lean": 40, "ND_Stand": 2.5, "ND_Hips": 2, "ND_Watch": 2, "ND_Look_up": 0.7, "ND_Sit": 0.8, "ND_Squat": 0.3,
                 "Idle": 1.2, "Idle_turn": 0.8, "ND_Stretch": 0.5, "ND_Point": 0.8, "ND_Film": 0.8},
    "look_up": {"ND_Look_up": 3.5, "ND_Point": 2.5, "ND_Film": 2, "ND_Hips": 2, "ND_Watch": 1.5, "ND_Stand": 1.5,
                "Idle_turn": 1, "ND_Sit": 0.7},
    "look_sign": {"ND_Point": 2, "ND_Hips": 2, "ND_Watch": 2, "ND_Stand": 2, "Idle_turn": 1.5, "ND_Look_up": 1, "ND_Film": 1,
                  "Button_push": 0.7},
    "fish": {"ND_Fish": 5, "ND_Squat": 1.5, "ND_Sit": 1.2, "Take_from_floor": 0.8, "ND_Stand": 0.6, "ND_Lean": 12},
    "tend": {"Take_from_floor": 3, "ND_Squat": 3.5, "Button_push": 0.8, "ND_Stand": 0.6, "ND_Hips": 0.6, "ND_Stretch": 0.6},
    "browse": {"Take_from_table": 3, "Button_push": 2, "ND_Point": 1.5, "ND_Hips": 1, "Idle_turn": 1, "ND_Talk_a": 1,
               "ND_Watch": 0.7},
    "use": {"Button_push": 3, "Take_from_table": 2.5, "ND_Point": 0.8, "ND_Hips": 0.5},
    "work": {"Button_push": 2.5, "Take_from_floor": 2, "Take_from_table": 1.5, "ND_Squat": 1, "ND_Hips": 0.8, "ND_Stretch": 0.6,
             "ND_Point": 0.6},
    "wait": {"ND_Stand": 3, "ND_Watch": 2, "ND_Hips": 1.5, "ND_Look_up": 1, "ND_Stretch": 1, "ND_Sit": 1.5, "Idle": 1,
             "Idle_turn": 1.5, "ND_Nod": 1, "ND_Film": 0.6, "ND_Squat": 0.5},
    "stand": {"ND_Stand": 3, "ND_Hips": 2, "ND_Watch": 2, "ND_Stretch": 0.8, "ND_Look_up": 1, "Idle": 1.2, "Idle_turn": 1.5,
              "ND_Sit": 0.8, "ND_Nod": 1, "ND_Sway": 0.6, "ND_Film": 0.5, "ND_Point": 0.6},
    "sit_edge": {"ND_Sit": 5, "ND_Squat": 1.5},
    "keep": {"ND_Hips": 2, "ND_Watch": 2, "ND_Look_up": 1.5, "ND_Stand": 1.5, "ND_Point": 1},
    "ride": {"ND_Stand": 3, "ND_Watch": 1.5, "ND_Hips": 1, "ND_Point": 1.5, "Idle_turn": 1, "ND_Hello": 2, "ND_Film": 1},
}
for k in ("look_lantern", "look_moon", "look_mast"):
    TOWN[k] = TOWN["look_up"]
# how far a figure's facing may turn off the cast's (degrees, one sigma; clamped at twice that or 25)
TURN = {"watch_band": 12, "dance": 14, "gather": 6, "look_sea": 9, "fish": 5, "tend": 5, "browse": 4, "use": 3, "work": 4,
        "wait": 13, "stand": 13, "sit_edge": 10, "keep": 10, "look_lantern": 6, "look_moon": 6, "look_mast": 6, "look_sign": 6,
        "ride": 8}
PAUSES = {"ND_Stand": 3, "ND_Hips": 2, "ND_Look_up": 1.2, "ND_Stretch": 0.8, "ND_Watch": 1.5, "Idle": 1.5, "Idle_turn": 1.5}
# it14 (msg 202, "a light variety top-up where a patch looks uniform"): figure -> clip family, chosen on the plan
# (band_029 and band_012 took ND_Rock from Fight_idle beside two headbangers each, right before the lens: seven of the
# crowd's ten headbangers stood in one stretch of the front rows; band_076's four nearest all stood and watched)
TOPUP_14 = {"band_029": "ND_Clap", "band_012": "ND_Sway", "band_076": "ND_Sway"}


def family(c):
    return c[:-2] if c and c.endswith("_m") else c


def main():
    rng = random.Random(SEED)
    pop_path = os.path.join(ART, "population.json")
    pop = json.load(open(pop_path))
    poses = json.load(open(os.path.join(ART, "poses.json")))
    layout = json.load(open(os.path.join(ART, "layout.json")))
    events = json.load(open(os.path.join(AUDIO, "sabertooth_events.json")))
    rails = {}
    if os.path.exists(os.path.join(ART, "rails.json")):
        rails = json.load(open(os.path.join(ART, "rails.json")))["rails"]
    LEN = dict(poses["stock"])
    LEN.update({k: v["seconds"] for k, v in poses["authored"].items()})
    LEN.update({k: v["seconds"] for k, v in poses["mirrored_stock"].items()})
    kick0 = events["stems"]["kick"]["onsets"][0][0]
    crashes = [t for t, _ in events["stems"]["crash"]["onsets"]]
    stage = layout["band"]["stage"]
    sc, front = stage["centre"], stage["front_dir"]
    way = layout["camera"]["waypoints"]

    def cam(t):
        if t <= way[0]["t"]:
            return way[0]["pos"]
        for a, b in zip(way, way[1:]):
            if t <= b["t"]:
                u = (t - a["t"]) / max(b["t"] - a["t"], 1e-9)
                return [a["pos"][i] + (b["pos"][i] - a["pos"][i]) * u for i in range(3)]
        return way[-1]["pos"]

    def has_mirror(fam):
        return fam + "_m" in LEN

    def stock_of(clip):
        fam = family(clip)
        return STOCK.get(fam, fam)

    def rate_phase(clip, grid_off=0.0, which=None, jitter=0.03):
        """(rate, phase, grid) of a looping clip: on the kick grid for those that carry the beat, else free."""
        fam, L = family(clip), LEN[clip]
        if fam in ON_GRID:
            if fam.startswith("ND_"):
                rate = 1.0
                kicks = int(round(L / KICK))
            else:   # the pack's: a whole number of beats a cycle, never slower than authored
                n = max(1, int(L / BEAT + 1e-6))
                rate = L / (n * BEAT)
                kicks = max(1, n // 2)
            m = rng.randrange(kicks) if which is None else which % kicks
            t0 = kick0 + m * KICK + grid_off + rng.gauss(0.0, jitter)
            return round(rate, 4), round((-rate * t0) % L, 4), "off" if grid_off else "kick"
        return round(rng.uniform(1.0, 1.15), 3), round(rng.uniform(0.0, L), 4), "free"

    def offset_at(sp, t):
        """The clip second of the figure's own loop at timeline second t (for the cue that returns to it)."""
        return round((sp["nd_phase"] + sp["nd_rate"] * t) % LEN[sp["nd_clip"]], 4)

    def set_facing(sp, yaw):
        sp["facing_deg"] = round((yaw + 180.0) % 360.0 - 180.0, 2)
        a = math.radians(sp["facing_deg"])
        sp["forward"] = [round(math.sin(a), 4), 0.0, round(math.cos(a), 4)]

    # ---- start from the cast as it was
    figures = pop["spots"] + pop.get("riders", [])
    for sp in figures:
        for k in ("nd_clip", "nd_rate", "nd_phase", "nd_grid", "nd_cues", "anim_clip", "anim_rate", "anim_phase", "anim_stock",
                  "anim_grid", "anim_cues", "look_weight", "sway", "scale", "role", "rail"):
            sp.pop(k, None)
        if "facing_deg" in sp:
            sp.setdefault("facing0_deg", sp["facing_deg"])
            set_facing(sp, sp["facing0_deg"])
        if "pos0" in sp:
            sp["pos"] = list(sp["pos0"])
            del sp["pos0"]

    placed = []   # (pos, family, mirrored, phase fraction, reaction family)

    def near(pos, radius):
        out = []
        for q, fam, mir, ph, rx in placed:
            if abs(q[1] - pos[1]) < 1.6:
                d = math.hypot(q[0] - pos[0], q[2] - pos[2])
                if d < radius:
                    out.append((d, fam, mir, ph, rx))
        return out

    counts = collections.Counter()

    def choose(pos, palette, radius, key, slot=1):
        """A family of the palette for this place: drawn by weight, after its neighbours' choices are counted against
        each (the same family close by is out; further off, or the same silhouette, marked down) and the palette's
        shares so far are evened out."""
        nb = near(pos, radius)
        total = sum(palette.values())
        done = sum(counts[(key, f)] for f in palette) + 1e-9
        fams = list(palette)

        def scores(hard):
            out = []
            for fam in fams:
                s = palette[fam]
                for d, f2, _, _, rx in nb:
                    other = f2 if slot == 1 else rx
                    if other is None:
                        continue
                    if other == fam:
                        s *= (0.0 if hard else 0.02) if d < radius * 0.8 else 0.15
                    elif fam is not None and SIL.get(other) == SIL.get(fam):
                        s *= 0.5 if d < radius * 0.6 else 0.8
                share, want = counts[(key, fam)] / done, palette[fam] / total
                if done > 4:
                    s *= min(2.5, max(0.35, (want / (share + 0.02)) ** 0.8))
                out.append(s)
            return out
        w = scores(True)
        if sum(w) <= 1e-9:
            w = scores(False)
        best = rng.choices(fams, weights=w)[0]
        counts[(key, best)] += 1
        return best

    def mirrored(pos, fam, radius):
        if not has_mirror(fam):
            return False
        same = sorted((d, mir) for d, f2, mir, _, _ in near(pos, radius * 1.5) if f2 == fam)
        return (not same[0][1]) if same else rng.random() < 0.5

    def apply(sp, fam, radius, grid_off=0.0, which=None, mir=None, rate=None, phase=None):
        mir = mirrored(sp["pos"], fam, radius) if mir is None else (mir and has_mirror(fam))
        clip = fam + ("_m" if mir else "")
        r, ph, grid = rate_phase(clip, grid_off, which)
        if grid == "free":   # a same-family neighbour is never at the same point of the loop
            for _ in range(12):
                if all(abs(((ph / LEN[clip]) - q + 0.5) % 1.0 - 0.5) > 0.18 for _, f2, _, q, _ in near(sp["pos"], radius * 1.5)
                       if f2 == fam):
                    break
                ph = round(rng.uniform(0.0, LEN[clip]), 4)
        if rate is not None:
            r, ph = rate, phase
        sp["nd_clip"], sp["nd_rate"], sp["nd_phase"], sp["nd_grid"] = clip, r, ph, grid
        st = stock_of(clip)   # the same figure on the pack's own files
        sp["anim_clip"], sp["anim_rate"], sp["anim_phase"] = (st,) + rate_phase(st, grid_off, which)[:2]
        sp["sway"] = grid == "free" and SIL.get(fam) not in ("down", "lean")
        return mir

    def turn(sp, sigma, limit=25.0):
        if sigma:
            set_facing(sp, sp["facing0_deg"] + max(-limit, min(limit, rng.gauss(0.0, sigma))))

    # ================================================================== the band's audience
    band = [sp for sp in pop["spots"] if sp.get("district") == "band"]
    for sp in band:
        dx, dz = sp["pos"][0] - sc[0], sp["pos"][2] - sc[2]
        sp["_r"] = math.hypot(dx, dz)
        sp["_a"] = math.degrees(math.atan2(dx * front[2] - dz * front[0], dx * front[0] + dz * front[2]))
    rs = sorted(sp["_r"] for sp in band)
    r_front, r_back = rs[int(len(rs) * 0.30)], rs[int(len(rs) * 0.70)]
    angs = sorted(sp["_a"] for sp in band)
    cuts = [angs[int(len(angs) * k / 5)] for k in range(1, 5)]
    for sp in band:
        sp["_row"] = "front" if sp["_r"] <= r_front else ("back" if sp["_r"] > r_back else "mid")
        sp["_wedge"] = sum(sp["_a"] > c for c in cuts)
    # a section of the crowd (wedge x row) keeps time together: on the kick, or half a kick late
    section = {}
    for w in range(5):
        for row in ("front", "mid", "back"):
            section[(w, row)] = (0.0 if rng.random() < 0.62 else KICK / 2, rng.randrange(8))
    # children at the very front: the nearest figure to the stage in each of 14 slices of the arc
    kids = set()
    for k in range(14):
        lo, hi = angs[int(len(angs) * k / 14)], angs[min(len(angs) - 1, int(len(angs) * (k + 1) / 14))]
        slice_ = [sp for sp in band if lo <= sp["_a"] <= hi and sp["_row"] == "front"]
        if slice_:
            kids.add(min(slice_, key=lambda s: s["_r"])["id"])
    # pairs who talk: mutual nearest neighbours in the middle and back rows, well apart from other pairs
    pairs, used = [], set()

    def nearest(sp):
        return min((o for o in band if o is not sp), key=lambda o: math.dist(o["pos"], sp["pos"]))
    for sp in sorted(band, key=lambda s: -s["_r"]):
        if sp["id"] in used or sp["_row"] == "front" or sp["id"] in kids:
            continue
        o = nearest(sp)
        if o["id"] in used or o["_row"] == "front" or o["id"] in kids or nearest(o) is not sp:
            continue
        if math.dist(o["pos"], sp["pos"]) > 1.7 or abs(o["pos"][1] - sp["pos"][1]) > 0.1:
            continue
        if any(math.dist(sp["pos"], a["pos"]) < 4.0 for a, _ in pairs):
            continue
        pairs.append((sp, o))
        used |= {sp["id"], o["id"]}
        if len(pairs) >= 7:
            break
    R_BAND = 2.3
    for a, b in pairs:
        ra, ph, _ = rate_phase("ND_Talk_a")
        for sp, other, fam in ((a, b, "ND_Talk_a"), (b, a, "ND_Talk_b")):
            to = math.degrees(math.atan2(other["pos"][0] - sp["pos"][0], other["pos"][2] - sp["pos"][2]))
            d = (to - sp["facing0_deg"] + 180.0) % 360.0 - 180.0
            set_facing(sp, sp["facing0_deg"] + max(-48.0, min(48.0, d * 0.62)))
            mir = apply(sp, fam, R_BAND, mir=rng.random() < 0.5, rate=ra, phase=ph)
            sp["look_weight"], sp["sway"], sp["role"] = 0.0, False, "talk"
            sp["scale"] = round(min(1.1, max(0.9, rng.gauss(1.0, 0.045))), 3)
            placed.append((sp["pos"], fam, mir, ph / LEN["ND_Talk_a"], None))
    for sp in sorted(band, key=lambda s: s["_r"]):
        if sp["id"] in used:
            continue
        kid = sp["id"] in kids
        row = sp["_row"]
        fam = choose(sp["pos"], BAND["kid" if kid else row], R_BAND, "band_" + ("kid" if kid else row))
        off, which = section[(sp["_wedge"], row)]
        if rng.random() < 0.15:
            off = KICK / 2 - off
        mir = apply(sp, fam, R_BAND, off, which + rng.choice((0, 0, 0, 1, 2)))
        turn(sp, 4 if fam in ("ND_Point", "ND_Film") else TURN["watch_band"])
        sp["scale"] = round(rng.uniform(0.70, 0.84), 3) if kid else round(min(1.1, max(0.9, rng.gauss(1.0, 0.045))), 3)
        sp["look_weight"] = 0.0 if fam in ("ND_Headbang", "ND_Twist", "Crazy") else (0.5 if fam in ("ND_Watch", "ND_Point") else 1.0)
        if kid:
            sp["role"] = "child"
        placed.append((sp["pos"], fam, mir, sp["nd_phase"] / LEN[sp["nd_clip"]], None))
    # the 47.15 s crash: the cheer runs outward from the stage at 9 m/s; three in ten carry on as they were
    crash = min(crashes, key=lambda c: abs(c - 47.15))
    r0 = rs[0]
    reacts = {}
    for sp in sorted(band, key=lambda s: s["_r"]):
        own = family(sp["nd_clip"])
        pal = {k: v for k, v in REACT[sp["_row"]].items() if k != own}
        rx = choose(sp["pos"], pal, R_BAND, "react_" + sp["_row"], slot=2)
        # (the neighbours' reactions are looked up through `placed`: rewrite this figure's entry)
        for i, q in enumerate(placed):
            if q[0] is sp["pos"]:
                placed[i] = (q[0], q[1], q[2], q[3], rx)
        reacts[sp["id"]] = rx
        if rx is None:
            sp["nd_cues"] = []
            continue
        t = crash + 0.08 + (sp["_r"] - r0) / 9.0 + rng.uniform(0.0, 0.14)
        clip = rx + ("_m" if has_mirror(rx) and rng.random() < 0.5 else "")
        rate = 1.0 if rx.startswith("ND_") else rate_phase(clip)[0]
        hold = (8 if rx in ("ND_Cheer", "ND_Arms_up", "ND_Clap", "ND_Clap_b", "ND_Wave") else 4) * KICK
        cues = [{"t": round(t, 3), "clip": clip, "rate": rate, "offset": 0.0, "blend": 0.14, "stock": stock_of(clip)}]
        back = t + hold
        if own in ("ND_Nod", "ND_Watch", "ND_Stand", "ND_Hips", "Idle", "Idle_turn", "ND_Stretch", "ND_Talk_a", "ND_Talk_b"):
            # the quiet ones stay won over: applause to the end
            after = "ND_Clap_b" if rx != "ND_Clap_b" else "ND_Clap"
            r2, p2, _ = rate_phase(after, *section[(sp["_wedge"], sp["_row"])])
            cues.append({"t": round(back, 3), "clip": after, "rate": r2, "offset": round((p2 + r2 * back) % LEN[after], 4),
                         "blend": 0.3, "stock": stock_of(after)})
        else:
            cues.append({"t": round(back, 3), "clip": sp["nd_clip"], "rate": sp["nd_rate"], "offset": offset_at(sp, back),
                         "blend": 0.3, "stock": sp["anim_clip"]})
        sp["nd_cues"] = cues
    for sp in band:
        for k in ("_r", "_a", "_row", "_wedge"):
            sp.pop(k, None)

    # ================================================================== the town
    R_TOWN = 9.0     # the lens takes in a stretch of pier or street at once: no repeats within 7 m
    group_of = {}
    for g in pop.get("groups", []):
        for m in g["members"]:
            group_of[m] = g
    by_id = {sp["id"]: sp for sp in pop["spots"]}
    town = [sp for sp in pop["spots"] if sp.get("district") != "band"]
    town.sort(key=lambda s: -(s.get("visible") or {}).get("max_px", 0.0))
    talkers = {}
    for g in pop.get("groups", []):
        ms = [m for m in g["members"] if m in by_id and by_id[m].get("district") != "band"]
        if len(ms) >= 2:
            k = 2 if len(ms) >= 4 else 1
            first = rng.randrange(len(ms))
            for j in range(k):
                talkers[ms[(first + j * (len(ms) // k)) % len(ms)]] = g["id"]
    for sp in town:
        act = sp.get("activity", "stand")
        pal = dict(TOWN.get(act, TOWN["stand"]))
        rail = rails.get(sp["id"])
        if "ND_Lean" in pal and not rail:
            del pal["ND_Lean"]
        elif rail:
            pal["ND_Lean"] = 40
        if sp["id"] in talkers:
            fam = "ND_Talk_a"
            counts[("town_gather", fam)] += 1
        else:
            if sp["id"] in group_of:
                pal = dict(OTHERS)
                # nobody shares a clip inside a ring
                for m in group_of[sp["id"]]["members"]:
                    o = by_id.get(m)
                    if o is not None and o is not sp and o.get("nd_clip"):
                        pal.pop(family(o["nd_clip"]), None)
            fam = choose(sp["pos"], pal, R_TOWN, "town_" + act)
        if fam == "ND_Lean":
            # up to the rail: 0.36 m short of it (at its own size), turned to meet it, sized so its forearms rest on it
            s = min(1.12, max(0.88, rail["top"] / 1.0))
            yaw = sp["facing0_deg"] + rail["yaw"]
            a = math.radians(yaw)
            step = rail["dist"] - 0.36 * s
            sp["pos0"] = list(sp["pos"])
            sp["pos"] = [round(sp["pos"][0] + math.sin(a) * step, 3), sp["pos"][1], round(sp["pos"][2] + math.cos(a) * step, 3)]
            set_facing(sp, yaw)
            sp["scale"] = round(s, 3)
            sp["rail"] = rail
        else:
            turn(sp, TURN.get(act, 10) * (0.5 if fam in ("ND_Point", "ND_Film", "ND_Look_up", "ND_Fish") else 1.0))
            small = sp["id"] in group_of and rng.random() < 0.09 and fam not in ("ND_Sit", "ND_Squat")
            sp["scale"] = round(rng.uniform(0.72, 0.84), 3) if small else round(min(1.1, max(0.9, rng.gauss(1.0, 0.045))), 3)
            if small:
                sp["role"] = "child"
        mir = apply(sp, fam, R_TOWN)
        placed.append((sp["pos"], fam, mir, sp["nd_phase"] / LEN[sp["nd_clip"]], None))
        sp["nd_cues"] = []
    # ---- crashes: one of each gathering answers (a different one each time); the answer runs outward from where
    # the lens is at the crash, at 25 m/s, within 45 m of it: the nearest answer on the hit and the rest follow
    answers = ["ND_Cheer", "ND_Arms_up", "ND_Hello", "ND_Jump", "ND_Clap", "ND_Point"]
    for gi, g in enumerate(pop.get("groups", [])):
        ms = [by_id[m] for m in g["members"] if m in by_id and by_id[m].get("district") != "band"
              and "gather_crash" in by_id[m].get("react", []) and by_id[m].get("tier") in ("A", "B")
              and family(by_id[m].get("nd_clip", "")) not in ("ND_Sit", "ND_Squat", "ND_Lean")]
        if not ms:
            continue
        for ci, c in enumerate(crashes):
            sp = ms[(ci + gi) % len(ms)]
            far = math.dist(sp["pos"], cam(c))
            if far > 45.0:      # out of the lens's reach at this crash: no answer (it would land seconds late)
                continue
            t = c + 0.05 + far / 25.0 + rng.uniform(0.0, 0.2)
            if any(abs(q["t"] - t) < 6.0 for q in sp["nd_cues"]):
                continue
            fam = answers[(ci + gi * 3 + rng.randrange(2)) % len(answers)]
            clip = fam + ("_m" if has_mirror(fam) and rng.random() < 0.5 else "")
            hold = LEN[clip] if fam in ("ND_Hello", "ND_Point") else 4 * KICK
            sp["nd_cues"] += [
                {"t": round(t, 3), "clip": clip, "rate": 1.0, "offset": 0.0, "blend": 0.2, "stock": stock_of(clip)},
                {"t": round(t + hold, 3), "clip": sp["nd_clip"], "rate": sp["nd_rate"], "offset": offset_at(sp, t + hold),
                 "blend": 0.35, "stock": sp["anim_clip"]}]
    # ---- a wave for the passing lens: a few who stand near its path, turned to it, as it comes by
    waves = []
    for sp in town:
        if sp.get("tier") != "A" or sp["id"] in talkers or family(sp["nd_clip"]) in ("ND_Sit", "ND_Squat", "ND_Lean", "ND_Fish",
                                                                                         "Take_from_floor", "ND_Talk_b"):
            continue
        if sp.get("activity") not in ("stand", "wait", "gather", "look_sea", "look_sign", "keep"):
            continue
        best_t, best_d = None, 1e9
        t = 0.0
        while t < 58.0:
            d = math.dist(cam(t), sp["pos"])
            if d < best_d:
                best_t, best_d = t, d
            t += 0.1
        if not (4.0 <= best_d <= 15.0) or best_t < 4.0:
            continue
        c = cam(best_t - 1.4)
        to = math.degrees(math.atan2(c[0] - sp["pos"][0], c[2] - sp["pos"][2]))
        off = (to - sp["facing0_deg"] + 180.0) % 360.0 - 180.0
        limit = 40.0 if sp["id"] not in group_of else 12.0
        if abs(off) > limit + 35.0:
            continue
        waves.append((best_t, best_d, off, limit, sp))
    waves.sort(key=lambda w: w[0])
    last = -99.0
    n_wave = 0
    for best_t, best_d, off, limit, sp in waves:
        if best_t - last < 2.2 or any(abs(q["t"] - best_t) < 7.0 for q in sp["nd_cues"]):
            continue
        last = best_t
        n_wave += 1
        set_facing(sp, sp["facing0_deg"] + max(-limit, min(limit, off)))
        clip = "ND_Hello" + ("_m" if rng.random() < 0.5 else "")
        t = best_t - 3.3
        sp["nd_cues"] += [
            {"t": round(t, 3), "clip": clip, "rate": 1.0, "offset": 0.0, "blend": 0.3, "stock": "Button_push"},
            {"t": round(t + LEN[clip], 3), "clip": sp["nd_clip"], "rate": sp["nd_rate"],
             "offset": offset_at(sp, t + LEN[clip]), "blend": 0.4, "stock": sp["anim_clip"]}]
        sp["role"] = "waves at the lens %.1f s (%.0f m)" % (best_t, best_d)
    for sp in town:
        sp["nd_cues"].sort(key=lambda q: q["t"])
    # ---- riders
    for rd in pop.get("riders", []):
        fam = choose([rng.uniform(-1e6, 1e6), 0.0, 0.0], TOWN["ride"], 1.0, "ride")
        clip = fam + ("_m" if has_mirror(fam) and rng.random() < 0.5 else "")
        r, ph, grid = rate_phase(clip)
        rd["nd_clip"], rd["nd_rate"], rd["nd_phase"], rd["nd_grid"] = clip, r, ph, grid
        st = stock_of(clip)
        rd["anim_clip"], rd["anim_rate"], rd["anim_phase"] = (st,) + rate_phase(st)[:2]
        rd["scale"] = round(min(1.06, max(0.9, rng.gauss(0.98, 0.04))), 3)
    # ---- walkers: what each does at a path's end and at a shopfront
    for p in pop.get("paths", []):
        n = max(1, int(p.get("walkers", 1)))
        fams = [rng.choices(list(PAUSES), weights=list(PAUSES.values()))[0] for _ in range(n)]
        p["pause_clips"] = [f + ("_m" if has_mirror(f) and rng.random() < 0.5 else "") for f in fams]
        if p.get("stops"):
            p["stop_clip"] = rng.choice(["ND_Point", "ND_Hips", "Take_from_table", "ND_Watch", "Button_push"])

    # ================================================================== it14 (owner msgs 195, 200, 202)
    # msg 195 marked band_026 at 44.1 s ("going a little too hard"), msg 202: "the crowd has pretty good reactions
    # already ... excessive twitchiness in the headbanging". band_026 looped the pack's Crazy, which is not a loop (its
    # last pose is 45 deg at the head and half a metre from its first: a snap every 2.46 s) and whips the head 16 deg a
    # frame. Everything above is the it12 cast draw for draw; this pass only (a) takes Crazy out of the band's loops
    # and crash answers, Fight_idle (a boxer's guard) and Idle_turn (a head whip) out of its loops, (b) softens the
    # crash's blends, (c) tops up the few uniform patches, (d) opens the gaps the crowd's flyers land in (msg 200).
    rng = random.Random(SEED + 14)
    band = [sp for sp in pop["spots"] if sp.get("district") == "band"]
    entry = {id(q[0]): i for i, q in enumerate(placed)}
    log14 = {"loops": collections.Counter(), "answers": collections.Counter(), "topup": [], "gaps": {}}

    def reloop(sp, fam):
        old = family(sp["nd_clip"])
        off = KICK / 2 if sp.get("nd_grid") == "off" else 0.0
        i = entry.get(id(sp["pos"]))
        if i is not None:   # its own old entry must not count as a neighbour
            placed[i] = (placed[i][0], None, False, 0.0, placed[i][4])
        mir = apply(sp, fam, R_BAND, off, rng.randrange(4))
        if i is not None:
            placed[i] = (sp["pos"], fam, mir, sp["nd_phase"] / LEN[sp["nd_clip"]], placed[i][4])
        sp["look_weight"] = 0.0 if fam in ("ND_Headbang", "ND_Rock", "ND_Twist") else (0.5 if fam in ("ND_Watch", "ND_Point") else 1.0)
        for q in sp.get("nd_cues", [])[1:]:   # the way back from the crash's answer is to the new loop
            if family(q["clip"]) == old:
                q.update({"clip": sp["nd_clip"], "rate": sp["nd_rate"], "offset": offset_at(sp, q["t"]), "stock": sp["anim_clip"]})
        log14["loops"]["%s -> %s" % (old, fam)] += 1

    def fams_near(sp, radius=R_BAND):
        return {f2 for _, f2, _, _, _ in near(sp["pos"], radius) if f2}

    for sp in sorted(band, key=lambda s: s["id"]):
        fam = family(sp["nd_clip"])
        if fam == "Crazy":
            reloop(sp, "ND_Headbang" if "ND_Headbang" not in fams_near(sp) else "ND_Rock")
        elif fam == "Fight_idle":
            reloop(sp, "ND_Rock" if "ND_Rock" not in fams_near(sp) else "ND_Headbang")
        elif fam == "Idle_turn":
            reloop(sp, "ND_Watch")
    # (c) a light top-up (msg 202): figures picked by eye on the plan (work/art/it14/plan_types.py), where three or
    # four neighbours in a row did the same kind of thing
    for sid, fam in TOPUP_14.items():
        sp = next((x for x in band if x["id"] == sid), None)
        if sp is not None and family(sp["nd_clip"]) != fam:
            log14["topup"].append("%s %s -> %s" % (sid, sp["nd_clip"], fam))
            reloop(sp, fam)
    # (b) the crash: nobody answers with Crazy; the answers come in over 0.28 s (was 0.14) and go back over 0.45 s
    answer = {sp["id"]: family(sp["nd_cues"][0]["clip"]) for sp in band if sp.get("nd_cues")}
    for sp in sorted(band, key=lambda s: s["id"]):
        cues = sp.get("nd_cues") or []
        if not cues:
            continue
        if family(cues[0]["clip"]) == "Crazy":
            own = family(sp["nd_clip"])
            close = [answer.get(o["id"]) for o in band if o is not sp and abs(o["pos"][1] - sp["pos"][1]) < 1.6
                     and math.hypot(o["pos"][0] - sp["pos"][0], o["pos"][2] - sp["pos"][2]) < R_BAND]
            opts = [f for f in ("ND_Cheer", "ND_Arms_up", "ND_Rock", "ND_Fist_pump", "ND_Clap_b") if f != own]
            fam = min(opts, key=lambda f: (close.count(f), rng.random()))
            clip = fam + ("_m" if has_mirror(fam) and rng.random() < 0.5 else "")
            cues[0].update({"clip": clip, "rate": 1.0, "offset": 0.0, "stock": stock_of(clip)})
            answer[sp["id"]] = fam
            log14["answers"]["Crazy -> %s" % fam] += 1
        cues[0]["blend"] = 0.28
        for q in cues[1:]:
            q["blend"] = 0.45
    # (d) msg 200: a flyer that lands in the crowd needs room. Figures inside `gap_m` of its landing step back to the
    # gap's edge (pos0 keeps the place they had), then anyone they now crowd makes room in turn.
    for fl in pop.get("flyers", []):
        gap = fl.get("landing", {}).get("gap_m")
        if not gap:
            continue
        L = fl["landing"]["pos"]
        moved = []
        for sp in band:
            d = math.hypot(sp["pos"][0] - L[0], sp["pos"][2] - L[2])
            if abs(sp["pos"][1] - L[1]) < 0.3 and d < gap:
                ux, uz = ((sp["pos"][0] - L[0]) / d, (sp["pos"][2] - L[2]) / d) if d > 1e-3 else (-front[0], -front[2])
                sp.setdefault("pos0", list(sp["pos"]))
                sp["pos"] = [round(L[0] + ux * gap, 3), sp["pos"][1], round(L[2] + uz * gap, 3)]
                moved.append(sp)
        for _ in range(6):
            again = False
            for sp in list(moved):
                for o in band:
                    if o is sp or abs(o["pos"][1] - sp["pos"][1]) > 0.3:
                        continue
                    d = math.hypot(o["pos"][0] - sp["pos"][0], o["pos"][2] - sp["pos"][2])
                    if d < 0.8 and math.hypot(o["pos"][0] - L[0], o["pos"][2] - L[2]) < gap + 2.0:
                        ux, uz = ((o["pos"][0] - sp["pos"][0]) / d, (o["pos"][2] - sp["pos"][2]) / d) if d > 1e-3 else (0.0, 1.0)
                        o.setdefault("pos0", list(o["pos"]))
                        o["pos"] = [round(sp["pos"][0] + ux * 0.85, 3), o["pos"][1], round(sp["pos"][2] + uz * 0.85, 3)]
                        if o not in moved:
                            moved.append(o)
                        again = True
            if not again:
                break
        log14["gaps"][fl["id"]] = [sp["id"] for sp in moved]

    # ---- the record
    spots = pop["spots"]
    fam_count = collections.Counter(family(sp["nd_clip"]) for sp in spots)
    edits = [e for e in pop.get("edits", []) if e.get("script") != "blender/nd_variety.py"]   # (the it12 and it14 records)
    edits.append({"it": 12, "msg": 170, "by": "art", "script": "blender/nd_variety.py",
                  "what": "every spot and rider: nd_clip / nd_rate / nd_phase / nd_grid / nd_cues (+ anim_clip / anim_rate / anim_phase for the pack's own files), "
                          "look_weight, sway, scale, facing (facing0_deg keeps the original); rail leaners moved to their "
                          "rail (pos0 keeps the original); paths: pause_clips, stop_clip",
                  "families": dict(fam_count.most_common()), "talk_pairs_band": len(pairs), "children_band": len(kids),
                  "rail_leaners": sum(1 for sp in spots if sp.get("rail")), "waves_at_lens": n_wave,
                  "crash_reactors_band": sum(1 for sp in band if sp.get("nd_cues"))})
    edits.append({"it": 14, "msg": [195, 200, 202], "by": "art", "script": "blender/nd_variety.py",
                  "what": "the band's audience: the pack's Crazy (not a loop; msg 195's marked figure band_026) out of the loops "
                          "and the crash answers, Fight_idle and Idle_turn out of the loops; the crash's blends 0.28 s in / "
                          "0.45 s back; a light top-up; room made where a flyer lands in the crowd (pos0 keeps the old place)",
                  "loops": dict(log14["loops"]), "answers": dict(log14["answers"]), "topup": log14["topup"],
                  "gaps": log14["gaps"]})
    pop["edits"] = edits
    if "--dry" not in sys.argv:
        json.dump(pop, open(pop_path, "w"), indent=None, separators=(",", ":"))
    report(pop, LEN)


def report(pop, LEN=None):
    spots = pop["spots"]
    for name, sel, radius in (("band audience", [s for s in spots if s.get("district") == "band"], 1.8),
                              ("town", [s for s in spots if s.get("district") != "band"], 2.5)):
        fam = collections.Counter(family(s.get("nd_clip", (s.get("clips") or ["Idle"])[0])) for s in sel)
        same = pairs = samephase = 0
        for i, a in enumerate(sel):
            for b in sel[i + 1:]:
                if abs(a["pos"][1] - b["pos"][1]) < 1.6 and math.hypot(a["pos"][0] - b["pos"][0], a["pos"][2] - b["pos"][2]) < radius:
                    pairs += 1
                    ca, cb = a.get("nd_clip"), b.get("nd_clip")
                    if family(ca) == family(cb):
                        same += 1
                        if ca == cb and LEN and abs(((a["nd_phase"] - b["nd_phase"]) / LEN[ca] + 0.5) % 1.0 - 0.5) < 0.1:
                            samephase += 1
        print("%s: %d figures, %d clip families; neighbours within %.1f m: %d pairs, %d on one family, %d of those on one clip "
              "within a tenth of its loop" % (name, len(sel), len(fam), radius, pairs, same, samephase))
        print("   ", ", ".join("%s %d" % kv for kv in fam.most_common()))
    for e in [x for x in pop.get("edits", []) if x.get("script") == "blender/nd_variety.py"]:
        print("   ", {k: v for k, v in e.items() if k not in ("what", "families", "script", "by")})


if __name__ == "__main__":
    if "--report" in sys.argv:
        pop = json.load(open(os.path.join(ART, "population.json")))
        poses = json.load(open(os.path.join(ART, "poses.json")))
        LEN = dict(poses["stock"])
        LEN.update({k: v["seconds"] for k, v in poses["authored"].items()})
        LEN.update({k: v["seconds"] for k, v in poses["mirrored_stock"].items()})
        report(pop, LEN)
    else:
        main()
