"""Fixes to the world Glowmere Valley 3 inherits, each a measured defect rather than a taste.

  * The composition's focal point "elder" sat at (-1, 4.5, -46), 98 m from the elder it names --
    and it is where the light rig's `elder-practical` point light is placed. It lit an empty field.
  * Hero stems were seated by one height probe at their centre, so on a slope the downhill side of
    the stem's base floats: scree by 13 cm, cairn by 4 cm. Each is re-seated on the lowest ground
    under its base ring, sunk a little, so no side of the tube's open end shows.
  * The elder's own spores were switched off in the source; they are its light made visible.
  * The valley ran out through both ends of the world (`close_ends`, below).
"""

import copy
import math

SINK = 0.10  # metres a stem's lowest ground-contact point is sunk below the surface


# ---- the valley's ends ------------------------------------------------------------------------------
#
# The WorldMap is a 640 m square and the terrain mesh stops at its chunk grid, [-320, 352] on both
# axes (terrain.cpp chunkGrid). The valley's corridor, river and banks all ran to +-352, so at both
# ends the flattened valley floor reached the edge at 10-20 m (north) and -7..+5 m (south) between
# walls of 90-110 m, and every wide looking up or down the valley met a flat line of sky -- below
# eye level in the south (docs/glowmere-valley-3/revision/phase3/world.md has the measurements).
#
# Ridges alone cannot close it. A world height is `mix(noise + raises, flattenTarget, flattenWeight)`
# (world_map.cpp heightUncached), and within ~54 m of the river the banks (flatten 0.8) and the
# corridor (0.55) together weigh at least 1: any ridge crossing the river is erased there. So each end
# is closed by changing what flattens it, and every change is placed where it cannot reach anything
# the film already shows:
#
#   * A path's control point P_j reshapes its smoothed curve from mid(P_j-2, P_j-1) to
#     mid(P_j+1, P_j+2) (3 passes of Chaikin), and a point's level only reads the curve within 10 m of
#     its nearest point. Every edit below is made at or beyond the path's second point from the end,
#     so the curve through the filmed valley is the same curve, float for float.
#   * The corridor (300 m wide) is never touched: its far end reaches the north-west corner, where
#     the map's surveyed maximum lies.
#   * The terrain material and every biome read altitude as (h - min) / (max - min) over a 97 x 97
#     survey of the map (WorldMap::prepare). If either extreme moved, every plant's biome weight would
#     move with it. The minimum is the river bed at (19.8, 316.7), the maximum the north-west rim at
#     (-310.1, -296.9). `python3 tools/gv3/world.py --check` re-measures both, and no new feature
#     reaches either.
#
# North: the banks' level climbs beyond their second point, and a head ridge with a lower shoulder
# spans the V between the walls. The river keeps its course to the edge -- it has to: its water, edge
# to edge, is what keeps the two banks apart for the navigator (maxSlope 0.55, ~63 degrees, is
# walkable, so only water divides the valley). Ending it in a pool inside the world joined the halves
# (49% of walkable cells unreachable -> 0%), and every alien took a different route from 13 s on,
# though not one of their heights changed. Its head is bent east instead, so the gorge it cuts
# through the head turns out of every line of sight up the valley.
# South: the banks end at their eleventh point (z 244), so beyond it only the corridor flattens
# (0.55) and a sill across the end stands at 45% of its height; the river keeps its whole course and
# leaves through a gorge in the sill that turns away from every camera looking south.
#
# What this cannot help moving (and the iteration log measures): scatter rows run north to south
# (ecology.cpp), and a plant's hue jitter, glow and whether it stays dark are keyed on its index in its
# layer (procedural.cpp materialVariation), so a changed count in the northern rows re-deals those
# for every plant south of them. Positions, sizes and yaws are keyed on the cell and do not move.
NORTH_BANKS_HEAD = [[-44.0, 70.0, -352.0], [-52.0, 62.0, -322.0]]  # replaces the banks' first point
NORTH_RIVER_HEAD = [40.0, 15.0, -350.0]  # replaces the river's first point, (-44, 15, -352)
NORTH_HEAD = {"name": "north-head", "kind": "ridge",
              "path": [[-140.0, 0.0, -300.0], [-90.0, 0.0, -306.0], [-40.0, 0.0, -306.0], [10.0, 0.0, -300.0],
                       [50.0, 0.0, -292.0]],
              "width": 70.0, "amplitude": 70.0, "falloff": 1.0, "roughness": 1.0, "smoothing": 3}
