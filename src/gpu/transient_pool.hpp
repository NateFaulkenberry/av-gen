#pragma once

// Transient texture pool (ADR-016): passes acquire scratch textures by (size, format, usage) and
// release them when done; textures are reused within and across frames. This is the resource
// half of a frame graph; pass ordering stays explicit in the renderer for now.

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <vector>

namespace avgen::gpu {

class Context;

struct TransientTexture {
    wgpu::Texture texture;
    wgpu::TextureView view;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
    [[nodiscard]] bool valid() const { return texture != nullptr; }
};

class TransientPool {
public:
    explicit TransientPool(Context& context) : context_(context) {}

    // Returns a free texture matching the request (creating one if needed) and marks it in use.
    // The default usage includes CopySrc so a full-resolution result can be read back (EXR output).
    TransientTexture acquire(std::uint32_t width, std::uint32_t height, wgpu::TextureFormat format,
                             wgpu::TextureUsage usage = wgpu::TextureUsage::RenderAttachment |
                                                        wgpu::TextureUsage::TextureBinding |
                                                        wgpu::TextureUsage::CopySrc,
                             const char* label = "transient");
    // Marks a texture free for reuse (by handle identity).
    void release(const TransientTexture& texture);
    // Frees everything not used during the last `keepFrames` frames. Call once per frame.
    void endFrame(std::uint32_t keepFrames = 60);

    [[nodiscard]] std::size_t size() const { return entries_.size(); }
    [[nodiscard]] std::size_t inUse() const;
    [[nodiscard]] std::uint64_t allocations() const { return allocations_; }

private:
    struct Entry {
        TransientTexture texture;
        wgpu::TextureUsage usage;
        bool inUse = false;
        std::uint64_t lastUsedFrame = 0;
    };
    Context& context_;
    std::vector<Entry> entries_;
    std::uint64_t frame_ = 0;
    std::uint64_t allocations_ = 0;
};

} // namespace avgen::gpu
