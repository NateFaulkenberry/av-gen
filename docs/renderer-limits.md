# Renderer hard limits

A single place for the fixed-size caps compiled into the renderer. Each one is a `constexpr` in a
header or source file, chosen once and never revisited by a config file or a quality tier — hitting
it does not fail the render, it silently (mostly) drops whatever did not fit. That is a defensible
design for a fixed-size GPU buffer, but it means nobody discovers a cap exists until something they
placed in the scene stops showing up.

Two facts anchor this document, both checked against the current source rather than assumed:

- **`kMaxObjects = 256` is gone (ADR-128).** It came from 512-byte dynamic-offset slots in a 128 KB
  uniform buffer allocated once at `init()`. The buffer now grows with the frame, and what remains
  is a 64 MiB byte budget — see the section below, which is kept here rather than deleted because
  the *mechanism* it describes is still the one two other caps use.
- **Glowmere flattens to 278 entities, and the denser rungs of its own benchmark ladder go much
  further:** `glowmere-dense` is 482 entities and `glowmere-extreme` is 840. On the pre-ADR-128
  binary, three of those four scenes never warned — culling kept the visible set under 256 at the
  camera each recipe opens on — and `glowmere-extreme` logged `extra entities skipped` on 60 of 60
  frames and rendered without a hero tree that is in the scene. Whether content worked depended on
  where the camera was pointing. Glowmere also logs `"22 splines; only the first 16 are
  available on the GPU"` today, so `kMaxGpuSplines` is a limit that is *already* user-visible in
  the wild, not a hypothetical.

Everything below is verified against the current source tree unless a line explicitly says
otherwise.

---

## `kMaxObjects` — **lifted** (ADR-128); what replaced it

- **Value:** none. The cap was deleted in ADR-128 and replaced by an allocation policy.
- **Source:** `src/rendering/scene_renderer.hpp` — `kInitialObjects = 256` (what `init()`
  allocates, so a small scene pays exactly the 128 KB it always did), `kObjectStride = 512`,
  `kObjectBufferBudgetMiB = 64`, and `kMaxObjectCapacity = 64 MiB / kObjectStride` = **131,072
  slots**. `SceneRenderer::ensureObjectCapacity` (`scene_renderer.cpp`) grows the buffer, the
  staging mirror, the object bind group and the skinned path's group 1 to fit the frame's drawable
  entity count, doubling and never shrinking.
- **Why anything is left:** a scene must not be able to ask the driver for a gigabyte. 64 MiB is a
  memory budget rather than a slot count, and it is deliberately **eight times** the 16,383 distinct
  entity indices the identifier target can even name (`kPickIndexBits = 14`,
  `scene/scene_types.hpp`), so the object buffer can never be the binding limit again. ADR-130
  records that relation and `tests/unit/test_renderer_layout_guards.cpp` holds it, scraped from the
  headers rather than retyped.
- **User-visible when hit:** the clamp logs both numbers — what was asked for and what the budget
  allows. The old silent-drop symptom is gone: `ensureObjectCapacity` sizes the buffer *before* the
  entity loop runs, so the `"more than {} visible entities"` warning in `render()` is now a backstop
  for an arithmetic disagreement rather than a budget anyone reaches.
- **The real ceiling now:** 16,383 entities, from the pick-id encoding, not from an allocation.
  `packPickId` saturates rather than wrapping, so past it two entities share a name in the
  identifier target and a click resolves the wrong one — a quieter failure than a dropped draw, and
  the reason the budget is kept above it.
- **Proof:** `tests/rendering/test_object_capacity_gpu.cpp` renders `testing::denseEntityGrid` — 361
  boxes, all visible, no floor — and checks the identifier target texel by texel: every entity
  claims at least one pixel, every claim names its own entity with a one-based material id, the sky
  is claimed by nobody, and entity zero is claimed by entity zero.
- **Related, same mechanism, NOT lifted:** `kMaxProceduralObjects = 256`
  (`src/rendering/procedural_renderer.cpp:32`) for procedural instance *sources*, and
  `SdfRenderer::kMaxObjects = 256` (`src/rendering/sdf_renderer.hpp:131`) for SDF objects. Both are
  the identical "256-byte-aligned slots in one fixed uniform buffer" pattern with their own silent
  overflow warnings, and ADR-128's `ensureObjectCapacity` is the shape the fix takes in both. Glowmere
  has 16–30 procedurals and no SDFs, so neither is firing today; they are listed so that "the object
  cap was lifted" is not read as "every 256-slot buffer was lifted".

