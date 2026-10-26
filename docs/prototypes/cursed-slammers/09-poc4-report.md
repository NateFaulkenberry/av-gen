# Cursed Slammers POC 4: holofoil and engraving alternatives (report)

2026-10-26, branch `scene/cursed-slammers`. The brief is `08-brief-poc4-holofoil.md`.
- The asset is Gorilla Face (CC BY 4.0, `assets/cursed-slammers.manifest.json`). It is the same mesh in every
  variant; everything derived from it stays gitignored.
- Nothing is registered and no engine code changed. The branch merged main (ccdf88d6) for ADR-1661's 64-op programs.
- The POC 1-3 files are untouched and still runnable.
- Media: `~/Desktop/av-gen-review/93-cursed-slammers/poc4/`.

## Recommendation

**D (hybrid security foil) is the strongest candidate for the brief's target, built on B's linework.** As a material it
is the closest thing to a premium engraved foil card:
- black engraved relief lines that follow the brow, sockets, nose and muzzle;
- crosshatch only where the face turns from the light;
- a polished silver base, so the face stays readable at every framing;
- spectral colour confined to narrow bands;
- sparse sparkle.

Its weaknesses:
1. **The holographic component is weak.** The spectral bands land mostly in the eye sockets and on the muzzle as a
   magenta/teal tint. They do follow the face (their phase is the face's depth plus the view angle), but they do not
   yet sweep across it as a band of colour in motion. They need a wider band and a stronger view term.
2. **The lines read as contour lines** (a relief map or a topographic medallion) more than as a portrait engraving.
   Real intaglio varies the line *direction* with the form, not just its bend. That needs line directions from the
   surface, which this asset cannot give cheaply (see §4).
3. **Cost.** D is the most expensive variant. Headless it is 22.1 ms GPU, against 12.9 ms for the bare scan. Live it
   is 19.9 ms at High (0.85 scale). It holds 30 fps, but the face alone uses 60% of the budget (§3).
4. **Sparkle at grazing angles flickers.**
   - Checked frame by frame at the orbit's extremes: on the far cheek, at grazing view, the sparkle becomes a dense
     field of specks that changes every frame. That is flicker, not flake (A as well).
   - Cause: the sparkle is weighted by Fresnel, and at grazing many cells' angle gates pass quickly.
   - Fix (not yet done): cap the Fresnel weight, and use larger cells near the silhouette.

**B (intaglio)** has the cleanest engraving and the best black/silver read, but almost no holographic character.
**C (chrome)** reads the face best and is cheapest, but it looks like an oil-slick sculpture, not a printed foil.
**A** has the most "holo" colour, but it is the busiest and the closest to the generic rainbow the brief warns
against.

**Verdict on the direction: better than lenticular for a flat screen.** These treatments work in a still and survive
motion. Nothing swims: the lines are fixed in object space, and the colour moves only with the view. But none reaches
"premium vintage holographic card" yet. The engraving is there; the convincing diffraction foil is not.

## 1. What was built

`docs/prototypes/cursed-slammers/poc4/variants.py`:
- Each variant is **one material program on the face** (a gltf node `program`, ADR-1572): no post effect, no extra
  pass, no temporal effect.
- `--variant base|A|B|C|D` selects the variant; `--shot front|orbit|close|final` selects the camera. The camera,
  lights, exposure and bloom are identical in every variant.
- Programs run 37-61 ops (A 42, B 37, C 23, D 61; the limit is 64).
- Every constant is a routable `material/poc4/op/<n>/…` parameter (the existing material architecture).

Reused from POC 1-3:
- the harness lights and backdrop;
- the depth-recentred face (`cs-face-b0.glb`, POC 2's `depth_variants.py`);
- the ridge and cosine-palette idioms.

| | A Holofoil engraving | B Intaglio / guilloche | C Iridescent chrome | D Hybrid security foil |
|---|---|---|---|---|
| Lines | Relief lines: phase = 100·y + 70·depth, so they bend round the form. Grooves widen in shadow | The same, with a gentle guilloche ripple, plus a diagonal crosshatch only where N·L < 0.45 | None | B's |
| Colour | Silver F0 through a muted cosine spectrum indexed by Fresnel and depth | Silver and black | Dark metal, F0 = the spectrum, brighter toward grazing | Silver. The spectrum only in bands (cos peak of 0.6·t), plus a 320/unit "grating" phase in the colour |
| Sparkle | Per-cell, angle-gated (each cell flares at its own view angle), on the ridges only | None | None | The same, sparser |
| Roughness | 0.3 ridges / 0.75 grooves, then roughnessFilter | 0.3 / 0.85 | 0.16-0.38 from a soft noise | 0.28 / 0.85 |
| Grooves | Black, occluded | Black, occluded | | Black, occluded |

**Techniques taken, not code:**
- **Cyanilux's holofoil breakdown** (its code is CC0): view direction indexes a colour ramp, with a pattern
  distorting the index.
- **holocloth** (MIT): tint the metal's F0 with the spectrum, so colour comes from reflections, not glow;
  angle-gated per-cell glints; cavity occlusion.
- **Natane Toon's lenticular parameters** (no licence stated): only confirmed the frame-by-angle approach POC 2 already
  tested.

## 2. Problems found and fixed on the way (evidence-based)

1. **The scan's colour texture multiplies the program's base colour after the program runs.** The texture is a
   near-black bronze patina, so no metal could read as silver. The variants draw the **same mesh with its colour
   texture removed** (`tools/cursed_slammers/strip_color.py`; the baked normal map is kept). The base reference keeps
   the texture.
2. **The first anti-alias fade** started fading the lines immediately. That left the grooves at about 70% depth, grey
   instead of black, and inverted the engraving in shadow. Found by rendering the groove mask as red emission. The
   fade now holds full contrast until a line period drops to 6 px and is gone at 2 px.
3. **Black metal needs something to reflect.** In the near-black studio every metal read as near-black. The shared
   studio sky now has a soft brighter horizon band (identical for all variants, including the reference).
4. **The POC 3 back volume pokes through this unexaggerated face.** It is not used here.

## 3. Performance (M2 Max on AC; every run under `tools/gpu-lock.sh`; `20-performance.txt`)

| Variant | Headless 1080p GPU p50 (3 reps, orbit) | p95 | Live profile at 30 fps (final framing) |
|---|---|---|---|
| Reference (no program) | 12.9 ms | 15.1 | |
| A | 19.6 | 26.1 | |
| B | 18.9 | 26.8 | GPU median 16.3 ms (auto: High, 0.85 scale), 100% on time |
| C | 17.0 | 22.3 | |
| D | 22.1 | 30.5 | GPU median 19.9 ms (High, 0.85 scale), 99.3% on time (1 late of 150) |

- **The programs cost 4-9 ms** on top of the bare face. The face is a gltf node, and a gltf node's program runs
  through the material *interpreter* (ADR-1493 compiles programs only for procedural nodes). That is the likely
  reason a 61-op program costs this much.
- **The bare scene already costs about 13 ms**, the same unexplained full-frame cost seen in POC 2/3.
- **Before the face goes into a live scene**, the cheapest wins are, in order:
  1. draw the face through the procedural path (`kind: mesh`) so the compiled program is used;
  2. trim D toward B's op count;
  3. look into the 13 ms base.
  The first two are not engine changes.

## 4. Can the asset support the effect?

**Partly. Geometry yes, surface parametrisation no.**
- **Geometry: adequate.**
  - The depth relief is what makes every line set follow the face, and the scan's relief carries it.
  - Brow, sockets, nose and muzzle all bend the lines convincingly.
  - The baked normal map adds the wrinkle highlights that sell the metal.
- **UVs: unusable for line patterns.** The UV layout is a fragmented photogrammetry atlas, so UV-driven engraving would
  break at every island. Object-space position plus depth was the simplest viable fallback.
- **Tangents: none.** Normals are fine. There are no smooth tangents to steer line *direction* along the form, which
  limits B/D to "bent parallel lines" rather than true portrait-engraving flow. The minimum fix: a hand-made or
  generated UV and tangent flow on a retopologised copy. That is an asset change, not a renderer one.
- **Colour texture:** a baked bronze patina with lighting in it. It is useful for the reference, a hindrance for
  metals (it had to be removed).
- **Mask shape:** a front mask with photogrammetry noise on the crown. That noise shows as fine speckle under strong
  speculars in close-ups. It is adequate for a frontal or near-frontal card framing, not for wide orbits.

## 5. Defect log (stationary, orbit ±15°, close-up, final framing)

| Defect | A | B | C | D |
|---|---|---|---|---|
| Swimming | None (object space) | None | None | None |
| Visible tiling | None (procedural) | None | None | None |
| Aliasing (lines) | None seen at 1080p (the fade guards below 6 px a period) | None seen | n/a | None seen |
| Excess sparkle | **Yes at grazing:** dense specks on the far cheek that change every frame (flicker); fine front-on | n/a | n/a | **Same, slightly less dense** |
| Washout | No. Bloom threshold 1.4, intensity 0.2 | No | No | No |
| Rainbow not following the face | Follows depth contours, but covers everything | n/a | Follows the face (Fresnel and depth); reads as oil slick | Follows the face, but concentrates in the sockets and muzzle |
| Readability | Good front-on, busy in the close-up | Very good | Best | Very good |

## Files

- `poc4/variants.py`: the four variants and the reference, the shots.
- `tools/cursed_slammers/strip_color.py`: the untextured face for the metals.
- Review folder: 2x2 slow orbit, 2x2 final framing drift, same-setup stills (front, close-up, final framing), orbit
  extremes, the performance file.
