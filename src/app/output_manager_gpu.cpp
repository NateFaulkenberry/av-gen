// GPU half of the OutputManager: windows, swapchains and presentation.
#include "app/output_manager.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/surface.hpp"
#include "platform/window.hpp"
#include "rendering/output_mapper.hpp"

namespace avgen::app {

struct OutputRuntime {
    std::unique_ptr<platform::Window> window;
    std::unique_ptr<gpu::Surface> surface; // declared after the window: destroyed first
};

std::uint32_t Output::pixelWidth() const { return runtime ? runtime->surface->width() : 0; }

std::uint32_t Output::pixelHeight() const { return runtime ? runtime->surface->height() : 0; }

Result<void> OutputManager::open(gpu::Context& context, gpu::ShaderLibrary& shaders) {
    if (!mapper_) {
        mapper_ = std::make_shared<rendering::OutputMapper>(context, shaders);
    }
    if (!mapper_->initialised()) {
        if (auto r = mapper_->init(); !r) {
            return std::unexpected(r.error());
        }
    }
    Result<void> first{};
    for (auto& o : outputs_) {
        if (!o->desc.enabled || o->open()) {
            continue;
        }
        const auto& d = o->desc;
        platform::WindowDesc wd;
        wd.title = "avgen output: " + d.name;
        wd.width = d.width;
        wd.height = d.height;
        wd.resizable = !d.fullscreen;
        wd.displayIndex = d.display;
        wd.fullscreen = d.fullscreen;
        wd.borderless = d.borderless;
        wd.alwaysOnTop = d.alwaysOnTop;
        auto runtime = std::make_shared<OutputRuntime>();
        auto window = platform::Window::create(wd);
        if (!window) {
            o->lastError = window.error().message;
            log::error("output '{}': {}", d.name, o->lastError);
            if (first) {
                first = std::unexpected(window.error());
            }
            continue;
        }
        runtime->window = std::move(*window);
        auto surface = gpu::Surface::create(context, runtime->window->metalLayer(), runtime->window->pixelWidth(),
                                            runtime->window->pixelHeight());
        if (!surface) {
            o->lastError = surface.error().message;
            log::error("output '{}': {}", d.name, o->lastError);
            if (first) {
                first = std::unexpected(surface.error());
            }
            continue;
        }
        runtime->surface = std::move(*surface);
        o->runtime = std::move(runtime);
        o->lastError.clear();
        log::info("output '{}' open: {}x{} px on display {}", d.name, o->pixelWidth(), o->pixelHeight(),
                  o->runtime->window->displayIndex());
    }
    return first;
}

void OutputManager::closeAll() {
    for (auto& o : outputs_) {
        o->runtime.reset();
    }
}

void OutputManager::pumpEvents() {
    // Empty when the primary window already pumped this frame; needed when there is none (tests).
    platform::Window::pumpEvents(nullptr);
    for (auto& o : outputs_) {
        if (!o->open()) {
            continue;
        }
        auto& rt = *o->runtime;
        const auto events = rt.window->takeEvents();
        if (rt.window->wantsClose()) {
            log::info("output '{}' closed by the user", o->desc.name);
            o->runtime.reset();
            o->desc.enabled = false;
            continue;
        }
        if (events.resized) {
            rt.surface->configure(rt.window->pixelWidth(), rt.window->pixelHeight());
        }
    }
}

Result<void> OutputManager::presentAll(gpu::Context& context, const wgpu::Texture& finalTexture, std::uint32_t width,
                                       std::uint32_t height) {
    if (!mapper_ || !mapper_->initialised()) {
        return openCount() == 0 ? Result<void>{} : fail("OutputManager::presentAll before open");
    }
    if (!finalTexture || width == 0 || height == 0) {
        return fail("OutputManager::presentAll: empty final texture");
    }
    wgpu::TextureViewDescriptor viewDesc{};
    viewDesc.label = "final-frame-view";
    viewDesc.dimension = wgpu::TextureViewDimension::e2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    const wgpu::TextureView sourceView = finalTexture.CreateView(&viewDesc);

    Result<void> first{};
    for (auto& o : outputs_) {
        if (!o->open()) {
            continue;
        }
        auto& rt = *o->runtime;
        if (rt.window->minimised() || !rt.surface->configured()) {
            continue;
        }
        auto target = rt.surface->acquire();
        if (!target) {
            o->lastError = target.error().message;
            if (first) {
                first = std::unexpected(target.error());
            }
            continue;
        }
        wgpu::CommandEncoder encoder = context.device().CreateCommandEncoder();
        if (auto r = mapper_->draw(encoder, sourceView, *target, rt.surface->width(), rt.surface->height(),
                                   o->desc.mapping, rt.surface->format());
            !r) {
            o->lastError = r.error().message;
            if (first) {
                first = r;
            }
            continue;
        }
        wgpu::CommandBuffer commands = encoder.Finish();
        context.queue().Submit(1, &commands);
        rt.surface->present();
        ++o->framesPresented;
        o->lastError.clear();
    }
    return first;
}

} // namespace avgen::app
