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
#include "app/camera_director.hpp"
#include "core/error.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <cstdint>
#include <string>

namespace avgen::app {

enum class AppearanceTheme : std::uint8_t { System, Dark, Light };

[[nodiscard]] const char* appearanceThemeName(AppearanceTheme theme);
[[nodiscard]] bool appearanceThemeFromName(const std::string& name, AppearanceTheme& out);

struct AppSettings {
    static constexpr const char* kFormatName = "avgen-settings";
    static constexpr int kFormatVersion = 1;

    // ---- general ----
    // How much of the canvas's pixels the world is rendered at before being shown stretched
    // (ADR-084). It lives here because it is a property of this machine and this display, not of
    // the project: the same project on a laptop and on a workstation wants different answers.
    float canvasRenderScale = 1.0f;
    AppearanceTheme appearance = AppearanceTheme::System;

    // ---- ai ----
    ai::AiSettings ai;

    // ---- the auto-director ----
    // Every control in the Auto-director panel. It lived only in `DirectorState`, which is a
    // member of the running `Application` and nothing else, so a user who set a max swing of 8
    // deg/s and a hold of six shots got the defaults back on the next launch -- and the defaults
    // are "off" and "one shot", which is the setting those two controls exist to move away from.
    //
    // Per installation rather than per project, like the canvas scale above and for the same
    // reason: it is a statement about how fast *this viewer* wants a camera to move, not about
    // the piece. A project carries its cut as baked timeline tracks either way.
    AutoDirectorSettings director;

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
