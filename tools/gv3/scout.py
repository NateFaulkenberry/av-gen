"""A location scout: one short locked shot per candidate viewpoint, so a single render returns a
still of every place the production might put a camera. Not part of the film; `--scout` builds it
instead of the cut."""

from .rig import Rig, Shot

SHOT_SECONDS = 2.0


def viewpoints(ground):
    """(name, rig) for every candidate. Heights are metres above the local ground or water."""
    g = ground.above
    v = []
    elder = [-12.0, 11.0, 52.0]
    v.append(("elder from the west bank, across the pool", Rig("scout", 35, g(-62, 44, 2.5), elder)))
    v.append(("elder low from the south-west, looking up", Rig("scout", 24, g(-40, 95, 1.5), [-12, 13, 52])))
    v.append(("elder close, under the cap from the east", Rig("scout", 20, g(6, 60, 2.0), [-12, 14, 50])))
    v.append(("elder from the north, long lens", Rig("scout", 85, g(-20, -60, 4.0), [-12, 10, 52])))
    v.append(("elder from the south, very long lens", Rig("scout", 100, g(0, 200, 6.0), [-12, 10, 52])))
    v.append(("river, low, downstream to the bend", Rig("scout", 28, g(0, -30, 1.2), [-41, 1, 76])))
    v.append(("river, low, upstream from the pool", Rig("scout", 28, g(-38, 62, 1.2), [5, 4, -34])))
    v.append(("lantern from the hollow", Rig("scout", 28, g(-70, -40, 1.8), [-46, 11, -28])))
    v.append(("valley from the south rim", Rig("scout", 50, g(0, 300, 30.0), [-20, 5, 0])))
    v.append(("valley from the north rim", Rig("scout", 50, g(-20, -290, 25.0), [-10, 5, 60])))
    v.append(("valley from the west wall", Rig("scout", 35, g(-230, 20, 12.0), [0, 5, 40])))
    v.append(("valley from the east wall", Rig("scout", 35, g(240, 60, 12.0), [-20, 5, 40])))
    v.append(("valley floor, low and wide", Rig("scout", 24, g(60, 140, 2.0), [-30, 8, 20])))
    v.append(("cairn on the spur", Rig("scout", 35, g(-125, -45, 2.0), [-150, 35, -60])))
    v.append(("scree on the west slope", Rig("scout", 35, g(-150, 110, 2.0), [-178, 25, 96])))
    v.append(("bloom by the river", Rig("scout", 35, g(-40, 130, 2.0), [-62, 9, 118])))
    v.append(("umbra in the south", Rig("scout", 35, g(-45, 180, 2.0), [-66, 8, 166])))
    v.append(("veil at the water", Rig("scout", 35, g(95, 210, 2.0), [78, 3, 198])))
    v.append(("ember on the east terrace", Rig("scout", 35, g(160, 170, 2.0), [176, 15, 150])))
    v.append(("spire on the east bank", Rig("scout", 35, g(50, -90, 2.0), [68, 15, -104])))
    v.append(("ridge high in the north-east", Rig("scout", 35, g(110, -170, 3.0), [132, 25, -190])))
    v.append(("the saucer at rest, from below", Rig("scout", 50, g(-10, 120, 2.0), [20, 29, 150])))
    v.append(("rook, close follow", Rig("scout", 35, follow="rook", follow_offset=(6, 2.5, 6), aim="rook",
                                        aim_offset=(0, 2.0, 0), clearance=1.0)))
    v.append(("vane, close follow", Rig("scout", 35, follow="vane", follow_offset=(-8, 2.5, 4), aim="vane",
                                        aim_offset=(0, 2.0, 0), clearance=1.0)))
    v.append(("ember the alien, follow", Rig("scout", 35, follow="ember", follow_offset=(5, 3.0, -7), aim="ember",
                                             aim_offset=(0, 2.0, 0), clearance=1.0)))
    v.append(("horse-11 by the elder", Rig("scout", 35, g(-20, 80, 2.0), [-4, 6, 66])))
    return v


def build(ground, start=0.0):
    shots = []
    t = start
    for i, (name, rig) in enumerate(viewpoints(ground)):
        rig.name = f"scout {i + 1:02d} {name}"
        shots.append(Shot(f"k{i + 1:02d}", t, t + SHOT_SECONDS, rig, purpose=name, subject=name))
        t += SHOT_SECONDS
    return shots
