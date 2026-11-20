"""Neon Dreams, iteration 9 (owner, msg 73): the Mooring built out, and the settlements that float.

Called by nd_layout.py after everything earlier is placed (`nd_city9.build(layout_module)`), with its own random
sources, so nothing placed before moves. What it adds:

  * THE LAMPMARKET: a market court one floor above the plaza, west of the Great Lantern, open to the south-west so the
    approach looks into it (2.8 to 12 s, left of the Lantern). Three rows round the court, a second ring of stacks
    behind, stalls, lantern strings across the court, and more neon per building than the Quay: a blade or roundel on
    every building, valances on most shop bays, roof signs. A walk joins it to the Lantern's deck.
  * LANTERNHILL: the town climbing the Head's south flank behind the Stacks: four terraces two floors apart
    (144.0 to 163.2), residential stacks turned a little toward the approach, forecourts, lamps, roof gardens, and the
    Mooring Mast at the crest (the observation landmark).
  * EASTREACH: garden roofs on three terraces east of the Stacks, north of the Veil: low houses with roof terraces
    planted, umbel saplings and planters on the roofs. Left of the journey from 41 to 47 s.
  * THE FLOATING SETTLEMENTS (they bob with their cloud: "bob" on every instance): Lampwick, a hamlet on a cloud of
    its own south-east of the garden, reached by a gondola line; the Watch, one tall house on a tall cloud stack; the
    Keep's town on the colossus's shelf 2.6 km out on the left of the approach.
  * The drowned mast, 9.3 km out: far/drowned_mast.glb.

Every building obeys the envelope check (nd_layout.checks: no overlaps), and takes a (tint, colour) the Quay or the
Stacks already use for the same asset, so the engine gains instances, not nodes (and not material parts).
"""
import math, random

TOWER_TINT = {"win_warm": "win_d", "win_cool": "win_b", "strip": "eave"}
L = None                                   # the nd_layout module (its globals: INST, ENV, BLD, MAN, inst, spot, path, light ...)


def _looks():
    seen = {}
    for i in L.INST:
        if i["asset"].startswith("buildings/") and i["zone"] in ("quay_north", "quay_south", "stacks"):
            key = (tuple(sorted(i.get("tint", {}).items())), str(i.get("color")))
            lst = seen.setdefault(i["asset"], [])
            if key not in [k for k, _ in lst]:
                lst.append((key, (i.get("tint"), i.get("color"))))
    return seen


def envelope(asset, x, y, z, yaw, scale=1.0):
    b = L.MAN[asset]["bounds_m"]
    return dict(c=(x, y), yaw=yaw, x0=b[0] * scale, x1=b[3] * scale, y0=-b[5] * scale, y1=-b[2] * scale, z0=z + max(b[1], -2.0), z1=z + b[4] * scale)


def free(asset, x, y, z, yaw, margin=0.6):
    e = envelope(asset, x, y, z, yaw)
    return not any(L.obb_overlap(e, o, margin) for o in L.ENV)


def building(asset, x, y, z, yaw, zone, rr, seen, bid, bob=None, tries=((0, 0),)):
    """Place a building of the kit if its envelope is clear of every other (trying the offsets given), with a look the
    town already uses for that asset. Returns its id, or None."""
    a = "buildings/%s.glb" % asset
    for (dx, dy) in tries:
        if free(a, x + dx, y + dy, z, yaw):
            looks = seen.get(a)
            if asset.startswith("tower_"):
                tint, color = TOWER_TINT, None                              # one node per tower type
            else:
                tint, color = (rr.choice(looks)[1] if looks else (L.tint_for(asset), None))
            tags = ["bob:" + bob] if bob else None
            d = L.inst(a, x + dx, y + dy, z, yaw, 1.0, zone, tint=tint, color=color, iid=bid, tags=tags)
            if bob:
                d["bob"] = bob
            L.BLD[bid] = (a, x + dx, y + dy, z, yaw, 1)
            return bid
    return None


def put(asset, x, y, z, yaw=0.0, scale=1.0, zone="", bob=None, **kw):
    d = L.inst(asset, x, y, z, yaw, scale, zone, env=False, **kw)
    if bob:
        d["bob"] = bob
    return d


def lamp(x, y, z, key, radius, intensity, zone, bob=None, name=None):
    L.light(x, y, z, key, radius, intensity, zone, name)
    if bob:
        L.LIGHTS[-1]["bob"] = bob


