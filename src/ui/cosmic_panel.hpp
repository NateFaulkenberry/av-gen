#pragma once

// ADR-375: the Environment and Tree panels the brief's §16 asks for.
//
// Everything these show was already reachable: registering a parameter puts it in the Parameters
// panel, makes it a modulation target, a timeline key and a save entry (ADR-011), which is what
// ADR-350 requires. Reachable is not the same as findable. The Parameters panel is one flat list
// grouped by path prefix, so "how windy is it" is a scroll through `scene/` between `stylized` and
// `volumeAbsorption`, and the vortex's fourteen controls sit in a block whose order is the order
// somebody happened to register them in.
//
// The test for these panels is not that a control exists. It is that somebody who has not read the
// brief can find it, which means the grouping has to match how the work is thought about -- wind,
// leaves, the vortex -- rather than how the parameters are named.

namespace avgen::app {
class Engine;
}

namespace avgen::ui {

// "Environment": the sky and the cosmic phenomenon under the island.
void drawEnvironmentPanel(app::Engine& engine);

// "Tree": what the tree does -- how it moves, what it sheds.
void drawTreePanel(app::Engine& engine);

} // namespace avgen::ui
