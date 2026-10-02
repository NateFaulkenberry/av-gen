#!/usr/bin/env python3
"""Build the Sonic VFX live scenes (02-brief-vfx-expansion.md, deliverables 10 and 17).

    python3 -m tools.sonic_vfx.make                 # every scene, and the Examples menu entries
    python3 -m tools.sonic_vfx.make event_horizon   # one scene (no index change)
    python3 -m tools.sonic_vfx.make --out <dir>     # write elsewhere (look development)

Writes examples/sonic-vfx/<id>.json and <id>.scene.json per scene. SONIC_VFX_ENGINE=legacy builds against the
first pin's signals (no response model); see signals.py.
"""
import argparse
import importlib
import os
import sys

if __package__ in (None, ""):
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    __package__ = "tools.sonic_vfx"

from tools.sonic_vfx import kit, signals  # noqa: E402
from tools.sonic_vfx.scenes import SCENES  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenes", nargs="*", help="module names (default: all)")
    ap.add_argument("--out", default=kit.OUT_DIR)
    ap.add_argument("--no-index", action="store_true")
    a = ap.parse_args()
    names = a.scenes or SCENES
    built = []
    for name in names:
        mod = importlib.import_module("tools.sonic_vfx.scenes." + name)
        sc = mod.build()
        pp, sp = sc.write(a.out)
        built.append(sc)
        print(f"{sc.id:22s} {len(sc.nodes):3d} nodes {len(sc.effects):2d} effects {len(sc.routes):4d} routes "
              f"{len(sc.mappings) + len(sc.mappings2):3d} mappings  -> {os.path.relpath(pp, kit.REPO)}")
    if not a.scenes and not a.no_index and a.out == kit.OUT_DIR:
        mods = [importlib.import_module("tools.sonic_vfx.scenes." + n) for n in SCENES]
        kit.write_index([m.build() for m in mods])
        print("examples/index.json: the Sonic VFX category lists", len(mods), "scenes")
    print("signals:", signals.ENGINE)


if __name__ == "__main__":
    main()
