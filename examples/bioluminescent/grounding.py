"""THE RIFT's grounding and camera-clearance rules (docs/prototypes/bioluminescent/grounding-pass-2026-10.md).

Everything here is measured against the ground the renderer DRAWS, not the world's analytic height:

  * land.Ground.height interpolates the engine's heights exactly as the terrain's LOD 0 mesh does (the same 2 m
    vertices, the same triangle diagonal), so "on the ground" means on the triangles on screen.
  * The scene's terrain is drawn at LOD 0 everywhere (`assert_terrain_full_resolution`). The coarser rungs it used to
    have (4, 8 and 16 m cells from 70 m out) moved the drawn ground by up to +-6-10 m under organisms placed on the
    true surface: a meadow on a rib crest stood on a shelf of air, one in a gully was swallowed.

The fit (`fit`) moves each instance along its own up axis -- and, for a patch that cannot lie on a curved wall at its
size, shrinks it -- until its contact geometry (the base ring of a trunk, the underside of a cushion, the points of a
crust patch) touches the drawn surface with no gap and nothing deeper than the instance's authored sink. The check
(`check`) measures the result per instance as a distance along the surface normal, so a fan on a 70-degree wall is
judged by how far it stands off the rock, not by a vertical number that a steep slope inflates.

The camera (`flight_corridor`, `clear_corridor`, `camera_clearance`): the spline camera's eye is the flight point plus
an offset in the spline frame (lateral = binormal, up = normal; composition.cpp). The arc's stages move that offset
between the CAM behaviours with eased blends, a MIDI pad can jump between any two, and two LFOs drift it by up to
+-2 m lateral and +-1 m up -- so the set of places the eye can be at a point of the flight is the convex hull of the
behaviours' offsets, grown by the drift. Trunks and large organisms are kept out of that set by a clearance margin.
"""
from __future__ import annotations

import json
import math
import struct
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent

# How each placed kind meets the ground: its contact geometry and how deep it may go.
#   source   the GLB whose lowest band of vertices is the contact (None: an emitter-only patch, its template points)
#   band     metres above the mesh's lowest vertex that count as contact
#   gap      the most the contact may stand OFF the surface (along the normal), metres
#   bury     the most the contact may go INTO it beyond the authored sink, metres (a cushion on a hump, a patch on a
#            hollow); past this the fit shrinks the instance (shrink=True) or reports it
CONTACT = {
    "crinoid": dict(source="crinoid_stalk", band=0.12, gap=0.02, bury=4.0),
    "seapen": dict(source="seapen", band=0.08, gap=0.02, bury=0.25),
    "mat": dict(source="mat_cushion", band=0.06, gap=0.03, bury=0.30, shrink=True),
    "fan": dict(source="fan_body", band=0.10, gap=0.02, bury=0.40),
    "whips": dict(source="whips_body", band=0.10, gap=0.02, bury=0.5),
    "lanterns": dict(source="lantern_stalks", band=0.06, gap=0.02, bury=0.30),
    "crust": dict(source="crust.emit", band=1.0, gap=0.06, bury=0.35, shrink=True),
    "ember": dict(source="crust.emit", band=1.0, gap=0.06, bury=0.35, shrink=True),
}
MIN_SHRINK = 0.35   # a patch shrinks at most to this fraction of its authored size before it is dropped


def read_glb_positions(path):
    b = Path(path).read_bytes()
    jl = struct.unpack("<I", b[12:16])[0]
    doc = json.loads(b[20:20 + jl])
    binary = b[20 + jl + 8:]
    acc = doc["accessors"][doc["meshes"][0]["primitives"][0]["attributes"]["POSITION"]]
    v = doc["bufferViews"][acc["bufferView"]]
    return np.frombuffer(binary[v["byteOffset"]:v["byteOffset"] + v["byteLength"]], dtype=np.float32).reshape(-1, 3)


_CONTACT_CACHE = {}


def contact_points(kind):
    """Local-space contact points of a kind (unit scale)."""
    if kind in _CONTACT_CACHE:
        return _CONTACT_CACHE[kind]
    c = CONTACT[kind]
    if c["source"].endswith(".emit"):
        pts = np.array(json.loads((HERE / "meshes" / f"{c['source']}.json").read_text())["points"])[:, :3]
    else:
        p = read_glb_positions(HERE / "meshes" / f"{c['source']}.glb").astype(float)
        pts = p[p[:, 1] < p[:, 1].min() + c["band"]]
    # thin a dense set: contact is decided by its extremes, and a few hundred points are plenty
    if len(pts) > 240:
        pts = pts[np.linspace(0, len(pts) - 1, 240).astype(int)]
    _CONTACT_CACHE[kind] = pts
    return pts