NORTH_SHOULDER = {"name": "north-shoulder", "kind": "ridge",
                  "path": [[60.0, 0.0, -300.0], [100.0, 0.0, -306.0], [130.0, 0.0, -310.0]],
                  "width": 50.0, "amplitude": 30.0, "falloff": 1.0, "roughness": 1.0, "smoothing": 3}
SOUTH_BANKS_POINTS = 11  # the banks end at (45, -4.6, 244)
SOUTH_SILL = {"name": "south-sill", "kind": "ridge",
              "path": [[-110.0, 0.0, 338.0], [-40.0, 0.0, 342.0], [40.0, 0.0, 340.0], [120.0, 0.0, 336.0],
                       [200.0, 0.0, 328.0]],
              "width": 60.0, "amplitude": 70.0, "falloff": 1.0, "roughness": 1.0, "smoothing": 3}


def _feature(world, name):
    for f in world["features"]:
        if f["name"] == name:
            return f
    raise RuntimeError(f"the world has no feature '{name}': the valley this closes has changed")


def close_ends(world, report):
    """Close both ends of the valley in the terrain's `world` block. Call before any height is
    asked of the world (ground.py), so every probe sees the closed valley."""
    banks = _feature(world, "river-banks")
    river = _feature(world, "glowmere-run-2")
    # The edits assume the multicam's valley: 13 points from z -352 to +352 on each of the three.
    for f in (banks, river, _feature(world, "valley-corridor")):
        if len(f["path"]) != 13 or f["path"][0][2] != -352.0 or f["path"][-1][2] != 352.0:
            raise RuntimeError(f"feature '{f['name']}' is not the 13-point valley course close_ends was measured on")
    if any(f["name"] in (NORTH_HEAD["name"], NORTH_SHOULDER["name"], SOUTH_SILL["name"]) for f in world["features"]):
        raise RuntimeError("the valley's ends are already closed")
    banks["path"] = [list(p) for p in NORTH_BANKS_HEAD] + banks["path"][1:SOUTH_BANKS_POINTS]
    river["path"][0] = list(NORTH_RIVER_HEAD)
    world["features"].extend(copy.deepcopy([NORTH_HEAD, NORTH_SHOULDER, SOUTH_SILL]))
    report.append("valley ends closed: north head ridge and shoulder, the river's head bent east through a gorge; "
                  "south sill, the banks ending at z 244")


def mushrooms(scene):
    """{name: [nodes of that mushroom]} for the generated hero mushrooms (cap/under/stem/gills)."""
    out = {}
    for node in scene["nodes"]:
        for part in ("-cap", "-under", "-stem", "-gills"):
            if node["name"].endswith(part) and node.get("kind") == "procedural":
                src = node.get("procedural", {}).get("source", {})
                if src.get("kind") == "generated":
                    out.setdefault(node["name"][: -len(part)], []).append(node)
    return out


def stem_base_radius(stem_node):
    """The stem's base radius in metres: the generator's clamp(capRadius * 0.16, 0.02, 0.30) in its
    unit frame (src/organism/mushroom.cpp), times the node's uniform scale."""
    proc = stem_node["procedural"]
    values = proc["source"]["generated"]["values"]
    scale = proc.get("sourceTransform", {}).get("scale", [1.0, 1.0, 1.0])[0]
    cap = max(values[0], 0.05)
    return min(max(cap * 0.16, 0.02), 0.30) * scale


def seat_heroes(project, scene, ground, report):
    """Seat every hero mushroom on the lowest ground under its stem's base ring.

    The project's `nodes/<name>/position` parameters are applied over the scene's positions
    (ADR-264), and the source project carries one for every part of every hero -- the elder's stem
    among them at a different height from its scene node. So the seat is computed from the position
    that actually renders and written to both, or one of the two would silently undo the other.
    """
    params = project["parameters"]

    def effective(node):
        return params.get(f"nodes/{node['name']}/position", node["position"])

    for name, nodes in sorted(mushrooms(scene).items()):
        stem = next((n for n in nodes if n["name"].endswith("-stem")), None)
        if stem is None:
            continue
        x, y, z = effective(stem)
        r = stem_base_radius(stem)
        ring = [(x + r * math.cos(2 * math.pi * k / 16), z + r * math.sin(2 * math.pi * k / 16)) for k in range(16)]
        lowest = min(h for h, _ in ground.probe(ring))
        seat = lowest - SINK
        if y > seat + 1e-3:
            drop = y - seat
            for n in nodes:
                px, py, pz = effective(n)
                moved = [px, round(py - drop, 4), pz]
                n["position"] = moved
                params[f"nodes/{n['name']}/position"] = moved
            report.append(f"{name}: lowered {drop:.3f} m so the lowest side of its {r:.2f} m stem base "
                          f"is {SINK:.2f} m under the ground (was {y - lowest:+.3f} m above it)")


