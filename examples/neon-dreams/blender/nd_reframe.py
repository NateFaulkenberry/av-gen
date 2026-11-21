#!/usr/bin/env python3
"""Neon Dreams, iteration 11 (owner msgs 130, 132, 136): the journey reframed closer to the aliens.

    python3 blender/nd_reframe.py [--root examples/neon-dreams]      (plain Python, no Blender; idempotent)

Edits, in the engine frame (Y up, z = -north), the art data under ROOT/art:
  layout.json        camera.waypoints (the new journey); three fill isles moved beside it (instances, lights, islands)
  cloud_design.json  the moved isles' own seat lobes (tagged with the isle's bob) moved with them
  population.json    the moved isles' figures and groups moved with them; new figures along the pier (fishers, rail
                     leaners, a talking group, strollers) and on the moved isles; the early flight re-routed into a long
                     follow up the pier (msg 132's framing); an `edits` record. Tiers are re-cast separately
                     (nd_reframe_tiers.py) from what the new camera sees.
It reads the ORIGINAL positions from the `it11` record when it has already run, so a second run gives the same files.

The pier frame used below: origin at the first intact span's outer end O = plan (-317, -164), s along the pier toward
the root (heading 34.7 degrees, the root at s = 242), n to its south-east side (the deck spans n = -2.5..2.5).
"""
import json, math, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
argv = sys.argv[1:]
ROOT = argv[argv.index("--root") + 1] if "--root" in argv else os.path.join(HERE, "..")
ART = os.path.join(ROOT, "art")
O = (-317.0, -164.0)
D = (0.8216, 0.5698)
N = (0.5698, -0.8216)
DECK = 131.2


def pier(s, n, z):
    """Engine point at pier coordinates (s, n) and height z."""
    x, y = O[0] + s * D[0] + n * N[0], O[1] + s * D[1] + n * N[1]
    return [round(x, 3), round(z, 3), round(-y, 3)]


def plan(x, y, z):
    return [round(x, 3), round(z, 3), round(-y, 3)]


# ---------------------------------------------------------------------------------------------- the journey
# Times are the song's; only 0-31 s, 44-47 s and 58.95-70.2 s move. Into the Quay (31 s), the close pass (35.75 s),
# into the Veil (47 s), out on the crash (56.88 s) and the lip (58.95 s) are where they were.
OPENING = [
    (0.0, pier(-30, 12.0, 119.0)),     # in the swell beside the pier's dropped outer spans, the moved west2 isle ahead right
    (2.8, pier(-22, 10.5, 127.5)),     # first sight (crash): rising out of the swell, the pier runs to the Lantern
    (5.6, pier(-12, 8.5, 131.5)),      # the pilot lifted off the west2 isle on the crash, 31 m ahead (msg 132's framing)
    (8.4, pier(-1, 7.0, 133.0)),      # the lens lingers while the pilot crosses ahead, right to left, to the pier
    (11.0, pier(10, 6.4, 133.3)),      # at rail height, 6.4 m off the centre line (3.9 m beyond the south-east rail)
    (14.0, pier(40, 6.4, 133.3)),      # the pilot comes down on the first span ahead
    (17.0, pier(82.3, 6.4, 133.3)),
    (20.0, pier(124.6, 6.4, 133.3)),
    (23.0, pier(166.9, 6.4, 133.3)),     # the moved inner2 isle passes on the right
    (26.0, pier(209.2, 6.4, 133.6)),
    (28.6, pier(246, 6.8, 136.0)),     # the root: the station's platform on the left, the plaza crowd ahead
    (31.0, plan(-92, -2.0, 140.0)),    # into the Quay (as before)
]
TAIL_GARDEN = [                        # 44-47 s: low over the garden's gatherings, then up into the Veil as before
    (45.0, plan(96, -22.0, 128.5)),
    (46.2, plan(126, -31.0, 131.5)),
]
TAIL_END = [                           # after the lip: a slow drift toward the moved Lone Lamp isle, the works on the left
    (62.5, plan(728, 56.0, 223.5)),
    (66.5, plan(792, 62.0, 221.0)),
    (70.2, plan(836, 66.0, 219.5)),
]

