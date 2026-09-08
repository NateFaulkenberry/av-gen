#pragma once

// Synchronous GPU->CPU texture readback for tests and offline output. Not for the real-time
// hot path (it blocks); the offline renderer will use a ring of staging buffers instead.

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

// Reads an 8-bit-per-channel 4-component texture (RGBA8Unorm or BGRA8Unorm, swizzled to RGBA).
Result<Image8> readTexture8(Context& context, const wgpu::Texture& texture, std::uint32_t width,
                            std::uint32_t height, bool bgra);

// 64-bit FNV-1a over the pixel data; used for same-GPU determinism checks.
std::uint64_t hashImage(const Image8& image);

// Writes an uncompressed PPM (P6). Enough for debugging; PNG arrives with stb_image_write in 0.2.
Result<void> writePpm(const Image8& image, const std::filesystem::path& path);

} // namespace avgen::gpu
