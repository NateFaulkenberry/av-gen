"""Ground and water heights of the Glowmere world, asked of the engine rather than re-derived.

A camera placed "1.8 m above the ground" has to know where the ground is, and the only faithful
answer is the engine's own `WorldMap::height` -- a Python copy of the terrain noise would drift from
it silently. So this asks `avgen_world_preview` for probes and caches the answers per world.
"""

import hashlib
import json
import os
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOL = ROOT / "build" / "release" / "tools" / "avgen_world_preview"
CACHE_DIR = ROOT / "build" / "gv3"


class Ground:
    def __init__(self, world_json):
        """`world_json` is the terrain node's `world` block."""
        self.world = world_json
        text = json.dumps(world_json, sort_keys=True)
        self.key = hashlib.sha256(text.encode()).hexdigest()[:16]
        CACHE_DIR.mkdir(parents=True, exist_ok=True)
        self.world_file = CACHE_DIR / f"world-{self.key}.json"
        if not self.world_file.exists():
            self.world_file.write_text(json.dumps({"world": world_json}))
        self.cache_file = CACHE_DIR / f"ground-{self.key}.json"
        self.cache = json.loads(self.cache_file.read_text()) if self.cache_file.exists() else {}

    @staticmethod
    def _k(x, z):
        return f"{x:.2f},{z:.2f}"

    def probe(self, points):
        """[(x, z)] -> [(height, water or None)], asking the engine for whatever is not cached."""
        missing = [p for p in points if self._k(*p) not in self.cache]
        if missing:
            if not TOOL.exists():
                raise RuntimeError(f"{TOOL} is not built (cmake --build build/release --target avgen_world_preview)")
            for i in range(0, len(missing), 2000):
                chunk = missing[i:i + 2000]
                args = [a for (x, z) in chunk for a in (f"{x:.3f}", f"{z:.3f}")]
                out = subprocess.run([str(TOOL), str(self.world_file), str(CACHE_DIR / "probe.png"), "16"] + args,
                                     capture_output=True, text=True, check=True).stdout
                found = re.findall(r"probe \([^)]*\): height (-?[\d.]+) .*? water (\S+)", out)
                if len(found) != len(chunk):
                    raise RuntimeError(f"expected {len(chunk)} probes, parsed {len(found)}")
                for (x, z), (h, w) in zip(chunk, found):
                    self.cache[self._k(x, z)] = [float(h), None if w == "none" else float(w)]
            self.cache_file.write_text(json.dumps(self.cache))
        return [tuple(self.cache[self._k(*p)]) for p in points]

    def height(self, x, z):
        return self.probe([(x, z)])[0][0]

    def above(self, x, z, metres):
        """A point `metres` above the ground -- or above the water, where there is water."""
        h, w = self.probe([(x, z)])[0]
        top = max(h, w) if w is not None else h
        return [float(x), top + metres, float(z)]
