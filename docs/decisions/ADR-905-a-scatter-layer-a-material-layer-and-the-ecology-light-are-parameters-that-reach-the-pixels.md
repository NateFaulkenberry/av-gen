# ADR-905: A scatter layer, a material layer and the ecology light are parameters that reach the pixels

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-053 (the light a glowing ecology casts), ADR-054 (scatter layers), ADR-036
(material layers), ADR-232 (a parameter path carries the structure it was saved against), ADR-903
(the object emission lane these ride on)
**Found by:** the GV3 revision brief §4 ("the small colored mushrooms ... Use them") and its audit:
the valley's fungi, shelf fungi and beacons could be reached only through the one material program
they share with every other layer that names it.
**Implemented by:** `world::ScatterLayer::{emissionGain, hueOffset, emissiveField,
emissiveFieldAmount}` (`src/world/ecology.*`); `CompositionNode::scatterParams`, the terrain branch
of `registerNodeParameters`/`applyParameters`, `NodeRange::ecologyLayers` and
`Composition::updateEcologyLights` (`src/scene/composition.*`); `GlowCluster::layer`;
`registerMaterialProgramParameters` (`src/scene/material_params.cpp`); `fs_proc`'s emissive field
(`shaders/procedural.wgsl`); `ui::inspectorRowLabel` (`src/ui/ui_logic.hpp`), used by the World
panel Inspector (`src/ui/world_panel.cpp`).
**Tests:** `tests/rendering/test_emission_lanes_gpu.cpp`: "A scatter layer's gain and hue act after
its program, on that layer alone", "A material layer's emissionIntensity parameter moves a glow that
lives in a layer", "Through a composition: a layer's lanes, the ecology light and a ring on a marker
reach the mushrooms" (three sections). `tests/unit/test_emission_lanes.cpp`: "A scatter layer's
emission lane round-trips and replants nothing", "A scatter layer's lane reaches every part of it,
and the light it casts follows", "A program's base intensity is registered only where the base
emits", "A material layer's emission intensity is a parameter, by index and name", "UI reach: every
new emission control is exposed, sectioned and named for what the viewer sees" (both panels' own
arithmetic on the registered set).

## Context

- **A scatter layer had no handle of its own.** A terrain registered LOD, cull, distance and water
  parameters. Its layers become procedural objects with no node (`valley_fungi`), so neither a
  node's `emissiveBoost` (ADR-903 fixes that for the whole terrain) nor an entity lane effect could
  single one out. GV3's only control over its mushrooms was `material/glowmereTissue/*`, which moves
  all three mushroom layers -- and any hero part on the same program -- together.
- **A scatter layer could not take a field.** A procedural node's `emissiveField` (emission x
  `1 + amount * field(p)`, applied after the program) had no scatter key.
- **A material layer's intensity was not a parameter.** `glowmereFirefliesCrown` and `...Scaled`
  keep their glow in a layer and write no base emission, so `material/<program>/emissionIntensity`
  -- the only one registered -- scaled nothing, and GV3's arcs on both programs (and on
  `paintedGround2`, which emits nowhere) bound and did nothing.
- **The ecology light was a file value.** `ecologyLight` (ADR-053) set the power of the lights the
  glowing layers cast, once, and was also part of the terrain's build key.

## Decision

**Per-layer lanes, by the layer's name.** For every scatter layer of a terrain:

| Parameter | JSON key on the layer | Effect |
|---|---|---|
| `nodes/<terrain>/scatter/<layer>/emissionGain` | `emissionGain` (default 1) | multiplies everything the layer emits, after its program |
| `nodes/<terrain>/scatter/<layer>/hueOffset` | `hueOffset` (turns, default 0) | rotates the layer's displayed hue (OKLCH), after its program |
| `nodes/<terrain>/scatter/<layer>/emissiveFieldAmount` | `emissiveFieldAmount` (default 0) | the depth of the layer's field |
| -- | `emissiveField` (a field's name) | emission x `1 + amount * field(p)` per fragment, after the program |

- The gain and hue ride the ADR-903 object lane of every part of the layer (part 0 and its material
  subs), times the terrain's own boost. The field is the procedural objects' emissive-field path,
  now a gain on the finished emission rather than a multiplier the program's emission drops.
- Named, not indexed, so a layer added in front of `fungi` does not move what
  `scatter/fungi/emissionGain` means. None of the four is structural: they are absent from
  `ScatterLayer::structuralHash`, so moving one replants nothing, and the keys are written only when
  set, so every existing scene round-trips byte-identically.
- **The light a layer casts follows the glow it shows.** Each `GlowCluster` records its layer; an
  ecology light's intensity is `power x scene/ecologyLight x the terrain's boost x the layer's gain`,
  and its colour is turned by the layer's `hueOffset`.

**`scene/ecologyLight` is a parameter** (default: the file's `ecologyLight`). The per-frame pass
reads its final, a save writes its base, and zero makes no lights. The glow clusters are now reduced
whenever a layer emits, whatever the gain, so a route can raise the light from zero, and the gain
leaves the terrain's build key -- changing it no longer rebuilds the terrain.

**A material layer's intensity is a parameter:** `material/<program>/layer/<i>/<name>/emissionIntensity`
(i 1-based, the layer's name in the path for ADR-232's reason), registered for every layer that
writes emission. **And an intensity that multiplies nothing is no longer registered:** the program's
own `emissionIntensity` exists only when its base writes emission.

**UI reach (the owner's rule: anything visible is findable, under a name for what the viewer
sees).** Every control here is a parameter, so both panels list it. The labels say what the picture
does, and the World panel Inspector now draws a label that is words (it drew only the bare path
below its heading, `ui::inspectorRowLabel`; a label with a slash, or equal to the leaf, keeps the row
exactly as before, which on main is every labelled parameter the Inspector shows):

| Control | Parameters panel: group -> section -> row | World panel Inspector: selection -> heading -> row |
|---|---|---|
| a layer's gain | nodes -> `<terrain>/scatter/<layer>` -> "glow" | the terrain (a click on a mushroom selects it) -> scatter -> `fungi/glow` |
| a layer's hue | same section -> "hue shift" | -> scatter -> `fungi/hue shift` |
| a layer's field depth | same section -> "light wave" | -> scatter -> `fungi/light wave` |
| the ecology light | scene -> (none) -> "light cast by glowing plants and fungi" | Atmosphere -> environment -> the same words |
| a material layer's intensity | material/`<program>` -> `layer/<i>/<name>` -> "glow" | Materials -> `<program>` -> layer -> `1/fireflies/glow` |

The layer's `emissiveField` (which field) is a name in the scene file, as every other reference
between scene objects is; the field it names is listed under the World panel's Fields, where its
own parameters (position, speed, width, falloff, and ADR-906's trigger numbers) are edited.

## Consequences

- **GV3 can address each mushroom layer** -- routes, keys and fields on `valley/scatter/fungi`,
  `shelf-fungi` and `beacons` separately -- and the light they cast moves with them.
- **GV3's dead arcs become loud.** Its tracks on `material/glowmereFirefliesCrown/emissionIntensity`,
  `material/glowmereFirefliesScaled/emissionIntensity` and `material/paintedGround2/emissionIntensity`
  now fail to bind with a load warning instead of binding in silence. The fireflies' arcs belong on
  `.../layer/1/fireflies/emissionIntensity`. The 15 GV2-family projects and `glowmere-stylized`/
  `glowmere-lyrics` saved values on the same inert paths (`paintedGround2`, `paintedGround`, the
  fireflies); those are dropped with a warning at load, which changes no pixel -- they never reached
  one.
- **A scene with `ecologyLight` 0 now reduces its glow clusters** at build (one `aggregateGlow` per
  emitting layer, logged as before) and makes no lights, as it always did.
- **The terrain's water** takes no lane (ADR-903).
- **The Inspector's rows for labelled parameters** read in words: `fungi/light wave` rather than
  `fungi/emissiveFieldAmount`. No row that existed on main changes (every label the Inspector could
  show there is a path or equal to its leaf).