def forecourt(bid, zone, extra=1, bob=None, lamp_key="amber"):
    asset, x, y, z, yaw, side = L.BLD[bid]; m = L.MAN[asset]; fy = -m["bounds_m"][5]
    hw = int((m["bounds_m"][3] - m["bounds_m"][0]) / 2 // 6) + extra
    for k in range(-hw, hw + 1):
        dx, dy = L.rot(yaw, k * 6.0, fy - 3.4)
        put("decks/deck_pad_6m.glb", x + dx, y + dy, z, yaw, 1.0, zone + "_deck", bob=bob)
    dx, dy = L.rot(yaw, 4.2, fy - 2.0); put("street/street_lamp.glb", x + dx, y + dy, z, yaw, 1.0, zone + "_deck", bob=bob)
    return (x + dx, y + dy, z + 3.8)


def dress(bid, rr, blade_p=0.75, second_p=0.0, valance_p=0.5, roof_p=0.0, keys=("sign_a", "sign_a", "sign_b", "sign_c", "sign_a", "sign_b"), zone="signs", bob=None):
    """nd_layout.dress_shop with its own random source and stronger odds: the market's buildings carry more light."""
    asset, x, y, z, yaw, side = L.BLD[bid]; m = L.MAN[asset]
    if not m.get("blocks"):
        return None, None
    blk0 = m["blocks"][0]
    brand = rr.randrange(8); key = rr.choice(keys)
    nb = int(blk0["w"] / L.BAY + 1e-6); mg = (blk0["w"] - nb * L.BAY) / 2; d0 = blk0["d"]
    for b in range(nb):
        if b == nb // 2:
            continue
        lx = -blk0["w"] / 2 + mg + (b + 0.5) * L.BAY
        u = rr.random()
        if u < valance_p:
            px, py, pz = L.building_point(bid, lx, -d0 / 2 - 1.17, 2.5)
            put("signs/sign_fascia_%d.gltf" % brand, px, py, pz, yaw, 0.48, zone, tint={"neon_a": key}, bob=bob)
        elif u < valance_p + 0.35:
            px, py, pz = L.building_point(bid, lx, -d0 / 2, 2.62)
            put("street/awning_1bay.glb", px, py, pz, yaw, 1.0, "street", bob=bob, color={"cloth": rr.choice([[0.09, 0.02, 0.07], [0.03, 0.03, 0.11], [0.10, 0.045, 0.02], [0.05, 0.05, 0.06]])})
    cand = [blk for blk in m["blocks"][1:] if blk["floors"] >= 2]
    used = set()
    for p in (blade_p, second_p):
        if not cand or rr.random() >= p:
            continue
        blk = rr.choice(cand[:3]); zmid = blk["z0"] + blk["floors"] * L.FLOOR / 2
        piers, _ = L.front_piers(bid, zmid)
        piers = [q for q in piers if round(q[2], 1) not in used]
        if not piers:
            continue
        px, py, lx_, ly_ = rr.choice(piers); used.add(round(lx_, 1))
        if rr.random() < 0.75:
            s = max(0.55, min(1.25, (blk["floors"] * L.FLOOR - 1.3) / 6.66))
            put("signs/sign_blade_%d.gltf" % rr.randrange(8), px, py, z + zmid - 0.1, yaw - math.pi / 2, s, zone, tint={"neon_a": rr.choice(keys)}, tags=["blade"], bob=bob)
        else:
            put("signs/sign_roundel_%d.gltf" % rr.randrange(10), px, py, z + blk["z0"] + 2.2, yaw - math.pi / 2, 0.9, zone, tint={"neon_a": rr.choice(keys)}, bob=bob)
    if roof_p and rr.random() < roof_p and "roof_y_m" in m and m.get("roof") != "garden":
        put("signs/sign_roof_%d.gltf" % rr.randrange(4), x, y, z + m["roof_y_m"], yaw, rr.uniform(0.55, 0.8), zone, tint={"neon_a": rr.choice(("sign_a", "sign_b", "sign_c"))}, bob=bob)
    return brand, key


def vines(bid, rr, p=0.6, bob=None):
    """Tidevine hanging at the piers under a block's eave (never across a window: piers only), on a share of buildings."""
    asset = L.BLD[bid][0]; m = L.MAN[asset]; z = L.BLD[bid][3]
    if rr.random() > p or not m.get("blocks"):
        return 0
    n = 0
    for blk in rr.sample(m["blocks"], min(2, len(m["blocks"]))):
        ztop = blk["z0"] + blk["floors"] * L.FLOOR - 0.3
        piers, _ = L.front_piers(bid, ztop - 1.0)
        for (px, py, lx, ly) in rr.sample(piers, min(len(piers), rr.randint(1, 3))):
            put("garden/vine_curtain_%s.glb" % rr.choice("abc"), px, py, z + ztop, L.BLD[bid][4], 1.0, "vines", bob=bob, tint={"garden": rr.choice(["garden_a", "garden_b"])})
            n += 1
    return n


def string_between(a, b, key, k, bob=None):
    Ls = math.dist(a, b); yaw = math.atan2(b[1] - a[1], b[0] - a[0]); span = 9.0 if Ls <= 12.5 else 16.0
    L.STRING_KEYS.append("%.1f,%.1f,%.1f|%.1f,%.1f,%.1f" % (*a, *b))
    put("street/lantern_string_%dm.glb" % int(span), (a[0] + b[0]) / 2, (a[1] + b[1]) / 2, a[2], yaw, [Ls / span, 1, 1], "lantern_strings", tint={"lantern": key}, iid="lantern_string_m%02d" % k, bob=bob)


# ============================================================================================ the Lampmarket
def lampmarket(seen):
    rr = random.Random(9101); C = L.LAMPMARKET; cx, cy = C["centre"]; z = C["deck"]; W, D = C["court"]
    zone = "lampmarket"
    # the court's floor (pads 6 m) and a walk on to the Lantern's deck
    for i in range(-4, 5):
        for j in range(-3, 4):
            put("decks/deck_pad_6m.glb", cx + i * 5.9, cy + j * 5.9, z, 0.0, [0.99, 1, 0.99], zone + "_deck")
    gx, gy, gz = L.GREAT_LANTERN
    a = (cx + W / 2 + 4.0, cy - 6.0); b = (gx - 25.0, gy + 2.0)
    yawc = math.atan2(b[1] - a[1], b[0] - a[0]); Lw = math.hypot(b[0] - a[0], b[1] - a[1])
    for k in range(int(Lw // 6.0) + 1):
        s = 3.0 + k * 6.0
        put("decks/deck_lowwalk_6m.glb", a[0] + s * math.cos(yawc), a[1] + s * math.sin(yawc), z, yawc, 1.0, zone + "_deck")
        if k % 3 == 1:
            put("street/street_lamp.glb", a[0] + s * math.cos(yawc) - 1.1 * math.sin(yawc), a[1] + s * math.sin(yawc) + 1.1 * math.cos(yawc), z, yawc, 0.8, zone + "_deck")
    ids = []
    # the north row: tall shophouses facing south into the court (and at the approach)
    xx = cx - W / 2 - 2.0
    for asset in ("mooring_shophouse_h", "mooring_shophouse_b", "mooring_shophouse_g", "mooring_shophouse_a", "mooring_shophouse_h"):
        bb = L.MAN["buildings/%s.glb" % asset]["bounds_m"]; d = bb[5] - bb[2]
        x = xx - bb[0] + 0.4; y = cy + D / 2 + d / 2 + 0.6
        bid = building(asset, x, y, z, 0.0, zone, rr, seen, "%s_m%02d" % (asset.replace("mooring_", ""), len(ids)), tries=((0, 0), (0.6, 0), (1.2, 0), (0, 1.0)))
        if bid:
            ids.append(bid); xx = x + bb[3] + 0.4
        else:
            xx += 3.0
    # the west row: facing east
    yy = cy - D / 2 + 1.0
    for asset in ("mooring_shophouse_c", "mooring_shophouse_f", "mooring_shophouse_e"):
        bb = L.MAN["buildings/%s.glb" % asset]["bounds_m"]; w = bb[3] - bb[0]; d = bb[5] - bb[2]
        x = cx - W / 2 - d / 2 - 0.6; y = yy + w / 2 + 0.3
        bid = building(asset, x, y, z, math.pi / 2, zone, rr, seen, "%s_m%02d" % (asset.replace("mooring_", ""), len(ids)), tries=((0, 0), (0, 0.6), (-0.8, 0), (0, 1.4)))
        if bid:
            ids.append(bid); yy = y + w / 2 + 0.3
        else:
            yy += 3.0
    # the north-east corner, facing west across the court; the court's east side is the gate to the Lantern
    bid = building("mooring_shophouse_i", cx + W / 2 + 5.6, cy + D / 2 - 5.0, z, -math.pi / 2, zone, rr, seen, "shophouse_i_m%02d" % len(ids), tries=((0, 0), (0.8, 0), (1.6, -1.0)))
    if bid:
        ids.append(bid)
    # the market's tower at the south-west corner, turned to face the approach
    bid = building("mooring_stack_d", cx - W / 2 - 7.0, cy - D / 2 - 9.0, z, math.radians(-40.0), zone, rr, seen, "stack_d_m%02d" % len(ids), tries=((0, 0), (-2, -2), (-4, 0)))
    if bid:
        ids.append(bid)
    # the second ring, one floor up behind the north and west rows: stacks over the market's roofs
    back = []
    for (asset, dx, dy, yawd) in (("mooring_stack_e", -24, 40, -10), ("tower_mast", -4, 42, 0), ("mooring_stack_a", 16, 40, 8), ("mooring_capsule_tower", -44, 22, -30),
                                  ("tower_lean", -46, -2, -45), ("mooring_shophouse_h", 34, 34, 0)):
        bid = building(asset, cx + dx, cy + dy, z + 3.2, math.radians(yawd), zone, rr, seen, "%s_m%02d" % (asset.replace("mooring_", ""), len(ids) + len(back)),
                       tries=((0, 0), (0, 2), (2, 2), (-2, 3), (0, 5)))
        if bid:
            back.append(bid); forecourt(bid, zone, extra=0)
    for bid in ids:
        dress(bid, rr, blade_p=1.0, second_p=0.6, valance_p=0.62, roof_p=0.45, zone="signs")
    for bid in back:
        dress(bid, rr, blade_p=0.8, second_p=0.3, valance_p=0.3, roof_p=0.5, zone="signs")
    # the stalls: two lines of kiosks with awnings, crates, lamps; residents at the counters
    for i, (sx, sy, syaw) in enumerate(((-14, 6, 0), (-6, 6, 0), (2, 6, 0), (10, 6, 0), (-14, -6, math.pi), (-6, -6, math.pi), (2, -6, math.pi), (10, -6, math.pi))):
        x, y = cx + sx + rr.uniform(-0.5, 0.5), cy + sy
        put("buildings/kiosk.glb", x, y, z, syaw, 1.0, zone, tint={"win_warm": "amber", "strip": "eave"})
        fx, fy = L.rot(syaw, 0.0, -1.7)
        put("street/awning_2bay.glb", x + fx, y + fy, z + 2.7, syaw, 0.8, "street", color={"cloth": rr.choice([[0.09, 0.02, 0.07], [0.03, 0.03, 0.11], [0.10, 0.045, 0.02]])})
        if i % 2 == 0:
            put("street/crate_stack.glb", x + 2.4, y + rr.uniform(-1, 1), z, rr.uniform(0, 6.28), 1.0, zone)
        ux, uy = L.rot(syaw, 0.0, -2.6)
        L.spot("use" if i % 3 else "look", x + ux, y + uy, z, math.degrees(syaw) + 90, zone, 3, look=(x, y, z + 1.4), note="at a market stall")
    for k, (sx, sy) in enumerate(((-20, -14), (18, -14), (-20, 14), (18, 14))):
        put("street/street_lamp.glb", cx + sx, cy + sy, z, 0.6 * k, 1.0, zone + "_deck")
    # lantern strings across the court, north row to the stalls' south line: low, warm, criss-crossed
    k = 0
    for (x0, x1) in ((-20, -8), (-8, 6), (6, 18), (-16, 14)):
        string_between((cx + x0, cy + D / 2 - 0.5, z + 6.2), (cx + x1, cy - D / 2 + 2.0, z + 6.2), "amber" if k % 2 else "win_b", k); k += 1
    for (y0, y1) in ((-10, 4), (4, -12)):
        string_between((cx - W / 2 + 0.5, cy + y0, z + 7.0), (cx + W / 2, cy + y1, z + 7.0), "amber", k); k += 1
    for (sx, sy, key, r) in ((-10, 0, "amber", 26.0), (10, 0, "sign_b", 24.0), (0, 14, "sign_a", 20.0)):
        lamp(cx + sx, cy + sy, z + 5.0, key, r, 1.2, zone)
    L.path("lampmarket_stroll", [(cx - 18, cy - 1, z), (cx - 4, cy + 1, z), (cx + 14, cy - 1, z), (cx + 30, cy - 5, z)], zone, 3, 1.1, count=2)
    for (sx, sy) in ((-4, 12), (6, -12), (16, 10)):
        L.spot("gather", cx + sx, cy + sy, z, rr.uniform(0, 360), zone, 3, note="in the market court")
    return ids + back


# ============================================================================================ Lanternhill
def lanternhill(seen):
    rr = random.Random(9102); H = L.LANTERNHILL; zone = "lanternhill"; ids = []
    # low houses in front, the tall stacks only near the crest; houses in twos and threes with a planted gap between groups
    palette = [["mooring_shophouse_c", "mooring_shophouse_g", None, "mooring_shophouse_f", "mooring_shophouse_d", None, "mooring_shophouse_a", "mooring_shophouse_e", "mooring_shophouse_c"],
               ["mooring_shophouse_h", "tower_hanging", None, "mooring_shophouse_g", "mooring_shophouse_b", None, "mooring_shophouse_f", "mooring_stack_c"],
               ["mooring_stack_c", "tower_setback", None, "mooring_stack_a", None, "mooring_shophouse_e", "tower_lean"],
               ["tower_spire", "tower_crowned", None, None, "tower_twin"]]
    x0, x1 = H["x"]
    for ri, (z, y) in enumerate(H["rows"]):
        lst = palette[ri]; span = (x1 - x0) * (1.0 - 0.12 * ri); xa = (x0 + x1) / 2 - span / 2
        for k, asset in enumerate(lst):
            x = xa + span * (k + 0.5) / len(lst) + rr.uniform(-2.5, 2.5); yy = y + rr.uniform(-3, 3) + 0.08 * (x - 0)
            yaw = math.radians(H["yaw_deg"] + rr.uniform(-7, 7))
            if asset is None:                                               # a planted gap: an umbel tree, a bed, a bench, a lamp
                for j in range(-1, 2):
                    dx, dy = L.rot(yaw, j * 6.0, -4.0); put("decks/deck_pad_6m.glb", x + dx, yy + dy, z, yaw, 1.0, zone + "_deck")
                put("garden/tree_pit.glb", x, yy - 4.0, z, 0.0, 1.0, zone + "_garden")
                put("garden/umbel_tree_%s.glb" % rr.choice("abc"), x, yy - 4.0, z + 0.35, rr.uniform(0, 6.28), rr.uniform(0.9, 1.2), zone + "_garden", tint={"garden": rr.choice(["garden_a", "garden_b"])})
                put("garden/sprig_bed_3m.glb", x + 3.5, yy - 6.0, z, rr.uniform(0, 6.28), 1.0, zone + "_garden", tint={"garden": "garden_b"})
                put("garden/shrub_%s.glb" % rr.choice("ab"), x - 3.2, yy - 2.0, z, rr.uniform(0, 6.28), 1.3, zone + "_garden", tint={"garden": "garden_a"})
                lamp(x, yy - 4.0, z + 6.0, rr.choice(["garden_a", "garden_b"]), 14.0, 0.8, zone)
                L.spot("gather", x + 1.5, yy - 6.5, z, rr.uniform(0, 360), zone, 3, note="under an umbel tree on Lanternhill")
                continue
            bid = building(asset, x, yy, z, yaw, zone, rr, seen, "%s_h%d%d" % (asset.replace("mooring_", ""), ri, k), tries=((0, 0), (2, 0), (-2, 0), (0, 3), (3, 3), (-3, 3), (0, 6)))
            if not bid:
                continue
            ids.append(bid); lp = forecourt(bid, zone)
            if k % 2 == 0:
                lamp(*lp, "amber", 10.0, 0.5, zone)
            m = L.MAN["buildings/%s.glb" % asset]
            if m.get("roof") == "garden":
                rc, rs = m["roof_centre_m"], m["roof_size_m"]
                px, py, pz = L.building_point(bid, rc[0], rc[1], m["roof_y_m"])
                L.spot("gather", px, py, pz, math.degrees(yaw) - 90, "roof_terrace", 3, note="a roof garden on Lanternhill")
                lamp(px, py, pz + 2.2, "garden_a", 7.0, 0.5, "roof_terrace")
            elif "roof_y_m" in m and rr.random() < 0.55:                    # roof gardens added on plain roofs: planters and a sapling
                rz = m["roof_y_m"]; rc = m.get("roof_centre_m", [0.0, 0.0])
                for (lx, ly, a_) in ((-2.0, 1.0, "garden/planter_trough_c.glb"), (2.2, -1.2, "garden/planter_round.glb")):
                    px, py, pz = L.building_point(bid, rc[0] + lx, rc[1] + ly, rz)
                    put(a_, px, py, pz, yaw, 1.0, "roof_garden", tint={"garden": rr.choice(["garden_a", "garden_b"])})
                px, py, pz = L.building_point(bid, rc[0] + 0.4, rc[1] + 1.6, rz)
                put("garden/umbel_sapling.glb", px, py, pz, rr.uniform(0, 6.28), 1.2, "roof_garden", tint={"garden": "garden_a"})
            if rr.random() < 0.5:
                dress(bid, rr, blade_p=0.5, valance_p=0.2, zone="signs")
        L.path("lanternhill_walk_%d" % ri, [(xa, y - 9.0, z), (xa + span, y - 9.0 + 0.08 * span, z)], zone, 3, 1.1)
    # the Mooring Mast at the crest: its deck, a stair down to the top terrace, a light on the cloud round it
    mx, my, mz = L.MOORING_MAST
    put("decks/deck_round_14m.glb", mx, my, mz, 0.3, 1.0, zone + "_deck")
    if "landmarks/mooring_mast.glb" in L.MAN:
        put("landmarks/mooring_mast.glb", mx, my, mz, math.radians(-18.0), 1.0, zone, tint={"strip": "sign_c", "lantern": "amber"}, iid="mooring_mast")
    L.ENV.append(dict(id="mooring_mast", c=(mx, my), yaw=0.0, x0=-6.0, x1=6.0, y0=-6.0, y1=6.0, z0=mz, z1=mz + 140.0, zone=zone))
    lamp(mx, my, mz + 60.0, "sign_c", 40.0, 1.2, zone, name="mooring_mast")
    for ang in (200, 250):
        L.spot("look", mx + 11 * math.cos(math.radians(ang)), my + 11 * math.sin(math.radians(ang)), mz, ang + 180, zone, 3, look=(mx, my, mz + 80), note="at the foot of the Mooring Mast, looking up")
    return ids


# ============================================================================================ Eastreach
def eastreach(seen):
    rr = random.Random(9103); E_ = L.EASTREACH; zone = "eastreach"; ids = []
    rows = [["mooring_shophouse_e", "mooring_shophouse_d", "mooring_shophouse_i", "mooring_shophouse_f", "mooring_shophouse_j", "mooring_shophouse_e", "mooring_shophouse_d"],
            ["mooring_stack_c", "mooring_shophouse_f", "tower_setback", "mooring_shophouse_e", "mooring_stack_c", "mooring_shophouse_i"],
            ["tower_crowned", "mooring_shophouse_d", "mooring_stack_c", "tower_hanging"]]
    for ri, (z, y, (xa, xb)) in enumerate(E_["rows"]):
        lst = rows[ri]
        for k, asset in enumerate(lst):
            x = xa + (xb - xa) * (k + 0.5) / len(lst) + rr.uniform(-2, 2); yy = y + rr.uniform(-2.5, 2.5)
            yaw = math.radians(E_["yaw_deg"] + rr.uniform(-8, 8))
            bid = building(asset, x, yy, z, yaw, zone, rr, seen, "%s_e%d%d" % (asset.replace("mooring_", ""), ri, k), tries=((0, 0), (2, 0), (-2, 0), (0, 3), (2, 3), (-2, 3)))
            if not bid:
                continue
            ids.append(bid); lp = forecourt(bid, zone, extra=1)
            if k % 2:
                lamp(*lp, "amber", 9.0, 0.5, zone)
            m = L.MAN["buildings/%s.glb" % asset]
            rz = m.get("roof_y_m")
            if rz is None:
                continue
            rc = m.get("roof_centre_m", [0.0, 0.0]); rs = m.get("roof_size_m", [6.0, 6.0])
            # more planting on every roof: saplings and planters at the corners, a lamp; residents among them
            for (fx, fy) in ((-0.3, 0.25), (0.3, -0.2)):
                px, py, pz = L.building_point(bid, rc[0] + fx * rs[0], rc[1] + fy * rs[1], rz)
                put(rr.choice(["garden/umbel_sapling.glb", "garden/umbel_tree_s.glb"]), px, py, pz + 0.1, rr.uniform(0, 6.28), rr.uniform(0.8, 1.0), "roof_garden", tint={"garden": rr.choice(["garden_a", "garden_b"])})
            px, py, pz = L.building_point(bid, rc[0], rc[1] + 0.35 * rs[1], rz)
            put("garden/planter_trough_c.glb", px, py, pz, yaw, 1.0, "roof_garden", tint={"garden": "garden_b"})
            px, py, pz = L.building_point(bid, rc[0], rc[1], rz)
            lamp(px, py, pz + 2.0, "garden_a" if k % 2 else "garden_b", 9.0, 0.6, "roof_garden")
            L.spot("gather", px, py, pz, math.degrees(yaw) - 90, "roof_terrace", 3, note="a roof garden in Eastreach")
    return ids


# ============================================================================================ the floating settlements
def gondola_line(a, b, name, zone, n_pyl=0, bob=None, key="amber"):
    """A cable pair from a to b (plan points at the cable's height) with a cabin on each; pylons in between."""
    dx, dy = b[0] - a[0], b[1] - a[1]; Lh = math.hypot(dx, dy); u = (dx / Lh, dy / Lh); nrm = (-u[1], u[0]); yaw = math.atan2(dy, dx)
    pts = [a] + [(a[0] + dx * k / (n_pyl + 1), a[1] + dy * k / (n_pyl + 1), a[2] + (b[2] - a[2]) * k / (n_pyl + 1)) for k in range(1, n_pyl + 1)] + [b]
    for p in pts[1:-1]:
        put("street/gondola_pylon.glb", p[0], p[1], p[2] - 14.0, yaw, [1, 1.6, 1], zone)
    lines = []
    for s in (1, -1):
        line = [(p[0] + nrm[0] * 2.2 * s, p[1] + nrm[1] * 2.2 * s, p[2]) for p in pts]; lines.append(line)
        for p, q in zip(line[:-1], line[1:]):
            Lc = math.dist(p, q); sl = math.asin((q[2] - p[2]) / Lc)
            put("street/cable_unit.glb", p[0], p[1], p[2], yaw, [Lc, 1, 1], zone, roll=-sl)
    for k, (line, f) in enumerate(((lines[0], 0.35), (lines[1], 0.68))):
        p, q = line[0], line[-1]; x, y, z = (p[0] + (q[0] - p[0]) * f, p[1] + (q[1] - p[1]) * f, p[2] + (q[2] - p[2]) * f)
        put("vehicles/gondola_cabin.glb", x, y, z - 3.6, yaw, 1.0, zone, tint={"win_warm": "amber"}, iid="%s_cabin_%d" % (name, k),
            tags=["vehicle:%s_%s" % (name, "out" if k == 0 else "back"), "static_stand_in: leave this instance out when the vehicle of the same spline is animated"])
    return lines


def lampwick(seen):
    """A hamlet on its own cloud south-east of the garden: six houses round a little green, lanterns strung between
    them, umbel trees, a landing for the gondola from the garden's promenade. It floats: everything carries bob."""
    rr = random.Random(9104); x0, y0, z0, rad = L.ISLE_LAMPWICK; zone = "lampwick"; bob = "isle_lampwick"; ids = []
    for i in range(-2, 3):
        for j in range(-2, 3):
            if i * i + j * j <= 6:
                put("decks/deck_pad_6m.glb", x0 + i * 5.9, y0 + j * 5.9, z0, 0.0, [0.99, 1, 0.99], zone + "_deck", bob=bob)
    for k, (asset, ang, dist, face) in enumerate((("mooring_shophouse_c", 200, 21, 0), ("mooring_stack_e", 140, 22, 0), ("mooring_shophouse_f", 80, 21, 0),
                                                 ("mooring_shophouse_j", 20, 20, 0), ("mooring_shophouse_d", 300, 20, 0), ("mooring_stack_c", 250, 24, 0))):
        a = math.radians(ang); x, y = x0 + dist * math.cos(a), y0 + dist * math.sin(a)
        yaw = math.atan2(y0 - y, x0 - x) + math.pi / 2          # front (local -y) toward the green
        bid = building(asset, x, y, z0, yaw, zone, rr, seen, "%s_lw%d" % (asset.replace("mooring_", ""), k), bob=bob, tries=((0, 0), (1.5 * math.cos(a), 1.5 * math.sin(a)), (3 * math.cos(a), 3 * math.sin(a))))
        if bid:
            ids.append(bid)
            dress(bid, rr, blade_p=0.6, valance_p=0.3, zone="signs", bob=bob)
    for k, (tx, ty, v) in enumerate(((0, 2, "a"), (-6, -5, "s"), (7, -4, "b"))):
        put("garden/tree_pit.glb", x0 + tx, y0 + ty, z0, 0.0, 1.0, zone, bob=bob)
        put("garden/umbel_tree_%s.glb" % v, x0 + tx, y0 + ty, z0 + 0.35, rr.uniform(0, 6.28), 1.0, zone, tint={"garden": "garden_a" if k % 2 else "garden_b"}, bob=bob)
    pts = [(x0 + 15 * math.cos(math.radians(a)), y0 + 15 * math.sin(math.radians(a)), z0 + 6.5) for a in range(0, 360, 60)]
    for k in range(6):
        string_between(pts[k], pts[(k + 2) % 6], "amber" if k % 2 else "win_b", 20 + k, bob=bob)
    lamp(x0, y0, z0 + 6.0, "amber", 30.0, 1.4, zone, bob=bob, name="lampwick_green")
    for k in range(3):
        a = 2.1 * k; L.spot("gather", x0 + 3 * math.cos(a), y0 + 3 * math.sin(a), z0, math.degrees(a) + 180, zone, 2, look=(x0, y0, z0 + 3))
    L.SPOTS[-1]["bob"] = bob; L.SPOTS[-2]["bob"] = bob; L.SPOTS[-3]["bob"] = bob
    # its landing and the gondola from the garden's promenade
    ga = (L.GARDEN[0] + 18.0, L.GARDEN[1] - 22.0, L.GARDEN[2] + 14.0)
    gb = (x0 - 12.0, y0 + 26.0, z0 + 14.0)
    put("street/gondola_pylon.glb", ga[0], ga[1], L.GARDEN[2], math.atan2(gb[1] - ga[1], gb[0] - ga[0]), 1.0, "garden")
    put("street/gondola_pylon.glb", gb[0], gb[1], z0, math.atan2(gb[1] - ga[1], gb[0] - ga[0]), 1.0, zone, bob=bob)
    put("decks/deck_pad_6m.glb", gb[0], gb[1], z0, 0.0, 1.0, zone + "_deck", bob=bob)
    lines = gondola_line(ga, gb, "gondola_lampwick", "gondola", n_pyl=0)
    return ids, lines


def watch(seen):
    """One tall house on a tall cloud stack, alone: it floats. Its roof beacon and lit stair read from the Quay's end."""
    rr = random.Random(9105); x, y, z, rad = L.ISLE_WATCH; bob = "isle_watch"
    bid = building("mooring_stack_b", x, y, z, math.radians(150.0), "watch", rr, seen, "stack_b_watch", bob=bob)
    for k in (-1, 0, 1):
        dx, dy = L.rot(math.radians(150.0), k * 6.0, -9.0); put("decks/deck_pad_6m.glb", x + dx, y + dy, z, math.radians(150.0), 1.0, "watch_deck", bob=bob)
    lamp(x, y, z + 8, "amber", 26.0, 1.2, "watch", bob=bob, name="watch_lamp")
    return [bid] if bid else []


def keep_town(seen):
    """The Keep's town: a settlement on the colossus's broad shelf, 2.6 km out on the left of the approach. At that range
    it is lights: windows, eave lines, three roof signs at twice life size, lantern strings; the buildings turned so their
    lit stairwells face the Mooring. Its own random source; everything carries bob."""
    rr = random.Random(9106); x0, y0, z0 = L.KEEP_SHELF; bob = "keep"; zone = "keep"; ids = []
    face = math.atan2(-y0, -x0) + math.pi / 2                     # fronts toward the Mooring
    rows = [(0.0, 0.0, ["mooring_shophouse_h", "mooring_shophouse_b", "mooring_shophouse_a", "mooring_shophouse_g", "mooring_shophouse_h"]),
            (6.4, 26.0, ["mooring_stack_d", "mooring_stack_a", "mooring_corner_tower", "mooring_stack_b"]),
            (12.8, 50.0, ["mooring_stack_b", "mooring_capsule_tower", "mooring_stack_d"])]
    for (dz, back, lst) in rows:
        for k, asset in enumerate(lst):
            v = (k - (len(lst) - 1) / 2) * 19.0 + rr.uniform(-2, 2)
            lx, ly = L.rot(face, v, back)
            bid = building(asset, x0 + lx, y0 + ly, z0 + dz, face + rr.uniform(-0.08, 0.08), zone, rr, seen, "%s_kp%d" % (asset.replace("mooring_", ""), len(ids)), bob=bob,
                           tries=((0, 0), (2, 2), (-2, 2)))
            if bid:
                ids.append(bid); forecourt(bid, zone, extra=0, bob=bob)
                m = L.MAN["buildings/%s.glb" % asset]
                if asset in ("mooring_stack_d", "mooring_shophouse_h") and "roof_y_m" in m:
                    put("signs/sign_roof_%d.gltf" % rr.randrange(4), x0 + lx, y0 + ly, z0 + dz + m["roof_y_m"], face, 2.4, "keep", tint={"neon_a": rr.choice(("sign_a", "sign_b"))}, bob=bob)
    lamp(x0, y0, z0 + 20, "amber", 120.0, 2.0, zone, bob=bob, name="keep_town")
    return ids


# ============================================================================================ rock islands (owner msgs 78, 80)
# Everything that floats away from the Mooring stands on a solid floating rock island wrapped in cloud at its rim: the
# lighthouses, the Condensery, the Lone Lamp, Highwater, Lampwick, the Watch and the Keep's town. The rock is the
# owner's floating island of the Tree of Life POC, re-shaded for the night by nd_isles.py (third-party: gitignored,
# referenced by path, never committed); the hanging roots under it are the kit's own (garden/root_curtain_*).
# (bob, rock variant, plan x, y, top z, width m, yaw deg, roots: [(dx, dy, scale)])
ISLANDS = []


def island(bob, variant, x, y, ztop, width, yaw_deg, zone, roots=((0.0, 0.0, 1.0),), curtain="a"):
    a = "_thirdparty/isles/isle_rock_%s.gltf" % variant
    if a not in L.MAN:
        return
    depth = -L.MAN[a]["bounds_m"][1] * width
    put(a, x, y, ztop, math.radians(yaw_deg), [width, width, width], zone, bob=bob, iid="isle_%s" % bob)
    # hanging roots: a curtain is sized for a 60 m island, so a big island takes several, never scaled past 2.2 (at 14x the
    # Condensery's buds were 4 m balls of light under the works, draft 1)
    rr = random.Random(int(abs(x) * 7 + abs(y) * 3)); n = max(1, int(round((width / 60.0) ** 1.4)))
    key = "cond_b" if bob == "condensery" else None
    for k in range(min(n, 9) if roots else 0):
        a = rr.uniform(0, 6.283); d = width * 0.2 * math.sqrt(rr.random()) if n > 1 else 0.0
        s_ = min(2.2, max(0.5, width / 60.0 * rr.uniform(0.7, 1.0)))
        put("garden/root_curtain_%s.glb" % (curtain if k % 2 == 0 else ("b" if curtain == "a" else "a")), x + d * math.cos(a), y + d * math.sin(a), ztop - depth * (0.25 + 0.25 * d / max(1.0, width * 0.2)),
            rr.uniform(0, 6.28), [s_, s_, s_], "condensery" if bob == "condensery" else zone, bob=bob, tint={"garden": key or ("garden_b" if k % 2 else "garden_a")})
    ISLANDS.append({"bob": bob, "asset": a, "centre": L.E((x, y, ztop)), "width_m": width, "depth_m": round(depth, 1), "zone": zone})


def islands():
    for (name, asset, x, y, z, yawd, rpm, ph, blen) in L.LIGHTHOUSES:
        var, w = {"harbour_light": ("a", 34.0), "outer_light": ("a", 40.0), "break_light": ("c", 44.0)}[name]      # the Harbour Light's spire rock was a black chimney across the 2.8 s frame
        island(name, var, x, y, z - 0.6, w, yawd + 20, "lighthouse_isle", roots=((0, 0, 0.8), (0.12, -0.1, 0.5)), curtain="b")
    cx, cy, cz = L.CONDENSERY
    island("condensery", "b", cx, cy, cz - 7.0, 330.0, math.degrees(L.CONDENSERY_YAW) + 10, "condensery_isle", roots=((0, 0, 2.6), (-0.18, 0.08, 2.0), (0.2, -0.1, 1.8), (0.05, 0.2, 1.6)))
    lx, ly, lz = L.LONE_LAMP
    island("lone_lamp", "a", lx, ly, lz - 0.4, 40.0, 30.0, "lone_lamp_isle", roots=((0, 0, 0.7),), curtain="b")
    hx, hy, hz = L.HIGHWATER
    island("highwater", "b", hx, hy, hz - 0.8, 220.0, 20.0, "highwater_isle", roots=((0, 0, 2.0), (0.15, 0.1, 1.4)))
    dx, dy = L.rot(L.HIGHWATER_YAW, 20.0, 0.0)
    island("highwater", "a", hx + dx, hy + dy, hz + 5.6, 120.0, -30.0, "highwater_isle", roots=())
    x0, y0, z0, rad = L.ISLE_LAMPWICK
    island("isle_lampwick", "b", x0, y0, z0 - 0.4, 78.0, 15.0, "lampwick_isle", roots=((0, 0, 1.2), (0.1, 0.15, 0.7)))
    x, y, z, rad = L.ISLE_WATCH
    island("isle_watch", "c", x, y, z - 0.5, 36.0, 0.0, "watch_isle", roots=((0, 0, 0.6),), curtain="b")
    kx, ky, kz = L.KEEP_SHELF
    island("keep", "b", kx, ky + 25, kz - 0.8, 170.0, 40.0, "keep_isle", roots=((0, 0, 2.2),))


def small_isles(seen):
    """Owner msg 83: the gaps round the town filled with small floating islands carrying gardens, houses and towers."""
    rr = random.Random(9120); n = 0
    for (bob, var, x, y, z, w, yawd, what) in L.SMALL_ISLES:
        zone = bob; yaw = math.radians(yawd)
        island(bob, var, x, y, z, w, yawd, zone + "_isle", roots=((0, 0, w / 60.0 * 1.6),), curtain="a" if w > 30 else "b")
        R_ = w * 0.36
        for i in range(-2, 3):                                              # a paved top: pads where it is flat enough
            for j in range(-2, 3):
                if (i * 6.0) ** 2 + (j * 6.0) ** 2 < (R_ - 2.0) ** 2:
                    put("decks/deck_pad_6m.glb", x + i * 5.9, y + j * 5.9, z, 0.0, [0.99, 1, 0.99], zone + "_deck", bob=bob)
        kind, _, asset = what.partition(":")
        if kind in ("house", "tower"):
            bid = building(asset, x, y + 1.0, z, yaw, zone, rr, seen, "%s_%s" % (asset.replace("mooring_", ""), bob), bob=bob, tries=((0, 0), (1.5, 0), (0, 1.5)))
            if bid:
                dress(bid, rr, blade_p=0.6, valance_p=0.3, zone="signs", bob=bob); vines(bid, rr, p=0.9, bob=bob); n += 1
            tx, ty = x - R_ * 0.55, y - R_ * 0.55
        elif kind == "houses":
            for k, (a_, dx, dy, fy) in enumerate((("mooring_shophouse_i", -9.0, 4.0, -10), ("mooring_shophouse_d", 9.0, 6.0, 15))):
                bid = building(a_, x + dx, y + dy, z, math.radians(fy), zone, rr, seen, "%s_%s%d" % (a_.replace("mooring_", ""), bob, k), bob=bob, tries=((0, 0), (1.5, 0), (0, 1.5)))
                if bid:
                    dress(bid, rr, blade_p=0.5, valance_p=0.4, zone="signs", bob=bob); vines(bid, rr, p=0.9, bob=bob); n += 1
            put("garden/pergola_4m.glb", x, y - 9.0, z, yaw, 1.0, zone, bob=bob)
            tx, ty = x + 1.0, y - 15.0
        elif kind == "tea":
            put("buildings/kiosk.glb", x + 4.0, y + 3.0, z, yaw + math.pi, 1.0, zone, bob=bob, tint={"win_warm": "amber", "strip": "eave"})
            put("garden/pavilion.glb", x - 5.0, y + 4.0, z, yaw, 1.0, zone, bob=bob, tint={"win_warm": "amber", "strip": "eave"})
            for k in range(3):
                put("garden/garden_bench.glb", x - 2.0 + 3.0 * k, y - 5.0, z, yaw, 1.0, zone, bob=bob)
            pts = [(x + 11 * math.cos(a), y + 11 * math.sin(a), z + 5.5) for a in (0.3, 2.4, 4.4)]
            for k in range(3):
                string_between(pts[k], pts[(k + 1) % 3], "amber" if k % 2 else "win_b", 40 + k, bob=bob)
            L.spot("gather", x - 1.0, y - 3.5, z, 90.0, zone, 2, note="in the tea garden on its own rock"); L.SPOTS[-1]["bob"] = bob
            tx, ty = x + 6.0, y - 7.0
        else:
            tx, ty = x, y
        nt = {"grove": 4, "tree": 1}.get(kind, 1)
        for k in range(nt):                                                 # umbel trees and bioluminescent beds
            a = k * 2.4 + 0.7; d = 0 if nt == 1 else R_ * 0.5
            px, py = tx + d * math.cos(a), ty + d * math.sin(a)
            put("garden/tree_pit.glb", px, py, z, 0.0, 1.0, zone, bob=bob)
            put("garden/umbel_tree_%s.glb" % rr.choice("abc"), px, py, z + 0.35, rr.uniform(0, 6.28), rr.uniform(0.9, 1.25), zone, bob=bob, tint={"garden": rr.choice(["garden_a", "garden_b"])})
            put("garden/sprig_bed_3m.glb", px + 2.6, py - 1.5, z, rr.uniform(0, 6.28), 1.0, zone, bob=bob, tint={"garden": "garden_b"})
            put("garden/shrub_%s.glb" % rr.choice("ab"), px - 2.4, py + 1.2, z, rr.uniform(0, 6.28), 1.4, zone, bob=bob, tint={"garden": "garden_a"})
        if kind == "grove":
            put("garden/pavilion.glb", x + 4.0, y - 2.0, z, yaw, 1.0, zone, bob=bob, tint={"win_warm": "amber", "strip": "eave"})
            for k in range(3):
                L.spot("gather", x + 2.0 * math.cos(k * 2.1), y + 2.0 * math.sin(k * 2.1), z, k * 120.0, zone, 2, note="in the grove on its own rock"); L.SPOTS[-1]["bob"] = bob
        lamp(x, y, z + 5.0, rr.choice(["garden_a", "garden_b", "amber"]), max(14.0, w * 0.5), 0.9, zone, bob=bob)
    return n


def ride(bob, zone_test):
    """Mark what already stands on a formation (the lighthouses, the Condensery, the Lone Lamp, Highwater) with its bob."""
    for i in L.INST:
        if zone_test(i):
            i["bob"] = bob


def build(layout_module):
    global L
    L = layout_module
    seen = _looks()
    out = dict(lampmarket=lampmarket(seen), lanternhill=lanternhill(seen), eastreach=eastreach(seen))
    lw, lw_lines = lampwick(seen)
    out.update(lampwick=lw, watch=watch(seen), keep=keep_town(seen))
    islands()
    out["small_isles"] = small_isles(seen)
    rv = random.Random(9110); nv = 0                                  # nature through the town (owner msg 78): tidevine at the piers
    for bid in list(L.BLD):
        zone = next((i["zone"] for i in L.INST if i.get("id") == bid), "")
        if zone in ("quay_north", "quay_south", "stacks", "lampmarket", "lanternhill", "eastreach", "lampwick"):
            nv += vines(bid, rv, p=0.45 if zone.startswith("quay") else 0.65, bob="isle_lampwick" if zone == "lampwick" else None)
    out["vines"] = nv
    for (name, asset, x, y, z, yawd, rpm, ph, blen) in L.LIGHTHOUSES:
        ride(name, lambda i, x=x, y=y: i["zone"] == "lighthouse" and math.hypot(i["pos"][0] - x, -i["pos"][2] - y) < 40.0)
    ride("condensery", lambda i: i["zone"] == "condensery")
    ride("lone_lamp", lambda i: i["zone"] == "lone_lamp")
    ride("highwater", lambda i: i["zone"] == "highwater")
    for lt in L.LIGHTS:
        for (name, asset, x, y, z, yawd, rpm, ph, blen) in L.LIGHTHOUSES:
            if lt["zone"] == "lighthouse" and math.hypot(lt["pos"][0] - x, -lt["pos"][2] - y) < 40.0:
                lt["bob"] = name
        if lt["zone"] in ("condensery", "lone_lamp", "highwater"):
            lt["bob"] = lt["zone"]
    for sp in L.SPOTS:
        if sp["zone"] in ("condensery", "lone_lamp", "highwater"):
            sp["bob"] = sp["zone"]
    if "far/drowned_mast.glb" in L.MAN:
        put("far/drowned_mast.glb", L.DROWNED_MAST[0], L.DROWNED_MAST[1], L.DROWNED_MAST[2], math.radians(-25.0), 1.0, "anomaly", iid="drowned_mast")
    return out, {"gondola_lampwick_out": lw_lines[0], "gondola_lampwick_back": list(reversed(lw_lines[1]))}