def quat_rotate(q, v):
    """Rotate v (N x 3) by quaternions q (N x 4, x y z w)."""
    u, w = q[:, None, :3], q[:, None, 3:4]
    t = 2.0 * np.cross(u, v)
    return v + w * t + np.cross(u, t)


def world_contacts(kind, recs):
    """(N, K, 3) world positions of each record's contact points."""
    a = np.asarray(recs, float)
    local = contact_points(kind)[None, :, :] * a[:, None, 7:8]
    return quat_rotate(a[:, 3:7], np.broadcast_to(local, (len(a),) + local.shape[1:])) + a[:, None, :3]


def normal_gap(g, pts):
    """Signed distance of points above the drawn surface, along its normal (positive = in the air)."""
    x, y, z = pts[..., 0], pts[..., 1], pts[..., 2]
    ny = g.normal(x.ravel(), z.ravel())[:, 1].reshape(x.shape)
    return (y - g.height(x.ravel(), z.ravel()).reshape(x.shape)) * ny


def fit(g, kind, recs, sink):
    """Seat each record on the drawn surface: slide it along its own up axis until its contact geometry stands no
    more than `gap` off the rock (along the surface normal), with nothing deeper than its authored sink + `bury`. A
    patch that cannot (a crust patch wider than the wall is flat) shrinks about its centre; past MIN_SHRINK of its
    size it is dropped. Returns the records kept and how many were shrunk / dropped."""
    if not recs:
        return recs, 0, 0
    c = CONTACT[kind]
    orig = np.asarray(recs, float)
    a = orig.copy()
    up = quat_rotate(a[:, 3:7], np.tile([[0.0, 1.0, 0.0]], (len(a), 1))[:, None, :])[:, 0, :]
    shrunk = np.zeros(len(a), bool)
    for _ in range(12):
        pts = world_contacts(kind, a)
        gap = normal_gap(g, pts)
        n = g.normal(pts[..., 0].ravel(), pts[..., 2].ravel()).reshape(pts.shape)
        rate = np.maximum((n * up[:, None, :]).sum(-1), 0.25)   # normal-gap change per metre along `up`
        # seat the highest contact point just inside the tolerance
        need = ((gap - c["gap"] * 0.5) / rate).max(1)
        a[:, :3] -= up * need[:, None]
        span = gap.max(1) - gap.min(1)
        allowed = sink * a[:, 7] + c["bury"]
        bad = span > allowed + c["gap"]
        done = np.abs(need).max() < 0.005 and (not c.get("shrink") or not bad.any())
        if done:
            break
        if c.get("shrink") and bad.any():
            f = np.clip((allowed + c["gap"]) / np.maximum(span, 1e-6), 0.7, 1.0)
            a[bad, 7:10] *= f[bad, None]
            shrunk |= bad
    gap = normal_gap(g, world_contacts(kind, a))
    keep = np.ones(len(a), bool)
    if c.get("shrink"):
        keep = (a[:, 7] >= MIN_SHRINK * orig[:, 7]) & (gap.min(1) >= -(sink * a[:, 7] + c["bury"] + c["gap"]))
    out = [[round(float(v), 5 if 3 <= i < 7 else (4 if i >= 7 else 3)) for i, v in enumerate(r)] for r in a[keep]]
    return out, int(shrunk.sum()), int((~keep).sum())


def check(g, P, sinks):
    """Per kind: the contact's worst gap off the surface and worst depth into it (metres, along the normal)."""
    report = {}
    for kind, c in CONTACT.items():
        if not P.get(kind):
            continue
        a = np.asarray(P[kind], float)
        gap = normal_gap(g, world_contacts(kind, a))
        hi = gap.max(1)                    # each instance's furthest-off contact point
        lo = gap.min(1)
        report[kind] = {"n": len(a), "maxGap": float(hi.max()), "p99Gap": float(np.percentile(hi, 99)),
                        "deepest": float(lo.min()), "allowedDepth": float(-(sinks.get(kind, 0.0) * a[:, 7].max()
                                                                            + c["bury"] + c["gap"])),
                        "gapTol": c["gap"]}
    return report


