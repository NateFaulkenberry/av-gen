#include "ui/imgui_layer.hpp"
#include "ui/theme.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "platform/window.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_wgpu.h>
#include <implot.h>

#include <system_error>

namespace avgen::ui {

Result<std::unique_ptr<ImGuiLayer>> ImGuiLayer::create(platform::Window& window, gpu::Context& context,
                                                       const std::filesystem::path& iniFile) {
    if (!context.hasSurface()) {
        return fail("ImGui requires a window surface");
    }
    std::unique_ptr<ImGuiLayer> layer(new ImGuiLayer());
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    // ImGui owns the dock tree and the window geometry; avgen owns which panels are open
    // (ADR-076). Asked before the file is written, because the answer is "has this user ever
    // arranged the editor", and after the first save it is yes forever.
    if (iniFile.empty()) {
        io.IniFilename = nullptr;
    } else {
        std::error_code ec;
        std::filesystem::create_directories(iniFile.parent_path(), ec);
        layer->hadSavedLayout_ = std::filesystem::exists(iniFile, ec);
        layer->iniPath_ = iniFile.string();
        io.IniFilename = layer->iniPath_.c_str();
    }

    // SDL3 reports logical points on macOS; ImGui 1.92's dynamic fonts rasterise at the
    // framebuffer scale automatically, so styles and font sizes stay in points.
    const float scale = window.pixelScale();
    ui::applyTheme(app::AppearanceTheme::System);
    ImFontConfig fontConfig;
    fontConfig.SizePixels = 15.0f;
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

void ImGuiLayer::applyTheme(app::AppearanceTheme theme) { ui::applyTheme(theme); }

ImGuiLayer::~ImGuiLayer() {
    if (initialised_) {
        ImGui_ImplWGPU_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
}

void ImGuiLayer::forgetCachedTextures() {
    // There is no finer-grained call. The backend rebuilds the pipeline and re-uploads the font
    // atlas from ImGui_ImplWGPU_NewFrame, which is the very next thing this layer does.
    ImGui_ImplWGPU_InvalidateDeviceObjects();
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
