#pragma once

// Synchronous GPU->CPU texture readback for tests, captures and the classic renderToImage path.
// Not for the real-time hot path (it blocks); the offline render job uses gpu::ReadbackRing.

#include "core/error.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <filesystem>
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

// Reads `size` bytes of a buffer created with CopySrc usage (blocking). For tests and tools.
Result<std::vector<std::uint8_t>> readBuffer(Context& context, const wgpu::Buffer& buffer, std::uint64_t offset,
                                             std::uint64_t size);

// 64-bit FNV-1a over the pixel data; used for same-GPU determinism checks.
std::uint64_t hashImage(const Image8& image);
std::uint64_t hashImage(const ImageF& image); // over the float bit patterns, one step per value

// Writes an uncompressed PPM (P6). Enough for debugging; PNG arrives with stb_image_write in 0.2.
Result<void> writePpm(const Image8& image, const std::filesystem::path& path);

} // namespace avgen::gpu