---

## `kMaxSceneLights` — 256 packed lights per frame

- **Value:** 256
- **Source:** `src/rendering/light_data.hpp:62`
- **Why it exists:** every enabled light (directional first, then local) is packed into a
  `GpuLight` array uploaded as one buffer (`sizeof(GpuLight) * kMaxSceneLights` bytes,
  `scene_renderer.cpp:586`). This is a **quality-setting-shaped** limit rather than an API
  requirement — 256 is a size chosen for the light buffer's static allocation, not a WebGPU or
  hardware ceiling — but nothing in the renderer currently makes it configurable per quality tier.
- **User-visible when hit:** **no warning**. `orderLightsForShading` (`light_data.cpp:118`) sorts
  directional lights first, then local lights, and silently truncates the combined list past 256
  (`light_data.cpp:133-135`, plain `out.resize(kMaxSceneLights)` with no log call). Directional
  lights are effectively protected (they are placed first and no realistic scene has hundreds of
  them); local lights past the 256th simply stop being shaded, with nothing printed anywhere.
- **What would be needed to lift it:** widen the light buffer and the `ClusterParamsGpu` /
  `FrameUniforms` structures that size against it (both have `static_assert`s tied to
  `kMaxSceneLights`, so raising the constant is a recompile, not a config change), and add the
  missing overflow log so a scene that exceeds it fails loudly rather than silently. Raising it
  further than a few thousand starts to matter for the cluster-build compute pass's per-froxel
  scan cost, which is `O(lights)` per froxel before the `kMaxLightsPerCluster` cap applies.

---

## `kMaxLightsPerCluster` — 32 lights per froxel

- **Value:** 32
- **Source:** `src/rendering/light_data.hpp:61`
- **Why it exists:** the clustered forward-lighting pass (ADR-033) builds a froxel grid
  (`kClusterX × kClusterY × kClusterZ` = 16×8×24 = 3,072 froxels) and, for each one, a fixed-length
  index list of the local lights that reach it. The cluster buffer is one contiguous allocation of
  `kClusterCount * (1 + kMaxLightsPerCluster)` `u32`s (`scene_renderer.cpp:577`) — a count plus a
  fixed-width slot per froxel — so the per-froxel list has to have a compile-time bound the same
  way a hash table's open-addressing probe sequence does. This is a **quality-setting-shaped**
  choice (memory for the cluster buffer versus how many overlapping lights a single froxel can
  represent), not an API or hardware ceiling.
- **User-visible when hit:** **overflow only** — a froxel that thirty-two lights already reach
  simply does not gain a thirty-third; `assignClusters`'s CPU reference and the GPU compute pass
  both stop appending once the per-cluster list is full (`light_data.hpp:93-96`). This only matters
  where many small lights overlap in a small volume (a chandelier, a wall of practical lights); the
  scene otherwise never notices.
- **What would be needed to lift it:** the froxel index list would need to become variable-length
  (an offset + count into a shared pool, rather than a fixed stride) to raise this without a
  quadratic blow-up in the cluster buffer's fixed size — 3,072 froxels × 32 lights is already
  98,304 `u32` slots before the light data itself.

---

## `kMaxShadowViews` — 8 shadow views per frame

- **Value:** 8
- **Source:** `src/rendering/shadow_math.hpp:17`, alongside `kMaxCascades = 4` (`:18`)
- **Why it exists:** every shadow-casting light contributes one or more views into a shared shadow
  atlas — one cascaded directional light can take up to `kMaxCascades` (4) views, a spot light
  takes 1, and a point or area light takes `kPointShadowFaces` (6, one per cube face). The atlas's
  texture array is sized `std::max(want, kMaxShadowViews)` layers and the uniform arrays that carry
  per-view matrices to the shader (`ShadowViewGpu views[kMaxShadowViews]`,
  `shadow_math.hpp:47`) are fixed-size C arrays mirrored in WGSL — this is a **quality-setting**
  limit (how much shadow-casting a frame budgets for) enforced through a **uniform-array size**,
  which behaves like a hard ceiling once compiled.
- **User-visible when hit:** **no**. `ShadowRenderer::update` (`shadow_renderer.cpp:160-225`)
  allocates cascades and spot/point views on a first-come basis against the remaining budget: one
  cascaded directional light is allowed at all (`directionalDone`), a spot light is skipped once
  `views_.size() >= kMaxShadowViews`, and a point/area light is skipped unless six free slots
  remain (`views_.size() + kPointShadowFaces <= kMaxShadowViews`). None of these branches log
  anything — a ninth shadow-casting light beyond the budget just casts no shadow, and the only way
  to notice is that its shadow is missing.
