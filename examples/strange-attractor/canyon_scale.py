"""The canyon's world scale (ADR-1893), shared by the Art Pass 2 builders.

The canyon was authored (Art Pass 1) at generator scale 1. With figures in it, the owner found it far bigger than they
had pictured, so the whole world is scaled down uniformly by WORLD_SCALE. `attractorWorld.worldScale` scales the
generated terrain, river, flight and approach; `rescale()` scales every other length the canyon scene and the project
carry, so the opening is the same picture at a smaller size:
- the camera, the fog, the shadows and the cirrus;
- the water's ripples and its depth fades;
- the terrain program's world-position frequencies and depth fades;
- the scan, and the portal's corruption (aperture, blocks, glyphs, field and light);
- the corruption slip route.

Characters and props keep their real size: they are placed by the builders with `canyon()` (scaled) and their own
dimensions (unscaled).

`rescale()` is idempotent: it reads the scale the scene was last written at (`attractorWorld.worldScale`, 1 if absent)
and applies the ratio.
"""

import math

# The canyon's scale. (M3b's smaller-scale POC, SA_WORLD_SCALE and its "poc-" files, was retired in M3d.)
REF = 0.4
WORLD_SCALE = REF
R = WORLD_SCALE / REF                 # terrain-scale lengths (authored at REF) times this


def canyon_frame(d, lat):
    """The canyon frame used since Gate C 2: d metres down-canyon from the dusk exit, lat metres toward the camp bank.
    Both are in generator metres (scale 1); the result is in world metres."""
    return [WORLD_SCALE * (166.5 - 0.426 * d + 0.905 * lat), WORLD_SCALE * (3427.3 - 0.905 * d - 0.426 * lat)]


canyon = canyon_frame  # (the name the scaling and the early builders used)

O_UNIT = (-0.426, -0.905)             # down-canyon
L_UNIT = (0.905, -0.426)              # toward the camp bank


def site(d, lat):
    """A place on the terrain, authored in REF metres (the current canyon's): it follows the terrain at any scale."""
    return canyon_frame(d / REF, lat / REF)


def near(anchor, dd, dl):
    """dd, dl real metres (down-canyon, toward the bank) from a site: a character-scale offset, never scaled."""
    a = site(*anchor)
    return [a[0] + dd * O_UNIT[0] + dl * L_UNIT[0], a[1] + dd * O_UNIT[1] + dl * L_UNIT[1]]


CAMP_SITE = (16.0, 16.0)


def knot(d, lat, h, t, anchor=CAMP_SITE, radius=12.0):
    """A flight knot authored at REF: within `radius` of the camp it is character scale (real metres from the camp,
    its height real); further out it is terrain scale (a site, its height above the ground scaled)."""
    if math.hypot(d - anchor[0], lat - anchor[1]) < radius:
        x, z = near(anchor, d - anchor[0], lat - anchor[1])
        return [x, h, z, t]
    x, z = site(d, lat)
    return [x, h * R, z, t]


def _mul(v, r):
    return [x * r for x in v] if isinstance(v, list) else v * r


def rescale(scene, project=None):
    old = scene.get("attractorWorld", {}).get("worldScale", 1.0)
    r = WORLD_SCALE / old
    if abs(r - 1.0) < 1e-9:
        return 1.0
    aw = scene["attractorWorld"]
    aw["worldScale"] = WORLD_SCALE
    if WORLD_SCALE == 1.0:
        aw.pop("worldScale")
    if "approach" in aw:
        ap = aw["approach"]
        ap["site"] = _mul(ap["site"], r)
        for k in ("departArc", "speed"):
            ap[k] *= r
    aw["sunShadowBias"] = aw.get("sunShadowBias", 0.6) * r
    w = aw.get("water", {})
    for k in ("clarity", "edgeFade", "foamWidth", "refraction", "ssrDistance"):
        if k in w:
            w[k] *= r
    if "rippleScale" in w:
        w["rippleScale"] /= r          # cycles per metre
    w["rippleSpeed"] = w.get("rippleSpeed", 1.0) * r

    cam = scene.get("camera", {})
    for k in ("position", "target"):
        if k in cam:
            cam[k] = _mul(cam[k], r)

    env = scene.get("environment", {})
    if "volumeDensity" in env:
        env["volumeDensity"] /= r      # per metre
    for k in ("shadowRange", "shadowCasterReach"):
        if k in env:
            env[k] *= r
    ci = env.get("sky", {}).get("cirrus")
    if ci:
        for k in ("altitude", "length", "width", "bandWidth"):
            if k in ci:
                ci[k] *= r

    # the terrain program: the ops fed by world position (frequencies per metre) and by depth (metres)
    for prog in scene.get("materialPrograms", []):
        if prog["name"] != "canyonRock":
            continue
        ops = prog["ops"]
        regs = {}
        for o in ops:
            kind = o["kind"]
            src = o.get("srcA")
            fromPos = regs.get(src) == "pos" if src is not None else False
            fromDepth = regs.get(src) == "depth" if src is not None else False
            if kind == "input":
                regs[o["dst"]] = {"worldPosition": "pos", "depth": "depth"}.get(o["input"])
                continue
            if fromPos and kind in ("gradient", "noise", "triplanar"):
                o["value"] /= r
            elif fromPos and kind == "palette":
                o["constant3"] = [c / r for c in o["constant3"]]
            elif fromDepth and kind == "remap":
                o["constant"][0] *= r
                o["constant"][1] *= r
            # a swizzle of a position is still a position; anything else is no longer a length
            regs[o["dst"]] = regs.get(src) if (kind == "swizzle" and src is not None) else None

    mw = scene.get("meshWire")
    if mw:
        for k in ("fadeNear", "fadeFar", "waveRadius", "waveWidth", "waveAhead", "clipBelow", "pulseSpacing",
                  "pulseSpeed", "pulseWidth"):
            if k in mw:
                mw[k] *= r

    co = scene.get("corruption")
    if co and "attractor.terrain" in co.get("targets", []):
        co["focus"] = _mul(co["focus"], r)
        for k in ("radius", "fieldNear", "fieldFar", "openNear", "openFar", "width", "height", "depth", "block",
                  "slip", "strip", "stripSlip", "pull", "lift", "fold", "glyphSize", "front", "floor"):
            if k in co:
                co[k] *= r
        if "light" in co:
            co["light"] *= r * r       # candela: the same light at the scaled distances

    if project is not None:
        for route in project.get("routes", []):
            if route["target"] == "corruption/slip":
                ch = route["chain"]
                ch["remapOutMin"] *= r
                ch["remapOutMax"] *= r
    return r


