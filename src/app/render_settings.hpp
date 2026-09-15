#pragma once

// Offline render settings (milestone 1.0, ADR-020): what to render, at which size and rate,
// over which time range, and where. Stored in the project under "render" (optional) and
// overridable from the CLI. GPU-free; the RenderJob executes it.

#include "core/error.hpp"
#include "scene/detail_limits.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

namespace avgen::app {

// PngSequence: display-referred 8-bit PNGs after tone mapping. ExrSequence: scene-linear half
// EXRs from the HDR target before tone mapping (compositing/grading). Video: see VideoWriter.
enum class RenderOutput : std::uint8_t { PngSequence, Video, ExrSequence };
[[nodiscard]] const char* renderOutputName(RenderOutput output); // "sequence" | "video" | "exr"
[[nodiscard]] bool isSequence(RenderOutput output);

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
    int encoderThreads = 0;            // 0 = hardware threads - 1, clamped to [1, 16]
    // ADR-147 / §5.9: a batch render is the deliverable, so it renders at the offline tier unless
    // told otherwise. Before this existed `RenderJob` never called `setQuality` at all and a batch
    // frame came out byte-identical to an interactive Realtime one -- the offline promise was
    // stated in the tier table and not kept by the path that produces the actual output.
    std::string tier = "offline";      // preview | realtime | high | offline
    // Phases switched off for this render, comma-separated, in `--disable`'s vocabulary. Empty is
    // the ordinary deliverable and is what every real render uses.
    //
    // It is here for the same reason `tier` is (ADR-147): the offline engine builds its own
    // renderer, so a toggle set on the interactive one never reached the path that produces the
    // output. `--disable water` on a `--render` came out byte-for-byte identical to the baseline --
    // an attribution arm that cannot fail, which is worse than no arm at all, because a null result
    // from it reads as "this subsystem is innocent".
    //
    // Not a deliverable feature: a render with a pass switched off is a diagnostic, and the job
    // says so in the log rather than letting a disabled frame be mistaken for a finished one.
    std::string disablePasses;
    // Quality arms applied to this render, comma-separated, in `--quality-arm`'s vocabulary. Same
    // reason and same defect as `disablePasses`: it was applied to the interactive renderer only,
    // so a quality arm on a `--render` was a third flag that validated and then did nothing.
    std::string qualityArms;
    // ADR-186: which distance-based detail reductions this render is under. "tier" (the default)
    // takes the tier's answer -- offline lifts them all, every other tier keeps live playback's;
    // "live" keeps them whatever the tier, which is what a quick proof render wants; "unlimited"
    // lifts them at any tier.
    //
    // Not a diagnostic like the two fields above it: this is a property of the deliverable, and a
    // render with it lifted is the *better* picture rather than a broken one. The far field draws
    // real geometry instead of billboards, distant characters are posed every frame instead of at
    // 20 Hz, and nothing past 120 m stands frozen.
    // How many times the output resolution the scene is rendered at, before being resolved back down
    // (ADR-212). 1 is off and is the default, so nothing changes for a render that does not ask.
    //
    // The renderer has supported this since ADR-137 -- `QualitySettings::renderScale` clamps to
    // [0.25, 2.0] and the resolve is already written -- but nothing could reach the top half of that
    // range: `--canvas-scale` refuses anything above 1, the settings slider stops at 1, and the
    // Offline tier pins `renderScale` to 1 under a comment about taking "no resolution shortcut".
    // That comment is about not going *down*. Going up is not a shortcut, it is spending more, which
    // is the one thing an offline render is for.
    //
    // Why it matters, measured on Glowmere through `--project`: neighbour-to-neighbour chroma noise
    // is 2.80% at 1280x720, 2.19% at 1920x1080 and 1.86% at 2560x1440. The artifact is undersampling
    // of sub-pixel foliage, it gets monotonically worse as the output shrinks, and a 720p deliverable
    // had no way to buy its way out of it.
    float supersample = 1.0f;

    std::string limits = "tier";

    // Frame count for a resolved end time (endSeconds >= startSeconds); the last frame is the one
    // whose time is < end (end exclusive), at least 1.
    [[nodiscard]] std::uint64_t frameCount(double resolvedEndSeconds) const;
    [[nodiscard]] double resolvedEnd(double audioSeconds, double timelineSeconds) const;
    // Output file for frame `index` of a sequence.
    [[nodiscard]] std::filesystem::path frameFile(const std::filesystem::path& dir, std::uint64_t index) const;
    // Infers the output kind from a path's extension, keeping `fallback` when the extension says
    // nothing about it.
    //
    // A path is weak evidence and the setting is strong evidence. This used to answer PngSequence
    // for everything that was not a video -- for a directory, for an `.exr`, for the `.json` a save
    // dialog had appended -- so choosing an output file silently moved the radio button back to PNG
    // under someone who had just pressed Video.
    static RenderOutput outputForPath(const std::filesystem::path& path,
                                      RenderOutput fallback = RenderOutput::PngSequence);
    // The path a video would actually be written to: the same name with a container extension the
    // muxer understands, so a name typed without one (or with somebody else's) still names a movie.
    static std::filesystem::path withVideoExtension(const std::filesystem::path& path);
    // The default frame pattern of a sequence kind ("frame_{:06d}.png" / ".exr").
    static const char* defaultPattern(RenderOutput output);
    // Swaps a pattern that is still the other sequence kind's default for this kind's default, so
    // switching PNG <-> EXR does not leave ".png" names on EXR files.
    void normalisePattern();

    // The detail limits this render runs under, resolved against its own tier. The one place the
    // three words mean anything, so the job, the UI and a test cannot disagree about what "tier"
    // resolves to.
    [[nodiscard]] scene::DetailLimits resolvedLimits() const;

    [[nodiscard]] Result<void> validate() const; // sizes > 0 and even for video, fps > 0, quality range, pattern has {}
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<RenderSettings> fromJson(const nlohmann::json& j); // missing fields keep defaults
};

} // namespace avgen::app
