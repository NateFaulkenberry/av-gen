#pragma once

// Dear ImGui + ImPlot glue for SDL3 and WebGPU/Dawn (ADR-007). Owns the ImGui context.

#include "core/error.hpp"
#include "gpu/render_target.hpp"
#include "app/settings.hpp"

#include <webgpu/webgpu_cpp.h>

#include <filesystem>
#include <memory>
#include <string>

union SDL_Event;

namespace avgen::gpu {
class Context;
}
namespace avgen::platform {
class Window;
}

namespace avgen::ui {

class ImGuiLayer {
public:
    // `iniFile` is where ImGui keeps the dock tree and the window geometry (ADR-076). Empty
    // disables it entirely, which is what the offline paths want: a render should not rearrange
    // somebody's editor.
    static Result<std::unique_ptr<ImGuiLayer>> create(platform::Window& window, gpu::Context& context,
                                                      const std::filesystem::path& iniFile = {});
    ~ImGuiLayer();
    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void processEvent(const SDL_Event& event);
    void newFrame();
    void applyTheme(app::AppearanceTheme theme);
    // Appends a render pass (LoadOp::Load) that draws the UI over `target`.
    void render(wgpu::CommandEncoder& encoder, const gpu::TargetView& target);

    // Drops every cached image bind group. The WebGPU backend keeps one per texture view it has
    // ever been shown and has no way to forget a single one, so a host that recreates its canvas
    // render target on every resize would otherwise retain every size it has ever been. Costs a
    // pipeline and font rebuild on the next frame; call it only when a view is actually retired.
    void forgetCachedTextures();

    // True when Dear ImGui would use the keyboard *for anything*, which with keyboard navigation
    // enabled means "a window has focus" -- in a docked UI, essentially always. Almost never the
    // question a caller wants: guarding shortcuts on it disables every shortcut in the application.
    [[nodiscard]] bool wantsKeyboard() const;
    // True when the user is typing into a field. This is the one that decides whether a shortcut
    // may be taken: Cmd+C in a text box is copy-the-text, and Delete is delete-a-character.
    [[nodiscard]] bool wantsTextInput() const;
    // True when a widget is being interacted with -- a slider mid-drag, a combo open. Arrow keys
    // belong to it while that lasts, so an editor nudge must not also fire.
    [[nodiscard]] bool itemActive() const;
    // True when any popup or menu is open, at any level. `itemActive` does not answer this: an open
    // menu is a popup window, not an active item, so a menu the user has just pulled down reports
    // no active item while plainly owning the arrow keys.
    [[nodiscard]] bool popupOpen() const;
    [[nodiscard]] bool wantsMouse() const;
    // Whether the ini file already existed when the context was made. False means a first run, and
    // the shell has to build its default dock tree rather than trust an empty one.
    [[nodiscard]] bool hadSavedLayout() const { return hadSavedLayout_; }

private:
    ImGuiLayer() = default;
    bool initialised_ = false;
    bool hadSavedLayout_ = false;
    // ImGui stores the pointer, not the characters, so this has to outlive the context.
    std::string iniPath_;
};

} // namespace avgen::ui
