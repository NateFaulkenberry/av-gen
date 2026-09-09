#pragma once

// Video output for offline rendering (milestone 1.0, ADR-020). Frames are RGBA8, top-left origin,
// tightly packed (width * height * 4 bytes), appended in order at a fixed frame rate.
//
// Backends:
//   - "native": macOS AVFoundation (AVAssetWriter) — ProRes 4444 / ProRes 422 / H.264 / HEVC
//     into .mov or .mp4 (ProRes requires .mov). No third-party dependency.
//   - "ffmpeg": a user-supplied `ffmpeg` executable (never shipped, never linked; research
//     §8.1): raw RGBA frames are piped to its stdin. Any encoder ffmpeg has.
// Audio: when `audio` names a WAV/FLAC/MP3 file the backend muxes it (native: via AVAssetReader
// for the sample range 0..frames/fps; ffmpeg: `-i <audio> -shortest`).

#include "core/error.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace avgen::assets {

struct VideoSettings {
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    double fps = 60.0;
    // Codec id: "prores4444", "prores422", "h264", "hevc" (native backend), or any ffmpeg
    // encoder name when backend == "ffmpeg" (e.g. "libx264", "prores_ks", "libvpx-vp9").
    std::string codec = "h264";
    std::string backend = "auto";   // "auto" (native when available, else ffmpeg), "native", "ffmpeg"
    int quality = 80;               // 0..100, mapped to the backend's quality/CRF scale (lossy codecs)
    std::filesystem::path audio;    // optional: audio file to mux
    double audioOffsetSeconds = 0.0; // where in the audio file frame 0 lies
    std::filesystem::path ffmpegPath; // empty = search PATH
};

class VideoWriter {
public:
    virtual ~VideoWriter() = default;
    // Appends one frame (exactly width * height * 4 bytes). Errors are sticky: after one, every
    // later call fails and finish() reports it.
    [[nodiscard]] virtual Result<void> writeFrame(std::span<const std::uint8_t> rgba) = 0;
    // Flushes and closes the file (muxing audio). Must be called once; the destructor without
    // finish() abandons the file.
    [[nodiscard]] virtual Result<void> finish() = 0;
    [[nodiscard]] virtual std::size_t framesWritten() const = 0;
    [[nodiscard]] virtual const std::filesystem::path& path() const = 0;
    [[nodiscard]] virtual std::string backendName() const = 0;
};

// Opens a writer for `file` (extension decides the container: .mov, .mp4, .mkv, .webm …).
[[nodiscard]] Result<std::unique_ptr<VideoWriter>> openVideoWriter(const std::filesystem::path& file,
                                                                   const VideoSettings& settings);

// Capabilities, for the UI and the CLI help.
[[nodiscard]] bool hasNativeVideo();                        // AVFoundation available (macOS)
[[nodiscard]] std::vector<std::string> nativeCodecs();      // empty when !hasNativeVideo()
[[nodiscard]] std::filesystem::path findFfmpeg(const std::filesystem::path& hint = {}); // empty = not found
// Human-readable summary, e.g. "native: prores4444, prores422, h264, hevc; ffmpeg: /opt/homebrew/bin/ffmpeg".
[[nodiscard]] std::string describeVideoBackends();

} // namespace avgen::assets

// ---- probe (tests/UI) -------------------------------------------------------------------------
// Reads a finished file back through the native backend (AVFoundation). Fails where
// hasNativeVideo() is false. `frames` is exact: the video track's samples are counted without
// decoding, so probing a long file costs a pass over its index.

namespace avgen::assets {

struct VideoInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t frames = 0;
    double durationSeconds = 0.0;
    bool hasAudio = false;
};

[[nodiscard]] Result<VideoInfo> probeVideo(const std::filesystem::path& file);

} // namespace avgen::assets
