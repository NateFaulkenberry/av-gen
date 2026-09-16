#pragma once

// A swapchain surface for one CAMetalLayer (milestone 1.2, multi-output). The Context owns the
// primary window's surface and delegates its surface API here; every further output window
// creates its own Surface on the same device. Only gpu/ knows about wgpu::Surface.

#include "core/error.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <memory>

namespace avgen::gpu {

class Context;

class Surface {
public:
    // Wraps `metalLayer` (CAMetalLayer*) in a surface on the context's device and configures it
    // to `width` x `height` pixels when both are non-zero.
    static Result<std::unique_ptr<Surface>> create(Context& context, void* metalLayer, std::uint32_t width,
                                                   std::uint32_t height);
    ~Surface();
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    // (Re)configures the swapchain to the given pixel size. No-op for a zero size or an unchanged one.
    void configure(std::uint32_t width, std::uint32_t height);
    // Acquires the current swapchain texture view. Reconfigures once on lost/outdated surfaces.
    [[nodiscard]] Result<wgpu::TextureView> acquire();
    void present();

    [[nodiscard]] wgpu::TextureFormat format() const { return format_; }
    // The texture behind the view the last `acquire()` handed out, kept so a capture can read the
    // finished frame back -- interface included. `renderToImage` re-renders the *scene*, which is
    // why it cannot answer "what does the editor look like": the UI is drawn into this texture and
    // into nothing else. Valid between an `acquire()` and the `present()` that follows it.
    [[nodiscard]] wgpu::Texture currentTexture() const { return current_; }
    [[nodiscard]] std::uint32_t width() const { return width_; }
    [[nodiscard]] std::uint32_t height() const { return height_; }
    [[nodiscard]] bool configured() const { return configured_; }
    [[nodiscard]] const wgpu::Surface& handle() const { return surface_; }

private:
    friend class Context;
    Surface(Context& context, wgpu::Surface surface);
    Result<void> chooseFormat();

    Context& context_;
    wgpu::Surface surface_;
    wgpu::Texture current_;
    wgpu::TextureFormat format_ = wgpu::TextureFormat::Undefined;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool configured_ = false;
};

} // namespace avgen::gpu
