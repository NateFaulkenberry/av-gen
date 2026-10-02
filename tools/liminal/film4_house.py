"""All You Got, art pass 4: the house, owner bars 17-41 (37.43-92.48 s), on Builder4 (pass 3's film3_house.py, revised:
04-art-pass-4.md PARTS 4, 5 and 15). Pass 4: the walls no longer swell on the quarter; the kitchen runs to the
downbeat of bar 38 and its clap (34.4, 76.51 s) finds him at the stove looking into a steaming pot; the hall and the
climb start on 38.1 (pass 2's timing), the climb, the top and the fall are pass 3's.

  17-32  the living room (the release and verse 1's first half): ONE continuous camera that sweeps the room. The
         mannequin is always somewhere in it and never moves while seen: in the armchair as the Thinker when we come
         in through the window (the room's furniture lands around him, he does not), lying on the sofa when the
         camera comes round to it, sitting on its edge with his face in his hands when it returns, standing at the
         front window when it looks back from the far wall. Every change happens while the camera looks elsewhere.
  33-36  through the back door into the kitchen: at the table, his head resting in his hands; C05 lifts the plate,
         the cup, the kettle and the empty chair, never his.
  37-40  through the hall door, round the foot of the stair (he stands beside it, looking up), and up it: one word
         of IT'S STEPS IN A PROCESS, LET IT GO on each riser just ahead, the flight rising out of the house into the
         dark; at C06 (40.4) the house flares and dissolves; the stair ends in an edge.
  41     over the edge: the camera tips and falls into the black, LET IT GO falling with it.

The world breathes on the quarter (lines, lamps, shades, the walls' scale on the smooth wave, the curtains on the
bar, the clock on the beat); the camera never does (section 20). Nothing is moved by a beat pulse (section 28).
"""

from __future__ import annotations

import kit as K
import pass2_grid as G
import rooms3 as R3
import rooms4 as R4

t = G.t
BEAT1 = G.BAR1 / 4.0
ENTRY = t(17)


def lyric_glitch(b, name, tt, amount=0.3, shift=8.0, release=0.12):
    """A short corruption as a line of lyric appears (section 21): the frame tears for a few frames."""
    c = b.clap(name, tt, release=release)
    b.f.route(c, "temporal/mosh/amount", amount)
    b.f.route(c, "temporal/mosh/shift", shift)
    return c


