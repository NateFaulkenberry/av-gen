"""Neon Dreams kit, iteration 12 (owner msg 149): the band stage in the garden plaza, its lights, and two fixes.

  stage/band_stage: the round stage that replaces the garden's pool. A drum 6 m in radius and 1 m high in the
      Lanternwright way: a paved deck with a glowing ring inlaid near its edge (the stage reads as a lit circle from the
      Quay), a fascia with a light line under the lip and footlights round its foot, a drum riser behind the middle,
      steps down at the back. Over it a HALO: a lantern ring 8.8 m up on four masts standing just outside the deck,
      32 lanterns hanging from it, a light line under it, a red beacon on each mast's crown. Each mast carries a
      searchlight bracket at 9.6 m (the head is stage/spot_head, placed by the layout, so each can aim its own way).
      Front = -Y in Blender (+z in the GLB), the band faces it; origin at the plaza's deck level, the stage's centre.
  stage/band_terrace: three standing tiers bending round the BACK of the stage (140 degrees, radii 11.5 to 15.5, rises
      of 0.45 m), lantern footlights on each riser, a lantern balustrade on the top tier. Same frame as the stage (place
      both with one transform): the crowd behind the band, raised so the lens sees it over the band.
  stage/spot_base: a floor fixture's plinth and yoke (pivot 0.95 m up). stage/spot_head: its lamp, a ribbed can with a
      lens (`beam_lamp`) looking along +z in the GLB; origin at the pivot, so an instance's rotation aims it.
  stage/speaker_stack: (it12, owner msg 161) a Lanternwright speaker stack for each side of the band; see its doc.
  stage/pylon_leg_ext: the gondola pylon's mast continued 55 m down (it stopped 12 m under the deck, in the air: owner
      msg 149, the 1.05 s frame). Place at a pylon's position, 12 m lower, same yaw.

    Blender -b --factory-startup -P examples/neon-dreams/blender/nd_kit.py -- --out examples/neon-dreams/meshes \
        --only stage --merge-manifest examples/neon-dreams/meshes/kit_manifest.json
"""
import math, random
from mathutils import Vector
from nd_kitcore import *  # noqa

R_STAGE, H_STAGE = 6.0, 1.0
MAST_R, MAST_A = 6.75, (-45.0, -135.0, 45.0, 135.0)       # plan angles (deg, 0 = +x, CCW); the front is -90
HALO_Z, MAST_TOP = 8.8, 10.6
SPOT_BRACKET_Z = 9.6
TERRACE = dict(r0=11.5, depth=1.33, rise=0.45, tiers=3, a0=20.0, a1=160.0)   # behind the band: +Y is 90 deg


def _lamp_ring(A, mat, r, z, n, rad, level=0.85, squash=1.0, seed=0.0):
    for k in range(n):
        a = k / n * 2 * math.pi
        lamp(A.m(mat), (r * math.cos(a), r * math.sin(a), z), rad, level, hh01(k, r, seed, 3.0), n=8, m=5, squash=squash)


