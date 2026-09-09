#pragma once

// Offline render settings (milestone 1.0, ADR-020): what to render, at which size and rate,
// over which time range, and where. Stored in the project under "render" (optional) and
// overridable from the CLI. GPU-free; the RenderJob executes it.

#include "core/error.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

namespace avgen::app {

enum class RenderOutput : std::uint8_t { PngSequence, Video };
[[nodiscard]] const char* renderOutputName(RenderOutput output);

struct RenderSettings {
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    double fps = 60.0;
    double startSeconds = 0.0;
    double endSeconds = -1.0;          // < 0: the audio duration, else the timeline duration, else 10 s
    RenderOutput output = RenderOutput::PngSequence;
    std::filesystem::path outputPath;  // directory (sequence) or file (video); relative to the project
    std::string pattern = "frame_{:06d}.png"; // fmt pattern with the frame index (sequence)
    std::string codec = "h264";        // video codec id (see assets/video_writer.hpp)
    std::string backend = "auto";      // "auto" | "native" | "ffmpeg"
    int quality = 80;                  // 0..100
    bool muxAudio = true;              // include the project's audio in the video
    int encoderThreads = 0;            // 0 = hardware threads - 1, clamped to [1, 8]

    // Frame count for a resolved end time (endSeconds >= startSeconds); the last frame is the one
    // whose time is < end (end exclusive), at least 1.
    [[nodiscard]] std::uint64_t frameCount(double resolvedEndSeconds) const;
    [[nodiscard]] double resolvedEnd(double audioSeconds, double timelineSeconds) const;
    // Output file for frame `index` of a sequence.
    [[nodiscard]] std::filesystem::path frameFile(const std::filesystem::path& dir, std::uint64_t index) const;
    // Infers the output kind from the path: a known video extension → Video, else PngSequence.
    static RenderOutput outputForPath(const std::filesystem::path& path);

    [[nodiscard]] Result<void> validate() const; // sizes > 0 and even for video, fps > 0, quality range, pattern has {}
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<RenderSettings> fromJson(const nlohmann::json& j); // missing fields keep defaults
};

} // namespace avgen::app
