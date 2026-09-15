#pragma once

#include "app/settings.hpp"

#include <imgui.h>

namespace avgen::ui {

enum class ResolvedTheme { Dark, Light };

[[nodiscard]] ResolvedTheme resolveTheme(app::AppearanceTheme preference);
[[nodiscard]] const char* appearanceThemeName(app::AppearanceTheme theme);
[[nodiscard]] bool appearanceThemeFromName(const std::string& name, app::AppearanceTheme& out);

// ---- the semantic palette ----------------------------------------------------------------------
//
// Colour in this application means something or it is not used. Before this struct every panel
// picked its own literals -- `IM_COL32(33, 48, 70, 235)` for an audio clip, `IM_COL32(96, 150, 110,
// 220)` for a selected actor, `IM_COL32(255, 220, 120, 230)` for the playhead -- so "selected" was
// a different colour in each of the four lanes of one strip, and none of them moved when the theme
// did. A reader cannot learn a language whose words change per speaker.
//
// The roles below are the whole vocabulary. A panel that needs a colour picks the role that says
// what it means; if no role says it, the answer is usually that the thing did not need a colour.
//
// Two deliberate omissions. There is **no per-track or per-kind hue ramp** here: the strip's lanes
// are told apart by position and label, which is what a professional editor does, and a rainbow of
// lanes is decoration pretending to be information. And there is no `disabled` background -- a
// disabled control is the normal one with `textDisabled` on it, because a control that changes
// shape when it is unavailable is harder to find again when it is not.
struct Palette {
    // Surfaces, darkest first. Four is enough to express depth; a fifth would be a shade nobody
    // could name.
    ImU32 ground = 0;    // behind everything: the dockspace, the empty canvas
    ImU32 panel = 0;     // a panel's own background
    ImU32 lane = 0;      // a timeline lane, a list's alternate row: inset from the panel
    ImU32 raised = 0;    // a block sitting on a lane, a button

    ImU32 border = 0;    // a divider that must be found rather than seen
    ImU32 borderStrong = 0; // the outline of something selected or being dragged

    ImU32 text = 0;
    ImU32 textMuted = 0;     // a label that is context rather than content
    ImU32 textDisabled = 0;

    // The interaction states, in the order a pointer meets them. Each is a *fill*; the text on it
    // stays `text` in every one, which is what keeps a row from jumping as the pointer crosses it.
    ImU32 hover = 0;
    ImU32 selected = 0;
    ImU32 active = 0;   // pressed, or being dragged
    ImU32 playing = 0;  // under the playhead right now: a wash, not an outline

    // Meaning, not decoration.
    ImU32 accent = 0;
    ImU32 accentMuted = 0;
    ImU32 processing = 0;
    ImU32 warning = 0;
    ImU32 error = 0;
    ImU32 success = 0;

    // The playhead is its own role because it is the one thing on the strip that must never be
    // mistaken for a clip boundary, a section edge or a marker -- and all three of those are
    // vertical lines too.
    ImU32 playhead = 0;
};

// The palette `applyTheme` last installed. Valid from the first `applyTheme` call; before that it
// is the dark palette, so a panel drawn by a tool that never applied a theme still gets coherent
// colours rather than zeroes.
[[nodiscard]] const Palette& palette();

// Applies the complete global palette and metrics. Call after ImGui::CreateContext and again only
// when the preference or resolved system appearance changes.
void applyTheme(app::AppearanceTheme preference);

} // namespace avgen::ui