# ---------------------------------------------------------------------------------------------- the moved isles
# bob name -> new plan centre (x, y) and the new top of the rock (deck height); everything carrying the bob moves.
MOVES = {
    "isle_west2": ("pier", (12.0, 24.0), 124.5),      # ahead right of the rise from the trough (0-11 s)
    "isle_inner2": ("pier", (165.0, 27.0), 126.5),    # beside the pier run at 22-24 s
    "lone_lamp": ("plan", (890.0, 49.0), 214.0),      # right of the end drift, 5 m under the lens: the last frame's right third
}


SEAT_R = 30.0                          # lobes of a moved isle's bob within this of its centre are its seat
SEAT_SOFT = {"lone_lamp": (0.45, 0.7)}   # density, soft of the moved seat lobes (a thin collar, not balls)


def target_centre(spec):
    kind, (a, b), z = spec
    if kind == "pier":
        e = pier(a, b, z)
        return e[0], -e[2], z
    return a, b, z


def move_all(lay, des, pop, orig):
    log = {}
    for bob, spec in MOVES.items():
        isl = next(i for i in lay["islands"] if i.get("bob") == bob)
        c0 = orig["centres"][bob]                      # the original engine centre
        x, y, z = target_centre(spec)
        dv = [x - c0[0], z - c0[1], -y - c0[2]]
        moved = 0

        def mv(p):
            return [round(p[0] + dv[0], 3), round(p[1] + dv[1], 3), round(p[2] + dv[2], 3)]
        for i in lay["instances"]:
            if i.get("bob") == bob:
                i["pos"] = mv(orig["inst"][i["id"]]); moved += 1
        for li in lay["lights"]:
            if li.get("bob") == bob:
                li["pos"] = mv(orig["lights"][li["name"]])
        for k, il in enumerate(lay["islands"]):
            if il.get("bob") == bob:
                il["centre"] = mv(orig["islands"][str(k)])
        for name, lobes in des["masses"].items():
            if not isinstance(lobes, list):
                continue
            for k, lb in enumerate(lobes):
                if isinstance(lb, dict) and lb.get("bob") == bob:
                    oc = orig["lobes"]["%s/%d" % (name, k)]
                    # only the rock's own seat moves; a formation that rides the same bob further off (the Lone
                    # Lamp's cumulus, 50-90 m away) stays where it was: moved, it stood beside the reveal as a column
                    if math.hypot(oc[0] - c0[0], oc[2] - c0[2]) <= SEAT_R:
                        lb["c"] = mv(oc)
                        if bob in SEAT_SOFT:          # seen at 20-60 m the seat's dense balls read as white blobs
                            lb["density"], lb["soft"] = SEAT_SOFT[bob]
                    else:
                        lb["c"] = list(oc)
        for s in pop["spots"]:
            if s.get("bob") == bob and s["id"] in orig["spots"]:
                s["pos"] = mv(orig["spots"][s["id"]])
                if s.get("look_at") and s["id"] in orig["look"]:
                    s["look_at"] = orig["look"][s["id"]]
        for g in pop["groups"]:
            if g.get("bob") == bob:
                g["centre"] = mv(orig["groups"][g["id"]])
        for p in pop["paths"]:
            if p.get("bob") == bob:
                p["points"] = [mv(q) for q in orig["paths"][p["id"]]]
        log[bob] = {"delta_m": [round(v, 2) for v in dv], "instances": moved}
    return log


