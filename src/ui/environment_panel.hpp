#pragma once

// ADR-375, revised by ADR-387: the Environment panel.
//
// Everything it shows was already reachable: registering a parameter puts it in the Parameters
// panel, makes it a modulation target, a timeline key and a save entry (ADR-011), which is what
// ADR-350 requires. Reachable is not the same as findable. The Parameters panel is one flat list
// grouped by path prefix, so "how windy is it" is a scroll through `scene/` between `stylized` and
// `volumeAbsorption`.
//
// ADR-387 removed the Tree panel that shipped beside this one and cut this panel back to what is
// generic. The rule the owner set is that AV Gen's first-class UI represents reusable engine
// concepts, and that a scene's artistic composition is data inside those systems rather than a new
// panel. What is left here is world state that every scene has -- the sky, the fog, and what the
// air is doing (ADR-055). What left:
//
//   * the cosmic vortex      -> an authored atmospheric effect, in the World Effects panel
//   * a node's wind response -> the World panel's Inspector, which edits the selected object's
//                               own parameters and groups them by the sub-prefix they register
//                               under, so `nodes/<n>/wind/*` is a "wind" group on that node
//   * falling leaves, motes  -> the same Inspector: a particle node is an object
//   * tree energy, shimmer   -> the same Inspector, for the same reason
//
// None of those needed a bespoke panel; they needed the generic one to be editable, which it now
// is. The name-sniffing that found them ("a path containing 'leaf'") went with them.

namespace avgen::app {
class Engine;
}

namespace avgen::ui {

// "Environment": the sky, the fog and the wind field over this world.
void drawEnvironmentPanel(app::Engine& engine);

} // namespace avgen::ui