def pace(points, t0, t1, v0, v1, ramp_in=0.25, ramp_out=0.25):
    """Times for a path of 3D points (absolute), a smooth speed profile: v0 at t0, easing (smoothstep) to a held cruise,
    easing to v1 at t1. The cruise speed is solved so the path takes exactly t1 - t0. Returns [x, y, z, t] knots."""
    seg = [math.dist(points[i], points[i + 1]) for i in range(len(points) - 1)]
    total, T = sum(seg), t1 - t0
    a, b = ramp_in * T, ramp_out * T
    # distance = v0 a + (vm - v0) a/2 + vm (T - a - b) + v1 b + (vm - v1) b/2  ->  vm
    vm = (total - v0 * a / 2.0 - v1 * b / 2.0) / (T - a / 2.0 - b / 2.0)

    def speed(t):
        def ss(x):
            x = min(max(x, 0.0), 1.0)
            return x * x * (3.0 - 2.0 * x)
        v = v0 + (vm - v0) * ss(t / a) if a > 0 else vm
        if t > T - b:
            v = vm + (v1 - vm) * ss((t - (T - b)) / b)
        return v

    # integrate s(t) finely, then invert at each waypoint's arc length
    n = 4000
    s, ts = [0.0], [0.0]
    for i in range(1, n + 1):
        t = T * i / n
        s.append(s[-1] + 0.5 * (speed(t - T / n) + speed(t)) * T / n)
        ts.append(t)
    scale = total / s[-1]
    out, acc, j = [], 0.0, 0
    for k, p in enumerate(points):
        if k > 0:
            acc += seg[k - 1]
        while j < n and s[j + 1] * scale < acc:
            j += 1
        f = 0.0 if j >= n else (acc - s[j] * scale) / max((s[j + 1] - s[j]) * scale, 1e-9)
        out.append([p[0], p[1], p[2], t0 + ts[j] + f * (T / n)])
    out[-1][3] = t1
    return out, vm


# M3b: the gathering round the fire, in real metres from it (down-canyon, toward the bank). The night camera arcs on
# the river side (-lat) at eye height; the Gray stands across the fire from the three, facing them, so it reads in
# profile and three-quarter front as the camera turns; the craft stands behind the group on the opened bench.
CAST = {
    "sasquatch-a": (-2.1, 1.7),    # standing
    "sasquatch-b": (0.45, 2.85),   # crouching by the fire (M3e, owner: no character in the fire -- 2.9 m out, not 2.0)
    "sasquatch-c": (-2.55, -0.45),  # sitting on a rock
    "gray": (2.6, -0.9),
}
GATHER_AT = (sum(v[0] for v in CAST.values()) / len(CAST), sum(v[1] for v in CAST.values()) / len(CAST))


# M3g (owner, 54- §1, a standing rule: the camera looks where it is going): every portal flight's gaze follows the path
# -- its look-ahead short (0.35 s, not 1.2 s, which led the curves by up to 60 degrees), barely below the line, and a
# climb looked at in full (ADR-1909)
GAZE = {"lookAhead": 0.35, "lookDown": 0.02, "climbLook": 1.0}
