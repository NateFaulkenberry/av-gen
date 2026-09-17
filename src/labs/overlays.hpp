#pragma once

// Each lab's overlays (ADR-260, spec §8).
//
// §8 asked for "a common debug drawing system". **It already exists** -- `rendering::DebugDraw`,
// `rendering::buildDebugGeometry` and `rendering::DebugViewOptions` (ADR-031) have been carrying
// twenty-one switches since the renderer forensics, and building a second one would have been the
// duplication this whole architecture was meant to prevent. What did not exist is the sentence
// immediately after it in the spec: *"Do not display everything simultaneously. Each lab should
// selectively enable relevant overlays."* That selection is this file, and it is the only thing
// here.
//
// **Two switches are deliberately absent from every profile.** `DebugViewOptions::lod` and
// `::culling` have checkboxes in `world_panel.cpp` and `debug_visualizer.cpp` never reads either
// field, so both draw nothing. That is ADR-225's defect exactly -- a control wired to nothing --
// and turning them on in the LOD and Visibility profiles would have made the suite's own launcher
// the place the defect was hidden (§37). They stay off until the scatter cull's per-instance state
// is readable, which is the Visibility Lab's first work item and is recorded as such in
// `docs/engineering-labs.md`.

#include "labs/lab.hpp"
#include "rendering/debug_view_options.hpp"

namespace avgen::labs {

// The overlays worth having on while asking this lab's question, and no others. A caller is free
// to turn more on; the point of the profile is where it *starts*.
[[nodiscard]] rendering::DebugViewOptions overlaysFor(LabId id);

} // namespace avgen::labs
