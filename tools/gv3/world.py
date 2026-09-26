"""Fixes to the world Glowmere Valley 3 inherits, each a measured defect rather than a taste.

  * The composition's focal point "elder" sat at (-1, 4.5, -46), 98 m from the elder it names --
    and it is where the light rig's `elder-practical` point light is placed. It lit an empty field.
  * Hero stems were seated by one height probe at their centre, so on a slope the downhill side of
    the stem's base floats: scree by 13 cm, cairn by 4 cm. Each is re-seated on the lowest ground
    under its base ring, sunk a little, so no side of the tube's open end shows.
  * The elder's own spores were switched off in the source; they are its light made visible.
"""

import math

SINK = 0.10  # metres a stem's lowest ground-contact point is sunk below the surface


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
