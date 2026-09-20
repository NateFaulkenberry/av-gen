#pragma once

// ADR-500. The declarations `effect_registry.cpp`'s `builtinSchemas()` references.
//
// **This header and that list are two of the three lines a new effect costs outside its own file**
// -- the others being the enumerator in `AtmosphereKind` and its entry in `kAtmosphereKinds`.
// Everything an effect is otherwise
// (fields, ranges, labels, tooltips, panel rows, JSON, validation, default audio routes, style
// presets, resolution) is in `effects/<name>_effect.cpp`.
//
// An explicit list rather than a static self-registering constructor. These compile into a static
// library, and a translation unit nothing references is dropped by the linker -- a registry that is
// complete in one build and short in another is the worst failure this repository knows how to
// have. A declaration here is a reference.

#include "world/world_effects/effect_registry.hpp"

namespace avgen::world {

[[nodiscard]] const EffectSchema& cometSchema();
[[nodiscard]] const EffectSchema& auroraSchema();
[[nodiscard]] const EffectSchema& vortexSchema();
[[nodiscard]] const EffectSchema& meteorShowerSchema();
[[nodiscard]] const EffectSchema& volumetricFogSchema();

// Helpers the ported kinds share, so the three files that carry ADR-230's two sky kinds do not each
// re-spell the hue-cycle rows. Not a shared *list* in the sense ADR-392 condemned: each kind still
// names which of these it takes, and the rows it takes point at its own struct.
//
// `rainbowRows` is parameterised on the accessors because the comet's hue cycle lives in
// `e.comet.rainbow` and the aurora's in `e.aurora.rainbow` -- the same five questions about two
// different pieces of state, which is exactly the case a shared row cannot serve.

} // namespace avgen::world