def band_stage():
    A = Asset("band_stage")
    R, H = R_STAGE, H_STAGE
    # the deck: paved rings, a metal lip, an inlaid glowing ring near the edge and a fainter one round the riser
    A.m("trim").cyl((0, 0, 0.0), R - 0.2, H - 0.02, n=48)                    # the body (hidden: caps under the pavers)
    ring_band(A, 0.0, 2.4, H, n=24, kind="pavers", mat="deck")
    ring_band(A, 2.4, 2.55, H, n=24, kind="pavers", mat="metal")
    ring_band(A, 2.55, 5.4, H, n=48, kind="pavers", mat="deck")
    ring_band(A, 5.4, 5.62, H + 0.004, n=64, kind="pavers", mat="strip")         # the stage's light ring
    ring_band(A, 5.62, R, H, n=64, kind="pavers", mat="metal")
    for k in range(8):                                                       # eight inlaid spokes
        a = math.radians(k * 45 + 22.5)
        A.m("metal").box((3.95 * math.cos(a), 3.95 * math.sin(a), H + 0.005), (2.7, 0.12, 0.02), yaw=a)
    wall_ring(A, "trim", R, 0.0, H, n=64)                                    # the fascia
    A.m("strip").cyl((0, 0, H - 0.16), R + 0.03, 0.06, n=64, cap=False)      # the light line under the lip
    A.m("metal").cyl((0, 0, 0.0), R + 0.06, 0.12, n=64, cap=False)            # a kick plate
    _lamp_ring(A, "lantern", R + 0.14, 0.2, 40, 0.07, level=0.9, seed=1.0)    # footlights round its foot
    # the drum riser behind the middle (the drummer sits on it, the keys stand in front at deck level)
    rc = (0.0, 1.5)
    A.m("deck").cyl((rc[0], rc[1], H), 1.75, 0.3, n=32)
    A.m("strip").cyl((rc[0], rc[1], H + 0.22), 1.77, 0.04, n=32, cap=False)
    # steps down at the back
    for i in range(5):
        A.m("deck").box((0.0, R + 0.15 + i * 0.32, H - 0.2 * (i + 1) + 0.1), (2.6, 0.34, 0.2))
    for sx in (-1.35, 1.35):
        A.m("metal").beam((sx, R, H + 0.95), (sx, R + 1.6, 0.95), 0.05)
        A.m("metal").beam((sx, R + 1.6, 0.0), (sx, R + 1.6, 0.95), 0.05)
    # the halo: four masts outside the deck, a double ring truss with a lattice, hanging lanterns, a light line
    tops = []
    for ad in MAST_A:
        a = math.radians(ad)
        x, y = MAST_R * math.cos(a), MAST_R * math.sin(a)
        A.m("metal").box((x, y, 0.06), (0.9, 0.9, 0.12), yaw=a)                 # foot plate
        A.m("metal").cyl((x, y, 0.0), 0.17, MAST_TOP, n=10, r_top=0.12)
        A.m("metal").beam((x, y, 1.2), (x * 0.93, y * 0.93, 0.12), 0.06)           # a kicker toward the deck
        lamp(A.m("beacon"), (x, y, MAST_TOP + 0.12), 0.14, 1.0, 0.5, n=8, m=5)
        # the searchlight bracket on the outer side: an arm and a fork; the head (spot_head) sits at the fork's pivot
        ox, oy = math.cos(a), math.sin(a)
        A.m("metal").beam((x, y, SPOT_BRACKET_Z - 0.45), (x + ox * 0.55, y + oy * 0.55, SPOT_BRACKET_Z - 0.45), 0.09)
        A.m("metal").cyl((x + ox * 0.55, y + oy * 0.55, SPOT_BRACKET_Z - 0.5), 0.07, 0.35, n=6)
        tops.append((x, y))
    n = 48
    for rr, zz in ((MAST_R, HALO_Z), (MAST_R - 0.45, HALO_Z), (MAST_R - 0.22, HALO_Z + 0.5)):
        for k in range(n):
            a0, a1 = k / n * 2 * math.pi, (k + 1) / n * 2 * math.pi
            A.m("metal").beam((rr * math.cos(a0), rr * math.sin(a0), zz), (rr * math.cos(a1), rr * math.sin(a1), zz), 0.07)
    for k in range(n):                                                       # the lattice: a zigzag between the rings
        a0, a1 = k / n * 2 * math.pi, (k + 1) / n * 2 * math.pi
        r_a, r_b = (MAST_R, MAST_R - 0.45) if k % 2 == 0 else (MAST_R - 0.45, MAST_R)
        A.m("metal").beam((r_a * math.cos(a0), r_a * math.sin(a0), HALO_Z), ((MAST_R - 0.22) * math.cos(a1), (MAST_R - 0.22) * math.sin(a1), HALO_Z + 0.5), 0.035)
        A.m("metal").beam((r_b * math.cos(a0), r_b * math.sin(a0), HALO_Z), ((MAST_R - 0.22) * math.cos(a0), (MAST_R - 0.22) * math.sin(a0), HALO_Z + 0.5), 0.035)
    A.m("strip").cyl((0, 0, HALO_Z - 0.09), MAST_R - 0.2, 0.06, n=64, cap=False)
    for k in range(32):                                                       # lanterns on drops, alternate lengths
        a = (k + 0.5) / 32 * 2 * math.pi
        rr = MAST_R - 0.45
        x, y = rr * math.cos(a), rr * math.sin(a)
        drop = 0.55 if k % 2 else 1.05
        A.m("metal").beam((x, y, HALO_Z), (x, y, HALO_Z - drop), 0.02)
        lamp(A.m("lantern"), (x, y, HALO_Z - drop - 0.16), 0.16, 0.75 + 0.25 * (k % 3) / 2, hh01(k, 7.0), n=8, m=5, squash=1.3)
    A.meta = dict(radius_m=R, deck_m=H, riser=dict(centre_xz=[rc[0], -rc[1]], radius_m=1.75, top_m=H + 0.3),
                  halo_m=HALO_Z, masts_xz=[[round(x, 3), round(-y, 3)] for x, y in tops], mast_top_m=MAST_TOP,
                  spot_pivots_xyz=[[round(x + math.cos(math.radians(a)) * 0.55, 3), SPOT_BRACKET_Z, round(-(y + math.sin(math.radians(a)) * 0.55), 3)]
                                   for (x, y), a in zip(tops, MAST_A)],
                  front="+z in the GLB (Blender -Y): the band faces it")
    A.finish()
    return A


