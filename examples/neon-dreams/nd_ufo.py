"""Neon Dreams iteration 17 (owner msg 258: "use our UFO asset and shoot some UFOs across the sky at various points in the
video", arrows at 11.58 s sweeping left to right across the upper sky).

The craft is AV Gen's own `assets/imported/ufo.gltf` (flexunit, CC0 1.0; assets/imported/ATTRIBUTION.md) -- referenced
from the repository, not copied. Its root carries a x100 scale and the Z-up to Y-up turn, so a node scale of 0.1 is a
46 m saucer.

Five fly-bys (FLYBYS), each a fast, smooth arc across the upper sky: a straight chord through the frame at the middle
second, bowed up in its middle, crossed in 2-3 s (300-550 m/s). Each is placed where the lens looks at its middle
second; the chord's height in the picture and its distance are searched (like the fireworks' sites, nd_fireworks) for
the line of sight clear of cloud (CloudModel) and the town's boxes over the whole crossing, and the craft at least
CLEAR_ABOVE m over the local cloud tops. Three are timed to the music: the Moog's phrases at 19.32 and 25.98 s, and
the pair over the Condensery after the 58.97 s shell has fallen. (The Quay, 30-44 s, and the band and the plug, 44-58 s,
were searched too: a street's slit of sky and a turning lens kept a crossing in the picture under half its time.)

Each craft: the saucer (banked into its turn), a point light under it that lights the cloud tops it skims, a halo of
soft additive light round its underside and, for the ones with `trail`, a trail of sparks drawn over the clouds
(ADR-2127). A particle system spawns where its emitter is at each step, so a craft crossing 15 m a frame would leave
dots: the trail's sparks leave along the travel at 0..v and stop within ~3 steps (drag 10 at 30 fps: the velocity keeps
two thirds a step), filling each step's gap.
Everything is a timeline track keyed every frame: deterministic, a seek draws what a play draws.

it19 (owner msg 283): "remove UFO trail" -- no trails; "the glowing underneath the UFOs looks a little CG" -- the halo
sprites are gone and the asset's flat light-grey belly disc (lit by a 9000 point light 8 m under it, it read as a
white plate) is covered by a modelled belly (meshes/vehicles/ufo_belly_<colour>.glb, blender/nd_ufo_belly.py): a
gunmetal plate, a ring of small emissive panels, a dim frosted lens and a thin rim strip, so the light comes from
fittings in the hull; the point light (BELLY_LIGHT) sits well under the craft, a quarter as strong, lighting the cloud
tops it skims, not the hull. "this UFO disappears before it's fully off screen" (63.4 s) -- the chord is extended at
both ends, frame by frame against the lens at that second, until the whole saucer is out of the picture (extend()).
"""
from __future__ import annotations

import math

import numpy as np

import nd_fireworks as NFW

UFO_ASSET = "../../../assets/imported/ufo.gltf"   # from the scene folder
RADIUS = 2.3 * 100.0                               # m at node scale 1 (the glTF root's x100)
CLEAR_ABOVE = 40.0
# (middle second, crossing seconds, +1 left to right / -1 right to left, saucer diameter (m), glow colour, count)
FLYBYS = [
    {"t": 12.0, "dur": 2.6, "dir": 1, "diam": 46.0, "glow": [0.35, 0.85, 1.0], "n": 1},     # msg 258's arrows
    {"t": 20.3, "dur": 2.2, "dir": 1, "diam": 34.0, "glow": [1.0, 0.45, 0.9], "n": 1},     # the Moog at 19.32
    {"t": 26.0, "dur": 2.0, "dir": -1, "diam": 30.0, "glow": [0.6, 1.0, 0.7], "n": 1},      # the Moog at 25.98
    {"t": 62.2, "dur": 2.8, "dir": 1, "diam": 40.0, "glow": [0.35, 0.85, 1.0], "n": 2},     # the reveal, a pair
    {"t": 66.2, "dur": 2.6, "dir": -1, "diam": 50.0, "glow": [1.0, 0.7, 0.35], "n": 1},
]
BELLY = {(0.35, 0.85, 1.0): "cyan", (1.0, 0.45, 0.9): "pink", (0.6, 1.0, 0.7): "green", (1.0, 0.7, 0.35): "amber"}
BELLY_ASSET = "../meshes/vehicles/ufo_belly_%s.glb"
BELLY_LIGHT = {"intensity": 2200.0, "range": 120.0, "below": 0.7}   # x 9000 / 0.25 diam before
EXTEND_MAX = 8.0   # s added at most at either end


def _track(target, keys):
    return {"target": target, "component": -1, "enabled": True, "timeBase": "seconds", "mode": "replace",
            "loopLength": 0.0, "keys": keys}


