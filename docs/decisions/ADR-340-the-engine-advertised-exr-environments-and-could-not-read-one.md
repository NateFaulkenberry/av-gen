# ADR-340: The engine advertised EXR environments and could not read one

Status: accepted
Date: 2026-09-18

## Context

Two BlenderKit HDRIs arrived for the Tree of Life world: a daytime map ("Clouds") and a night one
("Moon star"). The brief wanted them owned by the project, used as *environmental illumination*
while the procedural sky stays the visible sky, and crossfaded by the day/night cycle.

## What was found

### 1. `.exr` was a format the engine claimed and could not load

`src/assets/asset_catalog.cpp` has classified `.exr` as an `"environment"` for as long as it has
existed. `loadImage` routed every file through stb_image, which has no EXR decoder. So
`--env map.exr`, and a scene `environment.map` pointing at an EXR, both came back
`unknown image type` — and it was fatal, not a degradation:

```
[error] environment: failed to decode image '…/tree_of_life_day.exr': unknown image type
[error] initialisation failed: …
```

`readExr` (tinyexr) already existed in the same module and already returned the same
`scene::TextureData` that `loadImage` returns. The fix is to route `.exr` to it, case-insensitively.

### 2. tinyexr rejects the compression BlenderKit ships

With the routing fixed, the next layer appeared: `Unknown compression type. (tinyexr -8)`.
BlenderKit's `resolution_2K` tier is **DWAA**-compressed, which tinyexr does not decode. The full
original (`blend` fileType) is **PIZ** — readable in principle, but 18000 × 9000, 440 MB for the day
map and **1.76 GB** for the night one, and 2.6 GB as RGBA32Float. Not a runtime asset.

So `tools/import_hdri.py` writes a **derived runtime representation**, which the spec's §9 allows
provided the original is preserved and documented: decode with Blender, resample 4500 × 2250 →
2048 × 1024, re-encode EXR **half + ZIP**. 5.7 MB and 4.2 MB. It stays scene-linear throughout, and
the tool proves it rather than claiming it — exact channel means either side of the encode, refusing
to keep a file that moved:

```
day    means [0.293811, 0.343158, 0.443047] -> [0.293809, 0.343165, 0.443049]  max 29520.7 -> 29520.0
night  means [0.238935, 0.216888, 0.217789] -> [0.238938, 0.216887, 0.217790]  max 33032.4 -> 33024.0
```

Two mistakes in that tool are worth keeping, because both produced a plausible file:

* `Image.save()` **silently ignores** the scene's image settings. It wrote uncompressed 32-bit
  float — 25 MB, and both maps came out byte-identical in *size*, which is what gave it away.
  `save_render(scene=…)` with a `Raw` view transform honours the codec.
* The linearity check first sampled every Nth pixel. That is a fine mean estimator for the day map
  and a 2.6×-wrong one for the night map, whose mean is carried by the few thousand pixels of the
  moon. It fired on a file that was correct. It now measures exactly, over the whole buffer, and
  *after* the resample so the comparison isolates the encode.

### 3. Provenance is an inference, and the evidence for it has since been destroyed

Neither `asset_base_id` the owner supplied appears in any filename or directory: the folders are
`clouds_c854a031-*` and `moon-star_158063fa-*` and the files carry their own unrelated UUIDs. The
identification rests on the BlenderKit client log — one AssetID, two downloads whose Content-Lengths
match the two files byte for byte, one asset name.

**That log is truncated when the client restarts.** It restarted at 18:38:52 for the second asset,
and by the time the assets were imported `grep` found **zero** occurrences of *either* asset ID
anywhere on disk. The day asset's chain was read earlier in the same working session and is recorded
in `assets/environments.manifest.json` as an observation that can no longer be re-verified. It is
labelled as such. **No licence or author information exists anywhere** — not the log, not either EXR
header, no sidecar, no saved `.blend`, no running Blender session. The manifest says "NOT RECORDED"
and nothing is invented.

## Decision

**The HDRIs are illumination and reflection only. Neither is ever drawn as the visible sky.**

That one decision settles four separate hazards at once, and it needed no engine change because
ADR-049 already split the two ideas: `environmentIntensity` scales *the IBL that lights surfaces*,
`skyIntensity` scales *only the visible background pass*.

* The night map contains a **large, prominent crescent moon**. The day map contains a bright sun
  glare region. Neither can produce a double moon or a double sun if neither is drawn.
* Both maps are open ocean to the horizon in every direction. Never drawing them means no HDRI
  horizon competes with the procedural one, and the brief's shoreline-contradiction concern cannot
  arise — there is also, as it happens, no shoreline in either image.
* No HDRI seam can appear, because no HDRI pixel is ever displayed.

Measured, at identical camera, three arms differing only in the environment map:

| arm | grass plateau | island underside | sky patch |
|---|---|---|---|
| no HDRI | 74.4 71.3 50.7 | 11.6 13.2 11.6 | **14.5 26.6 50.5** |
| day HDRI | 102.7 99.5 82.5 | **101.6 90.0 81.0** | **14.5 26.6 50.5** |
| night HDRI | 95.0 89.6 69.2 | **36.3 30.6 26.6** | **14.5 26.6 50.5** |

The underside is the honest test — it is lit almost entirely by ambient, and it moves **8.8×** with
the day map and **3.1×** with the night one. The sky patch does not move **at all**, across three
arms with three different hashes and whole-frame MADs of 0.043 and 0.014. That is the double-moon
guarantee as a measurement rather than a promise, and it is a control that could have failed.

### The owner's moon decision, and what it costs

The owner chose the HDRI's moon over a procedural one, so `environment.moon.*` is not built.
Consequences, recorded rather than argued:

* **Size, colour and phase stop being parameters.** They are properties of the image now. They are
  not faked with a scalar. What remains controllable is *contribution* (`environment.intensity`) and
  *position* (`environment.rotation`).
* **An environment map is not a light.** It contributes ambient and reflection; this renderer's
  shadows come from punctual lights. Night moon shadows therefore still need a directional light
  aimed along the map's moon. The aiming does not have to be done by hand: the loader already
  reports the radiance-weighted brightest direction (ADR-049), and logged it for both maps —
  day `(0.966, 0.259, -0.000)`, elevation **15.0°**; night `(0.103, 0.698, -0.709)`, elevation
  **44.3°**. `Environment::lightFromEnvironment` exists to bind a key light to exactly that
  direction.
* **The night sky rotates as one object.** `environmentRotation` turns the map's moon, stars and
  horizon together, because it is one image. A moon that rises independently of the stars is not
  available and would not be correct anyway.

## Consequences

`assets/environments/*.exr` is gitignored like every other asset here. At merge:

```
/Applications/Blender.app/Contents/MacOS/Blender --background --python tools/import_hdri.py -- \
    --cache ~/blenderkit_data/hdrs --out assets/environments
```

It needs the BlenderKit cache to still hold the four source files. If it does not, the tool says
which size it wanted and stops rather than substituting anything.

The `.exr` routing fix is engine-wide, not scoped to this project: any scene or `--env` that names
an EXR now works, and the asset catalogue's claim about the format is true for the first time.
