#pragma once

// The Feeds row (ADR-1580/1581/1631): four combos -- Beat, Hits, Levels, History -- each "Main mix", a source key or a
// role (ADR-1630: "role: drums", the first source of that role), drawn under the
// Live panel's Inputs table and the Audio clips popup's Stems section. Per-class hit feeds from the project file show
// as "custom" (choosing an entry replaces them). Edits go straight to the engine; the project's dirty check sees them.

#include "audio/source_role.hpp"

namespace avgen::app {
class Engine;
}

namespace avgen::ui {

void drawFeedsRow(app::Engine& engine);
// ADR-1630: a source's role (drums, bass, synth, vocal, other) as a combo. Returns whether it changed.
bool roleCombo(const char* label, audio::SourceRole& role);

} // namespace avgen::ui