- **What would be needed to lift it:** raise `kMaxShadowViews` and the atlas layer count together
  (the atlas is already sized from it, so this is mostly free), but the uniform array
  (`ShadowViewGpu views[kMaxShadowViews]`) baked into `FrameUniforms`-adjacent structures would grow
  in lock-step and the WGSL side (`shaders/shadows.wgsl` et al.) would need the same constant
  bumped — plus an overflow log, which does not exist today at any of the three skip sites.

---

## `kMaxRigs` — 64 skinned rigs per frame

- **Value:** 64
- **Source:** `src/rendering/skinning.cpp:23`
- **Why it exists:** each skinned rig's joint palette (this frame's and the previous frame's, for
  velocity) occupies one slice of a shared buffer, addressed the same way object uniforms are — by
  a dynamic offset rounded up to WebGPU's 256-byte alignment (`kOffsetAlignment`,
  `skinning.cpp:19`). The comment at the definition is explicit about the intended failure mode:
  *"a ceiling on how many rigs a frame may draw. Beyond it the extra rigs simply do not get a
  palette, and their entities render in the bind pose rather than the frame failing."* This is an
  **architectural limitation** in the same family as `kMaxObjects` *was* — a fixed-size buffer of
  per-rig slices — chosen smaller because skinned characters are rarer than static entities in
  practice.
- **User-visible when hit:** **yes** — `log::warn("{} skinned rigs in the scene; only the first {}
  get a joint palette", scene.rigs.size(), kMaxRigs)` (`skinning.cpp:295-297`). Rigs beyond the
  64th are not an error and not invisible: they draw in their bind pose (T-pose or similar), which
  reads as "this character stopped animating" rather than "this character disappeared."
- **What would be needed to lift it:** the same fix `kMaxObjects` got in ADR-128 — the joint buffer
  is already reallocated per scene by `SkinningRenderer::ensureBuffer`, so the change is to size it
  from `scene.rigs.size()` instead of clamping that to 64, and to let the bind group follow. Note
  that ADR-129 declined the *storage buffer indexed per-draw* framing this line originally proposed,
  for reasons that apply here too: the joint palette is already a storage buffer, and what is fixed
  about it is the slice count, not the binding kind.

---

## `kMaxLodLevels` — 4 LOD levels per procedural object

- **Value:** 4
- **Source:** `src/scene/procedural.hpp:434`
- **Why it exists:** an object's LOD ladder — full mesh down to an impostor billboard — is capped
  at 4 rungs, and this number is load-bearing in more than one place: the GPU-side culling compute
  pass (`shaders/cull.wgsl:66`) hardcodes the identical constant to stride a shared indirect-draw
  buffer (`kMaxLodLevels` slots of 5 `u32`s each, per object), and
  `procedural_renderer.cpp:44` has a `static_assert(scene::kMaxLodLevels == 4, ...)` specifically so
  the two copies of the constant (C++ and WGSL) cannot silently drift apart. This is an
  **architectural limitation**: the indirect buffer's per-object stride is computed from it, so
  raising it means recomputing that stride and matching the shader's hardcoded copy.
- **User-visible when hit:** **no.** An object's `lodCount` (author-configurable) is silently
  clamped: `std::clamp(lod.lodCount, 1, scene::kMaxLodLevels)`
  (`procedural_renderer.cpp:162`, also `:913`), with no log call at either clamp site. Authoring a
  5-level LOD ladder simply gets the top 4 levels; the 5th is discarded with no diagnostic at all —
  the quietest limit in this document.
- **What would be needed to lift it:** widen the constant in both `scene/procedural.hpp` and
  `shaders/cull.wgsl` together (the `static_assert` exists to catch exactly the failure mode of
  changing one and not the other), recompute the indirect buffer's per-object stride
  (`kMaxLodLevels` slots × 5 `u32`s), and add a log line at the two silent clamp sites so a content
  author discovers the cap instead of an unexplained gap in LOD transitions.

---

## `kMaxGpuSplines` — 16 splines resident on the GPU