def snapshot(lay, des, pop):
    """The original positions of everything a move touches (kept in population.json's it11 edit record)."""
    o = {"centres": {}, "inst": {}, "lights": {}, "islands": {}, "lobes": {}, "spots": {}, "look": {}, "groups": {}, "paths": {}}
    for bob in MOVES:
        isl = next(i for i in lay["islands"] if i.get("bob") == bob)
        o["centres"][bob] = list(isl["centre"])
    for i in lay["instances"]:
        if i.get("bob") in MOVES:
            o["inst"][i["id"]] = list(i["pos"])
    for li in lay["lights"]:
        if li.get("bob") in MOVES:
            o["lights"][li["name"]] = list(li["pos"])
    for k, il in enumerate(lay["islands"]):
        if il.get("bob") in MOVES:
            o["islands"][str(k)] = list(il["centre"])
    for name, lobes in des["masses"].items():
        if isinstance(lobes, list):
            for k, lb in enumerate(lobes):
                if isinstance(lb, dict) and lb.get("bob") in MOVES:
                    o["lobes"]["%s/%d" % (name, k)] = list(lb["c"])
    for s in pop["spots"]:
        if s.get("bob") in MOVES:
            o["spots"][s["id"]] = list(s["pos"])
            if s.get("look_at"):
                o["look"][s["id"]] = s["look_at"]
    for g in pop["groups"]:
        if g.get("bob") in MOVES:
            o["groups"][g["id"]] = list(g["centre"])
    for p in pop["paths"]:
        if p.get("bob") in MOVES:
            o["paths"][p["id"]] = [list(q) for q in p["points"]]
    return o


# ---------------------------------------------------------------------------------------------- new figures
CAST = ["alien-scout", "alien-elder", "alien-diver", "alien-pilot", "alien-ranger", "alien-trooper"]
CLIPS = {"fish": ["Idle", "Take_from_floor"], "look_sea": ["Idle", "Idle_turn"], "stand": ["Idle", "Idle_turn"],
         "gather": ["Idle", "Idle_turn", "Fight_idle"], "tend": ["Take_from_floor", "Idle"], "look_lantern": ["Idle", "Idle_turn"]}


def facing(fx, fz):
    L = math.hypot(fx, fz)
    fx, fz = fx / L, fz / L
    return round(math.degrees(math.atan2(fx, fz)), 1), [round(fx, 4), 0.0, round(fz, 4)]


def face_plan(heading_deg):
    """Engine facing for a plan heading (0 = east, 90 = north)."""
    h = math.radians(heading_deg)
    return facing(math.cos(h), -math.sin(h))


PIER_HDG = math.degrees(math.atan2(D[1], D[0]))       # 34.7: toward the root
SE = PIER_HDG - 90.0                                   # facing out over the south-east rail (toward the lens)
NW = PIER_HDG + 90.0
LANTERN = [-117.0, 178.0, -30.0]

# (s, n, activity, plan heading, note) on the deck (131.2); the south-east rail is n = +2.2
PIER_NEW = [
    (24.0, 2.1, "fish", SE, "a fisher at the south-east rail, line over the cloud: faces the lens as it comes"),
    (37.0, 2.0, "look_sea", SE + 15, "leans on the rail"),
    (46.0, 2.1, "fish", SE - 10, "a fisher"),
    (47.2, 1.2, "stand", SE + 160, "talks to the fisher"),
    (58.0, -1.9, "look_sea", NW, "at the far rail, looking toward the Lampmarket"),
    (71.0, 2.1, "fish", SE, "a fisher"),
    (88.0, 1.4, "gather", 0, "a group of three on the deck"),
    (89.4, 0.6, "gather", 0, ""),
    (88.6, -0.2, "gather", 0, ""),
    (104.0, 2.1, "look_sea", SE - 5, "leans on the rail"),
    (112.0, 2.0, "fish", SE + 8, "a fisher"),
    (138.0, 1.9, "look_sea", SE + 20, "watching the inner2 isle's garden"),
    (141.0, 2.1, "fish", SE - 12, "a fisher"),
    (166.0, 1.8, "stand", PIER_HDG + 180, "looks back down the pier toward the sea"),
    (186.0, 2.1, "look_sea", SE, "leans on the rail"),
]
# two strollers more, on the open middle third
PIER_WALKS = [((30.0, 0.9), (118.0, 1.0), 0.9, ["alien-trooper", "alien-scout"], [3.0, 70.0]),
              ((60.0, -0.8), (150.0, -0.9), 0.85, ["alien-elder", "alien-diver"], [20.0, 110.0])]


