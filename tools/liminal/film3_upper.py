"""All You Got, art pass 3: below and above. Owner bars 42-66 (92.48-147.52 s), on Builder3.

  42-49  LET IT GO in the basement. We fell off the top of the stair; on 42.1, when the kick comes back, we land in
         the laundry, and the camera walks on through the storage room into the boiler room, where the house keeps
         what it cannot let go of. LET, IT and GO land on its surfaces twice a bar, on their eighths; C07 throws every
         word in the storage room off its wall; 48-49 the room drains to outline and the hats flicker faster; C08
         whips up and out.
  50-65  verse 2, upstairs, the house breaking: the bedroom (in bed, a forearm over his eyes; then sitting on its edge),
         the bathroom (at the mirror, his hands on the sink; then the toilet, deadpan), the study (at the desk in the
         monitor's static; then on the floor against the wall, knees up). Each BIG CLAP corrupts the world in its own
         way: data-mosh, a positional dislocation, colour, a stretch, the walls tearing, a lurch, a floor drop, the
         roof lifting.
  66     FEEL IT GROW: the study's ceiling splits and flies into the sky.
"""

from __future__ import annotations

import kit as K
import pass2_grid as G
import rooms3 as R3
from film3_house import lyric_glitch

t = G.t
BEAT1 = G.BAR1 / 4.0