- **Value:** 16
- **Source:** `src/rendering/spline_buffers.hpp:33`
- **Why it exists:** the spline table is one fixed-size storage buffer (`SplineInfoGpu` header of
  `kMaxGpuSplines` entries, followed by `kMaxGpuSplines × kSplineGpuSamples` sample records,
  `spline_buffers.hpp:12-14`, with a `static_assert(sizeof(SplineInfoGpu) == 16 *
  kMaxGpuSplines)` pinning the header layout to the constant). Slot assignment is positional —
  slot *i* is `Scene::splines.splines[i]` for *i* < 16 — so this is an **architectural
  limitation** shaped like a quality setting: a fixed table size chosen for a bounded per-frame
  upload cost, with no mechanism to spill extra splines anywhere else.
- **User-visible when hit:** **yes, and already firing in production content.** `SplineBuffers::update`
  logs once per limit crossing: `"scene has {} splines; only the first {} are available on the
  GPU"` (`spline_buffers.cpp:46-49`, `warnedLimit_` guards it to once). Glowmere has 22 splines
  authored and logs exactly this — `"22 splines; only the first 16 are available on the GPU"` — so
  this is the one limit in this document with a confirmed, live user report rather than a
  theoretical one. Splines past the 16th are simply unavailable to any material program or
  procedural system that samples them by name.
- **What would be needed to lift it:** raise `kMaxGpuSplines` and the `static_assert` tied to it in
  `spline_buffers.hpp`, **and** hand-edit `shaders/spline.wgsl`'s own copy of the table layout —
  `struct SplineTable { info: array<vec4<f32>, 16>, ... }` (`spline.wgsl:19-20`) hardcodes the same
  16 independently, in WGSL, where a C++ `static_assert` cannot reach it. Unlike `kMaxLodLevels`,
  which has an explicit `static_assert` whose failure message points at the matching WGSL constant
  (`procedural_renderer.cpp:44`), nothing here would catch the two copies drifting apart — raising
  one without the other compiles cleanly and corrupts the spline table at slot 16 and up. (A
  separate `16u` at `spline.wgsl:146` is an unrelated binary-search iteration cap, not a second copy
  of this constant — worth noting so it is not mistaken for one when this is eventually raised.)
  Because slot assignment is otherwise positional and by authoring order, the buffer-size change
  itself is simple; the unguarded second copy is the actual risk.

---

## Summary table

| Limit | Value | Source | Kind | User-visible when hit? |
|---|---|---|---|---|
| `kMaxObjects` | **lifted (ADR-128)** | `scene_renderer.hpp` | Allocation policy; 64 MiB budget = 131,072 slots | Yes — clamp logs both numbers |
| entity naming | 16,383 | `scene_types.hpp` (`kPickIndexBits`) | Encoding (identifier target) | No — `packPickId` saturates |
| `kMaxProceduralObjects` | 256 | `procedural_renderer.cpp:32` | Architectural (same mechanism as above) | Yes — log warning, objects dropped |
| `kMaxSceneLights` | 256 | `light_data.hpp:62` | Quality setting (fixed light buffer) | No |
| `kMaxLightsPerCluster` | 32 | `light_data.hpp:61` | Quality setting (fixed froxel index width) | Overflow only, no log |
| `kMaxShadowViews` | 8 | `shadow_math.hpp:17` | Quality setting via fixed uniform array | No |
| `kMaxRigs` | 64 | `skinning.cpp:23` | Architectural (fixed joint-palette slots) | Yes — log warning, bind pose |
| `kMaxLodLevels` | 4 | `scene/procedural.hpp:434` | Architectural (shared with a WGSL constant) | No — silent clamp |
| `kMaxGpuSplines` | 16 | `spline_buffers.hpp:33` | Architectural (fixed, positional table) | **Yes — firing on Glowmere today** |

Not covered above but sharing the same shape, for completeness: `kMaxPaletteJoints = 256`
(`scene/skeleton.hpp:35`, an authoring-time/asset-import cap checked in `gltf_loader.cpp`, distinct
from the per-frame `kMaxRigs`) and `kMaxDrawsInFlight = 64`
(`rendering/output_mapper.hpp:53`, a ring-buffer depth for in-flight draw slots — memory-safety
sized, not user-facing).

This document does not lift any limit. Where "what would be needed to lift it" is described above,
that is the scope of the change, not a proposal to make it — several of these are flagged in
`docs/renderer-upgrade/01-audit-and-baseline.md` §1.5 as needing a real architectural replacement
(object addressing moving off fixed uniform-buffer slots being the highest-priority one), and
that work is out of scope here. **Exception:** the object-addressing item was subsequently done —
ADR-128/129/130, recorded above.