def apply(project, scene, ground, report):
    comp = scene.setdefault("composition", {})
    for fp in comp.get("focalPoints", []):
        if fp.get("name") == "elder":
            fp["position"] = [-12.0, 12.0, 52.0]  # under the elder's cap, where its light belongs
            report.append("focal point 'elder' moved to the elder (it drives the elder-practical light)")
    seat_heroes(project, scene, ground, report)
    # The water's glowing patches are a threshold on a noise, not a parameter the project can
    # route: fewer of them, so the river has "something glowing under there" rather than being lit.
    for node in scene["nodes"]:
        if node.get("kind") == "terrain" and "water" in node.get("terrain", {}):
            node["terrain"]["water"]["glowCoverage"] = 0.1
            report.append("water glow coverage 0.22 -> 0.10")
            # Calm water concentrates the moon's glint into one bright path. Iteration 2 halved it and
            # it was still the brightest thing in every shot that faces the moon (s08, s25, s26, s28);
            # 0.25 keeps a path without a floodlit foreground. Not a project parameter, so it is set here.
            node["terrain"]["water"]["specular"] = 0.25
            report.append("water specular 1.2 -> 0.25")
    project["parameters"]["nodes/elder-2-spores/visible"] = True


# ---- measuring the world: the heightfield, skylines and the open-end check --------------------------
#
# Everything here asks the engine (`avgen_world_preview` probes) and never re-derives the terrain, the
# same rule as ground.py. The heightfield is the drawn terrain's extent on a 2 m grid, cached per world
# under build/gv3/, and every ray below is marched over it.

GRID_LO, GRID_HI, GRID_STEP = -320.0, 352.0, 2.0  # the terrain mesh's chunk grid (48 m chunks)
EDGE_BAND = 6.0      # metres inside the grid's boundary where a skyline counts as the edge
OPEN_RISE = 0.2      # below this rise per metre, ground cut by the boundary reads as land running on
OPEN_ELEVATION = 5.0 # ...and below this many degrees above the eye, as the sky meeting the land
SURVEY = 97          # WorldMap::prepare's survey, points a side


def _probe_heights(world_json, points, cache_dir):
    """[(x, z)] -> [height], straight from the engine in batches."""
    import pathlib
    import re
    import subprocess
    root = pathlib.Path(__file__).resolve().parents[2]
    tool = root / "build" / "release" / "tools" / "avgen_world_preview"
    wf = cache_dir / "field-world.json"
    import json
    wf.write_text(json.dumps({"world": world_json}))
    out = []
    pattern = re.compile(r"probe \([^)]*\): height (-?[\d.]+) ")
    for i in range(0, len(points), 10000):
        chunk = points[i:i + 10000]
        args = [a for (x, z) in chunk for a in (f"{x:.6f}", f"{z:.6f}")]
        text = subprocess.run([str(tool), str(wf), str(cache_dir / "field-probe.png"), "16"] + args,
                              capture_output=True, text=True, check=True).stdout
        found = pattern.findall(text)
        if len(found) != len(chunk):
            raise RuntimeError(f"expected {len(chunk)} probes, parsed {len(found)}")
        out.extend(float(h) for h in found)
    return out