def check_free(g, P):
    """What is not on the ground must be above it: comb jellies (they bob 1.2 m), plankton (on the water)."""
    out = {}
    a = np.asarray(P["comb"], float)
    out["comb"] = {"n": len(a), "minAboveGround": float((a[:, 1] - g.height(a[:, 0], a[:, 2])).min())}
    a = np.asarray(P["plankton"], float)
    wl = g.water_level(a[:, 0], a[:, 2])
    out["plankton"] = {"n": len(a), "minAboveBed": float((a[:, 1] - g.height(a[:, 0], a[:, 2])).min()),
                       "maxOffWater": float(np.abs(a[:, 1] - wl).max())}
    return out


def assert_grounded(report, free):
    msgs = []
    for k, r in report.items():
        if r["maxGap"] > r["gapTol"] + 1e-3:
            msgs.append(f"{k}: a contact stands {r['maxGap']:.3f} m off the drawn ground (tolerance {r['gapTol']})")
        if r["deepest"] < r["allowedDepth"] - 1e-3:
            msgs.append(f"{k}: a contact is {-r['deepest']:.2f} m into the ground (allowed {-r['allowedDepth']:.2f})")
    if free["comb"]["minAboveGround"] < 1.5:
        msgs.append(f"comb: {free['comb']['minAboveGround']:.2f} m above the ground; it bobs 1.2 m")
    if free["plankton"]["minAboveBed"] < 0.0 or free["plankton"]["maxOffWater"] > 0.01:
        msgs.append("plankton: off the water surface or under the bed")
    if msgs:
        raise SystemExit("grounding check failed:\n  " + "\n  ".join(msgs))


def assert_terrain_full_resolution(terrain):
    """The drawn ground is the LOD 0 mesh wherever anything stands on it: one level (no coarser rung)."""
    if terrain.get("lodLevels", 4) != 1:
        raise SystemExit("grounding check failed: the terrain must draw at LOD 0 (lodLevels 1); a coarse rung moves "
                         "the drawn ground metres off the placements (grounding-pass-2026-10.md)")


# ============================================================================================ the camera
def eye_hull(cam, drift=(2.0, 1.0)):
    """Convex hull (lateral, up) of every offset the eye can take, grown by the LFO drift: a polygon, CCW."""
    pts = []
    for (up, lat, *_r) in cam.values():
        for dx in (-drift[0], drift[0]):
            for dy in (-drift[1], drift[1]):
                pts.append((lat + dx, up + dy))
    pts = sorted(set(pts))

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return np.array(lower[:-1] + upper[:-1])


def _dist_to_polygon(poly, q):
    """Distance from 2D points q (N x 2) to a convex polygon (0 inside)."""
    n = len(poly)
    inside = np.ones(len(q), bool)
    best = np.full(len(q), np.inf)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        e = b - a
        rel = q - a
        inside &= (e[0] * rel[:, 1] - e[1] * rel[:, 0]) >= 0
        t = np.clip((rel @ e) / (e @ e), 0, 1)
        d = np.linalg.norm(rel - t[:, None] * e, axis=1)
        best = np.minimum(best, d)
    return np.where(inside, 0.0, best)


class Flight:
    """The flight spline sampled densely (Catmull-Rom through the points, as the engine's spline), with its frame."""

    def __init__(self, pts, step=0.5):
        P = np.asarray(pts, float)
        Q = np.vstack([P[0] * 2 - P[1], P, P[-1] * 2 - P[-2]])
        out = []
        for i in range(1, len(Q) - 2):
            p0, p1, p2, p3 = Q[i - 1], Q[i], Q[i + 1], Q[i + 2]
            n = max(2, int(np.linalg.norm(p2 - p1) / step))
            for t in np.linspace(0, 1, n, endpoint=False):
                t2, t3 = t * t, t * t * t
                out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 +
                                  (-p0 + 3 * p1 - 3 * p2 + p3) * t3))
        out.append(P[-1])
        self.p = np.array(out)
        tan = np.gradient(self.p, axis=0)
        self.t = tan / np.linalg.norm(tan, axis=1, keepdims=True)
        upv = np.array([0.0, 1.0, 0.0])
        nrm = upv - (self.t @ upv)[:, None] * self.t
        self.n = nrm / np.linalg.norm(nrm, axis=1, keepdims=True)
        self.b = np.cross(self.n, self.t)          # binormal = cross(normal, tangent), the engine's convention

    def local(self, q):
        """Each world point's (lateral, up, along) relative to its nearest flight sample (z-sorted lookup)."""
        q = np.asarray(q, float)
        idx = np.clip(np.searchsorted(self.p[:, 2], q[:, 2]), 0, len(self.p) - 1)
        best = idx.copy()
        bd = np.full(len(q), np.inf)
        for off in range(-60, 61, 2):
            k = np.clip(idx + off, 0, len(self.p) - 1)
            d = np.linalg.norm(q - self.p[k], axis=1)
            better = d < bd
            bd[better], best[better] = d[better], k[better]
        for off in (-1, 1):
            k = np.clip(best + off, 0, len(self.p) - 1)
            d = np.linalg.norm(q - self.p[k], axis=1)
            better = d < bd
            bd[better], best[better] = d[better], k[better]
        rel = q - self.p[best]
        return np.stack([(rel * self.b[best]).sum(1), (rel * self.n[best]).sum(1), (rel * self.t[best]).sum(1)], 1)

    def eye_distance(self, hull, q):
        """Distance from world points to the swept eye volume (hull extruded along the flight)."""
        loc = self.local(q)
        d2 = _dist_to_polygon(hull, loc[:, :2])
        # beyond either end of the flight the along-coordinate counts too (inside it, it is under half a sample)
        return np.hypot(d2, loc[:, 2])


