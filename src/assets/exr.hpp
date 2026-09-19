#pragma once

// OpenEXR output and input through tinyexr (ADR-020 follow-up; research offline-rendering.md §7):
// scene-linear float RGBA, ZIP-compressed, stored as half (the renderer's HDR precision) or float.
// EXR is the scene-referred interchange for compositing and grading; PNG stays display-referred.

#include "core/error.hpp"
#include "scene/scene.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace avgen::assets {

// `rgba` must hold width * height * 4 floats (row-major, top-left origin). `half` stores 16-bit
// channels (exact for values that came from an RGBA16F target); false stores 32-bit floats.
Result<void> writeExr(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height,
                      std::span<const float> rgba, bool half = true);

// Reads a single-part RGB(A) EXR as Rgba32Float (missing alpha reads as 1). For tests and tools.
Result<scene::TextureData> readExr(const std::filesystem::path& path);

// ---- multi-channel EXR with arbitrary named layers (ADR-348) -------------------------------------
//
// `writeExr` above writes exactly R/G/B/A, which is right for a beauty pass and wrong for an AOV.
// Spec section 33 is explicit: normals and motion must NOT be stored as R/G/B, because a colour
// managed pipeline downstream will treat those names as colour and transform them. They need named
// layers -- `normal.X`, `normal.Y`, `normal.Z` -- which are then unambiguously not colour.
//
// This is new engine code, not a new dependency: the pinned tinyexr header has always exposed
// `EXRHeader`, `EXRChannelInfo` and `SaveEXRImageToFile`. It also discharges the debt recorded at
// `src/app/render_settings.hpp` -- "the layered form is a better file and a bigger change; it is
// recorded as not done rather than half-built".
struct ExrChannel {
    std::string name;             // e.g. "R", "normal.X", "depth.Z"
    std::span<const float> data;  // width * height values, row-major, top-left
    bool half = false;            // per-channel precision, which `writeExr` cannot express
};

// Channels are written in the order given; OpenEXR itself sorts them alphabetically on read, which
// is why the names matter more than the order. Fails if any channel is the wrong length or if two
// channels share a name.
Result<void> writeExrLayers(const std::filesystem::path& path, std::uint32_t width,
                            std::uint32_t height, std::span<const ExrChannel> channels);

} // namespace avgen::assets
