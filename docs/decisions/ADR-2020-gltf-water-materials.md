# ADR-2020: A gltf node's named materials can be drawn as water

- Status: Accepted (2026-11-11), eng/gltf-water-materials.
- Request: [docs/requests/2026-11-11-gltf-water-materials.md](../requests/2026-11-11-gltf-water-materials.md), from
  av-agent, which embeds AV Gen and imports its alpine world as one GLB.
- Code: `readWaterSettings`, the `waterMaterials` parse, the Gltf flatten and `addNode` in
  `src/scene/composition.cpp`; `CompositionNode::waterMaterials` in `src/scene/composition.hpp`;
  `scene::kMaxWaterSurfaces` in `src/scene/water_surface.hpp`; the `water:` log line in
  `src/rendering/scene_renderer.cpp`.
- Tests: `tests/unit/test_gltf_water_materials.cpp` (`[adr2020]`), `tests/rendering/test_gltf_water_gpu.cpp`.

## Context

An imported lake is an ordinary PBR entity. Its only reflection is the environment cube: the sky, never the mountains
around it. Glossy SSR (ADR-1820) can take it at roughness under 0.45, but its march reaches 30 m and the mountains are
kilometres away, so every ray misses and the cube stands. The surface that does reflect distant terrain is the
terrain's own water: the cinematic path (ADR-1286, a march against a copy of the lit frame out to `ssrDistance`) plus
the sky mirror (ADR-1650, which reads the copy at the reflected ray's vanishing point, right for anything far).

Nothing in that path needs a terrain except how its entity is made. The renderer finds a water entity by
`MeshStyle::Water` and its settings by the name in `material.program` (`Scene::waters`). `water.wgsl` reads the
standard vertex layout, with `normal.xz` as flow direction and `normal.y` as speed; a zero direction is guarded.

## Decision

A `gltf` node takes an optional `waterMaterials` object: key = an exact, case-sensitive glTF material name; value =
the object `terrain.water` takes.

- **One reader.** The value is read by `readWaterSettings`, which the terrain parse now calls too. Same keys, types,
  ranges, defaults and messages (the terrain's messages are unchanged word for word). `enabled` defaults to true;
  `enabled: false` is the same as the key being absent.
- **The flatten.** Every entity of the node whose `materialName` matches becomes the terrain water chunk's twin:
  `MeshStyle::Water`, a fresh `Material` (base colour = `shallowColor`, roughness, metallic 0, double-sided,
  `surfaceClass = Water`), `castsShadow = false`, and `material.program = "<node>_water_<material>"`. One
  `WaterSurface` per matched material carries the settings under that name. The node's `program` (ADR-1572) does not
  reach these entities, at the flatten or per frame; `roughnessScale` already applies only to Lit entities.
- **Flow.** A sheet's vertex normal is its surface normal, so a flat lake reads as flow direction zero (the shader's
  +X fallback) with a speed fraction of 1. The surface's `fastestFlow` is 0.05 m/s, the terrain's still-water floor,
  so that fraction is a still lake. `rippleSpeed` scales it as usual. A mesh that is not flat has normals with an xz
  part, which the shader reads as a direction. That is allowed and documented, not corrected.
- **Skinned entities** of a matched material are skipped with a warning and keep the asset's material. The water
  pipeline draws a static sheet.
- **The limit.** A scene draws at most `kMaxWaterSurfaces` = 8 water surfaces (the renderer's `kMaxWaterMaterials`
  is now this constant). A node that names more than 8 enabled materials is refused. So is a scene whose terrains plus
  its gltf `waterMaterials` entries come to more than 8, counted only when a gltf node asks for water, so a scene of
  terrains loads exactly as it did. Past the last slot the renderer used to give a surface the first surface's
  settings without a word.
- **Reporting.** Three lines, one each:
  - `gltf node '<node>': water material '<m>' -> N entity(ies), M triangles, bounds y [lo..hi], cinematic=yes|no (ssr
    S, skyMirror K)`, once per node per asset load (info);
  - `gltf node '<node>': waterMaterials '<m>' matches no material; available: A, B, ...`, when the asset loads (warn),
    and in `Engine::projectWarnings` through `Composition::waterMaterialProblems()`;
  - `water: K surface(s), C cinematic`, the renderer's first frame that has any water (info).
- **Saving.** The object is written back verbatim, and only when authored.
- **Absent, nothing changes.** No entity, surface or byte of the image differs.

`environment.glossyReflections` is unchanged. Water entities were never in its opaque list, so a converted lake is
not drawn twice.

## Measured

The GPU acceptance test builds the request's scene (§7): a 3 km `Lake` plane, a 1500 m ridge 3 km out, a red block
200 m out, the camera 5 m up, level. With the request's own `waterMaterials.Lake`:

- with the key, the ridge and the block are both in the lake;
- without it, neither;
- `waterSsrScale` 0 (the `nowaterssr` arm) takes the block's reflection and keeps the ridge;
- `skyMirror` 0 takes the ridge and keeps the block;
- a key that matches no material renders byte-identically to no key.

The frames (480x270, post off) show the ridge upright-flipped and meeting its own base at the far shore. The block
is mirrored below itself. The sky's reflection is bright at the horizon and fades to the deep colour toward the
bottom of the frame (the 0.04 fresnel).

`nowaterssr` has a side effect worth knowing. Where the block's reflection was, the lake shows the environment cube,
not the ridge behind the block. The sky mirror does not trust the block's 200 m depth at that lookup, so it does not
replace it with what lies beyond. Live, the ladder's Emergency rung leaves a sky-coloured silhouette of each near
object inside a far reflection. That behaviour is ADR-1650's and is unchanged here.

**Cost.** Live frame (`avgen --headless --live-frame --size 1920x1080`), on the acceptance scene, with about half
the frame lake. These are the request's settings: SSR 128 steps, 1 ray, mirror on.

| arm | result |
|---|---|
| `--ab nowaterssr`, 10 pairs | the water SSR costs **+2.8 ms GPU** (baseline 7.41, arm 5.00). Every pair is positive (+0.9 to +3.4). The tool's noise floor (43%) calls it "not a result". An earlier 6-pair run, at a slower clock, measured +3.7 ms (12.05 vs 8.25) and called it a result |
| `--ab waterssrhalf`, 10 pairs | +0.6 ms, inside the noise |
| the same scene without the key (`--ab none`) | 4.3 ms GPU frame, against 6.5-7.4 ms for the cinematic lake in the runs above |

The scene is 28 triangles, so the GPU is far from loaded and its clocks wander, which explains the floors. The
answer is roughly 2-3 ms at 1080p for half a frame of cinematic lake. Nearly all of it is the march: every ray toward
the 3 km ridge runs to its 2000 m reach and misses. Halving the steps saves little, so the cost is not linear in
`ssrSteps` here. The frame copy and the mirror are the rest. On a real world the march also crosses real depth, so the
request's 11 M-triangle alpine scene has to be measured on its own.

## Rejected alternatives

- **Raise glossy SSR's reach** (the request's option B). Its 30 m reach is an authored constant. A km-range march in
  a 64-step Hi-Z would need a far-field fallback, and that is what the sky mirror already is. It also touches the
  glossy path's byte-identical guarantees.
