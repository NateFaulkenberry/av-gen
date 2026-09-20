#pragma once

// ADR-387: one widget for "edit this parameter", shared by every panel that offers one.
//
// The Parameters panel had the only implementation -- the kind switch, the component fan-out, the
// base-not-final write-back and the "= 0.312" annotation when a route is moving it. Every other
// panel that wanted an editable row either reached for a bare `ImGui::SliderFloat` over a value it
// had read itself, or did not offer one. The second is why the Tree panel existed at all: the World
// panel's Inspector could say what was moving a node and could not let anybody change it.
//
// Writing the *base* value rather than the final is the whole of the subtlety. The final is this
// frame's modulated value; writing it is overwritten by the next route evaluation, and a slider
// that visibly snaps back is how a panel loses somebody's trust.

#include "params/parameter.hpp"

namespace avgen::ui {

// Draws the value editor for one parameter and writes any change into its base value. Returns true
// on the frame it changed. `label` overrides the parameter's own; null uses `param.label()`.
//
// Does not push an id: the caller owns that, because the same parameter can legitimately be drawn
// in two places in one frame (a panel section and the Parameters list).
bool drawParameterValue(params::IParameter& param, const char* label = nullptr);

} // namespace avgen::ui
