#pragma once

// Phase 2 of the Quality Lab: reading what a render wrote (docs/quality-lab/architecture.md §5).
//
// The Lab does not render. `avgen --project ... --render ... --aov ...` renders, and it already
// writes the determinism proof every full-reference comparison depends on. This layer is the other
// half of the filesystem coupling ADR-250 chose deliberately: it finds a sequence on disk, reads
// one frame of it, and reads the auxiliary views that sit beside that frame.
//
// **Everything here is a reader.** There is no code path in the Quality Lab that produces a frame,
// which is the property that makes a finding about the renderer a finding about the renderer.

#include "metrics/spatial.hpp"

#include "core/error.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::quality {

// A scene-linear auxiliary view: the AOV EXRs `--aov` writes, and the intermediate planes the
// temporal detectors build. Always four channels, because `writeExr` writes RGBA and
// `RenderJob` expands every single-channel target into it (ADR-242 §2).
//
// The channel meanings, per AOV, taken from `RenderJob`'s export rather than assumed:
//   normal    rgb = unit normal (oct-DECODED on the way out, ADR-242 §3), a = roughness.
//             (0,0,0,0) exactly where no geometry wrote -- which is a sky matte, not a normal.
//   emission  rgb = pre-bloom, pre-tonemap emitted radiance.
//   depth     all channels = linear depth, in world units.
//   velocity  r,g = screen motion in UV units; b,a replicate. `screenVelocityAt` computes
//             `nowUv - beforeUv`, so the PREVIOUS position of a pixel is `uv - velocity`.
//   id        all channels = the R32Uint identifier, widened to float. Exact to 2^24.
struct Plane {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> rgba;

    [[nodiscard]] bool valid() const {
        return width > 0 && height > 0 &&
               rgba.size() == static_cast<std::size_t>(width) * height * 4;
    }
    [[nodiscard]] const float* at(std::uint32_t x, std::uint32_t y) const {
        return rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
    }
    [[nodiscard]] float* at(std::uint32_t x, std::uint32_t y) {
        return rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
    }
};

// ---- single files ------------------------------------------------------------------------------

// A beauty frame. PNG, display-referred sRGB, which is the correct input for every metric in
// metrics.md §2 -- all of MS-SSIM, CIEDE2000, PSNR-HVS, CAMBI and VMAF are *defined* on
// display-referred content, so this is the right file and not a fallback (ADR-251 §1).
[[nodiscard]] Result<Frame> readFrame(const std::filesystem::path& path);

// An AOV. EXR, scene-linear.
[[nodiscard]] Result<Plane> readPlane(const std::filesystem::path& path);

// PNG out, for the diagnostics.
[[nodiscard]] Result<void> writeFrame(const std::filesystem::path& path, const Frame& frame);

// ---- sequences ---------------------------------------------------------------------------------

// A render's output directory, read as an ordered sequence.
//
// The order is the numeric order of the digit run in each filename, NOT lexicographic: a sequence
// that rolls from `frame_000009` to `frame_000010` sorts correctly either way, but one whose
// pattern lost its zero padding does not, and a temporal metric fed a mis-ordered sequence reports
// alternation that is an artifact of the sort.
struct Sequence {
    std::filesystem::path directory;
    std::vector<std::filesystem::path> frames;
    [[nodiscard]] std::size_t size() const { return frames.size(); }
    [[nodiscard]] bool empty() const { return frames.empty(); }
};

// Finds the beauty frames in `directory` -- files whose extension is `extension` and whose stem
// ends in digits, ordered by that number. AOV files are excluded: they carry a second extension
// (`frame_000123.normal.exr`) and would otherwise interleave with the beauty pass.
[[nodiscard]] Result<Sequence> discoverSequence(const std::filesystem::path& directory,
                                                std::string_view extension = ".png");

// The AOV file that sits beside `frame`, following `RenderSettings::aovFile`: the frame's pattern
// with `.<aov>.exr` in place of its extension.
[[nodiscard]] std::filesystem::path aovPathFor(const std::filesystem::path& frame,
                                               std::string_view aov);

// True when that file exists. The Lab degrades rather than fails when an AOV was not exported
// (ADR-250 §2), and this is the question every degradation asks.
[[nodiscard]] bool hasAov(const std::filesystem::path& frame, std::string_view aov);

} // namespace avgen::quality
