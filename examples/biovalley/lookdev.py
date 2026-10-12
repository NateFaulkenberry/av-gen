"""A look-development scene: generated meshes in a row on a dark plane, under the valley's sky and moon, so a
species or a massif can be judged on its own before it is placed.

    python3 examples/biovalley/lookdev.py <name> <spacing> <eye_dist> <eye_height> <fov> <glb>[:<color>] ...

writes shots/lookdev-<name>.{scene,}.json. Colours: rock, bark, leaf, cap.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import valley  # noqa: E402

COLORS = {"rock": valley.mat(valley.ROCK, 0.85), "bark": valley.mat([0.03, 0.028, 0.03], 0.8),
          "leaf": valley.mat([0.02, 0.05, 0.04], 0.7), "cap": valley.mat([0.05, 0.1, 0.1], 0.4),
          "glowcap": valley.mat([0.02, 0.06, 0.06], 0.4, [0.25, 1.0, 0.8], 0.8, doubleSided=True),
          "glowthread": valley.mat([0.02, 0.04, 0.06], 0.4, [0.3, 0.8, 1.0], 1.5, doubleSided=True),
          "glowstalk": valley.mat([0.03, 0.08, 0.06], 0.5, [0.2, 0.9, 0.5], 0.3),
          "capprog": valley.mat([0.02, 0.06, 0.06], 0.4, [0.12, 1.0, 0.38], 0.32, program="cap", doubleSided=True),
          "stalk2": valley.mat([0.02, 0.05, 0.045], 0.5, [0.12, 1.0, 0.38], 0.06, opacity=0.85)}


def main():
    name, spacing, dist, height, fov = sys.argv[1], *map(float, sys.argv[2:6])
    items = sys.argv[6:]
    import numpy as np
    from geom import Mesh, grid_faces
    g = Mesh()
    xs = np.linspace(-8000, 8000, 3)
    X, Z = np.meshgrid(xs, xs, indexing="ij")
    g.add(np.stack([X, np.zeros_like(X), Z], -1).reshape(-1, 3), grid_faces(3, 3, False))
    g.write(valley.MESHES / "lookdev_ground.glb", "ground", outward_from=lambda P: np.tile([0, 1.0, 0], (len(P), 1)))
    nodes = [valley.mesh_node("ground", "lookdev_ground", [[0, 0, 0, 0, 0, 0, 1, 1, 1, 1]],
                              valley.mat(valley.GROUND, 0.95))]
    n = len(items)
    for k, it in enumerate(items):
        glb, _, col = it.partition(":")
        x = (int(glb.split("@")[1]) if "@" in glb else k - (n - 1) / 2) * spacing
        glb = glb.split("@")[0]
        node = valley.mesh_node(f"m{k}", glb, [[x, 0, 0, 0, 0, 0, 1, 1, 1, 1]], COLORS[col or "rock"])
        nodes.append(node)
    eye = [0.0, height, -dist]
    sc = valley.scene({}, eye, [0.0, height * 0.6, 0.0], fov, 0.05, 26000.0)
    sc["nodes"] = [nd for nd in sc["nodes"] if nd["name"] in ("moon",)] + nodes
    import life
    sc["materialPrograms"] = life.programs()
    (valley.SHOTS / f"lookdev-{name}.scene.json").write_text(json.dumps(sc))
    (valley.SHOTS / f"lookdev-{name}.json").write_text(json.dumps(valley.project(name, f"lookdev-{name}.scene.json")))


if __name__ == "__main__":
    main()
