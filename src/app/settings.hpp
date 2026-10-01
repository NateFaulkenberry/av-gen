#pragma once

// Application settings (ADR-094, spec §7).
//
// ## Why this exists now
//
// AV Gen had no application-level settings. `RenderSettings`, `PlacementSettings` and
// `ViewportControlSettings` are per-subsystem structs that travel with a project or with a panel,
// and the editor layout is stored separately again. That was fine until something needed to be
// configured *per installation rather than per project* -- which is exactly what an AI provider
// credential is, and exactly what §7 asks for: "Credentials live in the application's existing
// global Settings, not in the AI panel."
//
// So this is the smallest thing that makes §7 possible, kept general rather than AI-shaped. It is
// a container of sections; AI is one of them.
//
// ## The storage rule that matters
//
// §35: conversation history belongs to a project or session; credentials belong to global
// settings; **never mix those storage domains**. This file is the global half, and it is also
// where the "no secrets in files" rule is finally enforced -- `ai::ProviderConfig` has no field
// for a secret and `ProviderConfig::fromJson` refuses a document that carries one, so a settings
// file cannot acquire a credential even by hand-editing without the load failing loudly.
//
// A project file carries none of this. Opening someone else's project cannot change which provider
// you use, and cannot carry a key.

#include "ai/control_plane.hpp"
#include "core/error.hpp"
#include "ui/output_preview.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>

namespace avgen::app {

enum class AppearanceTheme : std::uint8_t { System, Dark, Light };

[[nodiscard]] const char* appearanceThemeName(AppearanceTheme theme);
[[nodiscard]] bool appearanceThemeFromName(const std::string& name, AppearanceTheme& out);

// ADR-1026: how the projection's picture meets a screen of another shape. Fit letterboxes (the whole frame, black
// bars), Fill crops (the screen full, the frame's edges lost), Stretch distorts (the existing outputs' identity).
enum class ProjectionScaling : std::uint8_t { Fit, Fill, Stretch };

[[nodiscard]] const char* projectionScalingName(ProjectionScaling scaling);
[[nodiscard]] bool projectionScalingFromName(const std::string& name, ProjectionScaling& out);

struct AppSettings {
    static constexpr const char* kFormatName = "avgen-settings";
    static constexpr int kFormatVersion = 1;

    // ---- general ----
    // How much of the canvas's pixels the world is rendered at before being shown stretched
    // (ADR-084). It lives here because it is a property of this machine and this display, not of
    // the project: the same project on a laptop and on a workstation wants different answers.
    float canvasRenderScale = 1.0f;
    // §15-§17. Whether the editor is allowed to lower the *scene's* resolution on its own when the
    // GPU cannot hold the budget, filtering the result back up into the canvas the person asked
    // for (`app::InteractiveResolution`). On by default, which needs the justification it gets in
    // that header: at rung 0 it changes nothing at all, and it only leaves rung 0 on a frame that
    // would otherwise be missing the budget by a factor of two or more. It lives here rather than
    // in the project for `canvasRenderScale`'s reason -- it is a property of this machine and this
    // display, and opening somebody else's project must not change how you are watching yours.
    //
    // The two are separate levers on purpose. `canvasRenderScale` is a manual reduction of the
    // *canvas target*, which the person chooses and the application never overrides; this one moves
    // `QualitySettings::renderScale`, the renderer's own scene-below-output scale, and the two
    // compose rather than fight.
    bool adaptiveCanvasScale = true;
    // The GPU frame time the controller aims at, in milliseconds. One frame at 60 Hz (§3's
    // playhead budget). Nothing below about 8 ms is reachable on the content this was measured on,
    // because the frame has a ~4.6 ms floor that no resolution can touch.
    double adaptiveCanvasBudgetMs = 16.67;
    // ADR-1024. The lowest scale the controller may reach, one of `kRenderScaleRungs`. 0.5 is the
    // ladder's own floor and the default. Raising it is the trade the adaptive scale cannot make for
    // you: measured on the Sonic Garden (AA-RESEARCH.md), thin geometry -- 4-5 px rings -- beads at
    // 0.71 and breaks into dots at 0.5, while the frame gets 38% cheaper rather than 75%, because
    // a third of it (shadows) does not scale with resolution. A per-machine choice, like the budget.
    float adaptiveCanvasFloor = 0.5f;
    // ADR-1024. Edge antialiasing for the live viewport: FXAA at no less than `kLiveAntialiasFloor`,
    // or off (the scene's own `post/output/antialias` only). A render never sees it.
    bool liveAntialias = true;
    AppearanceTheme appearance = AppearanceTheme::System;
    // ADR-320/ADR-225: the Render panel shows the frames a render is writing. Off by default --
    // it is an instrument, and an instrument is never the reason a deliverable costs more -- but a
    // person who turns it on has turned it on, and a toggle the application forgets between
    // sessions is not a setting. Here rather than in the project for the same reason
    // `canvasRenderScale` is: opening someone else's project must not change how you are watching
    // your own render.
    bool renderFramePreview = false;
    // ADR-364: while an offline render or a path trace is running, stop drawing the world into the
    // viewport. On by default, because that is what a person doing a final render wants and the
    // brief asks for it as a hard requirement -- but reachable in one click, because the opposite
    // was a shipped, documented feature ("renders load the saved project; the live view keeps
    // playing") and a person iterating wants it back. Here rather than in the project for the same
    // reason `canvasRenderScale` is: it is how you work, not what the piece is.
    bool suspendViewportDuringRender = true;