# Obstacles the eye must not pass through: a crinoid's stalk as its centre line (radius from the mesh), a large body
# as a sphere around its bulk.
def stalk_line(n=24):
    """The crinoid stalk's centre line (unit scale), from its mesh: the mean of each height band's vertices."""
    p = read_glb_positions(HERE / "meshes" / "crinoid_stalk.glb").astype(float)
    top = p[:, 1].max() - 1.5          # below the calyx
    out = []
    for y0, y1 in zip(np.linspace(0, top, n)[:-1], np.linspace(0, top, n)[1:]):
        band = p[(p[:, 1] >= y0) & (p[:, 1] < y1)]
        if len(band):
            c = np.median(band, 0)
            r = np.percentile(np.linalg.norm(band[:, [0, 2]] - c[[0, 2]], axis=1), 60)
            out.append([c[0], (y0 + y1) / 2, c[2], r])
    return np.array(out)


BODY_SPHERES = {   # kind: (centre height, radius) at unit scale, the bulk of the body
    "fan": (1.4, 1.5), "whips": (1.7, 1.4), "lanterns": (0.3, 0.6),
}


def obstacles(P):
    """(points (N x 3), radii (N), owner kind, owner index)."""
    pts, rad, kind, idx = [], [], [], []
    line = stalk_line()
    a = np.asarray(P["crinoid"], float)
    if len(a):
        loc = line[None, :, :3] * a[:, None, 7:8]
        w = quat_rotate(a[:, 3:7], loc) + a[:, None, :3]
        pts.append(w.reshape(-1, 3))
        rad.append((line[None, :, 3] * a[:, 7:8]).ravel())
        kind += ["crinoid"] * w.shape[0] * w.shape[1]
        idx.append(np.repeat(np.arange(len(a)), w.shape[1]))
    for k, (h, r) in BODY_SPHERES.items():
        a = np.asarray(P.get(k, []), float)
        if not len(a):
            continue
        up = quat_rotate(a[:, 3:7], np.tile([[0.0, 1.0, 0.0]], (len(a), 1))[:, None, :])[:, 0, :]
        pts.append(a[:, :3] + up * h * a[:, 7:8])
        rad.append(r * a[:, 7])
        kind += [k] * len(a)
        idx.append(np.arange(len(a)))
    return np.vstack(pts), np.concatenate(rad), np.array(kind), np.concatenate(idx)


def camera_clearance(flight, hull, P, g=None):
    """The closest any trunk or large body comes to the eye's swept volume (surface to eye, metres), per kind, and the
    nearest the eye comes to the drawn ground."""
    pts, rad, kind, _ = obstacles(P)
    # only what is near the flight matters; the rest is far out on the walls
    d = flight.eye_distance(hull, pts) - rad
    out = {k: float(d[kind == k].min()) for k in np.unique(kind)}
    if g is not None:
        # every corner of the hull along the flight: the eye's height above the drawn ground
        lo = []
        for lat, up in hull:
            e = flight.p + flight.b * lat + flight.n * up
            lo.append(e[:, 1] - g.height(e[:, 0], e[:, 2]))
        out["ground"] = float(np.min(lo))
    return out