def band_terrace():
    A = Asset("band_terrace")
    t = TERRACE; D = math.radians
    a0, a1 = D(t["a0"]), D(t["a1"])
    n = 40
    for k in range(t["tiers"]):
        r0 = t["r0"] + k * t["depth"]; r1 = r0 + t["depth"]; z = (k + 1) * t["rise"]
        ring_band(A, r0, r1 if k < t["tiers"] - 1 else r1 + 0.4, z, a0, a1, n, "pavers", "deck" if k % 2 == 0 else "trim")
        wall_ring(A, "trim", r0, z - t["rise"], z, a0, a1, n, inward=True)          # the riser faces the stage
        for i in range(n):                                                          # footlights in each riser
            if i % 2:
                continue
            a = a0 + (a1 - a0) * (i + 0.5) / n
            lamp(A.m("lantern"), ((r0 - 0.04) * math.cos(a), (r0 - 0.04) * math.sin(a), z - t["rise"] * 0.5), 0.06, 0.8, hh01(i, k, 5.0), n=6, m=4)
        for e in (a0, a1):                                                          # the tiers' end walls
            A.m("trim").beam((r0 * math.cos(e), r0 * math.sin(e), z / 2), (r1 * math.cos(e), r1 * math.sin(e), z / 2), z, t2=0.12)
    r_top = t["r0"] + t["tiers"] * t["depth"] + 0.4
    ztop = t["tiers"] * t["rise"]
    wall_ring(A, "trim", r_top, 0.0, ztop, a0, a1, n)                               # the back wall
    balustrade(A, r_top - 0.12, ztop, a0, a1, seed=3.0)
    A.meta = dict(tiers=[dict(r0=round(t["r0"] + k * t["depth"], 2), r1=round(t["r0"] + (k + 1) * t["depth"], 2),
                              top_m=round((k + 1) * t["rise"], 2)) for k in range(t["tiers"])],
                  arc_deg=[t["a0"], t["a1"]], note="plan angles in the asset (0 = +x, CCW in Blender; +90 = behind the band)")
    A.finish()
    return A


def balustrade(A, r, z, a0, a1, step=math.radians(6.0), seed=0.0):
    nseg = max(1, int(round((a1 - a0) / step)))
    P = [(r * math.cos(a0 + (a1 - a0) * i / nseg), r * math.sin(a0 + (a1 - a0) * i / nseg)) for i in range(nseg + 1)]
    for i, (x, y) in enumerate(P):
        tall = i % 4 == 0
        A.m("metal").box((x, y, z + (0.95 if tall else 0.54)), (0.07, 0.07, 1.9 if tall else 1.08))
        if tall:
            lamp(A.m("lantern"), (x, y, z + 1.76), 0.13, 0.8, hh01(x, y, seed), squash=1.25)
    for i in range(nseg):
        (xa, ya), (xb, yb) = P[i], P[i + 1]
        A.m("metal").beam((xa, ya, z + 1.05), (xb, yb, z + 1.05), 0.06, t2=0.05)
        A.m("metal").beam((xa, ya, z + 0.55), (xb, yb, z + 0.55), 0.03)


