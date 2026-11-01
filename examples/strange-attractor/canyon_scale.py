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

WORLD_SCALE = 0.4


def canyon(d, lat):
    """The canyon frame used since Gate C 2: d metres down-canyon from the dusk exit, lat metres toward the camp bank.
    Both are in generator metres (scale 1); the result is in world metres."""
    return [WORLD_SCALE * (166.5 - 0.426 * d + 0.905 * lat), WORLD_SCALE * (3427.3 - 0.905 * d - 0.426 * lat)]


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