def build(b):
    f = b.f
    house = {"liv": R3.living_room(), "kit": R4.kitchen(), "hall": R3.hall()}
    for key, room in house.items():
        b.room_world(key, room)
    liv, kit, hall = house["liv"], house["kit"], house["hall"]
    ground = ("liv", "kit", "hall")

    # =========================================================================================================
    # THE LIVING ROOM (17.1-33.1): one sweep, four tableaux
    # =========================================================================================================
    eye = [(ENTRY, (-0.6, 1.53, 2.12)), (37.75, (-0.58, 1.52, 1.5)), (38.6, (-0.52, 1.5, 1.18)), (41.5, (-0.25, 1.5, 0.98)),
           (44.5, (0.5, 1.55, 0.55)), (47.5, (0.75, 1.58, 0.38)), (50.6, (0.35, 1.6, 0.2)), (t(24, 4), (0.05, 1.5, -0.8)),
           (t(25), (0.12, 1.48, -0.75)), (57.6, (1.15, 1.36, 0.1)), (60.6, (1.45, 1.38, 0.85)), (63.0, (0.75, 1.5, -0.95)),
           (66.0, (0.5, 1.55, -1.42)), (70.5, (0.25, 1.55, -0.7)), (t(32, 4), (-1.15, 1.52, -1.6)), (t(33), (-1.75, 1.5, -2.2))]
    look = [(ENTRY, (1.3, 0.95, -1.1)), (41.5, (1.35, 0.95, -1.15)), (43.0, (2.6, 1.2, -0.3)), (44.5, (1.6, 1.3, 2.2)),
            (45.7, (-0.6, 1.4, 2.2)), (47.6, (-2.1, 0.62, 0.15)), (50.6, (-2.1, 0.62, 0.2)), (52.5, (-1.0, 1.2, -2.2)),
            (t(24, 4), (0.3, 1.3, -2.2)), (55.6, (0.0, 1.3, -2.2)), (57.2, (-1.95, 0.85, 0.35)), (60.6, (-1.95, 0.85, 0.35)),
            (62.2, (-1.5, 1.4, -2.2)), (t(28, 4), (1.6, 1.3, -2.2)), (64.9, (-0.6, 1.25, 2.0)), (70.5, (-0.6, 1.25, 2.0)),
            (t(32, 4), (-2.0, 1.15, -2.7)), (t(33), (-2.3, 1.1, -3.6))]
    b.glide("liv", eye, look, nodes_keys=ground, fov=62.0)
    f.shots[-1]["fov"] = [(ENTRY, 70.0), (ENTRY + 0.5, 62.0), (t(33), 62.0)]
    # the tableaux, each change while the camera looks away
    s1 = b.swap("livThinker", "livLie", 44.6)
    s2 = b.swap("livLie", "livHands", 53.6)
    s3 = b.swap("livHands", "livWindow", 62.4)
    b.show("livThinker", 0.0, s1)
    b.show("livLie", s1, s2)
    b.show("livHands", s2, s3)

    # ---- the impact (17.1): we are in. A white flash; the furniture lands around him (he does not move) ------
    c = b.clap("impact", ENTRY, release=1.3)
    f.route(c, "camera/exposure/compensation", 2.6)
    f.route(c, "post/bloom/intensity", 2.0)
    f.route(c, "post/lens/chromaticAberration", 0.8)
    f.route(c, "temporal/mosh/amount", 0.55)
    f.route(c, "temporal/mosh/shift", 16.0)
    for o in ("livShell", "livSofa", "livMedia", "livShelf", "livDecor"):
        f.route(c, f"sdf/{o}/look/edge/intensity", 10.0)
    for node, obj, h in (("livCouch", "livSofa", 1.6), ("livTable", "livSofa", 2.2), ("livTV", "livMedia", 1.4),
                         ("livShelfAt", "livShelf", 1.2), ("livPlant", "livMedia", 2.4), ("livLampAt", "livSofa", 1.8)):
        base = _node_translation(liv, obj, node)
        up = [base[0], base[1] + h, base[2]]
        f.track(f"sdf/{obj}/node/{node}/translation", [(0.0, up, "step"), (ENTRY - 0.001, up, "easeIn"),
                                                       (ENTRY + 0.16, base, "step")])
    # (the sofa he will lie on lands; the armchair he sits in is already there)

    # ---- the world breathes on the quarter: lines, lamps, shades, the room's walls; the figure stays still ---
    g_rel = b.gate("gRel", ENTRY, t(24, 4))
    g_v1 = b.gate("gV1", t(25), t(40, 4))
    f.track("sources/song/pulseDecay", [(0.0, 0.3), (t(17), 0.42), (t(25), 0.32), (t(49, 4), 0.3), (t(50), 0.22),
                                        (t(58), 0.16), (t(66), 0.3)])
    b.pulse("sdf/livShell/look/edge/intensity", 3.5, "bar", g_rel)
    for o, amt in (("livSofa", 3.5), ("livMedia", 3.5), ("livShelf", 2.5), ("livDecor", 2.5)):
        b.pulse(f"sdf/{o}/look/edge/intensity", amt, "quarter", g_rel)
        b.pulse(f"sdf/{o}/look/edge/intensity", 1.6, "quarter", g_v1)
    for lamp in ("livLamp", "livSideLamp"):
        f.route("grid.song.quarter", f"lights/{lamp}/intensity", 3.0, depth=g_rel)
        f.route("grid.song.quarter", f"lights/{lamp}/intensity", 2.2, depth=g_v1)
    for o in ("livSofa", "livShelf"):
        f.route("grid.song.quarter", f"sdf/{o}/surface/{K.GLOW}/emission", 2.0, depth=g_v1)
    # (pass 4, PART 15: the walls no longer swell on the quarter -- a structure stays where it was built; the room
    # listens through its lines, lamps and screens instead)
    f.route("grid.song.bar.wave", "sdf/livShell/node/livCurtain/amount", 0.03, depth=b.gate("gCurtain", ENTRY, t(33)))
    f.route("grid.song.eighth", f"sdf/livMedia/surface/{K.SCREEN}/emission", 1.6, depth=b.gate("gTV", ENTRY, t(91)))
    f.route("grid.song.bar.phase", "sdf/livDecor/node/livClockM/rotation", -360.0, component=2)

    # ---- the words ---------------------------------------------------------------------------------------------
    for bar in range(17, 25):
        sx, sy = [(0.45, 0.5), (-0.5, 0.45), (0.5, 0.4), (-0.4, 0.5), (0.3, 0.5), (-0.3, 0.45), (0.45, 0.45), (-0.45, 0.35)][bar - 17]
        b.word_at("ALL YOU GOT", t(bar) + 0.02, t(bar, 3.5), ("box", liv["interior"]), sx, sy, k=0.06, style="flash",
                  role="accent", intensity=4.0, tin=0.2, tout=0.15, at=0.12, room="livingRoom")
    vw = [("HOW LITTLE DO I KNOW?", 55.3, t(28, 3), 0.0, 0.45, 0.06, "rise", "word"),
          ("HOW LITTLE", 57.4, t(28, 4), -0.5, 0.4, 0.05, "flicker", "accent"),
          ("DO I KNOW?", 59.5, t(28, 4), 0.45, 0.45, 0.05, "pop", "word"),
          ("BREATHE AND GROW", 61.5, t(28, 4), -0.2, 0.5, 0.045, "rise", "accent"),
          ("CAN YOU TELL ME", 68.4, t(32, 3), -0.45, 0.5, 0.045, "flicker", "word"),
          ("IT'S FINE THOUGH?", 69.4, t(32, 3), 0.4, 0.45, 0.045, "flicker", "accent"),
          ("FEEL AND GROW", 70.4, t(32, 4), 0.0, 0.55, 0.05, "rise", "accent")]
    for i, (text, t0, t1, sx, sy, kk, style, role) in enumerate(vw):
        b.word_at(text, t0, t1, ("box", liv["interior"]), sx, sy, k=kk, style=style, role=role, name=f"v1w{i:02d}",
                  room="livingRoom")
        if i in (0, 4):
            lyric_glitch(b, f"lg{i}", t0)

    # ---- C01 (20.4): a colour explosion, the whole wheel in a beat; the lamps burst -------------------------
    c = b.clap("c01", t(20, 4), release=0.9)
    f.route(c, "post/grade/hueShift", 3.1)
    f.route(c, "palette/saturation", 0.8)
    f.route(c, "palette/value", 1.6)
    f.route(c, "camera/exposure/compensation", 1.2)
    f.route(c, "lights/livLamp/intensity", 25.0)
    f.route(c, "post/bloom/intensity", 1.2)
    f.route(c, "temporal/mosh/shift", 18.0)
    # ---- C02 (24.4): the walls blow outward into the black and come back for the verse ----------------------
    c = b.clap("c02", t(24, 4), release=0.55)
    f.route(c, "palette/value", 2.2)
    f.route(c, "post/bloom/intensity", 2.2)
    f.route(c, "camera/exposure/compensation", 1.6)
    f.route(c, "post/lens/chromaticAberration", 0.7)
    f.route(c, "temporal/mosh/amount", 0.5)
    c = b.clap("v1in", t(25), release=0.6)           # the walls slam back for the verse: a white flash on the downbeat
    f.route(c, "camera/exposure/compensation", 1.6)
    f.route(c, "post/bloom/intensity", 1.2)
    for o in ("livShell", "livDecor"):
        f.track(f"sdf/{o}/transform/scale", [(0.0, [1.0, 1.0, 1.0], "step"), (t(24, 4) - 0.002, [1.0, 1.0, 1.0], "easeOut"),
                                             (t(24, 4) + 0.14, [2.4, 1.5, 2.4], "easeIn"), (t(25) - 0.002, [3.4, 1.9, 3.4], "step"),
                                             (t(25), [1.0, 1.0, 1.0], "step")])
    # ---- C03 (28.4): the side lamp flares and floods the room with light ------------------------------------
    c = b.clap("c03", t(28, 4), release=1.1, attack=BEAT1 * 0.35)
    f.route(c, "camera/exposure/compensation", 2.4)
    f.route(c, "lights/livSideLamp/intensity", 40.0)
    f.route(c, "post/bloom/intensity", 1.8)
    f.route(c, "temporal/mosh/amount", 0.3)
    # ---- C04 (32.4): the palette turn, and the camera goes through to the kitchen ------------------------------
    c = b.clap("c04", t(32, 4), release=0.8)
    f.route(c, "post/bloom/intensity", 0.9)
    f.route(c, "camera/exposure/compensation", 2.0)
    f.route(c, "palette/value", 1.8)
    for o in ("livShell", "livSofa", "livMedia", "livShelf", "livDecor", "kitShell", "kitCounter", "kitTable"):
        f.route(c, f"sdf/{o}/look/edge/intensity", 8.0)
    f.route(c, "lights/livLamp/intensity", 12.0)
    f.route(c, "temporal/mosh/amount", 0.4)
    f.route(c, "temporal/mosh/shift", 12.0)

    # =========================================================================================================
    # THE KITCHEN (33.1-38.1): dinner. The camera comes in looking left (the fridge, the window), pans right along
    # the counter and arrives on him at the stove ON the clap (34.4, 76.51 s); the steam puffs up into the hood's
    # light. Then the table set for one; C05 (36.4) lifts its plate, the kettle and the empty chair; out to the hall.
    # =========================================================================================================
    CLAP = t(34, 4)
    eye = [(t(33), (-1.75, 1.5, -2.3)), (74.4, (-1.4, 1.52, -3.15)), (75.7, (-0.95, 1.55, -3.65)), (CLAP, (-0.8, 1.55, -3.78)),
           (79.4, (-0.45, 1.5, -4.05)), (80.7, (0.15, 1.48, -3.55)), (82.2, (0.95, 1.48, -3.65)), (83.1, (1.85, 1.47, -4.35)),
           (t(38), (2.45, 1.46, -4.6))]
    look = [(t(33), (-2.45, 1.0, -4.7)), (74.0, (-2.35, 1.0, -6.3)), (75.35, (-0.95, 0.98, -6.4)), (CLAP, (1.58, 1.08, -5.8)),
            (79.2, (1.55, 1.12, -5.9)), (80.6, (0.0, 0.85, -4.6)), (t(36, 4) + 0.6, (0.1, 1.0, -4.6)), (82.4, (1.6, 1.2, -5.0)),
            (t(38), (3.7, 1.4, -4.6))]
    b.glide("kit", eye, look, nodes_keys=ground, fov=62.0)
    s4 = b.swap("livWindow", "kitStove", t(32, 4) + 0.3)
    b.show("livWindow", s3, s4)
    b.show("kitStove", s4, t(38))
    kitchen_words(b, kit)
    g_k = b.gate("gV1k", t(33), t(38))
    f.route("grid.song.quarter", "lights/kitPend/intensity", 3.0, depth=g_k)
    for o in ("kitCounter", "kitTable", "kitRange"):
        b.pulse(f"sdf/{o}/look/edge/intensity", 2.0, "quarter", g_k)
    b.pulse("sdf/kitShell/look/edge/intensity", 2.5, "bar", g_k)
    f.route("grid.song.bar.phase", "sdf/kitShell/node/kitClockM/rotation", -360.0, component=2)
    # the steam: always rising from the pot into the hood's light (PART 15: atmosphere may keep moving); the burner
    # glows; on the clap a puff of steam, the burner flares, the hood light comes up -- no flash, no corruption
    pot = kit["anchors"]["pot"]
    f.nodes.append({"name": "kitSteam", "kind": "particles", "particles": {
        "capacity": 1500, "spawnRate": 55.0, "burst": 0.0, "shape": "box", "position": [pot[0], pot[1] + 0.14, pot[2]],
        "extent": [0.09, 0.02, 0.09], "lifetimeMin": 1.6, "lifetimeMax": 2.6, "direction": [0.0, 1.0, 0.0], "spread": 0.22,
        "speedMin": 0.18, "speedMax": 0.42, "gravity": [0.0, 0.12, 0.0], "drag": 0.5, "sizeStart": 0.035, "sizeEnd": 0.2,
        "blend": "additive", "colorStart": [0.85, 0.85, 0.9, 0.22], "colorEnd": [0.7, 0.7, 0.8, 0.0], "emissive": 0.6}})
    f.shots[-1]["nodes"].append("kitSteam")
    c = b.clap("kitClap", CLAP, release=0.9)
    cb = b.clap("kitPuff", CLAP, release=0.05)
    f.route(cb, "particles/kitSteam/burst", 260.0)
    f.route(c, f"sdf/kitRange/surface/{K.GLOW}/emission", 4.0)
    f.route(c, "lights/kitHood/intensity", 6.0)
    f.route(c, "sdf/kitRange/look/edge/intensity", 5.0)
    f.track("lights/kitHood/intensity", [(0.0, 0.6), (CLAP - 0.02, 0.6, "step"), (CLAP, 2.4, "smooth"), (t(38), 2.4, "step")])
    # C05 (36.4): levitation -- the plate and cup, the kettle and the empty chair jump and hang, then drift down
    c = b.clap("c05", t(36, 4), release=0.4)
    f.route(c, "post/bloom/intensity", 1.0)
    f.route(c, "camera/exposure/compensation", 1.0)
    f.route(c, "temporal/mosh/amount", 0.35)
    for node, obj, h in (("kitPlate", "kitTable", 0.55), ("kitChairA", "kitTable", 0.6), ("kitKettle", "kitCounter", 0.7)):
        base = _node_translation(kit, obj, node)
        upk = [base[0], base[1] + h, base[2]]
        f.track(f"sdf/{obj}/node/{node}/translation", [(0.0, base, "step"), (t(36, 4) - 0.001, base, "easeOut"),
                                                       (t(36, 4) + 0.12, upk, "step"), (t(37, 3), upk, "easeIn"),
                                                       (t(37, 4), base, "step")])

    # =========================================================================================================
    # THE HALL AND THE CLIMB (38.1-41.1), then over the edge (41)
    # =========================================================================================================
    s = R3.STAIR
    top_z = s["foot_z"] + s["steps"] * s["run"]

    def climb_y(z):      # the eye 1.5 m above the treads (1.52 at the foot)
        return 1.52 + max(0.0, min(z - (-4.95), 4.5)) / 4.5 * 2.68
    zs = {86.4: -4.95, 87.75: -3.64, 89.6: -1.95, 90.25: top_z - 0.08}
    eye = [(t(38), (2.45, 1.46, -4.6)), (84.6, (3.3, 1.5, -4.92)), (85.6, (4.1, 1.5, -5.25))] + \
          [(tt, (4.42, climb_y(z), z)) for tt, z in sorted(zs.items())] + \
          [(90.6, (4.42, 4.15, 0.25)), (91.0, (4.42, 3.6, 1.4)), (91.6, (4.42, 3.05, 2.6)), (t(42), (4.42, 1.0, 3.6))]

    def ahead(tt, z):    # looking 15 degrees down, as a climber watches the next steps (they fill the lower frame)
        return (tt, (4.42, climb_y(z) - 3.0 * 0.268, z + 3.0))
    look = [(t(38), (3.7, 1.4, -4.6)), (84.4, (4.3, 1.15, -3.3)), (85.4, (4.4, 1.4, -3.0)), (86.0, (4.42, 1.7, -2.6)),
            ahead(86.4, -4.95), ahead(87.75, -3.64), ahead(89.6, -1.95), (89.85, (4.42, climb_y(-1.4) - 1.1, 1.6)), (90.05, (4.42, 3.5, 2.0)),
            (t(41), (4.42, 1.6, 2.2)), (90.75, (4.42, 0.4, 2.4)), (t(42), (4.42, -3.0, 4.6))]
    b.glide("hall", eye, look, nodes_keys=("hall",), fov=64.0)
    b.show("hallMan", t(38), t(41))
    # IT'S STEPS IN A PROCESS, LET IT GO: one word a riser, risers 8-15, each 1.1 m ahead of the camera as it lands
    words8 = ["IT'S", "STEPS", "IN", "A", "PROCESS,", "LET", "IT", "GO"]
    for i, wd in enumerate(words8):
        tw = 87.5 + i * 0.3
        pos, nrm, h = R3.riser(8 + i)
        b.word(wd, tw, t(41, 3), pos, nrm, min(0.11, h * 0.62), style="pop", role="word", room="hall", category="stairText",
               intensity=4.5, name=f"steps{i}")
    b.word_at("I KNOW, GET A LITTLE PEACE OF MIND THOUGH", 85.1, 87.6, ("box", hall["interior"]), 0.1, 0.45, k=0.028,
              style="rise", role="accent", room="hall")
    lyric_glitch(b, "lgh0", 85.1)
    g_h = b.gate("gV1h", t(38), t(40, 4))
    f.route("grid.song.quarter", "lights/hallLamp/intensity", 2.5, depth=g_h)
    f.route("grid.song.quarter", "lights/hallLamp2/intensity", 2.5, depth=g_h)
    b.pulse("sdf/hallStair/look/edge/intensity", 3.0, "quarter", b.gate("gStair", t(38), t(41)))
    # C06 (40.4): the house flares white and dissolves; only the stair's top and its words remain
    c = b.clap("c06", t(40, 4), release=0.55)
    f.route(c, "post/bloom/intensity", 2.0)
    f.route(c, "camera/exposure/compensation", 1.4)
    f.route(c, "temporal/mosh/amount", 0.45)
    # (every one of these comes back at 42.1: the hall is used again for the dance)
    for o in ("hallShell", "hallFurn"):
        f.track(f"sdf/{o}/look/edge/intensity", [(0.0, 1.0, "step"), (t(40, 4) - 0.001, 1.0, "step"), (t(40, 4) + 0.05, 4.0, "smooth"),
                                                 (t(41) - 0.05, 0.0, "step"), (t(42), 1.0, "step")], mode="multiply")
        b.key(f"nodes/{o}/visible", 0.0, 1.0)
        b.key(f"nodes/{o}/visible", t(41) - 0.02, 0.0)
        b.key(f"nodes/{o}/visible", t(42), 1.0)
    for lamp in ("hallLamp", "hallLamp2"):
        f.track(f"lights/{lamp}/intensity", [(0.0, 2.5), (t(40, 4), 2.5, "smooth"), (t(41) - 0.05, 0.0, "step"), (t(42), 2.5, "step")])
    f.track("sdf/hallStair/look/edge/intensity", [(0.0, 1.0, "step"), (t(41, 2), 1.0, "smooth"), (t(41, 3.5), 0.0, "step"),
                                                  (t(42), 1.0, "step")], mode="multiply")

    # ---- 41: over the edge. LET IT GO falls with us; the second, hot pink, stops dead on 41.4 --------------
    for i, (wd, beat, sx, sy, dist) in enumerate((("LET", 1.0, -0.35, 0.1, 3.0), ("IT", 1.5, 0.08, -0.15, 3.4), ("GO", 2.0, 0.4, 0.05, 3.8))):
        e = b.word_at(wd, t(41, beat), t(41, 3.0), ("view", dist), sx, sy, k=0.12, style="cut", role="word", name=f"pauseA{i}",
                      tilt=[-8, 4, 10][i], intensity=4.0, category="floatingText")
        p0 = list(e["position"])
        f.track(f"nodes/pauseA{i}/position", [(t(41, beat), p0, "smooth"), (t(41, 3.0), [p0[0], p0[1] - 3.2, p0[2] + 0.6], "step")])
    for i, (wd, beat, sx, sy) in enumerate((("LET", 3.0, -0.4, 0.0), ("IT", 3.5, 0.0, 0.0), ("GO", 4.0, 0.4, 0.0))):
        b.word_at(wd, t(41, beat), t(42), ("view", 3.2), sx, sy, k=0.11, style="cut", role="accent", name=f"pauseB{i}",
                  intensity=5.0, category="floatingText")
    return house


