#!/usr/bin/env python3
"""Neon Dreams: write art/layout.json -- where every kit asset stands, in ENGINE coordinates (see nd_world.py).

    python3 examples/neon-dreams/blender/nd_layout.py            (pure Python, deterministic; reads meshes/kit_manifest.json)

The arrangement is authored here, not scattered: the S-curved Quay with its two stairs, north row tall and south row low
with roof terraces at the camera's height, pocket gardens in the gaps, the Steps (a flight of planted terraces) inside
the bend, the stepped Stacks, plaza, station and pier, the Great Lantern, the east garden, three lighthouses on cloud
rocks, the Condensery and the Lone Lamp. It also marks where the inhabitants stand and walk, the lighthouse beams, the
plume emitters, suggested lights, the splines for vehicles and the journey itself, and it checks itself: no two
buildings' envelopes overlap, and nothing comes within 2 m of the camera path. Schema: notes/00-coordination.md.
"""
import json, math, os, random, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from nd_world import *  # noqa

ROOT = os.path.join(HERE, "..")
MAN = {a["asset"]: a for a in json.load(open(os.environ.get("ND_MANIFEST", os.path.join(ROOT, "meshes", "kit_manifest.json"))))["assets"]}   # ND_MANIFEST, and a path as the first argument: a scratch run that writes nothing here
R = random.Random(20261119)
INST, SPOTS, PATHS, LIGHTS, ENV, BOX = [], [], [], [], [], []
STRING_KEYS = []          # the anchor pair of each lantern string, in order (for the mesh check to refuse one)
SIGN_KEYS = ["sign_a", "sign_a", "sign_b", "sign_c", "sign_a", "sign_b"]
WIN_COOL = ["win_a", "win_a", "win_a", "win_b", "win_b", "win_c"]


def rot(yaw, lx, ly):
    c, s = math.cos(yaw), math.sin(yaw)
    return lx * c - ly * s, lx * s + ly * c


def qmul(a, b):                                     # quaternions as (w, x, y, z)
    return (a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3], a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2],
            a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1], a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0])


def quat_engine(yaw, pitch=0.0, roll=0.0):
    """Plan yaw about +z, then pitch about the asset's local x, then roll about the asset's local y (plan frame); returned
    as an engine quaternion [x, y, z, w]."""
    qz_ = (math.cos(yaw / 2), 0, 0, math.sin(yaw / 2)); qx = (math.cos(pitch / 2), math.sin(pitch / 2), 0, 0); qy_ = (math.cos(roll / 2), 0, math.sin(roll / 2), 0)
    w, x, y, z = qmul(qmul(qz_, qx), qy_)
    return [round(x, 6), round(z, 6), round(-y, 6), round(w, 6)]


def inst(asset, x, y, z, yaw=0.0, scale=1.0, zone="", tint=None, color=None, pitch=0.0, roll=0.0, iid=None, env=True, tags=None, gain=None):
    """Place an asset at a PLAN position. scale: a number, or [sx, sy, sz] in the GLB's own axes (x, y up, z front)."""
    assert asset in MAN, asset
    d = {"id": iid or "%s_%03d" % (os.path.basename(asset).split(".")[0], len(INST)), "asset": asset, "pos": E((x, y, z)), "rot_y_deg": round(math.degrees(yaw), 3),
         "quat": quat_engine(yaw, pitch, roll), "scale": [scale] * 3 if isinstance(scale, (int, float)) else list(scale), "zone": zone}
    if tint:
        d["tint"] = tint
    if color:
        d["color"] = color
    if gain:
        d["gain"] = gain
    if tags:
        d["tags"] = tags
    INST.append(d)
    if "bounds_m" in MAN[asset]:
        b = MAN[asset]["bounds_m"]; s = d["scale"]
        e = dict(id=d["id"], c=(x, y), yaw=yaw, x0=b[0] * s[0], x1=b[3] * s[0], y0=-b[5] * s[2], y1=-b[2] * s[2], z0=z + max(b[1], -2.0) * s[1], z1=z + b[4] * s[1], zone=zone)
        BOX.append(e)                                           # everything, for the camera's clearance
        if env:
            ENV.append(e)                                       # buildings, for the overlap check
    return d


def spot(kind, x, y, z, face, zone, prio=2, look=None, clip=None, note=None):
    """An inhabitant's place. face: plan heading in degrees (0 = +x east, 90 = north)."""
    d = {"id": "spot_%03d" % len(SPOTS), "kind": kind, "pos": E((x, y, z)), "facing_deg": round(face, 1),
         "forward": [round(math.cos(math.radians(face)), 4), 0.0, round(-math.sin(math.radians(face)), 4)], "zone": zone, "priority": prio,
         "clips": clip or {"stand": ["Idle", "Idle_turn"], "look": ["Idle_turn", "Idle"], "rail": ["Idle", "Idle_turn"], "gather": ["Idle", "Idle_turn"],
                           "wait": ["Idle", "Idle_turn"], "use": ["Button_push", "Take_from_table"], "tend": ["Take_from_floor", "Idle"]}[kind]}
    if look:
        d["look_at"] = E(look)
    if note:
        d["note"] = note
    SPOTS.append(d)
    return d


def path(name, pts, zone, prio=2, speed=1.25, mode="pingpong", clip="Walking", count=1, note=None):
    PATHS.append({"id": name, "points": [E(p) for p in pts], "zone": zone, "priority": prio, "speed_mps": speed, "mode": mode, "clip": clip, "walkers": count,
                  **({"note": note} if note else {})})


def light(x, y, z, key, radius, intensity, zone, name=None):
    LIGHTS.append({"name": name or "light_%03d" % len(LIGHTS), "pos": E((x, y, z)), "key": key, "radius_m": radius, "intensity": intensity, "zone": zone})


def tint_for(asset, warm_share=0.3):
    """Window and line tints of a building: one cool window family per building, amber for the warm panes."""
    return {"win_warm": "win_d", "win_cool": R.choice(WIN_COOL), "strip": "eave"}


# Iteration 8: a building's hue is its facade's base colour factor, written in its GLB (nd_kitcore.SCHEME: muted plum,
# slate, rust, bone, ink). The same asset stands in a row two to four times, so every repeat takes another of the
# Lanternwright wall colours here (ADR-2102: partMaterials.facade.baseColor from an instance's "color"). Each row
# building is already a node of its own (its window tint differs), so this adds no nodes.
WALLS = {"plum": [0.066, 0.036, 0.062], "slate": [0.032, 0.047, 0.078], "rust": [0.094, 0.044, 0.028], "bone": [0.104, 0.094, 0.080], "ink": [0.030, 0.029, 0.046],
         "moss": [0.038, 0.056, 0.050], "wine": [0.078, 0.032, 0.044], "ash": [0.066, 0.064, 0.074]}      # as nd_kitcore's SCHEME (second pass)
OWN_WALL = {"shophouse_a": "plum", "shophouse_b": "slate", "shophouse_c": "bone", "shophouse_d": "rust", "shophouse_e": "slate", "shophouse_f": "plum", "shophouse_g": "bone",
            "shophouse_h": "ink", "shophouse_i": "rust", "shophouse_j": "bone", "stack_a": "slate", "stack_b": "plum", "stack_c": "ink", "stack_d": "rust", "stack_e": "bone"}
_WALL_SEEN = {}


def wall_for(asset):
    """None for the first of an asset (its own colour); for each repeat the next wall colour that is not its own."""
    nm = os.path.basename(asset).split(".")[0].replace("mooring_", ""); k = _WALL_SEEN.get(nm, 0); _WALL_SEEN[nm] = k + 1
    if k == 0 or nm not in OWN_WALL:
        return None
    order = ["rust", "ash", "slate", "wine", "bone", "moss", "plum", "ink"]; order = [c for c in order if c != OWN_WALL[nm]]
    return {"facade": WALLS[order[(k - 1 + sum(map(ord, nm))) % len(order)]]}


# ============================================================================================ the Quay
ZONES = {}
BLD = {}                                              # id -> (asset, x, y, z, yaw) of row buildings, for signs and spots


def quay_decks():
    spans = [(QUAY_X0, STAIRS[0] - 2.4), (STAIRS[0] + 2.4, STAIRS[1] - 2.4), (STAIRS[1] + 2.4, QUAY_X1)]
    for a, b in spans:
        n = max(1, round((b - a) / 6.0)); L = (b - a) / n
        for i in range(n):
            x = a + (i + 0.5) * L
            inst("decks/deck_quay_6m.glb", x, qy(x), qz(x), qyaw(x), [L / 6.0 * 1.012, 1, 1], "quay_deck", env=False)
    for xs in STAIRS:
        inst("decks/deck_stair_3m2.glb", xs, qy(xs), qz(xs + 3), qyaw(xs), 1.0, "quay_deck", env=False, tint={"strip": "eave"})


