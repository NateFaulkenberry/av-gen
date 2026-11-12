# Request to the AV Gen owner: let an imported glTF lake use the cinematic water path

Date: 2026-11-11. From: av-agent (M8). Read-only inspection of AV Gen commit b2195952 (`artifacts/m8-avgen/av-gen`).
We do not modify AV Gen. Paths below are relative to that clone. *verified* = read in the source or seen in a render.
*assumption* = inference we have not run.

## 1. Problem

The alpine lake is an ordinary glTF mesh (material `Lake`, flat plane at y=0, x -688..188, z -2988..198). In Blender
Cycles it mirrors the massif and the sky. In AV Gen it shows no mountains. Mountains are 2-6 km away (hero camera
(0,15,0) looks at z=-3174). Camera heights: hero 15 m, ground 1.7 m.

## 2. Evidence (verified)

- Reference `artifacts/m8-export/alpine-v3/reference/{hero,ground}.png`: clear mirrored peaks, trees, waterfall.
  AV Gen `.../render/{hero,ground}/frame_000000.png`: flat pale blue, no reflection.
- With the unpatched roughness 0.04, the lake is a blown chalky white (`artifacts/m8-export/alpine-v4/profile/v4.capture.png`).
  We therefore patch roughness to 0.5 (the e2e GLB, `Lake.roughnessFactor 0.5`).
- **Correction to our notes.** `docs/architecture/engine.md:134` says glossy SSR logged 1 eligible for v3 and 0 for
  v4. The logs say 1 for both, and 1 at all three v4 resolutions (`v3.stdout.txt:17`, `v4.stdout.txt:17`,
  `v4_1280x720.stdout.txt:17`, `v4_960x540.stdout.txt:17`). Batching did not change eligibility. A scan of the GLB
  materials shows `Lake` is the only one with a roughness factor under 0.6, so the one eligible surface is the Lake
  (the rest carry a metallic-roughness texture, factor 1, or sit at 0.5). Batching merges meshes; the `Lake` material
  and its single node `water` are identical in v3 and v4 GLBs.

## 3. Every reflection mechanism in AV Gen today (verified)

| Mechanism | What it reflects | Eligible | Runs in |
|---|---|---|---|
| Environment cube (IBL, procedural sky or HDRI) | Sky only. Never mountains, never anything off the cube | every lit surface | everywhere |
| Glossy SSR, ADR-1820 (`src/rendering/glossy_ssr.cpp`, `shaders/glossy_ssr.wgsl`) | On-screen opaque geometry, via a Hi-Z march of this frame's depth. A miss falls back to the cube. **Reach is a fixed 30 m** (`glossy_ssr.hpp:28`, "authored defaults, not per scene", `:23`) | scene key `environment.glossyReflections` true AND tier flag `glossySsr`, AND entity is static, opaque, lit, no material program, roughness < 0.45 (`scene_renderer.cpp:4559-4568`; cap `glossy_ssr.hpp:30`). Water-style entities are excluded because they are not in the `opaque` list (`scene_renderer.cpp:4281-4297`) | tier on in Ultra/High (offline, realtime); off at Medium, Low, Emergency and Preview (`render_quality.hpp:371-372`, `:413`; `interactive_resolution.cpp:171-173`). Logged once: "glossy SSR on: N surface(s) eligible" (`scene_renderer.cpp:4570-4573`) |
| Cinematic terrain water, ADR-1286 (`shaders/water.wgsl:123-184`, `waterSsr`) | On-screen lit frame (copy taken before the water), world-space march vs linear depth, `ssrDistance` 1..2000 m, 8..160 steps (`src/scene/water_surface.cpp:118-122`); beyond reach or off screen falls back to the cube | water entity (below) with any of `ssr`, `transmission`, `undulation`, `roughnessVariation`, `tintPatches`, `flowStreaks`, `skyMirror` > 0 (`water_surface.hpp:cinematic()`) | offline and live. Ladder: SSR steps halve at Low, SSR off at Emergency; env reflection and sky mirror stay (`interactive_resolution.cpp:143-148`, `water_renderer.cpp:137-147`). ADR-1286 says cost "is not measured" |
| Sky mirror, ADR-1650 (`water.wgsl:1012-1041`) | Reads the frame copy at the reflected ray's vanishing point `viewProj*(r,0)`. Right for anything far (sky, distant range), wrong by about 2H/D radians for finite D. Trusted when the point is on screen (faded over the last 6%), the ray points up, and the copy there is farther than 0.35-0.5 x `ssrDistance` | same water entities, `skyMirror` > 0 | same as above; independent of `ssr` |
| Emitter mirror, ADR-1281 | Ecosystem emitter sprites only | ecosystem layers | not relevant |
| Planar reflection, reflection probes, parallax cubemaps | **None exist** (`docs/water.md:384-387` "There is no planar reflection"; `ADR-099-water.md:224`; ADR-1281 rejects it as "a whole second scene pass", `ADR-1281...md:42`) | | |

