#pragma once

// GPU texture helpers: upload a scene::TextureData with a CPU-generated mip chain, samplers by
// (wrap, filter) key, and 1x1 defaults for absent material textures.

#include "core/error.hpp"
#include "scene/scene.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <unordered_map>

namespace avgen::gpu {

class Context;

struct GpuTexture {
    wgpu::Texture texture;
    wgpu::TextureView view;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mipLevels = 1;
    wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
    [[nodiscard]] bool valid() const { return texture != nullptr; }
};

// Uploads with a full mip chain (box filter on the CPU; sRGB data is filtered in linear space).
// Rgba32Float textures get no mips unless `mips` is true.
Result<GpuTexture> uploadTexture(Context& context, const scene::TextureData& data, bool mips = true);

// Solid 1x1 texture of the given 8-bit colour (used for missing material textures).
GpuTexture solidTexture(Context& context, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a,
                        bool srgb, const char* label);

std::uint32_t mipLevelCount(std::uint32_t width, std::uint32_t height);
wgpu::TextureFormat toWgpuFormat(scene::TextureFormat format);

class SamplerCache {
public:
    explicit SamplerCache(Context& context) : context_(context) {}
    // Trilinear or nearest with the given wraps.
    const wgpu::Sampler& get(scene::WrapMode wrapU, scene::WrapMode wrapV, bool linear);
    const wgpu::Sampler& get(const scene::TextureRef& ref) { return get(ref.wrapU, ref.wrapV, ref.linearFilter); }

private:
    Context& context_;
    std::unordered_map<std::uint32_t, wgpu::Sampler> samplers_;
};

} // namespace avgen::gpu