    // ---- output preview (ADR-246) ----
    // The editor-local half of the output preview: which of the three view modes the canvas is in,
    // the zoom, the guides, the letterbox treatment. It lives here rather than in the project for
    // the same reason `canvasRenderScale` does -- it describes how this person is looking at the
    // piece on this machine, and opening someone else's project must not change how you are
    // looking at yours.
    //
    // The project-owned half -- the output's width, height and frame rate -- is `RenderSettings`,
    // in the project under "render", and is deliberately not duplicated here. One output
    // configuration; a second would be one that drifts.
    //
    // ADR-225: a setting the application does not keep is not a setting, which is what this block
    // and `test_output_preview_settings` are between them for. `panX`/`panY` and `fullscreen` are
    // the exceptions and are *not* written: a pan only means anything against the canvas size it
    // was made at, and a session that exits in fullscreen must not reopen with every panel closed
    // and no memory of which ones they were.
    ui::PreviewViewState preview;

    // ---- live Sonic input (ADR-1025) ----
    // The devices a person plays through on this machine, so "plug in the keyboard and it works" survives a
    // restart. Not in the project: the same live project must open on another machine with that machine's rig.
    struct LiveInput {
        std::string audioInput;     // capture device name (substring match); "" = none chosen yet
        std::string midiInput = "*"; // MIDI source filter: "*" every source, else a name substring
        float smoothing = 1.0f;     // multiplier on the Sonic Character's time constants (0.25..4)
    } live;

    // ---- projection (ADR-1026) ----
    // The Live panel's "Start projection": which screen the clean output window goes to and how. This machine's, for
    // the reason the devices above are: the projector is plugged into this Mac, not into the project.
    struct Projection {
        std::string display;            // display name; "" = automatic (the first non-primary display, else the primary)
        std::optional<bool> fullscreen; // unset: fullscreen exactly when the chosen display is not the primary
        std::uint32_t windowWidth = 0;  // windowed size in points; 0 = the display's own size ("match display")
        std::uint32_t windowHeight = 0;
        ProjectionScaling scaling = ProjectionScaling::Fit;
    } projection;

    // ---- ai ----
    ai::AiSettings ai;

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Result<AppSettings> fromJson(const nlohmann::json& doc);

    // Written beside-and-renamed, so a crash mid-write cannot leave a half-file that later reads
    // as corrupt settings -- the same discipline ADR-065 applied to the inference cache.
    [[nodiscard]] Result<void> save(const std::filesystem::path& path) const;
    // A missing file is not an error: it is a first run, and the defaults are the answer.
    [[nodiscard]] static Result<AppSettings> load(const std::filesystem::path& path);

    // `<preferences>/settings.json`, beside `recent.json` and `editor-layout.json`. The directory
    // is passed in rather than looked up, because it comes from SDL and this file has to link into
    // the test binary, which has no window layer. Empty in, empty out: settings are then
    // session-only, which is the right answer for a platform with no preferences directory.
    [[nodiscard]] static std::filesystem::path pathIn(const std::filesystem::path& preferencesDir);
};

} // namespace avgen::app