How the terrain water becomes a water surface: the composition flattens each terrain chunk's water mesh into an entity
with `style = MeshStyle::Water`, `material.program = <unique name>`, and pushes a `WaterSurface{program, settings}` into
`scene_.waters` (`src/scene/composition.cpp:7637-7660`, `:7685-7689`). The renderer finds the settings through
`material.program` (`scene_renderer.cpp:4281-4285`; `scene_types.hpp:458-463`). Settings are the same `WaterSettings`
struct read from `terrain.water` (`composition.cpp:13797+`). Nothing in that path needs terrain except the way the
entity is created. `water.wgsl` uses the standard vertex layout: `normal.xz` is read as flow direction and `normal.y`
as speed, but a zero direction is guarded (`water.wgsl:712-715`), so a glTF plane (normal 0,1,0) works. Depth
thickness and the shoreline fade come from the scene's linear depth, so the terrain bed under the lake gives the
shoreline for free (a water surface forces the depth prepass: `scene_renderer.cpp:4577-4580`).

## 4. Why the imported Lake does not reflect the mountains now

1. It is a normal PBR entity. Its only reflection is the environment cube: sky, no mountains (*verified*, table above).
2. At roughness 0.04 it is glossy-SSR-eligible (the single eligible surface). But the march reaches 30 m and the
   mountains are kilometres away, so every ray misses and the cube stands (*verified*: reach in
   `glossy_ssr.hpp:28`, `glossy_ssr.wgsl:85-94`; distances from our scene JSON). Cube at near-mirror and grazing
   Fresnel is full sky radiance, which we see as the blown white (v4 capture). The explanation of the blown value
   is an *assumption*; the 30 m reach and the capture are verified.
3. At roughness 0.5 (our patch) it is not eligible at all (0.5 >= 0.45), so it is just a rough sky tint.
4. Batching changed nothing about this (see section 2).

## 5. Recommendation: a per-material water override on `gltf` nodes (option a', not a planar render)

Make a named glTF material an AV Gen water surface. It reuses the terrain water's cinematic path (march plus sky
mirror), adds no shader and no render pass.

Why this is the smallest correct change for a flat lake:
- **Geometry fit (*assumption*, derived from the two reference views).** For a flat mirror, a lake pixel `d` pixels
  below the horizon reflects the direction `d` pixels above it. Horizon row is about 397/720 in ground and 515/720 in
  hero (measured by eye), so the largest `d` (323 and 205) is inside the frame in both views. Peaks at or above the
  top edge are mirrored below the frame bottom, so they are never needed. Off-screen sources only appear when the
  horizon is above the frame centre (roughly: lake pixels below row 2 x horizon row). Pitched-down cameras may
  hit that and fall back to the cube; the same is true of every screen-space option.
- **Far field.** Sky mirror reads the frame at the vanishing point, so km-range mountains need no long march. With
  `ssrDistance 2000` the mirror is fully trusted where the copy is deeper than 1000 m. Parallax error 2H/D is about 28 px
  at 1 km and 9 px at 3 km for the hero camera (H=15 m, about 16 px/deg; *assumption*, our arithmetic), about 1 px for ground.
  The near shore (trees, bank, waterfall base) goes through the march, which is exact in screen space.
- **Look knobs already exist** (all in `WaterSettings`, `water_surface.hpp:26-160`): colour ramp, `clarity`, `maxOpacity`,
  `edgeFade`, `fresnel`, `reflection`, `reflectionTint`, `roughness`, `ripple`, `rippleScale`, `rippleSpeed`, `foam`,
  `refraction`, `ssr*`, `skyMirror`. No new look parameters are requested.
- **Size (*assumption*):** about 100-150 lines in `composition.cpp` (parse, flatten hook beside `:7064-7080`, one
  `WaterSurface` per named material, a warning for unmatched names) plus a unit test and a GPU test patterned on
  `tests/rendering/test_water_sky_mirror_gpu.cpp`. Existing scenes are byte-identical when the key is absent.
- **Live cost (*unmeasured*):** one HDR frame copy plus the SSR march over the lake pixels; the ladder already degrades
  it (SSR halves at Low, off at Emergency, mirror kept). Please measure with `--ab nowaterssr` on alpine (11 M triangles).

### Interface we would use (new)

On a `gltf` node, one new optional key, material-level addressing by the glTF material name (`Entity::materialName`,
`scene_types.hpp:472-481`, set at `src/assets/gltf_loader.cpp:509`). Batching can merge nodes but the material name
`Lake` is unique in the GLB (verified), so it survives.

```json
{ "name": "environment", "kind": "gltf", "asset": "../export/out/alpine.glb",
  "waterMaterials": {
    "Lake": {
      "shallowColor": [0.113, 0.250, 0.251], "deepColor": [0.02, 0.09, 0.11],
      "clarity": 6.0, "maxOpacity": 1.0, "edgeFade": 0.6,
      "fresnel": 0.04, "reflection": 1.0, "reflectionTint": [1, 1, 1], "roughness": 0.04,
      "ripple": 0.03, "rippleScale": 0.05, "rippleSpeed": 0.0, "foam": 0.0, "refraction": 0.0,
      "ssr": 1.0, "ssrDistance": 2000, "ssrSteps": 128, "ssrGain": 1.0, "skyMirror": 1.0
    } } }
```