def spot_base():
    A = Asset("spot_base")
    A.m("metal").box((0, 0, 0.12), (0.56, 0.56, 0.24))
    A.m("trim").box((0, 0, 0.26), (0.44, 0.44, 0.05))
    A.m("metal").cyl((0, 0, 0.28), 0.07, 0.34, n=8)
    A.m("metal").box((0, 0, 0.64), (0.62, 0.12, 0.06))                       # the yoke's bridge
    for sx in (-0.29, 0.29):
        A.m("metal").box((sx, 0, 0.8), (0.04, 0.1, 0.34))                     # the arms up to the pivot at 0.95
    lamp(A.m("lantern"), (0.2, 0.2, 0.3), 0.025, 0.6, 0.3, n=6, m=4)         # a pilot lamp
    A.meta = dict(pivot_m=0.95)
    A.finish()
    return A


def spot_head():
    """The lamp, looking along -Y in Blender (+z in the GLB) from its pivot at the origin."""
    A = Asset("spot_head")
    L0, L1, r = 0.3, -0.36, 0.2
    for k in range(7):                                                       # a ribbed can: alternating bands
        y0 = L0 - (L0 - L1) * k / 7; y1 = L0 - (L0 - L1) * (k + 1) / 7
        rr = r if k % 2 else r + 0.025
        A.m("metal").beam((0, y0, 0), (0, y1, 0), rr * 2 * 0.89, t2=rr * 2 * 0.89)
    A.m("metal").beam((0, L0, 0), (0, L0 + 0.06, 0), 0.3)                    # the back cap
    A.m("trim").beam((0, L1, 0), (0, L1 - 0.06, 0), 0.5, t2=0.5)             # the bezel
    mb = A.m("beam_lamp")
    # the lens: a shallow dome of 16 segments facing -Y
    n = 16; c = Vector((0, L1 - 0.065, 0))
    ring = [Vector((0.19 * math.cos(2 * math.pi * i / n), L1 - 0.062, 0.19 * math.sin(2 * math.pi * i / n))) for i in range(n)]
    tip = c + Vector((0, -0.05, 0))
    for i in range(n):
        mb._add([tuple(ring[(i + 1) % n]), tuple(ring[i]), tuple(tip)], [(0, 1, 2)], None, [(lvl(1.0), 0.5)] * 3)
    for sx in (-0.27, 0.27):                                                 # the trunnions
        A.m("metal").beam((sx, 0, 0), (sx * 0.8, 0, 0), 0.1)
    A.meta = dict(pivot="origin", looks="+z in the GLB (Blender -Y)", lens_m=round(-(L1 - 0.11), 2))
    A.finish()
    return A


def pylon_leg_ext():
    A = Asset("pylon_leg_ext")
    A.m("metal").cyl((0, 0, -55.0), 0.4, 55.0, n=8, r_top=0.34)
    for z in (-14.0, -30.0, -46.0):                                         # collars where the sections meet
        A.m("metal").cyl((0, 0, z), 0.46, 0.4, n=8)
    A.meta = dict(place="at a gondola pylon's position 12 m lower (its mast's foot), same rotation")
    A.finish()
    return A


def _ring_front(mb, cx, y, cz, r0, r1, n=20, uv=None):
    """An annulus facing -Y (the front) at depth y, centre (cx, cz): r0 inner, r1 outer."""
    for i in range(n):
        a0, a1 = 2 * math.pi * i / n, 2 * math.pi * (i + 1) / n
        pts = [(cx + r0 * math.cos(a0), y, cz + r0 * math.sin(a0)), (cx + r1 * math.cos(a0), y, cz + r1 * math.sin(a0)),
               (cx + r1 * math.cos(a1), y, cz + r1 * math.sin(a1)), (cx + r0 * math.cos(a1), y, cz + r0 * math.sin(a1))]
        mb._add(pts, [(0, 1, 2, 3)], None, [uv] * 4 if uv else None)


def _disc_front(mb, cx, y, cz, r, n=20, depth=0.0):
    """A cone: a shallow funnel facing -Y, its apex pushed back by depth."""
    for i in range(n):
        a0, a1 = 2 * math.pi * i / n, 2 * math.pi * (i + 1) / n
        mb._add([(cx + r * math.cos(a0), y, cz + r * math.sin(a0)), (cx, y + depth, cz),
                 (cx + r * math.cos(a1), y, cz + r * math.sin(a1))], [(0, 1, 2)])


