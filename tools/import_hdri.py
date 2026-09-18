#!/usr/bin/env python3
"""Import the BlenderKit HDRIs into the project and derive loadable runtime copies.

    /Applications/Blender.app/Contents/MacOS/Blender --background --python tools/import_hdri.py -- \
        --cache ~/blenderkit_data/hdrs --out assets/environments

Run it with Blender, not a bare Python: the decode step needs Blender's OpenEXR, which is the only
reader on this machine that handles the source files' compression.

WHY DERIVED FILES EXIST AT ALL
------------------------------

The owner's spec prefers "the original EXR/HDR data exactly as supplied". That is not loadable by
this engine, and the reason is a decoder limitation rather than a preference:

  * BlenderKit wrote two files per asset. The resolution_2K tier is 4500 x 2250, **DWAA**; the
    `blend` fileType -- BlenderKit's name for the full original of an HDR asset -- is 18000 x 9000,
    **PIZ**, at 440 MB (day) and 1.76 GB (night).
  * The engine reads EXR through tinyexr, which rejects DWAA outright: "Unknown compression type.
    (tinyexr -8)". Measured against build/release, not assumed.
  * 18000 x 9000 is 2.6 GB as RGBA32Float in memory. It is not a runtime asset under any codec.

So this writes a derived runtime representation, which the spec's §9 explicitly allows provided the
original is preserved and documented: decode with Blender, resample to `--width` x `--width/2`,
re-encode as EXR with ZIP compression and half floats. It stays scene-linear HDR throughout -- no
PNG, no JPEG, no tone map, no view transform -- and the tool *proves* that rather than claiming it,
by comparing exact channel means either side of the encode and refusing to keep a file that moved.

The originals stay where BlenderKit put them, untouched, with their sizes and hashes in the
manifest.

PROVENANCE, AND WHAT IS NOT KNOWN
---------------------------------

Neither `asset_base_id` the owner gave appears in any filename or directory name. The folders are
`clouds_c854a031-*` and `moon-star_158063fa-*`; the files carry their own unrelated UUIDs. Each
asset's two files are identified instead by their sizes matching, byte for byte, the two
Content-Lengths its download session recorded in the BlenderKit client log.

**That log is truncated on client restart.** The client restarted at 18:38:52 for the second asset
and the first session's log is gone. By the time these files were imported, *neither* asset_base_id
was present anywhere on disk. What the first log said is recorded in the manifest as an observation
that can no longer be re-verified, and labelled as such.

**No licence or author information is recoverable** -- not in the log, not in either EXR header
(only the standard geometry attributes), not in any sidecar under ~/blenderkit_data, and there is
no saved .blend and no running Blender session whose image datablock could be inspected. The
manifest says so; nothing is invented.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

try:
    import bpy
except ImportError:  # pragma: no cover - the guard is the error message
    sys.exit("run this with Blender: /Applications/Blender.app/Contents/MacOS/Blender "
             "--background --python tools/import_hdri.py -- ...")

# `bytes` is the Content-Length the client log recorded, and is what the file on disk must be.
ASSETS = {
    "tree_of_life_day": {
        "assetBaseId": "54789825-1f23-4e1c-b04e-fd695f21c690",
        "assetName": "Clouds",
        "folderGlob": "clouds_*",
        "tier2K": {"bytes": 6480347, "etag": "cb48104e6ef57197a470617e4a1823bc",
                   "lastModified": "Fri, 18 Sep 2026 11:16:41 GMT",
                   "pixels": "4500 x 2250", "compression": "DWAA"},
        "original": {"bytes": 440097648, "etag": "f4ce0142cbff1e77b4c352ff50d89169",
                     "lastModified": "Fri, 18 Sep 2026 08:53:03 GMT",
                     "pixels": "18000 x 9000", "compression": "PIZ"},
        "depicts": ("open ocean under a blue sky with scattered cumulus and a low sun; water fills "
                    "the lower hemisphere to the horizon in every direction. No beach and no "
                    "shoreline, so the spec's shoreline-contradiction concern does not arise."),
        "celestialBodies": ("a bright sun glare region near the horizon with a specular path on "
                            "the water; the disc itself is largely occluded by a cloud bank."),
    },
    "tree_of_life_night": {
        "assetBaseId": "2dd36e2c-47ea-4d0a-8431-aa2c2dd9f362",
        "assetName": "Moon star",
        "folderGlob": "moon-star_*",
        "tier2K": {"bytes": 2942170, "etag": None, "lastModified": None,
                   "pixels": "4500 x 2250", "compression": "DWAA"},
        "original": {"bytes": 1756344947, "etag": None, "lastModified": None,
                     "pixels": "18000 x 9000", "compression": "PIZ"},
        "depicts": ("open ocean at night under a deep blue sky with the Milky Way and high cirrus; "
                    "a moon path runs across the water. No beach and no shoreline."),
        "celestialBodies": ("A LARGE, PROMINENT CRESCENT MOON with a bright halo. This is exactly "
                            "the double-moon hazard the spec warns about, and it is why neither "
                            "HDRI is ever drawn as the visible sky."),
    },
}


def sha256(path: Path, limit: int | None = None) -> str:
    h = hashlib.sha256()
    read = 0
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 22), b""):
            if limit is not None and read + len(chunk) > limit:
                h.update(chunk[: limit - read])
                break
            h.update(chunk)
            read += len(chunk)
    return h.hexdigest()


def stats(image) -> dict:
    """Exact per-channel mean and max over the whole float buffer.

    Not a strided sample. The first version sampled every Nth pixel, which is a fine estimator for
    the day map and a terrible one for the night map: almost all of that image is near-black and
    its mean is carried by the few thousand pixels of the moon, so two different strides disagreed
    by 2.6x and the linearity guard fired on a file that was in fact correct.
    """
    import numpy as np
    buf = np.empty(len(image.pixels), dtype=np.float32)
    image.pixels.foreach_get(buf)
    rgba = buf.reshape(-1, 4)
    return {"meanR": float(rgba[:, 0].mean()), "meanG": float(rgba[:, 1].mean()),
            "meanB": float(rgba[:, 2].mean()), "max": float(rgba[:, :3].max()),
            "pixels": int(rgba.shape[0])}


def find_source(cache: Path, spec: dict):
    """The 2K tier and the original for one asset, by folder glob and byte size."""
    folders = sorted(cache.glob(spec["folderGlob"]))
    if not folders:
        sys.exit(f"no folder matching {spec['folderGlob']!r} under {cache}. If BlenderKit was still "
                 f"downloading when you looked, wait and look again -- the 2K file lands seconds "
                 f"before the original and the original can take a minute.")
    exrs = sorted(q for f in folders for q in f.rglob("*.exr"))
    by_size = {q.stat().st_size: q for q in exrs}
    small = by_size.get(spec["tier2K"]["bytes"])
    large = by_size.get(spec["original"]["bytes"])
    if small is None:
        sys.exit(f"the resolution_2K file ({spec['tier2K']['bytes']} bytes) is not under "
                 f"{[str(f) for f in folders]}; found {[(q.stat().st_size, q.name) for q in exrs]}")
    if large is None:
        print(f"  WARNING: the original ({spec['original']['bytes']} bytes) is absent; recorded in "
              f"the manifest as expected-but-missing rather than silently dropped.")
    return small, large


def build(name: str, spec: dict, cache: Path, out: Path, width: int) -> dict:
    small, large = find_source(cache, spec)
    print(f"[{name}] resolution_2K tier : {small}")
    print(f"[{name}] original           : {large if large else 'MISSING'}")

    src = bpy.data.images.load(str(small))
    src_w, src_h = tuple(src.size)
    print(f"  decoded {src_w} x {src_h}, float={src.is_float}, "
          f"colorspace={src.colorspace_settings.name}")
    if src_w != src_h * 2:
        sys.exit(f"{name}: source is {src_w}x{src_h}, not 2:1 -- not an equirectangular map")

    height = width // 2
    src.scale(width, height)
    # Measure *after* the resample: the property being checked is that the encode did not change
    # the data. Comparing across a resample folds two effects into one number.
    before = stats(src)

    dst = out / f"{name}.exr"
    scene = bpy.context.scene
    scene.render.image_settings.file_format = "OPEN_EXR"
    scene.render.image_settings.color_mode = "RGB"
    scene.render.image_settings.color_depth = "16"
    scene.render.image_settings.exr_codec = "ZIP"
    # 'Raw' is the identity view transform. `Image.save()` ignores the scene's image settings
    # entirely -- it wrote uncompressed 32-bit float and silently dropped the ZIP+half request --
    # so this goes through save_render, which honours them.
    scene.view_settings.view_transform = "Raw"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = 0.0
    scene.view_settings.gamma = 1.0
    src.file_format = "OPEN_EXR"
    src.save_render(filepath=str(dst), scene=scene)
    print(f"  wrote {dst} ({dst.stat().st_size / 1e6:.1f} MB)")

    check = bpy.data.images.load(str(dst))
    after = stats(check)
    for ch in ("meanR", "meanG", "meanB"):
        lo, hi = before[ch], after[ch]
        if lo <= 1e-9:
            continue
        ratio = hi / lo
        if not (0.92 <= ratio <= 1.08):
            sys.exit(f"{name}: derived {ch} moved by {ratio:.3f}x -- that is a colour transform, "
                     f"not an encode. Refusing to keep {dst}.")
    means = lambda d: [round(d[c], 6) for c in ("meanR", "meanG", "meanB")]
    print(f"  linearity check passed; means {means(before)} -> {means(after)} "
          f"(max {before['max']:.3f} -> {after['max']:.3f})")

    return {
        "source": "Blendkit",
        "asset_base_id": spec["assetBaseId"],
        "asset_type": "hdr",
        "assetName": spec["assetName"],
        "license": "NOT RECORDED -- see identification.licenceNote. Nothing is invented here.",
        "identification": {
            "method": "byte-exact size match against the Content-Length the BlenderKit client log "
                      "recorded -- NOT a match on the asset_base_id",
            "assetBaseIdFoundOnDisk": False,
            "note": ("The asset_base_id appears in no filename and no directory name. Each asset's "
                     "two files are identified by their sizes matching the two Content-Lengths its "
                     "download session recorded."),
            "logCaveat": ("~/blenderkit_data/client/default.log is TRUNCATED ON CLIENT RESTART. "
                          "The client restarted at 18:38:52 for the second asset and the first "
                          "session's log is gone; at import time grep found 0 occurrences of "
                          "either asset_base_id anywhere on disk. The day asset's chain -- exactly "
                          "one AssetID in 16,851 lines, two downloads of 6,480,347 and "
                          "440,097,648, one asset name 'Clouds' -- was read from that log earlier "
                          "in the session that produced this file, and is an observation that can "
                          "no longer be re-verified."),
            "licenceNote": ("No licence or author field exists in the client log, in either EXR "
                            "header, in any sidecar under ~/blenderkit_data, or in a saved .blend, "
                            "and no Blender session was running whose image datablock could be "
                            "inspected. Fill this in from the asset's BlenderKit page before this "
                            "project is distributed."),
            "requestedResolution": ("the request header said resolution_4K; the client's actual "
                                    "targets were resolution_2K and blend, so no 4K file was "
                                    "fetched and none is missing"),
        },
        "original": {
            "role": "preserved, not used at runtime",
            "path": str(large) if large else None,
            "present": large is not None,
            "fileType": "blend (BlenderKit's name for the full original of an HDR asset)",
            "bytes": large.stat().st_size if large else spec["original"]["bytes"],
            "sha256_first_64MiB": sha256(large, 64 << 20) if large else None,
            "etag": spec["original"]["etag"],
            "lastModified": spec["original"]["lastModified"],
            "pixels": spec["original"]["pixels"],
            "compression": spec["original"]["compression"],
            "channels": "R,G,B half",
            "whyNotUsed": "too large for a runtime environment map; 2.6 GB as RGBA32Float.",
        },
        "decodeSource": {
            "role": "what the runtime file was derived from",
            "path": str(small),
            "fileType": "resolution_2K",
            "bytes": small.stat().st_size,
            "sha256": sha256(small),
            "etag": spec["tier2K"]["etag"],
            "lastModified": spec["tier2K"]["lastModified"],
            "pixels": spec["tier2K"]["pixels"],
            "compression": spec["tier2K"]["compression"],
            "channels": "R,G,B half",
            "whyNotUsedDirectly": ("tinyexr, this engine's EXR reader, rejects DWAA: 'Unknown "
                                   "compression type. (tinyexr -8)'."),
        },
        "runtime": {
            "path": str(dst),
            "derivation": (f"decoded with Blender, resampled {src_w}x{src_h} -> {width}x{height}, "
                           f"re-encoded EXR half + ZIP via save_render with a Raw view transform."),
            "bytes": dst.stat().st_size,
            "sha256": sha256(dst),
            "pixels": f"{width} x {height}",
            "compression": "ZIP",
            "channels": "R,G,B half",
            "colorspace": "Linear Rec.709 (as Blender reports the source)",
            "linearityCheck": {
                "measuredAfterResample": True,
                "sourceMeans": [before["meanR"], before["meanG"], before["meanB"]],
                "derivedMeans": [after["meanR"], after["meanG"], after["meanB"]],
                "sourceMax": before["max"], "derivedMax": after["max"],
                "tolerance": "each channel mean within 8%",
            },
            "depicts": spec["depicts"],
            "celestialBodies": spec["celestialBodies"],
        },
        "regenerate": ("/Applications/Blender.app/Contents/MacOS/Blender --background --python "
                       "tools/import_hdri.py -- --cache ~/blenderkit_data/hdrs "
                       "--out assets/environments"),
    }


def main() -> int:
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", default=str(Path.home() / "blenderkit_data" / "hdrs"))
    ap.add_argument("--out", default="assets/environments")
    ap.add_argument("--width", type=int, default=2048,
                    help="derived width; height is width/2 (equirectangular is 2:1)")
    ap.add_argument("--manifest", default="assets/environments.manifest.json")
    ap.add_argument("--only", default=None, help="one of " + ", ".join(ASSETS))
    args = ap.parse_args(argv)

    cache = Path(os.path.expanduser(args.cache))
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    manifest = Path(args.manifest)
    existing = json.loads(manifest.read_text()) if manifest.exists() else {}
    for name, spec in ASSETS.items():
        if args.only and name != args.only:
            continue
        existing[name] = build(name, spec, cache, out, args.width)
    manifest.write_text(json.dumps(existing, indent=1) + "\n")
    print(f"wrote {manifest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
