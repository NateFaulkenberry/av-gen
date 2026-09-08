#include "ui/imgui_layer.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "platform/window.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_wgpu.h>
#include <implot.h>

namespace avgen::ui {

Result<std::unique_ptr<ImGuiLayer>> ImGuiLayer::create(platform::Window& window, gpu::Context& context) {
    if (!context.hasSurface()) {
        return fail("ImGui requires a window surface");
    }
    std::unique_ptr<ImGuiLayer> layer(new ImGuiLayer());
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // no layout persistence in 0.1

    const float scale = window.pixelScale();
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(scale);
    style.WindowRounding = 6.0f * scale;
    style.FrameRounding = 4.0f * scale;
    io.FontGlobalScale = 1.0f;
    ImFontConfig fontConfig;
    fontConfig.SizePixels = 15.0f * scale;
    io.Fonts->AddFontDefault(&fontConfig);

    if (!ImGui_ImplSDL3_InitForOther(window.handle())) {
        return fail("ImGui_ImplSDL3_InitForOther failed");
    }
    ImGui_ImplWGPU_InitInfo info;
    info.Device = context.device().Get();
    info.NumFramesInFlight = 3;
    info.RenderTargetFormat = static_cast<WGPUTextureFormat>(context.surfaceFormat());
    info.DepthStencilFormat = WGPUTextureFormat_Undefined;
    if (!ImGui_ImplWGPU_Init(&info)) {
        return fail("ImGui_ImplWGPU_Init failed");
    }
    layer->initialised_ = true;
    log::info("ImGui {} + ImPlot ready (scale {:.2f})", IMGUI_VERSION, scale);
    return layer;
}

ImGuiLayer::~ImGuiLayer() {
    if (initialised_) {
        ImGui_ImplWGPU_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
}

void ImGuiLayer::processEvent(const SDL_Event& event) { ImGui_ImplSDL3_ProcessEvent(&event); }

void ImGuiLayer::newFrame() {
    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::render(wgpu::CommandEncoder& encoder, const gpu::TargetView& target) {
    ImGui::Render();
    wgpu::RenderPassColorAttachment color{};
    color.view = target.view;
    color.loadOp = wgpu::LoadOp::Load;
    color.storeOp = wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor pass{};
    pass.label = "ui-pass";
    pass.colorAttachmentCount = 1;
    pass.colorAttachments = &color;
    wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), rp.Get());
    rp.End();
}

bool ImGuiLayer::wantsKeyboard() const { return ImGui::GetIO().WantCaptureKeyboard; }
bool ImGuiLayer::wantsMouse() const { return ImGui::GetIO().WantCaptureMouse; }

} // namespace avgen::ui