def clear_corridor(flight, hull, P, margin):
    """Move each crinoid whose stalk comes within `margin` of the eye's swept volume straight out, sideways from the
    flight, until it clears. Returns how many moved and the largest move."""
    a = np.asarray(P["crinoid"], float)
    if not len(a):
        return 0, 0.0
    line = stalk_line()
    moved, biggest = 0, 0.0
    for k in range(len(a)):
        shift = 0.0
        for _ in range(40):
            loc = quat_rotate(a[k:k + 1, 3:7], line[None, :, :3] * a[k, 7]) [0] + a[k, :3]
            d = flight.eye_distance(hull, loc) - line[:, 3] * a[k, 7]
            if d.min() >= margin:
                break
            side = flight.local(a[k:k + 1, :3])[0, 0]
            i = int(np.argmin(np.linalg.norm(flight.p - a[k, :3], axis=1)))
            direction = flight.b[i] * (1.0 if side >= 0 else -1.0)
            direction[1] = 0.0
            direction /= np.linalg.norm(direction)
            a[k, [0, 2]] += direction[[0, 2]] * 0.5
            shift += 0.5
        if shift:
            moved += 1
            biggest = max(biggest, shift)
    P["crinoid"] = a.tolist()
    return moved, biggest


def outside_corridor(flight, hull, P, kind, margin):
    """The records of `kind` whose bulk stays at least `margin` from the eye's corridor (the rest are dropped: a fan or
    a whip tuft is one of thousands, and nudging one off a wall would unseat it)."""
    a = np.asarray(P.get(kind, []), float)
    if not len(a):
        return P.get(kind, [])
    h, r = BODY_SPHERES[kind]
    up = quat_rotate(a[:, 3:7], np.tile([[0.0, 1.0, 0.0]], (len(a), 1))[:, None, :])[:, 0, :]
    d = flight.eye_distance(hull, a[:, :3] + up * h * a[:, 7:8]) - r * a[:, 7]
    return [rec for rec, ok in zip(P[kind], d >= margin) if ok]


def check_river(g):
    """The river sits in its bed: wherever its water ends, the ground beyond is at or above the water line (the bank
    holds it), so the sheet never ends in mid-air; and its surface is continuous along and across its course. On the
    drawn ground's own 2 m vertices (the sheet is built on the same grid, terrain.cpp)."""
    H, W = g.h, g.water
    wet = W > H
    held = []
    for ax in (0, 1):
        for s in (1, -1):
            wn, hn = np.roll(wet, s, axis=ax), np.roll(H, s, axis=ax)
            edge = wet & ~wn
            edge[[0, -1], :] = False
            edge[:, [0, -1]] = False
            held.append((hn - W)[edge])
    held = np.concatenate(held)
    step = max(float(np.abs(np.diff(W, axis=0))[wet[1:] & wet[:-1]].max()),
               float(np.abs(np.diff(W, axis=1))[wet[:, 1:] & wet[:, :-1]].max()))
    return {"wetCells": int(wet.sum()), "worstBankBelowWater": float(-held.min()), "maxDepth": float((W - H)[wet].max()),
            "maxSurfaceStep": step}


def assert_river(r, tol=0.05):
    if r["worstBankBelowWater"] > tol or r["maxSurfaceStep"] > 0.1:
        raise SystemExit(f"grounding check failed: the river's water ends {r['worstBankBelowWater']:.2f} m above the "
                         f"ground beyond it (tolerance {tol}), or its surface steps {r['maxSurfaceStep']:.2f} m: {r}")


def assert_world_covers(world, terrain, region, flight_pts):
    """The terrain is drawn over the world map's extent and nowhere else, so the map must hold every placement and
    the flight, plus the terrain's view distance ahead of either end of it. (The map stopped at z = +-1200 while the
    flight started at -1300: the opening looked at the ground's cut edge from outside the world.)"""
    half = np.array(world["size"], float) * 0.5
    f = np.asarray(flight_pts, float)
    view = terrain["viewDistance"]
    need_x = max(abs(region[0]), abs(region[1]), np.abs(f[:, 0]).max())
    need_z = max(abs(region[2]), abs(region[3]), np.abs(f[:, 2]).max() + view)
    if need_x > half[0] or need_z > half[1]:
        raise SystemExit(f"grounding check failed: the world map spans +-{half[0]:.0f} x +-{half[1]:.0f} m; the "
                         f"placements and the flight (+ the {view:.0f} m view ahead) need +-{need_x:.0f} x "
                         f"+-{need_z:.0f} m")