Rules:
- `waterMaterials` is an object, key = exact (case-sensitive) glTF material name, value = the same object `terrain.water`
  accepts, with identical key names, types, ranges and defaults (reuse the parser at `composition.cpp:13797+`). Unknown
  keys fail like they do today. `enabled` defaults to true. Absent key = nothing changes.
- Every entity of that node whose `materialName` matches becomes `MeshStyle::Water` with its own `WaterSurface`
  (`castsShadow=false`, `surfaceClass=Water`, double-sided kept). `node.program`/`roughnessScale` do not apply to it.
- Limit: `kMaxWaterMaterials = 8` (`water_renderer.hpp:87`). Say so in the error if exceeded.
- Skinned entities are skipped with a warning.
- Mesh need not be flat; we only use it on flat meshes. Flow direction is zero, so we set `rippleSpeed 0`.

What the loader or log must report so we can verify (grep-able, one line each):
- `info: gltf node 'environment': water material 'Lake' -> N entity(ies), M triangles, bounds y [lo..hi], cinematic=yes (ssr 1, skyMirror 1)`
- `warn: gltf node 'environment': waterMaterials 'Lakee' matches no material; available: Lake, Fall, Foliage, ...` (also in `Engine::projectWarnings`)
- the first-frame renderer line: `water: K surface(s), C cinematic` (today only the glossy SSR count is logged,
  `scene_renderer.cpp:4570`).
- Unrelated to this request but we need it: `environment.glossyReflections` should stay as is. We will turn it off for the
  alpine scene once the water path works, so the Lake is not also drawn by the glossy SSR (it will not be: water entities
  are not in the opaque list).

## 6. Alternatives considered

| Option | Quality | Cost | Size | Verdict |
|---|---|---|---|---|
| **A. Water override on gltf material (recommended)** | Mountains via sky mirror, near shore via march. Off-screen sources fall back to the cube (rare in our two views). Parallax error on mid-range things (28 px at 1 km, hero) | one frame copy + march, already laddered | small, no shader | recommended |
| B. Make the glossy SSR work for the Lake (raise reach, add far fallback) | `reach` is a constant 30 m, "not per scene" (`glossy_ssr.hpp:23,28`); a km-range march in a 64-step Hi-Z needs a new far-field fallback, which is what the sky mirror already is. Only roughness < 0.45 | pyramid plus second draw of the lake (about 1 ms at 1080p on their S3 test, ADR-1820) | medium, shader work, per-scene parameters, touches the byte-identical guarantees | not recommended |
| C. Real planar reflection (second scene pass from the mirrored camera) | Exact, including off-screen sources and parallax | a second 11 M-triangle pass: roughly doubles the 12.6 ms geometry GPU time in the batched alpine (our measure, `engine.md` table), so offline only | large; ADR-1281 and `docs/water.md:387` record it as not built | only if A fails on pitched-down views |
| D. Do nothing in AV Gen, bake the mirror in Blender | Lake texture would carry a baked reflection that is wrong for any other camera | none | none, and wrong for AV Gen's own cameras | rejected |

## 7. Acceptance test (what the owner can run)

Minimal scene: a GLB with two meshes and no other content.
1. `water`: a 3000 x 3000 m plane at y=0, material named `Lake`, base colour teal.
2. `ridge`: a 1500 m tall box or triangular ridge at z=-3000, 2000 m wide, white-lit side facing the camera, plus a
   small coloured block at 200 m on the lake edge for the near march.
Scene JSON: the node above, `camera.position [0,5,0]` looking at the ridge, procedural sky, one sun.
Pass if all of these hold (look at the frame, do not trust a metric alone):
- With `waterMaterials` absent: no ridge in the lake (today's behaviour, regression guard).
- With it: the ridge is mirrored, upright-flipped, aligned at the shoreline; the near block is mirrored, and the sky
  fades toward the horizon (fresnel).
- Log shows the three lines in section 5. A wrong material name produces the warning with the material list.
- `--ab nowaterssr` removes only the near block's reflection; the ridge stays (sky mirror). `skyMirror 0` removes the ridge.
- A scene with the key absent hashes the same as before (existing byte-identical tests).

How we will verify on alpine (our side, after we receive the build): set `waterMaterials.Lake` as above in
`integration/scene.hero.json` and `scene.ground.json`, restore the Lake roughness to the Blender value (stop
patching it), render hero and ground, and compare against `artifacts/m8-export/alpine-v3/reference/{hero,ground}.png`
by eye first, then by our evaluation chain. Check the grep lines, then check the mirrored left cliff and the central
peak position in ground view. Pitched-down views (`elevated`, `overview`) are expected to show cube fallback in the
lower lake; we report that and do not call it a failure.

## 8. Open questions for the owner

- Is a hard cap of 2000 m on `ssrDistance` acceptable for 3-6 km views? Our plan relies on the sky mirror for the far field.
  If the mirror gives visible offsets, raising the cap to about 8000 m is the one extra change we would ask for.
- Should the water entities of a gltf node be able to receive shadows (the terrain water does not cast any)?
  Not needed for this request.