def new_spots(pop):
    out = []
    k = 0
    for s, n, act, hdg, note in PIER_NEW:
        if act == "gather":
            continue
        fd, fw = face_plan(hdg)
        out.append({"id": "it11_%03d" % k, "activity": act, "pos": pier(s, n, DECK), "facing_deg": fd, "forward": fw,
                    "district": "pier", "cast": CAST[(k * 5 + 1) % 6], "clips": CLIPS[act], "tier": "A",
                    "visible": {}, "react": ["turn_to_lantern", "sway"] if act != "fish" else ["sway"],
                    "zone": "pier", "source": "it11 (msg 130): beside the new pier run", "note": note})
        k += 1
    # the talking group of three: faces its centre
    g = [r for r in PIER_NEW if r[2] == "gather"]
    cx = sum(pier(r[0], r[1], DECK)[0] for r in g) / 3; cz = sum(pier(r[0], r[1], DECK)[2] for r in g) / 3
    ids = []
    for r in g:
        p = pier(r[0], r[1], DECK)
        fd, fw = facing(cx - p[0], cz - p[2])
        sid = "it11_%03d" % k; ids.append(sid)
        out.append({"id": sid, "activity": "gather", "pos": p, "facing_deg": fd, "forward": fw, "district": "pier",
                    "cast": CAST[(k * 5 + 1) % 6], "clips": CLIPS["gather"], "tier": "A", "visible": {},
                    "react": ["turn_to_lantern", "sway"], "group": "grp_it11_pier", "zone": "pier",
                    "source": "it11 (msg 130)", "note": "a group of three talking on the pier"})
        k += 1
    groups = [{"id": "grp_it11_pier", "district": "pier", "centre": [round(cx, 3), DECK, round(cz, 3)], "members": ids, "bob": None,
               "note": "it11: a ring of three facing its centre"}]
    paths = []
    for j, ((s0, n0), (s1, n1), sp, cast, ph) in enumerate(PIER_WALKS):
        a, b = pier(s0, n0, DECK), pier(s1, n1, DECK)
        paths.append({"id": "walk_it11_%d" % j, "district": "pier", "points": [a, b], "speed_mps": sp, "mode": "pingpong",
                      "clip": "Walking_low_grav", "walkers": 2, "pause_s": 2.5, "phases_s": ph, "cast": cast, "bob": None,
                      "length_m": round(math.dist(a, b), 1), "tier": "A", "visible": {}, "react": ["sway"],
                      "note": "it11 (msg 130): strollers on the open middle of the pier"})
    return out, groups, paths


WORKS = (2000.0, 450.0)               # plan: the Condensery's deck, what the Lone Lamp's people watch


def lamp_spots(lay):
    """it11 (msg 136): the people of the Lone Lamp, watching the works from its pads (the end's foreground)."""
    pads = sorted([i["pos"] for i in lay["instances"] if i.get("bob") == "lone_lamp" and i["asset"].endswith("deck_pad_6m.glb")],
                  key=lambda p: p[0])
    out, ids = [], []
    mid = pads[1]
    look = math.degrees(math.atan2(WORKS[1] - (-mid[2]), WORKS[0] - mid[0]))
    for k, (dx, dz, act) in enumerate([(-1.2, -0.8, "look_moon"), (0.3, 1.1, "look_moon"), (1.3, -0.6, "look_moon")]):
        p = [round(mid[0] + dx, 3), mid[1], round(mid[2] + dz, 3)]
        fd, fw = face_plan(look + (k - 1) * 12)
        sid = "it11_lamp_%d" % k; ids.append(sid)
        out.append({"id": sid, "activity": act, "pos": p, "facing_deg": fd, "forward": fw, "district": "isle:lone_lamp",
                    "cast": CAST[(k * 2 + 1) % 6], "clips": ["Idle", "Idle_turn"], "tier": "A", "visible": {},
                    "react": ["turn_to_lantern"], "group": "grp_it11_lamp", "bob": "lone_lamp", "zone": "lone_lamp",
                    "look_at": [WORKS[0], 230.0, -WORKS[1]], "source": "it11 (msg 136)",
                    "note": "watches the works from the Lone Lamp's pad: the end's foreground (seen from behind)"})
    g = pads[0]
    fd, fw = face_plan(look + 160)
    out.append({"id": "it11_lamp_3", "activity": "tend", "pos": [round(g[0] + 0.8, 3), g[1], round(g[2] + 0.6, 3)], "facing_deg": fd,
                "forward": fw, "district": "isle:lone_lamp", "cast": "alien-elder", "clips": ["Take_from_floor", "Idle"], "tier": "A",
                "visible": {}, "react": [], "bob": "lone_lamp", "zone": "lone_lamp", "source": "it11 (msg 136)",
                "note": "tends the pad's planters, facing the lens"})
    grp = {"id": "grp_it11_lamp", "district": "isle:lone_lamp", "centre": [mid[0], mid[1], mid[2]], "members": ids,
           "bob": "lone_lamp", "note": "it11: three watching the works (they face it, not each other)"}
    return out, grp


