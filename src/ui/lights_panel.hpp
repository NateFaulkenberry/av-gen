#pragma once

// The Lights panel: creating, choosing and shaping a scene's authored lights.
//
// The controls this draws were already reachable -- ADR-358 registered a light's knobs as ordinary
// parameters, so each is a row in the Parameters panel, a modulation target, a timeline key and a
// save entry. ADR-375's point applies here with more force than it did to the wind: `lights/` is a
// path prefix, so the flat list interleaves four lights' twenty parameters each in registration
// order, and the question "how bright is the key light" is answered somewhere between
// `lights/cosmic-fill/width` and `lights/glowmere-rim/azimuth`.
//
// What is genuinely new, and is the reason this panel is not only a re-grouping: **nothing in the
// application could bring a light into being.** `Composition::setAuthoredLights` had exactly one
// caller, the scene-file parser, so authoring a light meant editing JSON by hand. Add, duplicate,
// rename and delete are structural edits and they live here.
//
// The boundary is the brief's §21 and it is deliberate: this panel is for **authored local lights**
// -- point, spot, directional, area. Sky, fog, ambient and environment intensity belong to the
// Environment panel (ADR-375), which is where they already are.
//
// ImGui-only. Everything with an answer that does not need a window -- what a new light's placement
// is, what a duplicate is named, which parameter paths a light of a given type has -- is in
// `lights_panel_logic.hpp` beside it, so it can be tested without one.

#include "scene/scene_types.hpp"

#include <string>
#include <vector>

namespace avgen::app {
class Engine;
}

namespace avgen::ui {

class Selection;
class EditHistory;

// "Lights": the scene's authored lights, and the controls that shape them.
void drawLightsPanel(app::Engine& engine, Selection& selection, EditHistory& history);

} // namespace avgen::ui