def pack_row(side, tokens, x_start):
    """Lay buildings along one side of the Quay. side = +1 north (faces south), -1 south. tokens: ('b', asset) |
    ('gap', metres, kind) | ('until', x). Spacing uses the assets' true envelopes (eaves, shifted blocks, vines)."""
    cur = x_start; out = []; gaps = []
    for tk in tokens:
        if tk[0] == "until":
            if tk[1] > cur:
                gaps.append((cur, tk[1], tk[2] if len(tk) > 2 else "alley")); cur = tk[1]
            continue
        if tk[0] == "gap":
            gaps.append((cur, cur + tk[1], tk[2])); cur += tk[1]; continue
        asset = "buildings/%s.glb" % tk[1]; m = MAN[asset]; b = m["bounds_m"]; w, d = m["footprint_m"]
        ex0, ex1 = (b[0], b[3]) if side > 0 else (-b[3], -b[0])
        xc = cur - ex0 + 0.35
        for xs in STAIRS:                                           # a building's base stands on one level, clear of the stairs
            if xc - w / 2 - 0.4 < xs + 2.4 and xc + w / 2 + 0.4 > xs - 2.4:
                xc = xs + 2.8 + w / 2
                gaps.append((cur, xc + ex0 - 0.35, "stair"))
        bid = "%s_%s%02d" % (tk[1].replace("mooring_", ""), "n" if side > 0 else "s", len(out))
        while True:                                                 # the street curves: step along until this envelope clears the last one
            yaw = qyaw(xc) + (0 if side > 0 else math.pi); yc = qy(xc) + side * (4.3 + d / 2 + 0.05)
            e = dict(c=(xc, yc), yaw=yaw, x0=b[0], x1=b[3], y0=-b[5], y1=-b[2], z0=0, z1=1)
            if not out or not obb_overlap(e, dict(prev, z0=0, z1=1), 0.25):
                break
            xc += 0.25
        prev = e
        inst(asset, xc, yc, qz(xc), yaw, 1.0, "quay_north" if side > 0 else "quay_south", tint=tint_for(asset), color=wall_for(asset), iid=bid)
        BLD[bid] = (asset, xc, yc, qz(xc), yaw, side)
        out.append(bid); cur = xc + ex1 + 0.35
    return out, gaps, cur


def building_point(bid, lx, ly, lz=0.0):
    asset, x, y, z, yaw, side = BLD[bid]
    dx, dy = rot(yaw, lx, ly)
    return x + dx, y + dy, z + lz


def front_piers(bid, zrel):
    """World points on the front wall of the block that covers height zrel (above the building's deck), at its cell
    boundaries: where signs, strings and vines may attach without crossing a window. -> [(x, y, lx)], block"""
    asset = BLD[bid][0]
    for blk in MAN[asset].get("blocks", []):
        z0, z1 = blk["z0"], blk["z0"] + blk["floors"] * FLOOR
        if z0 + 0.5 <= zrel <= z1 - 0.5:
            nb = int(blk["w"] / BAY + 1e-6); mg = (blk["w"] - nb * BAY) / 2
            piers = []
            for b in range(nb + 1):
                bb, sb = blk["front_balcony_bay"], blk.get("stair_slot_bay", -1)
                if (bb >= 0 and bb in (b - 1, b)) or (sb >= 0 and sb in (b - 1, b)):      # not beside a balcony or the stair slot
                    continue
                lx = blk["c"][0] - blk["w"] / 2 + mg + b * BAY; ly = blk["c"][1] - blk["d"] / 2
                piers.append(building_point(bid, lx, ly)[:2] + (lx, ly))
            return piers, blk
    return [], None


def dress_shop(bid, north):
    """Shop floor: valance signs under the eave lip or awnings over the bays; a blade sign or a roundel on a pier of an
    upper block; an identity (brand) per building."""
    asset, x, y, z, yaw, side = BLD[bid]; m = MAN[asset]; blk0 = m["blocks"][0]
    brand = R.randrange(8); key = R.choice(SIGN_KEYS)
    nb = int(blk0["w"] / BAY + 1e-6); mg = (blk0["w"] - nb * BAY) / 2; d0 = blk0["d"]
    for b in range(nb):
        if b == nb // 2:
            continue
        lx = -blk0["w"] / 2 + mg + (b + 0.5) * BAY
        u = R.random()
        if u < (0.5 if north else 0.35):                         # a valance sign hung in front of the eave lip, above the pane head
            px, py, pz = building_point(bid, lx, -d0 / 2 - 1.17, 2.5)
            inst("signs/sign_fascia_%d.gltf" % brand, px, py, pz, yaw, 0.48, "signs", tint={"neon_a": key}, env=False)
        elif u < 0.8:
            px, py, pz = building_point(bid, lx, -d0 / 2, 2.62)
            inst("street/awning_1bay.glb", px, py, pz, yaw, 1.0, "street", env=False, color={"cloth": R.choice([[0.09, 0.02, 0.07], [0.03, 0.03, 0.11], [0.10, 0.045, 0.02], [0.05, 0.05, 0.06]])})
    lx, ly, lz = building_point(bid, 0.0, -d0 / 2 - 1.6, 2.4)       # the shop's light spilling on the deck
    light(lx, ly, lz, "amber" if R.random() < 0.6 else "bounce_b", 9.0, 1.0, "quay")
    # one blade or roundel on an upper block
    cand = [blk for blk in m["blocks"][1:] if blk["floors"] >= 2]
    if cand and R.random() < (0.75 if north else 0.45):
        blk = R.choice(cand[:2]); zmid = blk["z0"] + blk["floors"] * FLOOR / 2
        piers, _ = front_piers(bid, zmid)
        if piers:
            px, py, lx_, ly_ = R.choice(piers)
            if R.random() < 0.8:
                s = max(0.55, min(1.25, (blk["floors"] * FLOOR - 1.3) / 6.66))
                inst("signs/sign_blade_%d.gltf" % brand, px, py, z + zmid - 0.1, yaw - math.pi / 2, s, "signs", tint={"neon_a": key}, env=False, tags=["blade"])
            else:
                inst("signs/sign_roundel_%d.gltf" % R.randrange(10), px, py, z + blk["z0"] + 2.2, yaw - math.pi / 2, 0.9, "signs", tint={"neon_a": R.choice(SIGN_KEYS)}, env=False)
    return brand, key