# ---------------------------------------------------------------------------------------------- the early flight
# msg 132: the wide clip's framing (a pilot about 60 px tall at 1080p, 30-35 m off, side on, the plume and the town
# behind) for the early take-off. The pilot on the moved west2 isle lifts off 34 m ahead-right of the lens as it rises
# out of the swell, crosses ahead of it right to left while it lingers, and comes down on the pier's first span ahead of
# it, about 20 m off when the feet touch (the lens then runs on past it). The engine's flight eases over the whole
# route, so a short hop (46 m) over 10 s keeps it near a walking jet-pack's pace.
FLY_ID = "fly_pier_isle"
FLY_FROM_ISLE = "isle_west2"
FLY_FROM_OFF = (-1.53, 6.51, -0.88)  # engine offset from the isle's centre (its deck): the house's roof terrace
FLY_TO = (48.0, -1.0)              # pier (s, n): the deck's north-west half, open sky (the cables are 14 m up at n = +-2.3)
FLY_CRUISE = 136.0
FLY_T0 = 2.25                      # the jump starts (feet leave 0.57 s later, snapped to a kick)
FLY_TD = 12.6                      # touchdown (snapped to a kick)


def kicks():
    ev = json.load(open(os.path.join(ROOT, "audio", "sabertooth_events.json")))
    return [t for t, _ in ev["stems"]["kick"]["onsets"]]


def flight(pop, lay):
    K = kicks()
    f = next(x for x in pop["flyers"] if x["id"] == FLY_ID)
    B = pier(FLY_TO[0], FLY_TO[1], DECK)
    isl = next(i for i in lay["islands"] if i.get("bob") == FLY_FROM_ISLE)
    # the house's roof terrace (open sky by build.py --check-flights --open-near): from the deck the pilot rose
    # dark against the dark house; from the roof it rises against the town's lights and the sky
    A = [round(isl["centre"][i] + FLY_FROM_OFF[i], 3) for i in range(3)]
    dx, dz = B[0] - A[0], B[2] - A[2]; L0 = math.hypot(dx, dz)
    lift = min(K, key=lambda k: abs(k - (FLY_T0 + 0.567)))
    t0 = round(lift - 0.567, 3)
    td = round(min(K, key=lambda k: abs(k - FLY_TD)), 3)
    u = [dx / L0, dz / L0]
    route = [[A[0] + u[0] * 7, FLY_CRUISE - 0.5, A[2] + u[1] * 7],
             [(A[0] + B[0]) / 2, FLY_CRUISE, (A[2] + B[2]) / 2],
             [B[0] - u[0] * 7, FLY_CRUISE, B[2] - u[1] * 7]]
    route = [[round(v, 3) for v in p] for p in route]
    L = sum(math.dist(route[i], route[i + 1]) for i in range(len(route) - 1))
    tc0, tc1 = t0 + 2.6, td - 2.8
    keys = [{"t": round(t0 - 1.2, 3), "pos": A, "clip": "Idle"}, {"t": t0, "pos": A, "clip": "Jumping"},
            {"t": round(t0 + 0.8, 3), "pos": [A[0], A[1] + 0.6, A[2]], "clip": "Floating"}]
    acc = 0.0
    for i, p in enumerate(route):
        if i:
            acc += math.dist(route[i - 1], p)
        keys.append({"t": round(tc0 + (tc1 - tc0) * acc / L, 3), "pos": p, "clip": "Flying_jet"})
    keys.append({"t": round(tc1, 3), "pos": [B[0], B[1] + 7.0, B[2]], "clip": "Floating"})
    keys.append({"t": td, "pos": [B[0], B[1] + 0.4, B[2]], "clip": "Landing"})
    keys.append({"t": round(td + 0.9, 3), "pos": B, "clip": "Idle"})
    yaw = round(math.degrees(math.atan2(dx, dz)), 1)
    for kk in keys:
        kk["yaw_deg"] = yaw
    phases, last = [], None
    for kk in keys:
        if kk["clip"] != last:
            phases.append({"t": kk["t"], "clip": kk["clip"]}); last = kk["clip"]
    f.update({"note": "it11 (msgs 130, 132): lifts off the moved west2 isle 34 m ahead-right of the rising lens, crosses "
                      "ahead of it to the left and lands on the pier's first span about 20 m ahead of it",
              "t_start": keys[0]["t"], "t_takeoff": t0, "t_land": td, "duration_s": round(keys[-1]["t"] - keys[0]["t"], 2),
              "on_beat": {"liftoff_kick_s": round(lift, 3), "touchdown_kick_s": td, "cruise_stretch": 1.0},
              "takeoff": {"pos": A, "district": "isle:" + FLY_FROM_ISLE, "bob": FLY_FROM_ISLE},
              "landing": {"pos": B, "district": "pier", "bob": None},
              "cruise_height_m": FLY_CRUISE, "speed_mps": round(L / (tc1 - tc0), 2),
              "camera_min_dist_m": None, "phases": phases, "keys": keys})
    return {"t0": t0, "td": td, "route_m": round(L, 1)}


