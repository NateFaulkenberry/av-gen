#pragma once

// Internal transport interface behind share::TextureShare. Backends own every native resource
// (IOSurfaces, Metal objects, staging buffers, library handles) and release them in close().

#include "share/texture_share.hpp"

namespace avgen::share {

class ShareBackend {
public:
    virtual ~ShareBackend() = default;
    virtual Result<void> open(gpu::Context& context, const std::string& name) = 0;
    virtual Result<void> publish(const wgpu::Texture& source, std::uint32_t w, std::uint32_t h) = 0;
    virtual void close() = 0;
    virtual void setFrameRate(int numerator, int denominator) = 0;
    [[nodiscard]] virtual ShareStats stats() const = 0;
};

// Syphon (share/syphon_share.mm on Apple platforms; the stub returns nullptr elsewhere).
bool syphonAvailable();
std::string syphonDescribe();
std::unique_ptr<ShareBackend> createSyphonBackend();

// NDI (share/ndi_share.cpp): available when the runtime library loads (share/ndi_runtime.*).
bool ndiAvailable();
std::string ndiDescribe();
std::unique_ptr<ShareBackend> createNdiBackend();

// Shared helpers.
inline bool isShareableFormat(wgpu::TextureFormat format) {
    return format == wgpu::TextureFormat::RGBA8Unorm || format == wgpu::TextureFormat::BGRA8Unorm;
}

} // namespace avgen::share