def _frame(cam_at, t):
    eye, d, _roll, fov = cam_at(t)
    rgt = np.array([-d[2], 0.0, d[0]])
    rgt /= np.linalg.norm(rgt) or 1.0
    d = np.array(d, float)
    up = np.cross(rgt, d)
    tv = math.tan(math.radians(fov) * 0.5)
    return np.array(eye, float), d, rgt, up, tv


def path(cam_at, f, fy, dist, lane=0.0):
    """The chord's points (every frame of the crossing) for a height fy in the picture (of the half-height) at the
    middle second and a distance dist: from 1.25 half-widths off one side to 1.25 off the other, bowed up 0.08 of
    the half-height's span in the middle; `lane` shifts a wingman back and down."""
    t0 = f["t"] - 0.5 * f["dur"]
    eye, d, rgt, up, tv = _frame(cam_at, f["t"])
    th = tv * 16.0 / 9.0
    centre = eye + dist * (d + fy * tv * up)
    half = 1.25 * th * dist
    bow = 0.08 * tv * dist
    pts, n = [], int(round(f["dur"] * 30.0))
    for i in range(n + 1):
        u = i / n
        s = f["dir"] * (2.0 * u - 1.0)
        p = centre + rgt * (half * s - f["dir"] * lane * 1.6) + up * (bow * (1.0 - (2.0 * u - 1.0) ** 2) - lane * 0.5)
        pts.append((round(t0 + f["dur"] * u, 4), p))
    return pts


def score(cam_at, pts, clouds, boxes, diam):
    """(seen frames / frames, haze): a frame counts when the craft is in the picture, the sight line clear of cloud
    (tau <= 0.3) and of the town's boxes; haze sums what is in the way."""
    seen, haze, inframe = 0, 0.0, 0
    for k, (t, p) in enumerate(pts[::3]):
        eye, fx, fy = NFW._view(cam_at, t, list(p))
        if fx > 0.95 or fy > 0.8:   # the whole saucer in the picture, not its belly under the top edge
            continue
        inframe += 1
        tau = clouds.tau(eye, list(p)) if clouds else 0.0
        cut = False
        if boxes is not None and len(boxes[0]):
            le = NFW._local(boxes, eye)   # a box round the lens is the street it flies down, not a wall
            out = ~((boxes[3] <= le).all(1) & (le <= boxes[4]).all(1))
            cut = NFW._hits_boxes(eye, list(p), boxes, out)
        haze += min(tau, 5.0) + (2.0 if cut else 0.0)
        if tau <= 0.3 and not cut:
            seen += 1
    n = max(len(pts[::3]), 1)
    return seen / n, haze / n, inframe / n