def main():
    lay = json.load(open(os.path.join(ART, "layout.json")))
    des = json.load(open(os.path.join(ART, "cloud_design.json")))
    pop = json.load(open(os.path.join(ART, "population.json")))
    rec = next((e for e in pop.get("edits", []) if e.get("it") == 11), None)
    if rec is None:
        rec = {"it": 11, "msg": [130, 132, 136], "by": "art", "script": "blender/nd_reframe.py", "orig": snapshot(lay, des, pop)}
        pop.setdefault("edits", []).append(rec)
    orig = rec["orig"]
    # waypoints: keep the Quay and the Veil/reveal anchors
    wps = lay["camera"]["waypoints"]
    keep = [w for w in wps if 31.0 < w["t"] <= 44.0 or 47.0 <= w["t"] <= 58.95]
    new = [{"t": t, "pos": p} for t, p in OPENING + TAIL_GARDEN + TAIL_END]
    lay["camera"]["waypoints"] = sorted(keep + new, key=lambda w: w["t"])
    lay["camera"]["note"] = ("the 70.2 s journey; it11 (owner msgs 130, 132, 136): the opening slowed and brought down to "
                             "the pier's rail, low over the garden, a slow drift past the Lone Lamp isle at the end; "
                             "the Quay and the Veil as before; the camera looks along its path")
    # new figures replace any from an earlier run (taken out before the moves, which work from the snapshot)
    pop["spots"] = [s for s in pop["spots"] if not s["id"].startswith("it11_")]
    pop["groups"] = [g for g in pop["groups"] if not g["id"].startswith("grp_it11")]
    pop["paths"] = [p for p in pop["paths"] if not p["id"].startswith("walk_it11")]
    rec["moves"] = move_all(lay, des, pop, orig)
    sp, gr, pa = new_spots(pop)
    ls, lg = lamp_spots(lay)
    sp += ls; gr.append(lg)
    pop["spots"] += sp; pop["groups"] += gr; pop["paths"] += pa
    rec["added"] = {"spots": len(sp), "groups": len(gr), "paths": len(pa)}
    rec["flight"] = flight(pop, lay)
    json.dump(lay, open(os.path.join(ART, "layout.json"), "w"), indent=1)
    json.dump(des, open(os.path.join(ART, "cloud_design.json"), "w"), indent=None, separators=(",", ":"))
    json.dump(pop, open(os.path.join(ART, "population.json"), "w"), indent=None, separators=(",", ":"))
    print(json.dumps({k: rec[k] for k in ("moves", "added", "flight")}))


if __name__ == "__main__":
    main()