class Field:
    """The world's heights on the drawn terrain's extent, and rays over them."""

    def __init__(self, world_json):
        import hashlib
        import json
        import pathlib
        import numpy as np
        self.np = np
        self.world = world_json
        cache_dir = pathlib.Path(__file__).resolve().parents[2] / "build" / "gv3"
        cache_dir.mkdir(parents=True, exist_ok=True)
        key = hashlib.sha256(json.dumps(world_json, sort_keys=True).encode()).hexdigest()[:16]
        cache = cache_dir / f"field-{key}.npy"
        n = int(round((GRID_HI - GRID_LO) / GRID_STEP)) + 1
        if cache.exists():
            self.h = np.load(cache)
        else:
            xs = [GRID_LO + i * GRID_STEP for i in range(n)]
            self.h = np.array(_probe_heights(world_json, [(x, z) for z in xs for x in xs], cache_dir)).reshape(n, n)
            np.save(cache, self.h)
        self.cache_dir = cache_dir

    def at(self, x, z):
        np = self.np
        n = self.h.shape[0]
        fx = np.clip((np.asarray(x, dtype=float) - GRID_LO) / GRID_STEP, 0, n - 1.000001)
        fz = np.clip((np.asarray(z, dtype=float) - GRID_LO) / GRID_STEP, 0, n - 1.000001)
        ix, iz = np.floor(fx).astype(int), np.floor(fz).astype(int)
        tx, tz = fx - ix, fz - iz
        h = self.h
        return ((h[iz, ix] * (1 - tx) + h[iz, ix + 1] * tx) * (1 - tz) +
                (h[iz + 1, ix] * (1 - tx) + h[iz + 1, ix + 1] * tx) * tz)

    def survey(self):
        """The heights WorldMap::prepare surveys, asked of the engine at its own points."""
        lo, size = -320.0, 640.0
        pts = [(lo + size * ((i + 0.5) / SURVEY), lo + size * ((j + 0.5) / SURVEY))
               for j in range(SURVEY) for i in range(SURVEY)]
        return pts, _probe_heights(self.world, pts, self.cache_dir)

    def ray(self, eye, yaw, step=2.0, reach=1200.0):
        """(distances, heights) of the ground along a horizontal bearing, to the drawn edge."""
        np = self.np
        d = np.arange(step, reach, step)
        x, z = eye[0] + np.cos(yaw) * d, eye[2] + np.sin(yaw) * d
        inside = (x >= GRID_LO) & (x <= GRID_HI) & (z >= GRID_LO) & (z <= GRID_HI)
        if not inside.any():
            return d[:0], d[:0]
        last = int(np.nonzero(inside)[0].max()) + 1
        return d[:last], self.at(x[:last], z[:last])

    def open_ends(self, eye, target, vfov_deg, aspect=16 / 9, cols=96):
        """Columns of this view whose skyline is the world's edge on gentle, low ground: the valley
        running on out of the world. [(column, x, z, height, elevation)]."""
        np = self.np
        fx, fy, fz = (target[k] - eye[k] for k in range(3))
        yaw0 = math.atan2(fz, fx)
        pitch = math.degrees(math.atan2(fy, math.hypot(fx, fz)))
        half_v = vfov_deg / 2
        half_h = math.degrees(math.atan(math.tan(math.radians(half_v)) * aspect))
        found = []
        for c in range(cols):
            off = half_h * (2 * (c + 0.5) / cols - 1)
            yaw = yaw0 + math.radians(off)
            d, h = self.ray(eye, yaw)
            if len(d) == 0:
                continue
            ang = np.degrees(np.arctan2(h - eye[1], d))
            k = int(np.argmax(ang))
            if d[k] < d[-1] - EDGE_BAND:
                continue  # a crest inside the world: the edge is behind it
            if not (pitch - half_v * math.cos(math.radians(off)) <= ang[k] <= pitch + half_v * math.cos(math.radians(off))):
                continue  # the skyline is out of frame
            back = self.at(eye[0] + math.cos(yaw) * (d[k] - 20.0), eye[2] + math.sin(yaw) * (d[k] - 20.0))
            rise = (h[k] - float(back)) / 20.0
            if rise < OPEN_RISE and ang[k] < OPEN_ELEVATION:
                found.append((c, float(eye[0] + math.cos(yaw) * d[k]), float(eye[2] + math.sin(yaw) * d[k]),
                              float(h[k]), float(ang[k])))
        return found


def camera_poses(project, scene, per_shot=5):
    """[(shot label, time, eye, target, vfov)] for every locked shot whose rig is fixed or keyed
    (followed and aimed rigs need a cast trace: see --trace)."""
    cameras = {c["id"]: c for c in scene["cameraDirection"]["cameras"]}
    keys = {t["target"]: t["keys"] for t in project["timeline"]["tracks"] if t["target"].startswith("cameras/")}

    def sample(track, t, default):
        if not track:
            return default
        if t <= track[0]["time"]:
            return track[0]["value"]
        for a, b in zip(track, track[1:]):
            if a["time"] <= t <= b["time"]:
                u = (t - a["time"]) / max(b["time"] - a["time"], 1e-9)
                if a.get("interp") == "easeInOut":
                    u = u * u * (3 - 2 * u)
                return [a["value"][k] + u * (b["value"][k] - a["value"][k]) for k in range(3)]
        return track[-1]["value"]

    out = []
    for shot in scene["cameraDirection"]["shots"]:
        cam = cameras[shot["camera"]]
        if cam.get("followNode") or cam.get("aimNode"):
            continue
        slug = cam["slug"]
        for i in range(per_shot):
            t = shot["start"] + (shot["end"] - shot["start"]) * (i + 0.5) / per_shot
            out.append((slug, t, sample(keys.get(f"cameras/{slug}/position"), t, cam["position"]),
                        sample(keys.get(f"cameras/{slug}/target"), t, cam["target"]), cam["fov"]))
    return out