def speaker_stack():
    """it12 (owner msg 161): a speaker stack in the Lanternwright way -- cabinets stacked and shifted like the Stacks,
    each driver ringed with a light line (strip: the kick swells it), lantern bulbs on the cabinet corners, a slim
    rigging frame behind with a lamp bar and a red beacon. Front = -Y in Blender (+z in the GLB); base on the deck.
    The meta gives the pulse emitter's centre (the stack's middle driver) for the engine's kick distortion."""
    A = Asset("speaker_stack")
    r = random.Random(161)
    cabs = [  # (width, depth, height, x shift, yaw deg, drivers: [(dx, dz, radius)])
        (1.5, 1.0, 0.95, 0.00, 0.0, [(-0.36, 0.47, 0.34), (0.36, 0.47, 0.34)]),
        (1.4, 0.92, 0.85, 0.07, 3.0, [(0.0, 0.43, 0.36)]),
        (1.36, 0.9, 0.8, -0.05, -2.5, [(-0.34, 0.4, 0.22), (0.34, 0.4, 0.22)]),
        (1.2, 0.82, 0.5, 0.04, 2.0, []),   # the horn box
    ]
    z = 0.0
    drivers = []
    for k, (w, d, h, sx, yaw, drv) in enumerate(cabs):
        c = (sx, 0.0, z + h / 2)
        A.m("trim").box(c, (w, d, h), yaw=math.radians(yaw))
        A.m("metal").box((sx, 0.0, z + h - 0.02), (w + 0.04, d + 0.04, 0.04), yaw=math.radians(yaw))   # a lip on each
        yf = -d / 2 - 0.005
        for dx, dz, rr in drv:   # a dark cone, a light ring round it (strip), a dust cap
            _disc_front(A.m("metal"), sx + dx, yf - 0.004, z + dz, rr * 0.86, depth=0.12)
            _ring_front(A.m("strip"), sx + dx, yf - 0.006, z + dz, rr * 0.86, rr, n=24)
            A.m("trim").sphere((sx + dx, yf - 0.02, z + dz), rr * 0.18, n=8, m=4, squash=1.0)
            drivers.append((sx + dx, yf, z + dz, rr))
        if not drv:              # the horn: a flared mouth with a light line along its lip
            for i in range(5):
                hx = sx - 0.42 + i * 0.21
                A.m("metal").box((hx, yf - 0.01, z + h / 2), (0.16, 0.04, h * 0.62))
            A.m("strip").box((sx, yf - 0.03, z + h - 0.06), (w * 0.86, 0.03, 0.035))
        for cx in (-w / 2 + 0.06, w / 2 - 0.06):   # lantern bulbs at the front corners
            lamp(A.m("lantern"), (sx + cx, yf - 0.05, z + h - 0.08), 0.05, 0.85, hh01(k, cx, 3.0), n=6, m=4)
        z += h
    top = z
    # the rigging: two posts behind the stack up past it, a cross bar with a row of lamps, a beacon
    for px in (-0.62, 0.62):
        A.m("metal").cyl((px, 0.62, 0.0), 0.05, top + 1.1, n=6)
        for zz in (0.9, 1.8, 2.7):
            A.m("metal").beam((px, 0.62, zz), (px * 0.2, 0.5, zz), 0.03)   # ties back to the cabinets
    A.m("metal").beam((-0.7, 0.62, top + 1.05), (0.7, 0.62, top + 1.05), 0.07)
    for i in range(5):
        lamp(A.m("lantern"), (-0.5 + i * 0.25, 0.55, top + 0.93), 0.06, 0.9, hh01(i, 9.0), n=6, m=4, squash=1.3)
    lamp(A.m("beacon"), (0.0, 0.62, top + 1.2), 0.08, 1.0, 0.5, n=6, m=4)
    mid = drivers[2]   # the middle cabinet's big driver
    A.meta = dict(front="+z in the GLB (Blender -Y)", height_m=round(top + 1.25, 2), cabinets=len(cabs),
                  pulse_centre_local=[round(mid[0], 3), round(mid[2], 3), round(-mid[1], 3)],
                  drivers_local=[[round(x, 3), round(zz, 3), round(-y, 3), rr] for x, y, zz, rr in drivers])
    A.finish()
    return A


def stage_kit():
    return [band_stage(), band_terrace(), spot_base(), spot_head(), speaker_stack()]


def street_ext():
    return [pylon_leg_ext()]
