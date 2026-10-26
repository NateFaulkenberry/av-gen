#!/usr/bin/env python3
"""Talking Heads: build a look-development variant of the scene into a directory (not the repository's examples).

    python3 tools/talking_heads/build_variant.py --out DIR [--neutral]

`--neutral`: every slot's palette grey (fx1, 14-brief-secondary-fx.md section 10: a source must be recognisable from
its secondary effects alone; pair it with review.py --no-beams). The directory gets a `meshes` link to the repository's.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, REPO)
from tools.sonic_vfx import kit  # noqa: E402
from tools.sonic_vfx.scenes import talking_heads as th  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--neutral", action="store_true")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    sc = th.build(neutral=a.neutral)
    pp, _ = sc.write(a.out)
    link = os.path.join(a.out, "meshes")
    if not os.path.exists(link):
        os.symlink(os.path.join(kit.ABSTRACT_DIR, "meshes"), link)
    print(pp)


if __name__ == "__main__":
    main()