- **A planar reflection pass** (option C). That is a second scene pass from the mirrored camera, roughly doubling
  geometry time on an 11 M-triangle world. ADR-1281 already rejected it.
- **Per-entity addressing** (by node name). Batching merges nodes, but a material name survives batching. The glTF
  material is also the unit an artist means by "the lake".
- **Parameters for the gltf water (routes).** Not asked for. Terrain water has them because ADR-099 §15 wanted
  reactive rivers. A gltf surface's settings are set at the flatten, and day/night (`waterReflection`,
  `waterDeepColor`) still reaches them like every other surface.
- **Warn on unknown keys inside the value.** The request believed unknown keys fail today. They do not: `terrain.water`
  ignores them silently. The one-reader rule wins, so a typo in either place is equally silent. Changing that is a
  separate decision for both.

## Revisit triggers

- A scene wants more than 8 water surfaces.
- `ssrDistance`'s 2000 m cap (the request's §8). The acceptance render shows no far-field offset caused by the cap. The
  ridge at 3 km comes from the sky mirror and meets its own base at the shoreline (the mirror's 2H/D error is about
  0.2 degrees at H = 5 m, D = 3 km, under a pixel at that size). A higher cap would lengthen the march, which misses
  on every far ray anyway, and would raise the mirror's trust distance with it. Raise the cap only when a render shows
  an offset it causes.
- A gltf lake that should move with the music.