def place(cam_at, f, clouds, boxes):
    """The chord's height in the picture and distance with the most of the crossing seen, the least haze, and the
    craft CLEAR_ABOVE m over the cloud tops under its path."""
    best = None
    for fy in (0.25, 0.32, 0.4, 0.48, 0.56, 0.64, 0.72):   # the whole saucer in the picture, bow included
        for dist in (260.0, 340.0, 420.0, 520.0, 650.0, 800.0):
            pts = path(cam_at, f, fy, dist)
            mid = pts[len(pts) // 2][1]
            over = min(p[1] - clouds.top_near(p[0], p[2], 40.0) for _, p in pts[::6]) if clouds else 1e9
            s, haze, inf = score(cam_at, pts, clouds, boxes, f["diam"])
            val = s - 0.3 * haze - (1.0 if over < CLEAR_ABOVE else 0.0) - 0.0004 * dist + 0.1 * fy
            if best is None or val > best[0]:
                best = (val, fy, dist, s, haze, over, inf, mid)
    return best


def _in_picture(cam_at, t, p, diam):
    """Any of the saucer (its disc's reach, diam / 2 round the centre) inside the frame at t."""
    s = NFW._screen(cam_at, t, list(p))
    if s is None:
        return False
    x, y, z, tv = s
    r = 0.75 * diam / (z * tv)   # half-heights, half again over the half-diameter (the bank, the dome, motion blur)
    if diam / (z * tv) * 540.0 < 5.0:   # under 5 px across at 1080: gone into the distance
        return False
    return abs(x) - r < 16.0 / 9.0 and abs(y) - r < 1.0


def extend(cam_at, pts, diam):
    """it19 (msg 283: "this UFO disappears before it's fully off screen"): the crossing carried on along its end
    tangents, a frame at a time, until the whole saucer is out of the picture at that second (at most EXTEND_MAX s
    either side)."""
    step = 1.0 / 30.0
    (ta, pa), (ta2, pa2) = pts[0], pts[1]
    (tb, pb), (tb2, pb2) = pts[-1], pts[-2]
    va = (pa2 - pa) / (ta2 - ta)   # forward, as vb
    vb = (pb - pb2) / (tb - tb2)
    head, tail = [], []
    for out, (t, p, v, sgn) in ((head, (ta, pa, va, -1.0)), (tail, (tb, pb, vb, 1.0))):
        clear = 0   # it19 render check: out of the picture for six frames running (a sliver showed at the edge)
        while clear < 6 and abs(t - (ta if sgn < 0 else tb)) < EXTEND_MAX:
            t, p = t + sgn * step, p + sgn * v * step
            out.append((round(t, 4), p))
            clear = 0 if _in_picture(cam_at, t, p, diam) else clear + 1
    return head[::-1] + pts + tail


def build(cam_at, clouds_doc=None, layout=None, kit=None):
    """(nodes, lights, tracks, info)."""
    clouds = NFW.CloudModel(clouds_doc) if clouds_doc else None
    boxes = NFW.occluders(layout, kit) if (layout and kit) else None
    nodes, lights, tracks, info = [], [], [], []
    for k, f in enumerate(FLYBYS):
        _, fy, dist, s, haze, over, inf, _mid = place(cam_at, f, clouds, boxes)
        row = {"t": f["t"], "fy": fy, "dist": dist, "seen": round(s, 2), "haze": round(haze, 3),
               "over_cloud": round(float(min(over, 9999.0)), 1), "in_frame": round(inf, 2), "life": []}
        info.append(row)
        for j in range(f["n"]):
            name = "ufo%d%s" % (k, "abc"[j])
            core = path(cam_at, f, fy, dist, lane=j * f["diam"] * 1.4)
            pts = extend(cam_at, core, f["diam"])
            t0, t1 = pts[0][0], pts[-1][0]
            row["life"].append([t0, t1, _in_picture(cam_at, t0, pts[0][1], f["diam"]),
                                _in_picture(cam_at, t1, pts[-1][1], f["diam"])])
            vel = (core[-1][1] - core[0][1]) / (core[-1][0] - core[0][0])
            fwd = vel / float(np.linalg.norm(vel))
            yaw = math.degrees(math.atan2(-fwd[2], fwd[0]))
            bank = -10.0 * f["dir"]
            scale = f["diam"] / (2.0 * RADIUS)
            park = [round(v, 2) for v in (pts[0][1] + np.array([0.0, -3000.0, 0.0]))]   # out of sight before and after
            pk = [{"time": 0.0, "value": park, "interp": "step"},
                  {"time": round(t0 - 0.002, 4), "value": park, "interp": "step"}]
            pk += [{"time": t, "value": [round(v, 2) for v in p], "interp": "linear"} for t, p in pts]
            pk += [{"time": round(t1 + 0.002, 4), "value": park, "interp": "step"}]
            # a spin about its axis while it crosses (a saucer turns), at the it17 rate over the longer life
            spin = 140.0 * f["dir"] * (t1 - t0) / f["dur"]
            rot = [{"time": round(t0, 4), "value": [0.0, round(yaw, 2), round(bank, 2)], "interp": "linear"},
                   {"time": round(t1, 4), "value": [0.0, round(yaw + spin, 2), round(bank, 2)], "interp": "linear"}]
            belly = name + "_belly"   # it19 (msg 283): the lit fittings under the hull (the asset's own disc covered)
            for nm, asset in ((name, UFO_ASSET), (belly, BELLY_ASSET % BELLY[tuple(f["glow"])])):
                nodes.append({"name": nm, "kind": "gltf", "asset": asset, "position": park,
                              "rotation": [0.0, round(yaw, 2), round(bank, 2)], "scale": [round(scale, 4)] * 3})
                tracks.append(_track("nodes/%s/position" % nm, pk))
                tracks.append(_track("nodes/%s/rotation" % nm, rot))
            drop = BELLY_LIGHT["below"] * f["diam"]
            under = [{"time": k2["time"], "value": [k2["value"][0], round(k2["value"][1] - drop, 2),
                                                    k2["value"][2]], "interp": k2["interp"]} for k2 in pk]
            light = name + "_glow"
            lights.append({"name": light, "type": "point", "position": park, "color": f["glow"], "intensity": 0.0,
                           "range": BELLY_LIGHT["range"], "castsShadow": False, "volumetric": 0.0})
            tracks.append(_track("lights/%s/position" % light, under))
            tracks.append(_track("lights/%s/intensity" % light, [
                {"time": 0.0, "value": 0.0, "interp": "step"},
                {"time": round(t0, 4), "value": BELLY_LIGHT["intensity"], "interp": "step"},
                {"time": round(t1, 4), "value": 0.0, "interp": "step"}]))
    return nodes, lights, tracks, {"flybys": info}
