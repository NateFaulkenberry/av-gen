#pragma once

// The World Effects panel (ADR-204).
//
// The brief's rule for this panel is that it must be usable by somebody who is not a rendering
// programmer, and the way it meets that is a split rather than a shorter list of knobs:
//
//   * **Above the fold**: the eight or so things an author actually reaches for -- on/off, style,
//     colour, intensity, width, sparkle, and how hard it answers the beat.
//   * **Advanced**, collapsed: everything else, which is still every parameter, because a control
//     that only a preset can reach is a control the user does not own.
//
// Two things it deliberately does not do. It does not invent a second way to edit a parameter: every
// slider here writes the *base* value of an ordinary `worldfx/...` parameter, so a keyframe, a
// preset, a cue and a route all keep working and the Parameters panel shows the same numbers. And it
// does not invent a second way to react to music: **Beat response** is a slider that writes an
// ordinary `beat.pulse -> worldfx/<name>/intensity` modulation route, visible and editable in the
// Modulation panel like any other, rather than a hidden audio hook inside the effect.

#include "world/effect_params.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>

namespace avgen::app {
class Engine;
}

namespace avgen::ui {

class WorldEffectsPanel {
public:
    void draw(app::Engine& engine);

private:
    void drawEffect(app::Engine& engine, const world::WorldEffect& authored, std::size_t index);
    void drawAdvanced(app::Engine& engine, const world::WorldEffect& authored, std::size_t index);
    // Structural edits (source, activation, propagation kind, direction) and styles go through
    // here: they rewrite the authored set on the composition and re-register the parameters, which
    // is what a style has to do for its values to become the new defaults. Ordinary parameter edits
    // never come this way -- they write a base value and are done.
    //
    // It captures the current parameter *bases* onto the authored set before applying `edit`, so a
    // structural change does not quietly discard every slider somebody has moved.
    void commit(app::Engine& engine, std::size_t index, const std::function<void(world::WorldEffect&)>& edit);

    std::string status_;
    int pendingRemove_ = -1;
};

// ---- the pieces worth testing without Dear ImGui -------------------------------------------------
//
// Inline and ImGui-free, for the reason `ui_logic.hpp` exists: the question "what route does the
// Beat response slider actually write" is answerable without a window, and a test that needed one
// would not be written.

// The modulation route a **Beat response** slider owns: `beat.pulse` onto this effect's intensity.
[[nodiscard]] inline std::string beatResponseSource() { return "beat.pulse"; }
[[nodiscard]] inline std::string beatResponseTarget(const std::string& effectName) {
    return world::worldEffectParameterPrefix(effectName) + "intensity";
}
// How much of the parameter's own soft range one unit of beat response is worth. A slider in 0..1
// has to mean something in the target's units, and "the whole of what the slider offers" is the
// answer that makes 1.0 read as "as much as this knob goes".
[[nodiscard]] inline float beatResponseDepth(float amount, float softRange) {
    return std::clamp(amount, 0.0f, 1.0f) * std::max(softRange, 0.0f);
}

} // namespace avgen::ui
