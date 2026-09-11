#pragma once

// Application settings (ADR-094, spec §7).
//
// AV Gen had no application-level settings before this: everything was per-subsystem and travelled
// with a project or a panel. §7 asks for a global Settings with an AI/Providers section, and this
// is the smallest thing that makes that possible while staying general rather than AI-shaped.
//
// It is a registry panel rather than a modal, because that is this editor's convention (ADR-076):
// panels dock, remember their place, and can be left open beside the work. A modal would be the
// only window in the application that behaves differently.
//
// ## What is here, and what is deliberately not
//
// The sections carry the settings that genuinely belong to the *installation* rather than to a
// project: how much of the canvas the world is rendered at, and the AI providers. Audio and Input
// are not sections yet, because their settings today are per-subsystem structs that live in the
// Control and Modulation panels and belong to the project -- inventing empty sections to match a
// list would be padding, and moving those settings is the editor pass's territory, not this one's.
//
// ## The credential rule, enforced in the UI too
//
// A key is entered into a masked field, stored in the system keystore, and the buffer is cleared.
// It is never read back for display -- §7 is explicit that a saved secret is never exposed -- so
// the field is always empty when you open it and the row says only *whether* a credential exists
// and *where it came from*.

#include <array>
#include <cstddef>
#include <functional>
#include <string>

namespace avgen::app {
class Engine;
struct AppSettings;
} // namespace avgen::app

namespace avgen::ai {
class ControlPlane;
} // namespace avgen::ai

namespace avgen::ui {

class SettingsPanel {
public:
    enum class Section : int { General = 0, Rendering, Ai, Advanced, Count };

    // Installed by the host.
    avgen::ai::ControlPlane* plane = nullptr;
    app::AppSettings* settings = nullptr;
    float* canvasRenderScale = nullptr;
    std::string settingsFile; // shown in Advanced so a person can find it
    // Persists the settings file. Called after any change, so there is no Save button to forget.
    std::function<void()> onChanged;

    void draw(app::Engine& engine);
    // Opens the panel on a section. The AI panel's "Open Settings" button calls this.
    void show(Section section) { section_ = section; }

private:
    void drawGeneral();
    void drawRendering();
    void drawAi();
    void drawAdvanced();

    Section section_ = Section::General;
    // One masked entry buffer per provider row, indexed alongside the provider list. Cleared the
    // moment its contents are handed to the keystore.
    static constexpr std::size_t kSecretCapacity = 512;
    static constexpr std::size_t kMaxProviders = 16;
    std::array<std::string, kMaxProviders> secrets_;
    std::array<std::string, kMaxProviders> endpoints_;
    std::array<std::string, kMaxProviders> models_;
    bool fieldsSeeded_ = false;
    std::string status_;
};

} // namespace avgen::ui
