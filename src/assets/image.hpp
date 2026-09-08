#pragma once

// Image decode/encode through stb_image / stb_image_write (ADR-005). 8-bit files decode to
// RGBA8 (tagged sRGB or linear by the caller); Radiance .hdr decodes to RGBA32Float.

#include "core/error.hpp"
#include "scene/scene.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace avgen::assets {

// srgb applies to 8-bit sources only (colour textures true, data textures false).
Result<scene::TextureData> loadImage(const std::filesystem::path& path, bool srgb);
Result<scene::TextureData> loadImageFromMemory(std::span<const std::uint8_t> bytes, bool srgb, std::string name);

// PNG, 8-bit RGBA. `rgba` must hold width * height * 4 bytes.
Result<void> writePng(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height,
                      std::span<const std::uint8_t> rgba);
Result<std::vector<std::uint8_t>> encodePng(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba);

// Radiance HDR, float RGBA (alpha ignored). `rgba` must hold width * height * 4 floats.
Result<void> writeHdr(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height,
                      std::span<const float> rgba);

// Convenience accessors for TextureData.
std::span<const float> floatPixels(const scene::TextureData& texture); // empty unless Rgba32Float

} // namespace avgen::assets