def pocket_garden(x0, x1, side, name, big=True):
    """A planted pocket in a gap of the row: deck pads behind the street edge, an umbel tree in a pit, planters, a bench
    and a lamp. Residents gather here."""
    xc = (x0 + x1) / 2; yaw = qyaw(xc); z = qz(xc); wid = x1 - x0
    n = max(1, int(wid // 5.0)); L = wid / n
    for i in range(n):
        for j in range(2):
            lx = x0 + (i + 0.5) * L; off = side * (4.3 + 3.0 + j * 6.0)
            inst("decks/deck_pad_6m.glb", lx, qy(lx) + off, z, yaw, [L / 6.0, 1, 1.0], "pocket_" + name, env=False)
    cy = qy(xc) + side * (4.3 + 5.5)
    inst("garden/tree_pit.glb", xc + 0.6, cy, z, 0.0, 1.0, "pocket_" + name, env=False, tint={"garden": "garden_b"})
    inst("garden/umbel_tree_s.glb" if not big else "garden/umbel_tree_b.glb", xc + 0.6, cy, z + 0.35, R.uniform(0, 6.28), 1.0, "pocket_" + name, env=False, tint={"garden": "garden_a"})
    inst("garden/garden_bench.glb", xc - 1.6, qy(xc) + side * (4.3 + 2.2), z, yaw + (0 if side > 0 else math.pi), 1.0, "pocket_" + name, env=False)
    inst("garden/planter_round.glb", xc - wid / 2 + 1.0, qy(xc) + side * (4.3 + 1.2), z, 0.0, 1.0, "pocket_" + name, env=False)
    inst("garden/planter_round.glb", xc + wid / 2 - 1.0, qy(xc) + side * (4.3 + 1.4), z, 1.0, 0.85, "pocket_" + name, env=False, tint={"garden": "garden_b"})
    inst("garden/sprig_bed_3m.glb", xc - 1.2, qy(xc) + side * (4.3 + 9.0), z, 0.4, 1.0, "pocket_" + name, env=False)
    inst("garden/planter_trough_c.glb", xc + 0.8, qy(xc) + side * (4.3 + 11.4), z, yaw, 1.0, "pocket_" + name, env=False)
    inst("street/street_lamp.glb", xc + wid / 2 - 0.8, qy(xc) + side * (4.3 + 0.7), z, yaw + math.pi / 2 * side, 1.0, "pocket_" + name, env=False)
    light(xc, cy, z + 3.0, "garden_a", 12.0, 0.8, "pocket_" + name)
    for k in range(3):                                           # a small gathering by the tree, one on the bench side, one tending the bed
        a = 2.1 * k + 0.4; px, py = xc + 0.6 + 2.0 * math.cos(a), cy + 2.0 * math.sin(a) * 0.8
        spot("gather", px, py, z, math.degrees(math.atan2(cy - py, xc + 0.6 - px)), "pocket_" + name, 1, look=(xc + 0.6, cy, z + 3.5))
    spot("tend", xc - 1.2 + 1.7, qy(xc) + side * (4.3 + 9.0), z, 180.0, "pocket_" + name, 2)
    spot("look", xc - 2.6, qy(xc) + side * 4.9, z, -90.0 * side + math.degrees(yaw), "pocket_" + name, 1, note="at the street edge, watching the Quay")


def the_quay():
    quay_decks()
    north, gN, endN = pack_row(+1, [("b", "mooring_shophouse_h"), ("b", "mooring_shophouse_a"), ("b", "mooring_shophouse_g"), ("b", "mooring_shophouse_b"),
                                    ("until", STAIRS[0] + 1.9, "stair"), ("b", "mooring_shophouse_a"), ("gap", 3.2, "gangway"), ("b", "mooring_shophouse_h"),
                                    ("b", "mooring_shophouse_f"), ("gap", 3.2, "gangway"), ("gap", 8.5, "pocket"), ("b", "mooring_shophouse_b"),
                                    ("until", STAIRS[1] + 1.9, "stair"), ("b", "mooring_shophouse_a"), ("b", "mooring_shophouse_h"), ("b", "mooring_shophouse_e"),
                                    ("b", "mooring_corner_tower")], QUAY_X0 - 0.5)
    south, gS, endS = pack_row(-1, [("b", "mooring_shophouse_c"), ("b", "mooring_shophouse_i"), ("b", "mooring_shophouse_f"), ("b", "mooring_shophouse_j"),
                                    ("until", STAIRS[0] + 7.0, "stair"), ("b", "mooring_shophouse_e"), ("gap", 3.2, "gangway"), ("b", "mooring_shophouse_j"),
                                    ("b", "mooring_shophouse_d"), ("gap", 3.2, "gangway"), ("b", "mooring_shophouse_i")], QUAY_X0 + 2.0)
    ZONES["quay"] = dict(north=north, south=south, north_gaps=gN, south_gaps=gS, north_end=endN, south_end=endS)
    for bid in north:
        if "corner" not in bid:
            dress_shop(bid, True)
    for bid in south:
        dress_shop(bid, False)
    # pocket gardens in the north gaps; gangways with stair towers in the 'gangway' gaps
    pk = [g for g in gN if g[2] == "pocket" or (g[2] == "stair" and g[1] - g[0] > 7.0)]
    for i, g in enumerate(pk):
        pocket_garden(g[0] + 0.4, g[1] - 0.4, +1, "n%d" % i, big=(g[2] == "pocket"))
    gwN = [g for g in gN if g[2] == "gangway"]; gwS = [g for g in gS if g[2] == "gangway"]
    for i, (a, b) in enumerate(zip(gwN, gwS)):
        xn, xs_ = (a[0] + a[1]) / 2, (b[0] + b[1]) / 2; z = qz(xn) + 12.8
        pn, ps = (xn, qy(xn) + 4.3 + 2.3), (xs_, qy(xs_) - 4.3 - 2.3)
        mid = ((pn[0] + ps[0]) / 2, (pn[1] + ps[1]) / 2); L = math.hypot(pn[0] - ps[0], pn[1] - ps[1]) - 3.4
        yaw = math.atan2(pn[1] - ps[1], pn[0] - ps[0]) - math.pi / 2
        inst("decks/gangway_garden_10m.glb" if i == 1 else "decks/gangway_10m.glb", mid[0], mid[1], z, yaw, [1, 1, L / 10.0], "gangway", env=False, tint={"strip": "eave"}, iid="gangway_%d" % i)
        inst("decks/alley_stair.glb", pn[0], pn[1], qz(xn), qyaw(xn), 1.0, "gangway", env=False)
        inst("decks/alley_stair.glb", ps[0], ps[1], qz(xs_), qyaw(xs_) + math.pi, 1.0, "gangway", env=False)
        if i == 0:
            inst("garden/vine_curtain_a.glb", mid[0] + 1.02 * math.cos(yaw), mid[1] + 1.02 * math.sin(yaw), z - 0.2, yaw + math.pi / 2, 1.0, "gangway", env=False)
        spot("rail", mid[0] - 0.55 * math.cos(yaw), mid[1] - 0.55 * math.sin(yaw), z, math.degrees(yaw) + 180, "gangway", 1, note="mid-span, looking down the Quay toward the camera's approach")
        path("gangway_%d_cross" % i, [(pn[0], pn[1] - 1.9, z), (ps[0], ps[1] + 1.9, z)], "gangway", 1, 1.1)
    return north, south, gN, gS


def lantern_strings(north, south):
    """Strings cross the Quay from a pier of a north block to a pier of a south block: never in front of a window, and
    always clear of the camera (2.6 m or more above it where both sides are tall enough, else low over the shop
    fronts, where the camera looks down on them). Iteration 5: a candidate that finds no pier pair straight across now
    looks up to 4.2 m along the street for its north pier and lets the string run up to 3.6 m askew to its south pier
    (17 candidates gave 5 strings; the rest ended nowhere and were dropped). A pier that carries a blade or roundel at
    that height is not an anchor, a string never passes within 2.5 m of a gangway, and two strings keep 4.5 m apart."""
    signs = [i for i in INST if i["zone"] == "signs"]
    gangways = [i for i in INST if i["id"].startswith("gangway_")]
    # anchors the mesh check refused (blender/nd_check_strings.py: an end that does not touch a wall or a mast, or a span
    # within 0.15 m of a sign, a vine, a parapet or a neighbour); the generator then takes its next candidate
    rj = os.path.join(HERE, "string_rejects.json")
    rejects = set(json.load(open(rj))["rejects"]) if os.path.exists(rj) else set()
    key = lambda a, b: "%.1f,%.1f,%.1f,%.1f,%.1f" % (a[0], a[1], b[0], b[1], a[2])

    def free(p):
        for s in signs:                                              # engine pos = (x, z, -y); a sign's box from the manifest
            bb = MAN[s["asset"]]["bounds_m"]; sc = s["scale"][1]
            if math.hypot(s["pos"][0] - p[0], -s["pos"][2] - p[1]) < 1.2 and s["pos"][1] + bb[1] * sc - 0.6 < p[2] < s["pos"][1] + bb[4] * sc + 0.6:
                return False
        return True

    def piers_near(ids, za, x, near):
        out = []
        for b in ids:
            piers, blk = front_piers(b, za - BLD[b][3])
            out += [(px, py, za) for (px, py, lx, ly) in piers if abs(px - x) < near]
        return sorted(out, key=lambda q: abs(q[0] - x))

    def masts(a, za):
        out = []
        for bs in south:                                             # the mast of a south roof terrace, if it reaches this height
            m = MAN[BLD[bs][0]]
            if m.get("roof") == "garden":
                rc, rs = m["roof_centre_m"], m["roof_size_m"]
                mp = building_point(bs, rc[0] - rs[0] / 2 + 0.6, rc[1] + rs[1] / 2 - 0.6, 0.0)
                if abs(mp[0] - a[0]) < 4.0 and BLD[bs][3] + m["roof_y_m"] + 3.6 < za < BLD[bs][3] + m["roof_y_m"] + 6.8:
                    out.append((mp[0], mp[1], za))
        return out

    def clear_of_gangways(a, b):
        for g in gangways:
            gx, gz = g["pos"][0], g["pos"][1]
            if min(abs(a[0] - gx), abs(b[0] - gx), abs((a[0] + b[0]) / 2 - gx)) < 2.5 and abs(a[2] - gz) < 3.0:
                return False
        return True

    x = QUAY_X0 + 7.0; k = 0; placed = []
    while x < 46.0:
        zc = min(cam_at(t / 10.0)[2] for t in range(290, 450) if abs(cam_at(t / 10.0)[0] - x) < 6.0)
        pair = None
        R.uniform(2.7, 3.6)                                          # (keeps the layout's random stream where it was)
        heights = [zc + 3.0 + 0.4 * k_ for k_ in range(7)] + [qz(x) + R.uniform(4.3, 4.8)]      # overhead first (the lowest height that works), else low over the shop fronts
        for (near_n, near_s) in ((2.6, 1.9), (4.2, 3.6)):            # straight across first; then along the street and askew
            for za in heights:
                if not (za > zc + 2.5 or za < zc - 1.3):
                    continue
                best = None
                for a in piers_near(north, za, x, near_n):
                    if not free(a):
                        continue
                    cands = [b for b in piers_near(south, za, a[0], near_s) if free(b)]
                    if za > zc:
                        cands += masts(a, za)
                    for b in cands:
                        cost = abs(b[0] - a[0]) + 0.6 * abs(a[0] - x)
                        if key(a, b) not in rejects and clear_of_gangways(a, b) and all(abs((a[0] + b[0]) / 2 - px) >= 4.5 for px in placed) and (best is None or cost < best[0]):
                            best = (cost, a, b)
                    if best and near_n < 3.0:
                        break                                        # the first pass keeps the nearest north pier, as before
                if best:
                    pair = (best[1], best[2]); break
            if pair:
                break
        if pair:
            a, b = pair
            L = math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(3))); yaw = math.atan2(b[1] - a[1], b[0] - a[0])
            span = 9.0 if L <= 12.5 else 16.0                        # the 9 m string stretched to 17 m had lanterns twice as long as wide
            STRING_KEYS.append(key(a, b))
            inst("street/lantern_string_%dm.glb" % span, (a[0] + b[0]) / 2, (a[1] + b[1]) / 2, a[2], yaw, [L / span, 1, 1], "lantern_strings", env=False,
                 tint={"lantern": "amber" if k % 3 else "win_b"}, iid="lantern_string_%02d" % k)
            light((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, a[2] - 1.2, "amber", 8.0, 0.5, "quay")
            placed.append((a[0] + b[0]) / 2)
            k += 1
        x += R.uniform(6.0, 8.5)

def quay_south_side(south, gS):
    """South of the lower Quay there is no row: the Steps fall away to the garden, then a promenade rail with lamps."""
    xa, xb = max(ZONES["quay"]["south_end"] + 1.0, STAIRS[1] + 3.0), 81.0
    xr = ZONES["quay"]["south_end"] + 3.4
    while xr < xa - 1.0:                                           # between the last house and the Steps: a rail and a view
        inst("decks/rail_6m.glb", xr, qy(xr) - 4.15, qz(xr), qyaw(xr), 1.0, "promenade", env=False); xr += 6.0
    for xe in (xa + 4.0, xa + 16.0):                               # short flights from the Quay down to the first terrace
        inst("decks/catwalk_stair.glb", xe, qy(xe) - 4.3 - 0.75, qz(xe) - 0.8, qyaw(xe) - math.pi / 2, [0.28, 0.25, 1.0], "steps", env=False)
    n = max(1, round((xb - xa) / 6.0)); L = (xb - xa) / n
    ZONES["steps"] = dict(x0=xa, x1=xb, levels=4)
    for k in range(4):                                             # four terraces, 0.8 m apart, 6 m deep: Quay level down to the garden's
        for i in range(n):
            x = xa + (i + 0.5) * L; y = qy(x) - 4.3 - 3.0 - k * 6.0 - 1.5; z = qz(x) - 0.8 * (k + 1)
            inst("garden/terrace_step_6m.glb", x, y, z, qyaw(x), [L / 6.0, 1, 1], "steps", env=False, tint={"garden": "garden_b" if (i + k) % 3 == 0 else "garden_a"})
            if (i + 2 * k) % 3 == 1:
                inst("garden/tree_pit.glb", x - 0.8, y + 0.9, z, 0.0, 0.8, "steps", env=False)
                tree = R.choice(["garden/umbel_tree_s.glb", "garden/umbel_tree_s.glb", "garden/umbel_tree_b.glb"])
                inst(tree if k >= 2 else "garden/umbel_tree_s.glb", x - 0.8, y + 0.9, z + 0.28, R.uniform(0, 6.28), R.uniform(0.85, 1.1), "steps", env=False,      # the journey crosses the upper terraces: saplings there
                     tint={"garden": "garden_a" if (i + k) % 2 else "garden_b"})
                light(x - 0.8, y + 0.9, z + 4.0, "garden_a", 10.0, 0.6, "steps")
            elif (i + 2 * k) % 3 == 2:
                inst("garden/garden_bench.glb", x + 0.5, y + 1.6, z, qyaw(x) + math.pi, 1.0, "steps", env=False)
                spot("gather", x + 0.3, y + 0.6, z, -90 + math.degrees(qyaw(x)), "steps", 1 if k < 2 else 2, look=(x, y - 60, z - 10), note="on the Steps, facing the cloud sea")
            else:
                inst("garden/sprig_bed_3m.glb", x - 0.5, y + 0.6, z, R.uniform(0, 6.28), 0.9, "steps", env=False, tint={"garden": "garden_b"})
                if k % 2 == 0:
                    spot("tend", x + 1.2, y + 0.6, z, 180.0, "steps", 2)
    path("steps_stroll", [(xa + 2.0, qy(xa + 2) - 4.3 - 1.2, qz(xa + 2) - 0.8), (xb - 2.0, qy(xb - 2) - 4.3 - 1.2, qz(xb - 2) - 0.8)], "steps", 1, 1.0, clip="Walking_low_grav")
    x = xb + 3.0
    while x < QUAY_X1:                                             # promenade edge: rail, lamps, residents at the rail
        inst("decks/rail_6m.glb", x, qy(x) - 4.15, qz(x), qyaw(x), 1.0, "promenade", env=False)
        if int(x) % 12 < 6:
            inst("street/street_lamp.glb", x + 2.0, qy(x + 2) - 3.7, qz(x), qyaw(x) + math.pi / 2, 1.0, "promenade", env=False)
            light(x + 2.0, qy(x + 2) - 3.7, qz(x) + 3.6, "amber", 9.0, 0.7, "promenade")
        spot("rail", x + R.uniform(-1.5, 1.5), qy(x) - 3.6, qz(x), -90 + math.degrees(qyaw(x)), "promenade", 1, look=(GARDEN[0], GARDEN[1], GARDEN[2] + 6), note="at the rail above the garden")
        x += 6.0
    for g in gS:                                                   # gaps in the south row are landings with a rail and a view
        if g[2] in ("stair",):
            xc = (g[0] + g[1]) / 2
            for xx in (g[0] + 3.2, g[1] - 3.2):
                inst("decks/rail_6m.glb", xx, qy(xx) - 4.15, qz(xx), qyaw(xx), 1.0, "promenade", env=False)
            spot("rail", xc - 3.0, qy(xc - 3) - 3.6, qz(xc - 3), -90 + math.degrees(qyaw(xc)), "quay", 1, note="at the gap by the first stair, looking out over the Low Walk and the cloud sea")
            inst("garden/planter_trough_b.glb", xc + 3.2, qy(xc + 3.2) - 3.7, qz(xc + 3.2), qyaw(xc), 1.0, "promenade", env=False)


def quay_life(north, south):
    """Inhabitants of the street: walkers on each level, lookers at shop bays, on balconies and on the roof terraces."""
    levels = [(QUAY_X0 + 2, STAIRS[0] - 3.4), (STAIRS[0] + 3.4, STAIRS[1] - 3.4), (STAIRS[1] + 3.4, QUAY_X1 - 3)]
    for i, (a, b) in enumerate(levels):
        for side, off in ((1, 2.5), (-1, -2.6)):
            pts = [(x, qy(x) + off, qz((a + b) / 2)) for x in [a + (b - a) * k / 6 for k in range(7)]]
            path("quay_L%d_%s" % (i + 1, "n" if side > 0 else "s"), pts, "quay", 1, 1.2 + 0.1 * i, count=2)
    for bid in north + south:
        asset, x, y, z, yaw, side = BLD[bid]; m = MAN[asset]; blk0 = m["blocks"][0]
        if R.random() < 0.7:                                        # someone looking into a shop bay
            lx = R.choice([-2.4, 0.0, 2.4]) if blk0["w"] > 7.3 else R.choice([-1.2, 1.2])
            px, py, pz = building_point(bid, lx, -blk0["d"] / 2 - 1.5, 0.0)
            spot("look", px, py, pz, math.degrees(yaw) + 90, "quay", 1, look=building_point(bid, lx, -blk0["d"] / 2, 1.6), note="looking into a shop bay")
        for blk in m["blocks"][1:]:                                 # balconies on the front
            bb = blk["front_balcony_bay"]
            if bb >= 0:
                nb = int(blk["w"] / BAY + 1e-6); mg = (blk["w"] - nb * BAY) / 2
                for fl in range(1, blk["floors"]):
                    if R.random() < 0.5:
                        lx = blk["c"][0] - blk["w"] / 2 + mg + (bb + 0.5) * BAY
                        px, py, pz = building_point(bid, lx, blk["c"][1] - blk["d"] / 2 - 0.55, blk["z0"] + fl * FLOOR + 0.12)
                        spot("rail", px, py, pz, math.degrees(yaw) - 90, "balcony", 1 if abs(pz - cam_z_near(px)) < 7 else 2, note="on a balcony over the Quay")
        if m.get("roof") == "garden":                               # the roof terraces: at the parapet over the street, and under the pergola
            rc, rs = m["roof_centre_m"], m["roof_size_m"]; ry = m["roof_y_m"]
            for lx in (-rs[0] * 0.22, rs[0] * 0.2):
                px, py, pz = building_point(bid, rc[0] + lx, rc[1] - rs[1] / 2 + 1.05, ry)
                spot("rail", px, py, pz, math.degrees(yaw) - 90, "roof_terrace", 1 if abs(pz - cam_z_near(px)) < 8 else 2, note="at a roof-terrace parapet over the Quay")
            px, py, pz = building_point(bid, rc[0] - rs[0] / 2 + 2.3, rc[1] + rs[1] / 2 - 2.4, ry)
            spot("gather", px, py, pz, math.degrees(yaw) - 60, "roof_terrace", 2, note="under the pergola")
            light(*building_point(bid, rc[0], rc[1], ry + 2.2), "garden_a", 7.0, 0.5, "roof_terrace")


def cam_z_near(x):
    best = min(range(280, 470), key=lambda t: abs(cam_at(t / 10.0)[0] - x))
    return cam_at(best / 10.0)[2]


# ============================================================================================ the rest of the Mooring
def low_walk(south):
    """The harbour front: a narrow deck one and a half floors below the Quay behind the south row, with moored skiffs."""
    x = QUAY_X0 + 6.0; i = 0
    lw_y = lambda xx: qy(xx) - 4.3 - 8.4 - 1.1 - 1.9
    while x < 44.0:
        y = lw_y(x); z = qz(x) - 6.4
        if abs(x - STAIRS[0]) > 3.2:
            inst("decks/deck_lowwalk_6m.glb", x, y, z, qyaw(x), [1.012, 1, 1], "low_walk", env=False)
        if i % 3 == 1:
            inst("street/mooring_post.glb", x, y - 1.0, z, 0.3 * i, 1.0, "low_walk", env=False)
            sx, sy, sz = x + R.uniform(-1, 1), y - 6.5 - R.uniform(0, 3.5), z - 5.6 + R.uniform(-1.0, 0.6)
            inst("vehicles/cloud_skiff.glb", sx, sy, sz, R.uniform(-0.4, 0.4), 1.0, "low_walk", env=False, tint={"win_warm": "amber", "lantern": "amber"}, iid="skiff_moored_%d" % (i // 3))
            light(sx - 2.2, sy, sz + 4.4, "amber", 10.0, 0.6, "low_walk")
            spot("rail", x + 1.4, y - 0.9, z, -90.0, "low_walk", 3, look=(sx, sy, sz + 1.5), note="on the Low Walk above a moored skiff")
        if i % 4 == 0:
            inst("street/keel_lantern_16m.glb", x + 2.0, y, z - 0.5, 0.0, 1.0, "keel", env=False)
        x += 6.0; i += 1
    inst("decks/catwalk_stair.glb", STAIRS[0], lw_y(STAIRS[0]), qz(STAIRS[0] + 3) - 6.4, qyaw(STAIRS[0]), 1.0, "low_walk", env=False)       # the Low Walk steps down with the Quay
    xs = STAIRS[0] + 4.2                                           # two flights down from the gap by the first stair
    inst("decks/catwalk_stair.glb", xs, qy(xs) - 4.3 - 0.8 - 2.4, qz(xs) - 3.2, -math.pi / 2, 1.0, "low_walk", env=False)
    inst("decks/catwalk_stair.glb", xs, qy(xs) - 4.3 - 0.8 - 7.2, qz(xs) - 6.4, -math.pi / 2, 1.0, "low_walk", env=False)
    path("low_walk", [(QUAY_X0 + 8, lw_y(QUAY_X0 + 8) + 0.2, qz(QUAY_X0 + 8) - 6.4), (STAIRS[0] - 6, lw_y(STAIRS[0] - 6) + 0.2, qz(STAIRS[0] - 6) - 6.4)], "low_walk", 3, 1.1)


def keel_lanterns():
    x = QUAY_X0 + 4.0; i = 0
    while x < QUAY_X1:
        for sy in (-3.0, 3.0):
            if (i + (sy > 0)) % 3 == 0:
                inst("street/keel_lantern_%s.glb" % R.choice(["8m", "16m", "26m"]), x, qy(x) + sy, qz(x) - 0.6, 0.0, 1.0, "keel", env=False)
        x += 9.0; i += 1


def the_stacks():
    """Residential stacks stepping up behind the north row on three terraces (+3.2, +6.4, +9.6 m above the upper Quay)."""
    rows = [(134.4, [("mooring_stack_c", -64, 37), ("mooring_stack_e", -30, 39), ("mooring_stack_a", 4, 38), ("mooring_stack_e", 36, 30), ("mooring_stack_c", 66, 26)]),
            (137.6, [("mooring_stack_d", -47, 58), ("mooring_stack_b", -13, 61), ("mooring_stack_a", 21, 58), ("mooring_stack_d", 56, 49)]),
            (140.8, [("mooring_capsule_tower", -34, 84), ("mooring_stack_b", -2, 86), ("mooring_gallery_slab", 44, 86), ("mooring_stack_c", -84, 60)])]
    ids = []
    for ri, (z, lst) in enumerate(rows):
        for (nm, x, y) in lst:
            yaw = R.uniform(-0.1, 0.1) + (0.05 if "gallery" in nm else 0.0)
            bid = "%s_k%d%d" % (nm.replace("mooring_", ""), ri, len(ids))
            inst("buildings/%s.glb" % nm, x, y, z, yaw, 1.0, "stacks", tint=tint_for(nm), color=wall_for(nm), iid=bid); ids.append(bid)
            BLD[bid] = ("buildings/%s.glb" % nm, x, y, z, yaw, 1)
            m = MAN["buildings/%s.glb" % nm]; fy = -m["bounds_m"][5]
            hw = int((m["bounds_m"][3] - m["bounds_m"][0]) / 2 // 6) + 2
            for k in range(-hw, hw + 1):                             # a forecourt in front of each: together they make the terrace
                dx, dy = rot(yaw, k * 6.0, fy - 3.4)
                inst("decks/deck_pad_6m.glb", x + dx, y + dy, z, yaw, 1.0, "stacks_deck", env=False)
            dx, dy = rot(yaw, 4.2, fy - 2.0); inst("street/street_lamp.glb", x + dx, y + dy, z, yaw, 1.0, "stacks_deck", env=False)
            light(x + dx, y + dy, z + 3.8, "amber", 9.0, 0.5, "stacks")
            dx, dy = rot(yaw, -4.4, fy - 1.6); inst("garden/planter_round.glb", x + dx, y + dy, z, 0.0, 1.0, "stacks_deck", env=False)
            dx, dy = rot(yaw, 0.6, fy - 3.6)
            spot("stand", x + dx, y + dy, z, math.degrees(yaw) - 90, "stacks", 3, note="on a forecourt of the Stacks, looking over the roofs of the Quay")
            if m.get("roof") == "garden":
                rc, rs = m["roof_centre_m"], m["roof_size_m"]
                px, py, pz = building_point(bid, rc[0], rc[1] - rs[1] / 2 + 1.05, m["roof_y_m"])
                spot("rail", px, py, pz, math.degrees(yaw) - 90, "roof_terrace", 3, look=GREAT_LANTERN, note="a high roof terrace, looking toward the Great Lantern")
    inst("signs/sign_roof_2.gltf", 44 - 6.0, 86 - 3.0, 140.8 + MAN["buildings/mooring_gallery_slab.glb"]["roof_y_m"], 0.05, 0.62, "signs", tint={"neon_a": "sign_b"}, env=False)
    for (z, xa, xb, y) in ((134.4, -70, 70, 26.0), (137.6, -52, 60, 46.0), (140.8, -40, 30, 73.0)):
        path("stacks_walk_%d" % int(z), [(xa, y, z), (xb, y - 6.0, z)], "stacks", 3, 1.15, note="approximate: along the forecourts of this terrace")
    return ids


def west_end():
    """The plaza, the Kite Line station, the pier with its broken spans and gondola, and the Great Lantern."""
    px, py, pz = PLAZA; yaw = 0.15
    for i in range(7):
        for j in range(7):
            dx, dy = rot(yaw, (i - 3) * 5.72, (j - 3) * 5.72)
            if (px + dx) > QUAY_X0 - 2.5 and abs(py + dy - qy(QUAY_X0)) < 4.6:
                continue
            inst("decks/deck_pad_6m.glb", px + dx, py + dy, pz, yaw, [0.955, 1, 0.955], "plaza", env=False)
    pd = PIER_DIR; nrm = (-pd[1], pd[0]); pyaw = math.atan2(pd[1], pd[0])
    sc = (PIER_ROOT[0] - 10 * pd[0] - 9 * nrm[0], PIER_ROOT[1] - 10 * pd[1] - 9 * nrm[1])       # north-west of the pier root, clear of the journey
    syaw = pyaw + math.pi
    inst("buildings/mooring_station.glb", sc[0], sc[1], pz, syaw, 1.0, "station", tint={"win_warm": "amber", "strip": "eave", "lantern": "win_a"}, iid="station")
    ms = MAN["buildings/mooring_station.glb"]
    ca = ms["cable_anchor_m"]; cdx, cdy = rot(syaw, ca[0], -ca[2]); anchor = (sc[0] + cdx, sc[1] + cdy, pz + ca[1])
    rdx, rdy = rot(syaw, -3.0, -4.8)
    inst("signs/sign_roof_1.gltf", sc[0] + rdx, sc[1] + rdy, pz + 5.5, syaw, 0.55, "signs", tint={"neon_a": "sign_b"}, env=False)
    rdx, rdy = rot(syaw, 8.4, -4.0)
    inst("signs/sign_roundel_2.gltf", sc[0] + rdx, sc[1] + rdy, pz + 4.2, syaw - math.pi / 2, 1.3, "signs", tint={"neon_a": "sign_b"}, env=False)
    for (lx, ly, kind, note) in ((-5.6, -3.9, "wait", None), (-3.0, -3.9, "wait", None), (-0.6, -4.1, "wait", "at the platform edge, watching for the gondola"), (3.4, -3.4, "look", "watching the plaza"),
                                 (-1.2, -1.6, "use", "at the ticket window"), (6.0, -3.8, "wait", None)):
        dx, dy = rot(syaw, lx, ly)
        spot(kind, sc[0] + dx, sc[1] + dy, pz, math.degrees(syaw) - 90 + (180 if kind == "use" else 0), "station", 1, note=note)
    light(sc[0], sc[1], pz + 4.6, "win_a", 14.0, 1.0, "station")
    # the pier: 11 whole spans, then two that have dropped and still have their lights on
    spans = [(11.0 + 22.0 * i, 0.0, 0.0, 1.0) for i in range(11)] + [(268.0 + 15.0, -4.0, 0.05, 30.0 / 22.0), (322.0 + 12.0, -11.0, -0.09, 24.0 / 22.0)]
    for i, (s, drop, tilt, sx) in enumerate(spans):
        inst("decks/pier_span_22m.glb", PIER_ROOT[0] + pd[0] * s, PIER_ROOT[1] + pd[1] * s, pz + drop, pyaw, [sx, 1, 1], "pier", env=False, roll=tilt,
             tint={"lantern": "win_a" if i % 4 else "sign_b"}, iid="pier_%02d" % i)
        if i % 2 == 0:
            light(PIER_ROOT[0] + pd[0] * s, PIER_ROOT[1] + pd[1] * s, pz + drop + 2.0, "win_a", 10.0, 0.5, "pier")
    cable = [anchor]
    for s in (40.0, 110.0, 180.0, 242.0):
        x, y = PIER_ROOT[0] + pd[0] * s, PIER_ROOT[1] + pd[1] * s
        inst("street/gondola_pylon.glb", x, y, pz, pyaw, 1.0, "pier", env=False)
        cable.append((x, y, pz + 14.0))
    out_line = [(p[0] + nrm[0] * 2.2, p[1] + nrm[1] * 2.2, p[2]) for p in cable]; back_line = [(p[0] - nrm[0] * 2.2, p[1] - nrm[1] * 2.2, p[2]) for p in cable]
    for line in (out_line, back_line):
        for a, b in zip(line[:-1], line[1:]):
            L = math.sqrt(sum((a[k] - b[k]) ** 2 for k in range(3))); cyaw = math.atan2(b[1] - a[1], b[0] - a[0]); sl = math.asin((b[2] - a[2]) / L)
            inst("street/cable_unit.glb", a[0], a[1], a[2], cyaw, [L, 1, 1], "pier", env=False, roll=-sl)
    for k, (line, f) in enumerate(((out_line, 0.42), (back_line, 0.74))):
        a, b = line[1], line[3]; x, y, z = (a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f)
        inst("vehicles/gondola_cabin.glb", x, y, z - 3.6, pyaw, 1.0, "pier", env=False, tint={"win_warm": "amber"}, iid="gondola_%d" % k, tags=["vehicle:gondola_%s" % ("out" if k == 0 else "back"), "static_stand_in: leave this instance out when the vehicle of the same spline is animated"])
    path("pier_walk", [(PIER_ROOT[0] + pd[0] * 6, PIER_ROOT[1] + pd[1] * 6, pz), (PIER_ROOT[0] + pd[0] * 236, PIER_ROOT[1] + pd[1] * 236, pz)], "pier", 1, 1.25, count=2)
    spot("rail", PIER_ROOT[0] + pd[0] * 238 + nrm[0] * 1.9, PIER_ROOT[1] + pd[1] * 238 + nrm[1] * 1.9, pz, math.degrees(pyaw) - 20, "pier", 1,
         look=(LIGHTHOUSES[0][2], LIGHTHOUSES[0][3], LIGHTHOUSES[0][4] + 50), note="at the end of the whole pier, looking at the Harbour Light and the broken spans")
    for (lx, ly) in ((-8, -10), (-4, 6), (6, -4), (10, 9)):
        dx, dy = rot(yaw, lx, ly)
        spot("stand", px + dx, py + dy, pz, math.degrees(math.atan2(GREAT_LANTERN[1] - py - dy, GREAT_LANTERN[0] - px - dx)), "plaza", 1, look=(GREAT_LANTERN[0], GREAT_LANTERN[1], GREAT_LANTERN[2] + 45),
             note="on the plaza, looking up at the Great Lantern")
    for (lx, ly) in ((-15, 14), (14, -15), (-15, -15)):
        dx, dy = rot(yaw, lx, ly); inst("street/street_lamp.glb", px + dx, py + dy, pz, 0.7, 1.0, "plaza", env=False); light(px + dx, py + dy, pz + 3.8, "amber", 10.0, 0.6, "plaza")
    dx, dy = rot(yaw, 12, 12); inst("buildings/kiosk.glb", px + dx, py + dy, pz, yaw + math.pi, 1.0, "plaza", env=False, tint={"win_warm": "amber", "strip": "eave"})
    dx2, dy2 = rot(yaw + math.pi, 0.0, -2.2); spot("use", px + dx + dx2, py + dy + dy2, pz, math.degrees(yaw + math.pi) + 90, "plaza", 2, note="at the kiosk counter")
    dx, dy = rot(yaw, -6, 12); inst("garden/tree_pit.glb", px + dx, py + dy, pz, 0, 1.0, "plaza", env=False); inst("garden/umbel_tree_a.glb", px + dx, py + dy, pz + 0.35, 1.0, 1.0, "plaza", env=False)
    light(px + dx, py + dy, pz + 6.0, "garden_a", 14.0, 0.8, "plaza")
    # the Great Lantern on its round deck, reached by a catwalk and a stair from the plaza
    gx, gy, gz = GREAT_LANTERN
    inst("landmarks/great_lantern_frame.glb", gx, gy, gz, 0.0, 1.0, "great_lantern", tint={"strip": "sign_c"}, iid="great_lantern_frame")
    inst("landmarks/great_lantern_bell.glb", gx, gy, gz + 16.0, 0.0, 1.0, "great_lantern", env=False, iid="great_lantern_bell", tags=["ride_y:14.5..34.0", "bass"])
    light(gx, gy, gz + 40.0, "landmark", 120.0, 3.0, "great_lantern", "great_lantern")
    a = (px - 8, py + 21, pz); b = (gx + 22, gy - 14, gz)
    yawc = math.atan2(b[1] - a[1], b[0] - a[0]); L = math.hypot(b[0] - a[0], b[1] - a[1])
    n = int((L - 4.8) // 6.0)
    inst("decks/catwalk_stair.glb", a[0] + 2.4 * math.cos(yawc), a[1] + 2.4 * math.sin(yawc), pz, yawc + math.pi, 1.0, "great_lantern", env=False)
    for i in range(n + 1):
        s = 4.8 + 3.0 + i * 6.0
        if s < L + 2:
            inst("decks/deck_lowwalk_6m.glb", a[0] + s * math.cos(yawc), a[1] + s * math.sin(yawc), gz, yawc, 1.0, "great_lantern", env=False)
    for ang in (200, 250, 300):
        r_ = 22.0; x, y = gx + r_ * math.cos(math.radians(ang)), gy + r_ * math.sin(math.radians(ang))
        spot("look", x, y, gz, ang + 180, "great_lantern", 2, look=(gx, gy, gz + 40), note="on the Lantern's deck, looking up at the bell")
    for k in range(6):
        ang = k * 60 + 20; inst("street/keel_lantern_%s.glb" % ("26m" if k % 2 else "16m"), gx + 20 * math.cos(math.radians(ang)), gy + 20 * math.sin(math.radians(ang)), gz - 1.2, 0, 1.0, "keel", env=False)
    return dict(cable_out=out_line, cable_back=back_line, station=sc)


def east_garden():
    """The garden terrace on the lee tip: two round decks, the pool, nine umbel trees, a pavilion. The Steps arrive here."""
    gx, gy, gz = GARDEN
    inst("decks/deck_round_27m.glb", gx, gy, gz, 0.0, 1.0, "garden", env=False); inst("decks/deck_round_14m.glb", gx - 22, gy + 16, gz, 0.3, 1.0, "garden", env=False)
    inst("decks/deck_round_14m.glb", gx - 42, gy + 0, gz, 0.9, 1.0, "garden", env=False)        # the landing where the Steps meet the garden
    inst("decks/pool_15m.glb", gx + 6, gy - 4, gz, 0.0, 1.0, "garden", env=False)
    trees = [(-12, 15, "b"), (-4, -14, "a"), (9, -18, "c"), (17, -9, "b"), (-18, -7, "b"), (2, 6, "s"), (21, 8, "s"), (-26, 17, "b"), (-3, -23, "s"), (-10, 4, "s"), (-41, -4, "s")]
    for i, (tx, ty, v) in enumerate(trees):
        inst("garden/tree_pit.glb", gx + tx, gy + ty, gz, 0.0, 1.0, "garden", env=False)
        inst("garden/umbel_tree_%s.glb" % v, gx + tx, gy + ty, gz + 0.35, R.uniform(0, 6.28), 1.0, "garden", env=False, tint={"garden": "garden_a" if i % 3 else "garden_b"})
        light(gx + tx, gy + ty, gz + 6.0, "garden_a" if i % 3 else "garden_b", 14.0, 0.7, "garden")
    for k in range(14):
        a, rr = R.uniform(0, 6.283), min(21.2, 24 * math.sqrt(R.random()))          # inside the planters of the upper edge (iteration 8: the terrace steps down at 24.6 m)
        if math.hypot(rr * math.cos(a) - 6, rr * math.sin(a) + 4) > 9.5:
            inst(R.choice(["garden/sprig_bed_3m.glb", "garden/shrub_a.glb", "garden/shrub_b.glb", "garden/planter_round.glb"]), gx + rr * math.cos(a), gy + rr * math.sin(a), gz, a, R.uniform(0.8, 1.2), "garden",
                 env=False, tint={"garden": R.choice(["garden_a", "garden_b"])})
    for k in range(12):
        t = k / 11; inst("street/street_lamp.glb", gx - 30 + 46 * t, gy + 12 - 28 * t + 5 * math.sin(t * 6), gz, R.uniform(0, 6.28), 0.42, "garden", env=False)
    inst("garden/pavilion.glb", gx + 14, gy + 15, gz, math.radians(200), 1.0, "garden", env=False, tint={"win_warm": "amber", "strip": "eave"})
    light(gx + 14, gy + 15, gz + 2.0, "amber", 12.0, 1.0, "garden")
    for (tx, ty, f) in ((1, -12, 80), (-2, -11, 60), (3.5, -13.5, 100), (14, 1, 200), (15.5, -0.5, 215), (-8, 9, 300)):
        spot("gather", gx + tx, gy + ty, gz, f, "garden", 1, look=(gx + 6, gy - 4, gz + 0.5), note="by the pool")
    for (tx, ty) in ((-14, 4.5), (8, 12), (-27.5, 15.5)):
        spot("tend", gx + tx, gy + ty, gz, 0.0, "garden", 2, note="tending the bed under an umbel tree")
    spot("look", gx + 25.2, gy - 6, gz - 0.4, 0.0, "garden", 1, look=(VEIL[0], VEIL[1], 200), note="on the promenade at the east rail, looking up at the Veil")
    path("garden_stroll", [(gx - 34, gy - 4, gz), (gx - 18, gy + 4, gz), (gx - 2, gy + 8, gz), (gx + 12, gy + 2, gz), (gx + 18, gy - 12, gz)], "garden", 1, 0.95, clip="Walking_low_grav", count=2)
    for k in range(9):
        a = k * 40 + 10; inst("street/keel_lantern_%s.glb" % ("26m" if k % 3 == 0 else "16m"), gx + 20 * math.cos(math.radians(a)), gy + 20 * math.sin(math.radians(a)), gz - 0.8, 0, 1.0, "keel", env=False)
    # the ramp from the end of the Quay
    a = (QUAY_X1 + 0.5, qy(QUAY_X1) - 1.0, qz(QUAY_X1)); b = (gx - 22, gy + 16 + 9, gz)
    yawc = math.atan2(b[1] - a[1], b[0] - a[0])
    inst("decks/catwalk_stair.glb", a[0] + 2.4 * math.cos(yawc), a[1] + 2.4 * math.sin(yawc), gz, yawc, 1.0, "garden", env=False)


def lighthouses():
    """Lanternwright beacon towers on cloud rocks. Two opposed beams each, turning; the scene draws them."""
    beams = []
    for (name, asset, x, y, z, yawd, rpm, phase, blen) in LIGHTHOUSES:
        a = "landmarks/%s.glb" % asset; m = MAN[a]
        inst(a, x, y, z, math.radians(yawd), 1.0, "lighthouse", tint={"win_warm": "amber", "win_cool": "win_a", "strip": "eave", "beam_lamp": "moon_disc"}, iid=name)
        lm = m["lamp_m"]; dx, dy = rot(math.radians(yawd), lm[0], -lm[2]); lamp_p = (x + dx, y + dy, z + lm[1])
        beams.append({"name": name, "origin": E(lamp_p), "count": 2, "rpm": rpm, "phase": phase, "elevation_deg": -1.5, "length_m": blen, "half_angle_deg": 1.0,
                      "key": "moon_disc", "intensity": 1.0, "spin_axis": [0, 1, 0],
                      "cloud_glow": {"radius_m": 110.0, "orbit_radius_m": round(blen * 0.55, 1), "y": round(z - 20.0, 1), "intensity": 0.6},
                      "pane_program": "the lit pane of the lantern room follows the beam: beam_lamp uv.v = pane bearing / 360",
                      "audio": "rpm is the rest speed; kick onsets may add a short brightening, the bar clock (1.4 s) may quantise the sweep"})
        light(lamp_p[0], lamp_p[1], lamp_p[2], "moon_disc", 60.0, 2.0, "lighthouse", name + "_lamp")
        gy_ = m["gallery_y_m"]
        spot("rail", lamp_p[0] + 2.7, lamp_p[1], z + gy_, 0.0, "lighthouse", 1 if name == "harbour_light" else 3, note="the keeper on the gallery of the %s" % name.replace("_", " "))
        inst("vehicles/cloud_skiff.glb", x + 9.0, y - 6.0, z - 9.0, 0.6, 1.0, "lighthouse", env=False, tint={"win_warm": "amber", "lantern": "amber"})
    return beams


def condensery():
    cx, cy, cz = CONDENSERY; yaw = CONDENSERY_YAW
    T = {"win_warm": "cond_a", "win_cool": "cond_c", "strip": "cond_a", "keel": "cond_b", "lantern": "cond_a"}
    P = lambda lx, ly: (cx + rot(yaw, lx, ly)[0], cy + rot(yaw, lx, ly)[1])
    inst("condensery/cond_platform.glb", cx, cy, cz, yaw, 1.0, "condensery", tint=T, iid="cond_platform", env=False)
    emit = []
    for (nm, lx, ly) in (("cond_stack_a", -58, 14), ("cond_stack_b", -2, 30), ("cond_stack_c", 52, 2)):
        x, y = P(lx, ly); m = MAN["condensery/%s.glb" % nm]
        inst("condensery/%s.glb" % nm, x, y, cz, yaw + 0.4, 1.0, "condensery", iid=nm, env=False)
        emit.append({"name": nm + "_plume", "kind": "plume", "pos": E((x, y, cz + m["plume_origin_m"][1])), "radius_m": m["throat_radius_m"] * 0.8, "direction": [0.55, 0.8, 0.24],
                     "rise_mps": 9.0, "key": "cond_a", "glow_key": "cond_a", "audio": "BASS level -> glow of the plume's base"})
        light(x, y, cz + m["height_m"] + 12, "cond_a", 160.0, 3.0, "condensery", nm + "_throat")
    for (lx, ly) in ((90, 42), (106, 16)):
        inst("condensery/cond_sphere_tank.glb", *P(lx, ly), cz, yaw + 1.0, 1.0, "condensery", env=False)
    for (nm, lx, ly, dyaw) in (("cond_shed_a", -66, -44, 0), ("cond_shed_b", -22, -48, 0), ("cond_shed_a", 22, -50, 0), ("cond_shed_b", 62, -46, 0)):
        inst("condensery/%s.glb" % nm, *P(lx, ly), cz, yaw + math.pi + dyaw, 1.0, "condensery", tint=T, env=False)
    fx, fy = P(-108, 44); inst("condensery/cond_flare_mast.glb", fx, fy, cz, 0.0, 1.0, "condensery", iid="cond_flare", env=False)
    light(fx, fy, cz + 166, "cond_a", 150.0, 3.0, "condensery", "cond_flare")
    inst("condensery/cond_crane.glb", *P(97, -32), cz, yaw - 0.3, 1.0, "condensery", tint=T, iid="cond_crane", env=False)
    a = P(-112, -20); dirv = rot(yaw, -0.95, -0.3)
    for k in range(13):                                             # the conveyor pier points back at the Mooring and stops
        inst("condensery/cond_conveyor_span.glb", a[0] + dirv[0] * (10 + 20 * k), a[1] + dirv[1] * (10 + 20 * k), cz + 4.0 - 0.3 * k, math.atan2(dirv[1], dirv[0]), 1.0, "condensery", env=False)
    for k in range(26):                                             # the ring of sodium lamps
        t = k / 26.0; lx = (t * 4 - 1) * 114 if t < 0.5 else (1 - (t - 0.5) * 4) * 114; ly = -60 if t < 0.5 else 60
        inst("condensery/cond_lamp_post.glb", *P(lx, ly), cz, yaw + (math.pi / 2 if ly < 0 else -math.pi / 2), 1.0, "condensery", env=False)
    light(cx, cy, cz + 14, "cond_a", 180.0, 2.5, "condensery", "cond_yard"); light(cx - 20, cy, cz - 30, "cond_b", 160.0, 2.0, "condensery", "cond_keel")
    for (lx, ly, f) in ((-20, -52, 250), (-14, -50, 280), (70, -10, 20)):
        x, y = P(lx, ly); spot("stand", x, y, cz, f, "condensery", 3, note="a worker on the Condensery's yard")
    return emit


def lone_lamp():
    """One house on a cloud of its own. Its lit window (front, third floor) faces the Quay's east end, 850 m off."""
    x, y, z = LONE_LAMP; yaw = math.radians(-130.0)
    inst("buildings/lone_lamp_house.glb", x, y, z, yaw, 1.0, "lone_lamp", tint={"win_warm": "amber"}, iid="lone_lamp_house")
    for k in (-1, 0, 1):
        dx, dy = rot(yaw, k * 6.0, -8.0); inst("decks/deck_pad_6m.glb", x + dx, y + dy, z, yaw, 1.0, "lone_lamp", env=False)
    light(x, y, z + 8, "amber", 30.0, 1.0, "lone_lamp")


def highwater():
    """Highwater Mooring (iteration 6): a third settlement, never reached, glimpsed on the right of the reveal 2.2 km out
    with its decks at the horizon. Built to be read at that distance and from that side: three terraces stepping up
    away from the reveal, every building turned so its west side (the always-lit stairwell and an eave line) faces the
    camera, the tallest stacks at the back, three roof signs at two and a half times life size. It uses the Mooring's
    own assets with tints the Mooring already uses, so the scene gains instances, not nodes. Its own random source:
    nothing earlier in the layout moves."""
    rr = random.Random(606); x0, y0, z0 = HIGHWATER; yaw = HIGHWATER_YAW
    seen = {}
    for i in INST:
        if i["asset"].startswith("buildings/") and "tint" in i and i["zone"] in ("quay_north", "quay_south", "stacks"):
            seen.setdefault(i["asset"], i["tint"])
    rows = [(0.0, 34.0, [("mooring_shophouse_c", -52), ("mooring_shophouse_g", -32), ("mooring_shophouse_a", -12), ("mooring_shophouse_h", 9), ("mooring_shophouse_e", 30), ("mooring_shophouse_b", 50)]),
            (6.4, 2.0, [("mooring_stack_c", -44), ("mooring_stack_e", -21), ("mooring_stack_a", 2), ("mooring_corner_tower", 27), ("mooring_stack_c", 52)]),
            (12.8, -30.0, [("mooring_stack_d", -30), ("mooring_stack_b", -6), ("mooring_capsule_tower", 19), ("mooring_stack_a", 44)])]
    top = None
    for (dz, u, lst) in rows:                                   # u: metres toward the reveal (west of the centre), v: along the row
        for (nm, v) in lst:
            a = "buildings/%s.glb" % nm
            if a not in MAN:
                continue
            lx, ly = -u + rr.uniform(-2, 2), v + rr.uniform(-1.5, 1.5); dx, dy = rot(yaw, lx, ly)
            byaw = yaw + rr.uniform(-0.05, 0.05)
            inst(a, x0 + dx, y0 + dy, z0 + dz, byaw, 1.0, "highwater", tint=seen.get(a) or tint_for(nm), iid="hw_%s_%d" % (nm.replace("mooring_", ""), len(INST)))
            m = MAN[a]; fy = -m["bounds_m"][5]
            for k in (-1, 0, 1):
                ddx, ddy = rot(byaw, k * 6.0, fy - 3.4); inst("decks/deck_pad_6m.glb", x0 + dx + ddx, y0 + dy + ddy, z0 + dz, byaw, 1.0, "highwater", env=False)
            if nm in ("mooring_stack_b", "mooring_stack_d", "mooring_shophouse_h") and "roof_y_m" in m:
                inst("signs/sign_roof_2.gltf", x0 + dx, y0 + dy, z0 + dz + m["roof_y_m"], yaw + math.radians(-90.0), 2.6, "highwater", tint={"neon_a": "sign_b"}, env=False)
    return len([i for i in INST if i["zone"] == "highwater"])


def anomalies():
    """The Long Arc (iteration 6: it existed only in the Blender concept frames). It stands 22 km east of the journey and
    leaves the sea 6 degrees north of east as the reveal sees it, bending over to the south: in the reveal it climbs out
    of the horizon between the Condensery and the far tower; from the approach it is a hairline low on the right, beyond
    the Outer Light. Never reached, never explained."""
    inst("far/long_arc.glb", LONG_ARC[0], LONG_ARC[1], LONG_ARC[2], math.radians(-90.0), 1.0, "anomaly", iid="long_arc", env=False)


# ============================================================================================ checks and output
def obb_overlap(a, b, margin=0.0):
    def corners(e):
        return [(e["c"][0] + rot(e["yaw"], lx, ly)[0], e["c"][1] + rot(e["yaw"], lx, ly)[1]) for lx, ly in ((e["x0"], e["y0"]), (e["x1"], e["y0"]), (e["x1"], e["y1"]), (e["x0"], e["y1"]))]
    A, B = corners(a), corners(b)
    for e in (a, b):
        for ax in ((math.cos(e["yaw"]), math.sin(e["yaw"])), (-math.sin(e["yaw"]), math.cos(e["yaw"]))):
            pa = [p[0] * ax[0] + p[1] * ax[1] for p in A]; pb = [p[0] * ax[0] + p[1] * ax[1] for p in B]
            if max(pa) + margin < min(pb) or max(pb) + margin < min(pa):
                return False
    return not (a["z1"] < b["z0"] or b["z1"] < a["z0"])


def checks():
    bad = []
    for i in range(len(ENV)):
        for j in range(i + 1, len(ENV)):
            if obb_overlap(ENV[i], ENV[j]):
                bad.append((ENV[i]["id"], ENV[j]["id"]))
    worst = (1e9, None, None)
    for ti in range(0, 703):
        t = ti / 10.0; cx, cy, cz = cam_at(t)
        for e in BOX:
            dx, dy = cx - e["c"][0], cy - e["c"][1]; lx, ly = rot(-e["yaw"], dx, dy)
            ddx = max(e["x0"] - lx, 0, lx - e["x1"]); ddy = max(e["y0"] - ly, 0, ly - e["y1"]); ddz = max(e["z0"] - cz, 0, cz - e["z1"])
            d = math.sqrt(ddx * ddx + ddy * ddy + ddz * ddz)
            if d < worst[0]:
                worst = (d, e["id"], t)
    return bad, worst


north, south, gN, gS = the_quay()
lantern_strings(north, south)
quay_south_side(south, gS)
quay_life(north, south)
low_walk(south)
keel_lanterns()
stack_ids = the_stacks()
west = west_end()
east_garden()
beams = lighthouses()
emitters = condensery()
lone_lamp()
n_highwater = highwater()
anomalies()
bad, worst = checks()

tris = sum(MAN[i["asset"]]["tris"] for i in INST)
by_zone = {}
for i in INST:
    by_zone[i["zone"]] = by_zone.get(i["zone"], 0) + 1
cam = [{"t": t, "pos": E((x, y, z))} for (t, x, y, z) in CAMERA]
out = {
    "version": 2,
    "frame": {"units": "metres", "up": "+y", "axes": "x east, y up, z = -north (plan y); the journey runs toward +x; Lowlight Mooring at the origin, the Quay's decks at y = 124.8 to 131.2",
              "same_frame_as": "art/cloud_design.json", "rotation": "rot_y_deg is a yaw about +y (counter-clockwise seen from above); quat = [x, y, z, w] is the full rotation",
              "scale": "[sx, sy, sz] in the asset's own axes", "front": "a building's front (shop side) is the GLB's +z; at rot_y_deg = 0 it faces world +z (south)"},
    "kit": "../meshes/",
    "palette": "../art/palette.json",
    "tint_doc": "tint maps a light-role material of the instance to a palette key; roles not listed use defaults: " + json.dumps(
        dict(win_warm="win_d", win_cool="win_a", win_stair="win_a", strip="eave", beacon="beacon", landmark="landmark", garden="garden_a", lantern="amber", keel="bounce_a", neon_a="sign_a", beam_lamp="moon_disc",
             cond_glow="cond_a", cond_lamp="cond_a", cond_work="cond_c")),
    "inhabitants": {"cast": "Glowmere aliens (assets/aliens/alien-*.glb, third-party: referenced by path, never copied)", "scale": 1.0,
                    "scale_note": "the kit is built for bodies 1.66 to 1.79 m tall: doors 2.04 m, rails 1.05 m, risers 0.2 m. At Glowmere's 1.94x a body is taller than a storey (3.2 m).",
                    "facing": "facing_deg is a plan heading (0 = +x, 90 = north = -z); forward is the same as an engine vector",
                    "kinds": {"stand": "idle", "look": "idle, turning the head toward look_at", "rail": "at a railing or parapet, facing out", "gather": "one of a small group",
                              "wait": "on the station platform", "use": "at a counter or machine", "tend": "bending to the plants"},
                    "priority": "1 = within about 25 m of the journey (place these first), 2 = seen at mid distance, 3 = far",
                    "spots": SPOTS, "paths": PATHS},
    "instances": INST,
    "splines": {"quay_centre": [E((x, qy(x), qz(x))) for x in [QUAY_X0 + (QUAY_X1 - QUAY_X0) * k / 40 for k in range(41)]],
                "gondola_out": [E(p) for p in west["cable_out"]], "gondola_back": [E(p) for p in reversed(west["cable_back"])],
                "pier": [E((PIER_ROOT[0] + PIER_DIR[0] * s, PIER_ROOT[1] + PIER_DIR[1] * s, PIER_ROOT[2])) for s in (0, 60, 120, 180, 242)],
                "skiff_route": [E(p) for p in ((-300, -330, 112), (-240, -250, 111), (-150, -150, 112), (-90, -60, 113), (-40, -32, 113.5))]},
    "vehicles": [{"asset": "vehicles/gondola_cabin.glb", "spline": "gondola_out", "hang_below_m": 3.6, "speed_mps": 3.0, "phase": 0.42, "instance": "gondola_0"},
                 {"asset": "vehicles/gondola_cabin.glb", "spline": "gondola_back", "hang_below_m": 3.6, "speed_mps": 3.0, "phase": 0.26, "instance": "gondola_1"},
                 {"asset": "vehicles/cloud_skiff.glb", "spline": "skiff_route", "speed_mps": 2.2, "phase": 0.3, "tint": {"win_warm": "amber", "lantern": "amber"}}],
    "beams": beams,
    "emitters": emitters,
    "lights": LIGHTS,
    "camera": {"note": "the 70.2 s journey of art-direction section 11, moved a little where the layout needed room; the camera looks along its path", "waypoints": cam},
    "checks": {"lantern_string_anchor_keys": STRING_KEYS, "envelope_overlaps": bad, "camera_min_clearance_m": round(worst[0], 2), "nearest": worst[1], "at_t": worst[2]},
    "stats": {"instances": len(INST), "triangles": tris, "by_zone": by_zone, "spots": len(SPOTS), "paths": len(PATHS), "lights": len(LIGHTS)},
}
dst = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "art", "layout.json")
json.dump(out, open(dst, "w"), indent=1)
print("layout:", len(INST), "instances,", tris, "triangles,", len(SPOTS), "spots,", len(PATHS), "paths,", len(LIGHTS), "lights")
print("zones:", by_zone)
print("overlaps:", bad)
print("camera clearance: %.2f m from %s at t=%.1f" % worst)
for bid in north + south:
    a, x, y, z, yaw, side = BLD[bid]
    print("  %-22s x %7.1f y %6.1f z %6.1f yaw %6.1f" % (bid, x, y, z, math.degrees(yaw)))
print("gaps N:", [(round(a, 1), round(b, 1), k) for a, b, k in gN]); print("gaps S:", [(round(a, 1), round(b, 1), k) for a, b, k in gS], "south end", round(ZONES["quay"]["south_end"], 1), "north end", round(ZONES["quay"]["north_end"], 1))