def build(b):
    f = b.f
    end = f.end
    low, up = R3.basement(), R3.upstairs()
    for key, room in {**low, **up}.items():
        b.room_world(key, room)
    lau, sto, boi = low["lau"], low["sto"], low["boi"]
    bed, bath, stu = up["bed"], up["bath"], up["stu"]

    # =========================================================================================================
    # LET IT GO (42-49): the basement
    # =========================================================================================================
    eye = [(t(42), (-1.15, -0.55, 1.35)), (t(42) + 0.42, (-1.0, -1.35, 1.0)), (95.0, (-0.6, -1.4, 0.55)),
           (t(44), (0.2, -1.42, 0.05)), (98.4, (2.0, -1.42, 0.0)), (t(45, 4), (3.5, -1.42, 0.5)), (t(46), (3.4, -1.42, -0.6)),
           (102.6, (3.0, -1.42, -2.3)), (104.1, (2.4, -1.42, -3.5)), (t(48), (0.9, -1.42, -4.15)), (t(49, 4), (0.1, -1.45, -4.6))]
    look = [(t(42), (-1.6, -2.6, -0.4)), (t(42) + 0.42, (-2.0, -1.9, -0.6)), (95.0, (-1.6, -1.8, -2.2)),
            (t(44), (1.2, -1.6, 0.0)), (98.4, (4.6, -1.7, 0.8)), (t(45, 4), (2.2, -1.6, 2.2)), (t(46), (3.0, -1.6, -2.3)),
            (102.6, (2.0, -1.6, -5.0)), (104.1, (-0.5, -1.7, -6.2)), (t(48), (-0.6, -1.8, -6.4)), (t(49, 4), (-0.5, -1.9, -6.4))]
    b.glide("lig", eye, look, nodes_keys=("lau", "sto", "boi"), fov=64.0)
    f.shots[-1]["fov"] = [(t(42), 64.0), (t(49, 3.5), 58.0), (t(49, 4) - 0.02, 40.0)]
    c = b.clap("landing", t(42), release=0.5)
    f.route(c, "camera/exposure/compensation", 1.4)
    f.route(c, "post/bloom/intensity", 1.2)
    f.route(c, "temporal/mosh/amount", 0.4)
    # LET, IT and GO twice a bar, on their eighths, laid on whatever the camera is looking at
    layouts = [[(-0.55, 0.38, 0.09, -4), (0.05, -0.42, 0.05, 8), (0.58, 0.12, 0.11, 0)],
               [(0.5, 0.45, 0.07, 6), (-0.5, -0.2, 0.06, 0), (0.1, 0.05, 0.13, -8)],
               [(-0.3, 0.6, 0.08, 0), (-0.3, 0.3, 0.08, 0), (-0.3, -0.05, 0.11, 0)],
               [(0.62, -0.35, 0.1, 12), (0.0, 0.45, 0.05, 0), (-0.6, -0.3, 0.12, -10)]]
    rooms_at = [("lau", lau), ("lau", lau), ("lau", lau), ("lau", lau), ("sto", sto), ("sto", sto), ("sto", sto), ("sto", sto),
                ("boi", boi), ("boi", boi), ("boi", boi), ("boi", boi)]
    k = 0
    storage_words = []
    for half in range(12):
        bar = 42 + half // 2
        b0 = 1.0 if half % 2 == 0 else 3.0
        lay = layouts[half % 4]
        key, room = rooms_at[half]
        for j, wd in enumerate(("LET", "IT", "GO")):
            sx, sy, kk, tilt = lay[j]
            tw = t(bar, b0 + 0.5 * j)
            t1 = t(bar, b0 + 2.0) if half % 2 == 0 else t(bar + 1, 1.0)
            name = f"lig{k:02d}"
            b.word_at(wd, tw, t1, ("box", room["interior"]), sx, sy, k=kk, tilt=tilt, style=["flash", "pop", "flash"][j],
                      role="word" if j != 1 else "accent", name=name, intensity=4.2, tin=0.08, tout=0.18, room=room["id"])
            if key == "sto":
                storage_words.append(name)
            k += 1
    g_lig = b.gate("gLig", t(42), t(48))
    for o in ("lauShell", "stoShell", "boiShell"):
        b.pulse(f"sdf/{o}/look/edge/intensity", 3.5, "quarter", g_lig)
    for o in ("lauFurn", "stoFurn", "boiFurn"):
        b.pulse(f"sdf/{o}/look/edge/intensity", 2.0, "eighth", g_lig)
    for bulb in ("lauBulb", "stoBulb", "boiBulb"):
        f.route("grid.song.quarter", f"lights/{bulb}/intensity", 3.0, depth=g_lig)
        f.route("grid.song.quarter.wave", f"sdf/{bulb[:3]}Shell/transform/scale", 0.005, depth=g_lig)
    # C07 (45.4): every word in the storage room flashes and flies off its wall
    c = b.clap("c07", t(45, 4), release=0.5)
    f.route(c, "post/bloom/intensity", 1.4)
    f.route(c, "camera/exposure/compensation", 1.2)
    f.route(c, "post/grade/hueShift", -1.6)
    f.route(c, "temporal/mosh/amount", 0.5)
    f.route(c, "temporal/mosh/shift", 14.0)
    for name in storage_words:
        f.route(c, f"nodes/{name}/emissiveBoost", 14.0)
        f.route(c, f"nodes/{name}/scale", 1.4)
    # the rebuild (48-49): the boiler room drains to outline; the hats flicker faster into C08
    g_rb = b.gate("gRebuild", t(48), t(49, 4))
    for o in ("boiShell", "boiFurn"):
        b.pulse(f"sdf/{o}/look/edge/intensity", 3.0, "sixteenth", g_rb)
    b.val += [(t(48), 1.0, "smooth"), (t(48, 2), 0.55, "smooth"), (t(49, 3.9), 0.55, "step"), (t(49, 4), 1.0, "step")]
    # C08 (49.4): a whip-zoom, a one-frame negative, and up into verse 2
    c = b.clap("c08", t(49, 4), release=0.35, attack=0.12)
    f.route(c, "post/lens/distortion", 0.7)
    f.route(c, "post/lens/chromaticAberration", 1.0)
    f.route(c, "camera/exposure/compensation", 1.5)
    f.route(c, "temporal/mosh/amount", 0.6)
    f.track("post/grade/hueShift", [(0.0, 0.0), (t(49, 4) - 0.002, 0.0, "step"), (t(49, 4), 3.1, "step"), (t(49, 4) + 0.04, 0.0, "step")],
            mode="add")

    # =========================================================================================================
    # VERSE 2 (50-65): upstairs
    # =========================================================================================================
    v2a = t(49, 4)
    # ---- the bedroom: in bed; then on its edge ---------------------------------------------------------------
    eye = [(v2a, (0.85, 4.45, 1.55)), (112.5, (0.6, 4.42, 1.2)), (t(51, 4), (0.25, 4.4, 0.4)), (115.6, (0.2, 4.38, -1.4)),
           (t(53, 4), (-0.6, 4.4, 1.7))]
    look = [(v2a, (-1.55, 3.5, 0.0)), (112.5, (-1.5, 3.55, 0.0)), (t(51, 4), (0.3, 3.9, -2.2)), (115.0, (1.2, 3.9, -1.4)),
            (116.8, (-1.6, 3.6, 0.5)), (t(53, 4), (-1.7, 3.55, 0.55))]
    b.glide("v2bed", eye, look, nodes_keys=("bed",), fov=62.0)
    s1 = b.swap("bedLie", "bedEdge", 115.2)
    b.show("bedLie", v2a, s1)
    # ---- the bathroom: at the mirror; then the toilet ----------------------------------------------------------
    v2b = t(53, 4)
    eye = [(v2b, (2.05, 4.4, -0.25)), (121.0, (2.3, 4.42, 0.15)), (t(55, 4), (2.4, 4.4, 0.6)), (124.4, (2.6, 4.45, 0.2)),
           (t(57, 4), (2.9, 4.4, -0.35))]
    look = [(v2b, (4.3, 3.95, 0.75)), (121.0, (4.35, 3.95, 0.75)), (t(55, 4), (4.6, 4.3, 0.75)), (123.5, (1.4, 3.8, 0.7)),
            (124.0, (1.4, 3.8, 0.9)), (125.0, (3.0, 3.6, 1.8)), (t(57, 4), (3.0, 3.55, 1.85))]
    b.glide("v2bath", eye, look, nodes_keys=("bath",), fov=64.0)
    s2 = b.swap("bathMirror", "bathToilet", 123.75)
    b.show("bedEdge", s1, v2b)
    b.show("bathMirror", v2b, s2)
    # ---- the study: at the desk in the monitor's static; then on the floor against the wall ------------------
    v2c = t(57, 4)
    eye = [(v2c, (0.65, 4.45, -3.0)), (130.0, (0.2, 4.4, -4.2)), (t(59, 4), (-0.2, 4.35, -4.7)), (134.0, (0.6, 4.4, -4.9)),
           (t(61, 4), (0.75, 4.42, -3.9)), (138.6, (0.5, 4.4, -2.9)), (t(63, 4), (0.2, 4.38, -3.4)), (t(65, 4), (-0.2, 4.3, -3.9)),
           (t(66, 3), (-0.45, 4.15, -4.3)), (t(67), (-0.6, 4.6, -4.4))]
    look = [(v2c, (-1.15, 3.95, -5.6)), (130.0, (-1.1, 3.95, -5.75)), (t(59, 4), (-1.2, 3.95, -5.9)), (134.0, (-1.35, 3.9, -5.6)),
            (135.3, (1.2, 4.0, -3.4)), (t(61, 4), (1.2, 4.1, -2.6)), (138.0, (0.3, 4.1, -2.4)), (139.3, (-2.2, 3.4, -4.2)),
            (t(63, 4), (-2.2, 3.4, -4.2)), (t(65, 4), (-2.1, 3.5, -4.2)), (t(66), (-1.2, 4.8, -4.4)), (t(66, 3), (-0.8, 7.0, -4.4)),
            (t(67), (-0.6, 12.0, -4.4))]
    b.glide("v2stu", eye, look, nodes_keys=("stu",), extra=["stars"], fov=62.0)
    f.shots[-1]["fov"] = [(v2c, 62.0), (t(66, 3), 66.0), (t(67), 76.0)]
    s3 = b.swap("stuDesk", "stuFloor", 137.4)
    b.show("bathToilet", s2, v2c)
    b.show("stuDesk", v2c, s3)
    b.show("stuFloor", s3, t(67))

    g_v2 = b.gate("gV2", v2a, t(66))
    for o in ("bedShell", "bathShell", "stuShell"):
        b.pulse(f"sdf/{o}/look/edge/intensity", 3.0, "half", g_v2)
    for o in ("bedFurn", "bathFurn", "stuFurn", "stuShelf"):
        b.pulse(f"sdf/{o}/look/edge/intensity", 3.5, "quarter", g_v2)
        b.pulse(f"sdf/{o}/look/edge/pixels", 1.0, "quarter", g_v2)
    for lamp in ("bedLamp", "bedPend", "bathLamp", "stuLamp"):
        f.route("grid.song.quarter", f"lights/{lamp}/intensity", 2.5, depth=g_v2)
    for o in ("bedShell", "bathShell", "stuShell"):
        f.route("grid.song.quarter.wave", f"sdf/{o}/transform/scale", 0.006, depth=g_v2)
    f.route("grid.song.bar.wave", "sdf/bedShell/node/bedCurtain/amount", 0.04, depth=g_v2)
    f.track("sdf/stuCeil/node/stuFan/rotation", [(0.0, [0.0, 0.0, 0.0], "linear"), (end, [0.0, 300.0 * end, 0.0])])
    f.track("sdf/stuFurn/node/stuGlobe/rotation", [(0.0, [0.0, 0.0, 0.0], "linear"), (113.1, [0.0, 2600.0, 0.0], "step"),
                                                  (t(51, 4), [0.0, 2600.0, 0.0], "linear"), (t(54), [0.0, 4400.0, 0.0], "linear"),
                                                  (end, [0.0, 4400.0 + 260.0 * (end - t(54)), 0.0])])
    f.route("grid.song.eighth", f"sdf/stuFurn/surface/{K.SCREEN}/emission", 2.0, depth=b.gate("gMon", v2c, t(67)))
    f.track("temporal/mosh/seed", [(t(50) + 0.27 * i, float(i), "step") for i in range(0, 130)])
    g_strain = b.gate("gStrain", t(58), t(66))
    f.route("grid.song.sixteenth", "temporal/mosh/shift", 4.0, depth=g_strain)

    # the words of verse 2: more of them, smaller, arriving on the eighths; (text, time, sx, sy, size, style, role)
    vw = [("DO YOU WANNA HAVE FUN?", 110.2, -0.1, 0.55, 0.045, "flicker", "word"),
          ("AS THE FIRES KEEP BURNING", 111.4, -0.5, 0.4, 0.04, "pop", "accent"),
          ("THE WORLD STOPS TURNING", 113.1, 0.35, 0.45, 0.03, "cut", "word"),
          ("AND EVERYONE", 114.5, -0.45, 0.5, 0.05, "pop", "word"),
          ("UNDER THE SUN", 115.1, 0.5, 0.5, 0.05, "pop", "accent"),
          ("TAKES STEPS IN THE PROCESS", 115.9, -0.2, 0.55, 0.03, "flicker", "word"),
          ("TO HEAL AND GROW", 117.0, 0.3, 0.45, 0.04, "flicker", "accent"),
          ("TELL ME YOU'RE THE ONE", 118.7, 0.45, 0.4, 0.04, "rise", "word"),
          ("TO MAKE THE WORLD", 120.3, -0.35, 0.55, 0.045, "pop", "word"),
          ("STOP HURTING", 121.1, -0.35, 0.38, 0.06, "flash", "accent"),
          ("AND MY HEART KEEP PUMPING", 122.0, -0.2, 0.55, 0.032, "pop", "word"),
          ("WHILE EVERYONE UNDER THE SUN", 123.7, -0.2, 0.55, 0.03, "pop", "word"),
          ("TAKE STEPS", 125.3, -0.5, 0.3, 0.06, "rise", "accent"),
          ("TO FEEL IT GROW", 126.2, 0.45, 0.4, 0.045, "rise", "word"),
          ("HOW LITTLE DO I KNOW?", 127.5, 0.0, 0.55, 0.045, "flash", "word"),
          ("HOW LITTLE DO I KNOW?", 128.6, 0.5, 0.25, 0.035, "flash", "accent"),
          ("HOW LITTLE DO I KNOW?", 129.7, -0.5, 0.3, 0.04, "flash", "word"),
          ("HOW LITTLE DO I KNOW?", 130.8, 0.3, -0.3, 0.045, "flash", "accent"),
          ("HOW LITTLE DO I KNOW?", 132.0, -0.2, 0.55, 0.045, "flicker", "word"),
          ("HOW LITTLE DO I KNOW?", 133.6, 0.5, 0.0, 0.035, "flicker", "accent"),
          ("BREATHE AND GROW", 136.0, 0.0, 0.5, 0.055, "rise", "word"),
          ("HOW LITTLE DO I KNOW?", 138.2, -0.45, 0.3, 0.035, "flash", "accent"),
          ("TELL ME IT'LL BE FINE THOUGH?", 140.6, 0.35, 0.5, 0.032, "flicker", "word"),
          ("FEEL IT GROW", 143.3, 0.0, 0.45, 0.06, "rise", "word")]
    room_of = {"v2bed": bed, "v2bath": bath, "v2stu": stu}
    line_starts = {110.2, 113.1, 118.7, 122.0, 125.3, 127.5, 136.0, 140.6}
    for i, (text, t0, sx, sy, kk, style, role) in enumerate(vw):
        nxt = vw[i + 1][1] if i + 1 < len(vw) else t(66)
        t1 = max(min(t0 + 3.2, nxt + 1.2), t0 + 1.0)
        shot = f.camera_at(t0 + 0.12)[3]
        t1 = min(t1, next(s_["t1"] for s_ in f.shots if s_["name"] == shot) - 0.02)
        room = room_of[shot]
        b.word_at(text, t0, t1, ("box", room["interior"]), sx, sy, k=kk, style=style, role=role, name=f"v2w{i:02d}",
                  tin=0.1, tout=0.15, room=room["id"])
        if t0 in line_starts:
            lyric_glitch(b, f"lg2_{i}", t0, amount=0.35, shift=10.0)

    # ---- the eight BIG CLAPs: each corrupts the simulation its own way ---------------------------------------
    # C09 (51.4) data-mosh: three quarters of the frame freezes and smears, the channels split, then it snaps back
    c = b.clap("c09", t(51, 4), release=0.9, hold=BEAT1 * 0.5)
    f.route(c, "temporal/mosh/amount", 0.75)
    f.route(c, "palette/value", 1.7)
    f.route(c, "post/bloom/intensity", 1.2)
    for o in ("bedShell", "bedFurn"):
        f.route(c, f"sdf/{o}/look/edge/intensity", 9.0)
    f.route(c, "temporal/mosh/shift", 14.0)
    f.route(c, "post/lens/chromaticAberration", 0.6)
    # C10 (53.4) positional corruption: the bedroom's parts jump sideways by their own amounts (the cut lands
    # inside it)
    c = b.clap("c10", t(53, 4) - 0.12, release=0.12, attack=0.03)
    for o, off in (("bedShell", 0.18), ("bedFurn", -0.4)):
        f.route(c, f"sdf/{o}/transform/position", off, component=0, attackMs=5.0)
    f.route(c, "post/lens/chromaticAberration", 0.7)
    f.route(c, "temporal/mosh/amount", 0.5)
    # ... and on the beat the corruption carries across the cut (the jump alone was over before the beat it answers:
    # take 2 measured C10 only "visible"): the bathroom arrives with its layers misregistered, walls one way, the
    # vanity and the figure at it the other, and snaps together over four frames while the pixels smear
    c = b.clap("c10in", t(53, 4), release=0.35)
    f.route(c, "temporal/mosh/amount", 0.6)
    f.route(c, "temporal/mosh/shift", 14.0)
    f.route(c, "post/lens/chromaticAberration", 0.6)
    f.route(c, "post/bloom/intensity", 0.8)
    c = b.clap("c10echo", t(53, 4), release=0.14)
    for o, off in (("bathShell", 0.14), ("bathFurn", -0.3), ("bathMirror", -0.3)):    # z: across this view
        f.route(c, f"sdf/{o}/transform/position", off, component=2, attackMs=5.0)
    f.route(c, "camera/exposure/compensation", 1.8)      # the flash on the beat, not before it
    # C11 (55.4) colour corruption: a 180-degree hue jump, the channels split, the lamp pumps like a heart
    c = b.clap("c11", t(55, 4), release=0.55)
    f.route(c, "post/grade/hueShift", 3.1)
    f.route(c, "post/lens/chromaticAberration", 1.0)
    f.route(c, "lights/bathLamp/intensity", 30.0)
    f.route(c, "post/bloom/intensity", 1.2)
    f.route(c, "temporal/mosh/shift", 22.0)
    # C12 (57.4) stretch: the bathroom stretches to twice its height and the cut snaps us into the study
    c = b.clap("c12", t(57, 4) - 0.2, release=0.2, attack=0.15)
    for o in ("bathShell", "bathFurn"):
        f.route(c, f"sdf/{o}/transform/scale", 1.0, component=1)
    f.route(c, "temporal/mosh/amount", 0.45)
    f.route(c, "post/bloom/intensity", 1.6)
    f.route(c, "palette/value", 1.6)
    c = b.clap("c12in", t(57, 4), release=0.3)      # and the study comes in white-hot
    f.route(c, "camera/exposure/compensation", 2.2)
    for o in ("stuShell", "stuFurn", "stuShelf"):
        f.route(c, f"sdf/{o}/look/edge/intensity", 10.0)
    # C13 (59.4) the walls tear: the study's walls ripple (a displacement), the frame splits
    c = b.clap("c13", t(59, 4), release=0.7)
    f.route(c, "sdf/stuShell/node/stuWarp/amount", 0.16)
    f.route(c, "post/lens/chromaticAberration", 0.8)
    f.route(c, "temporal/mosh/amount", 0.35)
    f.route(c, "camera/exposure/compensation", 1.6)
    for o in ("stuShell", "stuFurn", "stuShelf"):
        f.route(c, f"sdf/{o}/look/edge/intensity", 8.0)
    # C14 (61.4) a lurch: the whole room jumps a hand's width sideways for two frames, twice, in a flash. The camera is
    # looking away across the dark side of the study here (the desk figure becomes the floor figure behind it), so a
    # dropout of an already black frame read as nothing (take 2: "not read"): the room's own light stutters instead,
    # a cold flash from beside the camera that lights the walls the lurch moves
    flash = f.point_light("stuFlash", (0.2, R3.UP + 1.75, -3.3), (0.85, 0.92, 1.0), 0.0, 3.5)
    c = b.clap("c14", t(61, 4), release=0.08, attack=0.0)
    for o in ("stuShell", "stuFurn", "stuShelf", "stuCeil", "stuDesk", "stuFloor"):
        f.route(c, f"sdf/{o}/transform/position", 0.22, component=0, attackMs=5.0)
    c = b.clap("c14f", t(61, 4), release=0.3)
    f.route(c, f"lights/{flash}/intensity", 16.0)
    f.route(c, "post/bloom/intensity", 1.0)
    c = b.clap("c14b", t(61, 4) + 0.14, release=0.06)
    for o in ("stuShell", "stuFurn", "stuShelf", "stuCeil", "stuDesk", "stuFloor"):
        f.route(c, f"sdf/{o}/transform/position", -0.16, component=0, attackMs=5.0)
    f.route(c, "temporal/mosh/amount", 0.5)
    c = b.clap("c14c", t(61, 4) + 0.07, release=0.25)   # between the two jumps, the lines flare
    f.route(c, "camera/exposure/compensation", 1.8)
    for o in ("stuShell", "stuFurn", "stuShelf", "stuCeil"):
        f.route(c, f"sdf/{o}/look/edge/intensity", 9.0)
    # C15 (63.4) the floor drops: the room falls a metre under the camera and every line flares white
    c = b.clap("c15", t(63, 4), release=0.6)
    f.route(c, "palette/saturation", -0.9)
    f.route(c, "post/bloom/intensity", 1.6)
    f.route(c, "camera/exposure/compensation", 1.4)
    for o in ("stuShell", "stuFurn", "stuShelf"):
        f.route(c, f"sdf/{o}/look/edge/intensity", 10.0)
    for o in ("stuShell", "stuFurn", "stuShelf", "stuFloor", "stuCeil"):
        f.track(f"sdf/{o}/transform/position", [(t(63, 4) - 0.002, [0.0, 0.0, 0.0], "easeIn"), (t(64) - 0.05, [0.0, -1.0, 0.0], "easeOut"),
                                                (t(64, 3), [0.0, 0.0, 0.0], "step")], mode="add")
    # C16 (65.4) lift-off; the bridge transition (66): the ceiling's halves split (66.3) and fly (66.4)
    c = b.clap("c16", t(65, 4), release=0.6)
    f.route(c, "post/bloom/intensity", 1.2)
    f.route(c, "camera/exposure/compensation", 1.0)
    f.route(c, "temporal/mosh/amount", 0.4)
    for half, sx in (("stuCeilL", -1.0), ("stuCeilR", 1.0)):
        f.track(f"sdf/stuCeil/node/{half}/translation", [(0.0, [0.0, 0.0, 0.0], "step"), (t(65, 4), [0.0, 0.0, 0.0], "easeOut"),
                                                        (t(65, 4) + 0.3, [0.0, 0.35, 0.0], "smooth"), (t(66, 3), [0.0, 0.35, 0.0], "easeOut"),
                                                        (t(66, 3) + 0.25, [1.6 * sx, 0.9, 0.0], "smooth"), (t(66, 4), [1.8 * sx, 1.1, 0.0], "easeIn"),
                                                        (t(67), [9.0 * sx, 30.0, 0.0], "step")])
    for fill_t in (t(66, 3), t(66, 4)):
        c = b.clap(f"fill{int(fill_t * 100)}", fill_t, release=0.35)
        f.route(c, "camera/exposure/compensation", 1.2)
        f.route(c, "post/bloom/intensity", 0.9)
        f.route(c, "temporal/mosh/shift", 12.0)
    for i, (wd, beat, sx, sy, kk) in enumerate((("FEEL", 1.0, -0.5, 0.35, 0.09), ("IT", 1.5, 0.0, 0.1, 0.07),
                                                ("GROW", 2.0, 0.5, 0.3, 0.11), ("FEEL", 3.0, -0.45, -0.1, 0.09),
                                                ("IT", 3.5, 0.05, 0.25, 0.07), ("GROW", 4.0, 0.45, 0.55, 0.12))):
        b.word_at(wd, t(66, beat), t(67), ("box", stu["interior"]), sx, sy, k=kk, style="rise", role="word",
                  name=f"fig66_{i}", tin=0.15, room="study")
