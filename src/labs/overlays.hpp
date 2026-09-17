#pragma once

// Each lab's overlays (ADR-261, spec §8).
//
// §8 asked for "a common debug drawing system". **It already exists** -- `rendering::DebugDraw`,
// `rendering::buildDebugGeometry` and `rendering::DebugViewOptions` (ADR-031) have been carrying
// twenty-one switches since the renderer forensics, and building a second one would have been the
// duplication this whole architecture was meant to prevent. What did not exist is the sentence
// immediately after it in the spec: *"Do not display everything simultaneously. Each lab should
// selectively enable relevant overlays."* That selection is this file, and it is the only thing
// here.
//
// **One switch is still absent from every profile.** `DebugViewOptions::culling` has a checkbox in
// `world_panel.cpp` and `debug_visualizer.cpp` reads the field nowhere, so it draws nothing -- and
// turning it on in the Visibility profile would make the suite's own launcher the place that defect
// was hidden (§37, ADR-225).
//
// `::lod` was in the same state and is not any more: the LOD Lab wired it to
// `ProceduralRenderer::readLodLevels`, which is the rung the cull pass wrote for each record, and
// it is on in the LOD profile. It colours an object grey rather than rung 0 when the pass did not
// run for it that frame, because the alternative is an overlay that reports history.

#include "labs/lab.hpp"
#include "rendering/debug_view_options.hpp"

namespace avgen::labs {

// The overlays worth having on while asking this lab's question, and no others. A caller is free
// to turn more on; the point of the profile is where it *starts*.
[[nodiscard]] rendering::DebugViewOptions overlaysFor(LabId id);

} // namespace avgen::labs
