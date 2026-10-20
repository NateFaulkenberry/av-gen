#pragma once

// The Feeds row (ADR-1580/1581): three combos -- Beat, Hits, Levels -- each "Main mix" or a source key, drawn under the
// Live panel's Inputs table and the Audio clips popup's Stems section. Per-class hit feeds from the project file show
// as "custom" (choosing an entry replaces them). Edits go straight to the engine; the project's dirty check sees them.

namespace avgen::app {
class Engine;
}

namespace avgen::ui {

void drawFeedsRow(app::Engine& engine);

} // namespace avgen::ui