def _node_translation(room, obj_name, node_name):
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


def kitchen_words(b, kit):
    """The verse's lines in the kitchen, one at a time where they can be read (pass 3 stacked three on one wall at
    77 s): each leaves as the next arrives, and none is placed while he is being revealed at the stove."""
    kw = [("COME ON, TELL ME", 72.9, 74.6, -0.35, 0.5, 0.05, "pop", "word"),
          ("WHAT YOU WANNA", 74.7, 76.1, 0.35, 0.45, 0.05, "flicker", "accent"),
          ("MAYBE CAUSE A LITTLE DRAMA", 77.2, 78.4, -0.45, 0.6, 0.032, "pop", "word"),
          ("IF YOU FEEL IT, SAY IT", 78.5, 79.6, -0.45, 0.55, 0.04, "rise", "accent"),
          ("LET IT SHOW", 79.7, 81.0, 0.35, 0.4, 0.06, "rise", "word"),
          ("CAN YOU TELL ME IT'S FINE THOUGH?", 81.1, 83.5, 0.0, 0.5, 0.03, "flicker", "word")]
    for i, (text, t0, t1, sx, sy, kk, style, role) in enumerate(kw):
        b.word_at(text, t0, t1, ("box", kit["interior"]), sx, sy, k=kk, style=style, role=role, name=f"v1k{i:02d}", room="kitchen")
        if i in (0, 5):
            lyric_glitch(b, f"lgk{i}", t0)