def trace_poses(trace_path, every=30):
    """[(shot index, time, eye, target, vfov)] from `avgen_cast_trace --camera`, every `every` frames."""
    import json
    cam = json.loads(open(trace_path).read())["camera"]
    return [(cam["camera"][i] or f"shot {cam['shot'][i]}", cam["t"][i], cam["eye"][i], cam["target"][i], cam["vfov"][i])
            for i in range(0, len(cam["t"]), every)]


def check(source_world, world, project, scene, trace=None):
    """The closed valley's guarantees, measured. Returns (lines, ok)."""
    lines, ok = [], True
    before, after = Field(source_world), Field(world)
    # 1. the altitude normalisation every biome reads
    _, h0 = before.survey()
    _, h1 = after.survey()
    for label, a, b in (("minimum", min(h0), min(h1)), ("maximum", max(h0), max(h1))):
        same = a == b
        ok &= same
        verdict = "(unchanged to the mm, the probe's precision)" if same else "(MOVED)"
        lines.append(f"survey {label}: {a:.3f} -> {b:.3f} {verdict}")
    # 2. the filmed valley: nothing between the two ends' edits moves, to the millimetre
    np = before.np
    zs = GRID_LO + np.arange(before.h.shape[0]) * GRID_STEP
    band = (zs >= -190.0) & (zs <= 170.0)
    worst = float(np.abs(after.h[band] - before.h[band]).max())
    ok &= worst == 0.0
    lines.append(f"ground from z -190 to 170: largest change {worst:.3f} m")
    # 3. the river still divides the valley. The navigator walks anything up to ~63 degrees
    # (maxSlope 0.55), so only water keeps the banks apart; a river that ends inside the world lets
    # every walker route around its head, and the aliens' choices change everywhere (phase3/world.md).
    river = next(f for f in world["features"] if f.get("water") and f["kind"] == "river")
    ends = (river["path"][0], river["path"][-1])
    outside = [not (GRID_LO < p[0] < GRID_HI and GRID_LO < p[2] < GRID_HI) for p in ends]
    ok &= all(outside)
    lines.append(f"river '{river['name']}' runs edge to edge (the navigator's divide): "
                 f"{'yes' if all(outside) else 'NO -- an end lies inside the terrain'}")
    # 4. no open end in any sampled view
    poses = trace_poses(trace) if trace else camera_poses(project, scene)
    shots = {}
    for label, t, eye, target, vfov in poses:
        n = len(after.open_ends(eye, target, vfov))
        s = shots.setdefault(label, [0, 0, 0])
        s[0] += 1
        s[1] += bool(n)
        s[2] = max(s[2], n)
    bad = {k: v for k, v in shots.items() if v[1]}
    ok &= not bad
    lines.append(f"views sampled: {len(poses)} in {len(shots)} shots ({'every frame of a trace, sampled' if trace else 'fixed and keyed rigs'}); "
                 f"views with an open end: {sum(v[1] for v in shots.values())}")
    for k, v in sorted(bad.items()):
        lines.append(f"  {k}: an open end in {v[1]} of {v[0]} views (up to {v[2]} of 96 columns)")
    return lines, ok


if __name__ == "__main__":
    import argparse
    import json
    import pathlib
    import sys
    parser = argparse.ArgumentParser(description="Check the closed valley: the survey, the filmed ground, the edge")
    parser.add_argument("--check", action="store_true", help="run the checks (the default)")
    parser.add_argument("--trace", help="an avgen_cast_trace --camera JSON, to check every sampled frame")
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]
    source = json.loads((root / "examples/world/glowmere-valley-2-multicam.scene.json").read_text())
    scene = json.loads((root / "examples/world/glowmere-valley-3.scene.json").read_text())
    project = json.loads((root / "examples/world/glowmere-valley-3.json").read_text())

    def world_of(s):
        return next(n["world"] for n in s["nodes"] if "world" in n)

    report_lines, passed = check(world_of(source), world_of(scene), project, scene, args.trace)
    print("\n".join(report_lines))
    print("PASS" if passed else "FAIL")
    sys.exit(0 if passed else 1)
