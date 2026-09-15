#pragma once

// Synchronous GPU->CPU texture readback for tests, captures and the classic renderToImage path.
// Not for the real-time hot path (it blocks); the offline render job uses gpu::ReadbackRing.

#include "core/error.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace avgen::gpu {

class Context;

struct Image8 {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba; // width * height * 4, row-major, top-left origin
    [[nodiscard]] const std::uint8_t* pixel(std::uint32_t x, std::uint32_t y) const {
        return rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
    }
};

// Scene-linear float image (RGBA32F on the CPU side, read back from an RGBA16F target).
struct ImageF {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> rgba; // width * height * 4, row-major, top-left origin
    [[nodiscard]] const float* pixel(std::uint32_t x, std::uint32_t y) const {
        return rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
    }
};

// Reads an 8-bit-per-channel 4-component texture (RGBA8Unorm or BGRA8Unorm, swizzled to RGBA).
Result<Image8> readTexture8(Context& context, const wgpu::Texture& texture, std::uint32_t width,
                            std::uint32_t height, bool bgra);

// Reads an RGBA16Float texture, converting every half to float exactly.
Result<ImageF> readTextureF16(Context& context, const wgpu::Texture& texture, std::uint32_t width,
                              std::uint32_t height);

// Reads an R32Uint texture (the identifier target, ADR-035) as one value per texel.
Result<std::vector<std::uint32_t>> readTextureR32Uint(Context& context, const wgpu::Texture& texture,
                                                      std::uint32_t width, std::uint32_t height);

// Reads a single texel of an R32Uint or R32Float texture. This is what picking uses: a click needs
// four bytes, and reading the whole identifier target to get them is five megabytes and a stall.
//
// Each of these is a whole GPU round trip -- a copy, a submit, a `MapAsync` and a blocking wait --
// so two of them cost twice what one does however few bytes each moves. Prefer `readTexelsR32`
// wherever more than one texel is wanted at the same moment.
Result<std::uint32_t> readTexelR32Uint(Context& context, const wgpu::Texture& texture,
                                       std::uint32_t x, std::uint32_t y);
Result<float> readTexelR32Float(Context& context, const wgpu::Texture& texture, std::uint32_t x,
                                std::uint32_t y);

// One texel of one texture, for `readTexelsR32`. The texture is held by pointer because the batch
// deliberately spans *several* textures -- picking wants the identifier target and the linear-depth
// target at the same pixel, which is the whole reason this exists.
struct TexelRequest {
    const wgpu::Texture* texture = nullptr;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
};

// Reads every requested texel in **one** submission and **one** map, returning their raw 32 bits in
// request order (`std::bit_cast` or memcpy to float where the texture is R32Float).
//
// Why this is worth having rather than a loop over the singular versions: the cost of a readback on
// this backend is almost entirely the round trip, not the bytes. `viewport_pick` read the depth,
// waited for the GPU, then read the identifier and waited again -- so every click on the viewport
// paid two full CPU-blocking stalls on the main thread to move eight bytes. It is item 4 of
// docs/application-performance.md section 14 ("coalesce the viewport pick's readbacks into one
// submission"), and it is on the interaction a person performs most often.
//
// Each texel gets its own 256-byte-aligned slice of one staging buffer, because that is WebGPU's
// minimum offset for a texture-to-buffer copy. Eight bytes of payload in two kilobytes of buffer is
// not a saving worth making; one wait instead of two is.
Result<std::vector<std::uint32_t>> readTexelsR32(Context& context,
                                                 std::span<const TexelRequest> requests);

// Reads `size` bytes of a buffer created with CopySrc usage (blocking). For tests and tools.
Result<std::vector<std::uint8_t>> readBuffer(Context& context, const wgpu::Buffer& buffer, std::uint64_t offset,
                                             std::uint64_t size);

// 64-bit FNV-1a over the pixel data; used for same-GPU determinism checks.
std::uint64_t hashImage(const Image8& image);
std::uint64_t hashImage(const ImageF& image); // over the float bit patterns, one step per value

// Writes an uncompressed PPM (P6). Enough for debugging; PNG arrives with stb_image_write in 0.2.
Result<void> writePpm(const Image8& image, const std::filesystem::path& path);

} // namespace avgen::gpu
