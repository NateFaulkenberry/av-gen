#pragma once

// OpenEXR output and input through tinyexr (ADR-020 follow-up; research offline-rendering.md §7):
// scene-linear float RGBA, ZIP-compressed, stored as half (the renderer's HDR precision) or float.
// EXR is the scene-referred interchange for compositing and grading; PNG stays display-referred.

#include "core/error.hpp"
#include "scene/scene.hpp"

#include <cstdint>
#include <filesystem>
#include <span>

namespace avgen::assets {

// `rgba` must hold width * height * 4 floats (row-major, top-left origin). `half` stores 16-bit
// channels (exact for values that came from an RGBA16F target); false stores 32-bit floats.
Result<void> writeExr(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height,
                      std::span<const float> rgba, bool half = true);

// Reads a single-part RGB(A) EXR as Rgba32Float (missing alpha reads as 1). For tests and tools.
Result<scene::TextureData> readExr(const std::filesystem::path& path);

} // namespace avgen::assets
