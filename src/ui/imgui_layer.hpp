#pragma once

// Dear ImGui + ImPlot glue for SDL3 and WebGPU/Dawn (ADR-007). Owns the ImGui context.

#include "core/error.hpp"
#include "gpu/render_target.hpp"

#include <webgpu/webgpu_cpp.h>

#include <memory>

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
    static Result<std::unique_ptr<ImGuiLayer>> create(platform::Window& window, gpu::Context& context);
    ~ImGuiLayer();
    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void processEvent(const SDL_Event& event);
    void newFrame();
    // Appends a render pass (LoadOp::Load) that draws the UI over `target`.
    void render(wgpu::CommandEncoder& encoder, const gpu::TargetView& target);

    [[nodiscard]] bool wantsKeyboard() const;
    [[nodiscard]] bool wantsMouse() const;

private:
    ImGuiLayer() = default;
    bool initialised_ = false;
};

} // namespace avgen::ui
