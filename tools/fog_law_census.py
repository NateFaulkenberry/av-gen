#!/usr/bin/env python3
"""Which of the engine's two fog laws each shipped scene uses (ADR-569).

    python3 tools/fog_law_census.py            # the decision-relevant census
    python3 tools/fog_law_census.py --all      # ...including `_`-prefixed probe scenes
    python3 tools/fog_law_census.py --present  # ...counting a key's PRESENCE, not its value

**This file exists because two people ran "the same census" and got different answers.** One
counted 120 scenes and the other 90; one found 0 scenes using the march alone and the other found
1. Neither was wrong -- they had asked two different questions, and the questions differed on two
axes that neither command made visible:

  1. **What counts as shipped.** `examples/` holds 120 tracked `*.scene.json` files and **30 of
     them are `_`-prefixed probe scenes** written by various agents' diagnostic tooling. A census
     that includes them is answered in part by its own instrumentation.
  2. **What counts as using a law.** `"fogDensity": 0.0` has the key and does not use the law.
     Counting presence gives 57/57/0/6; counting a non-zero value gives 30/23/1/36. Both are true
     statements about different things.

So the census lives in a file rather than in a shell, and the ADR cites the file. A number in a
document that cannot be reproduced by someone who has neither author's shell is a number that will
eventually be disbelieved for a reason that has nothing to do with its subject.

The two laws, from ADR-569:
  surface   `Environment::fogDensity`, applied as exp(-(distance * density)^2) to lit surfaces
  march     `Environment::volumeDensity`, applied as Beer-Lambert through the volumetric raymarch

**ADR-705 unified them** (ADR-569's option 2): `fogDensity` is gone and the surface pass is the
march's own air under the march's own law, carried past `volumeMaxDistance`. After it, "both" and
"surface only" should read 0 -- a non-zero count there is a scene still carrying the removed key --
and the ADR-705 lines below say how the one law is split between the two passes.
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path


def tracked_scenes(root: Path) -> list[Path]:
    """git-tracked `*.scene.json` under `examples/`. Tracked rather than globbed on purpose: an
    untracked file in a worktree is somebody's scratch and is not shipped by anyone."""
    out = subprocess.run(["git", "-C", str(root), "ls-files", "examples"],
                         capture_output=True, text=True, check=True).stdout.split()
    return [root / p for p in out if p.endswith(".scene.json")]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--all", action="store_true",
                    help="include `_`-prefixed probe scenes (30 of the 120)")
    ap.add_argument("--present", action="store_true",
                    help="count a key's presence rather than a non-zero value")
    args = ap.parse_args()

    root = Path(__file__).resolve().parent.parent
    files = tracked_scenes(root)
    if not args.all:
        files = [f for f in files if not f.name.startswith("_")]

    both = surface = march = neither = 0
    analytic_only = marched = 0   # ADR-705: volumeDensity > 0 with and without a march to carry it
    march_only: list[str] = []
    for f in files:
        try:
            env = (json.loads(f.read_text()).get("environment") or {})
        except Exception:
            continue
        if args.present:
            a, b = "fogDensity" in env, "volumeDensity" in env
        else:
            fa, fb = env.get("fogDensity"), env.get("volumeDensity")
            a = isinstance(fa, (int, float)) and fa > 0
            b = isinstance(fb, (int, float)) and fb > 0
        vd = env.get("volumeDensity")
        if isinstance(vd, (int, float)) and vd > 0:
            md = env.get("volumeMaxDistance", 200.0)
            if isinstance(md, (int, float)) and md <= 0:
                analytic_only += 1
            else:
                marched += 1
        if a and b:
            both += 1
        elif a:
            surface += 1
        elif b:
            march += 1
            march_only.append(f.name)
        else:
            neither += 1

    branch = subprocess.run(["git", "-C", str(root), "rev-parse", "--abbrev-ref", "HEAD"],
                            capture_output=True, text=True).stdout.strip()
    scope = "all tracked scenes" if args.all else "shipped scenes (no `_` probes)"
    test = "key present" if args.present else "value > 0"
    print(f"branch {branch}, {scope}, {test}")
    print(f"  total                      {len(files)}")
    print(f"  both laws active           {both}")
    print(f"  surface law only           {surface}")
    print(f"  march law only             {march}")
    print(f"  neither                    {neither}")
    if march_only and len(march_only) <= 5:  # after ADR-705 every fogged scene is here
        print(f"  march-only scenes: {', '.join(sorted(march_only))}")
    # The migration cost ADR-569's option 2 is costed against: every scene whose SURFACE law is
    # live would need a new density chosen, and the surface-only ones have no volumetric number to
    # derive it from.
    print(f"  scenes whose surface law is live (option 2's migration): {both + surface}"
          f"  -- of which {surface} have no volumetric density to derive one from")
    print(f"  ADR-705, one law: fogged scenes {analytic_only + marched} -- {marched} marched (then integrated "
          f"past volumeMaxDistance), {analytic_only} integrated only (volumeMaxDistance 0)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
